// C4 (docs/pgg/agent_tooling_plan.md): the OBJ export — the per-faces-group
// split (--obj-split-groups), the check-coloring of problem faces
// (--obj-color=check) and the shared face-issue classifier
// (classifyMeshIssueFaces, probe.h) both features build on.
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "pgg/eval.h"
#include "pgg/src/eval/obj_export.h"
#include "pgg/src/eval/probe.h"
#include "test_utils.h"

namespace {

std::filesystem::path tempDir(const char* name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "pgg_obj_export_test" / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

// Lines starting with "v " whose LAST three tokens (the vertex color) equal
// `color` (e.g. "1 0 0").
size_t countColoredVertices(const std::filesystem::path& path, const std::string& color) {
    std::ifstream in(path, std::ios::binary);
    std::string line;
    size_t n = 0;
    while (std::getline(in, line)) {
        if (line.rfind("v ", 0) != 0) continue;
        if (line.size() >= color.size() && line.compare(line.size() - color.size(), color.size(), color) == 0 &&
            line[line.size() - color.size() - 1] == ' ')
            ++n;
    }
    return n;
}

size_t countLinesWithPrefix(const std::filesystem::path& path, const std::string& prefix) {
    std::ifstream in(path, std::ios::binary);
    std::string line;
    size_t n = 0;
    while (std::getline(in, line))
        if (line.rfind(prefix, 0) == 0) ++n;
    return n;
}

// One valid triangle + one repeated-index (degenerate) face.
pgg::GeoPtr degenerateMesh() {
    return pgg::makeMesh({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {0, 1, 2, 0, 1, 1}, {0, 3, 6});
}

// Three triangles sharing the (0,1) edge: nonmanifold.
pgg::GeoPtr nonmanifoldMesh() {
    return pgg::makeMesh({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}},
                         {0, 1, 2, 1, 0, 3, 0, 1, 4}, {0, 3, 6, 9});
}

TEST(ObjExport, ClassifyWatertightBoxIsClean) {
    pgg::RunResult r = pgg::run("b = box(size = (2, 4, 6))\noutput b\n");
    pggtest::expectNoErrors(r);
    pgg::MeshIssueFaces issues;
    ASSERT_TRUE(pgg::classifyMeshIssueFaces(*pggtest::geoOutput(r, "b"), issues));
    EXPECT_TRUE(issues.degenerate.empty());
    EXPECT_TRUE(issues.nonmanifold.empty());
    EXPECT_TRUE(issues.boundary.empty());
    EXPECT_EQ(issues.nonmanifoldEdges, 0u);
    EXPECT_EQ(issues.boundaryEdges, 0u);
}

TEST(ObjExport, ClassifyOpenAndBrokenMeshes) {
    // A single triangle: three boundary edges, all incident to face 0.
    const pgg::GeoPtr tri = pgg::makeMesh({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {0, 1, 2}, {0, 3});
    pgg::MeshIssueFaces issues;
    ASSERT_TRUE(pgg::classifyMeshIssueFaces(*tri, issues));
    EXPECT_EQ(issues.boundaryEdges, 3u);
    EXPECT_EQ(issues.boundary, std::vector<int32_t>{0});
    EXPECT_TRUE(issues.degenerate.empty());
    EXPECT_TRUE(issues.nonmanifold.empty());

    // The degenerate face is classified; its edges add no boundary evidence.
    issues = pgg::MeshIssueFaces{};
    ASSERT_TRUE(pgg::classifyMeshIssueFaces(*degenerateMesh(), issues));
    EXPECT_EQ(issues.degenerate, std::vector<int32_t>{1});
    EXPECT_EQ(issues.boundary, std::vector<int32_t>{0});

    // The shared edge flags all three triangles; their outer edges are boundary.
    issues = pgg::MeshIssueFaces{};
    ASSERT_TRUE(pgg::classifyMeshIssueFaces(*nonmanifoldMesh(), issues));
    EXPECT_EQ(issues.nonmanifoldEdges, 1u);
    EXPECT_EQ(issues.nonmanifold, (std::vector<int32_t>{0, 1, 2}));
    EXPECT_EQ(issues.boundary, (std::vector<int32_t>{0, 1, 2}));
    EXPECT_TRUE(issues.degenerate.empty());

    // Non-mesh payloads classify as empty.
    const pgg::GeoPtr pts = pgg::makePoints({{0, 0, 0}});
    EXPECT_FALSE(pgg::classifyMeshIssueFaces(*pts, issues));
    EXPECT_TRUE(issues.degenerate.empty());
}

TEST(ObjExport, CheckColorsPaintProblemFaces) {
    const std::filesystem::path dir = tempDir("check_colors");
    pgg::ObjExportOptions opts;
    opts.checkColors = true;
    const std::string path = (dir / "m.obj").string();
    std::string err;
    ASSERT_TRUE(pgg::writeObj(path, *degenerateMesh(), &err, opts)) << err;
    // Unwelded: one vertex per corner; face 1 (corners 3..5) red, face 0 blue.
    EXPECT_EQ(countColoredVertices(path, "1 0 0"), 3u);
    EXPECT_EQ(countColoredVertices(path, "0 0.4 1"), 3u);

    ASSERT_TRUE(pgg::writeObj(path, *nonmanifoldMesh(), &err, opts)) << err;
    EXPECT_EQ(countColoredVertices(path, "1 1 0"), 9u);  // all three triangles
}

TEST(ObjExport, WriteObjPlainBoxUnchanged) {
    // No options: the welded export, no colors without @Cd.
    pgg::RunResult r = pgg::run("b = box(size = (2, 4, 6))\noutput b\n");
    pggtest::expectNoErrors(r);
    const std::filesystem::path dir = tempDir("plain");
    const std::string path = (dir / "b.obj").string();
    std::string err;
    ASSERT_TRUE(pgg::writeObj(path, *pggtest::geoOutput(r, "b"), &err)) << err;
    EXPECT_EQ(countLinesWithPrefix(path, "v "), 8u);
    EXPECT_EQ(countLinesWithPrefix(path, "f "), 12u);
}

TEST(ObjExport, SplitGroupsWritesOneFilePerGroup) {
    pgg::RunResult r = pgg::run(
        "b = box(size = (2, 4, 6))\n"
        "m = mark(b, \"top\", where = dot(@N, (0, 1, 0)) > 0.5, domain = faces)\n"
        "output m\n");
    pggtest::expectNoErrors(r);
    const pgg::GeoPtr m = pggtest::geoOutput(r, "m");
    ASSERT_TRUE(m);
    const std::filesystem::path dir = tempDir("split");
    std::vector<std::string> written;
    std::string err;
    ASSERT_TRUE(pgg::writeObjSplitGroups(dir.string(), "m", *m, written, &err)) << err;
    ASSERT_EQ(written.size(), 2u);
    EXPECT_TRUE(written[0].find("m.top.obj") != std::string::npos);
    EXPECT_TRUE(written[1].find("m._nogroup.obj") != std::string::npos);
    // The +Y quad alone: 4 compacted points, 2 fan triangles.
    EXPECT_EQ(countLinesWithPrefix(written[0], "v "), 4u);
    EXPECT_EQ(countLinesWithPrefix(written[0], "f "), 2u);
    // The other five quads: 2*4 + 4*... points of the box minus the top-only
    // ones are shared — count faces only: 5 quads -> 10 triangles.
    EXPECT_EQ(countLinesWithPrefix(written[1], "f "), 10u);
    // No plain m.obj next to the split files.
    EXPECT_FALSE(std::filesystem::exists(dir / "m.obj"));
}

TEST(ObjExport, SplitGroupsWithoutGroupsWritesTheSingleFile) {
    pgg::RunResult r = pgg::run("b = box(size = (2, 4, 6))\noutput b\n");
    pggtest::expectNoErrors(r);
    const std::filesystem::path dir = tempDir("split_plain");
    std::vector<std::string> written;
    std::string err;
    ASSERT_TRUE(pgg::writeObjSplitGroups(dir.string(), "b", *pggtest::geoOutput(r, "b"), written, &err))
        << err;
    ASSERT_EQ(written.size(), 1u);
    EXPECT_TRUE(written[0].find("b.obj") != std::string::npos);
    EXPECT_EQ(countLinesWithPrefix(written[0], "v "), 8u);
}

}  // namespace
