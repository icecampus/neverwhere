#include "../../pch.h"

#include "engine.h"

#include <filesystem>
#include <unordered_set>

#include "builtins.h"
#include "cache.h"
#include "expand.h"
#include "fingerprint.h"
#include "fracture.h"
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

// --- E6 probe plumbing (spec §9) ------------------------------------------------

// One pull root of a resolved probe: a flat binding plus an optional
// attr/group terminal on its (geo) value.
struct ProbeTarget {
    std::string recordPath;  // path of the printed record (instance path or as written)
    std::string binding;     // flat binding to pull
    std::string terminal;    // attr/group name ("" = the binding value itself)
};

struct ResolvedProbe {
    std::string origin;      // "probe" | "tap"
    std::string specPath;    // path as written (record path of aggregated probes)
    std::string inspector;   // schema|stats|coverage|table
    int limit = 8;
    bool aggregate = false;
    std::vector<ProbeTarget> targets;
};

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
                case NodeKind::RepeatZone: {
                    const auto* z = static_cast<const RepeatZone*>(item);
                    for (const std::string& n : z->targets.names) topLevel_[n] = item;
                    break;
                }
                case NodeKind::ForeachZone:
                    topLevel_[static_cast<const ForeachZone*>(item)->target] = item;
                    break;
                default:
                    break;  // tap (no-op while debug=off), imports/defs (expanded)
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

        // E6 phase 1 (spec §9): parse and resolve probe specs BEFORE any
        // pull — a typo in a probe must abort the run, not pass silently.
        std::vector<ResolvedProbe> probes;
        for (const std::string& text : params_.probes) {
            ProbeSpec spec;
            std::string err;
            if (!parseProbeSpec(text, spec, err)) {
                run_.report("E606", Span{}, "probe '" + text + "': " + err);
                continue;
            }
            resolveSpec("probe", spec, probes);
        }
        if (params_.debug) collectTaps(probes);
        // Value pulls (RunParams::pulls) resolve with the probe-path rules
        // minus the attr terminal: a binding, an instance path or
        // `<ipath>.<local>`; an index-less def name is rejected (one pull =
        // one record set with a definite target).
        std::vector<ProbeTarget> pullTargets;
        for (const std::string& path : params_.pulls) resolvePullPath(path, pullTargets);
        if (hasErrors(result_.diagnostics)) return;

        // Output suppression (§9.2): CLI probes (and value pulls) without
        // explicitly requested outputs skip the declared outputs (probe-only
        // run); taps never suppress (debug mode is a normal run plus
        // observation).
        const std::vector<std::string> pull =
            !requested.empty() ? requested
            : (params_.probes.empty() && params_.pulls.empty()) ? outputNames_
                                                                : std::vector<std::string>{};
        for (const std::string& w : pull) {
            const Node* decl = outputDecl_[w];
            TypedValue tv = evalBinding(w, decl ? decl->span : Span{});
            if (!tv || tv.field) continue;  // field/rng outputs were rejected statically
            result_.outputs.push_back({w, tv.value});
        }

        // E6 phase 2: pull the probe targets (shared run environment — a
        // target already computed for an output is not recomputed) and
        // evaluate the inspectors over the values.
        for (const ResolvedProbe& rp : probes) executeProbe(rp);

        // Value pulls share the environment too: a target already computed
        // for an output/probe is not recomputed.
        for (const ProbeTarget& t : pullTargets) {
            const TypedValue tv = evalBinding(t.binding, Span{});
            if (!tv) continue;  // the binding's own error was already reported
            if (tv.field) {
                run_.report("E606", Span{}, "pull target '" + t.recordPath + "' is a field, not a value");
                continue;
            }
            result_.pulled.push_back({t.recordPath, tv.value});
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

    // --- E6 probes (spec §9) ----------------------------------------------------

    bool isValueBinding(const std::string& name) const {
        auto it = topLevel_.find(name);
        if (it == topLevel_.end()) return false;
        const NodeKind k = it->second->kind;
        // Zone targets are pullable values (E7): probes work on zone outputs.
        return k == NodeKind::Binding || k == NodeKind::ParamDecl || k == NodeKind::RepeatZone ||
               k == NodeKind::ForeachZone;
    }

    const FlatInstance* findInstanceByPath(const std::string& path) const {
        for (const FlatInstance& inst : flat_.instances)
            if (inst.path == path) return &inst;
        return nullptr;
    }

    // Instances of a def in expansion order (= k ascending per def; the
    // deterministic "sorted by instance path" order for multi-instance probes).
    std::vector<size_t> instancesOfDef(const std::string& defName) const {
        std::vector<size_t> out;
        for (size_t i = 0; i < flat_.instances.size(); ++i)
            if (flat_.instances[i].defName == defName) out.push_back(i);
        return out;
    }

    // One target per instance output; a multi-output instance suffixes the
    // record path with the output's local name.
    void targetsForInstance(const FlatInstance& inst, const std::string& displayPath,
                            const std::string& terminal, std::vector<ProbeTarget>& out) const {
        for (const std::string& o : inst.outputs) {
            ProbeTarget t;
            t.recordPath = displayPath;
            if (inst.outputs.size() > 1) t.recordPath += "." + o.substr(o.rfind('.') + 1);
            t.binding = o;
            t.terminal = terminal;
            out.push_back(std::move(t));
        }
    }

    // Resolves a probe/tap path to pull targets (longest-prefix: first a
    // binding, then an instance path, then an index-less def name, then a
    // geo-binding + attr/group terminal with the single-output sugar).
    // false = E606 already reported.
    bool resolveProbePath(const std::string& path, ResolvedProbe& probe) {
        // 1. Exact flat binding (`rock`, `make_rock[1].out`, `make_rock[1].raw`).
        if (isValueBinding(path)) {
            probe.targets.push_back({path, path, ""});
            return true;
        }
        // 2. Exact instance path -> the instance's output(s).
        if (const FlatInstance* inst = findInstanceByPath(path)) {
            targetsForInstance(*inst, path, "", probe.targets);
            return true;
        }
        // 3. Index-less def name -> every instance of the def.
        if (std::vector<size_t> idxs = instancesOfDef(path); !idxs.empty()) {
            for (const size_t i : idxs)
                targetsForInstance(flat_.instances[i], flat_.instances[i].path, "", probe.targets);
            return true;
        }
        // 4. Terminal attr/group: split the last dot.
        const size_t dot = path.rfind('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 >= path.size()) {
            run_.report("E606", Span{},
                        "probe target '" + path + "' not found (no such binding, instance or def)");
            return false;
        }
        const std::string prefix = path.substr(0, dot);
        const std::string term = path.substr(dot + 1);
        // 4a. Instance-path prefix: first the instance-local binding
        //     (`<ipath>.raw` -> flat `<iname>.raw`), then the single-output
        //     sugar (`make_rock[1].disp` -> the only output + attr `disp`).
        if (const FlatInstance* inst = findInstanceByPath(prefix)) {
            const std::string flatLocal = inst->name + "." + term;
            if (isValueBinding(flatLocal)) {
                probe.targets.push_back({path, flatLocal, ""});
                return true;
            }
            if (inst->outputs.size() == 1) {
                probe.targets.push_back({path, inst->outputs[0], term});
                return true;
            }
            run_.report("E606", Span{},
                        "probe target '" + path + "' is ambiguous: instance '" + prefix + "' has " +
                            std::to_string(inst->outputs.size()) + " outputs; probe one explicitly");
            return false;
        }
        // 4b. Geo binding + terminal (`rock.flat_tops`, `make_rock[1].out.disp`).
        if (isValueBinding(prefix)) {
            probe.targets.push_back({path, prefix, term});
            return true;
        }
        // 4c. Index-less def + terminal: per instance, the local binding
        //     first, then the single-output sugar (mirrors 4a).
        if (std::vector<size_t> idxs = instancesOfDef(prefix); !idxs.empty()) {
            for (const size_t i : idxs) {
                const FlatInstance& inst = flat_.instances[i];
                const std::string flatLocal = inst.name + "." + term;
                if (isValueBinding(flatLocal)) {
                    probe.targets.push_back({inst.path + "." + term, flatLocal, ""});
                    continue;
                }
                if (inst.outputs.size() != 1) {
                    run_.report("E606", Span{},
                                "probe target '" + path + "' is ambiguous: instance '" + inst.path +
                                    "' has " + std::to_string(inst.outputs.size()) +
                                    " outputs; probe one explicitly");
                    return false;
                }
                probe.targets.push_back({inst.path + "." + term, inst.outputs[0], term});
            }
            return true;
        }
        run_.report("E606", Span{},
                    "probe target '" + path + "' not found (no such binding, instance or def)");
        return false;
    }

    // Resolves a value-pull path (RunParams::pulls): exact binding, exact
    // instance path (every output), or `<ipath>.<local>`. No attr terminal
    // and no index-less def (E606 — a pull must name definite values).
    void resolvePullPath(const std::string& path, std::vector<ProbeTarget>& out) {
        if (isValueBinding(path)) {
            out.push_back({path, path, ""});
            return;
        }
        if (const FlatInstance* inst = findInstanceByPath(path)) {
            targetsForInstance(*inst, path, "", out);
            return;
        }
        const size_t dot = path.rfind('.');
        if (dot != std::string::npos && dot > 0 && dot + 1 < path.size()) {
            const std::string prefix = path.substr(0, dot);
            const std::string term = path.substr(dot + 1);
            if (const FlatInstance* inst = findInstanceByPath(prefix)) {
                const std::string flatLocal = inst->name + "." + term;
                if (isValueBinding(flatLocal)) {
                    out.push_back({path, flatLocal, ""});
                    return;
                }
            }
        }
        run_.report("E606", Span{},
                    "pull target '" + path + "' not found (a pull names a binding, an instance or <instance>.<local>)");
    }

    // Parses/validates one spec and appends resolved probes (a spec without
    // an inspector expands to schema+stats, the tap default §9.3).
    void resolveSpec(const std::string& origin, const ProbeSpec& spec, std::vector<ResolvedProbe>& out) {
        if (spec.aggregate && spec.inspector == "table") {
            run_.report("E606", Span{},
                        "probe '" + spec.path + "': aggregate=stats is not supported for the table inspector");
            return;
        }
        if (spec.hasLimit && spec.inspector != "table") {
            run_.report("E606", Span{}, "probe '" + spec.path + "': limit applies to the table inspector");
            return;
        }
        std::vector<std::string> inspectors;
        if (spec.inspector.empty()) {
            inspectors = {"schema", "stats"};
        } else {
            inspectors = {spec.inspector};
        }
        for (const std::string& insp : inspectors) {
            ResolvedProbe rp;
            rp.origin = origin;
            rp.specPath = spec.path;
            rp.inspector = insp;
            rp.limit = spec.limit;
            rp.aggregate = spec.aggregate;
            if (resolveProbePath(spec.path, rp)) out.push_back(std::move(rp));
        }
    }

    void addTap(const std::string& label, bool hasLabel, const std::string& path, Span span,
                std::vector<ResolvedProbe>& out) {
        ProbeSpec spec;
        spec.path = path;
        if (hasLabel) {
            if (label != "schema" && label != "stats" && label != "coverage" && label != "table") {
                run_.report("E606", span,
                            "unknown tap inspector '" + label + "' (schema|stats|coverage|table)");
                return;
            }
            spec.inspector = label;
        }
        resolveSpec("tap", spec, out);
    }

    // debug=on: top-level taps in file order, then def-body instance taps in
    // expansion order (a tap inside a def fires on every instance, §9.4).
    void collectTaps(std::vector<ResolvedProbe>& out) {
        for (const Node* item : flat_.file->items) {
            if (item->kind != NodeKind::Tap) continue;
            const auto* t = static_cast<const Tap*>(item);
            addTap(t->label, t->hasLabel, pathText(t->path), t->span, out);
        }
        for (const FlatTap& ft : flat_.taps) addTap(ft.label, ft.hasLabel, ft.path, Span{}, out);
    }

    // Phase 2: pull the targets and evaluate the inspector. Per-target
    // problems are E606 but do not stop the remaining probes; the run fails
    // on the collected diagnostics at the end.
    void executeProbe(const ResolvedProbe& rp) {
        struct Pulled {
            const ProbeTarget* target;
            Value value;
        };
        std::vector<Pulled> pulled;
        for (const ProbeTarget& t : rp.targets) {
            const TypedValue tv = evalBinding(t.binding, Span{});
            if (!tv) continue;  // the binding's own error was already reported
            if (tv.field) {
                run_.report("E606", Span{}, "probe target '" + t.recordPath + "' is a field, not a value");
                continue;
            }
            if (!t.terminal.empty() && valueBase(tv.value) != ScalarType::Geo) {
                run_.report("E606", Span{},
                            "probe target '" + t.recordPath + "': a ." + t.terminal +
                                " terminal needs a geo value");
                continue;
            }
            pulled.push_back({&t, tv.value});
        }
        if (pulled.empty()) return;

        if (rp.inspector == "schema") {
            if (rp.aggregate) {
                std::vector<std::string> texts;
                for (const Pulled& p : pulled) texts.push_back(probeSchema(p.value));
                result_.probes.push_back({rp.origin, rp.specPath, rp.inspector, probeAggregateSchema(texts)});
            } else {
                for (const Pulled& p : pulled)
                    result_.probes.push_back({rp.origin, p.target->recordPath, rp.inspector, probeSchema(p.value)});
            }
            return;
        }

        if (rp.inspector == "stats") {
            std::vector<std::vector<ProbeStatsEntry>> perTarget;
            std::vector<const Pulled*> ok;
            for (const Pulled& p : pulled) {
                std::vector<ProbeStatsEntry> entries;
                std::string err;
                const bool isGeo = valueBase(p.value) == ScalarType::Geo;
                const bool valid = isGeo ? probeGeoStats(*asGeo(p.value), p.target->terminal, entries, err)
                                         : probeValueStats(p.value, entries, err);
                if (!valid) {
                    run_.report("E606", Span{}, "probe target '" + p.target->recordPath + "': " + err);
                    continue;
                }
                perTarget.push_back(std::move(entries));
                ok.push_back(&p);
            }
            if (ok.empty()) return;
            if (rp.aggregate) {
                // Targets without numeric point attributes carry no stats
                // labels; when that is every target, fall back to collapsed
                // counts lines so the record still says something.
                size_t withEntries = 0;
                for (const auto& e : perTarget) withEntries += e.empty() ? 0 : 1;
                std::string text;
                if (withEntries == 0) {
                    std::vector<std::string> texts;
                    for (const Pulled* p : ok)
                        texts.push_back(valueBase(p->value) == ScalarType::Geo
                                            ? probeGeoSummary(*asGeo(p->value))
                                            : probeSchema(p->value));
                    text = probeAggregateSchema(texts);
                } else {
                    text = probeAggregateStats(perTarget);
                }
                result_.probes.push_back({rp.origin, rp.specPath, rp.inspector, std::move(text)});
            } else {
                for (size_t i = 0; i < ok.size(); ++i) {
                    std::string text;
                    if (perTarget[i].empty()) {
                        text = probeGeoSummary(*asGeo(ok[i]->value));  // counts line (no numeric attrs)
                    } else {
                        for (const ProbeStatsEntry& e : perTarget[i])
                            text += (text.empty() ? "" : "\n") + formatProbeStats(e);
                    }
                    result_.probes.push_back({rp.origin, ok[i]->target->recordPath, rp.inspector, std::move(text)});
                }
            }
            return;
        }

        if (rp.inspector == "coverage") {
            std::vector<ProbeCoverage> perTarget;
            std::vector<const Pulled*> ok;
            for (const Pulled& p : pulled) {
                if (valueBase(p.value) != ScalarType::Geo) {
                    run_.report("E606", Span{},
                                "probe target '" + p.target->recordPath + "': coverage needs a geo value");
                    continue;
                }
                ProbeCoverage cov;
                std::string err;
                if (!probeGeoCoverage(*asGeo(p.value), p.target->terminal, cov, err)) {
                    run_.report("E606", Span{}, "probe target '" + p.target->recordPath + "': " + err);
                    continue;
                }
                perTarget.push_back(cov);
                ok.push_back(&p);
            }
            if (ok.empty()) return;
            if (rp.aggregate) {
                result_.probes.push_back({rp.origin, rp.specPath, rp.inspector, probeAggregateCoverage(perTarget)});
            } else {
                for (size_t i = 0; i < ok.size(); ++i)
                    result_.probes.push_back(
                        {rp.origin, ok[i]->target->recordPath, rp.inspector, formatProbeCoverage(perTarget[i])});
            }
            return;
        }

        // table
        for (const Pulled& p : pulled) {
            if (valueBase(p.value) != ScalarType::Geo) {
                run_.report("E606", Span{}, "probe target '" + p.target->recordPath + "': table needs a geo value");
                continue;
            }
            result_.probes.push_back(
                {rp.origin, p.target->recordPath, rp.inspector, probeGeoTable(*asGeo(p.value), rp.limit)});
        }
    }

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
        if (node->kind == NodeKind::RepeatZone || node->kind == NodeKind::ForeachZone) {
            // Zones are runtime constructs of the engine (E7, §5.4): evaluating
            // a zone installs ALL its targets, so sibling targets memoize too.
            // Zone fingerprints are 0 (v0): the cross-run cache skips them.
            const IdentResolver topResolve = [this](const std::string& n, Span s) { return resolveIdent(n, s); };
            if (node->kind == NodeKind::RepeatZone) {
                evalRepeatZone(static_cast<const RepeatZone*>(node), env_, topResolve, run_);
            } else {
                evalForeachZone(static_cast<const ForeachZone*>(node), env_, topResolve, run_);
            }
            auto eit = env_.find(name);
            return eit != env_.end() ? eit->second : TypedValue{};
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
        runContractsWith(inst, ensures, [this](const std::string& n, Span s) { return resolveIdent(n, s); }, run_);
    }

    // Contracts of an instance evaluated against an explicit resolver/run:
    // the top-level path passes resolveIdent/run_, a zone body passes its
    // local resolver and (for foreach pieces) its own RunContext.
    void runContractsWith(size_t inst, bool ensures, const IdentResolver& resolve, RunContext& run) {
        auto it = instanceContracts_.find(inst);
        if (it == instanceContracts_.end()) return;
        for (const size_t idx : it->second) {
            const FlatContract& c = flat_.contracts[idx];
            if (c.isEnsure != ensures) continue;
            if (c.attrForm) {
                checkAttrContract(c, resolve, run);
                continue;
            }
            const Value v = compileExprToValue(c.cond, run, resolve);
            if (valueBase(v) == ScalarType::Bool && !asBool(v)) reportContract(c, run);
        }
    }

    // --- zones (E7, spec §5.4) ---------------------------------------------------
    //
    // A zone is a runtime construct: the iteration count (repeat) and the
    // piece count (foreach) are only known at run time, so there is no static
    // unrolling — the engine executes the zone when one of its targets is
    // pulled, installing every target into the environment (laziness N2: a
    // zone whose targets are never pulled never runs).
    //
    // The body executes as a sub-program in a local scope: statements run in
    // text order; name resolution looks at the local environment first, then
    // falls through to the outer resolver. At the start of an iteration/piece
    // the state/item ports hold the input value; the body's single binding of
    // a port overwrites the entry (reads before it see the input port, reads
    // after see the output port — the validator's scope model). Taps inside a
    // zone body are ignored at v0 (§19 v1.5).

    using EnvMap = std::unordered_map<std::string, TypedValue>;

    void execBody(const std::vector<Stmt*>& body, EnvMap& localEnv, const IdentResolver& outerResolve,
                  RunContext& run) {
        const IdentResolver resolve = [&](const std::string& n, Span s) -> TypedValue {
            if (auto it = localEnv.find(n); it != localEnv.end()) return it->second;
            return outerResolve(n, s);
        };
        // Def instances expanded into the body (v1.20) run their contracts on
        // every iteration/piece: expects before the first output binding of
        // the instance, ensures after it (mirror of evalBinding's §7.4 order).
        std::unordered_set<size_t> contractsStarted;
        for (const Stmt* s : body) {
            if (!s) continue;
            switch (s->kind) {
                case NodeKind::Binding: {
                    const auto* b = static_cast<const Binding*>(s);
                    size_t inst = kNoInstance;
                    if (b->targets.names.size() == 1)
                        if (auto oi = outputInstance_.find(b->targets.names[0]); oi != outputInstance_.end())
                            inst = oi->second;
                    const bool first = inst != kNoInstance && contractsStarted.insert(inst).second;
                    if (first) runContractsWith(inst, /*ensures=*/false, resolve, run);
                    TypedValue tv = compileExpr(b->value, run, resolve);
                    installLocal(b, tv, localEnv);
                    if (first) runContractsWith(inst, /*ensures=*/true, resolve, run);
                    break;
                }
                case NodeKind::RepeatZone: {
                    const auto* z = static_cast<const RepeatZone*>(s);
                    evalRepeatZone(z, localEnv, resolve, run);
                    break;
                }
                case NodeKind::ForeachZone: {
                    const auto* z = static_cast<const ForeachZone*>(s);
                    evalForeachZone(z, localEnv, resolve, run);
                    break;
                }
                default:
                    break;  // tap inside a zone body: ignored at v0
            }
        }
    }

    // install() for a local zone environment: the state/item port rebind must
    // overwrite the seeded input entry, so this assigns rather than emplaces.
    void installLocal(const Binding* b, const TypedValue& tv, EnvMap& env) {
        if (b->targets.names.size() > 1) {
            if (tv && isListValue(tv.value)) {
                const std::vector<Value>& elems = asList(tv.value);
                for (size_t i = 0; i < b->targets.names.size(); ++i) {
                    const Value elem = i < elems.size() ? elems[i] : Value();
                    env[b->targets.names[i]] = TypedValue{Type{valueBase(elem), false, GeoKind::Any}, nullptr, elem};
                }
            }
            return;
        }
        env[b->targets.names[0]] = tv;
    }

    void evalRepeatZone(const RepeatZone* z, EnvMap& env, const IdentResolver& outerResolve,
                        RunContext& run) {
        const size_t nState = z->state.names.size();
        std::vector<TypedValue> ports(nState);
        const TypedValue input = compileExpr(z->value, run, outerResolve);
        if (nState == 1) {
            ports[0] = input;
        } else if (input && isListValue(input.value)) {
            // Multi-state: the value is a multi-output tuple destructured
            // across the ports (arity checked statically).
            const std::vector<Value>& elems = asList(input.value);
            for (size_t i = 0; i < nState; ++i) {
                const Value elem = i < elems.size() ? elems[i] : Value();
                ports[i] = TypedValue{Type{valueBase(elem), false, GeoKind::Any}, nullptr, elem};
            }
        }
        int64_t iterations = numericValueInt(compileExprToValue(z->iterations, run, outerResolve));
        if (iterations < 0) {
            run.report("E607", z->span,
                       "repeat iterations must be >= 0, got " + std::to_string(iterations),
                       "a negative count behaves as 0 (the output is the input)");
            iterations = 0;
        }
        for (int64_t it = 0; it < iterations; ++it) {
            run.zoneConstants.emplace_back("iteration", Value(it));
            EnvMap localEnv;
            for (size_t i = 0; i < nState; ++i) localEnv.emplace(z->state.names[i], ports[i]);
            execBody(z->body, localEnv, outerResolve, run);
            for (size_t i = 0; i < nState; ++i)
                if (auto it2 = localEnv.find(z->state.names[i]); it2 != localEnv.end())
                    ports[i] = it2->second;  // an unbound port (static E204) keeps its input
            run.zoneConstants.pop_back();
        }
        for (size_t i = 0; i < z->targets.names.size(); ++i)
            env[z->targets.names[i]] = i < nState ? ports[i] : TypedValue{};
    }

    void evalForeachZone(const ForeachZone* z, EnvMap& env, const IdentResolver& outerResolve,
                         RunContext& run) {
        const TypedValue coll = compileExpr(z->collection, run, outerResolve);
        GeoPtr collGeo;
        if (coll && !coll.field && valueBase(coll.value) == ScalarType::Geo) collGeo = asGeo(coll.value);
        if (!collGeo || collGeo->kind == GeoKind::Instances) {
            run.report("E607", z->span, "foreach expects a geo<mesh> or geo<points> collection",
                       "mesh: connected pieces; points: one piece per point (§5.4); realize() instances first");
            env[z->target] = TypedValue{};
            return;
        }
        // mesh -> connected pieces; points -> one piece per point (row model).
        const std::vector<GeoPtr> pieces =
            collGeo->kind == GeoKind::Mesh ? splitMeshPieces(*collGeo) : splitPointPieces(*collGeo);
        const Type kItem{ScalarType::Geo, false, collGeo->kind};
        // Free-name pre-evaluation (N1): every name the body reads but does
        // not define is force-evaluated sequentially here, so during the
        // parallel piece loop the outer environment is only read.
        preEvaluateFreeNames(z->body, z->item, outerResolve);

        struct PieceOut {
            TypedValue value;
            std::vector<Diagnostic> diagnostics;
            uint64_t fieldsEvaluated = 0;
        };
        std::vector<PieceOut> outs(pieces.size());
        parallelForPieces(pieces.size(), run.threads, [&](size_t begin, size_t end) {
            for (size_t pi = begin; pi < end; ++pi) {
                // Every piece runs with its own RunContext: own field arena,
                // memo cache, diagnostics and counters — no shared mutable
                // state between pieces (bit-identical at any lane count).
                RunContext pieceRun;
                pieceRun.diagnostics = &outs[pi].diagnostics;
                pieceRun.threads = run.threads;
                pieceRun.zoneConstants.emplace_back("piece_index", Value(static_cast<int64_t>(pi)));
                EnvMap localEnv;
                localEnv.emplace(z->item, TypedValue{kItem, nullptr, Value(pieces[pi])});
                execBody(z->body, localEnv, outerResolve, pieceRun);
                outs[pi].value = localEnv.count(z->item) ? localEnv[z->item] : TypedValue{};
                outs[pi].fieldsEvaluated = pieceRun.fieldsEvaluated;
            }
        });
        // Join in piece order: diagnostics, counters, then the rigid merge.
        // Every body result must be a mesh or points geometry and all results
        // must share one kind (the merged kind); anything else is E607.
        std::vector<GeoPtr> outPieces;
        outPieces.reserve(pieces.size());
        GeoKind outKind = GeoKind::Any;
        for (size_t pi = 0; pi < pieces.size(); ++pi) {
            for (const Diagnostic& d : outs[pi].diagnostics) result_.diagnostics.push_back(d);
            run.fieldsEvaluated += outs[pi].fieldsEvaluated;
            const TypedValue& tv = outs[pi].value;
            if (tv && !tv.field && valueBase(tv.value) == ScalarType::Geo &&
                asGeo(tv.value)->kind != GeoKind::Instances &&
                (outKind == GeoKind::Any || asGeo(tv.value)->kind == outKind)) {
                outKind = asGeo(tv.value)->kind;
                outPieces.push_back(asGeo(tv.value));
                continue;
            }
            run.report("E607", z->span,
                       "foreach body did not produce a geo<mesh>/geo<points> piece of one kind",
                       "the item port must end as the same geometry kind on every piece (realize() instances)");
            outPieces.push_back(pieces[pi]);  // defensive: pass the piece through
        }
        const GeoPtr merged = mergeMeshPieces(outPieces);
        env[z->target] = TypedValue{Type{ScalarType::Geo, false, merged->kind}, nullptr, Value(merged)};
    }

    // Names defined by the body itself (bindings, nested zone targets and
    // ports): these resolve locally during the piece loop.
    static void collectDefined(const std::vector<Stmt*>& body, std::unordered_set<std::string>& out) {
        for (const Stmt* s : body) {
            if (!s) continue;
            switch (s->kind) {
                case NodeKind::Binding:
                    for (const std::string& n : static_cast<const Binding*>(s)->targets.names) out.insert(n);
                    break;
                case NodeKind::RepeatZone: {
                    const auto* z = static_cast<const RepeatZone*>(s);
                    for (const std::string& n : z->targets.names) out.insert(n);
                    for (const std::string& n : z->state.names) out.insert(n);
                    collectDefined(z->body, out);
                    break;
                }
                case NodeKind::ForeachZone: {
                    const auto* z = static_cast<const ForeachZone*>(s);
                    out.insert(z->target);
                    out.insert(z->item);
                    collectDefined(z->body, out);
                    break;
                }
                default:
                    break;
            }
        }
    }

    void scanExprFreeNames(const Expr* e, const std::unordered_set<std::string>& defined,
                           std::unordered_set<std::string>& seen, const IdentResolver& resolve) {
        if (!e) return;
        switch (e->kind) {
            case NodeKind::Ident: {
                const std::string& n = static_cast<const Ident*>(e)->name;
                if (!defined.count(n) && seen.insert(n).second) resolve(n, e->span);
                return;
            }
            case NodeKind::Paren:
                scanExprFreeNames(static_cast<const Paren*>(e)->inner, defined, seen, resolve);
                return;
            case NodeKind::Unary:
                scanExprFreeNames(static_cast<const Unary*>(e)->operand, defined, seen, resolve);
                return;
            case NodeKind::Binary: {
                const auto* b = static_cast<const Binary*>(e);
                scanExprFreeNames(b->lhs, defined, seen, resolve);
                scanExprFreeNames(b->rhs, defined, seen, resolve);
                return;
            }
            case NodeKind::Ternary: {
                const auto* t = static_cast<const Ternary*>(e);
                scanExprFreeNames(t->cond, defined, seen, resolve);
                scanExprFreeNames(t->thenExpr, defined, seen, resolve);
                scanExprFreeNames(t->elseExpr, defined, seen, resolve);
                return;
            }
            case NodeKind::VecLit:
                for (const Expr* el : static_cast<const VecLit*>(e)->elems)
                    scanExprFreeNames(el, defined, seen, resolve);
                return;
            case NodeKind::ListLit:
                for (const Expr* el : static_cast<const ListLit*>(e)->elems)
                    scanExprFreeNames(el, defined, seen, resolve);
                return;
            case NodeKind::Call: {
                const auto* c = static_cast<const Call*>(e);
                const BuiltinSig* sig = c->path.size() == 1 ? findBuiltin(c->path[0]) : nullptr;
                size_t posIdx = 0;
                for (const CallArg& a : c->args) {
                    // Mirror the compiler's enum rule: a bare ident naming an
                    // enum value of its parameter is a literal, never resolved.
                    const ParamSig* p = nullptr;
                    if (sig) {
                        if (a.hasName) {
                            for (const ParamSig& ps : sig->params)
                                if (ps.name == a.name) p = &ps;
                        } else if (posIdx < sig->params.size()) {
                            p = &sig->params[posIdx];
                        }
                    }
                    if (!a.hasName) ++posIdx;
                    if (p && !p->enumValues.empty() && a.value && a.value->kind == NodeKind::Ident &&
                        std::find(p->enumValues.begin(), p->enumValues.end(),
                                  static_cast<const Ident*>(a.value)->name) != p->enumValues.end())
                        continue;
                    scanExprFreeNames(a.value, defined, seen, resolve);
                }
                return;
            }
            default:
                return;  // literals, attr refs
        }
    }

    void scanFreeNames(const std::vector<Stmt*>& body, const std::unordered_set<std::string>& defined,
                       std::unordered_set<std::string>& seen, const IdentResolver& resolve) {
        for (const Stmt* s : body) {
            if (!s) continue;
            switch (s->kind) {
                case NodeKind::Binding:
                    scanExprFreeNames(static_cast<const Binding*>(s)->value, defined, seen, resolve);
                    break;
                case NodeKind::RepeatZone: {
                    const auto* z = static_cast<const RepeatZone*>(s);
                    scanExprFreeNames(z->value, defined, seen, resolve);
                    scanExprFreeNames(z->iterations, defined, seen, resolve);
                    scanFreeNames(z->body, defined, seen, resolve);
                    break;
                }
                case NodeKind::ForeachZone: {
                    const auto* z = static_cast<const ForeachZone*>(s);
                    scanExprFreeNames(z->collection, defined, seen, resolve);
                    scanFreeNames(z->body, defined, seen, resolve);
                    break;
                }
                default:
                    break;  // tap
            }
        }
    }

    void preEvaluateFreeNames(const std::vector<Stmt*>& body, const std::string& item,
                              const IdentResolver& resolve) {
        std::unordered_set<std::string> defined{item};
        collectDefined(body, defined);
        std::unordered_set<std::string> seen;
        scanFreeNames(body, defined, seen, resolve);
    }

    void checkAttrContract(const FlatContract& c, const IdentResolver& resolve, RunContext& run) {
        const TypedValue tv = resolve(c.ident, c.span);
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
        if (!present) reportContract(c, run);
    }

    void reportContract(const FlatContract& c, RunContext& run) {
        const FlatInstance& inst = flat_.instances[c.instance];
        std::string msg = c.hasMessage ? c.message
                                       : (c.isEnsure ? "ensure of '" + inst.defName + "' failed"
                                                     : "expect of '" + inst.defName + "' failed");
        run.report(c.isEnsure ? "E304" : "E303", c.span, msg + " [instance " + inst.path + "]");
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
