// Formatter tests: canonical output, comment preservation, kw-arg reorder,
// and the E0 round-trip criterion parse(format(x)) == parse(x) over the
// corpus (src/tests/pgg/corpus, PGG_CORPUS_DIR is set by CMake).
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "pgg/pgg.h"

namespace {

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string corpusPath(const std::string& name) {
    return std::string(PGG_CORPUS_DIR) + "/" + name;
}

// Named call args are order-free (the formatter may put them into declared
// order), so round-trip compares ASTs with named args sorted by name.
void normalizeCalls(pgg::Node* n) {
    if (!n) return;
    auto normExpr = [](pgg::Expr* e, auto& self) -> void {
        if (!e) return;
        switch (e->kind) {
            case pgg::NodeKind::Call: {
                auto* c = static_cast<pgg::Call*>(e);
                std::stable_sort(c->args.begin(), c->args.end(),
                                 [](const pgg::CallArg& a, const pgg::CallArg& b) {
                                     if (a.hasName != b.hasName) return a.hasName > b.hasName;
                                     return a.name < b.name;
                                 });
                for (pgg::CallArg& a : c->args) self(a.value, self);
                break;
            }
            case pgg::NodeKind::Paren:
                self(static_cast<pgg::Paren*>(e)->inner, self);
                break;
            case pgg::NodeKind::ListLit: {
                auto* l = static_cast<pgg::ListLit*>(e);
                for (pgg::Expr* el : l->elems) self(el, self);
                break;
            }
            case pgg::NodeKind::Unary:
                self(static_cast<pgg::Unary*>(e)->operand, self);
                break;
            case pgg::NodeKind::Binary: {
                auto* b = static_cast<pgg::Binary*>(e);
                self(b->lhs, self);
                self(b->rhs, self);
                break;
            }
            case pgg::NodeKind::Ternary: {
                auto* t = static_cast<pgg::Ternary*>(e);
                self(t->cond, self);
                self(t->thenExpr, self);
                self(t->elseExpr, self);
                break;
            }
            default:
                break;
        }
    };
    auto normStmt = [](pgg::Stmt* s, auto& normExpr, auto& self) -> void {
        if (!s) return;
        switch (s->kind) {
            case pgg::NodeKind::Binding:
                normExpr(static_cast<pgg::Binding*>(s)->value, normExpr);
                break;
            case pgg::NodeKind::RepeatZone: {
                auto* z = static_cast<pgg::RepeatZone*>(s);
                normExpr(z->value, normExpr);
                normExpr(z->iterations, normExpr);
                for (pgg::Stmt* b : z->body) self(b, normExpr, self);
                break;
            }
            case pgg::NodeKind::ForeachZone: {
                auto* z = static_cast<pgg::ForeachZone*>(s);
                normExpr(z->collection, normExpr);
                for (pgg::Stmt* b : z->body) self(b, normExpr, self);
                break;
            }
            default:
                break;
        }
    };
    if (n->kind == pgg::NodeKind::File) {
        for (pgg::Node* item : static_cast<pgg::File*>(n)->items) normalizeCalls(item);
        return;
    }
    if (n->kind == pgg::NodeKind::Def) {
        auto* d = static_cast<pgg::Def*>(n);
        for (pgg::Stmt* s : d->body) normStmt(s, normExpr, normStmt);
        for (pgg::ContractStmt* c : d->expects) normExpr(c->cond, normExpr);
        for (pgg::ContractStmt* c : d->ensures) normExpr(c->cond, normExpr);
        return;
    }
    if (n->kind == pgg::NodeKind::ParamDecl) {
        normExpr(static_cast<pgg::ParamDecl*>(n)->def, normExpr);
        return;
    }
    if (n->kind >= pgg::NodeKind::Binding && n->kind <= pgg::NodeKind::ForeachZone) {
        normStmt(static_cast<pgg::Stmt*>(n), normExpr, normStmt);
    }
}

TEST(Formatter, MessyBecomesCanonicalGolden) {
    pgg::Document doc = pgg::parseFile(corpusPath("messy.pgg"));
    ASSERT_FALSE(doc.hasErrors());
    for (const pgg::Diagnostic& d : doc.diagnostics) ADD_FAILURE() << d.code << " " << d.message;
    const std::string formatted = pgg::format(doc.file, doc.comments);
    EXPECT_EQ(formatted, readFile(corpusPath("messy.canonical.pgg")));
}

TEST(Formatter, SignedVecLiteralCanonical) {
    const std::string src = "a = (-1,2.5)\noutput a\n";
    pgg::Document doc = pgg::parse(src);
    ASSERT_FALSE(doc.hasErrors());
    EXPECT_EQ(pgg::format(doc.file, doc.comments), "a = (-1, 2.5)\noutput a\n");
}

TEST(Formatter, KwArgsReorderToDeclaredOrder) {
    const std::string src =
        "def f(a: int, b: int, c: int) -> (o: int) {\n"
        "    o = a\n"
        "}\n"
        "x = f(c = 3, a = 1, b = 2)\n"
        "output x\n";
    pgg::Document doc = pgg::parse(src);
    ASSERT_FALSE(doc.hasErrors());
    EXPECT_EQ(pgg::format(doc.file, doc.comments),
              "def f(a: int, b: int, c: int) -> (o: int) {\n"
              "    o = a\n"
              "}\n"
              "x = f(a = 1, b = 2, c = 3)\n"
              "output x\n");
}

TEST(Formatter, NoReorderWhenAnyArgPositional) {
    const std::string src =
        "def f(a: int, b: int) -> (o: int) {\n"
        "    o = a\n"
        "}\n"
        "x = f(1, b = 2, a = 3)\n"
        "output x\n";
    pgg::Document doc = pgg::parse(src);
    ASSERT_FALSE(doc.hasErrors());
    EXPECT_NE(pgg::format(doc.file, doc.comments).find("f(1, b = 2, a = 3)"),
              std::string::npos);
}

TEST(Formatter, ListLiteralCanonical) {
    // Canonical form is single-line `[a, b]`; the trailing comma of the
    // multiline style (§6.4) is accepted but dropped.
    const std::string src =
        "a = 1\n"
        "b = 2\n"
        "xs = [a,    b,]\n"
        "empty = []\n"
        "output xs\n"
        "output empty\n";
    pgg::Document doc = pgg::parse(src);
    ASSERT_FALSE(doc.hasErrors());
    EXPECT_EQ(pgg::format(doc.file, doc.comments),
              "a = 1\n"
              "b = 2\n"
              "xs = [a, b]\n"
              "empty = []\n"
              "output xs\n"
              "output empty\n");
    // round-trip: parse(format(x)) == parse(x)
    const std::string formatted = pgg::format(doc.file, doc.comments);
    pgg::Document doc2 = pgg::parse(formatted);
    ASSERT_FALSE(doc2.hasErrors());
    EXPECT_TRUE(pgg::astEqual(doc.file, doc2.file));
}

TEST(Formatter, RoundTripCorpus) {
    for (const auto& entry : std::filesystem::directory_iterator(corpusPath(""))) {
        if (entry.path().extension() != ".pgg") continue;
        const std::string path = entry.path().string();
        pgg::Document doc = pgg::parseFile(path);
        ASSERT_FALSE(doc.hasErrors()) << path;
        const std::string formatted = pgg::format(doc.file, doc.comments);

        pgg::Document doc2 = pgg::parse(formatted, path);
        ASSERT_FALSE(doc2.hasErrors()) << path;
        normalizeCalls(doc.file);
        normalizeCalls(doc2.file);
        EXPECT_TRUE(pgg::astEqual(doc.file, doc2.file)) << "round-trip AST mismatch: " << path;

        // formatting is idempotent
        pgg::Document doc3 = pgg::parse(formatted, path);
        EXPECT_EQ(pgg::format(doc3.file, doc3.comments), formatted) << "not idempotent: " << path;
    }
}

TEST(Formatter, BrokenIndentButValidBracesFormats) {
    // the E0 acceptance case: braces intact, indentation all over the place
    const std::string src =
        "def f(x: geo) -> (o: geo) {\n"
        "shape = f(x)\n"
        "        o = shape\n"
        "}\n"
        "a = 1\n"
        "output a\n";
    pgg::Document doc = pgg::parse(src);
    ASSERT_FALSE(doc.hasErrors());
    for (const pgg::Diagnostic& d : doc.diagnostics) ADD_FAILURE() << d.code << " " << d.message;
    EXPECT_EQ(pgg::format(doc.file, doc.comments),
              "def f(x: geo) -> (o: geo) {\n"
              "    shape = f(x)\n"
              "    o = shape\n"
              "}\n"
              "a = 1\n"
              "output a\n");
}

}  // namespace
