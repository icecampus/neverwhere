// E8 layout tests (spec §10, acceptance §15 E8): deterministic auto-layout
// (same file -> same coordinates, layer sanity outside loops, zone frames
// wrapping their bodies), the `@pos X Y` hint format (valid/dirty/absent),
// hint write-back (replace vs append, idempotency, round-trip AST equality,
// formatter stability) and hint override inside the layout.
#include <gtest/gtest.h>

#include <string>

#include "pgg/pgg.h"
#include "pgg/src/graph.h"
#include "pgg/src/layout.h"

namespace {

void expectNoErrors(const pgg::Document& d) {
    for (const pgg::Diagnostic& diag : d.diagnostics)
        if (!diag.isWarning) ADD_FAILURE() << diag.code << " " << diag.message;
}

const pgg::GraphNode* definingNode(const pgg::GraphScope& g, const std::string& name) {
    for (const pgg::GraphNode& n : g.nodes)
        for (const std::string& out : n.outputs)
            if (out == name) return &n;
    return nullptr;
}

std::string lineOf(const std::string& text, int32_t line) {
    size_t begin = 0;
    for (int32_t i = 1; i < line; ++i) begin = text.find('\n', begin) + 1;
    size_t end = text.find('\n', begin);
    return text.substr(begin, end == std::string::npos ? end : end - begin);
}

// --- hint parsing ------------------------------------------------------------------

TEST(Layout, HintParseValid) {
    int x = 0, y = 0;
    EXPECT_TRUE(pgg::parsePosHint("@pos 340 120", x, y));
    EXPECT_EQ(x, 340);
    EXPECT_EQ(y, 120);

    EXPECT_TRUE(pgg::parsePosHint("move here: @pos -40 +12 please", x, y));
    EXPECT_EQ(x, -40);
    EXPECT_EQ(y, 12);

    EXPECT_TRUE(pgg::parsePosHint("@pos\t5\t6", x, y));
    EXPECT_EQ(x, 5);
    EXPECT_EQ(y, 6);

    // First match wins.
    EXPECT_TRUE(pgg::parsePosHint("@pos 1 2 @pos 3 4", x, y));
    EXPECT_EQ(x, 1);
    EXPECT_EQ(y, 2);

    // A third token after two clean integers is ordinary comment text.
    EXPECT_TRUE(pgg::parsePosHint("@pos 7 8 999", x, y));
    EXPECT_EQ(x, 7);
    EXPECT_EQ(y, 8);
}

TEST(Layout, HintParseDirtyIsIgnored) {
    int x = 0, y = 0;
    EXPECT_FALSE(pgg::parsePosHint("@pos abc", x, y));
    EXPECT_FALSE(pgg::parsePosHint("@pos 1", x, y));
    EXPECT_FALSE(pgg::parsePosHint("@pos", x, y));
    EXPECT_FALSE(pgg::parsePosHint("@position 1 2", x, y));
    EXPECT_FALSE(pgg::parsePosHint("@pos 1 20px", x, y));
    EXPECT_FALSE(pgg::parsePosHint("no hint here", x, y));
    EXPECT_FALSE(pgg::parsePosHint("", x, y));
}

// --- hint write-back ------------------------------------------------------------------

const std::string kHinted =
    "g = rng_from_seed(1)\n"
    "base = ico_sphere(subdiv = 1, radius = 1.0)\n"
    "n = fbm(scale = 2.0, rng = g)  # @pos 10 20\n"
    "rock = set_position(base, offset = @N * n * 0.35)  # keep this note\n"
    "mid = compute_normals(rock)  # scale @pos 1 2 rock\n"
    "output mid\n";

TEST(Layout, WriteBackAppendsAfterPlainLine) {
    const std::string out = pgg::applyPosHint(kHinted, 2, 500, 600);
    EXPECT_EQ(lineOf(out, 2), "base = ico_sphere(subdiv = 1, radius = 1.0)  # @pos 500 600");
    // The other lines are untouched.
    EXPECT_EQ(lineOf(out, 1), "g = rng_from_seed(1)");
    EXPECT_EQ(lineOf(out, 3), lineOf(kHinted, 3));
}

TEST(Layout, WriteBackAppendsAfterTrailingComment) {
    const std::string out = pgg::applyPosHint(kHinted, 4, 1, 2);
    EXPECT_EQ(lineOf(out, 4),
              "rock = set_position(base, offset = @N * n * 0.35)  # keep this note  # @pos 1 2");
}

TEST(Layout, WriteBackReplacesOnlyCoordinates) {
    const std::string out = pgg::applyPosHint(kHinted, 3, -5, 7);
    EXPECT_EQ(lineOf(out, 3), "n = fbm(scale = 2.0, rng = g)  # @pos -5 7");
}

TEST(Layout, WriteBackKeepsCommentTextAroundHint) {
    const std::string out = pgg::applyPosHint(kHinted, 5, 42, 43);
    EXPECT_EQ(lineOf(out, 5), "mid = compute_normals(rock)  # scale @pos 42 43 rock");
}

TEST(Layout, WriteBackIsIdempotent) {
    const std::string once = pgg::applyPosHint(kHinted, 2, 500, 600);
    const std::string twice = pgg::applyPosHint(once, 2, 500, 600);
    EXPECT_EQ(once, twice);
    const std::string replaced = pgg::applyPosHint(kHinted, 3, -5, 7);
    EXPECT_EQ(pgg::applyPosHint(replaced, 3, -5, 7), replaced);
}

TEST(Layout, WriteBackRoundTripKeepsAst) {
    pgg::Document before = pgg::parse(kHinted, "<test>");
    expectNoErrors(before);
    for (int32_t line : {1, 2, 3, 4, 5}) {
        const std::string edited = pgg::applyPosHint(kHinted, line, 11 * line, 22 * line);
        pgg::Document after = pgg::parse(edited, "<test>");
        expectNoErrors(after);
        ASSERT_TRUE(after.file);
        EXPECT_TRUE(pgg::astEqual(before.file, after.file)) << "line " << line;
        // ...and the hint is actually there.
        int x = 0, y = 0;
        const std::string l = lineOf(edited, line);
        const size_t hash = l.find('#');
        ASSERT_NE(hash, std::string::npos);
        EXPECT_TRUE(pgg::parsePosHint(l.substr(hash + 1), x, y));
        EXPECT_EQ(x, 11 * line);
        EXPECT_EQ(y, 22 * line);
    }
}

TEST(Layout, FormatterPreservesHints) {
    pgg::Document doc = pgg::parse(kHinted, "<test>");
    expectNoErrors(doc);
    const std::string fmt = pgg::format(doc.file, doc.comments);
    EXPECT_NE(fmt.find("@pos 10 20"), std::string::npos);
    EXPECT_NE(fmt.find("@pos 1 2 rock"), std::string::npos);
    // The formatted output still parses to the same AST (round-trip culture).
    pgg::Document reparsed = pgg::parse(fmt, "<test>");
    expectNoErrors(reparsed);
    EXPECT_TRUE(pgg::astEqual(doc.file, reparsed.file));
}

// --- auto-layout -------------------------------------------------------------------

TEST(Layout, LayerSanity) {
    pgg::Document doc = pgg::parse(
        "g = rng_from_seed(1)\n"
        "base = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "n = fbm(scale = 2.0, rng = g)\n"
        "rock = set_position(base, offset = @N * n * 0.35)\n"
        "m = compute_normals(rock)\n"
        "output m\n",
        "<test>");
    expectNoErrors(doc);
    pgg::GraphProject p = pgg::buildGraph(doc);
    pgg::layoutProject(p);

    // Every wire flows strictly left-to-right outside state loops.
    for (const pgg::GraphEdge& e : p.top.edges) {
        if (e.loop) continue;
        EXPECT_LT(p.top.nodes[e.fromNode].layer, p.top.nodes[e.toNode].layer);
    }
    EXPECT_EQ(definingNode(p.top, "g")->layer, 0);
    EXPECT_EQ(definingNode(p.top, "base")->layer, 0);
    EXPECT_EQ(definingNode(p.top, "n")->layer, 1);
    EXPECT_EQ(definingNode(p.top, "rock")->layer, 2);
    EXPECT_EQ(definingNode(p.top, "m")->layer, 3);
    // Same-layer nodes get distinct rows.
    EXPECT_NE(definingNode(p.top, "g")->y, definingNode(p.top, "base")->y);
}

TEST(Layout, DeterministicAcrossRuns) {
    const std::string src =
        "param seed: int = 42\n"
        "g = rng_from_seed(seed)\n"
        "pts = point_cloud(count = 4, bounds = (1, 1, 1), rng = g)\n"
        "settled = repeat (pts, iterations = 3) |state| {\n"
        "    d = distance_to(pts)\n"
        "    state = set_position(state, offset = vec3(0, -1, 0) * d)\n"
        "}\n"
        "m = compute_normals(settled)\n"
        "tap stats: m\n"
        "output m\n";
    pgg::GraphProject a = pgg::buildGraph(pgg::parse(src, "<test>"));
    pgg::GraphProject b = pgg::buildGraph(pgg::parse(src, "<test>"));
    pgg::layoutProject(a);
    pgg::layoutProject(b);
    ASSERT_EQ(a.top.nodes.size(), b.top.nodes.size());
    for (size_t i = 0; i < a.top.nodes.size(); ++i) {
        EXPECT_EQ(a.top.nodes[i].x, b.top.nodes[i].x) << "node " << i;
        EXPECT_EQ(a.top.nodes[i].y, b.top.nodes[i].y) << "node " << i;
        EXPECT_EQ(a.top.nodes[i].layer, b.top.nodes[i].layer) << "node " << i;
    }
    ASSERT_EQ(a.top.zones.size(), b.top.zones.size());
    for (size_t i = 0; i < a.top.zones.size(); ++i) {
        EXPECT_EQ(a.top.zones[i].x, b.top.zones[i].x);
        EXPECT_EQ(a.top.zones[i].y, b.top.zones[i].y);
        EXPECT_EQ(a.top.zones[i].w, b.top.zones[i].w);
        EXPECT_EQ(a.top.zones[i].h, b.top.zones[i].h);
    }
}

TEST(Layout, ZoneFrameWrapsBodyAndFollowsInputs) {
    pgg::Document doc = pgg::parse(
        "g = rng_from_seed(1)\n"
        "pts = point_cloud(count = 4, bounds = (1, 1, 1), rng = g)\n"
        "settled = repeat (pts, iterations = 3) |state| {\n"
        "    state = set_position(state, offset = vec3(0, -1, 0))\n"
        "}\n"
        "m = compute_normals(settled)\n"
        "output m\n",
        "<test>");
    expectNoErrors(doc);
    pgg::GraphProject p = pgg::buildGraph(doc);
    pgg::layoutProject(p);
    ASSERT_EQ(p.top.zones.size(), 1u);
    const pgg::GraphZone& z = p.top.zones[0];
    EXPECT_GT(z.w, 0.0f);
    EXPECT_GT(z.h, 0.0f);

    // The frame sits right of its input and left of the downstream node.
    const pgg::GraphNode* pts = definingNode(p.top, "pts");
    const pgg::GraphNode* m = definingNode(p.top, "m");
    ASSERT_TRUE(pts && m);
    EXPECT_GT(z.x, pts->x);
    EXPECT_LT(z.x + z.w, m->x);

    // Every member (ports included) lies inside the frame.
    for (const int id : z.members) {
        const pgg::GraphNode& n = p.top.nodes[id];
        EXPECT_GE(n.x, z.x) << n.name;
        EXPECT_LE(n.x, z.x + z.w) << n.name;
        EXPECT_GE(n.y, z.y) << n.name;
        EXPECT_LE(n.y, z.y + z.h) << n.name;
    }
    // The header rides the frame's title strip.
    const pgg::GraphNode& header = p.top.nodes[z.header];
    EXPECT_GE(header.x, z.x);
    EXPECT_LE(header.x, z.x + z.w);
    EXPECT_GE(header.y, z.y);
    EXPECT_LE(header.y, z.y + z.h);
}

TEST(Layout, HintOverridesLayoutPosition) {
    pgg::Document doc = pgg::parse(
        "a = ico_sphere(subdiv = 1, radius = 1.0)  # @pos 700 300\n"
        "b = compute_normals(a)\n"
        "output b\n",
        "<test>");
    expectNoErrors(doc);
    pgg::GraphProject p = pgg::buildGraph(doc);
    pgg::layoutProject(p);
    const pgg::GraphNode* a = definingNode(p.top, "a");
    const pgg::GraphNode* b = definingNode(p.top, "b");
    ASSERT_TRUE(a && b);
    EXPECT_EQ(a->x, 700.0f);
    EXPECT_EQ(a->y, 300.0f);
    // The layer assignment is unaffected; the auto-laid node stays on-grid.
    EXPECT_EQ(a->layer, 0);
    EXPECT_EQ(b->layer, 1);
    EXPECT_NE(b->x, 0.0f);
}

TEST(Layout, TowerProjectLaysOutDeterministically) {
    const std::string path = std::string(PGG_CORPUS_DIR) + "/tower.pgg";
    pgg::GraphProject a = pgg::buildGraph(pgg::parseFile(path));
    pgg::GraphProject b = pgg::buildGraph(pgg::parseFile(path));
    pgg::layoutProject(a);
    pgg::layoutProject(b);
    ASSERT_EQ(a.instanceScopes.size(), b.instanceScopes.size());
    for (size_t s = 0; s < a.instanceScopes.size(); ++s) {
        const pgg::GraphScope& sa = a.instanceScopes[s];
        const pgg::GraphScope& sb = b.instanceScopes[s];
        ASSERT_EQ(sa.nodes.size(), sb.nodes.size());
        for (size_t i = 0; i < sa.nodes.size(); ++i) {
            EXPECT_EQ(sa.nodes[i].x, sb.nodes[i].x) << "scope " << s << " node " << i;
            EXPECT_EQ(sa.nodes[i].y, sb.nodes[i].y) << "scope " << s << " node " << i;
        }
    }
}

}  // namespace
