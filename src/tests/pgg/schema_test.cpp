// E5 static schema tests (spec §7.6): missing attribute/group reads are
// caught before any execution — through def boundaries and field closures
// (fields inline into their consumption context), across the SDF attribute
// barrier, and through the set/remove/rename/promote transfer rules — while
// unprovable (open) schemas fall back to the runtime E302/E305 unchanged.
#include <gtest/gtest.h>

#include "pgg/eval.h"
#include "test_utils.h"

namespace {

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

pgg::RunResult runSrc(const std::string& src) { return pgg::run(src); }

TEST(Schema, StaticE302ThroughDefBoundary) {
    // The §7.2 example verbatim: the field closure inlines into the instance
    // and is checked against the geometry passed in — before execution.
    pgg::RunResult r = runSrc(
        "def displace(geo: geo, amount: field<f32>) -> (out: geo) {\n"
        "    \"\"\"Offset along normals.\"\"\"\n"
        "    out = set_position(geo, offset = @N * amount)\n"
        "}\n"
        "root = rng_from_seed(1)\n"
        "base = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "rock = displace(base, amount = fbm(scale = 2.5, rng = root) * @height_rel)\n"
        "output rock\n");
    EXPECT_EQ(countCode(r, "E302"), 1);
    EXPECT_TRUE(hasMessage(r, "E302", "height_rel"));
    EXPECT_TRUE(hasMessage(r, "E302", "static schema"));
    EXPECT_TRUE(r.outputs.empty());  // caught before execution
}

TEST(Schema, MissingNormalsAreStaticE302) {
    pgg::RunResult r = runSrc(
        "line = mesh_line(count = 4, length = 2.0)\n"
        "moved = set_position(line, offset = @N * 0.5)\n"
        "output moved\n");
    EXPECT_EQ(countCode(r, "E302"), 1);
    EXPECT_TRUE(hasMessage(r, "E302", "@N"));
    EXPECT_TRUE(hasMessage(r, "E302", "static schema"));
}

TEST(Schema, MissingGroupIsStaticE305) {
    pgg::RunResult r = runSrc(
        "root = rng_from_seed(1)\n"
        "base = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "pts = distribute_points(base, density = ingroup(\"nope\") * 3, rng = root)\n"
        "output pts\n");
    EXPECT_EQ(countCode(r, "E305"), 1);
    EXPECT_TRUE(hasMessage(r, "E305", "static schema"));
    // ... and it is a group error, not an attribute one.
    EXPECT_EQ(countCode(r, "E302"), 0);
}

TEST(Schema, SdfAttributeBarrier) {
    // §8.4/§16: attributes do not cross an SDF section; reading @slope after
    // mesh_from_sdf fails statically even though it existed before.
    pgg::RunResult r = runSrc(
        "base = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "tagged = set(base, \"slope\", 1.0)\n"
        "field = sdf_from_mesh(tagged, voxel = 0.25)\n"
        "m = mesh_from_sdf(field, voxel = 0.25)\n"
        "n = compute_normals(m)\n"
        "bad = set_position(n, offset = @N * @slope)\n"
        "output bad\n");
    EXPECT_EQ(countCode(r, "E302"), 1);
    EXPECT_TRUE(hasMessage(r, "E302", "static schema"));
}

TEST(Schema, RemoveRenamePromoteFlows) {
    // Removed attribute: reading it afterwards fails statically.
    pgg::RunResult removed = runSrc(
        "base = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "tagged = set(base, \"a\", 1.0)\n"
        "clean = remove_attr(tagged, \"a\")\n"
        "bad = set(clean, \"b\", @a)\n"
        "output bad\n");
    EXPECT_EQ(countCode(removed, "E302"), 1);
    // Renamed: the new name reads fine, the old one fails.
    pgg::RunResult renamed = runSrc(
        "base = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "tagged = set(base, \"a\", 1.0)\n"
        "rn = rename_attr(tagged, \"a\", \"b\")\n"
        "good = set(rn, \"c\", @b)\n"
        "output good\n");
    pggtest::expectNoErrors(renamed);
    pgg::RunResult oldName = runSrc(
        "base = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "tagged = set(base, \"a\", 1.0)\n"
        "rn = rename_attr(tagged, \"a\", \"b\")\n"
        "bad = set(rn, \"c\", @a)\n"
        "output bad\n");
    EXPECT_EQ(countCode(oldName, "E302"), 1);
    // Promote from a domain that does not have the attribute: static E302.
    pgg::RunResult promoted = runSrc(
        "base = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "bad = promote(base, \"a\", from = points, to = faces)\n"
        "output bad\n");
    EXPECT_EQ(countCode(promoted, "E302"), 1);
    EXPECT_TRUE(hasMessage(promoted, "E302", "static schema"));
}

TEST(Schema, AggregatorFieldsAreChecked) {
    pgg::RunResult r = runSrc(
        "base = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "m = min_of(@missing, on = base)\n"
        "output m\n");
    EXPECT_EQ(countCode(r, "E302"), 1);
    EXPECT_TRUE(hasMessage(r, "E302", "static schema"));
}

TEST(Schema, OpenSchemaFallsBackToRuntime) {
    // A non-literal attribute name (a launch param) makes the schema open:
    // the static pass stays silent and the runtime E302 fires instead
    // (unchanged fallback).
    pgg::RunResult r = runSrc(
        "param name: string = \"dynamic\"\n"
        "base = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "tagged = set(base, name, 1.0)\n"
        "bad = set(tagged, \"other\", @missing)\n"
        "output bad\n");
    EXPECT_EQ(countCode(r, "E302"), 1);
    for (const pgg::Diagnostic& d : r.diagnostics) {
        if (d.code == "E302") EXPECT_EQ(d.message.find("static schema"), std::string::npos);
    }
    // Same shape with the attribute actually written under the param name:
    // the open schema hides it statically, the runtime finds it — clean run.
    pgg::RunResult ok = runSrc(
        "param name: string = \"dynamic\"\n"
        "base = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "tagged = set(base, name, 1.0)\n"
        "good = set(tagged, \"other\", @dynamic)\n"
        "output good\n");
    pggtest::expectNoErrors(ok);
}

TEST(Schema, SetDomainInferenceMirrorsRuntime) {
    // Constant value -> detail attribute (readable everywhere); element
    // field -> points. Both must typecheck/run cleanly through the schema.
    pgg::RunResult r = runSrc(
        "base = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "tagged = set(base, \"konst\", 1.5)\n"
        "withfield = set(tagged, \"per_point\", @konst * 2.0)\n"
        "n = compute_normals(withfield)\n"
        "final = set(n, \"both\", @konst + @per_point)\n"
        "output final\n");
    pggtest::expectNoErrors(r);
}

}  // namespace
