/*
 Port of Code/Source/blocks.cpp from Rock-fracturing (Axel Paris, MIT).
 Adapted to namespace rock_fracture, OpenMP optional, configurable MC resolution.
*/

#include "Blocks.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

// Source: https://github.com/leomccormack/convhull_3d
#define CONVHULL_3D_ENABLE
#include "Convhull3d.h"

// Source: https://github.com/aparis69/MarchingCubeCpp
#define MC_IMPLEM_ENABLE
#include "MC.h"

// Source: http://nothings.org/stb
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// Multithread field function computation
#ifdef ROCK_FRACTURE_USE_OPENMP
#include <omp.h>
#endif

// Warping displacement strength stored as global
namespace rock_fracture {
static ScalarField2D warpingField;
}

namespace rock_fracture {

SDFNode::SDFNode() {}

SDFNode::SDFNode(const Box& box) : box(box) {}

Vector3 SDFNode::Gradient(const Vector3& p) const {
    static const double Epsilon = 0.01;
    double x = Signed(Vector3(p[0] - Epsilon, p[1], p[2])) - Signed(Vector3(p[0] + Epsilon, p[1], p[2]));
    double y = Signed(Vector3(p[0], p[1] - Epsilon, p[2])) - Signed(Vector3(p[0], p[1] + Epsilon, p[2]));
    double z = Signed(Vector3(p[0], p[1], p[2] - Epsilon)) - Signed(Vector3(p[0], p[1], p[2] + Epsilon));
    return Vector3(x, y, z) * (0.5 / Epsilon);
}

SDFUnionSphereLOD::SDFUnionSphereLOD(SDFNode* a, SDFNode* b, double re)
    : SDFNode(Box(a->box, b->box).Extended(Vector3(re))),
      re(re),
      sphere(Sphere(box.Center(), box.Size().Max())) {
    e[0] = a;
    e[1] = b;
}

double SDFUnionSphereLOD::Signed(const Vector3& p) const {
    double sd = sphere.Distance(p);
    if (sd > re) return sd;
    double se = Math::Min(e[0]->Signed(p), e[1]->Signed(p));
    if (sd < sphere.Radius()) return se;
    double a = (sd - sphere.Radius()) / (re - sphere.Radius());
    return (1.0 - a) * se + a * sd;
}

SDFNode* SDFUnionSphereLOD::OptimizedBVH(std::vector<SDFNode*>& nodes, double re) {
    if (nodes.empty()) return nullptr;
    return OptimizedBVHRecursive(nodes, 0, int(nodes.size()), re);
}

SDFNode* SDFUnionSphereLOD::OptimizedBVHRecursive(std::vector<SDFNode*>& pts, int begin, int end, double re) {
    struct BVHPartitionPredicate {
        int axis;
        double cut;
        BVHPartitionPredicate(int a, double c) : axis(a), cut(c) {}
        bool operator()(SDFNode* p) const {
            return (p->box.Center()[axis] < cut);
        }
    };

    int nodeCount = end - begin;
    if (nodeCount <= 1) return pts[begin];

    Box bbox = pts[begin]->box;
    for (int i = begin + 1; i < end; i++) bbox = Box(bbox, pts[i]->box);

    int stretchedAxis = bbox.Diagonal().MaxIndex();
    double axisMiddleCut = (bbox[0][stretchedAxis] + bbox[1][stretchedAxis]) / 2.0;

    auto pmid = std::partition(pts.begin() + begin, pts.begin() + end, BVHPartitionPredicate(stretchedAxis, axisMiddleCut));

    int midIndex = std::distance(pts.begin(), pmid);
    if (midIndex == begin || midIndex == end) midIndex = (begin + end) / 2;

    SDFNode* left = OptimizedBVHRecursive(pts, begin, midIndex, re);
    SDFNode* right = OptimizedBVHRecursive(pts, midIndex, end, re);

    return new SDFUnionSphereLOD(left, right, re);
}

SDFGradientWarp::SDFGradientWarp(SDFNode* e) : e(e) {
    box = e->box;
}

double SDFGradientWarp::WarpingStrength(const Vector3& p, const Vector3& n) const {
    const double texScale = 0.1642;
    Vector2 x = Abs(Vector2(p[2], p[1])) * texScale;
    Vector2 y = Abs(Vector2(p[0], p[2])) * texScale;
    Vector2 z = Abs(Vector2(p[1], p[0])) * texScale;

    double tmp;
    x = Vector2(modf(x[0], &tmp), modf(x[1], &tmp));
    y = Vector2(modf(y[0], &tmp), modf(y[1], &tmp));
    z = Vector2(modf(z[0], &tmp), modf(z[1], &tmp));

    Vector3 ai = Abs(n);
    ai = ai / (ai[0] + ai[1] + ai[2]);

    return  ai[0] * warpingField.GetValueBilinear(x)
          + ai[1] * warpingField.GetValueBilinear(y)
          + ai[2] * warpingField.GetValueBilinear(z);
}

double SDFGradientWarp::Signed(const Vector3& p) const {
    Vector3 g = e->Gradient(p);
    double s = 0.65 * WarpingStrength(p, -Normalize(g));
    return e->Signed(p + g * s);
}

SDFBlock::SDFBlock(const std::vector<Plane>& pl, double sr)
    : SDFNode(Box(Plane::ConvexPoints(pl)).Extended(Vector3(0.01))) {
    planes = pl;
    smoothRadius = sr;
    box = box.Extended(Vector3(smoothRadius));
}

double SDFBlock::SmoothingPolynomial(double d1, double d2, double sr) const {
    double h = Math::Max(sr - Math::Abs(d1 - d2), 0.0) / sr;
    return Math::Min(d1, d2) - h * h * sr * 0.25;
}

double SDFBlock::Signed(const Vector3& p) const {
    double d = planes.at(0).Signed(p);
    for (size_t i = 1; i < planes.size(); i++) {
        double dd = planes.at(i).Signed(p);
        d = -SmoothingPolynomial(-d, -dd, smoothRadius);
    }
    return d;
}

static bool BreakFractureConstraint(const Vector3& p, const Vector3& c, const FractureSet& fractures) {
    bool intersect = false;
    double tmax = Magnitude(p - c);
    Ray ray = Ray(p, Normalize(c - p));
    for (int cIdx = 0; cIdx < fractures.Size(); cIdx++) {
        double t;
        if (fractures.At(cIdx).Intersect(ray, t) && t < tmax) {
            intersect = true;
            break;
        }
    }
    return intersect;
}

static bool CanBeLinkedToCluster(const Vector3& candidate, const std::vector<Vector3>& cluster, const FractureSet& fractures, double R_Max) {
    bool canBeLinked = true;
    for (int i = 0; i < (int)cluster.size(); i++) {
        Vector3 p = cluster[i];

        if (SquaredMagnitude(candidate - p) > R_Max) {
            canBeLinked = false;
            break;
        }

        for (int c = 0; c < fractures.Size(); c++) {
            double tmax = Magnitude(p - candidate);
            double t;
            if (fractures.At(c).Intersect(Ray(p, Normalize(candidate - p)), t) && t < tmax) {
                canBeLinked = false;
                break;
            }
        }
        if (!canBeLinked) break;
    }
    return canBeLinked;
}

void LoadImageFileForWarping(const char* str, double a, double b) {
    int nx, ny, n;
    unsigned char* idata = stbi_load(str, &nx, &ny, &n, 1);
    warpingField = ScalarField2D(nx, ny, Box2D(Vector2(0), Vector2(1)));
    for (int i = 0; i < nx; i++) {
        for (int j = 0; j < ny; j++) {
            unsigned char g = idata[i * ny + j];
            double t = double(g) / 255.0;
            warpingField.Set(i, j, t);
        }
    }
}

void GenerateProceduralWarpingField(double frequency, int octaves, int seed) {
    (void)seed;
    const int N = 256;
    warpingField = ScalarField2D(N, N, Box2D(Vector2(0), Vector2(1)));
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            const double u = double(i) / double(N - 1);
            const double v = double(j) / double(N - 1);
            const double t = PerlinNoise::fBm(
                Vector3(u * frequency, v * frequency, 0.0),
                1.0, 1.0, octaves);
            warpingField.Set(i, j, t);
        }
    }
    // Normalize to [0, 1] so the warp strength stays comparable to the
    // greyscale image used in the paper.
    const double m = warpingField.Max();
    const double minV = warpingField.Min();
    const double range = (m - minV) > 1e-9 ? (m - minV) : 1.0;
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            const double cur = warpingField.Get(i, j);
            warpingField.Set(i, j, (cur - minV) / range);
        }
    }
}

PointSet3 PoissonSamplingBox(const Box& box, double r, int n) {
    PointSet3 set;
    double c = 4.0 * r * r;
    for (int i = 0; i < n; i++) {
        Vector3 t = box.RandomInside();
        bool hit = false;
        for (int j = 0; j < set.Size(); j++) {
            if (SquaredMagnitude(t - set.At(j)) < c) {
                hit = true;
                break;
            }
        }
        if (hit == false) set.pts.push_back(t);
    }
    return set;
}

FractureSet GenerateFractures(FractureType type, const Box& box, double r) {
    FractureSet set;
    if (type == FractureType::Equidimensional) {
        Box inflatedRockDomain = box.Extended(Vector3(-3.0));
        PointSet3 samples = PoissonSamplingBox(inflatedRockDomain, 3.0, 1000);
        for (int i = 0; i < samples.Size(); i++) {
            int a = Random::Integer() % 3;
            Vector3 axis = a == 0 ? Vector3(1, 0, 0) : a == 1 ? Vector3(0, 1, 0) : Vector3(0, 0, 1);
            double rr = Random::Uniform(10.0, 15.0);
            set.fractures.push_back(Circle(samples.At(i), axis, rr));
        }
    } else if (type == FractureType::Rhombohedral) {
        Box inflatedRockDomain = box.Extended(Vector3(-3.0));
        PointSet3 samples = PoissonSamplingBox(inflatedRockDomain, 3.0, 1000);
        for (int i = 0; i < samples.Size(); i++) {
            int a = Random::Integer() % 3;
            Vector3 axis = a == 0 ? Vector3(0.5, 0.5, 0) : a == 1 ? Vector3(0, 0.5, 0.5) : Vector3(0.5, 0, 0.5);
            double rr = Random::Uniform(10.0, 15.0);
            set.fractures.push_back(Circle(samples.At(i), axis, rr));
        }
    } else if (type == FractureType::Polyhedral) {
        PointSet3 samples = PoissonSamplingBox(box, 1.0, 1000);
        for (int i = 0; i < samples.Size(); i++) {
            double rr = Random::Uniform(2.0, 12.0);
            Vector3 axis = Sphere(Vector3(0), 1.0).RandomSurface();
            set.fractures.push_back(Circle(samples.At(i), axis, rr));
        }
    } else if (type == FractureType::Tabular) {
        const int fracturing = 10;
        Vector3 p = box[0];
        p.x += box.Diagonal()[0] / 2.0;
        p.z += box.Diagonal()[1] / 2.0;
        double step = box.Size()[2] / double(fracturing);
        double noiseStep = step / 10.0;
        for (int i = 0; i < fracturing - 1; i++) {
            p.y += step + Random::Uniform(-noiseStep, noiseStep);
            Vector3 axis = Vector3(0, -1, 0);
            set.fractures.push_back(Circle(p, axis, 20.0));
        }
    }
    (void)r;
    return set;
}

std::vector<BlockCluster> ComputeBlockClusters(PointSet3& set, const FractureSet& frac) {
    const int  allPtsSize     = set.Size();
    const float R_Neighborhood = 2.5f * 2.5f;

    std::vector<std::vector<int>> graph;
    graph.resize(allPtsSize);
    for (int i = 0; i < allPtsSize; i++) {
        Vector3 p = set.At(i);
        for (int j = 0; j < allPtsSize; j++) {
            if (i == j) continue;
            Vector3 q = set.At(j);
            if (SquaredMagnitude(p - q) < R_Neighborhood && !BreakFractureConstraint(p, q, frac))
                graph[i].push_back(j);
        }
    }

    const float R_Max_Block = 10.5f * 10.5f;
    std::vector<bool> visitedFlags;
    visitedFlags.resize(allPtsSize, false);
    std::vector<BlockCluster> clusters;
    for (int j = 0; j < allPtsSize; j++) {
        std::vector<int> toVisit;
        toVisit.push_back(j);
        std::vector<Vector3> cluster;
        while (!toVisit.empty()) {
            int index = toVisit.back();
            toVisit.pop_back();
            if (visitedFlags[index]) continue;

            Vector3 q = set.At(index);
            if (!CanBeLinkedToCluster(q, cluster, frac, R_Max_Block)) continue;
            cluster.push_back(q);
            visitedFlags[index] = true;
            for (int i = 0; i < (int)graph[index].size(); i++) {
                if (visitedFlags[graph[index][i]]) continue;
                toVisit.push_back(graph[index][i]);
            }
        }
        if (cluster.size() > 10) {
            clusters.push_back({ cluster });
        }
    }
    return clusters;
}

SDFNode* ComputeBlockSDF(const std::vector<BlockCluster>& clusters) {
    std::vector<SDFNode*> primitives;
    for (size_t k = 0; k < clusters.size(); k++) {
        std::vector<Vector3> allPts = clusters[k].pts;

        int n = int(allPts.size());
        if (n <= 4) continue;
        ch_vertex* vertices = new ch_vertex[n];
        for (int i = 0; i < n; i++)
            vertices[i] = { allPts[i][0], allPts[i][1], allPts[i][2] };
        int* faceIndices = NULL;
        int nFaces;
        convhull_3d_build(vertices, n, &faceIndices, &nFaces);
        if (nFaces == 0) continue;

        std::vector<Plane> planes;
        for (int i = 0; i < nFaces; i++) {
            const int j = i * 3;
            Vector3 v1 = Vector3(float(vertices[faceIndices[j + 0]].x), float(vertices[faceIndices[j + 0]].y), float(vertices[faceIndices[j + 0]].z));
            Vector3 v2 = Vector3(float(vertices[faceIndices[j + 1]].x), float(vertices[faceIndices[j + 1]].y), float(vertices[faceIndices[j + 1]].z));
            Vector3 v3 = Vector3(float(vertices[faceIndices[j + 2]].x), float(vertices[faceIndices[j + 2]].y), float(vertices[faceIndices[j + 2]].z));
            Vector3 pn = Triangle(v1, v2, v3).Normal();
            Vector3 pc = Triangle(v1, v2, v3).Center();
            planes.push_back(Plane(pc, pn));
        }

        auto convex = Plane::ConvexPoints(planes);
        if (convex.size() > 0) {
            const double smoothRadius = 0.25;
            primitives.push_back(new SDFGradientWarp(new SDFBlock(planes, smoothRadius)));
        }

        delete[] vertices;
        delete[] faceIndices;
    }
    return SDFUnionSphereLOD::OptimizedBVH(primitives, 0.5);
}

MC::mcMesh PolygonizeSDF(const Box& box, SDFNode* node, int resolution) {
    const int n = (resolution > 1) ? resolution : 2;
    MC::MC_FLOAT* field = new MC::MC_FLOAT[n * n * n];
    Vector3 cellDiagonal = (box[1] - box[0]) / (n - 1);
    {
#ifdef ROCK_FRACTURE_USE_OPENMP
#pragma omp parallel for num_threads(16) shared(field)
#endif
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                for (int k = 0; k < n; k++) {
                    Vector3 p = Vector3(box[0][0] + i * cellDiagonal[0], box[0][1] + j * cellDiagonal[1], box[0][2] + k * cellDiagonal[2]);
                    field[(k * n + j) * n + i] = (MC::MC_FLOAT)node->Signed(p);
                }
            }
        }
    }

    MC::mcMesh mesh;
    MC::marching_cube(field, (MC::muint)n, (MC::muint)n, (MC::muint)n, mesh);
    delete[] field;
    return mesh;
}

} // namespace rock_fracture
