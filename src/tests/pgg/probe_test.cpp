// E6 debug subsystem tests (spec §9, acceptance §15 E6): inspectors L0–L2
// (schema/stats/coverage/table) with exact deterministic formats, probe spec
// parsing and path resolution (flat names, instance paths, index-less def
// form, attr/group terminals with the single-output sugar), the output
// suppression rule, laziness (a mid-graph probe does not compute the tail —
// criterion 1), coverage=0% diagnosability (criterion 2), taps (debug flag,
// per-instance def-body taps), aggregate=stats merging and E606 cases.
#include <gtest/gtest.h>

#include <algorithm>
#include <limits>

#include "pgg/eval.h"
#include "pgg/src/eval/probe.h"
#include "test_utils.h"

namespace {

uint64_t fieldEvals(const pgg::RunResult& r, const std::string& binding) {
    auto it = r.stats.bindingFieldEvals.find(binding);
    return it == r.stats.bindingFieldEvals.end() ? 0 : it->second;
}

int countCode(const pgg::RunResult& r, const std::string& code) {
    int n = 0;
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (d.code == code) n += 1;
    return n;
}

bool hasMessage(const pgg::RunResult& r, const std::string& code, const std::string& needle) {
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (d.code == code && d.message.find(needle) != std::string::npos) return true;
    return false;
}

pgg::RunResult runProbe(const std::string& src, const std::string& spec) {
    pgg::RunParams p;
    p.probes = {spec};
    return pgg::run(src, p);
}

const pgg::ProbeRecord* findRecord(const pgg::RunResult& r, const std::string& origin,
                                   const std::string& path, const std::string& inspector) {
    for (const pgg::ProbeRecord& pr : r.probes)
        if (pr.origin == origin && pr.path == path && pr.inspector == inspector) return &pr;
    return nullptr;
}

// --- 1. schema (L0) -------------------------------------------------------------

const std::string kBasic =
    "g = rng_from_seed(1)\n"
    "base = ico_sphere(subdiv = 1, radius = 1.0)\n"  // 42 pts, 80 tri, @N
    "m0 = set(base, \"slope\", 1.0)\n"                // detail f32 (constant field)
    "m1 = set(m0, \"ord\", index())\n"                // points int
    "m = mark(m1, \"flat_tops\", where = index() > 20)\n"
    "pts = mesh_line(count = 3, length = 2.0)\n"
    "inst = instance_on_points(pts, source = base)\n"
    "field = sdf_sphere(r = 2.0)\n"
    "scalar = 41 + 1\n"
    "lst = [base, m]\n"
    "output m\n";

TEST(Probe, SchemaFormats) {
    pgg::RunParams p;
    p.probes = {"m:schema", "pts:schema", "inst:schema", "field:schema",
                "scalar:schema", "lst:schema", "g:schema"};
    pgg::RunResult r = pgg::run(kBasic, p);
    pggtest::expectNoErrors(r);
    EXPECT_TRUE(r.outputs.empty());  // probe-only: declared outputs suppressed
    ASSERT_EQ(r.probes.size(), 7u);

    const pgg::ProbeRecord* rec = findRecord(r, "probe", "m", "schema");
    ASSERT_TRUE(rec);
    // Attrs sorted by name (@N listed, domains abbreviated); groups sorted.
    EXPECT_EQ(rec->text,
              "mesh 42 pts, 80 tri; attrs: N(vec3, pts), ord(int, pts), slope(f32, detail); "
              "groups: flat_tops");

    rec = findRecord(r, "probe", "pts", "schema");
    ASSERT_TRUE(rec);
    EXPECT_EQ(rec->text, "points 3 pts");

    rec = findRecord(r, "probe", "inst", "schema");
    ASSERT_TRUE(rec);
    EXPECT_EQ(rec->text, "instances 3 anchors, 1 variants");

    rec = findRecord(r, "probe", "field", "schema");
    ASSERT_TRUE(rec);
    EXPECT_EQ(rec->text, "sdf nodes=1 bbox=(-2, -2, -2)..(2, 2, 2)");

    rec = findRecord(r, "probe", "scalar", "schema");
    ASSERT_TRUE(rec);
    EXPECT_EQ(rec->text, "int 42");

    rec = findRecord(r, "probe", "lst", "schema");
    ASSERT_TRUE(rec);
    EXPECT_EQ(rec->text, "list[2] of geo<mesh>");

    rec = findRecord(r, "probe", "g", "schema");
    ASSERT_TRUE(rec);
    EXPECT_EQ(rec->text, "<rng>");
}

// --- 2. stats (L1) --------------------------------------------------------------

TEST(Probe, StatsFormats) {
    pgg::RunParams p;
    p.probes = {"m.ord:stats", "m.slope:stats", "pts.P:stats", "scalar:stats", "pts:stats"};
    pgg::RunResult r = pgg::run(kBasic, p);
    pggtest::expectNoErrors(r);
    ASSERT_EQ(r.probes.size(), 5u);

    // int attr over ico subdiv 1: values 0..41, mean 20.5; p50/p90 nearest-rank.
    const pgg::ProbeRecord* rec = findRecord(r, "probe", "m.ord", "stats");
    ASSERT_TRUE(rec);
    EXPECT_EQ(rec->text, "ord: mean 20.5, p50 20, p90 37, min 0, max 41 (42 pts)");

    // Constant field lands on detail (domain inference of `set`).
    rec = findRecord(r, "probe", "m.slope", "stats");
    ASSERT_TRUE(rec);
    EXPECT_EQ(rec->text, "slope: mean 1, p50 1, p90 1, min 1, max 1 (1 detail)");

    // @P terminal: one line per component (mesh_line z ordinates are 0, 1, 2).
    rec = findRecord(r, "probe", "pts.P", "stats");
    ASSERT_TRUE(rec);
    EXPECT_EQ(rec->text,
              "P.x: mean 0, p50 0, p90 0, min 0, max 0 (3 pts)\n"
              "P.y: mean 0, p50 0, p90 0, min 0, max 0 (3 pts)\n"
              "P.z: mean 1, p50 1, p90 2, min 0, max 2 (3 pts)");

    // Numeric scalar: a single-element entry set.
    rec = findRecord(r, "probe", "scalar", "stats");
    ASSERT_TRUE(rec);
    EXPECT_EQ(rec->text, "value: mean 42, p50 42, p90 42, min 42, max 42 (1 value)");

    // Geo without numeric point attributes: the counts fallback line.
    rec = findRecord(r, "probe", "pts", "stats");
    ASSERT_TRUE(rec);
    EXPECT_EQ(rec->text, "points 3 pts");
}

TEST(Probe, StatsNoTerminalListsNumericPointAttrs) {
    pgg::RunResult r = runProbe(
        "l = mesh_line(count = 3, length = 2.0)\n"
        "m = set(l, \"ord\", index())\n"
        "output m\n",
        "m:stats");
    pggtest::expectNoErrors(r);
    ASSERT_EQ(r.probes.size(), 1u);
    EXPECT_EQ(r.probes[0].text, "ord: mean 1, p50 1, p90 2, min 0, max 2 (3 pts)");
}

// --- 3. coverage (L1) -------------------------------------------------------------

TEST(Probe, CoverageFormats) {
    // 21 of 42 points above index 20.
    pgg::RunResult r = runProbe(kBasic, "m.flat_tops:coverage");
    pggtest::expectNoErrors(r);
    ASSERT_EQ(r.probes.size(), 1u);
    EXPECT_EQ(r.probes[0].text, "flat_tops: true 50.0% (21/42)");
}

TEST(Probe, CoverageZeroPercentIsDiagnosable) {
    // Acceptance criterion 2: an all-false mask prints `true 0.0% (0/N)`.
    pgg::RunResult r = runProbe(
        "l = mesh_line(count = 3, length = 2.0)\n"
        "m = mark(l, \"sel\", where = dot(@P, (0, 0, 1)) > 100.0)\n"
        "output m\n",
        "m.sel:coverage");
    pggtest::expectNoErrors(r);
    ASSERT_EQ(r.probes.size(), 1u);
    EXPECT_EQ(r.probes[0].text, "sel: true 0.0% (0/3)");
}

// --- 4. table (L2) ----------------------------------------------------------------

TEST(Probe, TableFormat) {
    pgg::RunResult r = runProbe(
        "l = mesh_line(count = 3, length = 2.0)\n"
        "m = set(l, \"ord\", index())\n"
        "output m\n",
        "m:table[limit=2]");
    pggtest::expectNoErrors(r);
    ASSERT_EQ(r.probes.size(), 1u);
    EXPECT_EQ(r.probes[0].text,
              "table[limit=2] (first 2 of 3 by @index)\n"
              "0: @P=(0, 0, 0), ord=0\n"
              "1: @P=(0, 0, 1), ord=1");
}

// --- 5. laziness (acceptance criterion 1) -----------------------------------------

TEST(Probe, LazyMidGraphDoesNotComputeTail) {
    const std::string src =
        "root_rng = rng_from_seed(1)\n"
        "noise_rng = split_rng(root_rng, key = \"n\")\n"
        "base = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "n = fbm(scale = 2.0, rng = noise_rng)\n"
        "rock = set_position(base, offset = @N * n)\n"
        "output rock\n";
    // Probe-only run: the declared output is skipped and nothing downstream
    // of `base` evaluates.
    pgg::RunResult mid = runProbe(src, "base:schema");
    pggtest::expectNoErrors(mid);
    EXPECT_TRUE(mid.outputs.empty());
    ASSERT_EQ(mid.probes.size(), 1u);
    EXPECT_EQ(mid.probes[0].text, "mesh 42 pts, 80 tri; attrs: N(vec3, pts)");
    EXPECT_EQ(mid.stats.fieldsEvaluated, 0u);
    EXPECT_EQ(fieldEvals(mid, "n"), 0u);  // tail binding counters stay 0

    // An explicit output request brings the output back — and evaluates
    // exactly the requested tail.
    pgg::RunParams p;
    p.probes = {"base:schema"};
    pgg::RunResult withOut = pgg::run(src, p, {"rock"});
    pggtest::expectNoErrors(withOut);
    ASSERT_EQ(withOut.outputs.size(), 1u);
    EXPECT_EQ(fieldEvals(withOut, "n"), 1u);
    EXPECT_EQ(withOut.probes.size(), 1u);  // shared env: base not recomputed
}

// --- 6. instance paths ------------------------------------------------------------

const std::string kInst =
    "def make_rock(size: f32) -> (out: geo) {\n"
    "    \"\"\"Rock stub.\"\"\"\n"
    "    line = mesh_line(count = 3, length = size)\n"
    "    tagged = set(line, \"ord\", index())\n"
    "    marked = mark(tagged, \"sel\", where = index() > 0)\n"
    "    out = set(marked, \"extent\", size)\n"
    "}\n"
    "def pair(g: geo) -> (x: geo, y: geo) {\n"
    "    \"\"\"Two outputs.\"\"\"\n"
    "    x = g\n"
    "    y = g\n"
    "}\n"
    "a = make_rock(2.0)\n"
    "b = make_rock(4.0)\n"
    "px, py = pair(a)\n"
    "output a\n"
    "output b\n";

TEST(Probe, InstancePathResolution) {
    pgg::RunParams p;
    p.probes = {"make_rock[0]:schema", "make_rock[1].tagged:schema", "pair[0]:schema"};
    pgg::RunResult r = pgg::run(kInst, p);
    pggtest::expectNoErrors(r);

    // Instance path -> the instance's output (record path = the path probed).
    const pgg::ProbeRecord* rec = findRecord(r, "probe", "make_rock[0]", "schema");
    ASSERT_TRUE(rec);
    EXPECT_EQ(rec->text, "points 3 pts; attrs: extent(f32, detail), ord(int, pts); groups: sel");

    // `<ipath>.<local>` resolves to the flat binding of the instance local.
    rec = findRecord(r, "probe", "make_rock[1].tagged", "schema");
    ASSERT_TRUE(rec);
    EXPECT_EQ(rec->text, "points 3 pts; attrs: ord(int, pts)");

    // A multi-output instance gives one record per output, suffixed.
    rec = findRecord(r, "probe", "pair[0].x", "schema");
    ASSERT_TRUE(rec);
    EXPECT_EQ(rec->text, "points 3 pts; attrs: extent(f32, detail), ord(int, pts); groups: sel");
    rec = findRecord(r, "probe", "pair[0].y", "schema");
    ASSERT_TRUE(rec);
    EXPECT_EQ(rec->text, "points 3 pts; attrs: extent(f32, detail), ord(int, pts); groups: sel");
}

TEST(Probe, IndexLessFormMatchesAllInstances) {
    pgg::RunResult r = runProbe(kInst, "make_rock:stats");
    pggtest::expectNoErrors(r);
    // Per-instance records, ordered by instance path (expansion order).
    ASSERT_EQ(r.probes.size(), 2u);
    EXPECT_EQ(r.probes[0].path, "make_rock[0]");
    EXPECT_EQ(r.probes[0].text, "ord: mean 1, p50 1, p90 2, min 0, max 2 (3 pts)");
    EXPECT_EQ(r.probes[1].path, "make_rock[1]");
    EXPECT_EQ(r.probes[1].text, "ord: mean 1, p50 1, p90 2, min 0, max 2 (3 pts)");
}

TEST(Probe, SingleOutputSugar) {
    // `make_rock[1].extent` = the instance's only output + attr `extent`.
    pgg::RunResult r = runProbe(kInst, "make_rock[1].extent:stats");
    pggtest::expectNoErrors(r);
    ASSERT_EQ(r.probes.size(), 1u);
    EXPECT_EQ(r.probes[0].path, "make_rock[1].extent");
    EXPECT_EQ(r.probes[0].text, "extent: mean 4, p50 4, p90 4, min 4, max 4 (1 detail)");
}

TEST(Probe, BindingWinsOverSugar) {
    // `make_rock[1].size` is the def's PARAMETER binding — a binding always
    // resolves before the attr sugar (longest-prefix rule).
    pgg::RunResult r = runProbe(kInst, "make_rock[1].size:stats");
    pggtest::expectNoErrors(r);
    ASSERT_EQ(r.probes.size(), 1u);
    EXPECT_EQ(r.probes[0].text, "value: mean 4, p50 4, p90 4, min 4, max 4 (1 value)");
}

TEST(Probe, AggregateStats) {
    pgg::RunParams p;
    p.probes = {"make_rock.extent:stats[aggregate=stats]", "make_rock.ord:stats[aggregate=stats]",
                "make_rock.sel:coverage[aggregate=stats]", "make_rock:schema[aggregate=stats]"};
    pgg::RunResult r = pgg::run(kInst, p);
    pggtest::expectNoErrors(r);
    ASSERT_EQ(r.probes.size(), 4u);

    // Per-instance means 2 and 4: mean 3, population std 1.
    EXPECT_EQ(r.probes[0].text, "extent: mean 3 \xc2\xb1 1 across 2 instances");
    EXPECT_EQ(r.probes[0].path, "make_rock.extent");
    // Identical per-instance stats collapse to a zero spread.
    EXPECT_EQ(r.probes[1].text, "ord: mean 1 \xc2\xb1 0 across 2 instances");
    // Coverage pools the counts across instances.
    EXPECT_EQ(r.probes[2].text, "sel: true 66.7% (4/6) across 2 instances");
    // Identical schemas collapse with a multiplier.
    EXPECT_EQ(r.probes[3].text,
              "points 3 pts; attrs: extent(f32, detail), ord(int, pts); groups: sel x 2 instances");
}

// --- 7. taps ----------------------------------------------------------------------

// The per-instance constant `size` is written per point (domain = points) —
// the merge-safe idiom: on detail it would differ between the two instances
// and the merge would be an E609 (silent left-wins before v1.12).
const std::string kTaps =
    "def make_rock(size: f32) -> (out: geo) {\n"
    "    \"\"\"Rock stub.\"\"\"\n"
    "    line = mesh_line(count = 3, length = size)\n"
    "    tagged = set(line, \"ord\", index())\n"
    "    tap tagged\n"
    "    out = set(tagged, \"size\", size, domain = points)\n"
    "}\n"
    "a = make_rock(2.0)\n"
    "b = make_rock(4.0)\n"
    "merged = merge(a, b)\n"
    "output merged\n"
    "tap stats: merged\n";

TEST(Probe, TapsIgnoredWhenDebugOff) {
    pgg::RunResult r = pgg::run(kTaps);  // debug = false (default)
    pggtest::expectNoErrors(r);
    EXPECT_EQ(r.outputs.size(), 1u);
    EXPECT_TRUE(r.probes.empty());
}

TEST(Probe, TapsFireWhenDebugOn) {
    pgg::RunParams p;
    p.debug = true;
    pgg::RunResult r = pgg::run(kTaps, p);
    pggtest::expectNoErrors(r);
    EXPECT_EQ(r.outputs.size(), 1u);  // taps never suppress outputs

    // Top-level tap first (file order), then def-body taps per instance.
    ASSERT_EQ(r.probes.size(), 5u);
    EXPECT_EQ(r.probes[0].origin, "tap");
    EXPECT_EQ(r.probes[0].path, "merged");
    EXPECT_EQ(r.probes[0].inspector, "stats");
    EXPECT_EQ(r.probes[0].text,
              "ord: mean 1, p50 1, p90 2, min 0, max 2 (6 pts)\n"
              "size: mean 3, p50 2, p90 4, min 2, max 4 (6 pts)");

    // `tap tagged` inside the def fires on every instance with the default
    // schema+stats pair (§9.3/§9.4).
    EXPECT_EQ(r.probes[1].path, "make_rock[0].tagged");
    EXPECT_EQ(r.probes[1].inspector, "schema");
    EXPECT_EQ(r.probes[1].text, "points 3 pts; attrs: ord(int, pts)");
    EXPECT_EQ(r.probes[2].path, "make_rock[0].tagged");
    EXPECT_EQ(r.probes[2].inspector, "stats");
    EXPECT_EQ(r.probes[2].text, "ord: mean 1, p50 1, p90 2, min 0, max 2 (3 pts)");
    EXPECT_EQ(r.probes[3].path, "make_rock[1].tagged");
    EXPECT_EQ(r.probes[3].inspector, "schema");
    EXPECT_EQ(r.probes[4].path, "make_rock[1].tagged");
    EXPECT_EQ(r.probes[4].inspector, "stats");
}

TEST(Probe, TapWithAttrTerminal) {
    pgg::RunParams p;
    p.debug = true;
    pgg::RunResult r = pgg::run(
        "l = mesh_line(count = 3, length = 2.0)\n"
        "m = set(l, \"ord\", index())\n"
        "output m\n"
        "tap stats: m.ord\n",
        p);
    pggtest::expectNoErrors(r);
    ASSERT_EQ(r.probes.size(), 1u);
    EXPECT_EQ(r.probes[0].origin, "tap");
    EXPECT_EQ(r.probes[0].path, "m.ord");
    EXPECT_EQ(r.probes[0].text, "ord: mean 1, p50 1, p90 2, min 0, max 2 (3 pts)");
}

// --- 8. E606 ----------------------------------------------------------------------

const std::string kErr =
    "root_rng = rng_from_seed(1)\n"
    "noise_rng = split_rng(root_rng, key = \"n\")\n"
    "base = ico_sphere(subdiv = 1, radius = 1.0)\n"
    "n = fbm(scale = 2.0, rng = noise_rng)\n"
    "rock = set_position(base, offset = @N * n)\n"
    "tagged = set(rock, \"ord\", index())\n"
    "scalar = 2.5\n"
    "output tagged\n";

TEST(Probe, E606UnknownPath) {
    pgg::RunResult r = runProbe(kErr, "typo:schema");
    EXPECT_TRUE(r.hasErrors());
    EXPECT_EQ(countCode(r, "E606"), 1);
    EXPECT_TRUE(hasMessage(r, "E606", "probe target 'typo' not found"));
    EXPECT_TRUE(r.probes.empty());
}

TEST(Probe, E606FieldBinding) {
    pgg::RunResult r = runProbe(kErr, "n:schema");
    EXPECT_TRUE(r.hasErrors());
    EXPECT_EQ(countCode(r, "E606"), 1);
    EXPECT_TRUE(hasMessage(r, "E606", "is a field, not a value"));
}

TEST(Probe, E606MalformedSpecs) {
    for (const std::string spec : {"rock:bogus", "base:table[limit=abc]", "base[badparam=1]", ""}) {
        pgg::RunResult r = runProbe(kErr, spec);
        EXPECT_TRUE(r.hasErrors()) << spec;
        EXPECT_EQ(countCode(r, "E606"), 1) << spec;
    }
    EXPECT_TRUE(hasMessage(runProbe(kErr, "rock:bogus"), "E606", "unknown inspector 'bogus'"));
    EXPECT_TRUE(hasMessage(runProbe(kErr, "base:table[limit=abc]"), "E606", "bad limit value"));
    EXPECT_TRUE(hasMessage(runProbe(kErr, "base[badparam=1]"), "E606", "unknown probe parameter"));
}

TEST(Probe, E606ParamMisuse) {
    pgg::RunResult limitOnStats = runProbe(kErr, "base:stats[limit=2]");
    EXPECT_EQ(countCode(limitOnStats, "E606"), 1);
    EXPECT_TRUE(hasMessage(limitOnStats, "E606", "limit applies to the table and check inspectors"));
    pgg::RunResult aggOnTable = runProbe(kErr, "base:table[aggregate=stats]");
    EXPECT_EQ(countCode(aggOnTable, "E606"), 1);
    EXPECT_TRUE(hasMessage(aggOnTable, "E606", "aggregate=stats is not supported for the table inspector"));
}

TEST(Probe, E606CoverageOnNonBool) {
    pgg::RunResult r = runProbe(kErr, "tagged.ord:coverage");
    EXPECT_TRUE(r.hasErrors());
    EXPECT_EQ(countCode(r, "E606"), 1);
    EXPECT_TRUE(hasMessage(r, "E606", "is not a bool mask"));

    pgg::RunResult noTerminal = runProbe(kErr, "tagged:coverage");
    EXPECT_EQ(countCode(noTerminal, "E606"), 1);
    EXPECT_TRUE(hasMessage(noTerminal, "E606", "needs a bool attribute or group terminal"));
}

TEST(Probe, E606InvalidTargets) {
    // table on a scalar
    pgg::RunResult r = runProbe(kErr, "scalar:table");
    EXPECT_EQ(countCode(r, "E606"), 1);
    EXPECT_TRUE(hasMessage(r, "E606", "table needs a geo value"));
    // attr terminal on a scalar value
    pgg::RunResult term = runProbe(kErr, "scalar.xyz:stats");
    EXPECT_EQ(countCode(term, "E606"), 1);
    EXPECT_TRUE(hasMessage(term, "E606", "terminal needs a geo value"));
    // missing attribute on a valid geo binding
    pgg::RunResult missing = runProbe(kErr, "tagged.slope:stats");
    EXPECT_EQ(countCode(missing, "E606"), 1);
    EXPECT_TRUE(hasMessage(missing, "E606", "no such attribute or group"));
    // the single-output sugar on a multi-output instance is ambiguous
    pgg::RunResult amb = runProbe(kInst, "pair[0].ord:stats");
    EXPECT_EQ(countCode(amb, "E606"), 1);
    EXPECT_TRUE(hasMessage(amb, "E606", "ambiguous"));
}

// --- 9. determinism / regression barrier ------------------------------------------

TEST(Probe, DeterminismAndOutputIdentity) {
    const std::string kCorpus = std::string(PGG_CORPUS_DIR) + "/e1_rock.pgg";
    pgg::RunParams p;
    p.probes = {"base:schema", "rock.N:stats"};
    pgg::RunResult a = pgg::runFile(kCorpus, p, {"rock"});
    pgg::RunResult b = pgg::runFile(kCorpus, p, {"rock"});
    pggtest::expectNoErrors(a);
    pggtest::expectNoErrors(b);
    // Two runs produce byte-identical records.
    ASSERT_EQ(a.probes.size(), b.probes.size());
    ASSERT_EQ(a.probes.size(), 2u);
    for (size_t i = 0; i < a.probes.size(); ++i) {
        EXPECT_EQ(a.probes[i].origin, b.probes[i].origin);
        EXPECT_EQ(a.probes[i].path, b.probes[i].path);
        EXPECT_EQ(a.probes[i].inspector, b.probes[i].inspector);
        EXPECT_EQ(a.probes[i].text, b.probes[i].text);
    }
    // The output computed alongside probes is bit-identical to a clean run.
    pgg::RunResult plain = pgg::runFile(kCorpus, {}, {"rock"});
    pggtest::expectNoErrors(plain);
    EXPECT_EQ(pggtest::geoContentHash(pgg::asGeo(a.outputs[0].value)),
              pggtest::geoContentHash(pgg::asGeo(plain.outputs[0].value)));
}

// --- 10. sample (§9.6) ------------------------------------------------------------

TEST(Probe, SampleSdfSphere) {
    pgg::RunResult r = runProbe("s = sdf_sphere(r = 1.0)\noutput s\n", "s:sample[at=(2,0,0);(0,0,0)]");
    pggtest::expectNoErrors(r);
    ASSERT_EQ(r.probes.size(), 1u);
    EXPECT_EQ(r.probes[0].inspector, "sample");
    EXPECT_EQ(r.probes[0].text,
              "sample[at=(2, 0, 0);(0, 0, 0)]\n"
              "2 0 0 1\n"
              "0 0 0 -1");
}

TEST(Probe, SampleProfileForm) {
    pgg::RunResult r = runProbe("s = sdf_sphere(r = 1.0)\noutput s\n",
                                "s:sample[from=(2,0,0),to=(4,0,0),n=3]");
    pggtest::expectNoErrors(r);
    ASSERT_EQ(r.probes.size(), 1u);
    EXPECT_EQ(r.probes[0].text,
              "sample[from=(2, 0, 0),to=(4, 0, 0),n=3]\n"
              "2 0 0 1\n"
              "3 0 0 2\n"
              "4 0 0 3");
}

TEST(Probe, SampleGeoMeshPseudoSign) {
    // Box spans [-1,1]^3: the outside point reads +1, the centre -1 (sign by
    // the closest triangle's normal — pseudo, noted in the header).
    pgg::RunResult r = runProbe("b = box(size = vec3(2, 2, 2))\noutput b\n",
                                "b:sample[at=(2,0,0);(0,0,0)]");
    pggtest::expectNoErrors(r);
    ASSERT_EQ(r.probes.size(), 1u);
    EXPECT_EQ(r.probes[0].text,
              "sample[at=(2, 0, 0);(0, 0, 0)] (pseudo-sign distance from mesh)\n"
              "2 0 0 1\n"
              "0 0 0 -1");
}

TEST(Probe, SampleInstancePathWithVecParams) {
    // `make_ball[1]` ends with a bracket too — params still parse, and the
    // instance path still resolves (the vec-in-parens grammar regression).
    pgg::RunResult r = runProbe(
        "def make_ball(r: f32) -> (out: sdf) {\n"
        "    \"\"\"Ball.\"\"\"\n"
        "    out = sdf_sphere(r = r)\n"
        "}\n"
        "a = make_ball(1.0)\n"
        "b = make_ball(2.0)\n"
        "output a\n",
        "make_ball[1]:sample[at=(0,0,0)]");
    pggtest::expectNoErrors(r);
    ASSERT_EQ(r.probes.size(), 1u);
    EXPECT_EQ(r.probes[0].path, "make_ball[1]");
    EXPECT_EQ(r.probes[0].text, "sample[at=(0, 0, 0)]\n0 0 0 -2");
}

// --- 11. slice (§9.6) -------------------------------------------------------------

TEST(Probe, SliceSdfSphereAsciiExact) {
    pgg::RunResult r = runProbe("s = sdf_sphere(r = 1.0)\noutput s\n", "s:slice[axis=z,at=0,step=0.5]");
    pggtest::expectNoErrors(r);
    ASSERT_EQ(r.probes.size(), 1u);
    EXPECT_EQ(r.probes[0].text,
              "slice[axis=z,at=0,step=0.5] (5 x 5, bounds (-1, -1)..(1, 1))\n"
              "..#..\n"
              ".###.\n"
              "#####\n"
              ".###.\n"
              "..#..");
}

TEST(Probe, SliceSdfSphereCsv) {
    pgg::RunResult r = runProbe("s = sdf_sphere(r = 1.0)\noutput s\n",
                                "s:slice[axis=z,at=0,step=0.5,format=csv]");
    pggtest::expectNoErrors(r);
    ASSERT_EQ(r.probes.size(), 1u);
    const std::string& t = r.probes[0].text;
    EXPECT_TRUE(t.starts_with("slice[axis=z,at=0,step=0.5,format=csv] (5 x 5, bounds (-1, -1)..(1, 1))\n"));
    EXPECT_NE(t.find("\n0,0,0.414214\n"), std::string::npos);
    EXPECT_NE(t.find("\n2,2,-1\n"), std::string::npos);
    EXPECT_EQ(std::count(t.begin(), t.end(), '\n'), 25);  // 25 grid rows after the header
}

TEST(Probe, SliceThinShellRepro) {
    // Flattened analog of docs/pgg/mc_thin_shell_slivers.md: two crossing
    // box-prisms minus a slab cutter — 2 cm end walls at z = +/-3.425, so the
    // field is negative only in a narrow z window (the plus-shaped wall
    // footprint at 3.425, nothing one voxel step away).
    const std::string src =
        "main_outer = sdf_box(size = vec3(5.86, 6.86, 6.87))\n"
        "wing_outer = sdf_box(size = vec3(6.86, 5.86, 6.87))\n"
        "cutter = sdf_box(size = vec3(100.0, 100.0, 6.83))\n"
        "shell = sdf_subtract(sdf_union(main_outer, wing_outer), cutter)\n"
        "output shell\n";
    pgg::RunResult wall = runProbe(src, "shell:slice[axis=z,at=3.425,step=0.25]");
    pggtest::expectNoErrors(wall);
    ASSERT_EQ(wall.probes.size(), 1u);
    EXPECT_TRUE(wall.probes[0].text.starts_with(
        "slice[axis=z,at=3.425,step=0.25] (28 x 28, bounds (-3.43, -3.43)..(3.43, 3.43))\n"));
    EXPECT_GT(std::count(wall.probes[0].text.begin(), wall.probes[0].text.end(), '#'), 100);

    for (const std::string spec :
         {"shell:slice[axis=z,at=3.40,step=0.25]", "shell:slice[axis=z,at=3.45,step=0.25]"}) {
        pgg::RunResult miss = runProbe(src, spec);
        pggtest::expectNoErrors(miss);
        ASSERT_EQ(miss.probes.size(), 1u) << spec;
        EXPECT_EQ(miss.probes[0].text.find('#'), std::string::npos) << spec;
    }
}

TEST(Probe, SliceGeoMeshNotesDistance) {
    pgg::RunResult r = runProbe("b = box(size = vec3(2, 2, 2))\noutput b\n",
                                "b:slice[axis=z,at=0,step=1]");
    pggtest::expectNoErrors(r);
    ASSERT_EQ(r.probes.size(), 1u);
    // The z=0 plane cuts through the box: every grid point reads <= 0
    // (inside or on the surface), so the map is all '#'; the header marks
    // the pseudo-sign distance mode.
    EXPECT_EQ(r.probes[0].text,
              "slice[axis=z,at=0,step=1] (3 x 3, bounds (-1, -1)..(1, 1), pseudo-sign distance from mesh)\n"
              "###\n"
              "###\n"
              "###");
}

// --- 12. check (§9.6) -------------------------------------------------------------

TEST(Probe, CheckCleanBox) {
    pgg::RunResult r = runProbe("b = box(size = vec3(2, 2, 2))\noutput b\n", "b:check");
    pggtest::expectNoErrors(r);
    ASSERT_EQ(r.probes.size(), 1u);
    EXPECT_EQ(r.probes[0].text,
              "degenerate 0\n"
              "nonmanifold 0\n"
              "boundary 0\n"
              "isolated 0\n"
              "nan 0\n"
              "components 1\n"
              "oriented_mismatch 0\n"
              "edge_min 2\n"
              "edge_median 2\n"
              "needles 0\n"
              "ok");
}

TEST(Probe, CheckBrokenMeshDirect) {
    // Degenerate face (repeated index), a duplicated triangle (orientation
    // mismatch), an isolated NaN point. Direct C++-level call — PGG sources
    // cannot express broken meshes on purpose.
    const float qnan = std::numeric_limits<float>::quiet_NaN();
    pgg::GeoPtr g = pgg::makeMesh({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {qnan, 5, 5}},
                                  {0, 1, 2, 0, 0, 1, 0, 1, 2}, {0, 3, 6, 9});
    std::string text, err;
    ASSERT_TRUE(pgg::probeGeoCheck(*g, {}, 8, text, err)) << err;
    EXPECT_EQ(text,
              "degenerate 1\n"
              "degenerate_faces 1\n"
              "nonmanifold 0\n"
              "boundary 0\n"
              "isolated 1\n"
              "nan 1\n"
              "components 1\n"
              "oriented_mismatch 1\n"
              "edge_min 1\n"
              "edge_median 1\n"
              "needles 0\n"
              "issues 4");
}

TEST(Probe, CheckNeedlesAndComponents) {
    // Two disconnected triangles, one a sliver (aspect 100): boundary edges
    // and 2 components are informative, the needle is the only issue.
    pgg::GeoPtr g = pgg::makeMesh({{0, 0, 0}, {10, 0, 0}, {0, 0.1f, 0}, {0, 0, 0}, {0, 1, 0}, {1, 0, 0}},
                                  {0, 1, 2, 3, 4, 5}, {0, 3, 6});
    std::string text, err;
    ASSERT_TRUE(pgg::probeGeoCheck(*g, {}, 8, text, err)) << err;
    EXPECT_EQ(text,
              "degenerate 0\n"
              "nonmanifold 0\n"
              "boundary 6\n"
              "isolated 0\n"
              "nan 0\n"
              "components 2\n"
              "oriented_mismatch 0\n"
              "edge_min 0.1\n"
              "edge_median 1\n"
              "needles 1\n"
              "needle_ratio 50.0%\n"
              "needles_faces 0\n"
              "issues 1");
}

TEST(Probe, CheckLimitIsAccepted) {
    pgg::RunResult r = runProbe("b = box(size = vec3(2, 2, 2))\noutput b\n", "b:check[limit=2]");
    pggtest::expectNoErrors(r);
    ASSERT_EQ(r.probes.size(), 1u);
    EXPECT_EQ(r.probes[0].inspector, "check");
}

// --- 13. sample/slice/check E606 ----------------------------------------------------

TEST(Probe, E606SampleSliceCheckMisuse) {
    const std::string src =
        "b = box(size = vec3(2, 2, 2))\n"
        "pts = mesh_line(count = 3, length = 2.0)\n"
        "s = sdf_sphere(r = 1.0)\n"
        "output b\n";
    EXPECT_TRUE(hasMessage(runProbe(src, "b:sample[foo=1]"), "E606", "unknown probe parameter 'foo'"));
    EXPECT_TRUE(hasMessage(runProbe(src, "b:sample"), "E606", "sample needs at="));
    EXPECT_TRUE(hasMessage(runProbe(src, "b:sample[at=(0,0)]"), "E606", "bad at value"));
    EXPECT_TRUE(hasMessage(runProbe(src, "b:sample[from=(0,0,0)]"), "E606", "needs both"));
    EXPECT_TRUE(hasMessage(runProbe(src, "b:sample[aggregate=stats]"), "E606",
                           "aggregate=stats is not supported for the sample inspector"));
    EXPECT_TRUE(hasMessage(runProbe(src, "b:slice[axis=z]"), "E606", "slice needs at="));
    EXPECT_TRUE(hasMessage(runProbe(src, "b:slice[axis=q,at=0,step=1]"), "E606", "bad axis value"));
    EXPECT_TRUE(hasMessage(runProbe(src, "b:slice[axis=z,at=0,step=-1]"), "E606", "bad step value"));
    EXPECT_TRUE(hasMessage(runProbe(src, "pts:check"), "E606", "check needs a geo<mesh>"));
    EXPECT_TRUE(hasMessage(runProbe(src, "s:check"), "E606", "check needs a geo<mesh>"));
    EXPECT_TRUE(hasMessage(runProbe(src, "b:check[warn_aspect=abc]"), "E606", "bad warn_aspect value"));
}

}  // namespace
