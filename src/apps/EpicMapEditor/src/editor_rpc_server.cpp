#include "pch.h"
#include "editor_rpc_server.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QEventLoop>
#include <QPixmap>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QScreen>
#include <QSharedPointer>
#include <QTcpSocket>
#include <QWindow>
#include <magic_enum/magic_enum.hpp>

#include "editor_scene_registry.h"
#include "core_context.h"
#include "assets_library/asset.h"
#include "assets_library/assets/slice_asset.h"
#include "assets_library/assets_context.h"
#include "assets_library/assets_library_model.h"
#include "assets_library/assets_pack_model.h"
#include "assets_library/tools/tools_model.h"
#include "map/map_authoring.h"
#include "map/map_model.h"
#include "models/chapters_model.h"
#include "topology/diamond_isometry.h"
#include "map_render_item.h"

EditorRpcServer::EditorRpcServer(CoreContext* core, EditorSceneRegistry* registry, QObject* parent)
    : QObject(parent)
    , m_core(core)
    , m_registry(registry)
{
}

bool EditorRpcServer::start(const QHostAddress& addr, quint16 port)
{
    m_server = new QTcpServer(this);
    if (!m_server->listen(addr, port))
    {
        qWarning() << "[rpc] failed to listen on" << addr.toString() << port
                   << ":" << m_server->errorString();
        return false;
    }
    connect(m_server, &QTcpServer::newConnection, this, &EditorRpcServer::_onNewConnection);
    qInfo() << "[rpc] editor RPC listening on" << addr.toString() << port;
    return true;
}

void EditorRpcServer::_onNewConnection()
{
    while (m_server->hasPendingConnections())
    {
        QTcpSocket* client = m_server->nextPendingConnection();
        connect(client, &QTcpSocket::readyRead, this, &EditorRpcServer::_onReadyRead);
        connect(client, &QTcpSocket::disconnected, this, &EditorRpcServer::_onClientDisconnected);
        m_buffers.insert(client, QByteArray());
    }
}

void EditorRpcServer::_onClientDisconnected()
{
    QTcpSocket* client = qobject_cast<QTcpSocket*>(sender());
    if (!client)
        return;
    m_buffers.remove(client);
    client->deleteLater();
}

void EditorRpcServer::_onReadyRead()
{
    QTcpSocket* client = qobject_cast<QTcpSocket*>(sender());
    if (!client)
        return;

    QByteArray& buf = m_buffers[client];
    buf += client->readAll();

    int idx = -1;
    while ((idx = buf.indexOf('\n')) != -1)
    {
        QByteArray line = buf.left(idx);
        buf = buf.mid(idx + 1);
        _processLine(client, line);
    }
}

void EditorRpcServer::_processLine(QTcpSocket* client, const QByteArray& line)
{
    QByteArray response = _dispatch(QJsonDocument::fromJson(line).object());
    response.append('\n');
    client->write(response);
}

QByteArray EditorRpcServer::_dispatch(const QJsonObject& req)
{
    QString op = req.value("op").toString();
    QJsonObject args = req.value("args").toObject();
    if (op == "ping")            return _cmdPing(args);
    if (op == "status")          return _cmdStatus(args);
    if (op == "list_chapters")   return _cmdListChapters(args);
    if (op == "load_chapter")    return _cmdLoadChapter(args);
    if (op == "create_chapter")  return _cmdCreateChapter(args);
    if (op == "play")            return _cmdPlay(args);
    if (op == "list_assets")     return _cmdListAssets(args);
    if (op == "select_asset")    return _cmdSelectAsset(args);
    if (op == "select_tool")     return _cmdSelectTool(args);
    if (op == "click")           return _cmdClick(args);
    if (op == "set_tile")        return _cmdSetTile(args);
    if (op == "erase_tile")      return _cmdEraseTile(args);
    if (op == "fill_rect")       return _cmdFillRect(args);
    if (op == "set_landscape")   return _cmdSetLandscape(args);
    if (op == "get_map")         return _cmdGetMap(args);
    if (op == "set_camera")      return _cmdSetCamera(args);
    if (op == "screenshot")      return _cmdScreenshot(args);
    if (op == "save")            return _cmdSave(args);
    if (op == "reload")          return _cmdReload(args);
    return _error("unknown_op", "unknown op: " + op);
}

QByteArray EditorRpcServer::_ok(const QJsonObject& data)
{
    QJsonObject r;
    r["ok"] = true;
    r["data"] = data;
    return QJsonDocument(r).toJson(QJsonDocument::Compact);
}

QByteArray EditorRpcServer::_error(const QString& kind, const QString& message)
{
    QJsonObject r;
    r["ok"] = false;
    QJsonObject err;
    err["kind"] = kind;
    err["message"] = message;
    r["error"] = err;
    return QJsonDocument(r).toJson(QJsonDocument::Compact);
}

// --- helpers -------------------------------------------------------------

static Chapter* findChapterByName(ChaptersModel* model, const QString& name)
{
    for (size_t i = 0; i < model->size(); ++i)
    {
        Chapter* ch = model->element(static_cast<int>(i));
        if (ch && ch->name() == name)
            return ch;
    }
    return nullptr;
}

// Resolves the "layer" arg (LayerTypes enum name: Decoration, BaseLandscape,
// GameplayInteractive) for the cell-coordinate authoring ops.
static std::optional<LayerTypes::Type> parseLayerType(const QJsonObject& args)
{
    const QString layer = args.value("layer").toString().trimmed();
    if (layer.isEmpty())
        return std::nullopt;
    return magic_enum::enum_cast<LayerTypes::Type>(layer.toStdString());
}

// --- commands ------------------------------------------------------------

QByteArray EditorRpcServer::_cmdPing(const QJsonObject&)
{
    QJsonObject d;
    d["pong"] = true;
    return _ok(d);
}

QByteArray EditorRpcServer::_cmdStatus(const QJsonObject&)
{
    QJsonObject d;
    d["rpc"] = QStringLiteral("alive");
    Chapter* ch = m_registry ? m_registry->activeChapter() : nullptr;
    d["chapter"] = ch ? QJsonValue(ch->name()) : QJsonValue();
    MapModel* map = m_registry ? m_registry->activeMapModel() : nullptr;
    d["map_loaded"] = map != nullptr;
    AssetToolsSelector* sel = m_registry ? m_registry->activeTools() : nullptr;
    Asset* cur = sel ? sel->getCurrentAsset() : nullptr;
    d["current_asset"] = cur ? QJsonValue(cur->name()) : QJsonValue();
    d["current_tool_index"] = m_currentToolIndex;
    return _ok(d);
}

QByteArray EditorRpcServer::_cmdListChapters(const QJsonObject&)
{
    ChaptersModel* model = m_core ? m_core->getChaptersModel() : nullptr;
    if (!model)
        return _error("no_chapters_model", "chapters model not available");
    QJsonArray arr;
    for (size_t i = 0; i < model->size(); ++i)
    {
        Chapter* ch = model->element(static_cast<int>(i));
        if (!ch)
            continue;
        QJsonObject c;
        c["name"] = ch->name();
        c["uuid"] = ch->getUuid().toString(QUuid::WithoutBraces);
        arr.append(c);
    }
    QJsonObject d;
    d["chapters"] = arr;
    return _ok(d);
}

QByteArray EditorRpcServer::_cmdLoadChapter(const QJsonObject& args)
{
    QString name = args.value("name").toString().trimmed();
    if (name.isEmpty())
        return _error("invalid_input", "name is required");

    ChaptersModel* model = m_core ? m_core->getChaptersModel() : nullptr;
    if (!model)
        return _error("no_chapters_model", "chapters model not available");

    Chapter* ch = findChapterByName(model, name);
    if (!ch)
        return _error("not_found", "chapter not found: " + name);

    // Ask MainWindow.qml to open the tab; the QML side will setActiveScene
    // when the Workspace is ready. Client polls status afterwards.
    emit loadChapterRequested(ch->name(), ch->getUuid().toString(QUuid::WithoutBraces));

    QJsonObject d;
    d["name"] = ch->name();
    d["uuid"] = ch->getUuid().toString(QUuid::WithoutBraces);
    d["hint"] = QStringLiteral("poll 'status' until map_loaded=true");
    return _ok(d);
}

QByteArray EditorRpcServer::_cmdPlay(const QJsonObject& args)
{
    QString name = args.value("name").toString().trimmed();
    if (name.isEmpty())
        return _error("invalid_input", "name is required");

    ChaptersModel* model = m_core ? m_core->getChaptersModel() : nullptr;
    if (!model)
        return _error("no_chapters_model", "chapters model not available");

    Chapter* ch = findChapterByName(model, name);
    if (!ch)
        return _error("not_found", "chapter not found: " + name);

    // Ask MainWindow.qml to open (or restart) the play-test tab for the chapter.
    emit playChapterRequested(ch->name(), ch->getUuid().toString(QUuid::WithoutBraces));

    QJsonObject d;
    d["name"] = ch->name();
    d["uuid"] = ch->getUuid().toString(QUuid::WithoutBraces);
    return _ok(d);
}

QByteArray EditorRpcServer::_cmdListAssets(const QJsonObject&)
{
    AssetsLibraryModel* lib = m_core ? m_core->getAssetsLibraty() : nullptr;
    if (!lib)
        return _error("no_assets_model", "assets library not available");

    QJsonArray arr;
    // AssetsLibraryModel : SimpleModel<AssetsPackModel>, AssetsPackModel : SimpleModel<Asset>
    for (size_t p = 0; p < lib->size(); ++p)
    {
        AssetsPackModel* pack = lib->element(static_cast<int>(p));
        if (!pack)
            continue;
        for (size_t a = 0; a < pack->size(); ++a)
        {
            Asset* asset = pack->element(static_cast<int>(a));
            if (!asset)
                continue;
            QJsonObject o;
            o["uuid"] = asset->uuid().toString(QUuid::WithoutBraces);
            o["name"] = asset->name();
            o["layerType"] = QString::fromStdString(std::string(magic_enum::enum_name(asset->getLayerType())));
            arr.append(o);
        }
    }
    QJsonObject d;
    d["assets"] = arr;
    return _ok(d);
}

QByteArray EditorRpcServer::_cmdSelectAsset(const QJsonObject& args)
{
    QString uuidStr = args.value("uuid").toString().trimmed();
    if (uuidStr.isEmpty())
        return _error("invalid_input", "uuid is required");
    QUuid uuid(uuidStr);
    if (uuid.isNull())
        return _error("invalid_input", "bad uuid: " + uuidStr);

    AssetsLibraryModel* lib = m_core ? m_core->getAssetsLibraty() : nullptr;
    AssetToolsSelector* sel = m_registry ? m_registry->activeTools() : nullptr;
    if (!lib || !sel)
        return _error("no_assets_model", "assets/tools not available");

    Asset* asset = lib->getAsset(uuid);
    if (!asset)
        return _error("not_found", "asset not found: " + uuidStr);

    sel->setCurrentAsset(asset);
    // Drive the same selection state a palette click sets, so the right
    // panel (properties, cliff settings) follows the RPC selection too.
    if (AssetsContext* ctx = m_registry->activeAssetsContext())
        ctx->setAsset(asset);
    QJsonObject d;
    d["name"] = asset->name();
    d["uuid"] = uuidStr;
    return _ok(d);
}

QByteArray EditorRpcServer::_cmdSelectTool(const QJsonObject& args)
{
    QString tool = args.value("tool").toString().trimmed().toLower();
    AssetToolsSelector* sel = m_registry ? m_registry->activeTools() : nullptr;
    if (!sel)
        return _error("no_tools", "tools selector not available");
    Asset* cur = sel->getCurrentAsset();
    if (!cur)
        return _error("no_current_asset", "select_asset first");

    // Map tool name to index. ToolsModel registration order per asset type:
    //   image: 0=Cursor, 1=Pencil, 2=Eraser
    //   slice: 0=LandscapePencil
    //   shape3d: 0=Shape3dPencil
    //   cliff3d: 0=CliffPencil
    //   cyclopean3d: 0=CyclopeanPencil
    //   stone3d: 0=StonePencil
    //   texture2d: 0=TexturePencil
    //   tech3d: 0=TechPencil
    int index = -1;
    if (cur->type == AssetTypes::slice)
    {
        if (tool == "landscape_pencil" || tool == "0") index = 0;
    }
    else if (cur->type == AssetTypes::shape3d)
    {
        if (tool == "shape3d_pencil" || tool == "0") index = 0;
    }
    else if (cur->type == AssetTypes::cliff3d)
    {
        if (tool == "cliff_pencil" || tool == "0") index = 0;
    }
    else if (cur->type == AssetTypes::cyclopean3d)
    {
        if (tool == "cyclopean_pencil" || tool == "0") index = 0;
    }
    else if (cur->type == AssetTypes::stone3d)
    {
        if (tool == "stone_pencil" || tool == "0") index = 0;
    }
    else if (cur->type == AssetTypes::texture2d)
    {
        if (tool == "texture_pencil" || tool == "0") index = 0;
    }
    else if (cur->type == AssetTypes::tech3d)
    {
        if (tool == "tech_pencil" || tool == "0") index = 0;
    }
    else
    {
        if (tool == "cursor" || tool == "0") index = 0;
        else if (tool == "pencil" || tool == "1") index = 1;
        else if (tool == "eraser" || tool == "2") index = 2;
    }
    if (index < 0)
        return _error("invalid_input", "unknown tool for current asset: " + tool);

    // AssetToolsSelector exposes its model via getToolsModel(); the index
    // there is per-asset-type (matches the registration order above).
    AssetToolsModel* toolsModel = sel->getToolsModel();
    if (!toolsModel)
        return _error("no_tools_model", "toolsModel is null (asset has no tools)");
    toolsModel->setCurrentTool(index);
    m_currentToolIndex = index;

    QJsonObject d;
    d["tool"] = tool;
    d["index"] = index;
    return _ok(d);
}

QByteArray EditorRpcServer::_cmdClick(const QJsonObject& args)
{
    int x = args.value("x").toInt(-1);
    int y = args.value("y").toInt(-1);
    if (x < 0 || y < 0)
        return _error("invalid_input", "x and y are required (>= 0)");
    bool ctrl = args.value("ctrl").toBool(false);
    bool shift = args.value("shift").toBool(false);
    bool alt = args.value("alt").toBool(false);

    if (!m_registry)
        return _error("no_scene", "no active scene; load_chapter first");

    MapModel* mapModel = m_registry->activeMapModel();
    DiamondIsometryView* isoView = m_registry->activeIsometryView();
    if (!mapModel || !isoView)
        return _error("no_scene", "active scene missing mapModel/isoView");

    AssetToolsSelector* sel = m_registry ? m_registry->activeTools() : nullptr;
    if (!sel)
        return _error("no_tools", "tools selector not available");

    sel->click(QPoint(x, y), mapModel, isoView, ctrl, shift, alt);

    QJsonObject d;
    d["x"] = x;
    d["y"] = y;
    return _ok(d);
}

QByteArray EditorRpcServer::_cmdCreateChapter(const QJsonObject& args)
{
    QString name = args.value("name").toString().trimmed();
    if (name.isEmpty())
        return _error("invalid_input", "name is required");

    ChaptersModel* model = m_core ? m_core->getChaptersModel() : nullptr;
    if (!model)
        return _error("no_chapters_model", "chapters model not available");

    if (findChapterByName(model, name))
        return _error("duplicate", "chapter already exists: " + name);

    Chapter* ch = model->createChapter(name);
    if (!ch)
        return _error("create_failed", "could not create chapter on disk: " + name);

    // Reuse the load flow: MainWindow.qml opens a Workspace tab for it.
    emit loadChapterRequested(ch->name(), ch->getUuid().toString(QUuid::WithoutBraces));

    QJsonObject d;
    d["name"] = ch->name();
    d["uuid"] = ch->getUuid().toString(QUuid::WithoutBraces);
    d["hint"] = QStringLiteral("poll 'status' until map_loaded=true");
    return _ok(d);
}

QByteArray EditorRpcServer::_cmdSetTile(const QJsonObject& args)
{
    const std::optional<LayerTypes::Type> layerType = parseLayerType(args);
    if (!layerType)
        return _error("invalid_input", "layer is required (Decoration|BaseLandscape|GameplayInteractive)");

    const QString uuidStr = args.value("asset_uuid").toString().trimmed();
    const QUuid uuid(uuidStr);
    if (uuid.isNull())
        return _error("invalid_input", "valid asset_uuid is required");

    AssetsLibraryModel* lib = m_core ? m_core->getAssetsLibraty() : nullptr;
    MapModel* map = m_registry ? m_registry->activeMapModel() : nullptr;
    if (!lib || !map)
        return _error("no_scene", "no active scene; load_chapter first");

    Asset* asset = lib->getAsset(uuid);
    if (!asset)
        return _error("not_found", "asset not found: " + uuidStr);

    const math::ivec2 cell(args.value("x").toInt(), args.value("y").toInt());
    if (!MapAuthoring::setTile(*map->layer(*layerType), cell, asset))
        return _error("invalid_input", "set_tile supports image assets only; use set_landscape for slice assets");

    QJsonObject d;
    d["x"] = cell.x;
    d["y"] = cell.y;
    d["asset"] = asset->name();
    return _ok(d);
}

QByteArray EditorRpcServer::_cmdEraseTile(const QJsonObject& args)
{
    const std::optional<LayerTypes::Type> layerType = parseLayerType(args);
    if (!layerType)
        return _error("invalid_input", "layer is required (Decoration|BaseLandscape|GameplayInteractive)");

    MapModel* map = m_registry ? m_registry->activeMapModel() : nullptr;
    if (!map)
        return _error("no_scene", "no active scene; load_chapter first");

    const math::ivec2 cell(args.value("x").toInt(), args.value("y").toInt());
    const int removed = MapAuthoring::eraseTiles(*map->layer(*layerType), cell);

    QJsonObject d;
    d["x"] = cell.x;
    d["y"] = cell.y;
    d["removed"] = removed;
    return _ok(d);
}

QByteArray EditorRpcServer::_cmdFillRect(const QJsonObject& args)
{
    const std::optional<LayerTypes::Type> layerType = parseLayerType(args);
    if (!layerType)
        return _error("invalid_input", "layer is required (Decoration|BaseLandscape|GameplayInteractive)");

    const QString uuidStr = args.value("asset_uuid").toString().trimmed();
    const QUuid uuid(uuidStr);
    if (uuid.isNull())
        return _error("invalid_input", "valid asset_uuid is required");

    AssetsLibraryModel* lib = m_core ? m_core->getAssetsLibraty() : nullptr;
    MapModel* map = m_registry ? m_registry->activeMapModel() : nullptr;
    if (!lib || !map)
        return _error("no_scene", "no active scene; load_chapter first");

    Asset* asset = lib->getAsset(uuid);
    if (!asset)
        return _error("not_found", "asset not found: " + uuidStr);

    const math::ivec2 from(args.value("x0").toInt(), args.value("y0").toInt());
    const math::ivec2 to(args.value("x1").toInt(), args.value("y1").toInt());
    const int written = MapAuthoring::fillRect(*map->layer(*layerType), from, to, asset);

    QJsonObject d;
    d["written"] = written;
    d["asset"] = asset->name();
    return _ok(d);
}

QByteArray EditorRpcServer::_cmdSetLandscape(const QJsonObject& args)
{
    const QString uuidStr = args.value("asset_uuid").toString().trimmed();
    const QUuid uuid(uuidStr);
    if (uuid.isNull())
        return _error("invalid_input", "valid asset_uuid is required");

    const QJsonArray updates = args.value("updates").toArray();
    if (updates.isEmpty())
        return _error("invalid_input", "updates array is required: [[x, y, 0|1], ...]");

    AssetsLibraryModel* lib = m_core ? m_core->getAssetsLibraty() : nullptr;
    MapModel* map = m_registry ? m_registry->activeMapModel() : nullptr;
    if (!lib || !map)
        return _error("no_scene", "no active scene; load_chapter first");

    Asset* asset = lib->getAsset(uuid);
    if (!asset)
        return _error("not_found", "asset not found: " + uuidStr);
    SliceAsset* sliceAsset = dynamic_cast<SliceAsset*>(asset);
    if (!sliceAsset)
        return _error("invalid_input", "asset is not a slice (landscape) asset: " + uuidStr);

    std::vector<std::pair<math::ivec2, uint8_t>> parsed;
    parsed.reserve(static_cast<size_t>(updates.size()));
    for (const QJsonValue& value : updates)
    {
        const QJsonArray entry = value.toArray();
        if (entry.size() != 3)
            return _error("invalid_input", "each update must be [x, y, 0|1]");
        parsed.emplace_back(math::ivec2(entry[0].toInt(), entry[1].toInt()),
            static_cast<uint8_t>(entry[2].toInt() ? 1 : 0));
    }

    // Target the layer dictated by the asset (BaseLandscape for slice assets,
    // RaisedLandscape for shape3d assets) instead of hardcoding one layer.
    const int cells = MapAuthoring::applyLandscapeUpdates(
        *map->layer(asset->getLayerType()), sliceAsset, parsed);

    QJsonObject d;
    d["updates_applied"] = static_cast<qint64>(parsed.size());
    d["cells_recomputed"] = cells;
    return _ok(d);
}

QByteArray EditorRpcServer::_cmdGetMap(const QJsonObject& args)
{
    MapModel* map = m_registry ? m_registry->activeMapModel() : nullptr;
    if (!map)
        return _error("no_scene", "no active scene; load_chapter first");

    const QString layer = args.value("layer").toString().trimmed();
    if (!layer.isEmpty())
    {
        const std::optional<LayerTypes::Type> layerType =
            magic_enum::enum_cast<LayerTypes::Type>(layer.toStdString());
        if (!layerType)
            return _error("invalid_input", "unknown layer: " + layer);
        return _ok(MapAuthoring::dumpLayer(*map->layer(*layerType)));
    }
    return _ok(MapAuthoring::dumpMap(*map));
}

QByteArray EditorRpcServer::_cmdSetCamera(const QJsonObject& args)
{
    // The iso view is the camera source of truth (tools, screenToMap, and the
    // QML binding that forwards it into MapRenderItem all read it).
    DiamondIsometryView* isoView = m_registry ? m_registry->activeIsometryView() : nullptr;
    if (!isoView)
        return _error("no_scene", "no active scene; load_chapter first");

    if (args.contains("x"))
        isoView->setCameraX(static_cast<float>(args.value("x").toDouble()));
    if (args.contains("y"))
        isoView->setCameraY(static_cast<float>(args.value("y").toDouble()));
    if (args.contains("zoom"))
        isoView->setCameraZoom(static_cast<float>(args.value("zoom").toDouble()));

    QJsonObject d;
    d["x"] = isoView->getCameraX();
    d["y"] = isoView->getCameraY();
    d["zoom"] = isoView->getCameraZoom();
    return _ok(d);
}

QByteArray EditorRpcServer::_cmdScreenshot(const QJsonObject& args)
{
    const QString path = args.value("path").toString().trimmed();
    if (path.isEmpty())
        return _error("invalid_input", "path is required");

    MapRenderItem* item = m_registry ? m_registry->activeRenderItem() : nullptr;
    if (!item)
        return _error("no_scene", "no active scene; load_chapter first");

    // Capture what is actually displayed, not QQuickItem::grabToImage():
    // the grab re-renders the FBO item off the display path and noticeably
    // degrades the semi-transparent landscape tiles (sparse tufts instead
    // of the dense cover the window shows). grabWindow + crop to the item
    // rect gives the true on-screen pixels.
    //
    // source="fbo" skips the window grab and forces the item FBO grab: on
    // macOS without Screen Recording permission grabWindow either returns
    // null OR — worse — a valid-looking pixmap with the window's own content
    // privacy-excluded (you get the windows beneath it). There is no API to
    // tell the two apart, so the caller that knows the environment is
    // TCC-blocked asks for the FBO path explicitly.
    const bool forceFbo = args.value("source").toString() == QLatin1String("fbo");
    QWindow* window = item->window();
    if (!forceFbo && (!window || !window->screen()))
        return _error("render_failed", "render item has no window/screen");

    QImage image;
    const QPixmap pixmap = forceFbo ? QPixmap() : window->screen()->grabWindow(window->winId());
    if (!pixmap.isNull()) {
        const qreal dpr = pixmap.devicePixelRatio();
        const QPointF topLeft = item->mapToScene(QPointF(0, 0));
        const QRectF sceneRect(topLeft, item->size());
        const QRect crop(
            static_cast<int>(sceneRect.x() * dpr),
            static_cast<int>(sceneRect.y() * dpr),
            static_cast<int>(sceneRect.width() * dpr),
            static_cast<int>(sceneRect.height() * dpr));
        image = pixmap.copy(crop).toImage();
    } else {
        // Item FBO grab: same renderer/z-buffer, only the semi-transparent
        // tile density differs slightly from the display path. The grab
        // completes on the next scene-graph frame, and an occluded/minimized
        // window on macOS stops getting frames at all — so raise the window
        // and pump update requests while waiting.
        if (window) {
            if (window->windowStates() & Qt::WindowMinimized) window->showNormal();
            window->raise();
            window->requestActivate();
        }
        QSharedPointer<QQuickItemGrabResult> grab = item->grabToImage();
        if (!grab)
            return _error("render_failed", "grabWindow() failed and grabToImage() unavailable");
        QEventLoop loop;
        bool ready = false;
        QObject::connect(grab.data(), &QQuickItemGrabResult::ready, &loop,
            [&ready, &loop] { ready = true; loop.quit(); });
        QElapsedTimer deadline;
        deadline.start();
        while (!ready && deadline.elapsed() < 10000) {
            if (window) window->requestUpdate();
            loop.processEvents(QEventLoop::AllEvents, 100);
        }
        if (!ready)
            return _error("render_failed", "grabToImage() timed out");
        image = grab->image();
    }
    if (image.isNull())
        return _error("render_failed", "captured image is null");
    if (!image.save(path))
        return _error("io_failed", "could not save image to: " + path);

    QJsonObject d;
    d["path"] = path;
    d["width"] = image.width();
    d["height"] = image.height();
    return _ok(d);
}

QByteArray EditorRpcServer::_cmdSave(const QJsonObject&)
{
    Chapter* ch = m_registry ? m_registry->activeChapter() : nullptr;
    MapModel* map = m_registry ? m_registry->activeMapModel() : nullptr;
    if (!ch || !map)
        return _error("no_scene", "no active chapter/map to save");

    QString path = ch->getMapPath();
    map->save(path);

    QJsonObject d;
    d["path"] = path;
    return _ok(d);
}

QByteArray EditorRpcServer::_cmdReload(const QJsonObject&)
{
    Chapter* ch = m_registry ? m_registry->activeChapter() : nullptr;
    MapModel* map = m_registry ? m_registry->activeMapModel() : nullptr;
    if (!ch || !map)
        return _error("no_scene", "no active chapter/map to reload");

    QString path = ch->getMapPath();
    map->load(path);

    QJsonObject d;
    d["path"] = path;
    return _ok(d);
}
