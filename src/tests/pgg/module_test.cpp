// E5 module tests (spec §7.6): imports bind namespaces against the configured
// roots, qualified calls resolve to module defs (and only defs), modules
// import modules, and the module diagnostics fire — E501 (not found), E502
// (cycle), E505 (unknown qualified symbol), E506 (library def without a
// docstring), namespace conflicts (E102), version pinning (stage error).
#include <gtest/gtest.h>

#include "pgg/eval.h"
#include "test_utils.h"

namespace {

const std::string kLibRoot = std::string(PGG_CORPUS_DIR);

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

pgg::RunResult runWithLib(const std::string& src) {
    pgg::RunParams p;
    p.importRoots.push_back(kLibRoot);
    return pgg::run(src, p);
}

TEST(Module, ImportByPathAndAlias) {
    pgg::RunResult r = runWithLib(
        "import lib.rocks\n"
        "import lib.rocks as rb\n"
        "root = rng_from_seed(1)\n"
        "a = rocks.make_pebble(size = 2.0, rng = root)\n"
        "b = rb.make_pebble(size = 2.0, rng = root)\n"
        "w = rocks.scale_of(g = a)\n"
        "scene = merge(a, b)\n"
        "output scene\n"
        "output w\n");
    pggtest::expectNoErrors(r);
    glm::vec3 mn, mx;
    pgg::geoBBox(*pggtest::geoOutput(r, "scene"), mn, mx);
    pggtest::expectVec3Near(mn, glm::vec3(-1.0f));
    pggtest::expectVec3Near(mx, glm::vec3(1.0f));
    pggtest::expectF32Near(pgg::asF32(*pggtest::outputOf(r, "w")), 2.0f);
}

TEST(Module, MultiOutputThroughNamespace) {
    pgg::RunResult r = runWithLib(
        "import lib.rocks as rb\n"
        "root = rng_from_seed(1)\n"
        "first, second = rb.pebble_pair(size = 1.0, rng = root)\n"
        "scene = merge(first, second)\n"
        "output scene\n");
    pggtest::expectNoErrors(r);
    glm::vec3 mn, mx;
    pgg::geoBBox(*pggtest::geoOutput(r, "scene"), mn, mx);
    pggtest::expectF32Near(mn.x, -0.5f);
    pggtest::expectF32Near(mx.x, 2.5f);
}

TEST(Module, ModuleImportingModule) {
    pgg::RunResult r = runWithLib(
        "import lib.outer\n"
        "x = outer.quadruple_it(x = 2.0)\n"
        "output x\n");
    pggtest::expectNoErrors(r);
    pggtest::expectF32Near(pgg::asF32(*pggtest::outputOf(r, "x")), 8.0f);  // 2 -> 4 -> 8
}

TEST(Module, MissingModuleIsE501) {
    pgg::RunResult r = runWithLib(
        "import lib.no_such_module\n"
        "x = 1\n"
        "output x\n");
    EXPECT_EQ(countCode(r, "E501"), 1);
    EXPECT_TRUE(hasMessage(r, "E501", "no_such_module"));
}

TEST(Module, ImportWithoutRootsIsE501WithHint) {
    // pgg::run(text) has no implicit root (unlike runFile): imports need
    // explicit RunParams::importRoots.
    pgg::RunResult r = pgg::run(
        "import lib.rocks\n"
        "x = 1\n"
        "output x\n");
    EXPECT_EQ(countCode(r, "E501"), 1);
    bool hinted = false;
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (d.code == "E501" && d.hint.find("importRoots") != std::string::npos) hinted = true;
    EXPECT_TRUE(hinted);
}

TEST(Module, ImportCycleIsE502) {
    pgg::RunResult r = runWithLib(
        "import lib.cyc_a\n"
        "x = 1\n"
        "output x\n");
    EXPECT_EQ(countCode(r, "E502"), 1);
    EXPECT_TRUE(hasMessage(r, "E502", "cyc_"));
}

TEST(Module, UnknownQualifiedSymbolIsE505) {
    pgg::RunResult r = runWithLib(
        "import lib.rocks\n"
        "root = rng_from_seed(1)\n"
        "x = rocks.no_such_def(size = 1.0, rng = root)\n"
        "output x\n");
    EXPECT_EQ(countCode(r, "E505"), 1);
    // Unknown namespace (never imported) is the same code.
    pgg::RunResult r2 = runWithLib(
        "import lib.rocks\n"
        "root = rng_from_seed(1)\n"
        "x = pebbles.make_pebble(size = 1.0, rng = root)\n"
        "output x\n");
    EXPECT_EQ(countCode(r2, "E505"), 1);
}

TEST(Module, OnlyDefsAreVisibleThroughNamespace) {
    // A module's top-level binding is not exported: calling it is E505.
    pgg::RunResult r = runWithLib(
        "import lib.with_binding\n"
        "x = with_binding.secret()\n"
        "output x\n");
    EXPECT_EQ(countCode(r, "E505"), 1);
    EXPECT_TRUE(hasMessage(r, "E505", "secret"));
}

TEST(Module, NamespaceConflictsAreE102) {
    // Two imports binding one namespace.
    pgg::RunResult r = runWithLib(
        "import lib.rocks\n"
        "import lib.outer as rocks\n"
        "x = 1\n"
        "output x\n");
    EXPECT_EQ(countCode(r, "E102"), 1);
    // Namespace colliding with a built-in operation.
    pgg::RunResult r2 = runWithLib(
        "import lib.rocks as box\n"
        "x = 1\n"
        "output x\n");
    EXPECT_EQ(countCode(r2, "E102"), 1);
    // Namespace colliding with a top-level binding.
    pgg::RunResult r3 = runWithLib(
        "rocks = 1\n"
        "import lib.rocks\n"
        "x = rocks\n"
        "output x\n");
    EXPECT_EQ(countCode(r3, "E102"), 1);
}

TEST(Module, LibraryDefWithoutDocstringIsE506) {
    pgg::RunResult r = runWithLib(
        "import lib.bad_doc\n"
        "x = 1\n"
        "output x\n");
    EXPECT_EQ(countCode(r, "E506"), 1);
    EXPECT_TRUE(hasMessage(r, "E506", "undocumented"));
}

TEST(Module, VersionPinningIsAStageError) {
    pgg::RunResult r = runWithLib(
        "import lib.rocks @ 1.2\n"
        "x = 1\n"
        "output x\n");
    EXPECT_EQ(countCode(r, "E201"), 1);
    EXPECT_TRUE(hasMessage(r, "E201", "versioning"));
}

}  // namespace
