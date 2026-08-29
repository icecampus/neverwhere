// End-to-end parse tests: source text -> AST + diagnostics.
#include <gtest/gtest.h>

#include "pgg/pgg.h"

namespace {

bool hasCode(const pgg::Document& doc, const std::string& code) {
    for (const pgg::Diagnostic& d : doc.diagnostics)
        if (d.code == code) return true;
    return false;
}

const pgg::Node* itemAt(const pgg::Document& doc, size_t i) {
    return doc.file->items.at(i);
}

TEST(Parse, EmptyAndCommentOnly) {
    pgg::Document doc = pgg::parse("# just a comment\n\n# another\n");
    EXPECT_FALSE(doc.hasErrors());
    ASSERT_TRUE(doc.file != nullptr);
    EXPECT_TRUE(doc.file->items.empty());
}

TEST(Parse, KitchenSink) {
    const std::string src =
        "import rock_builders as rb @ 1.2\n"
        "param world_seed: int\n"
        "\n"
        "def make_rock(seed: rng, size: f32 = 1.0) -> (rock: geo<mesh>) {\n"
        "    \"\"\"Builds one rock.\"\"\"\n"
        "    expect seed has @rng\n"
        "    shape = sdf_sphere(radius = size, at = (0, 0, 0))\n"
        "    detail = @h > 0.5 ? 1 : 2\n"
        "    rock = mesh_from_sdf(displace(shape, fbm(seed = seed) * 0.1 * detail))\n"
        "    ensure rock has @P : \"rock must have points\"\n"
        "}\n"
        "rock = make_rock(seed = rng_from_seed(world_seed))\n"
        "pts = scatter(rock, density = 4)\n"
        "result = repeat (pts, iterations = 3) |cur| {\n"
        "    cur = relax(cur)\n"
        "}\n"
        "pieces = fracture(rock, count = 8)\n"
        "out = foreach piece in pieces {\n"
        "    chunk = piece\n"
        "    tap mid: chunk[0]\n"
        "}\n"
        "output result\n"
        "output out\n";
    pgg::Document doc = pgg::parse(src);
    ASSERT_FALSE(doc.hasErrors());
    for (const pgg::Diagnostic& d : doc.diagnostics) ADD_FAILURE() << d.message;
    ASSERT_TRUE(doc.file != nullptr);

    ASSERT_EQ(doc.file->items.size(), 10u);
    EXPECT_EQ(itemAt(doc, 0)->kind, pgg::NodeKind::Import);
    EXPECT_EQ(itemAt(doc, 1)->kind, pgg::NodeKind::ParamDecl);
    EXPECT_EQ(itemAt(doc, 2)->kind, pgg::NodeKind::Def);

    const auto* im = static_cast<const pgg::Import*>(itemAt(doc, 0));
    EXPECT_EQ(im->alias, "rb");
    EXPECT_TRUE(im->hasVersion);
    EXPECT_EQ(im->version, "1.2");

    const auto* def = static_cast<const pgg::Def*>(itemAt(doc, 2));
    EXPECT_TRUE(def->hasDoc);
    ASSERT_EQ(def->expects.size(), 1u);
    EXPECT_TRUE(def->expects[0]->attrForm);
    ASSERT_EQ(def->ensures.size(), 1u);
    EXPECT_TRUE(def->ensures[0]->hasMessage);
    ASSERT_EQ(def->body.size(), 3u);

    // ternary in `detail = @h > 0.5 ? 1 : 2`
    const auto* detail = static_cast<const pgg::Binding*>(def->body[1]);
    ASSERT_EQ(detail->value->kind, pgg::NodeKind::Ternary);

    // precedence: fbm(...) * 0.1 * detail is (fbm * 0.1) * detail
    const auto* rock = static_cast<const pgg::Binding*>(def->body[2]);
    const auto* meshCall = static_cast<const pgg::Call*>(rock->value);
    const auto* displace = static_cast<const pgg::Call*>(meshCall->args[0].value);
    const auto* mul = static_cast<const pgg::Binary*>(displace->args[1].value);
    EXPECT_EQ(mul->op, "*");
    ASSERT_EQ(mul->lhs->kind, pgg::NodeKind::Binary);

    const auto* zone = static_cast<const pgg::RepeatZone*>(itemAt(doc, 5));
    ASSERT_EQ(zone->state.names.size(), 1u);
    EXPECT_EQ(zone->state.names[0], "cur");
    ASSERT_EQ(zone->body.size(), 1u);

    const auto* fe = static_cast<const pgg::ForeachZone*>(itemAt(doc, 7));
    EXPECT_EQ(fe->item, "piece");
    ASSERT_EQ(fe->body.size(), 2u);
    const auto* tap = static_cast<const pgg::Tap*>(fe->body[1]);
    EXPECT_TRUE(tap->hasLabel);
    ASSERT_EQ(tap->path.size(), 2u);
    EXPECT_TRUE(tap->path[1].isIndex);
    EXPECT_EQ(tap->path[1].index, 0);

    EXPECT_EQ(itemAt(doc, 8)->kind, pgg::NodeKind::OutputDecl);
    EXPECT_EQ(itemAt(doc, 9)->kind, pgg::NodeKind::OutputDecl);
}

TEST(Parse, TypeForms) {
    const std::string src =
        "param a: geo\n"
        "param b: geo<points>\n"
        "param c: field<vec3>\n"
        "param d: f32?\n"
        "param e: string[]\n"
        "param f: enum {soft, hard} = soft\n";
    pgg::Document doc = pgg::parse(src);
    ASSERT_FALSE(doc.hasErrors());
    for (const pgg::Diagnostic& d : doc.diagnostics) ADD_FAILURE() << d.message;
    ASSERT_EQ(doc.file->items.size(), 6u);
    const auto* f = static_cast<const pgg::ParamDecl*>(doc.file->items[5]);
    EXPECT_EQ(f->type->base, "enum");
    ASSERT_TRUE(f->hasDefault);
    EXPECT_EQ(f->def->kind, pgg::NodeKind::EnumLit);  // bare ident in default position
}

TEST(Parse, ListLiteral) {
    const std::string src =
        "a = 1\n"
        "b = 2\n"
        "xs = [a, b]\n"
        "nested = [f(a), [a, b], 3,]\n"
        "empty = []\n"
        "output xs\n"
        "output nested\n"
        "output empty\n";
    pgg::Document doc = pgg::parse(src);
    ASSERT_FALSE(doc.hasErrors());
    for (const pgg::Diagnostic& d : doc.diagnostics) ADD_FAILURE() << d.code << " " << d.message;
    ASSERT_EQ(doc.file->items.size(), 8u);

    const auto* xs = static_cast<const pgg::Binding*>(itemAt(doc, 2));
    ASSERT_EQ(xs->value->kind, pgg::NodeKind::ListLit);
    const auto* list = static_cast<const pgg::ListLit*>(xs->value);
    ASSERT_EQ(list->elems.size(), 2u);
    EXPECT_EQ(list->elems[0]->kind, pgg::NodeKind::Ident);
    EXPECT_EQ(list->elems[1]->kind, pgg::NodeKind::Ident);

    // nested lists, arbitrary expressions and a trailing comma
    const auto* nested = static_cast<const pgg::Binding*>(itemAt(doc, 3));
    ASSERT_EQ(nested->value->kind, pgg::NodeKind::ListLit);
    const auto* nl = static_cast<const pgg::ListLit*>(nested->value);
    ASSERT_EQ(nl->elems.size(), 3u);
    EXPECT_EQ(nl->elems[0]->kind, pgg::NodeKind::Call);
    EXPECT_EQ(nl->elems[1]->kind, pgg::NodeKind::ListLit);
    EXPECT_EQ(nl->elems[2]->kind, pgg::NodeKind::NumberLit);

    const auto* empty = static_cast<const pgg::Binding*>(itemAt(doc, 4));
    ASSERT_EQ(empty->value->kind, pgg::NodeKind::ListLit);
    EXPECT_TRUE(static_cast<const pgg::ListLit*>(empty->value)->elems.empty());
}

TEST(Parse, UnclosedBraceYieldsE101AndPartialAst) {
    const std::string src =
        "a = 1\n"
        "def f() -> (o: geo) {\n"
        "    o = a\n";
    pgg::Document doc = pgg::parse(src);
    EXPECT_TRUE(doc.hasErrors());
    EXPECT_TRUE(hasCode(doc, "E101") || hasCode(doc, "E100"));
    // partial AST still exists for the linter
    ASSERT_TRUE(doc.file != nullptr);
}

TEST(Parse, WrongContextualKeywordIsE100) {
    pgg::Document doc = pgg::parse(
        "def f(x: geo) -> (o: geo) {\n"
        "    expect x was @P\n"
        "    o = x\n"
        "}\n");
    EXPECT_TRUE(hasCode(doc, "E100"));
    bool found = false;
    for (const pgg::Diagnostic& d : doc.diagnostics)
        if (d.message.find("'has'") != std::string::npos) found = true;
    EXPECT_TRUE(found);
}

TEST(Parse, UnknownEscapeIsE100) {
    pgg::Document doc = pgg::parse("a = f(key = \"x\\q\")\n");
    EXPECT_TRUE(hasCode(doc, "E100"));
}

}  // namespace
