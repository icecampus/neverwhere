#pragma once

#include "SceneSpec.h"
#include "ReplicationField.h"
#include "TerrainField.h"

namespace render_playground {

class SDFSceneComposed : public rock_fracture::SDFNode {
public:
    SDFSceneComposed(rock_fracture::SDFNode* terrain,
        rock_fracture::SDFNode* replication,
        const rock_fracture::Box& sceneBox,
        const SceneSpec& spec,
        bool enableReplication);

    double Signed(const rock_fracture::Vector3& p) const override;

private:
    rock_fracture::SDFNode* m_terrain;
    rock_fracture::SDFNode* m_replication;
    bool m_enableReplication;
    SceneSpec m_spec;
    float m_surfaceBand;
    float m_gapFill;
};

} // namespace render_playground
