#include "pch.h"

#include "StoneCut.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
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

// One corner cut. All vertices are candidates — bottom corners included — but
// a cut whose removed wedge reaches the base must pass the base filters
// (steep dihedral, footprint support floor, centroid inside) and the per-stone
// quota of base-touching cuts; rejected picks are resampled a few times, then
// the cut is skipped. Grazing cuts at the base would either bevel the rim all
// around ("rounded" look) or shave the whole footprint (floating stone).
void applyCornerCut(Nef& body, const StoneCutParams& params, double extent,
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
    const double baseArea0 =
        4.0 * static_cast<double>(params.sizeX) * static_cast<double>(params.sizeZ);
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

// Stage 1 — the starting box at `offset` (XZ center, base at offset.y),
// optionally carved by `subtract`. carvedOk reports whether the subtraction
// survived the retries; carvedVol is how much material it actually removed
// (zero also when the tool misses the box entirely).
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

    Nef body = nefBox(
        {offset.x - sx, offset.y, offset.z - sz},
        {offset.x + sx, offset.y + h, offset.z + sz});

    if (subtract) {
        const Nef& operand = *subtract;
        const bool ok = tryBoolean(body, [&](int attempt) {
            if (attempt == 0) {
                return body - operand;
            }
            // Retry: nudge the tool by a micron — same geometry class, but the
            // exact-arithmetic degeneracy that tripped CGAL is sidestepped.
            Nef shifted(operand);
            const double e = 1e-6 * static_cast<double>(attempt);
            shifted.transform(AffT(CGAL::TRANSLATION, CK::Vector_3(e, 2.0 * e, -e)));
            return body - shifted;
        });
        if (!ok) {
            spdlog::warn("carveStoneBox: subtraction failed, body left uncarved");
        }
        if (carvedOk) {
            *carvedOk = ok;
        }
        if (ok && carvedVol) {
            *carvedVol = 8.0 * sx * sz * h - bodyVolume(body);
        }
    }
    return body;
}

// Stage 2 — corner cuts (with the base filters), grooves, pits, ground clip.
void applyStoneDetail(Nef& body, const StoneCutParams& params) {
    std::mt19937_64 rng(mixSeed(params.seed));

    const double sx = std::max(static_cast<double>(params.sizeX), 0.05);
    const double sz = std::max(static_cast<double>(params.sizeZ), 0.05);
    const double h = std::max(static_cast<double>(params.height), 0.05);
    const double minExtent = std::min({sx, sz, h});
    const double clipExtent = 64.0 * std::max({sx, sz, h});

    int baseCutsUsed = 0;
    const int baseCutsMax = static_cast<int>(
        std::clamp(params.cuts, 0, 64) * std::clamp(params.baseCutQuota, 0.0f, 1.0f));
    for (int cut = 0, cuts = std::clamp(params.cuts, 0, 64); cut < cuts; ++cut) {
        applyCornerCut(body, params, clipExtent, rng, baseCutsUsed, baseCutsMax);
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
        glm::dvec2 center; // box XZ center the stone was placed at
        double sx, sz, h;  // starting-box extents
        int level;
    };

    StoneMesh out;
    std::vector<Node> nodes;
    std::vector<std::size_t> frontier;
    const auto spawn = [&](Nef body, const glm::dvec2& center, double sx, double sz,
                           double h, int level, float albedoMul, int seed) {
        emitInto(out, body, mixSeed(seed), params.base.tintJitter, albedoMul);
        nodes.push_back({std::move(body), center, sx, sz, h, level});
        frontier.push_back(nodes.size() - 1);
    };

    {
        const double sx = std::max(static_cast<double>(params.base.sizeX), 0.05);
        const double sz = std::max(static_cast<double>(params.base.sizeZ), 0.05);
        const double h = std::max(static_cast<double>(params.base.height), 0.05);
        spawn(
            buildStoneBody(params.base, glm::dvec3{0.0}, nullptr), {0.0, 0.0}, sx, sz, h,
            0, 1.0f, params.base.seed);
    }

    // BFS: every expanded node tries to grow 1..maxChildren companions on its
    // camera-facing sides (+X / +Z — the ones the diamond projection shows).
    for (std::size_t head = 0; head < frontier.size(); ++head) {
        const Node parent = nodes[frontier[head]]; // copy: `nodes` reallocates
        if (parent.level >= params.levels) {
            continue;
        }
        const int want =
            1 + static_cast<int>(uni01(rng) * std::max(params.maxChildren, 1));
        const double decay = std::clamp(static_cast<double>(params.decay), 0.2, 0.95);
        const double gap = std::max(static_cast<double>(params.gap), 0.0);
        const double overlap = std::max(static_cast<double>(params.overlap), 0.0);
        const double over = std::clamp(static_cast<double>(params.overshoot), 0.0, 1.0);
        const double minContact =
            std::clamp(static_cast<double>(params.minContact), 0.05, 1.0);

        int spawned = 0;
        for (int tries = 0; tries < 8 && spawned < want; ++tries) {
            const bool sideX = uni01(rng) < 0.5; // +X or +Z, never the far sides
            const double csx = std::max(parent.sx * decay, 0.15);
            const double csz = std::max(parent.sz * decay, 0.15);
            const double ch = std::max(parent.h * decay * (0.8 + 0.4 * uni01(rng)), 0.2);
            const double parentHalf = sideX ? parent.sx : parent.sz; // side axis
            const double parentHalfT = sideX ? parent.sz : parent.sx; // tangent axis
            const double childHalfT = sideX ? csz : csx; // along the tangent
            // Slide window: from fully within the parent's face to sticking out
            // past its edge, but always facing the parent by >= minContact.
            const double fullIn = std::max(0.0, parentHalfT - childHalfT);
            const double wide = parentHalfT + childHalfT - minContact * childHalfT;
            const double maxShift = fullIn + over * (wide - fullIn);
            const double shift = uniSigned(rng) * maxShift;

            glm::dvec2 c = parent.center;
            if (sideX) {
                c.x += parent.sx + csx - overlap;
                c.y += shift;
            } else {
                c.y += parent.sz + csz - overlap;
                c.x += shift;
            }

            StoneCutParams cp = params.base;
            cp.seed = static_cast<int>(rng() % 100000);
            cp.sizeX = static_cast<float>(csx);
            cp.sizeZ = static_cast<float>(csz);
            cp.height = static_cast<float>(ch);

            // Inflated parent -> gap-preserving imprint (same trick as the pair).
            Nef inflated(parent.body);
            const double s = 1.0 + gap / parentHalf;
            const glm::dvec3 pc{parent.center.x, 0.5 * parent.h, parent.center.y};
            inflated.transform(
                AffT(CGAL::TRANSLATION, CK::Vector_3(pc.x, pc.y, pc.z)) *
                AffT(CGAL::SCALING, s) *
                AffT(CGAL::TRANSLATION, CK::Vector_3(-pc.x, -pc.y, -pc.z)));

            bool carvedOk = false;
            double carvedVol = 0.0;
            Nef childBody =
                carveStoneBox(cp, {c.x, 0.0, c.y}, &inflated, &carvedOk, &carvedVol);
            const double boxVol = 8.0 * csx * csz * ch;
            if (!carvedOk || carvedVol < 1e-3 * boxVol) {
                continue; // slid past the parent's body — no real contact
            }
            bool collides = false;
            for (std::size_t oi = 0; oi < nodes.size(); ++oi) {
                if (oi == frontier[head]) {
                    continue;
                }
                try {
                    if (bodyVolume(childBody * nodes[oi].body) > 1e-4 * boxVol) {
                        collides = true;
                        break;
                    }
                } catch (const std::exception&) {
                    collides = true; // CGAL tangent-contact hiccup: resample
                    break;
                }
            }
            if (collides) {
                continue;
            }

            applyStoneDetail(childBody, cp);
            const float shade = static_cast<float>(std::pow(0.88, parent.level + 1));
            spawn(std::move(childBody), c, csx, csz, ch, parent.level + 1, shade, cp.seed);
            ++spawned;
        }
    }
    return out;
}
