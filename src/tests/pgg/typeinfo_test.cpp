// §4.4 attribute typeinfo (spec v1.14, Houdini-style tags): inference in set(),
// explicit tags, tag-driven transform/realize, E609 on tag conflicts in merge
// (runtime + static), typed neutral fill, derived @N read fallback, and the
// tag as part of the fingerprint.
#include <gtest/gtest.h>

#include "pgg/eval.h"
#include "pgg/src/eval/fingerprint.h"
#include "pgg/src/eval/geometry.h"

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

void expectNoErrors(const pgg::RunResult& r) {
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (!d.isWarning) ADD_FAILURE() << d.code << " " << d.message;
}

pgg::GeoPtr geoOutput(const pgg::RunResult& r, const std::string& name) {
    for (const auto& o : r.outputs)
        if (o.name == name) return pgg::asGeo(o.value);
    return nullptr;
}

const pgg::AttrColumn* column(const pgg::Geo& g, pgg::Domain d, const std::string& name) {
    const pgg::AttrSet* as = g.attrs(d);
    return as ? as->find(name) : nullptr;
}

template <typename T>
const std::vector<T>& values(const pgg::AttrColumn& col) {
    return *std::get<std::shared_ptr<const std::vector<T>>>(col.data);
}

TEST(TypeInfo, SetInfersReservedNamesAndHonorsExplicitTags) {
    pgg::RunResult r = pgg::run(
        "l = mesh_line(count = 2, length = 1.0)\n"
        "a = set(l, \"dir\", @P + (1, 0, 0), typeinfo = vector)\n"
        "b = set(a, \"tint\", @P * 0 + (1, 0.5, 0.25))\n"
        "c = set(b, \"orient\", orient_from_euler(@P * 0 + (0, 90, 0)))\n"
        "d = set(c, \"weight\", 2.0)\n"
        "e = set(d, \"anchor\", @P, typeinfo = point)\n"
        "f = set(e, \"raw\", @P, typeinfo = none)\n"
        "output f\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "f");
    ASSERT_TRUE(g);
    ASSERT_TRUE(column(*g, pgg::Domain::Points, "dir"));
    EXPECT_EQ(column(*g, pgg::Domain::Points, "dir")->typeInfo, pgg::AttrTypeInfo::Vector);
    EXPECT_EQ(column(*g, pgg::Domain::Points, "tint")->typeInfo, pgg::AttrTypeInfo::None);
    EXPECT_EQ(column(*g, pgg::Domain::Points, "orient")->typeInfo, pgg::AttrTypeInfo::Quaternion);
    EXPECT_EQ(column(*g, pgg::Domain::Detail, "weight")->typeInfo, pgg::AttrTypeInfo::None);
    EXPECT_EQ(column(*g, pgg::Domain::Points, "anchor")->typeInfo, pgg::AttrTypeInfo::Point);
    EXPECT_EQ(column(*g, pgg::Domain::Points, "raw")->typeInfo, pgg::AttrTypeInfo::None);
}

TEST(TypeInfo, TransformMovesColumnsByTag) {
    pgg::RunResult r = pgg::run(
        "l = mesh_line(count = 1, length = 1.0)\n"
        "a = set(l, \"dir\", @P * 0 + (1, 0, 0), typeinfo = vector)\n"
        "b = set(a, \"anchor\", @P * 0 + (1, 0, 0), typeinfo = point)\n"
        "c = set(b, \"raw\", @P * 0 + (1, 0, 0), typeinfo = none)\n"
        "d = set(c, \"n\", @P * 0 + (1, 0, 0), typeinfo = normal)\n"
        "t = transform(d, translate = (0, 5, 0), rotate = (0, 0, 90), scale = (2, 2, 2))\n"
        "output t\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "t");
    ASSERT_TRUE(g);
    // vector: rotated (+X -> +Y) and scaled, not translated.
    const glm::vec3 dir = values<glm::vec3>(*column(*g, pgg::Domain::Points, "dir"))[0];
    EXPECT_NEAR(glm::length(dir - glm::vec3(0, 2, 0)), 0.0f, 1e-5f);
    // point: full affine.
    const glm::vec3 anchor = values<glm::vec3>(*column(*g, pgg::Domain::Points, "anchor"))[0];
    EXPECT_NEAR(glm::length(anchor - glm::vec3(0, 7, 0)), 0.0f, 1e-5f);
    // none: untouched.
    const glm::vec3 raw = values<glm::vec3>(*column(*g, pgg::Domain::Points, "raw"))[0];
    EXPECT_NEAR(glm::length(raw - glm::vec3(1, 0, 0)), 0.0f, 1e-6f);
    // normal: rotated and renormalized.
    const glm::vec3 n = values<glm::vec3>(*column(*g, pgg::Domain::Points, "n"))[0];
    EXPECT_NEAR(glm::length(n - glm::vec3(0, 1, 0)), 0.0f, 1e-5f);
    // Tags survive the transform.
    EXPECT_EQ(column(*g, pgg::Domain::Points, "dir")->typeInfo, pgg::AttrTypeInfo::Vector);
    EXPECT_EQ(column(*g, pgg::Domain::Points, "anchor")->typeInfo, pgg::AttrTypeInfo::Point);
}

TEST(TypeInfo, RealizeRotatesTaggedSourceColumns) {
    pgg::RunResult r = pgg::run(
        "b = box(size = (1, 1, 1))\n"
        "src = set(b, \"up\", @P * 0 + (0, 1, 0), typeinfo = vector)\n"
        "l = mesh_line(count = 2, length = 4.0)\n"
        "o = set(l, \"orient\", orient_from_euler(@P * 0 + (90, 0, 0)))\n"
        "m = realize(instance_on_points(o, src))\n"
        "output m\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "m");
    ASSERT_TRUE(g);
    const pgg::AttrColumn* up = column(*g, pgg::Domain::Points, "up");
    ASSERT_TRUE(up);
    EXPECT_EQ(up->typeInfo, pgg::AttrTypeInfo::Vector);
    // Pitch 90 about X sends +Y to +Z for every realized point.
    for (const glm::vec3& v : values<glm::vec3>(*up)) EXPECT_NEAR(glm::length(v - glm::vec3(0, 0, 1)), 0.0f, 1e-5f);
}

TEST(TypeInfo, MergeTagConflictIsE609RuntimeAndStatic) {
    // Static: both sides closed, tags differ on the same domain.
    pgg::RunResult r = pgg::run(
        "a = set(mesh_line(count = 2, length = 1.0), \"d\", @P, typeinfo = vector)\n"
        "b = set(mesh_line(count = 2, length = 1.0), \"d\", @P, typeinfo = point)\n"
        "m = merge(a, b)\n"
        "output m\n");
    EXPECT_GE(countCode(r, "E609"), 1);
    EXPECT_TRUE(hasMessage(r, "E609", "typeinfo"));
    // Equal tags merge cleanly and keep the tag.
    pgg::RunResult ok = pgg::run(
        "a = set(mesh_line(count = 2, length = 1.0), \"d\", @P, typeinfo = vector)\n"
        "b = set(mesh_line(count = 2, length = 1.0), \"d\", @P, typeinfo = vector)\n"
        "m = merge(a, b)\n"
        "output m\n");
    expectNoErrors(ok);
    pgg::GeoPtr g = geoOutput(ok, "m");
    ASSERT_TRUE(g);
    EXPECT_EQ(column(*g, pgg::Domain::Points, "d")->typeInfo, pgg::AttrTypeInfo::Vector);
}

TEST(TypeInfo, MergeNeutralFillIsIdentityQuaternionAndDerivedNormal) {
    pgg::RunResult r = pgg::run(
        "a = set(mesh_line(count = 2, length = 1.0), \"orient\", orient_from_euler(@P * 0 + (0, 90, 0)))\n"
        "b = mesh_line(count = 3, length = 1.0)\n"
        "m = merge(a, b)\n"
        "s = mesh_from_sdf(sdf_sphere(r = 1.0), voxel = 0.5)\n"
        "bx = transform(box(size = (1, 1, 1)), translate = (5, 0, 0))\n"
        "mm = merge(bx, s)\n"
        "output m\n"
        "output mm\n");
    expectNoErrors(r);
    pgg::GeoPtr m = geoOutput(r, "m");
    ASSERT_TRUE(m);
    const auto& o = values<glm::vec4>(*column(*m, pgg::Domain::Points, "orient"));
    ASSERT_EQ(o.size(), 5u);
    for (size_t i = 2; i < 5; ++i) EXPECT_NEAR(glm::length(o[i] - glm::vec4(0, 0, 0, 1)), 0.0f, 1e-6f);
    // The sdf mesh had no @N: the merged column carries derived normals, not zeros.
    pgg::GeoPtr mm = geoOutput(r, "mm");
    ASSERT_TRUE(mm && mm->normals);
    size_t zero = 0;
    for (const glm::vec3& n : *mm->normals) zero += glm::dot(n, n) < 1e-12f ? 1 : 0;
    EXPECT_EQ(zero, 0u);
}

TEST(TypeInfo, DerivedNormalsReadableOnMeshesOnly) {
    pgg::RunResult r = pgg::run(
        "s = mesh_from_sdf(sdf_sphere(r = 1.0), voxel = 0.25)\n"
        "d = set_position(s, offset = @N * 0.5)\n"
        "output d\n");
    expectNoErrors(r);
    pgg::GeoPtr d = geoOutput(r, "d");
    ASSERT_TRUE(d);
    EXPECT_FALSE(d->normals);  // the column is still not stored (barrier §8.4)
    glm::vec3 mn, mx;
    pgg::geoBBox(*d, mn, mx);
    EXPECT_GT(mx.x, 1.3f);  // pushed outward along derived normals
    // Face-less geometry keeps E302 statically.
    pgg::RunResult bad = pgg::run(
        "l = mesh_line(count = 3, length = 1.0)\n"
        "d = set_position(l, offset = @N * 0.5)\n"
        "output d\n");
    EXPECT_EQ(countCode(bad, "E302"), 1);
}

TEST(TypeInfo, TagIsPartOfTheFingerprint) {
    pgg::RunResult a = pgg::run("g = set(mesh_line(count = 2, length = 1.0), \"d\", @P, typeinfo = vector)\noutput g\n");
    pgg::RunResult b = pgg::run("g = set(mesh_line(count = 2, length = 1.0), \"d\", @P, typeinfo = none)\noutput g\n");
    expectNoErrors(a);
    expectNoErrors(b);
    uint64_t fa = 0, fb = 0;
    ASSERT_TRUE(pgg::fingerprintValue(a.outputs[0].value, fa));
    ASSERT_TRUE(pgg::fingerprintValue(b.outputs[0].value, fb));
    EXPECT_NE(fa, fb);
}

// --- strictness contracts (agent-facing: every ambiguity is a diagnostic) ---

TEST(TypeInfo, UntaggedVec3IsE610StaticAndRuntime) {
    // A vec3/vec4 under a free name must say what it is. Static first (value
    // type known, name literal), and the runtime path guards open schemas too.
    pgg::RunResult r = pgg::run(
        "l = mesh_line(count = 2, length = 1.0)\n"
        "a = set(l, \"dir\", @P + (1, 0, 0))\n"
        "output a\n");
    EXPECT_TRUE(r.hasErrors());
    EXPECT_GE(countCode(r, "E610"), 1);
    EXPECT_TRUE(hasMessage(r, "E610", "typeinfo must be explicit"));
    // vec4 too.
    pgg::RunResult r4 = pgg::run(
        "l = mesh_line(count = 2, length = 1.0)\n"
        "a = set(l, \"q\", orient_from_euler(@P * 0 + (0, 90, 0)))\n"
        "output a\n");
    EXPECT_GE(countCode(r4, "E610"), 1);
    // Scalars, ints, bools and vec2 need no tag: nothing ambiguous about them.
    pgg::RunResult ok = pgg::run(
        "l = mesh_line(count = 2, length = 1.0)\n"
        "a = set(l, \"w\", dot(@P, (1, 0, 0)))\n"
        "b = set(a, \"i\", @index)\n"
        "c = set(b, \"m\", @index > 0)\n"
        "output c\n");
    expectNoErrors(ok);
    // Colors under the reserved names are plain data without a tag.
    pgg::RunResult col = pgg::run(
        "l = mesh_line(count = 2, length = 1.0)\n"
        "a = set(l, \"Cd\", @P * 0 + (1, 0, 0))\n"
        "b = set(a, \"color\", @P * 0 + (1, 0, 0))\n"
        "output b\n");
    expectNoErrors(col);
}

TEST(TypeInfo, TagTypeMismatchIsE610) {
    // quaternion needs vec4, vector/normal/point need vec3.
    pgg::RunResult a = pgg::run(
        "l = mesh_line(count = 2, length = 1.0)\n"
        "g = set(l, \"q\", @P, typeinfo = quaternion)\n"
        "output g\n");
    EXPECT_TRUE(hasMessage(a, "E610", "quaternion"));
    pgg::RunResult b = pgg::run(
        "l = mesh_line(count = 2, length = 1.0)\n"
        "g = set(l, \"w\", dot(@P, (1, 0, 0)), typeinfo = normal)\n"
        "output g\n");
    EXPECT_TRUE(hasMessage(b, "E610", "does not fit"));
    // Misspelled tag is the enum error, not a silent none.
    pgg::RunResult c = pgg::run(
        "l = mesh_line(count = 2, length = 1.0)\n"
        "g = set(l, \"w\", @P, typeinfo = vektor)\n"
        "output g\n");
    EXPECT_TRUE(c.hasErrors());
    EXPECT_GE(countCode(c, "E206"), 1);
}

TEST(TypeInfo, ReservedStampTypesAreE610) {
    // @orient must be a vec4 quaternion, @tint a vec3, @scale f32, @variant int.
    pgg::RunResult o = pgg::run(
        "l = mesh_line(count = 2, length = 1.0)\n"
        "g = set(l, \"orient\", @P)\n"
        "output g\n");
    EXPECT_TRUE(hasMessage(o, "E610", "reserved instance stamp"));
    pgg::RunResult t = pgg::run(
        "l = mesh_line(count = 2, length = 1.0)\n"
        "g = set(l, \"tint\", 0.5)\n"
        "output g\n");
    EXPECT_TRUE(hasMessage(t, "E610", "reserved instance stamp"));
    pgg::RunResult v = pgg::run(
        "l = mesh_line(count = 2, length = 1.0)\n"
        "g = set(l, \"variant\", 0.5)\n"
        "output g\n");
    EXPECT_TRUE(hasMessage(v, "E610", "reserved instance stamp"));
    // orient with a foreign tag is rejected as well.
    pgg::RunResult q = pgg::run(
        "l = mesh_line(count = 2, length = 1.0)\n"
        "g = set(l, \"orient\", orient_from_euler(@P * 0 + (0, 90, 0)), typeinfo = none)\n"
        "output g\n");
    EXPECT_TRUE(hasMessage(q, "E610", "must have typeinfo quaternion"));
    // Well-typed stamps pass.
    pgg::RunResult ok = pgg::run(
        "l = mesh_line(count = 2, length = 1.0)\n"
        "a = set(l, \"orient\", orient_from_euler(@P * 0 + (0, 90, 0)))\n"
        "b = set(a, \"tint\", @P * 0 + (1, 0, 0))\n"
        "c = set(b, \"scale\", 2.0)\n"
        "d = set(c, \"variant\", @index)\n"
        "output d\n");
    expectNoErrors(ok);
}

TEST(TypeInfo, NonUniformScaleOnInstancesIsE611) {
    const char* src =
        "pts = mesh_line(count = 2, length = 1.0)\n"
        "inst = instance_on_points(pts, source = box(size = (1, 1, 1)))\n"
        "t = transform(inst, scale = (1, 2, 1))\n"
        "output t\n";
    pgg::RunResult r = pgg::run(src);
    EXPECT_TRUE(r.hasErrors());
    EXPECT_GE(countCode(r, "E611"), 1);
    // Uniform scale composes into @scale without complaint.
    pgg::RunResult ok = pgg::run(
        "pts = mesh_line(count = 2, length = 1.0)\n"
        "inst = instance_on_points(pts, source = box(size = (1, 1, 1)))\n"
        "t = transform(inst, scale = (2, 2, 2))\n"
        "output t\n");
    expectNoErrors(ok);
    pgg::GeoPtr g = geoOutput(ok, "t");
    ASSERT_TRUE(g);
    const pgg::AttrColumn* sc = column(*g, pgg::Domain::Points, "scale");
    ASSERT_TRUE(sc);
    EXPECT_NEAR(values<float>(*sc)[0], 2.0f, 1e-6f);
}

TEST(TypeInfo, VariantOutOfRangeIsE611) {
    pgg::RunResult r = pgg::run(
        "pts = mesh_line(count = 3, length = 1.0)\n"
        "v = set(pts, \"variant\", @index)\n"
        "inst = instance_on_points(v, box(size = (1, 1, 1)), variants = [box(size = (1, 1, 1)), ico_sphere(subdiv = 0, radius = 0.5)])\n"
        "m = realize(inst)\n"
        "output m\n");
    EXPECT_TRUE(r.hasErrors());
    EXPECT_TRUE(hasMessage(r, "E611", "outside the variants list"));
    pgg::RunResult ok = pgg::run(
        "pts = mesh_line(count = 3, length = 1.0)\n"
        "v = set(pts, \"variant\", @index % 2)\n"
        "inst = instance_on_points(v, box(size = (1, 1, 1)), variants = [box(size = (1, 1, 1)), ico_sphere(subdiv = 0, radius = 0.5)])\n"
        "m = realize(inst)\n"
        "output m\n");
    expectNoErrors(ok);
}

TEST(TypeInfo, RealizeVariantSchemaConflictIsE609) {
    // Same name, different typeinfo across variants: realize is a merge of the
    // stamped copies and follows the merge rule instead of first-variant-wins.
    pgg::RunResult r = pgg::run(
        "pts = mesh_line(count = 2, length = 1.0)\n"
        "a = set(box(size = (1, 1, 1)), \"d\", @P, typeinfo = vector)\n"
        "b = set(box(size = (1, 1, 1)), \"d\", @P, typeinfo = none)\n"
        "v = set(pts, \"variant\", @index)\n"
        "inst = instance_on_points(v, a, variants = [a, b])\n"
        "m = realize(inst)\n"
        "output m\n");
    EXPECT_TRUE(hasMessage(r, "E609", "typeinfo"));
    // Same name on different domains across variants.
    pgg::RunResult d = pgg::run(
        "pts = mesh_line(count = 2, length = 1.0)\n"
        "a = set(box(size = (1, 1, 1)), \"w\", dot(@P, (0, 1, 0)))\n"
        "b = set(box(size = (1, 1, 1)), \"w\", dot(@P, (0, 1, 0)), domain = faces)\n"
        "v = set(pts, \"variant\", @index)\n"
        "inst = instance_on_points(v, a, variants = [a, b])\n"
        "m = realize(inst)\n"
        "output m\n");
    EXPECT_TRUE(hasMessage(d, "E609", "different domains"));
    // Face group present on one variant only is fine (zero-fill, no ambiguity).
    pgg::RunResult ok = pgg::run(
        "pts = mesh_line(count = 2, length = 1.0)\n"
        "a = mark(box(size = (1, 1, 1)), \"top\", dot(@N, (0, 1, 0)) > 0.5, domain = faces)\n"
        "b = box(size = (1, 1, 1))\n"
        "v = set(pts, \"variant\", @index)\n"
        "inst = instance_on_points(v, a, variants = [a, b])\n"
        "m = realize(inst)\n"
        "output m\n");
    expectNoErrors(ok);
}

TEST(TypeInfo, StaleNormalsAreW006AndRemoveAttrNClearsThem) {
    // Stored @N moved by set_position without compute_normals: reading @N warns.
    pgg::RunResult w = pgg::run(
        "b = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "d = set_position(b, offset = @P * 0.3)\n"
        "s = set(d, \"up\", dot(@N, (0, 1, 0)))\n"
        "output s\n");
    expectNoErrors(w);
    EXPECT_EQ(countCode(w, "W006"), 1);
    // compute_normals in between clears the flag.
    pgg::RunResult fixed = pgg::run(
        "b = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "d = compute_normals(set_position(b, offset = @P * 0.3))\n"
        "s = set(d, \"up\", dot(@N, (0, 1, 0)))\n"
        "output s\n");
    expectNoErrors(fixed);
    EXPECT_EQ(countCode(fixed, "W006"), 0);
    // remove_attr(g, "N") drops the dedicated column: derived normals are fresh.
    pgg::RunResult dropped = pgg::run(
        "b = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "d = remove_attr(set_position(b, offset = @P * 0.3), \"N\")\n"
        "s = set(d, \"up\", dot(@N, (0, 1, 0)))\n"
        "output d\n");
    expectNoErrors(dropped);
    EXPECT_EQ(countCode(dropped, "W006"), 0);
    pgg::GeoPtr g = geoOutput(dropped, "d");
    ASSERT_TRUE(g);
    EXPECT_FALSE(g->normals);
    // A mesh that never had a column (mesh_from_sdf) is never stale.
    pgg::RunResult sdf = pgg::run(
        "s = mesh_from_sdf(sdf_sphere(r = 1.0), voxel = 0.25)\n"
        "d = set_position(s, offset = @N * 0.1)\n"
        "e = set(d, \"up\", dot(@N, (0, 1, 0)))\n"
        "output e\n");
    expectNoErrors(sdf);
    EXPECT_EQ(countCode(sdf, "W006"), 0);
}

}  // namespace
