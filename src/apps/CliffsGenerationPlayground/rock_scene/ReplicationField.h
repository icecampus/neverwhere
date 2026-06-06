#pragma once

#include "SceneSpec.h"
#include "rock_fracture/Blocks.h"

namespace render_playground {

class SDFReplicationField : public rock_fracture::SDFNode {
public:
    SDFReplicationField(rock_fracture::SDFNode* tileRoot, const SceneSpec& spec, double tileSize);

    double Signed(const rock_fracture::Vector3& p) const override;

private:
    rock_fracture::SDFNode* m_tileRoot;
    SceneSpec m_spec;
    double m_tileSize;
    double m_halfTile;
};

double tileModuloAxis(double v, double tileSize);

rock_fracture::Vector3 worldToTileLocalForFace(const rock_fracture::Vector3& p,
    const rock_fracture::Box& solid, CliffFace face, double tileSize);

} // namespace render_playground
