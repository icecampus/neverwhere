#include "../../pch.h"

#include "engine.h"

#include "builtins.h"
#include "pgg/eval.h"
#include "pgg/pgg.h"
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

class Engine {
public:
    Engine(const Document& doc, const RunParams& params, RunResult& result)
        : doc_(doc), params_(params), result_(result) {
        run_.diagnostics = &result_.diagnostics;
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
                    if (!b->targets.names.empty()) topLevel_[b->targets.names[0]] = item;
                    break;
                }
                default:
                    break;  // zones (rejected statically), tap (no-op at E1)
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
    std::unordered_map<std::string, const Node*> topLevel_;
    std::unordered_map<std::string, const Node*> outputDecl_;
    std::vector<std::string> outputNames_;
    std::unordered_map<std::string, TypedValue> env_;

    TypedValue resolveIdent(const std::string& name, Span span) { return evalBinding(name, span); }

    TypedValue evalBinding(const std::string& name, Span span) {
        if (auto it = env_.find(name); it != env_.end()) return it->second;
        auto it = topLevel_.find(name);
        if (it == topLevel_.end()) {
            run_.report("E103", span, "'" + name + "' is not defined");
            return {};
        }
        const Node* node = it->second;
        TypedValue tv;
        if (node->kind == NodeKind::ParamDecl) {
            tv = bindParam(static_cast<const ParamDecl*>(node));
        } else if (node->kind == NodeKind::Binding) {
            const auto* b = static_cast<const Binding*>(node);
            tv = compileExpr(b->value, run_, [this](const std::string& n, Span s) { return resolveIdent(n, s); });
        } else {
            run_.report("E201", span, "'" + name + "' is not evaluable at stage E1");
            return {};
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
