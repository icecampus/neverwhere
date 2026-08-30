#pragma once

// Debug inspectors (stage E6, spec §9): a probe is an extra lazy pull root in
// the engine, not a graph node — an inspector is a pure function over the
// already evaluated value of a binding and never affects the semantic layer.
//
// Text form (CLI/API contract): `path:inspector[param=value,...]` with both
// the `:inspector` and the `[params]` part optional; a spec without an
// inspector means schema+stats (the tap default, §9.3). Path resolution lives
// in the engine (it needs the FlatProgram metadata); this module owns the
// spec parser, the deterministic L0–L2 output formats (schema/stats/coverage/
// table) and the aggregate=stats merging (§9.4).
//
// Determinism rules (pinned in §19): attribute/group names are sorted, floats
// print with %g, the mean accumulates in f64 in @index order, percentiles are
// nearest-rank on a sorted copy, multi-instance matches are ordered by
// instance path (expansion order).

#include <string>
#include <vector>

#include "value.h"

namespace pgg {

struct ProbeSpec {
    std::string path;
    std::string inspector;  // "" = default (schema+stats); schema|stats|coverage|table
    int limit = 8;          // table row cap
    bool hasLimit = false;  // limit explicitly given (valid for table only)
    bool aggregate = false; // aggregate=stats: merge per-instance lines (§9.4)
};

// Parses the text form; false + err on a malformed spec (the caller reports
// E606). The trailing `[...]` part counts as params only when its content is
// a valid `name=value` list — otherwise it is instance syntax of the path
// (`make_rock[1]` ends with a bracket too).
bool parseProbeSpec(const std::string& text, ProbeSpec& out, std::string& err);

// One printed record per (target, inspector); `text` may span several lines
// (vec component lines, table rows).
struct ProbeRecord {
    std::string origin;     // "probe" (CLI/API) | "tap" (file mark, debug mode)
    std::string path;       // target path as written / instance path
    std::string inspector;  // schema|stats|coverage|table
    std::string text;
};

// --- L0: schema ---------------------------------------------------------------

// Kind summary without attrs/groups: `mesh 5122 pts, 10240 tri`,
// `points 146 pts`, `instances 15 anchors, 2 variants` (also the counts
// fallback line of stats on an attribute-less geometry).
std::string probeGeoSummary(const Geo& g);

// Full L0 line: kind summary + `; attrs: name(type, domain), ...` (sorted by
// name; domains pts/corners/faces/detail; @N listed, @P/@index implicit) +
// `; groups: name, ...` (deduped, sorted). Empty sections are omitted.
std::string probeGeoSchema(const Geo& g);

// L0 for any value: geo as above; `sdf nodes=7 bbox=(...)..(...)`;
// `f32 0.41`; `list[3] of geo<mesh>`; `<rng>`.
std::string probeSchema(const Value& v);

// --- L1: stats ----------------------------------------------------------------

struct ProbeStatsEntry {
    std::string label;   // attr name (+ ".x"/".y"/".z"/".w" per vec component)
    double mean = 0.0;   // f64 accumulation in @index order
    float p50 = 0.0f;    // nearest-rank on a sorted copy
    float p90 = 0.0f;
    float mn = 0.0f;
    float mx = 0.0f;
    size_t n = 0;
    std::string domain;  // pts|corners|faces|detail|value
};

// With a terminal: one entry per component of the named attribute (a group
// reads as 0/1). Without a terminal: every numeric point attribute (sorted,
// @N included) — the caller falls back to probeGeoSummary when the result is
// empty. false + err for a missing/non-numeric target (E606 input).
bool probeGeoStats(const Geo& g, const std::string& terminal,
                   std::vector<ProbeStatsEntry>& out, std::string& err);

// Numeric scalar/vector values give a single-element entry set
// (label "value"); anything else is an error.
bool probeValueStats(const Value& v, std::vector<ProbeStatsEntry>& out, std::string& err);

// `slope: mean 0.41, p50 0.38, p90 0.78, min 0.02, max 0.99 (5122 pts)`
std::string formatProbeStats(const ProbeStatsEntry& e);

// --- L1: coverage -------------------------------------------------------------

struct ProbeCoverage {
    std::string label;
    size_t t = 0;  // true count
    size_t n = 0;  // element count
};

// The terminal must resolve to a bool attribute or a group; false + err
// otherwise (E606 input).
bool probeGeoCoverage(const Geo& g, const std::string& terminal,
                      ProbeCoverage& out, std::string& err);

// `flat_tops: true 18.2% (934/5122)` — an all-false mask prints
// `true 0.0% (0/N)` (acceptance criterion: coverage=0% is diagnosable).
std::string formatProbeCoverage(const ProbeCoverage& c);

// --- L2: table ----------------------------------------------------------------

// Header `table[limit=L] (first K of N by @index)` + rows
// `i: @P=(x, y, z), name=value, ...` (cols = @P + point attributes, sorted).
std::string probeGeoTable(const Geo& g, int limit);

// --- aggregate=stats (§9.4) -----------------------------------------------------

// Per-instance entries merged into `<label>: mean M ± S across K instances`
// (M = mean of the per-instance means, S = their population std, f64).
std::string probeAggregateStats(const std::vector<std::vector<ProbeStatsEntry>>& perInstance);

// Pooled counts: `<label>: true P% (t/n) across K instances`.
std::string probeAggregateCoverage(const std::vector<ProbeCoverage>& perInstance);

// Identical schema lines collapse into one with ` x K instances` appended
// (first-appearance order); a singleton line prints as-is.
std::string probeAggregateSchema(const std::vector<std::string>& perInstance);

}  // namespace pgg
