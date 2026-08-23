#include "pch.h"

#include "StonePoly.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kClipEps = 1e-9;

// Splitmix64: deterministic per-index hash for vertex noise.
std::uint64_t mix64(std::uint64_t z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

double hashUnit(std::uint64_t key) {
    // top 53 bits -> [0, 1)
    return static_cast<double>(mix64(key) >> 11) * (1.0 / 9007199254740992.0);
}

// [-1, 1) from the raw mt19937_64 output. uniform_real_distribution maps
// differently across STL implementations, which would break cross-platform
// seed determinism.
double uniSigned(std::mt19937_64& rng) {
    return static_cast<double>(rng() >> 11) * (1.0 / 4503599627370496.0) - 1.0;
}

// Two perpendicular axes spanning the plane perpendicular to n.
void planeBasis(const glm::dvec3& n, glm::dvec3& u, glm::dvec3& v) {
    const glm::dvec3 ref = std::abs(n.y) < 0.9 ? glm::dvec3{0.0, 1.0, 0.0} : glm::dvec3{1.0, 0.0, 0.0};
    u = glm::normalize(glm::cross(n, ref));
    v = glm::cross(n, u);
}

} // namespace

void clipByPlane(StonePoly& poly, const StonePlane& plane, int planeId) {
    std::vector<StonePoly::Face> outFaces;
    outFaces.reserve(poly.faces.size() + 1);

    // Intersection vertices keyed by the clipped edge so both adjacent faces
    // share the same index (watertight), each created exactly once.
    std::map<std::pair<int, int>, int> cutVerts;
    std::vector<int> cap;

    const auto dist = [&](int i) { return glm::dot(plane.n, poly.verts[i]) - plane.d; };
    const auto cutIndex = [&](int a, int b, double da, double db) -> int {
        const auto key = std::minmax(a, b);
        const auto it = cutVerts.find(key);
        if (it != cutVerts.end()) {
            return it->second;
        }
        const double t = da / (da - db);
        glm::dvec3 p = poly.verts[a] + t * (poly.verts[b] - poly.verts[a]);
        // Snap onto the plane to kill interpolation drift.
        p -= plane.n * (glm::dot(plane.n, p) - plane.d);
        const int idx = static_cast<int>(poly.verts.size());
        poly.verts.push_back(p);
        cutVerts.emplace(key, idx);
        cap.push_back(idx);
        return idx;
    };

    for (const StonePoly::Face& face : poly.faces) {
        std::vector<int> kept;
        const int n = static_cast<int>(face.idx.size());
        for (int i = 0; i < n; ++i) {
            const int a = face.idx[i];
            const int b = face.idx[(i + 1) % n];
            const double da = dist(a);
            const double db = dist(b);
            const bool ina = da <= kClipEps;
            const bool inb = db <= kClipEps;
            if (ina) {
                kept.push_back(a);
            }
            if (ina != inb) {
                kept.push_back(cutIndex(a, b, da, db));
            }
        }
        if (kept.size() >= 3) {
            outFaces.push_back({std::move(kept), face.planeId});
        }
    }

    if (cap.size() >= 3) {
        glm::dvec3 center{0.0};
        for (const int i : cap) {
            center += poly.verts[i];
        }
        center /= static_cast<double>(cap.size());
        glm::dvec3 u, v;
        planeBasis(plane.n, u, v);
        std::sort(cap.begin(), cap.end(), [&](int a, int b) {
            const glm::dvec3 pa = poly.verts[a] - center;
            const glm::dvec3 pb = poly.verts[b] - center;
            return std::atan2(glm::dot(pa, v), glm::dot(pa, u)) <
                std::atan2(glm::dot(pb, v), glm::dot(pb, u));
        });
        outFaces.push_back({std::move(cap), planeId});
    }

    poly.faces = std::move(outFaces);
}

StonePoly buildPolyhedron(const std::vector<StonePlane>& planes) {
    // Start box: generous margin around any unit-scale rock.
    const double m = 100.0;
    StonePoly poly;
    poly.verts = {
        {-m, -m, -m}, {m, -m, -m}, {m, m, -m}, {-m, m, -m},
        {-m, -m, m}, {m, -m, m}, {m, m, m}, {-m, m, m},
    };
    // CCW seen from outside.
    poly.faces = {
        {{4, 5, 6, 7}, -1}, // +z
        {{1, 0, 3, 2}, -1}, // -z
        {{5, 4, 0, 1}, -1}, // -y
        {{7, 6, 2, 3}, -1}, // +y
        {{4, 7, 3, 0}, -1}, // -x
        {{6, 5, 1, 2}, -1}, // +x
    };
    for (std::size_t i = 0; i < planes.size(); ++i) {
        clipByPlane(poly, planes[i], static_cast<int>(i));
    }
    compactPoly(poly);
    return poly;
}

void compactPoly(StonePoly& poly) {
    std::vector<int> remap(poly.verts.size(), -1);
    std::vector<glm::dvec3> verts;
    for (StonePoly::Face& face : poly.faces) {
        for (int& i : face.idx) {
            if (remap[i] < 0) {
                remap[i] = static_cast<int>(verts.size());
                verts.push_back(poly.verts[i]);
            }
            i = remap[i];
        }
    }
    poly.verts = std::move(verts);
}

std::vector<StonePlane> makePrismPlanes(
    int sides, double radius, double height, double taper, double yawRad) {
    std::vector<StonePlane> planes;
    planes.reserve(static_cast<std::size_t>(sides) + 2);
    planes.push_back({{0.0, -1.0, 0.0}, 0.0, true}); // bottom: y >= 0
    planes.push_back({{0.0, 1.0, 0.0}, height, false}); // top: y <= height
    // Inward slope per unit up: r(top) = radius * (1 - taper).
    const double m = taper * radius / std::max(height, 1e-6);
    const double inv = 1.0 / std::sqrt(1.0 + m * m);
    for (int k = 0; k < sides; ++k) {
        const double th = yawRad + 2.0 * kPi * static_cast<double>(k) / static_cast<double>(sides);
        const double c = std::cos(th);
        const double s = std::sin(th);
        planes.push_back({{c * inv, m * inv, s * inv}, radius * inv, false});
    }
    return planes;
}

std::vector<StonePlane> makeBallPlanes(
    int count, double radius, double offsetJitter, std::mt19937_64& rng) {
    std::vector<StonePlane> planes;
    planes.reserve(static_cast<std::size_t>(count));
    constexpr double kGoldenAngle = 2.39996322972865332;
    for (int i = 0; i < count; ++i) {
        const double y = 1.0 - 2.0 * (static_cast<double>(i) + 0.5) / static_cast<double>(count);
        const double r = std::sqrt(std::max(0.0, 1.0 - y * y));
        const double phi = kGoldenAngle * static_cast<double>(i);
        const glm::dvec3 dir{r * std::cos(phi), y, r * std::sin(phi)};
        planes.push_back({dir, radius * (1.0 + offsetJitter * uniSigned(rng)), false});
    }
    return planes;
}

void jitterPlanes(
    std::vector<StonePlane>& planes,
    double tiltRad,
    double offsetFrac,
    double refSize,
    std::mt19937_64& rng) {
    for (StonePlane& plane : planes) {
        if (plane.lock) {
            continue;
        }
        glm::dvec3 u, v;
        planeBasis(plane.n, u, v);
        // Two small Rodrigues rotations, one per in-plane axis.
        for (const glm::dvec3& axis : {u, v}) {
            const double ang = tiltRad * uniSigned(rng);
            const double c = std::cos(ang);
            const double s = std::sin(ang);
            plane.n = plane.n * c + glm::cross(axis, plane.n) * s + axis * glm::dot(axis, plane.n) * (1.0 - c);
        }
        plane.n = glm::normalize(plane.n);
        plane.d += uniSigned(rng) * offsetFrac * refSize;
    }
}

std::vector<StonePlane> chamferPlanes(
    const StonePoly& poly,
    const std::vector<StonePlane>& planes,
    double width,
    bool topOnly) {
    // Edge map: vertex-index pair -> faces sharing it.
    std::map<std::pair<int, int>, std::vector<int>> edgeFaces;
    double maxY = 0.0;
    for (const glm::dvec3& v : poly.verts) {
        maxY = std::max(maxY, v.y);
    }
    for (std::size_t fi = 0; fi < poly.faces.size(); ++fi) {
        const StonePoly::Face& face = poly.faces[fi];
        const int n = static_cast<int>(face.idx.size());
        for (int i = 0; i < n; ++i) {
            const auto key = std::minmax(face.idx[i], face.idx[(i + 1) % n]);
            edgeFaces[key].push_back(static_cast<int>(fi));
        }
    }

    std::vector<StonePlane> out;
    for (const auto& [key, faces] : edgeFaces) {
        if (faces.size() != 2) {
            continue; // non-manifold edge: leave it alone
        }
        const int idA = poly.faces[faces[0]].planeId;
        const int idB = poly.faces[faces[1]].planeId;
        if (idA < 0 || idB < 0 || idA >= static_cast<int>(planes.size()) ||
            idB >= static_cast<int>(planes.size())) {
            continue;
        }
        const glm::dvec3 mid = (poly.verts[key.first] + poly.verts[key.second]) * 0.5;
        if (topOnly && mid.y <= 0.5 * maxY) {
            continue;
        }
        const glm::dvec3& na = planes[idA].n;
        const glm::dvec3& nb = planes[idB].n;
        const double cosPhi = glm::clamp(glm::dot(na, nb), -1.0, 1.0);
        const double phi = std::acos(cosPhi);
        if (phi < 1e-3) {
            continue; // nearly coplanar: no edge to cut
        }
        const glm::dvec3 n = glm::normalize(na + nb);
        // Cross-section perpendicular to the edge: cutting `width` along each
        // face puts the chamfer plane at perpendicular distance width*sin(phi/2)
        // inward from the edge.
        const double d = glm::dot(n, mid) - width * std::sin(0.5 * phi);
        out.push_back({n, d, false});
    }
    return out;
}

void vertexNoise(StonePoly& poly, double amp, std::uint64_t seed) {
    if (amp <= 0.0) {
        return;
    }
    for (std::size_t i = 0; i < poly.verts.size(); ++i) {
        const std::uint64_t base = seed ^ (0x9E3779B97F4A7C15ull * (static_cast<std::uint64_t>(i) + 1));
        for (int axis = 0; axis < 3; ++axis) {
            poly.verts[i][axis] += amp * (2.0 * hashUnit(base + static_cast<std::uint64_t>(axis)) - 1.0);
        }
    }
}

void transformPoly(StonePoly& poly, const glm::dmat4& m) {
    for (glm::dvec3& v : poly.verts) {
        const glm::dvec4 p = m * glm::dvec4(v, 1.0);
        v = glm::dvec3(p) / p.w;
    }
}

StonePlane transformPlane(const StonePlane& plane, const glm::dmat4& m) {
    const glm::dmat3 invT = glm::transpose(glm::inverse(glm::dmat3(m)));
    const glm::dvec3 t{m[3]};
    const glm::dvec3 n2 = invT * plane.n;
    const double len = glm::length(n2);
    return {n2 / len, (plane.d + glm::dot(n2, t)) / len, plane.lock};
}

void transformPlanes(std::vector<StonePlane>& planes, const glm::dmat4& m) {
    for (StonePlane& plane : planes) {
        plane = transformPlane(plane, m);
    }
}

void groundClip(StonePoly& poly, double sink, int planeId) {
    clipByPlane(poly, StonePlane{{0.0, -1.0, 0.0}, sink, true}, planeId);
    compactPoly(poly);
}

glm::dvec3 faceNormal(const StonePoly& poly, const StonePoly::Face& face) {
    // Newell's method: robust for slightly non-planar faces (post vertexNoise).
    glm::dvec3 n{0.0};
    const int count = static_cast<int>(face.idx.size());
    for (int i = 0; i < count; ++i) {
        const glm::dvec3& a = poly.verts[face.idx[i]];
        const glm::dvec3& b = poly.verts[face.idx[(i + 1) % count]];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    const double len = glm::length(n);
    return len > 1e-12 ? n / len : glm::dvec3{0.0, 1.0, 0.0};
}

bool isWatertight(const StonePoly& poly) {
    std::map<std::pair<int, int>, int> directed;
    for (const StonePoly::Face& face : poly.faces) {
        const int n = static_cast<int>(face.idx.size());
        if (n < 3) {
            return false;
        }
        for (int i = 0; i < n; ++i) {
            directed[{face.idx[i], face.idx[(i + 1) % n]}]++;
        }
    }
    for (const auto& [edge, count] : directed) {
        if (count != 1) {
            return false; // same direction twice -> broken orientation
        }
        const auto it = directed.find({edge.second, edge.first});
        if (it == directed.end() || it->second != 1) {
            return false; // no reverse edge -> hole
        }
    }
    return true;
}

bool allInside(const StonePoly& poly, const std::vector<StonePlane>& planes, double eps) {
    for (const glm::dvec3& v : poly.verts) {
        for (const StonePlane& plane : planes) {
            if (glm::dot(plane.n, v) - plane.d > eps) {
                return false;
            }
        }
    }
    return true;
}
