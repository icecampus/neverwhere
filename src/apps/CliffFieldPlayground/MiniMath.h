// Minimal vector/matrix helpers for the cliff field prototype.
// Mat4 is column-major (m[col * 4 + row]); multiplying M * v treats v as a column.
// Perspective produces Metal-style clip space (NDC z in [0, 1], y up).
#pragma once

#include <cmath>

namespace cfm {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
    Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
    Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
    float& operator[](int i) { return (&x)[i]; }
    float operator[](int i) const { return (&x)[i]; }
};

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }

inline Vec3 normalize(const Vec3& v) {
    const float len = length(v);
    return len > 1e-12f ? v * (1.0f / len) : Vec3(0.0f, 1.0f, 0.0f);
}

inline float clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

inline float smoothstep(float edge0, float edge1, float x) {
    const float t = clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

struct Mat4 {
    float m[16]{};

    static Mat4 identity() {
        Mat4 r;
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }

    // Right-handed perspective, Metal clip conventions (NDC z in [0,1]).
    static Mat4 perspective(float fovYRadians, float aspect, float nearZ, float farZ) {
        const float t = std::tan(fovYRadians * 0.5f);
        Mat4 r;
        r.m[0] = 1.0f / (aspect * t);
        r.m[5] = 1.0f / t;
        r.m[10] = farZ / (nearZ - farZ);
        r.m[11] = -1.0f;
        r.m[14] = nearZ * farZ / (nearZ - farZ);
        return r;
    }

    // Right-handed look-at (camera looks along -z in view space).
    static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
        const Vec3 f = normalize(center - eye);
        const Vec3 s = normalize(cross(f, up));
        const Vec3 u = cross(s, f);
        Mat4 r = identity();
        r.m[0] = s.x;   r.m[4] = s.y;   r.m[8] = s.z;      r.m[12] = -dot(s, eye);
        r.m[1] = u.x;   r.m[5] = u.y;   r.m[9] = u.z;      r.m[13] = -dot(u, eye);
        r.m[2] = -f.x;  r.m[6] = -f.y;  r.m[10] = -f.z;    r.m[14] = dot(f, eye);
        return r;
    }
};

// Column-major multiply: result = a * b (b applied first).
inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            }
            r.m[col * 4 + row] = sum;
        }
    }
    return r;
}

} // namespace cfm
