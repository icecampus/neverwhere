#pragma once

#include <cstdint>
#include <random>
#include <vector>

#include <glm/glm.hpp>

// Polyhedral core of the stone generator (playground-local, no sokol/Qt).
// Everything is built by one operation — clipping a convex polyhedron by a
// plane (BSP-compiler style, doubles + eps). Base forms are plane-set
// factories; chamfers, ground clip and validation are more clips/reads on top.
//
// Conventions:
//  - StonePlane {n, d}: inside = dot(n, x) <= d, n unit length, outward.
//  - Face winding is CCW seen from outside; every face remembers the id of
//    the plane that created it (drives chamfer normals + per-face tint).
//  - y is up, rocks sit on the y=0 ground plane.

struct StonePlane {
    glm::dvec3 n{0.0, 1.0, 0.0};
    double d = 0.0;
    // Locked planes (the prism bottom cap) are skipped by jitterPlanes so the
    // base stays flat on the ground.
    bool lock = false;
};

struct StonePoly {
    struct Face {
        std::vector<int> idx;
        int planeId = -1; // index into the plane array that built the poly
    };
    std::vector<glm::dvec3> verts; // shared vertices (faces index into this)
    std::vector<Face> faces;
};

// --- Clipping ----------------------------------------------------------------

// Sutherland-Hodgman clip of every face + a cap face on the plane.
// Intersection vertices are shared between adjacent faces via an edge map, so
// the result stays watertight by construction.
void clipByPlane(StonePoly& poly, const StonePlane& plane, int planeId);

// Start from a big bounding box and clip by every plane (planeId = index).
StonePoly buildPolyhedron(const std::vector<StonePlane>& planes);

// Remove vertices no face references; reindex faces.
void compactPoly(StonePoly& poly);

// --- Base-form plane factories ------------------------------------------------

// Regular N-gonal prism: bottom cap on y=0 (locked), top cap at y=height,
// `sides` side planes at inradius `radius` around the y axis. taper shrinks the
// top radius linearly: r(top) = radius * (1 - taper); taper 0 = straight prism,
// ~0.25..0.4 = frustum, ->1 = pyramid. Box = sides 4, yaw 0.
std::vector<StonePlane> makePrismPlanes(
    int sides, double radius, double height, double taper, double yawRad);

// Tangent-plane ball: `count` planes whose normals follow a Fibonacci sphere,
// offsets radius * (1 + jitter * [-1,1]). Centered at the origin (the caller
// translates up for the ground sit). jitter 0 gives every plane a face.
std::vector<StonePlane> makeBallPlanes(
    int count, double radius, double offsetJitter, std::mt19937_64& rng);

// --- Modifiers ----------------------------------------------------------------

// Small random tilt (Rodrigues around two axes perpendicular to n) and offset
// drift (fraction of refSize) per plane; locked planes are skipped.
void jitterPlanes(
    std::vector<StonePlane>& planes,
    double tiltRad,
    double offsetFrac,
    double refSize,
    std::mt19937_64& rng);

// Chamfer clip planes for the current edges of poly (edge = pair of adjacent
// faces; their plane normals give the chamfer normal normalize(na + nb)).
// width = distance cut along each face. topOnly skips edges whose midpoint is
// not above half the poly height (the buried bottom keeps sharp edges).
// Appends nothing; the caller clips by the returned planes.
std::vector<StonePlane> chamferPlanes(
    const StonePoly& poly,
    const std::vector<StonePlane>& planes,
    double width,
    bool topOnly);

// Per-vertex hash noise in [-amp, amp] per axis. Vertices are shared between
// faces, so watertightness survives by construction.
void vertexNoise(StonePoly& poly, double amp, std::uint64_t seed);

void transformPoly(StonePoly& poly, const glm::dmat4& m);

// Plane through an affine transform: with x' = Lx + t, the plane n·x = d
// becomes n'·x' = d' with m = L^-T n, n' = m/|m|, d' = (d + m·t)/|m|.
// Keep the plane array in sync with transformPoly so validation and chamfer
// normals stay exact.
StonePlane transformPlane(const StonePlane& plane, const glm::dmat4& m);

void transformPlanes(std::vector<StonePlane>& planes, const glm::dmat4& m);

// Clip by the ground plane y >= -sink (cap gets planeId).
void groundClip(StonePoly& poly, double sink, int planeId);

// --- Inspection ----------------------------------------------------------------

glm::dvec3 faceNormal(const StonePoly& poly, const StonePoly::Face& face); // Newell

bool isWatertight(const StonePoly& poly); // every directed edge exactly twice
bool allInside(const StonePoly& poly, const std::vector<StonePlane>& planes, double eps);
