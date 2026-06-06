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

bool insideSolidBox(const rock_fracture::Vector3& p, const rock_fracture::Box& box, double margin = 0.0) {
    return p.x >= box[0][0] - margin && p.x <= box[1][0] + margin
        && p.y >= box[0][1] - margin && p.y <= box[1][1] + margin
        && p.z >= box[0][2] - margin && p.z <= box[1][2] + margin;
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
    return rock_fracture::Box(
        rock_fracture::Vector3(0.0),
        rock_fracture::Vector3(spec.sceneSizeX, spec.sceneSizeY, spec.sceneSizeZ));
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
    if (!insideSolidBox(p, solid, spec.surfaceBand)) {
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

bool isFaceInReplicationBand(const rock_fracture::Vector3& p, const SceneSpec& spec,
    const rock_fracture::Box& solid, CliffFace face, float wallBand) {
    if (!insideSolidBox(p, solid, wallBand)) {
        return false;
    }

    const MacroFaceDistances d = macroFaceDistances(p, solid);
    const double faceDist = faceDistance(d, face);
    if (faceDist > wallBand) {
        return false;
    }

    if (spec.replicationMode == CliffReplicationMode::SingleFace) {
        return face == spec.cliffFace;
    }

    const double minV = d.minVertical();
    const double minH = d.minHorizontal();
    if (minV > wallBand) {
        return false;
    }
    if (minH < minV) {
        return false;
    }
    return faceDist <= minV + 1e-6;
}

bool shouldApplyBlockReplication(const rock_fracture::Vector3& p, const SceneSpec& spec, float wallBand) {
    const rock_fracture::Box solid = macroSolidBox(spec);

    if (spec.replicationMode == CliffReplicationMode::SingleFace) {
        return cliffWallProximity(p, spec) <= wallBand;
    }

    if (!insideSolidBox(p, solid, wallBand)) {
        return false;
    }

    const MacroFaceDistances d = macroFaceDistances(p, solid);
    const double minV = d.minVertical();
    const double minH = d.minHorizontal();
    if (minV > wallBand) {
        return false;
    }
    return minV < minH;
}

} // namespace render_playground
