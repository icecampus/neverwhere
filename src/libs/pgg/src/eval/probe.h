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
// table, the field/mesh inspectors sample/slice/check of §9.6) and the
// aggregate=stats merging (§9.4).
//
// Param grammar (§9.6): pairs split on TOP-LEVEL commas (commas inside `()`
// do not split), vector values are parenthesised `(x,y,z)`, point lists use
// `;` inside one value (`at=(0,1,3.4);(1,0,0)`). The valid parameter names
// depend on the inspector; limit/aggregate stay typed fields, every other
// (inspector-specific) parameter is stored raw in `params` and parsed by the
// inspector function itself.
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
    std::string inspector;  // "" = default (schema+stats); schema|stats|coverage|table|sample|slice|check
    int limit = 8;          // table row cap / check index-list cap
    bool hasLimit = false;  // limit explicitly given (valid for table and check only)
    bool aggregate = false; // aggregate=stats: merge per-instance lines (§9.4)
    // Inspector-specific parameters in written order, values raw (vec/float
    // parsing happens inside the inspector functions); limit/aggregate are
    // NOT here — they are the typed fields above.
    std::vector<std::pair<std::string, std::string>> params;
};

// Parses the text form; false + err on a malformed spec (the caller reports
// E606). The trailing `[...]` part counts as params only when its content is
// a valid `name=value` list — otherwise it is instance syntax of the path
// (`make_rock[1]` ends with a bracket too).
bool parseProbeSpec(const std::string& text, ProbeSpec& out, std::string& err);

// One printed record per (target, inspector); `text` may span several lines
// (vec component lines, table rows, sample rows, the slice ASCII map).
struct ProbeRecord {
    std::string origin;     // "probe" (CLI/API) | "tap" (file mark, debug mode)
    std::string path;       // target path as written / instance path
    std::string inspector;  // schema|stats|coverage|table|sample|slice|check
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

// --- L2: sample (§9.6) ----------------------------------------------------------

// `path:sample[at=(x,y,z);(x2,y2,z2)]` or the profile form
// `path:sample[from=(x,y,z),to=(x,y,z),n=41]` (n >= 2, default 41, endpoints
// included). Header echoes the normalised params; rows are `x y z value`
// (%g). sdf target: the signed field value. geo<mesh>: pseudo-sign distance —
// BVH closest point, sign by the closest triangle's normal (the same oracle
// sdf_from_mesh uses), the header carries the note
// ` (pseudo-sign distance from mesh)`. geo<points>: unsigned distance to the
// nearest point, noted ` (unsigned distance from points)`.
bool probeSample(const Value& v, const std::vector<std::pair<std::string, std::string>>& params,
                 std::string& out, std::string& err);

// --- L2: slice (§9.6) -----------------------------------------------------------

// `path:slice[axis=x|y|z,at=<plane>,step=<cell>,bounds=(u0,v0,u1,v1)?,
// format=ascii|csv,iso=0.0]` — a grid over the plane perpendicular to `axis`.
// Plane coords (u,v): axis=x -> (y,z), axis=y -> (x,z), axis=z -> (x,y).
// Default bounds = the target's conservative bbox projection. ASCII (default):
// `#` where value <= iso, `.` otherwise, first row = v_max; width is capped
// at 120 columns by widening the step (noted in the header). csv: `i,j,value`
// rows (%g) in storage order (j ascending, then i). Header:
// `slice[<normalised params>] (W x H, bounds (u0, v0)..(u1, v1)[, notes])`.
// Values come from the same evaluators as sample (sdf field / mesh pseudo-sign
// distance / points unsigned distance).
bool probeSlice(const Value& v, const std::vector<std::pair<std::string, std::string>>& params,
                std::string& out, std::string& err);

// --- L2: check (§9.6) -----------------------------------------------------------

// `path:check[warn_aspect=20,limit=8]` on geo<mesh>: `key value` lines —
// degenerate (zero-area / repeated-index faces, `degenerate_faces` list when
// > 0), nonmanifold (edge incidence > 2), boundary (incidence 1; informative,
// never an issue), isolated (points in no face), nan (points with a
// non-finite @P/@N/@Cd component), components (face connectivity via shared
// points; informative), oriented_mismatch (components where some directed
// edge (a,b) appears 2+ times), edge_min/edge_median (%g, `-` when edgeless),
// needles (faces whose fan-triangle aspect longest^2/(2*area) exceeds
// warn_aspect) + needle_ratio/needles_faces when > 0. Last line: `ok` or
// `issues N` (N = degenerate+nonmanifold+isolated+nan+oriented_mismatch+
// needles; boundary and components are informative only).
bool probeGeoCheck(const Geo& g, const std::vector<std::pair<std::string, std::string>>& params,
                   int limit, std::string& out, std::string& err);

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
