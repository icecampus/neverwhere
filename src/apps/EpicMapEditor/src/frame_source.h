#pragma once

#include <QObject>

namespace render_core { class WorldRenderer; struct WorldFrame; }

// Interface between MapRenderItem and a world-data provider. One item, many
// sources: editor workspace (Qt models), play-test tab (game runtime), ...
class MapFrameSource : public QObject
{
    Q_OBJECT
public:
    explicit MapFrameSource(QObject* parent = nullptr) : QObject(parent) {}

    // Advance the source's own state (game logic). Called from synchronize()
    // with the GUI thread blocked by Qt, so GUI-thread sources are safe —
    // no concurrent access can happen at that moment.
    virtual void tick() {}

    // Snapshot world data into plain render lists. Called from synchronize().
    virtual void buildWorldFrame(render_core::WorldFrame& outFrame) = 0;

    // Upload/refresh GPU assets for the frame. Called from render() with an
    // active GL context on the render thread.
    virtual void ensureFrameAssets(const render_core::WorldFrame& frame, render_core::WorldRenderer& renderer) = 0;
};
