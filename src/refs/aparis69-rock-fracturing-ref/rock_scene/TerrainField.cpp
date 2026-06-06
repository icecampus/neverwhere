#include "TerrainField.h"

#include <algorithm>
#include <cmath>

namespace render_playground {

namespace {

double boxSignedDistance(const rock_fracture::Vector3& p, const rock_fracture::Box& box) {
    const rock_fracture::Vector3 center = box.Center();
    const rock_fracture::Vector3 half = box.Size() * 0.5;
    const rock_fracture::Vector3 q{
        std::abs(p.x - center.x) - half.x,
        std::abs(p.y - center.y) - half.y,
        std::abs(p.z - center.z) - half.z,
    };

    const double outsideX = std::max(q.x, 0.0);
    const double outsideY = std::max(q.y, 0.0);
    const double outsideZ = std::max(q.z, 0.0);
    const double outside = std::sqrt(outsideX * outsideX + outsideY * outsideY + outsideZ * outsideZ);

    const double inside = std::min(std::max(q.x, std::max(q.y, q.z)), 0.0);
    return outside + inside;
}

double faceDistance(const MacroFaceDistances& d, CliffFace face) {
    switch (face) {
    case CliffFace::NegX: return d.negX;
    case CliffFace::PosX: return d.posX;
    case CliffFace::NegY: return d.negY;
    case CliffFace::PosY: return d.posY;
    }
    return 1e30;
}

bool insideFaceFootprint(const rock_fracture::Vector3& p, const rock_fracture::Box& solid,
    CliffFace face, double margin) {
    switch (face) {
    case CliffFace::NegX:
    case CliffFace::PosX:
        return p.y >= solid[0][1] - margin && p.y <= solid[1][1] + margin
            && p.z >= solid[0][2] - margin && p.z <= solid[1][2] + margin;
    case CliffFace::NegY:
    case CliffFace::PosY:
        return p.x >= solid[0][0] - margin && p.x <= solid[1][0] + margin
            && p.z >= solid[0][2] - margin && p.z <= solid[1][2] + margin;
    }
    return false;
}

} // namespace

double MacroFaceDistances::minVertical() const {
    return std::min(std::min(negX, posX), std::min(negY, posY));
}

double MacroFaceDistances::minHorizontal() const {
    return std::min(bottom, top);
}

rock_fracture::Box macroSolidBox(const SceneSpec& spec) {
    const double ox = (spec.sceneSizeX - spec.cubeSize) * 0.5;
    const double oy = (spec.sceneSizeY - spec.cubeSize) * 0.5;
    const double oz = (spec.sceneSizeZ - spec.cubeSize) * 0.5;
    return rock_fracture::Box(
        rock_fracture::Vector3(ox, oy, oz),
        rock_fracture::Vector3(ox + spec.cubeSize, oy + spec.cubeSize, oz + spec.cubeSize));
}

rock_fracture::Box sceneBoundingBox(const SceneSpec& spec) {
    const float outer = spec.scenePadding + spec.protrusionMargin;
    const float domain = spec.cubeSize + 2.0f * outer;
    return rock_fracture::Box(
        rock_fracture::Vector3(0.0),
        rock_fracture::Vector3(domain, domain, domain));
}

MacroFaceDistances macroFaceDistances(const rock_fracture::Vector3& p, const rock_fracture::Box& solid) {
    MacroFaceDistances d{};
    d.negX = p.x - solid[0][0];
    d.posX = solid[1][0] - p.x;
    d.negY = p.y - solid[0][1];
    d.posY = solid[1][1] - p.y;
    d.bottom = p.z - solid[0][2];
    d.top = solid[1][2] - p.z;
    return d;
}

SDFMacroBox::SDFMacroBox(const SceneSpec& spec)
    : rock_fracture::SDFNode(sceneBoundingBox(spec))
    , m_spec(spec)
    , m_solid(macroSolidBox(spec)) {}

double SDFMacroBox::Signed(const rock_fracture::Vector3& p) const {
    return boxSignedDistance(p, m_solid);
}

double cliffWallProximity(const rock_fracture::Vector3& p, const SceneSpec& spec) {
    const rock_fracture::Box solid = macroSolidBox(spec);
    if (!isInVerticalFaceSlab(p, spec, solid, spec.cliffFace)) {
        return 1e30;
    }

    switch (spec.cliffFace) {
    case CliffFace::NegX:
        return p.x - solid[0][0];
    case CliffFace::PosX:
        return solid[1][0] - p.x;
    case CliffFace::NegY:
        return p.y - solid[0][1];
    case CliffFace::PosY:
        return solid[1][1] - p.y;
    }
    return 1e30;
}

double verticalFaceDepth(const rock_fracture::Vector3& p, const rock_fracture::Box& solid, CliffFace face) {
    switch (face) {
    case CliffFace::NegX:
        return p.x - solid[0][0];
    case CliffFace::PosX:
        return solid[1][0] - p.x;
    case CliffFace::NegY:
        return p.y - solid[0][1];
    case CliffFace::PosY:
        return solid[1][1] - p.y;
    }
    return 1e30;
}

CliffFace nearestVerticalFace(const rock_fracture::Vector3& p, const rock_fracture::Box& solid) {
    const double dNegX = std::abs(p.x - solid[0][0]);
    const double dPosX = std::abs(p.x - solid[1][0]);
    const double dNegY = std::abs(p.y - solid[0][1]);
    const double dPosY = std::abs(p.y - solid[1][1]);

    CliffFace best = CliffFace::NegX;
    double bestDist = dNegX;
    if (dPosX < bestDist) {
        bestDist = dPosX;
        best = CliffFace::PosX;
    }
    if (dNegY < bestDist) {
        bestDist = dNegY;
        best = CliffFace::NegY;
    }
    if (dPosY < bestDist) {
        best = CliffFace::PosY;
    }
    return best;
}

bool isInVerticalFaceSlab(const rock_fracture::Vector3& p, const SceneSpec& spec,
    const rock_fracture::Box& solid, CliffFace face) {
    const double depth = verticalFaceDepth(p, solid, face);
    const double band = spec.surfaceBand;
    const double protrusion = spec.protrusionMargin;
    if (depth < -protrusion || depth > band) {
        return false;
    }

    if (depth >= 0.0) {
        const MacroFaceDistances d = macroFaceDistances(p, solid);
        const double faceDist = faceDistance(d, face);
        if (faceDist > band) {
            return false;
        }
        const double minH = d.minHorizontal();
        if (minH < faceDist - 1e-6) {
            return false;
        }
        return true;
    }

    return insideFaceFootprint(p, solid, face, protrusion);
}

bool isInCliffReplicationZone(const rock_fracture::Vector3& p, const SceneSpec& spec,
    const rock_fracture::Box& solid) {
    if (spec.replicationMode == CliffReplicationMode::SingleFace) {
        return isInVerticalFaceSlab(p, spec, solid, spec.cliffFace);
    }

    const CliffFace faces[] = {
        CliffFace::NegX,
        CliffFace::PosX,
        CliffFace::NegY,
        CliffFace::PosY,
    };
    for (CliffFace face : faces) {
        if (isInVerticalFaceSlab(p, spec, solid, face)) {
            return true;
        }
    }
    return false;
}

bool isMacroCorePoint(const rock_fracture::Vector3& p, const SceneSpec& spec,
    const rock_fracture::Box& solid) {
    const MacroFaceDistances d = macroFaceDistances(p, solid);
    if (d.negX < 0.0 || d.posX < 0.0 || d.negY < 0.0 || d.posY < 0.0 || d.bottom < 0.0 || d.top < 0.0) {
        return false;
    }

    const double coreInset = std::max(1.0, (double)spec.surfaceBand * 0.72);
    return d.minVertical() > coreInset && d.minHorizontal() > coreInset;
}

bool isFaceInReplicationBand(const rock_fracture::Vector3& p, const SceneSpec& spec,
    const rock_fracture::Box& solid, CliffFace face, float wallBand) {
    (void)wallBand;
    return isInVerticalFaceSlab(p, spec, solid, face);
}

bool shouldApplyBlockReplication(const rock_fracture::Vector3& p, const SceneSpec& spec, float wallBand) {
    (void)wallBand;
    return isInCliffReplicationZone(p, spec, macroSolidBox(spec));
}

} // namespace render_playground
