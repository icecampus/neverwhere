#include "SceneComposer.h"

#include <algorithm>
#include <cmath>

namespace render_playground {

SDFSceneComposed::SDFSceneComposed(rock_fracture::SDFNode* terrain,
    rock_fracture::SDFNode* replication,
    const rock_fracture::Box& sceneBox,
    const SceneSpec& spec,
    bool enableReplication)
    : rock_fracture::SDFNode(sceneBox)
    , m_terrain(terrain)
    , m_replication(replication)
    , m_enableReplication(enableReplication)
    , m_spec(spec)
    , m_surfaceBand(spec.surfaceBand)
    , m_gapFill(spec.gapFill) {}

double SDFSceneComposed::Signed(const rock_fracture::Vector3& p) const {
    const double f = m_terrain->Signed(p);
    if (!m_enableReplication || m_replication == nullptr) {
        return f;
    }

    // Only modify rock volume, never carve air pockets into solid from fracture gaps.
    if (f > 0.0) {
        return f;
    }

    // Heightfield |f| is not distance to the vertical cliff face — use wall proximity instead.
    if (!shouldApplyBlockReplication(p, m_spec, m_surfaceBand)) {
        return f;
    }

    // Expand block field slightly to close hairline fracture gaps (t > 0 in tile voids).
    const double t = m_replication->Signed(p) - m_gapFill;

    const rock_fracture::Box solid = macroSolidBox(m_spec);
    const MacroFaceDistances faceD = macroFaceDistances(p, solid);
    const double minV = faceD.minVertical();

    // Paper-style rocky skin: max(f,t) carves block gaps and protrusions at the cliff face.
    // Deeper in the band use min(f,t) so tile air does not punch holes through the macro volume.
    const double shellDepth = std::min(1.5, (double)m_surfaceBand * 0.35);
    if (minV <= shellDepth) {
        return std::max(f, t);
    }
    return std::min(f, t);
}

} // namespace render_playground
