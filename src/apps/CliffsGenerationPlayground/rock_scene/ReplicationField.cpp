#include "ReplicationField.h"

#include "TerrainField.h"

#include <cmath>

namespace render_playground {

double tileModuloAxis(double v, double tileSize) {
    return v - tileSize * std::floor(v / tileSize);
}

rock_fracture::Vector3 worldToTileLocalForFace(const rock_fracture::Vector3& p,
    const rock_fracture::Box& solid, CliffFace face, double tileSize) {
    const double half = tileSize * 0.5;

    switch (face) {
    case CliffFace::NegX: {
        const double wallX = solid[0][0];
        // Wall at local x=0 (tile center depth); inward into rock is +local x.
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
    const float band = m_spec.surfaceBand;

    if (m_spec.replicationMode == CliffReplicationMode::SingleFace) {
        if (!isFaceInReplicationBand(p, m_spec, solid, m_spec.cliffFace, band)) {
            return 1e30;
        }
        const rock_fracture::Vector3 local = worldToTileLocalForFace(p, solid, m_spec.cliffFace, m_tileSize);
        return m_tileRoot->Signed(local);
    }

    double t = 1e30;
    const CliffFace faces[] = {
        CliffFace::NegX,
        CliffFace::PosX,
        CliffFace::NegY,
        CliffFace::PosY,
    };
    for (CliffFace face : faces) {
        if (!isFaceInReplicationBand(p, m_spec, solid, face, band)) {
            continue;
        }
        const rock_fracture::Vector3 local = worldToTileLocalForFace(p, solid, face, m_tileSize);
        t = std::min(t, m_tileRoot->Signed(local));
    }
    return t;
}

} // namespace render_playground
