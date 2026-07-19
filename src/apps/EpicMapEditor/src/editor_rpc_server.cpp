#include "pch.h"
#include "editor_rpc_server.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpSocket>
#include <magic_enum/magic_enum.hpp>

#include "editor_scene_registry.h"
#include "core_context.h"
#include "assets_library/asset.h"
#include "assets_library/assets_library_model.h"
#include "assets_library/assets_pack_model.h"
#include "assets_library/tools/tools_model.h"
#include "map/map_model.h"
#include "models/chapters_model.h"
#include "topology/diamond_isometry.h"

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
    if (op == "play")            return _cmdPlay(args);
    if (op == "list_assets")     return _cmdListAssets(args);
    if (op == "select_asset")    return _cmdSelectAsset(args);
    if (op == "select_tool")     return _cmdSelectTool(args);
    if (op == "click")           return _cmdClick(args);
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
    int index = -1;
    if (cur->getLayerType() == LayerTypes::BaseLandscape)
    {
        if (tool == "landscape_pencil" || tool == "0") index = 0;
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
