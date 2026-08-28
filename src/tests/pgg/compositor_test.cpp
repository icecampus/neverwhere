// Direct tests of the grammar compositor — no ANTLR involved (spec §13.2:
// the compositor is plain C++ and unit-testable without the parser).
#include <gtest/gtest.h>

#include "pgg/src/compositor.h"

namespace {

pgg::Span sp(int32_t line) {
    pgg::Span s;
    s.line = line;
    s.endLine = line;
    return s;
}

TEST(Compositor, FixedArityAssembly) {
    pgg::GrammarCompositor gc;
    pgg::Expr* one = gc.newNumber("1", sp(1));
    pgg::Expr* two = gc.newNumber("2", sp(1));
    pgg::Expr* sum = gc.newBinary("+", one, two, sp(1));
    ASSERT_EQ(sum->kind, pgg::NodeKind::Binary);
    auto* bin = static_cast<pgg::Binary*>(sum);
    EXPECT_EQ(bin->op, "+");
    EXPECT_EQ(bin->lhs->kind, pgg::NodeKind::NumberLit);
    EXPECT_EQ(bin->rhs->kind, pgg::NodeKind::NumberLit);

    pgg::CallArg a;
    a.name = "radius";
    a.hasName = true;
    a.value = one;
    pgg::Expr* call = gc.newCall({"sdf_sphere"}, {a}, sp(2));
    auto* c = static_cast<pgg::Call*>(call);
    EXPECT_EQ(c->path, std::vector<std::string>{"sdf_sphere"});
    ASSERT_EQ(c->args.size(), 1u);
    EXPECT_TRUE(c->args[0].hasName);
    EXPECT_EQ(c->args[0].value, one);
}

TEST(Compositor, NullChildrenBecomeErrorSentinels) {
    pgg::GrammarCompositor gc;
    pgg::Expr* bin = gc.newBinary("+", nullptr, nullptr, sp(1));
    EXPECT_EQ(bin->kind, pgg::NodeKind::Binary);
    EXPECT_EQ(static_cast<pgg::Binary*>(bin)->lhs->kind, pgg::NodeKind::ErrorExpr);
    EXPECT_EQ(static_cast<pgg::Binary*>(bin)->rhs->kind, pgg::NodeKind::ErrorExpr);
}

TEST(Compositor, DefFrameAssemblesBodyInOrder) {
    pgg::GrammarCompositor gc;
    gc.beginDef("f", sp(1), {}, {gc.newOutDecl("o", gc.newTypeName("geo", sp(1)))}, sp(1));
    gc.defDoc("\"\"\"doc text\"\"\"", sp(2));
    gc.addExpect(gc.newContract(pgg::NodeKind::Expect, false, {}, nullptr,
                                gc.newBool("true", sp(3)), {}, false, sp(3)));
    gc.addNode(gc.newBinding({{"x"}, {sp(4)}}, gc.newNumber("1", sp(4)), sp(4)));
    gc.addNode(nullptr);  // skipped
    gc.addEnsure(gc.newContract(pgg::NodeKind::Ensure, false, {}, nullptr,
                                gc.newBool("true", sp(5)), {}, false, sp(5)));
    pgg::Def* d = gc.endDef(sp(6));
    ASSERT_TRUE(d != nullptr);
    EXPECT_EQ(d->name, "f");
    EXPECT_TRUE(d->hasDoc);
    EXPECT_EQ(d->docstring, "doc text");
    ASSERT_EQ(d->expects.size(), 1u);
    ASSERT_EQ(d->body.size(), 1u);
    ASSERT_EQ(d->ensures.size(), 1u);
    ASSERT_EQ(d->outputs.size(), 1u);
    EXPECT_EQ(d->outputs[0].name, "o");
    EXPECT_TRUE(gc.diagnostics().empty());
}

TEST(Compositor, ExpectAfterBodyIsAnError) {
    pgg::GrammarCompositor gc;
    gc.beginDef("f", sp(1), {}, {}, sp(1));
    gc.addNode(gc.newBinding({{"x"}, {sp(2)}}, gc.newNumber("1", sp(2)), sp(2)));
    gc.addExpect(gc.newContract(pgg::NodeKind::Expect, false, {}, nullptr,
                                gc.newBool("true", sp(3)), {}, false, sp(3)));
    ASSERT_EQ(gc.diagnostics().size(), 1u);
    EXPECT_NE(gc.diagnostics()[0].message.find("precede"), std::string::npos);
}

TEST(Compositor, UnbalancedFramesAreDiscardedWithDiagnostic) {
    pgg::GrammarCompositor gc;
    gc.beginFile();
    gc.beginDef("f", sp(1), {}, {}, sp(1));
    // repeat begins but endRepeat never fires (error recovery aborted it)
    gc.beginRepeat({{"r"}, {sp(2)}}, gc.newNumber("1", sp(2)), gc.newNumber("3", sp(2)), {},
                   sp(2));
    pgg::Def* d = gc.endDef(sp(5));  // must discard the stray repeat frame
    ASSERT_TRUE(d != nullptr);
    EXPECT_EQ(d->name, "f");
    EXPECT_TRUE(d->body.empty());
    gc.addNode(d);  // the file rule does this via its def_stmt trigger
    pgg::File* f = gc.endFile(sp(6));
    ASSERT_TRUE(f != nullptr);
    ASSERT_EQ(f->items.size(), 1u);
    EXPECT_EQ(f->items[0]->kind, pgg::NodeKind::Def);
    // one diagnostic about the discarded unclosed repeat
    ASSERT_EQ(gc.diagnostics().size(), 1u);
    EXPECT_NE(gc.diagnostics()[0].message.find("unclosed"), std::string::npos);
}

TEST(Compositor, UnclosedDefAtEndFileIsReported) {
    pgg::GrammarCompositor gc;
    gc.beginFile();
    gc.addNode(gc.newBinding({{"x"}, {sp(1)}}, gc.newNumber("1", sp(1)), sp(1)));
    gc.beginDef("f", sp(2), {}, {}, sp(2));  // never closed
    pgg::File* f = gc.endFile(sp(3));
    ASSERT_TRUE(f != nullptr);
    ASSERT_EQ(f->items.size(), 1u);  // only the binding; the def is discarded
    EXPECT_EQ(f->items[0]->kind, pgg::NodeKind::Binding);
    ASSERT_EQ(gc.diagnostics().size(), 1u);
    EXPECT_NE(gc.diagnostics()[0].message.find("unclosed"), std::string::npos);
}

TEST(Compositor, StringEscapes) {
    pgg::GrammarCompositor gc;
    pgg::Expr* s = gc.newString("\"a\\nb\\\\c\\\"\"", sp(1));
    EXPECT_EQ(static_cast<pgg::StringLit*>(s)->value, "a\nb\\c\"");
    EXPECT_TRUE(gc.diagnostics().empty());

    pgg::Expr* bad = gc.newString("\"x\\q\"", sp(2));
    EXPECT_EQ(static_cast<pgg::StringLit*>(bad)->value, "xq");
    ASSERT_EQ(gc.diagnostics().size(), 1u);
    EXPECT_NE(gc.diagnostics()[0].message.find("unknown escape"), std::string::npos);
}

TEST(Compositor, TypeShapes) {
    pgg::GrammarCompositor gc;
    pgg::TypeRef* t = gc.newTypeGeneric("geo", sp(1), gc.newTypeName("mesh", sp(1)), sp(1));
    EXPECT_EQ(t->base, "geo");
    EXPECT_EQ(t->geoKind, "mesh");
    pgg::TypeRef* bad = gc.newTypeGeneric("geo", sp(2), gc.newTypeName("vec3", sp(2)), sp(2));
    EXPECT_TRUE(bad->geoKind.empty());
    EXPECT_EQ(gc.diagnostics().size(), 1u);
    pgg::TypeRef* f = gc.newTypeGeneric("field", sp(3), gc.newTypeName("vec3", sp(3)), sp(3));
    EXPECT_EQ(f->base, "field");
    ASSERT_TRUE(f->arg != nullptr);
    EXPECT_EQ(f->arg->base, "vec3");
    pgg::TypeRef* l = gc.typeList(gc.typeOptional(gc.newTypeName("f32", sp(4)), sp(4)), sp(4));
    EXPECT_TRUE(l->optional);
    EXPECT_TRUE(l->list);
}

}  // namespace
