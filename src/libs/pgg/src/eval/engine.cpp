#include "../../pch.h"

#include "engine.h"

#include "builtins.h"
#include "cache.h"
#include "fingerprint.h"
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
    Engine(const Document& doc, const RunParams& params, RunResult& result)
        : doc_(doc), params_(params), result_(result), fps_(*doc_.file, params_.values, numericProfileId()) {
        run_.diagnostics = &result_.diagnostics;
        run_.threads = resolveThreadCount(params_.threads);
        for (const Node* item : doc_.file->items) {
            switch (item->kind) {
                case NodeKind::ParamDecl:
                    topLevel_[static_cast<const ParamDecl*>(item)->name] = item;
                    break;
                case NodeKind::Def:
                    topLevel_[static_cast<const Def*>(item)->name] = item;
                    break;
                case NodeKind::Import: {
                    const auto* im = static_cast<const Import*>(item);
                    topLevel_[im->hasAlias ? im->alias : im->path.back()] = item;
                    break;
                }
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
                    break;  // zones (rejected statically), tap (no-op at E3)
            }
        }
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
    const Document& doc_;
    const RunParams& params_;
    RunResult& result_;
    RunContext run_;
    BindingFingerprinter fps_;
    std::unordered_map<std::string, const Node*> topLevel_;
    std::unordered_map<std::string, const Node*> outputDecl_;
    std::vector<std::string> outputNames_;
    std::unordered_map<std::string, TypedValue> env_;
    uint64_t cacheHits_ = 0;
    uint64_t cacheMisses_ = 0;

    TypedValue resolveIdent(const std::string& name, Span span) { return evalBinding(name, span); }

    TypedValue evalBinding(const std::string& name, Span span) {
        if (auto it = env_.find(name); it != env_.end()) return it->second;
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
            run_.report("E201", span, "'" + name + "' is not evaluable at stage E4");
            return {};
        }
        const auto* b = static_cast<const Binding*>(node);
        // Cross-run content-addressed cache (N3/N4): structural fingerprint of
        // the binding (0 = uncacheable); a hit short-circuits evaluation.
        const uint64_t fp = params_.cache ? fps_.fingerprint(name) : 0;
        if (fp != 0) {
            TypedValue cached;
            if (params_.cache->lookup(fp, cached)) {
                ++cacheHits_;
                return install(name, b, cached);
            }
            ++cacheMisses_;
        }
        TypedValue tv =
            compileExpr(b->value, run_, [this](const std::string& n, Span s) { return resolveIdent(n, s); });
        if (fp != 0 && cacheable(tv)) params_.cache->store(fp, tv);
        return install(name, b, tv);
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
    std::vector<std::string> boundNames;
    for (const auto& [name, v] : params.values) boundNames.push_back(name);
    typecheck(doc.file, boundNames, result.diagnostics);
    if (hasErrors(result.diagnostics)) return result;
    Engine eng(doc, params, result);
    eng.run(outputs);
    return result;
}

RunResult run(const std::string& text, const RunParams& params, const std::vector<std::string>& outputs,
              const std::string& fileName) {
    return run(parse(text, fileName), params, outputs);
}

RunResult runFile(const std::string& path, const RunParams& params, const std::vector<std::string>& outputs) {
    return run(parseFile(path), params, outputs);
}

}  // namespace pgg
