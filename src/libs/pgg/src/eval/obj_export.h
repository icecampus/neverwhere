#pragma once

// Wavefront OBJ export of a runtime geometry (PggTool --obj, viewer RPC
// export). Surface color @Cd (spec §4.3) goes out as the widely read
// `v x y z r g b` extension (Blender, MeshLab, Houdini). @Cd on points writes
// the welded mesh; on corners/faces/detail the mesh is unwelded (one vertex
// per corner) so face colors survive without bleeding into neighbours.
// Normals: stored point @N (or corner N from compute_normals flat) as `vn`.
// Instances are realized by the caller (realizeInstances); sdf values are not
// exportable — mesh them with mesh_from_sdf first.
//
// C4 (agent_tooling_plan): --obj-color=check repaints problem faces with flag
// colors, and writeObjSplitGroups splits the export into one file per
// faces-group.

#include <string>
#include <vector>

#include "geometry.h"

namespace pgg {

struct ObjExportOptions {
    // PggTool --obj-color=check: faces flagged by classifyMeshIssueFaces
    // (probe.h — the check inspector's categories) get flag colors instead of
    // their @Cd: degenerate -> red (1,0,0), nonmanifold-incident -> yellow
    // (1,1,0), boundary-incident -> blue (0,0.4,1); priority in that order.
    // The mesh is unwelded to one vertex per corner (the overrides live on
    // faces) and vertex colors are always written (the neutral gray base
    // replaces a missing @Cd).
    bool checkColors = false;
};

// Writes `geo` as Wavefront OBJ to `path`. false + `err` on IO failure.
bool writeObj(const std::string& path, const Geo& geo, std::string* err = nullptr,
              const ObjExportOptions& opts = {});

// PggTool --obj-split-groups: one OBJ per faces-group —
// `<dir>/<name>.<group>.obj` (sorted group names) — plus
// `<dir>/<name>._nogroup.obj` with the faces that belong to no group, when
// such faces exist. Empty groups get no file. A geometry without faces-groups
// (or a non-mesh) writes the single `<dir>/<name>.obj`, exactly what
// writeObj(path) would write. `written` receives the written paths in write
// order. false + `err` on the first IO failure (earlier files stay written).
bool writeObjSplitGroups(const std::string& dir, const std::string& name, const Geo& geo,
                         std::vector<std::string>& written, std::string* err = nullptr,
                         const ObjExportOptions& opts = {});

}  // namespace pgg
