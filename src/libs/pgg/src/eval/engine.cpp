#include "../../pch.h"

#include "engine.h"

#include <filesystem>

#include "builtins.h"
#include "cache.h"
#include "expand.h"
#include "fingerprint.h"
#include "modules.h"
#include "parallel.h"
#include "pgg/eval.h"
#include "pgg/pgg.h"
#include "profile.h"
#include "typecheck.h"

namespace pgg {

bool RunResult::hasErrors() const {
    for (const Diagnostic& d : diagnostics)
        if (!d.isWarning) return true;
    return false;
}

namespace {

bool hasErrors(const std::vector<Diagnostic>& diags) {
    for (const Diagnostic& d : diags)
        if (!d.isWarning) return true;
    return false;
}

// Cross-run cache stores value payloads only (spec §5.3): geometry, scalars,
// strings and lists of those. rng and field bindings recompile for pennies
// and never enter the cache.
bool cacheableValue(const Value& v) {
    switch (v.data.index()) {
        case 1:   // bool
        case 2:   // int
        case 3:   // f32
        case 4:   // vec2
        case 5:   // vec3
        case 6:   // vec4
        case 7:   // string
        case 9:   // geo
        case 12:  // sdf (self-contained payload: the displace field DAG is owned)
            return true;
        case 11: {  // list
            for (const Value& e : asList(v))
                if (!cacheableValue(e)) return false;
            return true;
        }
        default: return false;  // none / rng / field
    }
}

bool cacheable(const TypedValue& tv) { return tv && !tv.field && cacheableValue(tv.value); }

class Engine {
public:
    Engine(const FlatProgram& flat, const RunParams& params, RunResult& result,
           const std::vector<size_t>& runtimeContracts)
        : flat_(flat), params_(params), result_(result),
          fps_(*flat_.file, params_.values, numericProfileId()) {
        run_.diagnostics = &result_.diagnostics;
        run_.threads = resolveThreadCount(params_.threads);
        for (const Node* item : flat_.file->items) {
            switch (item->kind) {
                case NodeKind::ParamDecl:
                    topLevel_[static_cast<const ParamDecl*>(item)->name] = item;
                    break;
                case NodeKind::OutputDecl: {
                    const auto* o = static_cast<const OutputDecl*>(item);
                    outputNames_.push_back(o->name);
                    outputDecl_[o->name] = item;
                    break;
                }
                case NodeKind::Binding: {
                    const auto* b = static_cast<const Binding*>(item);
                    for (const std::string& n : b->targets.names) topLevel_[n] = item;
                    break;
                }
                default:
                    break;  // zones (rejected statically), tap (no-op while debug=off)
            }
        }
        // Runtime half of the def contracts (§7.4): grouped per instance;
        // instance outputs are the pulls that trigger them.
        for (const size_t idx : runtimeContracts) {
            const FlatContract& c = flat_.contracts[idx];
            instanceContracts_[c.instance].push_back(idx);
        }
        for (size_t i = 0; i < flat_.instances.size(); ++i)
            for (const std::string& o : flat_.instances[i].outputs) outputInstance_[o] = i;
        contractState_.assign(flat_.instances.size(), 0);
    }

    void run(const std::vector<std::string>& requested) {
        for (const auto& [name, v] : params_.values) {
            auto it = topLevel_.find(name);
            if (it == topLevel_.end() || it->second->kind != NodeKind::ParamDecl)
                run_.report("E204", Span{}, "launch parameter '" + name + "' is not declared in the file");
        }
        const std::vector<std::string> want = requested.empty() ? outputNames_ : requested;
        for (const std::string& w : want) {
            if (outputDecl_.find(w) == outputDecl_.end())
                run_.report("E204", Span{}, "requested output '" + w + "' is not declared in the file");
        }
        if (hasErrors(result_.diagnostics)) return;
        for (const std::string& w : want) {
            const Node* decl = outputDecl_[w];
            TypedValue tv = evalBinding(w, decl ? decl->span : Span{});
            if (!tv || tv.field) continue;  // field/rng outputs were rejected statically
            result_.outputs.push_back({w, tv.value});
        }
        result_.stats.fieldsEvaluated = run_.fieldsEvaluated;
        result_.stats.cacheHits = cacheHits_;
        result_.stats.cacheMisses = cacheMisses_;
        result_.stats.threadsUsed = run_.threads;
        result_.stats.profileId = numericProfileId();
        for (const auto& [name, tv] : env_) {
            if (!tv.field) continue;
            auto it = run_.nodeEvals.find(tv.field->id);
            result_.stats.bindingFieldEvals[name] = it == run_.nodeEvals.end() ? 0 : it->second;
        }
    }

private:
    static constexpr size_t kNoInstance = ~size_t(0);

    const FlatProgram& flat_;
    const RunParams& params_;
    RunResult& result_;
    RunContext run_;
    BindingFingerprinter fps_;
    std::unordered_map<std::string, const Node*> topLevel_;
    std::unordered_map<std::string, const Node*> outputDecl_;
    std::vector<std::string> outputNames_;
    std::unordered_map<std::string, TypedValue> env_;
    std::unordered_map<size_t, std::vector<size_t>> instanceContracts_;  // instance -> contract indices
    std::unordered_map<std::string, size_t> outputInstance_;             // output binding -> instance
    std::vector<uint8_t> contractState_;  // 0 = untouched, 1 = expects done, 2 = done
    uint64_t cacheHits_ = 0;
    uint64_t cacheMisses_ = 0;

    TypedValue resolveIdent(const std::string& name, Span span) { return evalBinding(name, span); }

    TypedValue evalBinding(const std::string& name, Span span) {
        if (auto it = env_.find(name); it != env_.end()) return it->second;
        // Instance outputs trigger the def contracts once per run (§7.4):
        // expects before the first pull, ensures after it.
        size_t inst = kNoInstance;
        if (auto oi = outputInstance_.find(name); oi != outputInstance_.end()) inst = oi->second;
        if (inst != kNoInstance && contractState_[inst] == 0) {
            contractState_[inst] = 1;
            runContracts(inst, /*ensures=*/false);
        }
        auto it = topLevel_.find(name);
        if (it == topLevel_.end()) {
            run_.report("E103", span, "'" + name + "' is not defined");
            return {};
        }
        const Node* node = it->second;
        if (node->kind == NodeKind::ParamDecl) {
            TypedValue tv = bindParam(static_cast<const ParamDecl*>(node));
            env_.emplace(name, tv);
            return tv;
        }
        if (node->kind != NodeKind::Binding) {
            run_.report("E201", span, "'" + name + "' is not evaluable at stage E5");
            return {};
        }
        const auto* b = static_cast<const Binding*>(node);
        // Cross-run content-addressed cache (N3/N4): structural fingerprint of
        // the binding (0 = uncacheable); a hit short-circuits evaluation.
        const uint64_t fp = params_.cache ? fps_.fingerprint(name) : 0;
        TypedValue tv;
        bool hit = false;
        if (fp != 0) {
            if (params_.cache->lookup(fp, tv)) {
                ++cacheHits_;
                hit = true;
            } else {
                ++cacheMisses_;
            }
        }
        if (!hit) {
            const size_t diagBefore = result_.diagnostics.size();
            tv = compileExpr(b->value, run_, [this](const std::string& n, Span s) { return resolveIdent(n, s); });
            // Runtime diagnostics raised inside an instance carry the instance
            // path (basic §9.5 chain; nested frames append outward).
            if (auto bi = flat_.instanceOfBinding.find(name); bi != flat_.instanceOfBinding.end()) {
                for (size_t i = diagBefore; i < result_.diagnostics.size(); ++i)
                    result_.diagnostics[i].message += " [instance " + flat_.instances[bi->second].path + "]";
            }
            if (fp != 0 && cacheable(tv)) params_.cache->store(fp, tv);
        }
        TypedValue installed = install(name, b, tv);
        if (inst != kNoInstance && contractState_[inst] == 1) {
            contractState_[inst] = 2;
            runContracts(inst, /*ensures=*/true);
        }
        return installed;
    }

    // Memoizes a binding result in env_ and fans a multi-output tuple out to
    // its destructuring targets. Shared by the evaluated and the cache-hit
    // paths so both behave identically downstream.
    TypedValue install(const std::string& name, const Binding* b, const TypedValue& tv) {
        if (b->targets.names.size() > 1) {
            // Multi-output destructure: the tuple (a list Value) fans out
            // to the targets by position; arity was checked statically.
            if (tv && isListValue(tv.value)) {
                const std::vector<Value>& elems = asList(tv.value);
                for (size_t i = 0; i < b->targets.names.size(); ++i) {
                    const Value elem = i < elems.size() ? elems[i] : Value();
                    TypedValue et{Type{valueBase(elem), false, GeoKind::Any}, nullptr, elem};
                    env_.emplace(b->targets.names[i], et);
                }
            }
            auto eit = env_.find(name);
            return eit != env_.end() ? eit->second : TypedValue{};
        }
        env_.emplace(name, tv);
        return tv;
    }

    void runContracts(size_t inst, bool ensures) {
        auto it = instanceContracts_.find(inst);
        if (it == instanceContracts_.end()) return;
        for (const size_t idx : it->second) {
            const FlatContract& c = flat_.contracts[idx];
            if (c.isEnsure != ensures) continue;
            if (c.attrForm) {
                checkAttrContract(c);
                continue;
            }
            const Value v = compileExprToValue(
                c.cond, run_, [this](const std::string& n, Span s) { return resolveIdent(n, s); });
            if (valueBase(v) == ScalarType::Bool && !asBool(v)) reportContract(c);
        }
    }

    void checkAttrContract(const FlatContract& c) {
        const TypedValue tv = evalBinding(c.ident, c.span);
        if (!tv) return;  // the binding's own error was already reported
        bool present = false;
        if (!tv.field && valueBase(tv.value) == ScalarType::Geo) {
            const Geo& g = *asGeo(tv.value);
            if (c.attrName == "P") {
                present = g.positions != nullptr;
            } else if (c.attrName == "index") {
                present = true;
            } else if (c.attrName == "N") {
                present = g.normals != nullptr || attrExistsOnAnyDomain(g, "N");
            } else {
                present = attrExistsOnAnyDomain(g, c.attrName);
            }
        }
        if (!present) reportContract(c);
    }

    void reportContract(const FlatContract& c) {
        const FlatInstance& inst = flat_.instances[c.instance];
        std::string msg = c.hasMessage ? c.message
                                       : (c.isEnsure ? "ensure of '" + inst.defName + "' failed"
                                                     : "expect of '" + inst.defName + "' failed");
        run_.report(c.isEnsure ? "E304" : "E303", c.span, msg + " [instance " + inst.path + "]");
    }

    TypedValue bindParam(const ParamDecl* p) {
        const Type declT = typeFromRef(*p->type);
        for (const auto& [name, v] : params_.values) {
            if (name != p->name) continue;
            const Value cv = valueBase(v) == declT.base ? v : convertValue(v, declT.base);
            if (isNone(cv) && !isNone(v)) {
                run_.report("E204", p->span,
                            "launch parameter '" + name + "' expects " + typeName(declT) + ", got " +
                                scalarName(valueBase(v)));
                return {};
            }
            return TypedValue{declT, nullptr, cv};
        }
        if (p->hasDefault) {
            const Value v = compileExprToValue(
                p->def, run_, [this](const std::string& n, Span s) { return resolveIdent(n, s); });
            const Value cv = valueBase(v) == declT.base ? v : convertValue(v, declT.base);
            return TypedValue{declT, nullptr, cv};
        }
        run_.report("E604", p->span, "param '" + p->name + "' was not bound at launch");
        return {};
    }
};

}  // namespace

RunResult run(const Document& doc, const RunParams& params, const std::vector<std::string>& outputs) {
    RunResult result;
    result.diagnostics = doc.diagnostics;  // E0 parse/lint findings first
    if (doc.hasErrors() || !doc.file) return result;

    // E5 pipeline: import closure (only when imports exist), then full static
    // expansion of def calls into a flat graph (pass-through otherwise).
    ModuleClosure closure;
    const ModuleClosure* closurePtr = nullptr;
    if (hasImports(*doc.file)) {
        closure = loadModuleClosure(*doc.file, params.importRoots, result.diagnostics);
        closurePtr = &closure;
    }
    FlatProgram flat = expandProgram(*doc.file, closurePtr, result.diagnostics);
    if (hasErrors(result.diagnostics)) return result;

    std::vector<std::string> boundNames;
    for (const auto& [name, v] : params.values) boundNames.push_back(name);
    std::vector<size_t> runtimeContracts;
    typecheckFlat(flat, boundNames, result.diagnostics, runtimeContracts);
    if (hasErrors(result.diagnostics)) return result;

    Engine eng(flat, params, result, runtimeContracts);
    eng.run(outputs);
    return result;
}

RunResult run(const std::string& text, const RunParams& params, const std::vector<std::string>& outputs,
              const std::string& fileName) {
    return run(parse(text, fileName), params, outputs);
}

RunResult runFile(const std::string& path, const RunParams& params, const std::vector<std::string>& outputs) {
    RunParams p = params;
    // The importing file's own directory is an implicit import root (§7.6).
    const std::string dir = std::filesystem::path(path).parent_path().string();
    if (!dir.empty()) p.importRoots.push_back(dir);
    return run(parseFile(path), p, outputs);
}

}  // namespace pgg
