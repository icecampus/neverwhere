#include "ReplicationField.h"

#include "TerrainField.h"

#include <cmath>

namespace render_playground {

namespace {

double sampleTileWithNeighborBlend(rock_fracture::SDFNode* tileRoot, const rock_fracture::Vector3& local,
    double tileSize, int periodicAxisA, int periodicAxisB) {
    const double half = tileSize * 0.5;
    const double blend = tileSize * 0.12;
    double t = tileRoot->Signed(local);

    auto blendAxis = [&](int axis) {
        const double u = local[axis] + half;
        if (u < blend) {
            rock_fracture::Vector3 shifted = local;
            shifted[axis] += tileSize;
            t = std::min(t, tileRoot->Signed(shifted));
        } else if (u > tileSize - blend) {
            rock_fracture::Vector3 shifted = local;
            shifted[axis] -= tileSize;
            t = std::min(t, tileRoot->Signed(shifted));
        }
    };

    blendAxis(periodicAxisA);
    blendAxis(periodicAxisB);
    return t;
}

double evaluateFaceTile(rock_fracture::SDFNode* tileRoot, const rock_fracture::Vector3& p,
    const rock_fracture::Box& solid, CliffFace face, double tileSize) {
    const rock_fracture::Vector3 local = worldToTileLocalForFace(p, solid, face, tileSize);
    switch (face) {
    case CliffFace::NegX:
    case CliffFace::PosX:
        return sampleTileWithNeighborBlend(tileRoot, local, tileSize, 1, 2);
    case CliffFace::NegY:
    case CliffFace::PosY:
        return sampleTileWithNeighborBlend(tileRoot, local, tileSize, 0, 2);
    }
    return 1e30;
}

} // namespace

double tileModuloAxis(double v, double tileSize) {
    return v - tileSize * std::floor(v / tileSize);
}

rock_fracture::Vector3 worldToTileLocalForFace(const rock_fracture::Vector3& p,
    const rock_fracture::Box& solid, CliffFace face, double tileSize) {
    const double half = tileSize * 0.5;

    switch (face) {
    case CliffFace::NegX: {
        const double wallX = solid[0][0];
        const double depth = p.x - wallX;
        return rock_fracture::Vector3(
            depth,
            tileModuloAxis(p.y, tileSize) - half,
            tileModuloAxis(p.z, tileSize) - half);
    }
    case CliffFace::PosX: {
        const double wallX = solid[1][0];
        const double depth = wallX - p.x;
        return rock_fracture::Vector3(
            depth,
            tileModuloAxis(p.y, tileSize) - half,
            tileModuloAxis(p.z, tileSize) - half);
    }
    case CliffFace::NegY: {
        const double wallY = solid[0][1];
        const double depth = p.y - wallY;
        return rock_fracture::Vector3(
            tileModuloAxis(p.x, tileSize) - half,
            depth,
            tileModuloAxis(p.z, tileSize) - half);
    }
    case CliffFace::PosY: {
        const double wallY = solid[1][1];
        const double depth = wallY - p.y;
        return rock_fracture::Vector3(
            tileModuloAxis(p.x, tileSize) - half,
            depth,
            tileModuloAxis(p.z, tileSize) - half);
    }
    }
    return rock_fracture::Vector3(0);
}

SDFReplicationField::SDFReplicationField(rock_fracture::SDFNode* tileRoot, const SceneSpec& spec, double tileSize)
    : rock_fracture::SDFNode(tileRoot != nullptr ? tileRoot->box : rock_fracture::Box())
    , m_tileRoot(tileRoot)
    , m_spec(spec)
    , m_tileSize(tileSize)
    , m_halfTile(tileSize * 0.5) {}

double SDFReplicationField::Signed(const rock_fracture::Vector3& p) const {
    if (m_tileRoot == nullptr) {
        return 1e30;
    }

    const rock_fracture::Box solid = macroSolidBox(m_spec);

    if (m_spec.replicationMode == CliffReplicationMode::SingleFace) {
        if (!isInVerticalFaceSlab(p, m_spec, solid, m_spec.cliffFace)) {
            return 1e30;
        }
        return evaluateFaceTile(m_tileRoot, p, solid, m_spec.cliffFace, m_tileSize);
    }

    const CliffFace face = nearestVerticalFace(p, solid);
    if (!isInVerticalFaceSlab(p, m_spec, solid, face)) {
        return 1e30;
    }
    return evaluateFaceTile(m_tileRoot, p, solid, face, m_tileSize);
}

} // namespace render_playground
