#pragma once

// Wavefront OBJ export of a runtime geometry (PggTool --obj, viewer RPC
// export). Surface color @Cd (spec §4.3) goes out as the widely read
// `v x y z r g b` extension (Blender, MeshLab, Houdini). @Cd on points writes
// the welded mesh; on corners/faces/detail the mesh is unwelded (one vertex
// per corner) so face colors survive without bleeding into neighbours.
// Normals: stored point @N (or corner N from compute_normals flat) as `vn`.
// Instances are realized by the caller (realizeInstances); sdf values are not
// exportable — mesh them with mesh_from_sdf first.

#include <string>

#include "geometry.h"

namespace pgg {

// Writes `geo` as Wavefront OBJ to `path`. false + `err` on IO failure.
bool writeObj(const std::string& path, const Geo& geo, std::string* err = nullptr);

}  // namespace pgg
