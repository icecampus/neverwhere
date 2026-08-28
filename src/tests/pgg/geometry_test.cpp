// Geometry tests: §8.1 sources (counts, topology, bounds, determinism) and
// §8.2 transforms (exact math, topology preservation, normals).
#include <gtest/gtest.h>

#include "pgg/eval.h"
#include "pgg/src/eval/builtins.h"

namespace {

TEST(Geometry, IcoSphereCountsAndWatertight) {
    for (int s = 0; s <= 3; ++s) {
        pgg::GeoPtr g = pgg::genIcoSphere(s, 1.0f);
        ASSERT_EQ(g->kind, pgg::GeoKind::Mesh);
        EXPECT_EQ(g->pointCount(), static_cast<size_t>(10 * (1 << (2 * s)) + 2)) << "subdiv " << s;
        EXPECT_EQ(g->faceCount(), static_cast<size_t>(20 * (1 << (2 * s)))) << "subdiv " << s;
        EXPECT_EQ(g->cornerCount(), 3 * g->faceCount());
        EXPECT_EQ(pgg::nonManifoldEdgeCount(*g), 0u) << "subdiv " << s;
        for (int32_t c : *g->cornerVerts) {
            EXPECT_GE(c, 0);
            EXPECT_LT(static_cast<size_t>(c), g->pointCount());
        }
    }
}

TEST(Geometry, IcoSphereNormalsOutward) {
    pgg::GeoPtr g = pgg::genIcoSphere(2, 2.0f);
    ASSERT_TRUE(g->normals);
    glm::vec3 mn, mx;
    pgg::geoBBox(*g, mn, mx);
    for (size_t i = 0; i < g->pointCount(); ++i) {
        const glm::vec3& p = (*g->positions)[i];
        const glm::vec3& n = (*g->normals)[i];
        EXPECT_NEAR(glm::length(n), 1.0f, 1e-5f);
        EXPECT_NEAR(glm::length(p), 2.0f, 1e-4f);
        EXPECT_GT(glm::dot(n, glm::normalize(p)), 0.999f);
    }
    EXPECT_NEAR(mx.x, 2.0f, 1e-4f);
    EXPECT_NEAR(mn.x, -2.0f, 1e-4f);
}

TEST(Geometry, BoxCountsBoundsNormals) {
    for (int res = 1; res <= 3; ++res) {
        pgg::GeoPtr g = pgg::genBox(glm::vec3(2, 4, 6), res);
        EXPECT_EQ(g->pointCount(), static_cast<size_t>(6 * res * res + 2)) << "res " << res;
        EXPECT_EQ(g->faceCount(), static_cast<size_t>(6 * res * res)) << "res " << res;
        EXPECT_EQ(g->cornerCount(), 4 * g->faceCount());
        EXPECT_EQ(pgg::nonManifoldEdgeCount(*g), 0u) << "res " << res;
        glm::vec3 mn, mx;
        pgg::geoBBox(*g, mn, mx);
        EXPECT_EQ(mn, glm::vec3(-1, -2, -3));
        EXPECT_EQ(mx, glm::vec3(1, 2, 3));
    }
    // Outward facet normals on a res=1 box.
    pgg::GeoPtr g = pgg::genBox(glm::vec3(2, 2, 2), 1);
    for (size_t i = 0; i < g->pointCount(); ++i) {
        const glm::vec3& n = (*g->normals)[i];
        EXPECT_NEAR(glm::length(n), 1.0f, 1e-6f);
        EXPECT_GT(glm::dot(n, glm::normalize((*g->positions)[i])), 0.5f);
    }
}

TEST(Geometry, GridCountsBoundsNormals) {
    pgg::GeoPtr g = pgg::genGrid(glm::vec2(10, 4), 5);
    EXPECT_EQ(g->pointCount(), 36u);
    EXPECT_EQ(g->faceCount(), 25u);
    EXPECT_EQ(g->cornerCount(), 100u);
    glm::vec3 mn, mx;
    pgg::geoBBox(*g, mn, mx);
    EXPECT_EQ(mn, glm::vec3(-5, 0, -2));
    EXPECT_EQ(mx, glm::vec3(5, 0, 2));
    for (const glm::vec3& n : *g->normals) EXPECT_EQ(n, glm::vec3(0, 1, 0));
    // Grid is an open surface: the boundary edges are non-manifold.
    EXPECT_GT(pgg::nonManifoldEdgeCount(*g), 0u);
}

TEST(Geometry, MeshLinePoints) {
    pgg::GeoPtr g = pgg::genMeshLine(5, 8.0f, glm::vec3(0, 0, 2));
    ASSERT_EQ(g->kind, pgg::GeoKind::Points);
    ASSERT_EQ(g->pointCount(), 5u);
    EXPECT_EQ((*g->positions)[0], glm::vec3(0, 0, 0));
    EXPECT_NEAR(glm::length((*g->positions)[4] - glm::vec3(0, 0, 8)), 0.0f, 1e-5f);
    for (size_t i = 1; i < 5; ++i)
        EXPECT_NEAR(glm::length((*g->positions)[i] - (*g->positions)[i - 1]), 2.0f, 1e-5f);
    // count = 1 -> a single point at the origin; zero dir falls back to +Z.
    EXPECT_EQ(pgg::genMeshLine(1, 8.0f, glm::vec3(0, 0, 0))->pointCount(), 1u);
}

TEST(Geometry, PointCloudDeterminismAndBounds) {
    const pgg::Rng rng = pgg::splitRng(pgg::rngFromSeed(7), "cloud");
    pgg::GeoPtr a = pgg::genPointCloud(64, glm::vec3(10, 2, 4), rng);
    pgg::GeoPtr b = pgg::genPointCloud(64, glm::vec3(10, 2, 4), rng);
    pgg::GeoPtr c = pgg::genPointCloud(64, glm::vec3(10, 2, 4), pgg::splitRng(rng, 1));
    ASSERT_EQ(a->pointCount(), 64u);
    EXPECT_EQ(*a->positions, *b->positions);        // same rng -> same cloud
    EXPECT_NE(*a->positions, *c->positions);        // split -> independent
    for (const glm::vec3& p : *a->positions) {
        EXPECT_GE(p.x, -5.0f);
        EXPECT_LE(p.x, 5.0f);
        EXPECT_GE(p.y, -1.0f);
        EXPECT_LE(p.y, 1.0f);
        EXPECT_GE(p.z, -2.0f);
        EXPECT_LE(p.z, 2.0f);
    }
}

// --- §8.2 transforms through the engine ----------------------------------------

pgg::GeoPtr runGeo(const std::string& src, const std::string& out = "out") {
    pgg::RunResult r = pgg::run(src);
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (!d.isWarning) ADD_FAILURE() << d.code << " " << d.message;
    for (const pgg::RunOutput& o : r.outputs)
        if (o.name == out) return pgg::asGeo(o.value);
    ADD_FAILURE() << "no output " << out;
    return nullptr;
}

TEST(Geometry, TransformExactMath) {
    pgg::GeoPtr g = runGeo(
        "b = mesh_line(count = 2, length = 1.0)\n"
        "out = transform(b, translate = (10, 0, 0), scale = (2, 2, 2))\n"
        "output out\n");
    ASSERT_EQ(g->pointCount(), 2u);
    EXPECT_EQ((*g->positions)[0], glm::vec3(10, 0, 0));
    EXPECT_EQ((*g->positions)[1], glm::vec3(10, 0, 2));
    // Yaw +90° maps +Z to +X.
    pgg::GeoPtr r = runGeo(
        "b = mesh_line(count = 2, length = 1.0)\n"
        "out = transform(b, rotate = (0, 90, 0))\n"
        "output out\n");
    EXPECT_NEAR((*r->positions)[1].x, 1.0f, 1e-5f);
    EXPECT_NEAR((*r->positions)[1].y, 0.0f, 1e-5f);
    EXPECT_NEAR((*r->positions)[1].z, 0.0f, 1e-5f);
    // Normals rotate with the geometry.
    pgg::GeoPtr n = runGeo(
        "b = grid(size = (2, 2), res = 1)\n"
        "out = transform(b, rotate = (0, 0, 90))\n"
        "output out\n");
    ASSERT_TRUE(n->normals);
    EXPECT_NEAR((*n->normals)[0].x, -1.0f, 1e-5f);
}

TEST(Geometry, SetPositionOffsetPosWhere) {
    pgg::GeoPtr off = runGeo(
        "g = grid(size = (2, 2), res = 1)\n"
        "out = set_position(g, offset = (0, 1, 0))\n"
        "output out\n");
    for (const glm::vec3& p : *off->positions) EXPECT_EQ(p.y, 1.0f);
    // Topology is carried through unchanged (structural sharing by design).
    pgg::GeoPtr plainGrid = runGeo("g = grid(size = (2, 2), res = 1)\noutput g\n", "g");
    EXPECT_EQ(*off->cornerVerts, *plainGrid->cornerVerts);
    EXPECT_EQ(*off->faceOffsets, *plainGrid->faceOffsets);

    pgg::GeoPtr pos = runGeo(
        "g = grid(size = (2, 2), res = 1)\n"
        "out = set_position(g, pos = (1, 2, 3))\n"
        "output out\n");
    for (const glm::vec3& p : *pos->positions) EXPECT_EQ(p, glm::vec3(1, 2, 3));

    pgg::GeoPtr masked = runGeo(
        "g = grid(size = (2, 2), res = 1)\n"
        "out = set_position(g, offset = (0, 1, 0), where = @index > 1)\n"
        "output out\n");
    EXPECT_EQ((*masked->positions)[0].y, 0.0f);
    EXPECT_EQ((*masked->positions)[1].y, 0.0f);
    EXPECT_EQ((*masked->positions)[2].y, 1.0f);
    EXPECT_EQ((*masked->positions)[3].y, 1.0f);

    pgg::GeoPtr nowhere = runGeo(
        "g = grid(size = (2, 2), res = 1)\n"
        "out = set_position(g, offset = (0, 1, 0), where = @index > 100)\n"
        "output out\n");
    EXPECT_EQ(*nowhere->positions, *plainGrid->positions);
}

TEST(Geometry, SmoothPreservesTopologyAndContracts) {
    pgg::GeoPtr base = runGeo("g = ico_sphere(subdiv = 2, radius = 1.0)\noutput g\n", "g");
    pgg::GeoPtr sm = runGeo(
        "g = ico_sphere(subdiv = 2, radius = 1.0)\n"
        "out = smooth(g, iterations = 3, factor = 0.5)\n"
        "output out\n");
    EXPECT_EQ(sm->pointCount(), base->pointCount());
    EXPECT_EQ(sm->faceCount(), base->faceCount());
    EXPECT_EQ(sm->cornerCount(), base->cornerCount());
    EXPECT_EQ(pgg::nonManifoldEdgeCount(*sm), 0u);
    // Laplacian relaxation pulls vertices inward on a sphere.
    float maxBase = 0.0f, maxSm = 0.0f;
    for (size_t i = 0; i < base->pointCount(); ++i) {
        maxBase = std::max(maxBase, glm::length((*base->positions)[i]));
        maxSm = std::max(maxSm, glm::length((*sm->positions)[i]));
    }
    EXPECT_LT(maxSm, maxBase);
    EXPECT_GT(maxSm, 0.5f);
}

TEST(Geometry, ComputeNormalsUnitAndOutward) {
    for (const char* mode : {"smooth", "by_angle"}) {
        const std::string src =
            "g = ico_sphere(subdiv = 2, radius = 1.0)\n"
            "out = compute_normals(g, mode = ";
        pgg::GeoPtr g = runGeo(src + mode + std::string(")\noutput out\n"));
        ASSERT_TRUE(g->normals);
        for (size_t i = 0; i < g->pointCount(); ++i) {
            EXPECT_NEAR(glm::length((*g->normals)[i]), 1.0f, 1e-4f) << mode;
            EXPECT_GT(glm::dot((*g->normals)[i], glm::normalize((*g->positions)[i])), 0.9f) << mode;
        }
    }
    // flat mode writes the per-corner "N" attribute (faceted shading).
    pgg::GeoPtr flat = runGeo(
        "g = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "out = compute_normals(g, mode = flat)\n"
        "output out\n");
    const pgg::AttrSet* corners = flat->attrs(pgg::Domain::Corners);
    ASSERT_TRUE(corners);
    const pgg::AttrColumn* col = corners->find("N");
    ASSERT_TRUE(col);
    const auto& cn = std::get<std::shared_ptr<const std::vector<glm::vec3>>>(col->data);
    ASSERT_EQ(cn->size(), flat->cornerCount());
    for (const glm::vec3& n : *cn) EXPECT_NEAR(glm::length(n), 1.0f, 1e-4f);
}

}  // namespace
