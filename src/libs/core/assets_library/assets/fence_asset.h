#pragma once
#include "assets_library/assets/slice_asset.h"

// Fence3D asset (fence brush): no atlas — the payload is the "fence3d" block
// in index.json (metersToPoints lift); the bundle carries the four baked
// piece meshes by convention (fence_post/fence_corner/fence_section2/
// fence_section3 .obj+.mtl in the bundle root). Lives on the FenceLandscape
// layer; its objects are Fence pieces (post/section), edited by FencePencil
// and the fence RPC ops.
//
// v1 has no settings panel: the payload is read-only through fenceData()
// and reaches the renderer via the canonical AssetData on the next sync.
class FenceAsset: public SliceAsset
{
    Q_OBJECT

public:
    explicit FenceAsset(QObject* parent);

    void load(const BaseData::AssetData& data) override;

    const BaseData::Fence3dAssetData* fenceData() const
    {
        return data.fence3dData ? &*data.fence3dData : nullptr;
    }
};
