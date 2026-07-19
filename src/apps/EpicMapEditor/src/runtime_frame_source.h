#pragma once

#include "frame_source.h"

#include <memory>

#include <QElapsedTimer>
#include <QString>

namespace game_runtime { class Runtime; }
namespace game_data { struct AssetIndex; }

// Frame source for the play-test tab: hosts an isolated game_runtime::Runtime
// + session (created from a Fixture) and feeds its world into the shared
// WorldRenderer — the same flow the standalone EpicGameClient uses.
//
// Threading: start()/restart() are called from QML on the GUI thread;
// tick()/buildWorldFrame() run from MapRenderItem::synchronize(), which Qt
// calls with the GUI thread blocked — so they can never overlap.
class RuntimeFrameSource : public MapFrameSource
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(float sessionTime READ sessionTime NOTIFY stateChanged)
    Q_PROPERTY(int worldDay READ worldDay NOTIFY stateChanged)
    Q_PROPERTY(int worldHour READ worldHour NOTIFY stateChanged)
    Q_PROPERTY(int worldMinute READ worldMinute NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)

public:
    explicit RuntimeFrameSource(QObject* parent = nullptr);
    ~RuntimeFrameSource() override;

    // Start a fresh session on the given map (absolute path to map.json).
    Q_INVOKABLE bool start(const QString& mapPath);
    // Drop the current session and start a new one from the same fixture.
    Q_INVOKABLE void restart();

    // MapRenderSource interface
    void tick() override;
    void buildWorldFrame(render_core::WorldFrame& outFrame) override;
    void ensureFrameAssets(const render_core::WorldFrame& frame, render_core::WorldRenderer& renderer) override;

    bool running() const;
    float sessionTime() const;
    int worldDay() const;
    int worldHour() const;
    int worldMinute() const;
    QString lastError() const { return m_lastError; }

signals:
    void stateChanged();

private:
    void createSession();
    bool fail(const QString& message);

    std::unique_ptr<game_runtime::Runtime> m_runtime;
    std::unique_ptr<game_data::AssetIndex> m_assetIndex;
    QString m_mapPath;
    QString m_lastError;
    QElapsedTimer m_tickTimer;
    bool m_tickTimerStarted = false;
};
