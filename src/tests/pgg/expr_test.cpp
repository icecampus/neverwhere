// Expression tests (§6.3, §4.5): operator precedence, conversions, broadcast,
// ternary, casts, vec constructors, expression builtins, orient_from_euler,
// ramp — at value level (constant folding) and at field level (per-element).
#include <gtest/gtest.h>

#include <glm/gtc/quaternion.hpp>

#include "pgg/eval.h"

namespace {

pgg::RunResult runOk(const std::string& src) {
    pgg::RunResult r = pgg::run(src);
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (!d.isWarning) ADD_FAILURE() << d.code << " " << d.message;
    return r;
}

const pgg::Value& outValue(const pgg::RunResult& r, const std::string& name) {
    for (const pgg::RunOutput& o : r.outputs)
        if (o.name == name) return o.value;
    ADD_FAILURE() << "no output " << name;
    static const pgg::Value dummy;
    return dummy;
}

pgg::Value evalExpr(const std::string& expr) {
    pgg::RunResult r = runOk("x = " + expr + "\noutput x\n");
    return outValue(r, "x");
}

float evalF32(const std::string& expr) { return pgg::asF32(evalExpr(expr)); }
int64_t evalInt(const std::string& expr) { return pgg::asInt(evalExpr(expr)); }
bool evalBool(const std::string& expr) { return pgg::asBool(evalExpr(expr)); }
glm::vec3 evalVec3(const std::string& expr) { return pgg::asVec3(evalExpr(expr)); }

// Probes a scalar field on a mesh_line (points (0,0,z), x == 0): the offset
// vec3(<expr>, 0, 0) shifts x, so output point i carries the field value at i.
std::vector<float> probeField(const std::string& expr, int count, const std::string& prelude = {},
                              float length = 1.0f) {
    pgg::RunResult r = runOk(
        prelude +
        "pts = mesh_line(count = " + std::to_string(count) + ", length = " + std::to_string(length) + ")\n"
        "out = set_position(pts, offset = vec3(" + expr + ", 0, 0))\n"
        "output out\n");
    pgg::GeoPtr g = pgg::asGeo(outValue(r, "out"));
    std::vector<float> xs(g->pointCount());
    for (size_t i = 0; i < xs.size(); ++i) xs[i] = (*g->positions)[i].x;
    return xs;
}

// --- value level -------------------------------------------------------------

TEST(Expr, OperatorPrecedence) {
    EXPECT_EQ(evalInt("2 + 3 * 4"), 14);
    EXPECT_EQ(evalInt("(2 + 3) * 4"), 20);
    EXPECT_EQ(evalInt("10 % 3"), 1);
    EXPECT_EQ(evalInt("-2 * 3"), -6);
    // & and | bind weaker than comparisons (GLSL-style, §13).
    EXPECT_TRUE(evalBool("1 < 2 & 3 < 4"));
    EXPECT_TRUE(evalBool("true | false & false"));
    EXPECT_FALSE(evalBool("true & false | false"));
}

TEST(Expr, ConversionsAndBroadcast) {
    EXPECT_FLOAT_EQ(evalF32("1 + 2.5"), 3.5f);          // int -> f32 widening
    EXPECT_EQ(evalInt("true + 1"), 2);                  // bool -> int widening
    EXPECT_EQ(evalVec3("2 * (1, 2, 3)"), glm::vec3(2, 4, 6));  // scalar broadcast
    EXPECT_EQ(evalVec3("(1, 2, 3) + 1"), glm::vec3(2, 3, 4));
    EXPECT_EQ(evalVec3("(1, 2, 3) * (2, 2, 2)"), glm::vec3(2, 4, 6));
}

TEST(Expr, SignedVecLiteral) {
    // Since v1.9 vec literal components may carry a sign (§6.3).
    EXPECT_EQ(evalVec3("(-1, 2, -3)"), glm::vec3(-1, 2, -3));
    EXPECT_EQ(pgg::asVec2(evalExpr("(-0.5, 1.25)")), glm::vec2(-0.5f, 1.25f));
    EXPECT_EQ(evalVec3("2 * (-1, 0, 1)"), glm::vec3(-2, 0, 2));  // broadcast
    EXPECT_EQ(evalVec3("vec3((-1, -2, -3))"), glm::vec3(-1, -2, -3));  // passthrough
    // A parenthesized single number stays a scalar expression, not a vec1.
    EXPECT_EQ(evalInt("(-1)"), -1);
    EXPECT_FLOAT_EQ(evalF32("(2.5)"), 2.5f);
    EXPECT_EQ(evalInt("2 * (-3)"), -6);
}

TEST(Expr, Ternary) {
    EXPECT_EQ(evalInt("1 > 2 ? 10 : 20"), 20);
    EXPECT_EQ(evalInt("1 < 2 ? 10 : 20"), 10);
    EXPECT_FLOAT_EQ(evalF32("true ? 1 : 2.5"), 1.0f);  // branches promote to f32
}

TEST(Expr, Casts) {
    EXPECT_EQ(evalInt("int(3.7)"), 3);
    EXPECT_EQ(evalInt("int(-3.7)"), -3);  // truncation toward zero
    EXPECT_FLOAT_EQ(evalF32("f32(2)"), 2.0f);
    EXPECT_FALSE(evalBool("bool(0)"));
    EXPECT_TRUE(evalBool("bool(2)"));
}

TEST(Expr, VecConstructors) {
    EXPECT_EQ(evalVec3("vec3(1, 2, 3)"), glm::vec3(1, 2, 3));
    EXPECT_EQ(evalVec3("vec3(1)"), glm::vec3(1, 1, 1));  // broadcast form
    EXPECT_EQ(pgg::asVec2(evalExpr("vec2(5)")), glm::vec2(5, 5));
    EXPECT_EQ(pgg::asVec4(evalExpr("vec4(1, 2, 3, 4)")), glm::vec4(1, 2, 3, 4));
    EXPECT_EQ(evalVec3("vec3((1, 2, 3))"), glm::vec3(1, 2, 3));  // passthrough
}

TEST(Expr, BuiltinFunctions) {
    EXPECT_FLOAT_EQ(evalF32("dot((1, 0, 0), (0, 1, 0))"), 0.0f);
    EXPECT_FLOAT_EQ(evalF32("dot((1, 2, 3), (1, 2, 3))"), 14.0f);
    EXPECT_EQ(evalVec3("cross((1, 0, 0), (0, 1, 0))"), glm::vec3(0, 0, 1));
    EXPECT_FLOAT_EQ(evalF32("length((0, 3, 4))"), 5.0f);
    EXPECT_EQ(evalVec3("normalize((0, 3, 0))"), glm::vec3(0, 1, 0));
    EXPECT_EQ(evalInt("clamp(5, 0, 3)"), 3);
    EXPECT_EQ(evalVec3("clamp(vec3(5, -5, 1), (0, 0, 0), (3, 3, 3))"), glm::vec3(3, 0, 1));
    EXPECT_FLOAT_EQ(evalF32("smoothstep(0, 1, 0.5)"), 0.5f);
    EXPECT_FLOAT_EQ(evalF32("smoothstep(0, 1, 0.25)"), 0.15625f);
    EXPECT_FLOAT_EQ(evalF32("mix(0.0, 10.0, 0.25)"), 2.5f);
    EXPECT_EQ(evalVec3("mix((0, 0, 0), (10, 10, 10), 0.5)"), glm::vec3(5, 5, 5));
    EXPECT_EQ(evalInt("abs(-3)"), 3);
    EXPECT_EQ(evalVec3("abs(vec3(-1, 2, -3))"), glm::vec3(1, 2, 3));
    EXPECT_EQ(evalInt("min(2, 3)"), 2);
    EXPECT_EQ(pgg::asVec2(evalExpr("max((1, 5), (3, 2))")), glm::vec2(3, 5));
    EXPECT_FLOAT_EQ(evalF32("floor(2.7)"), 2.0f);
    EXPECT_FLOAT_EQ(evalF32("floor(-2.3)"), -3.0f);
    EXPECT_FLOAT_EQ(evalF32("pow(2, 10)"), 1024.0f);
    EXPECT_EQ(pgg::asVec2(evalExpr("pow((2, 3), (2, 2))")), glm::vec2(4, 9));
}

TEST(Expr, OrientFromEuler) {
    const glm::vec4 identity = pgg::asVec4(evalExpr("orient_from_euler((0, 0, 0))"));
    EXPECT_NEAR(identity.x, 0.0f, 1e-6f);
    EXPECT_NEAR(identity.y, 0.0f, 1e-6f);
    EXPECT_NEAR(identity.z, 0.0f, 1e-6f);
    EXPECT_NEAR(identity.w, 1.0f, 1e-6f);
    const glm::vec4 q4 = pgg::asVec4(evalExpr("orient_from_euler((30, 45, 60))"));
    EXPECT_NEAR(glm::length(q4), 1.0f, 1e-6f);  // unit quaternion
    // Reference rotation: yaw +90° maps +Z to +X (right-handed, Y-up).
    const glm::vec4 yaw90 = pgg::asVec4(evalExpr("orient_from_euler((0, 90, 0))"));
    const glm::quat q(yaw90.w, yaw90.x, yaw90.y, yaw90.z);
    const glm::vec3 rotated = q * glm::vec3(0, 0, 1);
    EXPECT_NEAR(rotated.x, 1.0f, 1e-5f);
    EXPECT_NEAR(rotated.y, 0.0f, 1e-5f);
    EXPECT_NEAR(rotated.z, 0.0f, 1e-5f);
}

TEST(Expr, Ramp) {
    EXPECT_FLOAT_EQ(evalF32("ramp(0.5, 0, 0, 1, 1)"), 0.5f);
    EXPECT_FLOAT_EQ(evalF32("ramp(0.25, 0, 0, 1, 1)"), 0.25f);
    EXPECT_FLOAT_EQ(evalF32("ramp(2, 0, 0, 1, 1)"), 1.0f);    // clamped at the end
    EXPECT_FLOAT_EQ(evalF32("ramp(-1, 0, 5, 1, 10)"), 5.0f);  // clamped at the start
    EXPECT_FLOAT_EQ(evalF32("ramp(1.5, 0, 0, 1, 10, 2, 20)"), 15.0f);  // multi-segment
    EXPECT_EQ(evalVec3("ramp(0.5, 0, (0, 0, 0), 1, (1, 2, 3))"), glm::vec3(0.5f, 1.0f, 1.5f));
}

// --- field level -------------------------------------------------------------

TEST(Expr, FieldIndexAndArithmetic) {
    const std::vector<float> xs = probeField("@index", 4);
    ASSERT_EQ(xs.size(), 4u);
    EXPECT_FLOAT_EQ(xs[0], 0.0f);
    EXPECT_FLOAT_EQ(xs[1], 1.0f);
    EXPECT_FLOAT_EQ(xs[2], 2.0f);
    EXPECT_FLOAT_EQ(xs[3], 3.0f);
    const std::vector<float> mod = probeField("@index % 2", 4);
    EXPECT_FLOAT_EQ(mod[0], 0.0f);
    EXPECT_FLOAT_EQ(mod[1], 1.0f);
    EXPECT_FLOAT_EQ(mod[2], 0.0f);
    EXPECT_FLOAT_EQ(mod[3], 1.0f);
}

TEST(Expr, FieldBroadcastAndPosition) {
    // mesh_line runs along +Z: dot(@P, (0,0,1)) picks z, * 2 broadcasts.
    const std::vector<float> xs = probeField("dot(@P, (0, 0, 1)) * 2", 3);
    EXPECT_FLOAT_EQ(xs[0], 0.0f);
    EXPECT_FLOAT_EQ(xs[1], 1.0f);
    EXPECT_FLOAT_EQ(xs[2], 2.0f);
    // position() is the explicit reader form of @P.
    const std::vector<float> same = probeField("dot(position(), (0, 0, 1)) * 2", 3);
    EXPECT_EQ(xs, same);
}

TEST(Expr, FieldTernary) {
    const std::vector<float> xs = probeField("@index > 1 ? 5 : 1", 4);
    EXPECT_FLOAT_EQ(xs[0], 1.0f);
    EXPECT_FLOAT_EQ(xs[1], 1.0f);
    EXPECT_FLOAT_EQ(xs[2], 5.0f);
    EXPECT_FLOAT_EQ(xs[3], 5.0f);
}

TEST(Expr, FieldFbmDeterminismAndRange) {
    const std::string prelude =
        "noise_rng = split_rng(rng_from_seed(42), key = \"surface\")\n";
    const std::vector<float> a = probeField("fbm(scale = 2.5, octaves = 5, rng = noise_rng)", 8, prelude);
    const std::vector<float> b = probeField("fbm(scale = 2.5, octaves = 5, rng = noise_rng)", 8, prelude);
    EXPECT_EQ(a, b);  // same rng -> bit-identical field
    for (float v : a) {
        EXPECT_GE(v, -1.0f);
        EXPECT_LE(v, 1.0f);
    }
    // A different generator gives an independent field.
    const std::vector<float> c = probeField(
        "fbm(scale = 2.5, octaves = 5, rng = split_rng(noise_rng, key = 1))", 8, prelude);
    EXPECT_NE(a, c);
}

TEST(Expr, FieldVnoiseViaDotIdiom) {
    // No swizzle at E1: components are extracted with the dot() idiom (§19).
    const std::string prelude = "r = rng_from_seed(3)\n";
    const std::vector<float> xs = probeField("dot(vnoise(scale = 1.0, rng = r), (1, 0, 0))", 4, prelude);
    for (float v : xs) {
        EXPECT_GE(v, -1.0f);
        EXPECT_LE(v, 1.0f);
    }
}

TEST(Expr, FieldRandom) {
    const std::string prelude = "r = rng_from_seed(11)\n";
    const std::vector<float> xs = probeField("random(lo = 2, hi = 5, rng = r)", 16, prelude);
    for (float v : xs) {
        EXPECT_GE(v, 2.0f);
        EXPECT_LT(v, 5.0f);
    }
    // counter = 0: one uniform value over the whole geometry.
    const std::vector<float> uni = probeField("random(lo = 2, hi = 5, rng = r, counter = 0)", 4, prelude);
    for (float v : uni) EXPECT_FLOAT_EQ(v, uni[0]);
    // random_int stays in [0, n).
    const std::vector<float> ints = probeField("f32(random_int(4, rng = r))", 32, prelude);
    for (float v : ints) {
        EXPECT_GE(v, 0.0f);
        EXPECT_LE(v, 3.0f);
        EXPECT_FLOAT_EQ(v, std::floor(v));
    }
}

TEST(Expr, FieldDistanceTo) {
    // Points (0,0,0),(0,0,2),(0,0,4) against a unit icosphere.
    const std::string prelude = "target = ico_sphere(subdiv = 2, radius = 1.0)\n";
    const std::vector<float> xs = probeField("distance_to(target)", 3, prelude, 4.0f);
    EXPECT_NEAR(xs[0], 1.0f, 0.06f);  // center to surface (faceted sphere)
    EXPECT_NEAR(xs[1], 1.0f, 0.06f);
    EXPECT_NEAR(xs[2], 3.0f, 0.06f);
}

}  // namespace
