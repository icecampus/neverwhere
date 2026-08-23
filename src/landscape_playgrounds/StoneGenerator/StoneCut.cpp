#include "pch.h"

#include "StoneCut.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Nef_polyhedron_3.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/boost/graph/convert_nef_polyhedron_to_polygon_mesh.h>

#include <spdlog/spdlog.h>

namespace {

using CK = CGAL::Exact_predicates_exact_constructions_kernel;
using Nef = CGAL::Nef_polyhedron_3<CK>;
using CMesh = CGAL::Surface_mesh<CK::Point_3>;
using CPoint = CK::Point_3;

constexpr double kPi = 3.14159265358979323846;

// [0, 1) / [-1, 1) from the raw mt19937_64 output (STL-independent mapping).
double uni01(std::mt19937_64& rng) {
    return static_cast<double>(rng() >> 11) * (1.0 / 9007199254740992.0);
}
double uniSigned(std::mt19937_64& rng) {
    return static_cast<double>(rng() >> 11) * (1.0 / 4503599627370496.0) - 1.0;
}

std::uint64_t mixSeed(int seed) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(seed)) + 0x9E3779B9ull) *
        0xBF58476D1CE4E5B9ull;
}

std::uint64_t mix64(std::uint64_t z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// Two perpendicular axes spanning the plane perpendicular to n.
void planeBasis(const glm::dvec3& n, glm::dvec3& u, glm::dvec3& v) {
    const glm::dvec3 ref = std::abs(n.y) < 0.9 ? glm::dvec3{0.0, 1.0, 0.0} : glm::dvec3{1.0, 0.0, 0.0};
    u = glm::normalize(glm::cross(n, ref));
    v = glm::cross(n, u);
}

// --- CGAL operands -------------------------------------------------------------
// All Nef operands are closed triangle/polygon meshes built procedurally; the
// halfspace Nef constructor is NOT used (vertex-count-based is_empty() on
// halfspaces breaks chained intersections). Faces are auto-oriented outward
// (convex operands), so no hand-winding bugs.

// Force every face of a convex operand to point outward: reverse faces whose
// Newell normal looks at the mesh centroid.
void orientOutward(std::vector<glm::dvec3>& verts, std::vector<std::vector<int>>& faces) {
    glm::dvec3 center{0.0};
    for (const glm::dvec3& v : verts) {
        center += v;
    }
    center /= static_cast<double>(verts.size());
    for (auto& face : faces) {
        glm::dvec3 n{0.0};
        glm::dvec3 fc{0.0};
        const std::size_t count = face.size();
        for (std::size_t i = 0; i < count; ++i) {
            const glm::dvec3& a = verts[face[i]];
            const glm::dvec3& b = verts[face[(i + 1) % count]];
            n.x += (a.y - b.y) * (a.z + b.z);
            n.y += (a.z - b.z) * (a.x + b.x);
            n.z += (a.x - b.x) * (a.y + b.y);
            fc += a;
        }
        fc /= static_cast<double>(count);
        if (glm::dot(n, fc - center) < 0.0) {
            std::reverse(face.begin(), face.end());
        }
    }
}

Nef nefFromFaces(std::vector<glm::dvec3> verts, std::vector<std::vector<int>> faces) {
    orientOutward(verts, faces);
    CMesh mesh;
    std::vector<CMesh::Vertex_index> idx;
    idx.reserve(verts.size());
    for (const glm::dvec3& v : verts) {
        idx.push_back(mesh.add_vertex(CPoint(v.x, v.y, v.z)));
    }
    for (const auto& face : faces) {
        // Triangulate (fan): Nef construction asserts exact coplanarity for
        // faces with >3 edges, which double-computed quad corners can't give.
        for (std::size_t i = 1; i + 1 < face.size(); ++i) {
            const CMesh::Face_index fi = mesh.add_face(
                idx[static_cast<std::size_t>(face[0])],
                idx[static_cast<std::size_t>(face[i])],
                idx[static_cast<std::size_t>(face[i + 1])]);
            if (fi == CMesh::null_face()) {
                spdlog::warn("nefFromFaces: add_face rejected a face");
            }
        }
    }
    return Nef(mesh);
}

// Box with min/max corners.
Nef nefBox(const glm::dvec3& lo, const glm::dvec3& hi) {
    const std::vector<glm::dvec3> v = {
        {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
        {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z},
    };
    const std::vector<std::vector<int>> f = {
        {4, 5, 6, 7}, // +z
        {1, 0, 3, 2}, // -z
        {5, 4, 0, 1}, // -y
        {7, 6, 2, 3}, // +y
        {4, 7, 3, 0}, // -x
        {6, 5, 1, 2}, // +x
    };
    return nefFromFaces(v, f);
}

// Halfspace clip as a bounded box: top face lies on the plane n·x = d, the
// rest extends far below/sideways. Intersecting with it keeps n·x <= d.
Nef nefPlaneClip(const glm::dvec3& n, double d, double extent) {
    glm::dvec3 u, v;
    planeBasis(n, u, v);
    const glm::dvec3 p0 = n * d; // nearest point on the plane
    const double t = extent;
    const std::vector<glm::dvec3> verts = {
        p0 - t * u - t * v, p0 + t * u - t * v, p0 + t * u + t * v, p0 - t * u + t * v,
        p0 - t * u - t * v - t * n, p0 + t * u - t * v - t * n,
        p0 + t * u + t * v - t * n, p0 - t * u + t * v - t * n,
    };
    // same corner order as nefBox but in the (u, v, n) frame: bottom = -n side.
    const std::vector<std::vector<int>> f = {
        {0, 1, 2, 3}, // top (+n)
        {5, 4, 7, 6}, // bottom (-n)
        {4, 5, 1, 0}, // -v
        {6, 7, 3, 2}, // +v
        {7, 4, 0, 3}, // -u
        {5, 6, 2, 1}, // +u
    };
    return nefFromFaces(verts, f);
}

// V prism for grooves: apex line at p0 along d, opening along +m with
// half-angle beta, height hgt, half-length halfLen along d.
Nef nefVPrism(
    const glm::dvec3& p0, const glm::dvec3& d, const glm::dvec3& m,
    double beta, double halfLen, double hgt) {
    const glm::dvec3 s = glm::normalize(glm::cross(d, m));
    const double w = hgt * std::tan(beta);
    const glm::dvec3 top = p0 + m * hgt;
    const std::vector<glm::dvec3> verts = {
        p0 - d * halfLen, p0 + d * halfLen, // apex 0, 1
        top + s * w - d * halfLen, top + s * w + d * halfLen, // +s wall 2, 3
        top - s * w - d * halfLen, top - s * w + d * halfLen, // -s wall 4, 5
    };
    const std::vector<std::vector<int>> f = {
        {0, 1, 3, 2}, // +s wall
        {1, 0, 4, 5}, // -s wall
        {2, 3, 5, 4}, // top quad
        {0, 2, 4}, // -d cap
        {1, 5, 3}, // +d cap
    };
    return nefFromFaces(verts, f);
}

// Trihedral cone for pits: apex p0, three rays dirK fanning outward, height hgt.
Nef nefPitCone(const glm::dvec3& p0, const glm::dvec3 dirK[3], double hgt) {
    const std::vector<glm::dvec3> verts = {
        p0,
        p0 + dirK[0] * hgt, p0 + dirK[1] * hgt, p0 + dirK[2] * hgt,
    };
    const std::vector<std::vector<int>> f = {
        {0, 2, 1}, {0, 3, 2}, {0, 1, 3}, // side walls
        {1, 2, 3}, // top cap
    };
    return nefFromFaces(verts, f);
}

// --- Mesh snapshot -----------------------------------------------------------

struct Soup {
    std::vector<glm::dvec3> verts;
    std::vector<std::vector<std::size_t>> faces;
};

Soup snapshot(const Nef& body, bool triangulate); // fwd

// Deterministic order: CGAL's internal iteration order varies between process
// runs (pointer-keyed maps), so every snapshot is canonicalized before use —
// vertices sorted by position, faces by their sorted vertex-index set. Same
// geometry, same array, every run.
void canonicalize(Soup& soup) {
    const std::size_t n = soup.verts.size();
    std::vector<std::size_t> order(n);
    for (std::size_t i = 0; i < n; ++i) {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const glm::dvec3& pa = soup.verts[a];
        const glm::dvec3& pb = soup.verts[b];
        if (pa.x != pb.x) {
            return pa.x < pb.x;
        }
        if (pa.y != pb.y) {
            return pa.y < pb.y;
        }
        return pa.z < pb.z;
    });
    std::vector<glm::dvec3> verts;
    std::vector<std::size_t> remap(n);
    verts.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        remap[order[i]] = i;
        verts.push_back(soup.verts[order[i]]);
    }
    soup.verts = std::move(verts);
    for (auto& face : soup.faces) {
        for (std::size_t& vi : face) {
            vi = remap[vi];
        }
    }
    // Faces keep their cyclic order (geometry!); only their SEQUENCE is sorted,
    // by the sorted vertex-index set as the key.
    std::sort(soup.faces.begin(), soup.faces.end(), [](const auto& a, const auto& b) {
        std::vector<std::size_t> ka(a), kb(b);
        std::sort(ka.begin(), ka.end());
        std::sort(kb.begin(), kb.end());
        return ka < kb;
    });
}

Soup snapshot(const Nef& body, bool triangulate) {
    std::vector<CPoint> points;
    std::vector<std::vector<std::size_t>> polygons;
    CGAL::convert_nef_polyhedron_to_polygon_soup(body, points, polygons, triangulate);
    Soup out;
    out.verts.reserve(points.size());
    for (const CPoint& p : points) {
        out.verts.push_back(
            {CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z())});
    }
    out.faces = std::move(polygons);
    canonicalize(out);
    return out;
}

glm::dvec3 soupFaceNormal(const Soup& soup, const std::vector<std::size_t>& face) {
    glm::dvec3 n{0.0};
    const std::size_t count = face.size();
    for (std::size_t i = 0; i < count; ++i) {
        const glm::dvec3& a = soup.verts[face[i]];
        const glm::dvec3& b = soup.verts[face[(i + 1) % count]];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    const double len = glm::length(n);
    return len > 1e-12 ? n / len : glm::dvec3{0.0, 1.0, 0.0};
}

double soupFaceArea(const Soup& soup, const std::vector<std::size_t>& face) {
    glm::dvec3 n{0.0};
    const std::size_t count = face.size();
    for (std::size_t i = 0; i < count; ++i) {
        const glm::dvec3& a = soup.verts[face[i]];
        const glm::dvec3& b = soup.verts[face[(i + 1) % count]];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    return 0.5 * glm::length(n);
}

// First triangle hit of the ray o + t*dir, t > 0 (Möller–Trumbore). 1e30 miss.
double rayExit(const Soup& tris, const glm::dvec3& o, const glm::dvec3& dir) {
    double best = 1e30;
    for (const auto& face : tris.faces) {
        for (std::size_t i = 1; i + 1 < face.size(); ++i) {
            const glm::dvec3& a = tris.verts[face[0]];
            const glm::dvec3& b = tris.verts[face[i]];
            const glm::dvec3& c = tris.verts[face[i + 1]];
            const glm::dvec3 e1 = b - a;
            const glm::dvec3 e2 = c - a;
            const glm::dvec3 pv = glm::cross(dir, e2);
            const double det = glm::dot(e1, pv);
            if (std::abs(det) < 1e-15) {
                continue;
            }
            const double inv = 1.0 / det;
            const glm::dvec3 tv = o - a;
            const double u = glm::dot(tv, pv) * inv;
            if (u < -1e-9 || u > 1.0 + 1e-9) {
                continue;
            }
            const glm::dvec3 qv = glm::cross(tv, e1);
            const double v = glm::dot(dir, qv) * inv;
            if (v < -1e-9 || u + v > 1.0 + 1e-9) {
                continue;
            }
            const double t = glm::dot(e2, qv) * inv;
            if (t > 1e-7) {
                best = std::min(best, t);
            }
        }
    }
    return best;
}

// --- Corner cuts ---------------------------------------------------------------

// CGAL Nef booleans can hit internal assertions on exact tangent/coplanar
// contacts (e_below assertion in SNC_FM_decorator). Retry the operation with a
// tiny deterministic perturbation; give up after a few attempts (the wedge is
// skipped, the body stays valid). All retries are seed-deterministic.
template <typename BuildOp>
bool tryBoolean(Nef& body, BuildOp&& op) {
    for (int attempt = 0; attempt < 4; ++attempt) {
        try {
            Nef result = op(attempt);
            body = std::move(result);
            return true;
        } catch (const std::exception& e) {
            // CGAL assertion violations, bad_any_cast & co on degenerate
            // tangent contacts — retry with a perturbed operand.
            spdlog::warn("tryBoolean: CGAL exception ({}), retry {}/4", e.what(), attempt + 1);
        }
    }
    return false;
}

// Tiny deterministic tilt for retry attempts (1e-5 rad per attempt).
glm::dvec3 tilted(const glm::dvec3& n, int attempt) {
    if (attempt == 0) {
        return n;
    }
    glm::dvec3 u, v;
    planeBasis(n, u, v);
    const double ang = 1e-5 * static_cast<double>(attempt);
    return glm::normalize(n * std::cos(ang) + u * std::sin(ang));
}

void applyCornerCut(Nef& body, const StoneCutParams& params, double height, double extent,
    std::mt19937_64& rng) {
    const Soup soup = snapshot(body, /*triangulate=*/false);
    if (soup.verts.empty()) {
        return;
    }

    glm::dvec3 center{0.0};
    for (const glm::dvec3& v : soup.verts) {
        center += v;
    }
    center /= static_cast<double>(soup.verts.size());

    // Vertex adjacency from shared polygon edges (incident edge lengths).
    std::vector<std::vector<int>> adj(soup.verts.size());
    for (const auto& face : soup.faces) {
        const std::size_t n = face.size();
        for (std::size_t i = 0; i < n; ++i) {
            const int a = static_cast<int>(face[i]);
            const int b = static_cast<int>(face[(i + 1) % n]);
            adj[a].push_back(b);
            adj[b].push_back(a);
        }
    }

    std::vector<int> candidates;
    for (int i = 0; i < static_cast<int>(soup.verts.size()); ++i) {
        if (soup.verts[i].y > 0.1 * height) {
            candidates.push_back(i);
        }
    }
    if (candidates.empty()) {
        return;
    }
    const int corner = candidates[static_cast<std::size_t>(uni01(rng) * candidates.size()) %
                                  candidates.size()];
    const glm::dvec3 cv = soup.verts[corner];

    double meanLen = 0.0;
    for (const int nb : adj[corner]) {
        meanLen += glm::length(soup.verts[nb] - cv);
    }
    meanLen /= std::max(adj[corner].size(), std::size_t{1});
    if (meanLen < 1e-9) {
        return;
    }

    glm::dvec3 dir = cv - center;
    if (glm::length(dir) < 1e-9) {
        return;
    }
    dir = glm::normalize(dir);
    glm::dvec3 u, v;
    planeBasis(dir, u, v);
    const double tiltCone = std::tan(params.cutTiltDeg * kPi / 180.0);
    glm::dvec3 n = dir + tiltCone * (uniSigned(rng) * u + uniSigned(rng) * v);
    if (glm::length(n) < 1e-9) {
        return;
    }
    n = glm::normalize(n);

    const double depth = params.cutDepth * meanLen * (0.6 + 0.8 * uni01(rng));
    tryBoolean(body, [&](int attempt) {
        const glm::dvec3 nn = tilted(n, attempt);
        return body * nefPlaneClip(nn, glm::dot(nn, cv) - depth, extent);
    });
}

// --- Grooves / pits ------------------------------------------------------------

// Area-weighted face pick, bottom faces excluded (the base stays flat).
int pickSoupFace(const Soup& soup, std::mt19937_64& rng) {
    double total = 0.0;
    for (const auto& face : soup.faces) {
        if (soupFaceNormal(soup, face).y > -0.5) {
            total += soupFaceArea(soup, face);
        }
    }
    if (total <= 0.0) {
        return -1;
    }
    double ticket = uni01(rng) * total;
    for (std::size_t i = 0; i < soup.faces.size(); ++i) {
        if (soupFaceNormal(soup, soup.faces[i]).y <= -0.5) {
            continue;
        }
        ticket -= soupFaceArea(soup, soup.faces[i]);
        if (ticket <= 0.0) {
            return static_cast<int>(i);
        }
    }
    return static_cast<int>(soup.faces.size()) - 1;
}

void applyGroove(Nef& body, const StoneCutParams& params, double minExtent, std::mt19937_64& rng) {
    const Soup soup = snapshot(body, /*triangulate=*/false);
    const int fi = pickSoupFace(soup, rng);
    if (fi < 0) {
        return;
    }
    const auto& face = soup.faces[fi];
    const std::size_t n = face.size();
    const glm::dvec3 m = soupFaceNormal(soup, face);

    const std::size_t e = static_cast<std::size_t>(uni01(rng) * n) % n;
    const glm::dvec3& ea = soup.verts[face[e]];
    const glm::dvec3& eb = soup.verts[face[(e + 1) % n]];
    glm::dvec3 d = eb - ea;
    if (glm::length(d) < 1e-9) {
        return;
    }
    d = glm::normalize(d);
    const glm::dvec3 s = glm::normalize(glm::cross(d, m));

    glm::dvec3 centroid{0.0};
    for (const std::size_t vi : face) {
        centroid += soup.verts[vi];
    }
    centroid /= static_cast<double>(n);
    const glm::dvec3 q = centroid * 0.6 + ((ea + eb) * 0.5) * 0.4;

    const Soup tris = snapshot(body, /*triangulate=*/true);
    const double exitDist = rayExit(tris, q - m * 1e-6, -m);
    double depth = params.grooveDepth * minExtent;
    if (exitDist < 1e29) {
        depth = std::min(depth, 0.6 * exitDist);
    }
    const glm::dvec3 p0 = q - m * depth;

    // Chord extent along d within the face polygon.
    double tMin = -1e30, tMax = 1e30;
    for (std::size_t i = 0; i < n; ++i) {
        const glm::dvec3 a2 = soup.verts[face[i]] - q;
        const glm::dvec3 b2 = soup.verts[face[(i + 1) % n]] - q;
        const double da = glm::dot(a2, d), sa = glm::dot(a2, s);
        const double db = glm::dot(b2, d), sb = glm::dot(b2, s);
        if ((sa < 0.0) != (sb < 0.0)) {
            const double t = da + (db - da) * (-sa / (sb - sa));
            tMin = std::max(tMin, std::min(t, 0.0));
            tMax = std::min(tMax, std::max(t, 0.0));
        }
    }
    double halfLen = 4.0 * minExtent; // full chord default
    if (params.grooveLen < 0.999f && tMin > -1e29 && tMax < 1e29) {
        halfLen = 0.5 * (tMax - tMin) * params.grooveLen;
        halfLen = std::min(halfLen, std::min(-tMin, tMax));
    }
    // The prism must reach above the surface everywhere along its length.
    const double hgt = depth + 4.0 * minExtent;

    tryBoolean(body, [&](int attempt) {
        // On retry, rotate the groove axis slightly within the face plane.
        glm::dvec3 dd = d;
        if (attempt > 0) {
            const double ang = 1e-5 * static_cast<double>(attempt);
            dd = glm::normalize(d * std::cos(ang) - s * std::sin(ang));
        }
        return body -
            nefVPrism(p0, dd, m, params.grooveAngleDeg * kPi / 180.0, halfLen, hgt);
    });
}

void applyPit(Nef& body, const StoneCutParams& params, double minExtent, std::mt19937_64& rng) {
    const Soup soup = snapshot(body, /*triangulate=*/false);
    const int fi = pickSoupFace(soup, rng);
    if (fi < 0) {
        return;
    }
    const auto& face = soup.faces[fi];
    const std::size_t n = face.size();
    const glm::dvec3 m = soupFaceNormal(soup, face);

    glm::dvec3 centroid{0.0};
    for (const std::size_t vi : face) {
        centroid += soup.verts[vi];
    }
    centroid /= static_cast<double>(n);
    const glm::dvec3& rv = soup.verts[face[static_cast<std::size_t>(uni01(rng) * n) % n]];
    const glm::dvec3 p = centroid + (rv - centroid) * (0.5 * uni01(rng));

    const Soup tris = snapshot(body, /*triangulate=*/true);
    const double exitDist = rayExit(tris, p - m * 1e-6, -m);
    double depth = params.pitDepth * minExtent;
    if (exitDist < 1e29) {
        depth = std::min(depth, 0.5 * exitDist);
    }
    const glm::dvec3 p0 = p - m * depth;

    glm::dvec3 u, v;
    planeBasis(m, u, v);
    const double gamma = params.pitAngleDeg * kPi / 180.0;
    const double az0 = 2.0 * kPi * uni01(rng);
    glm::dvec3 dirK[3];
    for (int k = 0; k < 3; ++k) {
        const double az = az0 + 2.0 * kPi * static_cast<double>(k) / 3.0;
        const glm::dvec3 t = u * std::cos(az) + v * std::sin(az);
        dirK[k] = m * std::cos(gamma) + t * std::sin(gamma);
    }
    const double hgt = depth + 4.0 * minExtent;
    tryBoolean(body, [&](int attempt) {
        glm::dvec3 dk[3]{dirK[0], dirK[1], dirK[2]};
        for (auto& k : dk) {
            k = tilted(k, attempt);
        }
        return body - nefPitCone(p0, dk, hgt);
    });
}

// --- Emission ------------------------------------------------------------------

// Per-facet tint: quantized (normal, offset) plane key -> stable hash.
float planeTint(const glm::dvec3& n, double offset, std::uint64_t seed, float jitter) {
    const auto q = [](double x) { return static_cast<long long>(std::llround(x * 512.0)); };
    std::uint64_t key = seed;
    key = mix64(key ^ static_cast<std::uint64_t>(q(n.x)));
    key = mix64(key ^ static_cast<std::uint64_t>(q(n.y)));
    key = mix64(key ^ static_cast<std::uint64_t>(q(n.z)));
    key = mix64(key ^ static_cast<std::uint64_t>(q(offset)));
    const float h = static_cast<float>(static_cast<double>(key >> 11) * (1.0 / 9007199254740992.0));
    return 1.0f + jitter * (h - 0.5f) * 2.0f;
}

} // namespace

StoneMesh generateCutStone(const StoneCutParams& params) {
    std::mt19937_64 rng(mixSeed(params.seed));

    const double sx = std::max(static_cast<double>(params.sizeX), 0.05);
    const double sz = std::max(static_cast<double>(params.sizeZ), 0.05);
    const double h = std::max(static_cast<double>(params.height), 0.05);
    const double minExtent = std::min({sx, sz, h});
    const double clipExtent = 64.0 * std::max({sx, sz, h});

    // Starting parallelepiped: base on y=0, centered on the XZ origin.
    Nef body = nefBox({-sx, 0.0, -sz}, {sx, h, sz});

    for (int cut = 0, cuts = std::clamp(params.cuts, 0, 64); cut < cuts; ++cut) {
        applyCornerCut(body, params, h, clipExtent, rng);
    }
    for (int g = 0, grooves = std::clamp(params.grooves, 0, 8); g < grooves; ++g) {
        applyGroove(body, params, minExtent, rng);
    }
    for (int g = 0, pits = std::clamp(params.pits, 0, 8); g < pits; ++g) {
        applyPit(body, params, minExtent, rng);
    }

    // Ground clip: keep y >= -sink.
    tryBoolean(body, [&](int attempt) {
        return body * nefPlaneClip(
            tilted(glm::dvec3{0.0, -1.0, 0.0}, attempt),
            static_cast<double>(params.sink), clipExtent);
    });

    StoneMesh out;
    const Soup soup = snapshot(body, /*triangulate=*/true);
    if (soup.verts.empty()) {
        return out;
    }

    // Family light: same fixed sun + hemisphere as the rest of the playground.
    const glm::dvec3 sun = glm::normalize(glm::dvec3{-0.55, 0.80, -0.35});
    const glm::dvec3 albedo{0.74, 0.68, 0.57}; // warm beige, tmp/rock_example
    const std::uint64_t seedBits = mixSeed(params.seed);

    glm::dvec3 emin{1e30}, emax{-1e30};
    for (const auto& face : soup.faces) {
        for (std::size_t i = 1; i + 1 < face.size(); ++i) {
            const glm::dvec3& a = soup.verts[face[0]];
            const glm::dvec3& b = soup.verts[face[i]];
            const glm::dvec3& c = soup.verts[face[i + 1]];
            glm::dvec3 n = glm::cross(b - a, c - a);
            const double len = glm::length(n);
            if (len < 1e-15) {
                continue;
            }
            n /= len;
            const double diff = std::max(glm::dot(n, sun), 0.0);
            const double up = n.y * 0.5 + 0.5;
            const double light = std::min(0.40 + 0.30 * up + 0.55 * diff, 1.25);
            const double tint =
                planeTint(n, glm::dot(n, a), seedBits, params.tintJitter);
            const glm::dvec3 rgb = albedo * (light * tint);
            for (const glm::dvec3& v : {a, b, c}) {
                out.pos.insert(out.pos.end(), {
                    static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)});
                out.nrm.insert(out.nrm.end(), {
                    static_cast<float>(n.x), static_cast<float>(n.y), static_cast<float>(n.z)});
                out.col.insert(out.col.end(), {
                    static_cast<float>(rgb.x), static_cast<float>(rgb.y), static_cast<float>(rgb.z), 1.0f});
                emin = glm::min(emin, v);
                emax = glm::max(emax, v);
                ++out.triCount;
            }
        }
    }
    out.triCount /= 3;
    out.extentMin = glm::vec3(emin);
    out.extentMax = glm::vec3(emax);
    return out;
}
