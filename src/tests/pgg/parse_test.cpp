// End-to-end parse tests: source text -> AST + diagnostics.
#include <gtest/gtest.h>

#include "pgg/pgg.h"
#include "pgg/src/ast.h"
#include "pgg/src/graph.h"

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

// --- error-recovery hardening (spec §19 v1.7) ---------------------------------
//
// ANTLR error recovery ends a rule without its action firing; since v1.7 every
// pointer-valued `returns` initializes to nullptr and the compositor turns
// nullptr inputs into ErrorExpr sentinels, so downstream walkers (validate,
// dumpAst, buildGraph) never see a wild pointer. These tests pin the class:
// malformed sources parse with diagnostics, a partial AST and no crash.

// Recursive invariant: no nullptr in any expression slot of the AST
// (CallArg::value, VecLit/ListLit::elems, Binding::value, zone heads, contracts).
void expectNoNullExprs(const pgg::Expr* e) {
    ASSERT_TRUE(e != nullptr);
    switch (e->kind) {
        case pgg::NodeKind::Paren:
            expectNoNullExprs(static_cast<const pgg::Paren*>(e)->inner);
            break;
        case pgg::NodeKind::Unary:
            expectNoNullExprs(static_cast<const pgg::Unary*>(e)->operand);
            break;
        case pgg::NodeKind::Binary: {
            const auto* b = static_cast<const pgg::Binary*>(e);
            expectNoNullExprs(b->lhs);
            expectNoNullExprs(b->rhs);
            break;
        }
        case pgg::NodeKind::Ternary: {
            const auto* t = static_cast<const pgg::Ternary*>(e);
            expectNoNullExprs(t->cond);
            expectNoNullExprs(t->thenExpr);
            expectNoNullExprs(t->elseExpr);
            break;
        }
        case pgg::NodeKind::Call: {
            const auto* c = static_cast<const pgg::Call*>(e);
            for (const pgg::CallArg& a : c->args) expectNoNullExprs(a.value);
            break;
        }
        case pgg::NodeKind::VecLit:
            for (const pgg::Expr* el : static_cast<const pgg::VecLit*>(e)->elems)
                expectNoNullExprs(el);
            break;
        case pgg::NodeKind::ListLit:
            for (const pgg::Expr* el : static_cast<const pgg::ListLit*>(e)->elems)
                expectNoNullExprs(el);
            break;
        default:
            break;  // literals, idents, attr refs, error sentinels: no child slots
    }
}

void expectNoNullExprs(const pgg::Stmt* s) {
    ASSERT_TRUE(s != nullptr);
    switch (s->kind) {
        case pgg::NodeKind::Binding:
            expectNoNullExprs(static_cast<const pgg::Binding*>(s)->value);
            break;
        case pgg::NodeKind::RepeatZone: {
            const auto* z = static_cast<const pgg::RepeatZone*>(s);
            expectNoNullExprs(z->value);
            expectNoNullExprs(z->iterations);
            for (const pgg::Stmt* b : z->body) expectNoNullExprs(b);
            break;
        }
        case pgg::NodeKind::ForeachZone: {
            const auto* z = static_cast<const pgg::ForeachZone*>(s);
            expectNoNullExprs(z->collection);
            for (const pgg::Stmt* b : z->body) expectNoNullExprs(b);
            break;
        }
        case pgg::NodeKind::Expect:
        case pgg::NodeKind::Ensure: {
            const auto* c = static_cast<const pgg::ContractStmt*>(s);
            expectNoNullExprs(c->attr);
            expectNoNullExprs(c->cond);
            break;
        }
        default:
            break;  // tap: no expression slots
    }
}

void expectNoNullExprs(const pgg::File* f) {
    ASSERT_TRUE(f != nullptr);
    for (const pgg::Node* item : f->items) {
        ASSERT_TRUE(item != nullptr);
        switch (item->kind) {
            case pgg::NodeKind::ParamDecl: {
                // A default is an optional (legal nullptr) slot; when present
                // it still holds a real expression.
                const auto* p = static_cast<const pgg::ParamDecl*>(item);
                if (p->hasDefault && p->def) expectNoNullExprs(p->def);
                break;
            }
            case pgg::NodeKind::Def: {
                const auto* d = static_cast<const pgg::Def*>(item);
                for (const pgg::ContractStmt* e : d->expects) expectNoNullExprs(e);
                for (const pgg::Stmt* s : d->body) expectNoNullExprs(s);
                for (const pgg::ContractStmt* e : d->ensures) expectNoNullExprs(e);
                break;
            }
            case pgg::NodeKind::Binding:
            case pgg::NodeKind::RepeatZone:
            case pgg::NodeKind::ForeachZone:
            case pgg::NodeKind::Tap:
                expectNoNullExprs(static_cast<const pgg::Stmt*>(item));
                break;
            default:
                break;  // import/output: no expression slots
        }
    }
}

TEST(Parse, NegativeVecLiteralParses) {
    // Since v1.9 vec literal components are signed numeric literals:
    // `(-0.5, 0, 0)` is a valid vec3 (the original SIGSEGV repro, spec §19
    // v1.7, became the motivating case for lifting the non-negative rule).
    pgg::Document doc = pgg::parse(
        "param size: f32 = 1\n"
        "moved = translate(box(size = size), by = (-0.5, 0, 0))\n"
        "output moved\n");
    EXPECT_FALSE(doc.hasErrors());
    ASSERT_TRUE(doc.file != nullptr);
    EXPECT_FALSE(pgg::dumpAst(doc.file).empty());
    expectNoNullExprs(doc.file);
}

TEST(Parse, ParenthesizedNumberStaysScalar) {
    // vec_literal requires at least one comma, so `(1)` and `(-1)` are
    // parenthesized scalar expressions, not one-element vecs (which the
    // typechecker would reject: vector literals are vec2..vec4).
    pgg::Document doc = pgg::parse("a = (-1)\nb = (2.5)\noutput a\noutput b\n");
    EXPECT_FALSE(doc.hasErrors());
    ASSERT_TRUE(doc.file != nullptr);
    expectNoNullExprs(doc.file);
}

TEST(Parse, MemberAccessOnCallDoesNotCrash) {
    // Pre-existing defect logged in spec §19 v1.3, fixed with v1.7.
    pgg::Document doc = pgg::parse("a = f(x).y\n");
    EXPECT_TRUE(doc.hasErrors());
    ASSERT_TRUE(doc.file != nullptr);
    EXPECT_FALSE(pgg::dumpAst(doc.file).empty());
    expectNoNullExprs(doc.file);
}

TEST(Parse, MalformedSnippetsDoNotCrash) {
    const std::string snippets[] = {
        "a = f(x, )\n",             // missing arg after comma
        "a = (1, , 2)\n",           // missing vec component
        "a = [1, , 2]\n",           // missing list element
        "param a: geo< = 1\n",      // truncated generic type
        "def f(x: geo) -> (o: geo) {\n"  // broken expect (missing attr ref)
        "    expect x has\n"
        "    o = x\n"
        "}\n",
        "r = repeat (x, iterations = ) |s| {\n"  // broken repeat header
        "    s = x\n"
        "}\n",
        "a = f(1 +\n",              // file cut mid-expression
        "a = f(\n",                 // unclosed call
        "def f(x: ) -> (o: geo) {\n"  // missing param type
        "}\n",
        "param a: vec3? = (1, , 3)\n",  // hole in a default vec
    };
    for (const std::string& src : snippets) {
        SCOPED_TRACE(src);
        pgg::Document doc = pgg::parse(src);
        EXPECT_TRUE(doc.hasErrors());
        ASSERT_TRUE(doc.file != nullptr);
        pgg::dumpAst(doc.file);  // must not crash on the partial AST
        expectNoNullExprs(doc.file);
    }
}

TEST(Parse, GraphBuildsOnBrokenDoc) {
    // PggViewer derives the projection without gating on diagnostics.
    pgg::Document doc = pgg::parse(
        "param size: float = 1\n"
        "out geo = translate(box(size = size), by = (-0.5, 0, 0))\n"
        "a = f(x).y\n");
    EXPECT_TRUE(doc.hasErrors());
    ASSERT_TRUE(doc.file != nullptr);
    const pgg::GraphProject gp = pgg::buildGraph(doc, nullptr);  // must not crash
    (void)gp;
}

}  // namespace
