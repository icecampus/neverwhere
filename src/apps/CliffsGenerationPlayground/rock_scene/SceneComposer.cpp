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

    const rock_fracture::Box solid = macroSolidBox(m_spec);

    if (isMacroCorePoint(p, m_spec, solid)) {
        return f;
    }

    if (!isInCliffReplicationZone(p, m_spec, solid)) {
        return f;
    }

    const double t = m_replication->Signed(p) - m_gapFill;

    // Paper §5: fe = max(f, t) — block voids carve recessions, blocks add volume (incl. outward).
    return std::max(f, t);
}

} // namespace render_playground
