// E6 debug subsystem tests (spec §9, acceptance §15 E6): inspectors L0–L2
// (schema/stats/coverage/table) with exact deterministic formats, probe spec
// parsing and path resolution (flat names, instance paths, index-less def
// form, attr/group terminals with the single-output sugar), the output
// suppression rule, laziness (a mid-graph probe does not compute the tail —
// criterion 1), coverage=0% diagnosability (criterion 2), taps (debug flag,
// per-instance def-body taps), aggregate=stats merging and E606 cases.
#include <gtest/gtest.h>

#include "pgg/eval.h"
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
    EXPECT_TRUE(hasMessage(limitOnStats, "E606", "limit applies to the table inspector"));
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

}  // namespace
