#pragma once

#include "SceneSpec.h"
#include "rock_fracture/Blocks.h"

namespace render_playground {

struct MacroFaceDistances {
    double negX = 0.0;
    double posX = 0.0;
    double negY = 0.0;
    double posY = 0.0;
    double bottom = 0.0;
    double top = 0.0;

    double minVertical() const;
    double minHorizontal() const;
};

// Solid macro rock (axis-aligned cube) placed in the scene MC domain.
rock_fracture::Box macroSolidBox(const SceneSpec& spec);
rock_fracture::Box sceneBoundingBox(const SceneSpec& spec);

MacroFaceDistances macroFaceDistances(const rock_fracture::Vector3& p, const rock_fracture::Box& solid);

class SDFMacroBox : public rock_fracture::SDFNode {
public:
    explicit SDFMacroBox(const SceneSpec& spec);

    double Signed(const rock_fracture::Vector3& p) const override;

    const rock_fracture::Box& solidBox() const { return m_solid; }

private:
    SceneSpec m_spec;
    rock_fracture::Box m_solid;
};

double cliffWallProximity(const rock_fracture::Vector3& p, const SceneSpec& spec);

// Signed depth from a vertical face plane: 0 on the wall, positive inward into rock.
double verticalFaceDepth(const rock_fracture::Vector3& p, const rock_fracture::Box& solid, CliffFace face);

CliffFace nearestVerticalFace(const rock_fracture::Vector3& p, const rock_fracture::Box& solid);

// Inside the cliff wall slab (inward band + optional outward protrusion shell), excluding top/bottom.
bool isInVerticalFaceSlab(const rock_fracture::Vector3& p, const SceneSpec& spec,
    const rock_fracture::Box& solid, CliffFace face);

bool isInCliffReplicationZone(const rock_fracture::Vector3& p, const SceneSpec& spec,
    const rock_fracture::Box& solid);

// Deep macro interior — keep monolithic rock, do not apply tile fractures.
bool isMacroCorePoint(const rock_fracture::Vector3& p, const SceneSpec& spec,
    const rock_fracture::Box& solid);

bool shouldApplyBlockReplication(const rock_fracture::Vector3& p, const SceneSpec& spec, float wallBand);

bool isFaceInReplicationBand(const rock_fracture::Vector3& p, const SceneSpec& spec,
    const rock_fracture::Box& solid, CliffFace face, float wallBand);

} // namespace render_playground
