#pragma once

#include "frame_source.h"

// Fully-defined types are required for the pointer Q_PROPERTYs below.
#include "core/map/map_model.h"
#include "core/assets_library/assets_library_model.h"

// Frame source for the editor workspace: builds the world frame from the
// editor's Qt map models. Live view of the models — edits show up next frame.
class ModelFrameSource : public MapFrameSource
{
    Q_OBJECT
    Q_PROPERTY(MapModel* mapModel READ mapModel WRITE setMapModel NOTIFY mapModelChanged)
    Q_PROPERTY(AssetsLibraryModel* assetsLibrary READ assetsLibrary WRITE setAssetsLibrary NOTIFY assetsLibraryChanged)

public:
    explicit ModelFrameSource(QObject* parent = nullptr) : MapFrameSource(parent) {}

    void buildWorldFrame(render_core::WorldFrame& outFrame) override;
    void ensureFrameAssets(const render_core::WorldFrame& frame, render_core::WorldRenderer& renderer) override;

    MapModel* mapModel() const { return m_mapModel; }
    void setMapModel(MapModel* model);

    AssetsLibraryModel* assetsLibrary() const { return m_assetsLibrary; }
    void setAssetsLibrary(AssetsLibraryModel* library);

signals:
    void mapModelChanged();
    void assetsLibraryChanged();

private:
    MapModel* m_mapModel = nullptr;
    AssetsLibraryModel* m_assetsLibrary = nullptr;
};
