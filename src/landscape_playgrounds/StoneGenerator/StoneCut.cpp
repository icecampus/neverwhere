#include "pch.h"

#include "StoneCut.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <random>
#include <vector>

#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Aff_transformation_3.h>
#include <CGAL/Nef_polyhedron_3.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/boost/graph/convert_nef_polyhedron_to_polygon_mesh.h>

#include <spdlog/spdlog.h>

namespace {

using CK = CGAL::Exact_predicates_exact_constructions_kernel;
using Nef = CGAL::Nef_polyhedron_3<CK>;
using CMesh = CGAL::Surface_mesh<CK::Point_3>;
using CPoint = CK::Point_3;
using AffT = CGAL::Aff_transformation_3<CK>;

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

// Box rotated by `yaw` about the Y axis through its own XZ centre, standing on
// `baseY`. Corners are computed in double and every face is fan-triangulated by
// nefFromFaces, so an off-axis box needs no exact trigonometry: Nef only ever
// sees triangles, and those are coplanar by definition.
Nef nefYawBox(
    const glm::dvec2& center, double halfX, double halfZ, double baseY, double height,
    double yaw) {
    const double cs = std::cos(yaw);
    const double sn = std::sin(yaw);
    const glm::dvec2 ax{cs * halfX, sn * halfX};
    const glm::dvec2 az{-sn * halfZ, cs * halfZ};
    const double y1 = baseY + height;
    // 0..3 bottom ring, 4..7 top ring, same angular order.
    std::vector<glm::dvec3> v;
    v.reserve(8);
    for (const double y : {baseY, y1}) {
        for (const glm::dvec2 s : {glm::dvec2{-1.0, -1.0}, glm::dvec2{1.0, -1.0},
                                   glm::dvec2{1.0, 1.0}, glm::dvec2{-1.0, 1.0}}) {
            const glm::dvec2 p = center + ax * s.x + az * s.y;
            v.push_back({p.x, y, p.y});
        }
    }
    // Every edge must be walked once in each direction or Surface_mesh rejects
    // the second face as non-manifold and the operand silently loses a wall.
    // The ring above is clockwise seen from +Y (x cross z = -y), so the bottom
    // ring reads forward, the top ring reversed, and each side wall walks its
    // bottom edge backwards.
    std::vector<std::vector<int>> f = {
        {0, 1, 2, 3}, // bottom (-Y)
        {7, 6, 5, 4}, // top (+Y)
    };
    for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) % 4;
        f.push_back({j, i, i + 4, j + 4});
    }
    return nefFromFaces(v, f);
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

// --- Plateau ------------------------------------------------------------------

// Area-weighted centroid of the plateau: the up-facing faces in the top third of
// the body. Rim chamfers (45 degrees) and flank facets are excluded by the
// normal test, so what is left is the flat-ish crown — a point that is known to
// lie ON the stone's top surface, unlike (centre.xz, yTop), which floats in the
// air above the low side of a slanted plateau. Empty when the body has no crown
// left to protect.
std::optional<glm::dvec3> plateauCentre(const Soup& soup) {
    if (soup.verts.empty()) {
        return std::nullopt;
    }
    double yLo = 1e30;
    double yHi = -1e30;
    for (const glm::dvec3& v : soup.verts) {
        yLo = std::min(yLo, v.y);
        yHi = std::max(yHi, v.y);
    }
    const double crownY = yLo + 0.6 * (yHi - yLo);
    glm::dvec3 acc{0.0};
    double total = 0.0;
    for (const auto& face : soup.faces) {
        if (soupFaceNormal(soup, face).y < 0.9) {
            continue;
        }
        glm::dvec3 c{0.0};
        for (const std::size_t vi : face) {
            c += soup.verts[vi];
        }
        c /= static_cast<double>(face.size());
        if (c.y < crownY) {
            continue;
        }
        const double a = soupFaceArea(soup, face);
        acc += c * a;
        total += a;
    }
    if (total < 1e-12) {
        return std::nullopt;
    }
    return acc / total;
}

// --- Base footprint ----------------------------------------------------------
// 2D (x,z) polygon of the stone's resting face. Booleans may split the base
// into several coplanar facets, so the footprint is recovered by cancelling
// interior directed edges of all downward near-ground faces and walking the
// remaining boundary loop.

using Poly2 = std::vector<glm::dvec2>;

double polyArea(const Poly2& p) {
    double a = 0.0;
    for (std::size_t i = 0, n = p.size(); i < n; ++i) {
        const glm::dvec2& u = p[i];
        const glm::dvec2& w = p[(i + 1) % n];
        a += u.x * w.y - w.x * u.y;
    }
    return 0.5 * a;
}

Poly2 baseFootprint(const Soup& soup) {
    double groundY = 1e30;
    for (const glm::dvec3& v : soup.verts) {
        groundY = std::min(groundY, v.y);
    }
    if (groundY > 1e29) {
        return {};
    }
    std::map<std::pair<int, int>, int> edges;
    for (const auto& face : soup.faces) {
        if (soupFaceNormal(soup, face).y > -0.9) {
            continue;
        }
        double fy = 0.0;
        for (const std::size_t vi : face) {
            fy += soup.verts[vi].y;
        }
        if (fy / static_cast<double>(face.size()) > groundY + 1e-4) {
            continue; // a down-facing notch ceiling, not the resting face
        }
        const std::size_t n = face.size();
        for (std::size_t i = 0; i < n; ++i) {
            const int a = static_cast<int>(face[i]);
            const int b = static_cast<int>(face[(i + 1) % n]);
            const auto rev = edges.find({b, a});
            if (rev != edges.end()) {
                edges.erase(rev); // interior edge shared by two downward facets
            } else {
                edges[{a, b}] = 1;
            }
        }
    }
    Poly2 best;
    double bestArea = -1.0;
    while (!edges.empty()) {
        const int start = edges.begin()->first.first;
        const int guardMax = static_cast<int>(edges.size()) + 8;
        int cur = start;
        Poly2 loop;
        for (int guard = 0; guard < guardMax; ++guard) {
            loop.push_back({soup.verts[static_cast<std::size_t>(cur)].x,
                            soup.verts[static_cast<std::size_t>(cur)].z});
            int nxt = -1;
            for (auto it = edges.lower_bound({cur, -1});
                 it != edges.end() && it->first.first == cur; ++it) {
                nxt = it->first.second;
                edges.erase(it);
                break;
            }
            if (nxt < 0 || nxt == start) {
                break;
            }
            cur = nxt;
        }
        const double a = std::abs(polyArea(loop));
        if (a > bestArea) {
            bestArea = a;
            best = std::move(loop);
        }
    }
    return best;
}

// Sutherland–Hodgman clip of the footprint against the cut plane's trace at
// y=0 (n.x·x + n.z·z <= d). Returns true iff the removed half-space actually
// bites into the footprint (i.e. the cut touches the base).
bool clipFootprint(Poly2& poly, const glm::dvec3& n, double d) {
    if (std::hypot(n.x, n.z) < 1e-12) {
        // Horizontal plane: everything below/above it — the base is fully
        // shaved only when the kept half-space excludes the ground plane.
        if (d >= 0.0) {
            return false;
        }
        poly.clear();
        return true;
    }
    const auto val = [&](const glm::dvec2& p) { return n.x * p.x + n.z * p.y - d; };
    bool touched = false;
    for (const glm::dvec2& p : poly) {
        if (val(p) > 0.0) {
            touched = true;
            break;
        }
    }
    if (!touched) {
        return false;
    }
    Poly2 out;
    const std::size_t m = poly.size();
    for (std::size_t i = 0; i < m; ++i) {
        const glm::dvec2 a = poly[i];
        const glm::dvec2 b = poly[(i + 1) % m];
        const double va = val(a), vb = val(b);
        const bool ina = va <= 0.0, inb = vb <= 0.0;
        if (ina) {
            out.push_back(a);
        }
        if (ina != inb) {
            out.push_back(a + (va / (va - vb)) * (b - a));
        }
    }
    poly = std::move(out);
    return true;
}

bool pointInPoly(const Poly2& p, const glm::dvec2& q) {
    bool inside = false;
    for (std::size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
        const glm::dvec2& a = p[i];
        const glm::dvec2& b = p[j];
        if ((a.y > q.y) != (b.y > q.y) &&
            q.x < (b.x - a.x) * (q.y - a.y) / (b.y - a.y) + a.x) {
            inside = !inside;
        }
    }
    return inside;
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

// --- Boolean retries -----------------------------------------------------------

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

// --- Macro form: massif, taper, top --------------------------------------------

// A body's XZ silhouette and vertical span — everything the macro stages and
// the cluster placement need to aim at the real geometry instead of the
// starting box (a yawed massif reaches further on the diagonal than its
// half-extents suggest, and each taper cut shrinks it again).
struct BodyForm {
    std::vector<glm::dvec2> xz;
    glm::dvec2 center{0.0};
    double yBase = 0.0;
    double yTop = 0.0;

    double height() const { return yTop - yBase; }

    // Support radius: how far the silhouette reaches from `center` along `dir`.
    double radius(const glm::dvec2& dir) const {
        double best = 0.0;
        for (const glm::dvec2& p : xz) {
            best = std::max(best, glm::dot(p - center, dir));
        }
        return best;
    }

    double maxRadius() const {
        double best = 0.0;
        for (const glm::dvec2& p : xz) {
            best = std::max(best, glm::length(p - center));
        }
        return best;
    }
};

BodyForm bodyForm(const Soup& soup) {
    BodyForm form;
    if (soup.verts.empty()) {
        return form;
    }
    glm::dvec2 lo{1e30, 1e30};
    glm::dvec2 hi{-1e30, -1e30};
    form.yBase = 1e30;
    form.yTop = -1e30;
    form.xz.reserve(soup.verts.size());
    for (const glm::dvec3& v : soup.verts) {
        const glm::dvec2 p{v.x, v.z};
        form.xz.push_back(p);
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
        form.yBase = std::min(form.yBase, v.y);
        form.yTop = std::max(form.yTop, v.y);
    }
    form.center = (lo + hi) * 0.5;
    return form;
}

BodyForm bodyForm(const Nef& body) {
    return bodyForm(snapshot(body, /*triangulate=*/false));
}

// Support radius of a yawed box along `dir` (both unit vectors in XZ).
double yawBoxSupport(const glm::dvec2& dir, double halfX, double halfZ, double yaw) {
    const glm::dvec2 ax{std::cos(yaw), std::sin(yaw)};
    const glm::dvec2 az{-ax.y, ax.x};
    return std::abs(glm::dot(dir, ax)) * halfX + std::abs(glm::dot(dir, az)) * halfZ;
}

// Stage 1 — the massif. Lobe 0 is the main box; the rest are smaller, yawed
// shoulders unioned onto its flanks, all flush with its top, so `height` stays
// the true stone height for every downstream consumer (placement, imprint
// centre, cluster).
//
// The geometry of "two boxes overlapping" is the whole difficulty here, and it
// bites in two ways.
//
// Sideways: wherever two walls cross at a shallow angle they leave a razor-thin
// fin instead of a step. So neither the protrusion nor the tangential width is
// sampled and hoped for — both are solved for. The shoulder clears the main box
// along its azimuth by a visible margin, its yaw is kept away from zero so the
// crossings are never near-parallel, and its tangential reach is scaled down
// until its side walls sit well inside the main box.
//
// Vertically: shoulders are flush by construction. A shorter shoulder shows its
// own top as a horizontal shelf whose width is exactly the protrusion, hanging
// off the wall with a sharp overhang under it — on screen that is a bright slit,
// or at larger protrusions a pulled-out drawer, and neither reads as stone.
// Flush shoulders only widen the silhouette and break the four-wall symmetry,
// which is all that is wanted from a single mass; stepping down in height is
// what the cluster's companion stones are for.
Nef buildMassif(
    const StoneCutParams& params, const glm::dvec3& offset, double sx, double sz, double h,
    std::mt19937_64& rng) {
    Nef body = nefBox(
        {offset.x - sx, offset.y, offset.z - sz},
        {offset.x + sx, offset.y + h, offset.z + sz});

    const int lobes = std::clamp(params.lobes, 1, 4);
    const double sizeF = std::clamp(static_cast<double>(params.lobeSize), 0.35, 1.0);
    const double protrudeF = std::clamp(static_cast<double>(params.lobeSpread), 0.0, 0.8);
    const double yawMax = static_cast<double>(params.lobeYawDeg) * kPi / 180.0;

    constexpr double kMinYaw = 8.0 * kPi / 180.0; // never near-parallel walls
    constexpr double kMinProtrude = 0.12;         // step must be visible, in main-radius units
    constexpr double kMaxTangential = 0.8;        // side walls stay inside the main box
    constexpr double kMinTangential = 0.35;       // narrower than this reads as a fin

    for (int i = 1; i < lobes; ++i) {
        double lsx = sx * sizeF * (0.8 + 0.4 * uni01(rng));
        double lsz = sz * sizeF * (0.8 + 0.4 * uni01(rng));
        const double az = 2.0 * kPi * uni01(rng);
        const double yawSpan = std::max(yawMax - kMinYaw, 0.0);
        const double yaw = (uni01(rng) < 0.5 ? -1.0 : 1.0) * (kMinYaw + uni01(rng) * yawSpan);

        const glm::dvec2 dir{std::cos(az), std::sin(az)};
        const glm::dvec2 tan{-dir.y, dir.x};
        const double mainR = std::abs(dir.x) * sx + std::abs(dir.y) * sz;
        const double mainT = std::abs(tan.x) * sx + std::abs(tan.y) * sz;

        // Pull the tangential reach inside the main box's own.
        double lobeT = yawBoxSupport(tan, lsx, lsz, yaw);
        if (lobeT > kMaxTangential * mainT) {
            const double k = kMaxTangential * mainT / lobeT;
            lsx *= k;
            lsz *= k;
            lobeT = kMaxTangential * mainT;
        }
        if (lobeT < kMinTangential * mainT) {
            continue; // a fin is worse than no shoulder at all
        }

        // Protrusion: as far out as asked, but never so far that the shoulder
        // stops overlapping the main box (it would become a separate solid).
        const double lobeR = yawBoxSupport(dir, lsx, lsz, yaw);
        const double protrude = std::min(protrudeF * mainR, 1.2 * lobeR);
        if (protrude < kMinProtrude * mainR) {
            continue; // too small to read as a step; a fin is worse than nothing
        }
        const glm::dvec2 c =
            glm::dvec2{offset.x, offset.z} + dir * (mainR + protrude - lobeR);

        if (!tryBoolean(body, [&](int attempt) {
                return body +
                    nefYawBox(c, lsx, lsz, offset.y, h, yaw + 1e-5 * static_cast<double>(attempt));
            })) {
            spdlog::warn("buildMassif: lobe union failed, lobe skipped");
        }
    }
    return body;
}

// Stage 2 — one taper (batter) plane, tilted `theta` off vertical around
// azimuth `az`. Its trace sits at radius `R - inset + (yTop - y)*tan(theta)`, so
// it bites `inset` at the top and slides outward going down; at the base that is
// `R - inset + h*tan(theta) >= R` for any `inset <= h*tan(theta)` — which is
// exactly how `inset` is defined below. Hence the base is untouched by
// construction and the footprint filters never have to look at these cuts.
void applyTaperCut(
    Nef& body, double az, double thetaRad, double reach, double topKeep, double extent) {
    const BodyForm form = bodyForm(body);
    if (form.xz.empty() || form.height() < 1e-6) {
        return;
    }
    const glm::dvec2 dir{std::cos(az), std::sin(az)};
    const double R = form.radius(dir);
    if (R < 1e-6) {
        return;
    }
    const double tanT = std::tan(thetaRad);
    // Clamped so a single flank can never slice the body in half, and so the
    // plateau keeps at least `topKeep` of radius along this azimuth: flanks are
    // applied one after another, and without a floor a few of them stacking on
    // neighbouring azimuths meet in the middle and pinch the top into a cone.
    const double inset =
        std::min({reach * form.height() * tanT, 0.45 * R, std::max(R - topKeep, 0.0)});
    if (inset < 1e-6) {
        return;
    }
    const double cs = std::cos(thetaRad);
    const double sn = std::sin(thetaRad);
    const glm::dvec3 n{dir.x * cs, sn, dir.y * cs};
    const glm::dvec2 a = form.center + dir * (R - inset);
    const glm::dvec3 anchor{a.x, form.yTop, a.y};
    tryBoolean(body, [&](int attempt) {
        const glm::dvec3 nn = tilted(n, attempt);
        return body * nefPlaneClip(nn, glm::dot(nn, anchor), extent);
    });
}

// Stage 3a — tilt the plateau. The plane runs through the top-centre point and
// leans by `slant` toward `az`, shaving up to R*tan(slant) on that side and
// nothing on the far one: a slanted top face rather than a chamfer. The slant is
// clamped so the plane's lowest trace stays well above the base.
void applyTopSlant(Nef& body, double az, double slantRad, double extent) {
    const BodyForm form = bodyForm(body);
    const double R = form.maxRadius();
    if (form.xz.empty() || R < 1e-6 || form.height() < 1e-6 || slantRad < 1e-6) {
        return;
    }
    const double tanS = std::min(std::tan(slantRad), 0.6 * form.height() / R);
    if (tanS < 1e-6) {
        return;
    }
    const glm::dvec2 dir{std::cos(az), std::sin(az)};
    const glm::dvec3 n = glm::normalize(glm::dvec3{dir.x * tanS, 1.0, dir.y * tanS});
    const glm::dvec3 anchor{form.center.x, form.yTop, form.center.y};
    tryBoolean(body, [&](int attempt) {
        const glm::dvec3 nn = tilted(n, attempt);
        return body * nefPlaneClip(nn, glm::dot(nn, anchor), extent);
    });
}

// Stage 3b — chamfer the top rim: a 45-degree plane anchored `bevel` inside the
// rim corner along the (outward, up) diagonal. Removed iff `rho + y > R + yTop -
// bevel*sqrt(2)`, so at the base that threshold is >= R while
// `bevel <= h/sqrt(2)`: the chamfer cannot creep down onto the footprint.
void applyRimBevel(Nef& body, double az, double bevel, double extent) {
    const BodyForm form = bodyForm(body);
    if (form.xz.empty() || form.height() < 1e-6) {
        return;
    }
    const glm::dvec2 dir{std::cos(az), std::sin(az)};
    const double R = form.radius(dir);
    const double b = std::min({bevel, 0.5 * form.height(), 0.5 * R});
    if (R < 1e-6 || b < 1e-6) {
        return;
    }
    const double s = 1.0 / std::sqrt(2.0);
    const glm::dvec3 n{dir.x * s, s, dir.y * s};
    const glm::dvec2 rimXz = form.center + dir * R;
    const glm::dvec3 rim{rimXz.x, form.yTop, rimXz.y};
    tryBoolean(body, [&](int attempt) {
        const glm::dvec3 nn = tilted(n, attempt);
        return body * nefPlaneClip(nn, glm::dot(nn, rim) - b, extent);
    });
}

// --- Corner cuts ---------------------------------------------------------------

// One corner cut. All vertices are candidates — bottom corners included — but
// a cut whose removed wedge reaches the base must pass the base filters
// (steep dihedral, footprint support floor, centroid inside) and the per-stone
// quota of base-touching cuts; rejected picks are resampled a few times, then
// the cut is skipped. Grazing cuts at the base would either bevel the rim all
// around ("rounded" look) or shave the whole footprint (floating stone).
void applyCornerCut(Nef& body, const StoneCutParams& params, double extent, double baseArea0,
    std::mt19937_64& rng, int& baseCutsUsed, int baseCutsMax) {
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

    const Poly2 footprint = baseFootprint(soup);
    const std::optional<glm::dvec3> crown = plateauCentre(soup);
    const double steepLimit = std::cos(params.baseCutAngleDeg * kPi / 180.0);

    for (int pick = 0; pick < 8; ++pick) {
        const int corner = static_cast<int>(uni01(rng) * soup.verts.size()) %
            static_cast<int>(soup.verts.size());
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
        const double d0 = glm::dot(n, cv) - depth;

        // The plateau is protected the same way the footprint is: a chip may bite
        // the rim, but a plane that sweeps across the crown itself turns the stone
        // into a shard with a roof, and that is the single most damaging thing the
        // cut pass can do to the silhouette the macro stages just built.
        if (crown && glm::dot(n, *crown) > d0) {
            continue;
        }

        if (!footprint.empty()) {
            Poly2 clipped = footprint;
            if (clipFootprint(clipped, n, d0)) {
                // The wedge bites into the footprint: base-touching cut.
                if (baseCutsUsed >= baseCutsMax) {
                    continue;
                }
                if (std::abs(n.y) > steepLimit) {
                    continue; // grazing plane: rim bevel or base shave
                }
                if (polyArea(clipped) < params.baseMinArea * baseArea0) {
                    continue; // would eat too much of the support
                }
                if (!pointInPoly(clipped, {center.x, center.z})) {
                    continue; // would saw the support out from under the body
                }
                if (tryBoolean(body, [&](int attempt) {
                        const glm::dvec3 nn = tilted(n, attempt);
                        return body * nefPlaneClip(nn, glm::dot(nn, cv) - depth, extent);
                    })) {
                    ++baseCutsUsed;
                }
                return;
            }
        }
        tryBoolean(body, [&](int attempt) {
            const glm::dvec3 nn = tilted(n, attempt);
            return body * nefPlaneClip(nn, glm::dot(nn, cv) - depth, extent);
        });
        return;
    }
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

// Body volume from the triangle soup (outward-oriented -> positive).
double bodyVolume(const Nef& body) {
    const Soup soup = snapshot(body, /*triangulate=*/true);
    double v = 0.0;
    for (const auto& face : soup.faces) {
        for (std::size_t i = 1; i + 1 < face.size(); ++i) {
            const glm::dvec3& a = soup.verts[face[0]];
            const glm::dvec3& b = soup.verts[face[i]];
            const glm::dvec3& c = soup.verts[face[i + 1]];
            v += glm::dot(a, glm::cross(b, c));
        }
    }
    return std::abs(v) / 6.0;
}

// --- Body assembly -----------------------------------------------------------

// Subtract `tool` from `body`, retrying with a nudged tool on CGAL degeneracies.
// `removed` (optional) reports the volume actually taken off — which is zero when
// the tool simply missed the body, so callers can tell "carved" from "no contact".
bool carveWith(Nef& body, const Nef& tool, double* removed = nullptr) {
    const double before = removed ? bodyVolume(body) : 0.0;
    const bool ok = tryBoolean(body, [&](int attempt) {
        if (attempt == 0) {
            return body - tool;
        }
        // Retry: nudge the tool by a micron — same geometry class, but the
        // exact-arithmetic degeneracy that tripped CGAL is sidestepped.
        Nef shifted(tool);
        const double e = 1e-6 * static_cast<double>(attempt);
        shifted.transform(AffT(CGAL::TRANSLATION, CK::Vector_3(e, 2.0 * e, -e)));
        return body - shifted;
    });
    if (removed) {
        *removed = ok ? std::max(before - bodyVolume(body), 0.0) : 0.0;
    }
    return ok;
}

// A copy of `body` scaled about (centreXz, h/2) — the imprint tool of the pair
// and cluster carves. Scaling up by `1 + gap/radius` makes the subtracted shape
// slightly larger than the stone itself, so the contact face comes out as its
// negative with a ~`gap` slit instead of a coincident surface (which is both
// ugly and the worst case for exact booleans).
Nef inflatedCopy(const Nef& body, const glm::dvec2& centreXz, double h, double scale) {
    Nef out(body);
    const CK::Vector_3 c(centreXz.x, 0.5 * h, centreXz.y);
    out.transform(
        AffT(CGAL::TRANSLATION, c) * AffT(CGAL::SCALING, scale) *
        AffT(CGAL::TRANSLATION, -c));
    return out;
}

// Stage 1 — the starting massif at `offset` (XZ centre, base at offset.y),
// optionally carved by `subtract`. carvedOk reports whether the subtraction
// survived the retries; carvedVol is how much material it actually removed
// (zero also when the tool misses the body entirely).
Nef carveStoneBox(
    const StoneCutParams& params, const glm::dvec3& offset, const Nef* subtract,
    bool* carvedOk = nullptr, double* carvedVol = nullptr) {
    if (carvedOk) {
        *carvedOk = true;
    }
    if (carvedVol) {
        *carvedVol = 0.0;
    }
    const double sx = std::max(static_cast<double>(params.sizeX), 0.05);
    const double sz = std::max(static_cast<double>(params.sizeZ), 0.05);
    const double h = std::max(static_cast<double>(params.height), 0.05);

    // The massif rng runs off a differently mixed seed so the lobe layout and
    // the cut pass (which seeds from `seed` directly) stay independent streams:
    // nudging one stage does not reshuffle the other.
    std::mt19937_64 massifRng(mix64(mixSeed(params.seed) ^ 0xA5A5A5A55A5A5A5Aull));
    Nef body = buildMassif(params, offset, sx, sz, h, massifRng);

    if (subtract) {
        const bool ok = carveWith(body, *subtract, carvedVol);
        if (!ok) {
            spdlog::warn("carveStoneBox: subtraction failed, body left uncarved");
        }
        if (carvedOk) {
            *carvedOk = ok;
        }
    }
    return body;
}

// Stage 2 — the macro form (taper, plateau slant, rim chamfer), then the small
// features: corner cuts (with the base filters), grooves, pits, ground clip.
// Order matters: the flanks decide the silhouette first and the chips land on
// the finished form, not on a box that later gets sliced away.
void applyStoneDetail(Nef& body, const StoneCutParams& params) {
    std::mt19937_64 rng(mixSeed(params.seed));

    const double sx = std::max(static_cast<double>(params.sizeX), 0.05);
    const double sz = std::max(static_cast<double>(params.sizeZ), 0.05);
    const double h = std::max(static_cast<double>(params.height), 0.05);
    const double minExtent = std::min({sx, sz, h});
    const double clipExtent = 64.0 * std::max({sx, sz, h});

    // Radius the plateau may never shrink below, in units of the smallest box
    // extent: the stones of the references are topped by a real flat face, and a
    // rock that comes to a point reads as a shard.
    constexpr double kPlateauKeep = 0.45;

    // Taper flanks: azimuths spread evenly around the circle with jitter, so a
    // handful of planes covers every side instead of clustering on one.
    const int taperCuts = std::clamp(params.taperCuts, 0, 12);
    const double taperBase = std::max(static_cast<double>(params.taperDeg), 0.0) * kPi / 180.0;
    const double taperReach = std::clamp(static_cast<double>(params.taperReach), 0.0, 1.0);
    const double taperWalls = std::clamp(static_cast<double>(params.taperWalls), 0.0, 0.75);
    const double taperAz0 = 2.0 * kPi * uni01(rng);
    for (int i = 0; i < taperCuts; ++i) {
        const double slot = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(taperCuts);
        // Jitter stays inside a quarter slot: neighbouring flanks must not be
        // able to land on the same azimuth, or the pair degenerates into a
        // sliver that CGAL has to resolve and the eye reads as a crease.
        const double az =
            taperAz0 + slot + uniSigned(rng) * 0.5 * kPi / static_cast<double>(taperCuts);
        // Both rolls are drawn either way so the wall/lean decision does not
        // reshuffle the angles of the flanks after it.
        const double wallRoll = uni01(rng);
        const double angRoll = uni01(rng);
        if (wallRoll < taperWalls) {
            continue; // left vertical on purpose — see the header on frustums
        }
        // The leaning flanks carry the whole batter, so they lean harder than the
        // nominal angle would give if it were spread over every side.
        applyTaperCut(
            body, az, taperBase * (0.85 + 0.75 * angRoll), taperReach, kPlateauKeep * minExtent,
            clipExtent);
    }

    applyTopSlant(
        body, 2.0 * kPi * uni01(rng),
        std::max(static_cast<double>(params.topSlantDeg), 0.0) * kPi / 180.0, clipExtent);

    const int rimCuts = std::clamp(params.rimBevelCuts, 0, 12);
    const double rimAz0 = 2.0 * kPi * uni01(rng);
    for (int i = 0; i < rimCuts; ++i) {
        const double slot = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(rimCuts);
        const double az =
            rimAz0 + slot + uniSigned(rng) * 0.5 * kPi / static_cast<double>(rimCuts);
        applyRimBevel(
            body, az,
            std::max(static_cast<double>(params.rimBevel), 0.0) * minExtent *
                (0.7 + 0.6 * uni01(rng)),
            clipExtent);
    }

    // The support floor is measured against the footprint the macro stages left
    // behind (a massif rests on more ground than its main box), not against the
    // nominal box area — otherwise lobes would silently license deeper nicks.
    const Poly2 startFootprint = baseFootprint(snapshot(body, /*triangulate=*/false));
    const double baseArea0 = startFootprint.empty()
        ? 4.0 * sx * sz
        : std::abs(polyArea(startFootprint));

    int baseCutsUsed = 0;
    const int baseCutsMax = static_cast<int>(
        std::clamp(params.cuts, 0, 64) * std::clamp(params.baseCutQuota, 0.0f, 1.0f));
    for (int cut = 0, cuts = std::clamp(params.cuts, 0, 64); cut < cuts; ++cut) {
        applyCornerCut(body, params, clipExtent, baseArea0, rng, baseCutsUsed, baseCutsMax);
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
}

// One stone body: box -> optional carve of another body -> corner cuts ->
// grooves -> pits -> ground clip. `offset` is the XZ center (and base height)
// of the starting box. `subtract` (nullable) is boolean-carved out of the box
// before any cutting; carvedOk reports whether that carve survived the retries.
Nef buildStoneBody(
    const StoneCutParams& params, const glm::dvec3& offset, const Nef* subtract,
    bool* carvedOk = nullptr) {
    Nef body = carveStoneBox(params, offset, subtract, carvedOk);
    applyStoneDetail(body, params);
    return body;
}

// Append a body's triangle soup to the mesh: flat normals, baked sun +
// hemisphere light, per-plane tint hash. `albedoMul` shades one body of a
// composition darker so the stones read as separate rocks.
void emitInto(
    StoneMesh& out, const Nef& body, std::uint64_t seedBits, float tintJitter,
    float albedoMul) {
    const Soup soup = snapshot(body, /*triangulate=*/true);
    if (soup.verts.empty()) {
        return;
    }

    // Family light: same fixed sun + hemisphere as the rest of the playground.
    const glm::dvec3 sun = glm::normalize(glm::dvec3{-0.55, 0.80, -0.35});
    const glm::dvec3 albedo = glm::dvec3{0.74, 0.68, 0.57} * static_cast<double>(albedoMul); // warm beige, tmp/rock_example

    glm::dvec3 emin{1e30}, emax{-1e30};
    int tris = 0;
    if (out.triCount > 0) {
        emin = glm::dvec3(out.extentMin);
        emax = glm::dvec3(out.extentMax);
    }
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

            // Exact arithmetic can place two corners closer together than a
            // float can tell apart: two flanks crossing the same edge at almost
            // the same height leave a sliver whose ends collapse on the cast
            // below. The solid stays watertight, but the emitted triangle would
            // become a degenerate zero-area fan entry — an unmatched edge for
            // any closedness check and a garbage normal for flat shading. The
            // collapse is a plain edge collapse, so dropping the slivers welds
            // the surface back together.
            const glm::vec3 fa{
                static_cast<float>(a.x), static_cast<float>(a.y), static_cast<float>(a.z)};
            const glm::vec3 fb{
                static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(b.z)};
            const glm::vec3 fc{
                static_cast<float>(c.x), static_cast<float>(c.y), static_cast<float>(c.z)};
            if (fa == fb || fb == fc || fc == fa) {
                continue;
            }
            const double diff = std::max(glm::dot(n, sun), 0.0);
            const double up = n.y * 0.5 + 0.5;
            const double light = std::min(0.40 + 0.30 * up + 0.55 * diff, 1.25);
            const double tint =
                planeTint(n, glm::dot(n, a), seedBits, tintJitter);
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
            }
            ++tris;
        }
    }
    out.triCount += tris;
    out.extentMin = glm::vec3(emin);
    out.extentMax = glm::vec3(emax);
}

} // namespace

StoneMesh generateCutStone(const StoneCutParams& params) {
    StoneMesh out;
    emitInto(
        out, buildStoneBody(params, glm::dvec3{0.0}, nullptr),
        mixSeed(params.seed), params.tintJitter, 1.0f);
    return out;
}

StoneMesh generateCutStonePair(const StonePairParams& params) {
    const StoneCutParams& big = params.big;
    const StoneCutParams& small = params.small;

    Nef n1 = buildStoneBody(big, glm::dvec3{0.0}, nullptr);

    const double sx1 = std::max(static_cast<double>(big.sizeX), 0.05);
    const double sz1 = std::max(static_cast<double>(big.sizeZ), 0.05);
    const double h1 = std::max(static_cast<double>(big.height), 0.05);
    const double sx2 = std::max(static_cast<double>(small.sizeX), 0.05);
    const double sz2 = std::max(static_cast<double>(small.sizeZ), 0.05);
    const double overlap = std::max(static_cast<double>(params.overlap), 0.0);
    const double gap = std::max(static_cast<double>(params.gap), 0.0);

    // Small box against the chosen side of the big bbox, penetrating `overlap`.
    glm::dvec3 offset{0.0};
    double sideExtent = sx1; // centroid-to-wall distance along the side axis
    switch (params.side) {
        case 0: offset = { sx1 + sx2 - overlap, 0.0, params.shift}; break;
        case 1: offset = {-sx1 - sx2 + overlap, 0.0, params.shift}; break;
        case 2: offset = {params.shift, 0.0,  sz1 + sz2 - overlap}; sideExtent = sz1; break;
        default: offset = {params.shift, 0.0, -sz1 - sz2 + overlap}; sideExtent = sz1; break;
    }

    // Inflate a copy of the big stone about its centroid so the carved pocket
    // ends up ~gap wider than the stone itself: after placement the stones
    // stay disjoint by a slit of roughly `gap` (uniform only approximately —
    // the scale is about the centroid, not a true offset).
    const double s = 1.0 + gap / sideExtent;
    Nef inflated(n1);
    const glm::dvec3 c{0.0, 0.5 * h1, 0.0};
    inflated.transform(
        AffT(CGAL::TRANSLATION, CK::Vector_3(c.x, c.y, c.z)) *
        AffT(CGAL::SCALING, s) *
        AffT(CGAL::TRANSLATION, CK::Vector_3(-c.x, -c.y, -c.z)));

    bool carved = true;
    Nef n2 = buildStoneBody(small, offset, &inflated, &carved);
    if (!carved) {
        // Carve exhausted its retries: fall back to the two boxes standing gap
        // apart (no imprint) rather than leaving an interpenetrating pair.
        const double push = overlap + gap;
        switch (params.side) {
            case 0: offset.x += push; break;
            case 1: offset.x -= push; break;
            case 2: offset.z += push; break;
            default: offset.z -= push; break;
        }
        n2 = buildStoneBody(small, offset, nullptr);
    }

    StoneMesh out;
    emitInto(out, n1, mixSeed(big.seed), big.tintJitter, 1.0f);
    emitInto(out, n2, mixSeed(small.seed), small.tintJitter, 0.88f);
    return out;
}

StoneMesh generateCutStoneCluster(const StoneClusterParams& params) {
    std::mt19937_64 rng(mixSeed(params.seed));

    struct Node {
        Nef body;
        BodyForm form;     // real silhouette: support radius for the next ring
        glm::dvec2 center; // XZ centre the starting box was placed at
        double sx, sz, h;  // starting-box extents
        int level;
    };

    // The diamond projection puts +X+Z nearer the viewer and lower on screen, so
    // that quadrant is the cluster's front; its bisector is the reference
    // azimuth for every placement decision.
    constexpr double kFrontAz = 0.25 * kPi;

    const double decay = std::clamp(static_cast<double>(params.decay), 0.2, 0.95);
    const double gap = std::max(static_cast<double>(params.gap), 0.0);
    const double overlap = std::max(static_cast<double>(params.overlap), 0.0);
    const double spread = std::clamp(static_cast<double>(params.spread), 0.0, 1.0);
    const double heightVar = std::clamp(static_cast<double>(params.heightVar), 0.0, 0.8);
    const double spireChance = std::clamp(static_cast<double>(params.spireChance), 0.0, 1.0);

    const double rootSx = std::max(static_cast<double>(params.base.sizeX), 0.05);
    const double rootSz = std::max(static_cast<double>(params.base.sizeZ), 0.05);
    const double rootH = std::max(static_cast<double>(params.base.height), 0.05);

    StoneMesh out;
    std::vector<Node> nodes;
    std::vector<std::size_t> frontier;
    const auto spawn = [&](Nef body, const glm::dvec2& center, double sx, double sz,
                           double h, int level, float albedoMul, int seed) {
        emitInto(out, body, mixSeed(seed), params.base.tintJitter, albedoMul);
        BodyForm form = bodyForm(body);
        form.center = center; // measure support from the placement anchor
        nodes.push_back({std::move(body), std::move(form), center, sx, sz, h, level});
        frontier.push_back(nodes.size() - 1);
    };

    spawn(
        buildStoneBody(params.base, glm::dvec3{0.0}, nullptr), {0.0, 0.0}, rootSx, rootSz,
        rootH, 0, 1.0f, params.base.seed);

    // BFS: every expanded node grows 1..maxChildren companions on a fan of
    // azimuths around it.
    for (std::size_t head = 0; head < frontier.size(); ++head) {
        const Node parent = nodes[frontier[head]]; // copy: `nodes` reallocates
        if (parent.level >= params.levels) {
            continue;
        }
        const int want = 1 + static_cast<int>(uni01(rng) * std::max(params.maxChildren, 1));

        int spawned = 0;
        for (int tries = 0; tries < 10 && spawned < want; ++tries) {
            // A spire is the one companion allowed to out-top its parent, and it
            // always goes BEHIND it. Both halves of that rule matter: a taller
            // stone in front would swallow the parent's silhouette, and a
            // shorter stone behind it would disappear — so the back half of the
            // circle is reserved for spires and nothing else. Height is capped
            // against the root so recursion cannot grow an ever-taller tower.
            const bool spire = uni01(rng) < spireChance;
            double ch = spire
                ? parent.h * (1.05 + 0.25 * uni01(rng))
                : parent.h * decay * (1.0 + heightVar * uniSigned(rng));
            ch = std::clamp(ch, 0.2, rootH * 1.3);

            const double arc = kPi * 0.5 * spread;
            const double az = kFrontAz + (spire ? kPi : 0.0) + uniSigned(rng) * arc;

            // The role decides the PROPORTIONS, not merely the size. Height is
            // picked first and the footprint follows from a target aspect,
            // because scaling x, z and height by one decay factor preserves the
            // parent's aspect forever: under a tall root every descendant is a
            // thinner copy of a tall block, which reads as a fang. Companions
            // are squat boulders piling at the foot; a spire is taller than its
            // parent but still has to be chunky enough to read as a mass.
            const double aspect = spire ? 1.25 + 0.35 * uni01(rng) : 0.80 + 0.35 * uni01(rng);
            // A companion stays subordinate in plan even when the aspect asks
            // for more, so the group keeps one clear hero.
            const double m = std::min(0.5 * ch / aspect, 0.95 * std::min(parent.sx, parent.sz));
            const double csx = std::max(m * (1.0 + 0.35 * uni01(rng)), 0.15);
            const double csz = std::max(m * (1.0 + 0.35 * uni01(rng)), 0.15);

            // Push the child out to the parent's real support radius along the
            // chosen direction, then back in by `overlap` so the imprint carve
            // has material to bite into.
            const glm::dvec2 dir{std::cos(az), std::sin(az)};
            const double childR = std::abs(dir.x) * csx + std::abs(dir.y) * csz;
            const double parentR = parent.form.radius(dir);
            const glm::dvec2 c = parent.center + dir * (parentR + childR - overlap);

            StoneCutParams cp = params.base;
            cp.seed = static_cast<int>(rng() % 100000);
            cp.sizeX = static_cast<float>(csx);
            cp.sizeZ = static_cast<float>(csz);
            cp.height = static_cast<float>(ch);

            // Inflated parent -> gap-preserving imprint (same trick as the pair).
            const Nef inflated = inflatedCopy(
                parent.body, parent.center, parent.h, 1.0 + gap / std::max(parentR, 0.05));

            bool carvedOk = false;
            double carvedVol = 0.0;
            Nef childBody =
                carveStoneBox(cp, {c.x, 0.0, c.y}, &inflated, &carvedOk, &carvedVol);
            const double boxVol = 8.0 * csx * csz * ch;
            if (!carvedOk || carvedVol < 1e-3 * boxVol) {
                continue; // slid past the parent's body — no real contact
            }

            // Siblings that reach into each other get the same imprint treatment
            // as the parent rather than being resampled away. Rejecting them
            // instead is what used to keep groups thin: every companion crowds the
            // same front arc, so the second and third one almost always touch the
            // first, and the fan collapsed back into a pair of stones.
            for (std::size_t oi = 0; oi < nodes.size(); ++oi) {
                const Node& other = nodes[oi];
                if (oi == frontier[head]) {
                    continue;
                }
                const glm::dvec2 delta = other.center - c;
                const double reach = std::hypot(csx, csz) + std::hypot(other.sx, other.sz);
                if (glm::length(delta) > reach) {
                    continue; // support circles cannot meet: nothing to carve
                }
                const glm::dvec2 toChild =
                    glm::length(delta) > 1e-9 ? -delta / glm::length(delta) : glm::dvec2{1.0, 0.0};
                const double otherR = std::max(other.form.radius(toChild), 0.05);
                carveWith(
                    childBody,
                    inflatedCopy(other.body, other.center, other.h, 1.0 + gap / otherR));
            }

            // The carves above cannot fail silently into an interpenetration, so
            // this is a last guard: if anything is still overlapping, drop the
            // candidate rather than emit two stones sharing a volume.
            bool collides = false;
            for (std::size_t oi = 0; oi < nodes.size() && !collides; ++oi) {
                if (oi == frontier[head]) {
                    continue;
                }
                try {
                    collides = bodyVolume(childBody * nodes[oi].body) > 1e-4 * boxVol;
                } catch (const std::exception&) {
                    collides = true; // CGAL tangent-contact hiccup: resample
                }
            }
            if (collides) {
                continue;
            }
            // A companion whittled down by its neighbours' imprints is a splinter,
            // not a stone.
            if (bodyVolume(childBody) < 0.25 * boxVol) {
                continue;
            }

            applyStoneDetail(childBody, cp);
            const float shade = static_cast<float>(std::pow(0.88, parent.level + 1));
            spawn(std::move(childBody), c, csx, csz, ch, parent.level + 1, shade, cp.seed);
            ++spawned;
        }
    }

    // --- Debris ----------------------------------------------------------------
    // Small blocks on the ground around the group. They are pushed past the
    // cluster's own support radius along their azimuth, so they cannot intersect
    // any stone and need no boolean collision test — only a cheap 2D spacing
    // check against each other.
    const int pebbles = std::clamp(params.pebbles, 0, 24);
    if (pebbles > 0) {
        BodyForm hull;
        hull.center = {0.0, 0.0};
        for (const Node& node : nodes) {
            hull.xz.insert(hull.xz.end(), node.form.xz.begin(), node.form.xz.end());
        }
        const double pebbleF = std::clamp(static_cast<double>(params.pebbleSize), 0.05, 0.6);
        std::vector<glm::dvec3> placed; // xz + radius
        for (int i = 0; i < pebbles; ++i) {
            // Debris reads only where it is not hidden by the massif, so the
            // scatter stays in the front half, walking the arc slot by slot.
            const double slot = static_cast<double>(i) / static_cast<double>(pebbles);
            const double az = kFrontAz + (slot - 0.5) * 1.5 * kPi + uniSigned(rng) * 0.3;
            const glm::dvec2 dir{std::cos(az), std::sin(az)};
            const double psx = rootSx * pebbleF * (0.5 + 0.5 * uni01(rng));
            const double psz = rootSz * pebbleF * (0.5 + 0.5 * uni01(rng));
            // Squat, and hugging the foot: debris standing as tall as it is wide
            // and set well back from the group reads as scattered crates, not as
            // rubble that fell off this rock.
            const double ph = 0.85 * std::min(psx, psz) * (0.8 + 0.5 * uni01(rng));
            const double pr = std::max(psx, psz);
            const double margin = rootSx * (0.02 + 0.22 * uni01(rng));
            const glm::dvec2 c = dir * (hull.radius(dir) + pr + margin);

            bool tooClose = false;
            for (const glm::dvec3& p : placed) {
                if (glm::length(glm::dvec2{p.x, p.y} - c) < pr + p.z + 0.15 * rootSx) {
                    tooClose = true;
                    break;
                }
            }
            if (tooClose) {
                continue;
            }

            // Debris keeps the family recipe but at a reduced feature count: at
            // this size the extra planes cost booleans and buy no silhouette.
            // The rim chamfer is the exception — it is a fraction of the min
            // extent, so at pebble scale the inherited value rounds to nothing
            // and leaves a bare box; widening it is what makes the debris read as
            // the same kind of rock as the massif above it.
            StoneCutParams pp = params.base;
            pp.seed = static_cast<int>(rng() % 100000);
            pp.sizeX = static_cast<float>(psx);
            pp.sizeZ = static_cast<float>(psz);
            pp.height = static_cast<float>(ph);
            pp.lobes = 1;
            pp.taperCuts = std::min(params.base.taperCuts, 3);
            pp.rimBevelCuts = std::max(std::min(params.base.rimBevelCuts, 3), 2);
            pp.rimBevel = std::max(params.base.rimBevel, 0.3f);
            pp.cuts = std::min(params.base.cuts, 3);
            pp.grooves = 0;
            pp.pits = 0;

            Nef body = buildStoneBody(pp, {c.x, 0.0, c.y}, nullptr);
            emitInto(out, body, mixSeed(pp.seed), pp.tintJitter, 0.88f);
            placed.push_back({c.x, c.y, pr});
        }
    }
    return out;
}
