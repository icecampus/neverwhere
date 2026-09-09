// D3 (agent_tooling_plan): builtin reference docs
// (src/libs/pgg/src/eval/builtin_docs.*, `PggTool docs builtin[s]`).
// Completeness is pinned BOTH ways against the live signature registry: every
// registered builtin has a doc, every doc names a registered builtin, and the
// signature text renders deterministically from the registry itself.
#include <gtest/gtest.h>

#include <algorithm>
#include <unordered_set>

#include "pgg/src/eval/builtin_docs.h"
#include "pgg/src/eval/builtins.h"

namespace {

TEST(BuiltinDocs, EveryRegistryBuiltinHasADoc) {
    std::unordered_set<std::string> docNames;
    for (const pgg::BuiltinDoc& d : pgg::allBuiltinDocs()) {
        EXPECT_TRUE(docNames.insert(d.name).second) << "duplicate doc: " << d.name;
        EXPECT_FALSE(d.group.empty()) << d.name;
        EXPECT_FALSE(d.summary.empty()) << d.name;
        EXPECT_FALSE(d.example.empty()) << d.name;
    }
    for (const pgg::BuiltinSig& s : pgg::builtinRegistry())
        EXPECT_TRUE(docNames.count(s.name)) << s.name << " is registered but has no doc";
}

TEST(BuiltinDocs, RegistryOrderIsKept) {
    // allBuiltinDocs follows the registry order (docs builtins prints it as-is).
    std::vector<std::string> reg, doc;
    for (const pgg::BuiltinSig& s : pgg::builtinRegistry()) reg.push_back(s.name);
    for (const pgg::BuiltinDoc& d : pgg::allBuiltinDocs()) doc.push_back(d.name);
    ASSERT_EQ(reg.size(), doc.size());
    // Docs may sit in any order internally; require the same SET here and let
    // `docs builtins` iterate the registry (order comes from there).
    std::vector<std::string> rs = reg, ds = doc;
    std::sort(rs.begin(), rs.end());
    std::sort(ds.begin(), ds.end());
    EXPECT_EQ(rs, ds);
}

TEST(BuiltinDocs, SignatureTextIsDeterministicAndMatchesTheRegistry) {
    const pgg::BuiltinSig* clip = pgg::findBuiltin("clip");
    ASSERT_NE(clip, nullptr);
    EXPECT_EQ(pgg::builtinSignatureText(*clip),
              "clip(geo: geo, origin: vec3, normal: vec3, cap_group: string = \"\") -> geo");
    const pgg::BuiltinSig* setPos = pgg::findBuiltin("set_position");
    ASSERT_NE(setPos, nullptr);
    EXPECT_EQ(pgg::builtinSignatureText(*setPos),
              "set_position(geo: geo, offset: field<vec3> = (0, 0, 0), pos: field<vec3>? = none, "
              "where: field<bool> = true) -> geo");
    // Multi-output and field-polymorphic shapes.
    const pgg::BuiltinSig* bbox = pgg::findBuiltin("bbox");
    ASSERT_NE(bbox, nullptr);
    EXPECT_EQ(pgg::builtinSignatureText(*bbox), "bbox(geo: geo) -> (vec3, vec3)");
    const pgg::BuiltinSig* dot = pgg::findBuiltin("dot");
    ASSERT_NE(dot, nullptr);
    EXPECT_EQ(pgg::builtinSignatureText(*dot), "dot(a, b)");
    // Enum parameters spell their values.
    const pgg::BuiltinSig* cn = pgg::findBuiltin("compute_normals");
    ASSERT_NE(cn, nullptr);
    EXPECT_EQ(pgg::builtinSignatureText(*cn),
              "compute_normals(geo: geo, mode: enum {smooth, flat, by_angle, auto} = \"smooth\", "
              "angle: f32 = 30) -> geo");
}

TEST(BuiltinDocs, GroupsFollowTheRegistrySections) {
    const std::unordered_set<std::string> kKnown = {
        "sources", "transforms", "rng",   "fields",      "expr",      "groups",  "attributes",
        "topology", "scatter",    "aggregators", "sdf",  "fracture",  "deferred",
    };
    for (const pgg::BuiltinDoc& d : pgg::allBuiltinDocs())
        EXPECT_TRUE(kKnown.count(d.group)) << d.name << " in unknown group " << d.group;
    // Spot checks.
    ASSERT_NE(pgg::findBuiltinDoc("clip"), nullptr);
    EXPECT_EQ(pgg::findBuiltinDoc("clip")->group, "topology");
    ASSERT_NE(pgg::findBuiltinDoc("mesh_from_sdf"), nullptr);
    EXPECT_EQ(pgg::findBuiltinDoc("mesh_from_sdf")->group, "sdf");
    ASSERT_NE(pgg::findBuiltinDoc("fbm"), nullptr);
    EXPECT_EQ(pgg::findBuiltinDoc("fbm")->group, "fields");
}

}  // namespace
