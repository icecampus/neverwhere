#include "pch.h"
#include "runtime_frame_source.h"

// Qt's `slots` macro breaks Sokol internals which use a field with that name.
#ifdef slots
#undef slots
#endif

#include <render_core/world_frame_builder.h>

#include <filesystem>

#include <game_runtime/lib.h>
#include <game_data/assets.h>
#include <game_data/map.h>

#include <spdlog/spdlog.h>

namespace {

bool looksLikeDataRoot(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    return fs::exists(dir / "resources" / "assets", ec) && fs::exists(dir / "resources" / "chapters", ec);
}

std::filesystem::path findDataRootUpwards(std::filesystem::path startDir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    startDir = fs::weakly_canonical(startDir, ec);
    if (startDir.empty()) return {};

    fs::path dir = startDir;
    for (int i = 0; i < 16; i++) {
        if (looksLikeDataRoot(dir)) return dir;
        if (!dir.has_parent_path()) break;
        const fs::path parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return {};
}

} // namespace

RuntimeFrameSource::RuntimeFrameSource(QObject* parent)
    : MapFrameSource(parent)
{
}

RuntimeFrameSource::~RuntimeFrameSource() {
    m_runtime.reset();
    m_assetIndex.reset();
}

bool RuntimeFrameSource::fail(const QString& message) {
    m_lastError = message;
    spdlog::error("RuntimeFrameSource: {}", message.toStdString());
    m_runtime.reset();
    emit stateChanged();
    return false;
}

bool RuntimeFrameSource::start(const QString& mapPath) {
    namespace fs = std::filesystem;

    m_mapPath = mapPath;
    m_lastError.clear();

    // Never let data errors escape into QML as C++ exceptions (that aborts
    // the editor via std::terminate — e.g. a fresh chapter without map.json).
    try {
        const fs::path mapFsPath = fs::path(mapPath.toStdString());
        if (!fs::exists(mapFsPath)) {
            return fail(QStringLiteral("Map file not found: %1 (save the map first)").arg(mapPath));
        }

        const fs::path dataRoot = findDataRootUpwards(mapFsPath.parent_path());
        if (dataRoot.empty()) {
            return fail(QStringLiteral("Data root not found for map %1").arg(mapPath));
        }
        const fs::path assetsRoot = dataRoot / "resources" / "assets";

        m_assetIndex = std::make_unique<game_data::AssetIndex>(game_data::AssetIndex::load(assetsRoot));
        spdlog::info("RuntimeFrameSource: loaded {} assets", m_assetIndex->byUuid.size());

        // Player mode, same as the standalone client.
        game_runtime::RuntimeConfig config;
        config.windowTitle = "Playtest";
        config.defaultMap = mapFsPath;
        config.assetsRoot = assetsRoot;
        config.dataRoot = dataRoot;
        config.enableEditorExtensions = false;

        m_runtime = std::make_unique<game_runtime::Runtime>(config);
        if (!m_runtime->initialize()) {
            return fail(QStringLiteral("Failed to initialize Runtime"));
        }

        createSession();
        if (!running()) {
            return fail(QStringLiteral("Failed to create game session on %1").arg(mapPath));
        }
    } catch (const std::exception& e) {
        return fail(QStringLiteral("Start failed: %1").arg(e.what()));
    }

    m_tickTimerStarted = false; // restart dt accumulation from zero
    emit stateChanged();
    return true;
}

void RuntimeFrameSource::restart() {
    // A failed start leaves no session — repeated Play must retry a full start.
    if (!m_runtime || !m_runtime->currentSession()) {
        if (!m_mapPath.isEmpty()) start(m_mapPath);
        return;
    }

    try {
        m_runtime->destroySession(m_runtime->currentSession());
        createSession();
        if (!running()) {
            fail(QStringLiteral("Failed to recreate game session"));
            return;
        }
    } catch (const std::exception& e) {
        fail(QStringLiteral("Restart failed: %1").arg(e.what()));
        return;
    }

    m_tickTimerStarted = false;
    emit stateChanged();
}

void RuntimeFrameSource::createSession() {
    // Mock game profile for now — the newGame preset, same as the client.
    // Mock management UI comes later (see ROADMAP pillar 1).
    auto fixture = game_runtime::Fixture::create()
        .withName("Playtest")
        .withMap(m_mapPath.toStdString())
        .newGame()
        .build();

    auto* session = m_runtime->createSession(fixture);
    if (!session) {
        spdlog::error("RuntimeFrameSource: failed to create game session");
    } else {
        spdlog::info("RuntimeFrameSource: session started on {}", m_mapPath.toStdString());
    }
}

void RuntimeFrameSource::tick() {
    if (!m_runtime || !m_runtime->currentSession()) return;

    if (!m_tickTimerStarted) {
        m_tickTimer.start();
        m_tickTimerStarted = true;
    }
    const float dt = (float)m_tickTimer.nsecsElapsed() / 1e9f;
    m_tickTimer.restart();

    m_runtime->update(dt);
    emit stateChanged();
}

void RuntimeFrameSource::buildWorldFrame(render_core::WorldFrame& outFrame) {
    outFrame.landscapeTiles.clear();
    outFrame.sprites.clear();

    if (!m_runtime || !m_runtime->currentSession()) return;
    const game_data::Map* map = m_runtime->currentSession()->world().map();
    if (!map) return;

    // Shared with the standalone client (render_core/world_frame_builder).
    render_core::collectWorldFrame(*map, outFrame);
}

void RuntimeFrameSource::ensureFrameAssets(const render_core::WorldFrame& frame, render_core::WorldRenderer& renderer) {
    if (!m_assetIndex) return;
    render_core::ensureWorldAssets(*m_assetIndex, frame, renderer);
}

bool RuntimeFrameSource::running() const {
    return m_runtime && m_runtime->currentSession();
}

float RuntimeFrameSource::sessionTime() const {
    return running() ? m_runtime->currentSession()->sessionTime() : 0.0f;
}

int RuntimeFrameSource::worldDay() const {
    return running() ? m_runtime->currentSession()->world().getDay() : 0;
}

int RuntimeFrameSource::worldHour() const {
    return running() ? m_runtime->currentSession()->world().getHour() : 0;
}

int RuntimeFrameSource::worldMinute() const {
    return running() ? m_runtime->currentSession()->world().getMinute() : 0;
}
