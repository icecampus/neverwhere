// E8 graph projection tests (spec §10, acceptance §15 E8): names become nodes
// and uses become wires (incl. multi-output pins and the enum-literal rule),
// chips (param/output/import/tap), zone subgraphs with iteration ports and
// the state loop, def-call nodes with expansion-identical instance numbering
// — cross-checked against FlatProgram::instances on tower.pgg, synthetic
// nested def graphs and import-closure sources — and graceful projection of
// broken files.
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "pgg/pgg.h"
#include "pgg/src/eval/expand.h"
#include "pgg/src/eval/modules.h"
#include "pgg/src/graph.h"

namespace {

void expectNoErrors(const pgg::Document& d) {
    for (const pgg::Diagnostic& diag : d.diagnostics)
        if (!diag.isWarning) ADD_FAILURE() << diag.code << " " << diag.message;
}

const pgg::GraphNode* findNode(const pgg::GraphScope& g, pgg::GraphNode::Kind kind,
                               const std::string& op) {
    for (const pgg::GraphNode& n : g.nodes)
        if (n.kind == kind && n.op == op) return &n;
    return nullptr;
}

// The node defining a name (one of its out pins).
const pgg::GraphNode* definingNode(const pgg::GraphScope& g, const std::string& name) {
    for (const pgg::GraphNode& n : g.nodes)
        for (const std::string& out : n.outputs)
            if (out == name) return &n;
    return nullptr;
}

const pgg::GraphEdge* findEdge(const pgg::GraphScope& g, int from, int to) {
    for (const pgg::GraphEdge& e : g.edges)
        if (e.fromNode == from && e.toNode == to) return &e;
    return nullptr;
}

int countEdges(const pgg::GraphScope& g, int from, int to) {
    int n = 0;
    for (const pgg::GraphEdge& e : g.edges)
        if (e.fromNode == from && e.toNode == to) n += 1;
    return n;
}

pgg::GraphProject build(const std::string& src) {
    pgg::Document doc = pgg::parse(src, "<test>");
    expectNoErrors(doc);
    return pgg::buildGraph(doc);
}

// --- names → nodes, uses → wires ------------------------------------------------

TEST(Graph, NamesBecomeNodesUsesBecomeWires) {
    pgg::GraphProject p = build(
        "g = rng_from_seed(1)\n"
        "base = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "n = fbm(scale = 2.0, rng = g)\n"
        "rock = set_position(base, offset = @N * n * 0.35)\n"
        "output rock\n");
    const pgg::GraphScope& g = p.top;

    const pgg::GraphNode* rng = definingNode(g, "g");
    const pgg::GraphNode* base = definingNode(g, "base");
    const pgg::GraphNode* n = definingNode(g, "n");
    const pgg::GraphNode* rock = definingNode(g, "rock");
    ASSERT_TRUE(rng && base && n && rock);
    EXPECT_EQ(rng->kind, pgg::GraphNode::Kind::Binding);
    EXPECT_EQ(rng->op, "rng_from_seed");
    EXPECT_EQ(base->op, "ico_sphere");
    EXPECT_EQ(n->op, "fbm");
    EXPECT_EQ(rock->op, "set_position");

    // A use in a named argument wires into that pin.
    ASSERT_EQ(n->inputs.size(), 1u);
    EXPECT_EQ(n->inputs[0], "rng");
    const pgg::GraphEdge* e = findEdge(g, rng->id, n->id);
    ASSERT_TRUE(e);
    EXPECT_EQ(e->fromPin, 0);
    EXPECT_EQ(e->toPin, 0);

    // The positional geo argument is an unlabeled pin; the field read inside
    // `offset = @N * n * 0.35` wires into the named pin.
    ASSERT_EQ(rock->inputs.size(), 2u);
    EXPECT_EQ(rock->inputs[0], "");
    EXPECT_EQ(rock->inputs[1], "offset");
    EXPECT_TRUE(findEdge(g, base->id, rock->id));
    e = findEdge(g, n->id, rock->id);
    ASSERT_TRUE(e);
    EXPECT_EQ(e->toPin, 1);

    // The declared output is a sink chip fed by the defining node.
    const pgg::GraphNode* out = findNode(g, pgg::GraphNode::Kind::Output, "output");
    ASSERT_TRUE(out);
    EXPECT_EQ(out->name, "rock");
    EXPECT_TRUE(findEdge(g, rock->id, out->id));
}

TEST(Graph, MultiOutputTargetsBecomePins) {
    pgg::GraphProject p = build(
        "m = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "yes, no = separate(m, where = index() > 20)\n"
        "out = merge(yes, no)\n"
        "output out\n");
    const pgg::GraphScope& g = p.top;
    const pgg::GraphNode* sep = definingNode(g, "yes");
    const pgg::GraphNode* out = definingNode(g, "out");
    ASSERT_TRUE(sep && out);
    ASSERT_EQ(sep->outputs.size(), 2u);
    EXPECT_EQ(sep->outputs[0], "yes");
    EXPECT_EQ(sep->outputs[1], "no");

    const pgg::GraphEdge* eYes = nullptr;
    const pgg::GraphEdge* eNo = nullptr;
    for (const pgg::GraphEdge& e : g.edges) {
        if (e.fromNode != sep->id || e.toNode != out->id) continue;
        (e.fromPin == 0 ? eYes : eNo) = &e;
    }
    ASSERT_TRUE(eYes && eNo);
    EXPECT_EQ(eYes->fromPin, 0);
    EXPECT_EQ(eNo->fromPin, 1);
    // Positional arguments are distinct unlabeled pins.
    ASSERT_EQ(out->inputs.size(), 2u);
    EXPECT_EQ(out->inputs[0], "");
    EXPECT_EQ(out->inputs[1], "");
}

TEST(Graph, EnumLiteralIdentsMakeNoPhantomWires) {
    pgg::GraphProject p = build(
        "m = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "s = compute_normals(m, mode = flat)\n"
        "d = delete(s, where = index() > 2, domain = faces)\n"
        "output d\n");
    const pgg::GraphScope& g = p.top;
    const pgg::GraphNode* m = definingNode(g, "m");
    const pgg::GraphNode* s = definingNode(g, "s");
    const pgg::GraphNode* d = definingNode(g, "d");
    ASSERT_TRUE(m && s && d);
    // Only the real reads wire: m -> s -> d -> output chip.
    EXPECT_EQ(g.edges.size(), 3u);
    EXPECT_TRUE(findEdge(g, m->id, s->id));
    EXPECT_TRUE(findEdge(g, s->id, d->id));
}

TEST(Graph, Chips) {
    pgg::GraphProject p = build(
        "param seed: int = 42\n"
        "import lib.rocks\n"
        "r = rng_from_seed(seed)\n"
        "tap stats: r\n"
        "output r\n");
    const pgg::GraphScope& g = p.top;

    const pgg::GraphNode* param = findNode(g, pgg::GraphNode::Kind::Param, "param");
    ASSERT_TRUE(param);
    EXPECT_EQ(param->name, "seed");
    const pgg::GraphNode* r = definingNode(g, "r");
    ASSERT_TRUE(r);
    EXPECT_TRUE(findEdge(g, param->id, r->id));

    const pgg::GraphNode* imp = findNode(g, pgg::GraphNode::Kind::Import, "import");
    ASSERT_TRUE(imp);
    EXPECT_EQ(imp->name, "rocks");

    const pgg::GraphNode* tap = findNode(g, pgg::GraphNode::Kind::Tap, "tap");
    ASSERT_TRUE(tap);
    EXPECT_EQ(tap->name, "stats");
    EXPECT_TRUE(findEdge(g, r->id, tap->id));
}

TEST(Graph, BrokenFileStillProjects) {
    // `ghost` is undefined (E103): no wire for it, the rest of the graph is
    // unaffected and nothing crashes.
    pgg::Document doc = pgg::parse(
        "a = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "b = set_position(a, offset = ghost * 2)\n"
        "output b\n",
        "<test>");
    pgg::GraphProject p = pgg::buildGraph(doc);
    const pgg::GraphNode* a = definingNode(p.top, "a");
    const pgg::GraphNode* b = definingNode(p.top, "b");
    ASSERT_TRUE(a && b);
    EXPECT_EQ(countEdges(p.top, a->id, b->id), 1);
    EXPECT_EQ(b->inputs.size(), 1u);  // the positional geo pin only
}

// --- zones (§5.4) -------------------------------------------------------------------

TEST(Graph, RepeatZonePortsAndStateLoop) {
    pgg::GraphProject p = build(
        "g = rng_from_seed(1)\n"
        "pts = point_cloud(count = 4, bounds = (1, 1, 1), rng = g)\n"
        "settled = repeat (pts, iterations = 3) |state| {\n"
        "    d = distance_to(pts)\n"
        "    state = set_position(state, offset = vec3(0, -1, 0) * d)\n"
        "}\n"
        "output settled\n");
    const pgg::GraphScope& g = p.top;
    ASSERT_EQ(g.zones.size(), 1u);
    const pgg::GraphZone& z = g.zones[0];
    EXPECT_EQ(z.parent, -1);

    const pgg::GraphNode* header = findNode(g, pgg::GraphNode::Kind::ZoneHeader, "repeat");
    ASSERT_TRUE(header);
    EXPECT_EQ(header->id, z.header);
    ASSERT_EQ(header->outputs.size(), 1u);
    EXPECT_EQ(header->outputs[0], "settled");

    ASSERT_EQ(z.inputPorts.size(), 1u);
    ASSERT_EQ(z.outputPorts.size(), 1u);
    const pgg::GraphNode& in = g.nodes[z.inputPorts[0]];
    const pgg::GraphNode& out = g.nodes[z.outputPorts[0]];
    EXPECT_EQ(in.kind, pgg::GraphNode::Kind::ZoneInput);
    EXPECT_EQ(out.kind, pgg::GraphNode::Kind::ZoneOutput);
    EXPECT_EQ(in.name, "state");
    EXPECT_EQ(out.name, "state");

    // The header's input value feeds the first iteration; the zone input
    // arrives from outside (pts -> header).
    const pgg::GraphNode* pts = definingNode(g, "pts");
    ASSERT_TRUE(pts);
    EXPECT_TRUE(findEdge(g, header->id, in.id));
    EXPECT_TRUE(findEdge(g, pts->id, header->id));

    // The state rebind reads the iteration-input port (not an outer name)
    // and binds the iteration-output port.
    const pgg::GraphNode* rebind = nullptr;
    for (const pgg::GraphNode& n : g.nodes)
        if (n.kind == pgg::GraphNode::Kind::Binding && n.op == "set_position") rebind = &n;
    ASSERT_TRUE(rebind);
    EXPECT_EQ(rebind->zone, z.id);  // body nodes live inside the frame
    EXPECT_TRUE(findEdge(g, in.id, rebind->id));
    EXPECT_TRUE(findEdge(g, rebind->id, out.id));

    // Outer names read in the body wire straight across the frame.
    const pgg::GraphNode* d = definingNode(g, "d");
    ASSERT_TRUE(d);
    EXPECT_EQ(d->zone, z.id);
    EXPECT_TRUE(findEdge(g, pts->id, d->id));
    EXPECT_TRUE(findEdge(g, d->id, rebind->id));

    // The state loop is a marked back edge; the final state lands at the header.
    const pgg::GraphEdge* loop = findEdge(g, out.id, in.id);
    ASSERT_TRUE(loop);
    EXPECT_TRUE(loop->loop);
    EXPECT_TRUE(findEdge(g, out.id, header->id));

    // Downstream reads of the zone target resolve to the header.
    const pgg::GraphNode* chip = findNode(g, pgg::GraphNode::Kind::Output, "output");
    ASSERT_TRUE(chip);
    EXPECT_TRUE(findEdge(g, header->id, chip->id));
}

TEST(Graph, ForeachZoneItemPort) {
    pgg::GraphProject p = build(
        "g = rng_from_seed(1)\n"
        "m = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "fr = islands(m)\n"
        "chunks = foreach piece in fr {\n"
        "    piece = smooth(piece, iterations = 1)\n"
        "}\n"
        "output chunks\n");
    const pgg::GraphScope& g = p.top;
    ASSERT_EQ(g.zones.size(), 1u);
    const pgg::GraphZone& z = g.zones[0];
    const pgg::GraphNode* header = findNode(g, pgg::GraphNode::Kind::ZoneHeader, "foreach");
    ASSERT_TRUE(header);
    ASSERT_EQ(header->outputs.size(), 1u);
    EXPECT_EQ(header->outputs[0], "chunks");

    ASSERT_EQ(z.inputPorts.size(), 1u);
    const pgg::GraphNode& in = g.nodes[z.inputPorts[0]];
    EXPECT_EQ(in.name, "piece");
    // The collection wires into the header, the header into the item port.
    const pgg::GraphNode* fr = definingNode(g, "fr");
    ASSERT_TRUE(fr);
    EXPECT_TRUE(findEdge(g, fr->id, header->id));
    EXPECT_TRUE(findEdge(g, header->id, in.id));

    // The spec pattern `piece = smooth(piece, ...)`: read before the rebind
    // resolves to the iteration input, the binding feeds the output port.
    const pgg::GraphNode* rebind = findNode(g, pgg::GraphNode::Kind::Binding, "smooth");
    ASSERT_TRUE(rebind);
    EXPECT_NE(rebind->id, in.id);
    EXPECT_TRUE(findEdge(g, in.id, rebind->id));
    const pgg::GraphNode& out = g.nodes[z.outputPorts[0]];
    EXPECT_TRUE(findEdge(g, rebind->id, out.id));
    const pgg::GraphEdge* loop = findEdge(g, out.id, in.id);
    ASSERT_TRUE(loop);
    EXPECT_TRUE(loop->loop);
}

TEST(Graph, NestedZones) {
    pgg::GraphProject p = build(
        "g = rng_from_seed(1)\n"
        "pts = point_cloud(count = 4, bounds = (1, 1, 1), rng = g)\n"
        "a = repeat (pts, iterations = 2) |s| {\n"
        "    b = repeat (s, iterations = 2) |t| {\n"
        "        t = set_position(t, offset = (0, 1, 0))\n"
        "    }\n"
        "    s = set_position(b, offset = (0, 1, 0))\n"
        "}\n"
        "output a\n");
    const pgg::GraphScope& g = p.top;
    ASSERT_EQ(g.zones.size(), 2u);
    EXPECT_EQ(g.zones[0].parent, -1);
    EXPECT_EQ(g.zones[1].parent, 0);
    ASSERT_EQ(g.zones[0].children.size(), 1u);
    EXPECT_EQ(g.zones[0].children[0], 1);

    // The inner header lives in the outer frame and reads the outer port.
    const pgg::GraphZone& inner = g.zones[1];
    const pgg::GraphNode& innerHeader = g.nodes[inner.header];
    EXPECT_EQ(innerHeader.zone, 0);
    ASSERT_EQ(g.zones[0].inputPorts.size(), 1u);
    const int outerIn = g.zones[0].inputPorts[0];
    EXPECT_TRUE(findEdge(g, outerIn, innerHeader.id));
}

// --- def calls and instance numbering (§7.7, expansion-identical) ---------------------

void expectInstancesMatchFlat(const std::string& src, const std::string& path) {
    pgg::Document doc = src.empty() ? pgg::parseFile(path) : pgg::parse(src, path);
    expectNoErrors(doc);
    ASSERT_TRUE(doc.file);

    std::vector<pgg::Diagnostic> diags;
    pgg::FlatProgram flat = pgg::expandProgram(*doc.file, nullptr, diags);
    for (const pgg::Diagnostic& d : diags)
        if (!d.isWarning) ADD_FAILURE() << "expand: " << d.code << " " << d.message;

    pgg::GraphProject proj = pgg::buildGraph(doc);
    ASSERT_EQ(proj.instancePaths.size(), flat.instances.size()) << "instance count mismatch";
    for (size_t i = 0; i < flat.instances.size(); ++i) {
        EXPECT_EQ(proj.instancePaths[i], flat.instances[i].path) << "instance " << i;
        const pgg::GraphScope* body = proj.scopeOf(flat.instances[i].path);
        ASSERT_TRUE(body) << "no body scope for " << flat.instances[i].path;
    }
}

TEST(Graph, TowerInstancePathsMatchFlatProgram) {
    const std::string path = std::string(PGG_CORPUS_DIR) + "/tower.pgg";
    pgg::Document doc = pgg::parseFile(path);
    expectNoErrors(doc);
    pgg::GraphProject p = pgg::buildGraph(doc);

    // Hardcoded expectation (§16 tower): five instances, global counters —
    // the make_rock_sdf call inside make_rock[0] continues as [1].
    const std::vector<std::string> expected = {
        "cliff_wall[0]",
        "cliff_wall[0].make_rock_sdf[0]",
        "make_rock[0]",
        "make_rock[0].make_rock_sdf[1]",
        "make_rock[0].fbm_displace[0]",
    };
    EXPECT_EQ(p.instancePaths, expected);

    // The top level shows the two def-call nodes; dive targets exist.
    const pgg::GraphNode* wall = definingNode(p.top, "wall");
    ASSERT_TRUE(wall);
    EXPECT_EQ(wall->kind, pgg::GraphNode::Kind::DefCall);
    EXPECT_EQ(wall->instanceName, "cliff_wall[0]");
    ASSERT_EQ(wall->outputs.size(), 2u);
    EXPECT_EQ(wall->outputs[0], "wall");
    EXPECT_EQ(wall->outputs[1], "anchors");
    const pgg::GraphNode* hero = definingNode(p.top, "hero0");
    ASSERT_TRUE(hero);
    EXPECT_EQ(hero->instanceName, "make_rock[0]");

    // Nested dive: make_rock[0]'s body carries the globally numbered calls.
    const pgg::GraphScope* body = p.scopeOf("make_rock[0]");
    ASSERT_TRUE(body);
    const pgg::GraphNode* sdf = definingNode(*body, "src");
    ASSERT_TRUE(sdf);
    EXPECT_EQ(sdf->instancePath, "make_rock[0].make_rock_sdf[1]");
    const pgg::GraphNode* disp = definingNode(*body, "out");
    ASSERT_TRUE(disp);
    EXPECT_EQ(disp->instancePath, "make_rock[0].fbm_displace[0]");

    // The expansion agrees with every derived path (the mandatory check).
    expectInstancesMatchFlat({}, path);
}

TEST(Graph, NestedDefNumberingMatchesExpansion) {
    const std::string src =
        "def inner(x: f32) -> (out: f32) {\n"
        "    out = x * 2\n"
        "}\n"
        "def mid(x: f32) -> (out: f32) {\n"
        "    y = inner(x)\n"
        "    out = inner(y)\n"
        "}\n"
        "a = mid(1)\n"
        "b = inner(a)\n"
        "c = mid(b)\n"
        "output b\n";
    pgg::GraphProject p = build(src);
    const std::vector<std::string> expected = {
        "mid[0]",
        "mid[0].inner[0]",
        "mid[0].inner[1]",
        "inner[2]",
        "mid[1]",
        "mid[1].inner[3]",
        "mid[1].inner[4]",
    };
    EXPECT_EQ(p.instancePaths, expected);
    expectInstancesMatchFlat(src, "<nested>");
}

TEST(Graph, ExpressionInternalDefCallsLiftToNodes) {
    const std::string src =
        "def one() -> (out: f32) {\n"
        "    out = 1\n"
        "}\n"
        "a = one() + one()\n"
        "output a\n";
    pgg::GraphProject p = build(src);
    EXPECT_EQ(p.instancePaths, (std::vector<std::string>{"one[0]", "one[1]"}));
    const pgg::GraphNode* a = definingNode(p.top, "a");
    ASSERT_TRUE(a);
    EXPECT_EQ(a->kind, pgg::GraphNode::Kind::Binding);
    EXPECT_EQ(a->op, "expr");
    // Both lifted calls wire into the consuming expression node.
    int defEdges = 0;
    for (const pgg::GraphEdge& e : p.top.edges)
        if (e.toNode == a->id && p.top.nodes[e.fromNode].kind == pgg::GraphNode::Kind::DefCall)
            defEdges += 1;
    EXPECT_EQ(defEdges, 2);
    expectInstancesMatchFlat(src, "<lift>");
}

TEST(Graph, DefCallInArgumentNumbersBeforeOuterCall) {
    const std::string src =
        "def inner(x: f32) -> (out: f32) {\n"
        "    out = x * 2\n"
        "}\n"
        "def mid(x: f32) -> (out: f32) {\n"
        "    y = inner(x)\n"
        "    out = inner(y)\n"
        "}\n"
        "a = mid(inner(3))\n"
        "output a\n";
    pgg::GraphProject p = build(src);
    // Args expand first (the expansion order): inner[0], then mid[0], then
    // mid's body calls.
    EXPECT_EQ(p.instancePaths,
              (std::vector<std::string>{"inner[0]", "mid[0]", "mid[0].inner[1]", "mid[0].inner[2]"}));
    const pgg::GraphNode* a = definingNode(p.top, "a");
    ASSERT_TRUE(a);
    EXPECT_EQ(a->kind, pgg::GraphNode::Kind::DefCall);
    EXPECT_EQ(a->instanceName, "mid[0]");
    expectInstancesMatchFlat(src, "<argnest>");
}

TEST(Graph, ImportedDefCallsMatchExpansion) {
    const std::string src =
        "import lib.rocks\n"
        "r = rng_from_seed(1)\n"
        "a = rocks.make_pebble(size = 0.5, rng = r)\n"
        "f, s = rocks.pebble_pair(size = 1.0, rng = r)\n"
        "v = rocks.scale_of(g = a)\n"
        "output f\n";
    const std::string root = std::string(PGG_CORPUS_DIR);

    pgg::Document doc = pgg::parse(src, "<import-main>");
    expectNoErrors(doc);
    std::vector<pgg::Diagnostic> diags;
    pgg::ModuleClosure closure = pgg::loadModuleClosure(*doc.file, {root}, diags);
    pgg::GraphProject p = pgg::buildGraph(doc, &closure);

    const std::vector<std::string> expected = {
        "make_pebble[0]",
        "pebble_pair[0]",
        "pebble_pair[0].make_pebble[1]",
        "scale_of[0]",
    };
    EXPECT_EQ(p.instancePaths, expected);

    // Qualified def-call nodes dive; module bodies carry their origin file.
    const pgg::GraphNode* pair = definingNode(p.top, "f");
    ASSERT_TRUE(pair);
    EXPECT_EQ(pair->kind, pgg::GraphNode::Kind::DefCall);
    EXPECT_EQ(pair->op, "rocks.pebble_pair");
    const pgg::GraphScope* body = p.scopeOf("pebble_pair[0]");
    ASSERT_TRUE(body);
    EXPECT_FALSE(body->originFile.empty());

    // Cross-check against the expansion with the same closure.
    pgg::FlatProgram flat = pgg::expandProgram(*doc.file, &closure, diags);
    ASSERT_EQ(p.instancePaths.size(), flat.instances.size());
    for (size_t i = 0; i < flat.instances.size(); ++i)
        EXPECT_EQ(p.instancePaths[i], flat.instances[i].path);
}

TEST(Graph, ModuleDefBodiesResolveTheirOwnNamespaces) {
    const std::string src =
        "import lib.outer\n"
        "v = outer.quadruple_it(x = 1.0)\n"
        "output v\n";
    const std::string root = std::string(PGG_CORPUS_DIR);

    pgg::Document doc = pgg::parse(src, "<outer-main>");
    expectNoErrors(doc);
    std::vector<pgg::Diagnostic> diags;
    pgg::ModuleClosure closure = pgg::loadModuleClosure(*doc.file, {root}, diags);
    pgg::GraphProject p = pgg::buildGraph(doc, &closure);

    // quadruple_it's body resolves inner.double_it through the module's own
    // import — the numbering continues inside the module's def body.
    EXPECT_EQ(p.instancePaths,
              (std::vector<std::string>{"quadruple_it[0]", "quadruple_it[0].double_it[0]",
                                        "quadruple_it[0].double_it[1]"}));
    pgg::FlatProgram flat = pgg::expandProgram(*doc.file, &closure, diags);
    ASSERT_EQ(p.instancePaths.size(), flat.instances.size());
    for (size_t i = 0; i < flat.instances.size(); ++i)
        EXPECT_EQ(p.instancePaths[i], flat.instances[i].path);
}

TEST(Graph, DefBodyScopeHasParamAndOutputChips) {
    pgg::GraphProject p = build(
        "def twice(x: f32) -> (out: f32) {\n"
        "    out = x * 2\n"
        "}\n"
        "a = twice(21)\n"
        "output a\n");
    const pgg::GraphScope* body = p.scopeOf("twice[0]");
    ASSERT_TRUE(body);
    const pgg::GraphNode* param = findNode(*body, pgg::GraphNode::Kind::Param, "param");
    ASSERT_TRUE(param);
    EXPECT_EQ(param->name, "x");
    const pgg::GraphNode* out = definingNode(*body, "out");
    ASSERT_TRUE(out);
    EXPECT_TRUE(findEdge(*body, param->id, out->id));
    const pgg::GraphNode* chip = findNode(*body, pgg::GraphNode::Kind::Output, "output");
    ASSERT_TRUE(chip);
    EXPECT_TRUE(findEdge(*body, out->id, chip->id));
}

TEST(Graph, HintAttachesToBindingNode) {
    pgg::GraphProject p = build(
        "a = ico_sphere(subdiv = 1, radius = 1.0)  # @pos 340 120\n"
        "b = compute_normals(a)  # move later: @pos -40 12 done\n"
        "c = merge(a, b)\n"
        "output c\n");
    const pgg::GraphNode* a = definingNode(p.top, "a");
    const pgg::GraphNode* b = definingNode(p.top, "b");
    const pgg::GraphNode* c = definingNode(p.top, "c");
    ASSERT_TRUE(a && b && c);
    EXPECT_TRUE(a->hasHint);
    EXPECT_EQ(a->hintX, 340.0f);
    EXPECT_EQ(a->hintY, 120.0f);
    EXPECT_TRUE(b->hasHint);
    EXPECT_EQ(b->hintX, -40.0f);
    EXPECT_EQ(b->hintY, 12.0f);
    EXPECT_FALSE(c->hasHint);
}

}  // namespace
