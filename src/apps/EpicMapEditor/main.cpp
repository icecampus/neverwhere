#include <iostream>
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <memory>
#include "core/lib.h"
#include "ui/lib.h"
#include "src/editor_scene_registry.h"
#include "src/editor_rpc_server.h"
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSurfaceFormat>
#include <QDateTime>
#include <QFile>
#include <QJsonObject>
#include <QTextStream>
#include <QtGlobal>
#include <boost/uuid/string_generator.hpp>
#include <filesystem>

// Qt's `slots` macro breaks Sokol internals which use a field with that name
// (render_core headers pull sokol).
#ifdef slots
#undef slots
#endif

#include <render_core/world_renderer.h>

#include "src/map_render_item.h"
#include "src/model_frame_source.h"
#include "src/runtime_frame_source.h"

static QFile* g_logFile = nullptr;

static void qtMessageToFile(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    QString typeStr;
    switch (type)
    {
    case QtDebugMsg: typeStr = "DEBUG"; break;
    case QtInfoMsg: typeStr = "INFO"; break;
    case QtWarningMsg: typeStr = "WARN"; break;
    case QtCriticalMsg: typeStr = "CRIT"; break;
    case QtFatalMsg: typeStr = "FATAL"; break;
    default: typeStr = "LOG"; break;
    }

    const QString line = QString("[%1] %2 %3 (%4:%5)\n")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
        .arg(typeStr)
        .arg(msg)
        .arg(QString(ctx.file ? ctx.file : "?"))
        .arg(ctx.line);

    // Best-effort file logging (won't crash if file can't open).
    if (g_logFile && g_logFile->isOpen())
    {
        QTextStream ts(g_logFile);
        ts << line;
        ts.flush();
    }

    // Also print to stderr (useful when launched from terminal).
    std::cerr << line.toStdString();

    if (type == QtFatalMsg)
    {
        abort();
    }
}

void registreTypes()
{
#ifndef __EMSCRIPTEN__
    qmlRegisterType<EpicEditorWindow>("UI", 1, 0, "EpicEditorWindow");
#endif

    qmlRegisterType<TabsModel>("UI", 1, 0, "TabsModel");
    qmlRegisterUncreatableMetaObject(
        TabType::staticMetaObject, // static meta object
        "UI", // import statement (can be any string)
        1,
        0, // major and minor version of the import
        "TabType", // name in QML (does not have to match C++ name)
        "Error: only enums" // error in case someone tries to create a MyNamespace object
    );


    qmlRegisterUncreatableType<math::vec2>("Game", 1, 0, "vec2", "Use Vec2Factory to create");

    qRegisterMetaType<VisibleRegion>("VisibleRegion");

    qmlRegisterType<LayerModel>("Game", 1, 0, "LayerModel");
    qmlRegisterType<MapModel>("Game", 1, 0, "MapModel");
    
    qmlRegisterType<Tile2D>("Game", 1, 0, "Tile2D");
    qmlRegisterType<Landscape>("Game", 1, 0, "Landscape");
    qmlRegisterType<Resource>("Game", 1, 0, "Resource");
    qmlRegisterType<Building>("Game", 1, 0, "Building");

    qmlRegisterType<NoiseGenerator>("Game", 1, 0, "NoiseGenerator");

    qmlRegisterType<AssetsContext>("Game", 1, 0, "AssetsContext");
    qmlRegisterType<AssetToolsSelector>("Game", 1, 0, "AssetToolsSelector");
    qmlRegisterType<ImageAsset>("Game", 1, 0, "ImageAsset");
    qmlRegisterType<SliceAsset>("Game", 1, 0, "SliceAsset");
    qmlRegisterType<Shape3dAsset>("Game", 1, 0, "Shape3dAsset");


    qmlRegisterType<DiamondIsometryView>("Game", 1, 0, "DiamondIsometryView");
    qmlRegisterType<diamond_dimensions>("Game", 1, 0, "diamond_dimensions");

    // FBO-based map view on the shared sokol WorldRenderer (replaces the old
    // QML tile delegates, QSG DiamondGrid/DiamondCursor and RuntimeMapView).
    qmlRegisterType<MapRenderItem>("Game", 1, 0, "MapRenderItem");

    // Frame sources for MapRenderItem: editor models / game runtime.
    qmlRegisterType<ModelFrameSource>("Game", 1, 0, "ModelFrameSource");
    qmlRegisterType<RuntimeFrameSource>("Game", 1, 0, "RuntimeFrameSource");

    qmlRegisterType<PropertyContainersModel>("Game", 1, 0, "PropertyContainersModel");


    qmlRegisterUncreatableMetaObject(
        AssetTypes::staticMetaObject,       // static meta object
        "Game",                             // import statement (can be any string)
        1,
        0,                                  // major and minor version of the import
        "AssetTypes",                       // name in QML (does not have to match C++ name)
        "Error: only enums"                 // error in case someone tries to create a MyNamespace object
    );


    qmlRegisterUncreatableMetaObject(
        GameObjectTypes::staticMetaObject,  // static meta object
        "Game",                             // import statement (can be any string)
        1,
        0,                                  // major and minor version of the import
        "GameObjectTypes",                  // name in QML (does not have to match C++ name)
        "Error: only enums"                 // error in case someone tries to create a MyNamespace object
    );

    qmlRegisterUncreatableMetaObject(
        LayerTypes::staticMetaObject,       // static meta object
        "Game",                             // import statement (can be any string)
        1,                              
        0,                                  // major and minor version of the import
        "LayerTypes",                       // name in QML (does not have to match C++ name)
        "Error: only enums"                 // error in case someone tries to create a MyNamespace object
    );

    qmlRegisterUncreatableMetaObject(
        PropertiesContainerTypes::staticMetaObject, // static meta object
        "Game",                             // import statement (can be any string)
        1,
        0,                                  // major and minor version of the import
        "PropertiesContainerTypes",         // name in QML (does not have to match C++ name)
        "Error: only enums"                 // error in case someone tries to create a MyNamespace object
    );
}

void registerGlobalObject(QQmlApplicationEngine& engine)
{
    MathFactory* mathFactory = new MathFactory(&engine);
    engine.rootContext()->setContextProperty("math", mathFactory);

    CoreContext* coreContext = new CoreContext(engine);
    engine.rootContext()->setContextProperty("core", coreContext);

    // Editor automation: registry bridges QML scene objects to C++,
    // RPC server exposes them over TCP on 127.0.0.1:9877.
    auto sceneRegistry = new EditorSceneRegistry(&engine);
    engine.rootContext()->setContextProperty("sceneRegistry", sceneRegistry);

    auto rpcServer = new EditorRpcServer(coreContext, sceneRegistry, &engine);
    engine.rootContext()->setContextProperty("rpcServer", rpcServer);
    if (!rpcServer->start())
    {
        qWarning() << "Editor RPC server failed to start on 127.0.0.1:9877";
    }
}


static bool looksLikeDataRoot(const std::filesystem::path& dir)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    return fs::exists(dir / "resources" / "assets", ec) && fs::exists(dir / "resources" / "chapters", ec);
}

static std::filesystem::path findDataRootUpwards(std::filesystem::path startDir)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    startDir = fs::weakly_canonical(startDir, ec);
    if (startDir.empty()) startDir = fs::current_path(ec);
    if (startDir.empty()) return {};

    fs::path dir = startDir;
    for (int i = 0; i < 16; i++)
    {
        if (looksLikeDataRoot(dir)) return dir;
        if (!dir.has_parent_path()) break;
        const fs::path parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return {};
}

// Data-only smoke scenario (AGENTS.md): key map operations without a window.
static int runSmokeTest()
{
    int failures = 0;
    const auto check = [&failures](bool ok, const char* name)
    {
        if (ok)
        {
            qInfo().noquote() << "TEST PASS:" << name;
        }
        else
        {
            qCritical().noquote() << "TEST FAIL:" << name;
            failures++;
        }
    };

    const std::filesystem::path dataRoot = findDataRootUpwards(std::filesystem::current_path());
    check(!dataRoot.empty(), "data root resolved");
    if (dataRoot.empty()) return 1;

    const std::filesystem::path mapPath = dataRoot / "resources/chapters/Base/maps/map.json";

    MapModel mapModel;
    mapModel.load(QString::fromStdString(mapPath.string()));

    ModelFrameSource source;
    source.setMapModel(&mapModel);

    render_core::WorldFrame frame;
    source.buildWorldFrame(frame);
    check(!frame.landscapeTiles.empty(), "map has landscape tiles");
    check(!frame.sprites.empty(), "map has Tile2D sprites");

    // Round-trip through the Qt isometry the tools use (same math as the renderer).
    DiamondIsometryView iso;
    bool roundTripOk = true;
    for (const math::ivec2& cell : {math::ivec2(0, 0), math::ivec2(1, 1), math::ivec2(3, 12), math::ivec2(-2, 5), math::ivec2(10, -7)})
    {
        const math::ivec2 back = iso.fieldToMap(iso.mapToField(cell));
        if (back.x != cell.x || back.y != cell.y)
        {
            qCritical().noquote() << "Round-trip mismatch at cell" << cell.x << cell.y;
            roundTripOk = false;
        }
    }
    check(roundTripOk, "diamond isometry fieldToMap(mapToField(cell)) == cell");

    // Authoring cycle (the editor RPC authoring ops are thin wrappers over
    // MapAuthoring): write tiles/landscape in cell coordinates on the loaded
    // map, then round-trip through save/load into a temp file.
    ImageAsset tileAsset(nullptr);
    {
        BaseData::AssetData data;
        data.uuid = boost::uuids::string_generator()("11111111-2222-3333-4444-555555555555");
        data.name = "smoke_tile";
        data.layerType = LayerTypes::Decoration;
        data.imageData = BaseData::ImageAssetData{};
        tileAsset.load(data);
    }
    SliceAsset sliceAsset(nullptr);
    {
        BaseData::AssetData data;
        data.uuid = boost::uuids::string_generator()("00000000-1111-2222-3333-444444444444");
        data.name = "smoke_landscape";
        data.layerType = LayerTypes::BaseLandscape;
        data.sliceData = BaseData::SliceAssetData{};
        sliceAsset.load(data);
    }

    check(MapAuthoring::setTile(*mapModel.layer(LayerTypes::Decoration), math::ivec2(1, 2), &tileAsset),
        "authoring setTile");
    check(MapAuthoring::fillRect(*mapModel.layer(LayerTypes::Decoration), math::ivec2(3, 3), math::ivec2(5, 4), &tileAsset) == 6,
        "authoring fillRect");
    // 4 raised nodes → dirty-cell union spans (7..9)x(7..9) = 9 cells.
    const int landCells = MapAuthoring::applyLandscapeUpdates(*mapModel.layer(LayerTypes::BaseLandscape), &sliceAsset,
        {{math::ivec2(8, 8), 1}, {math::ivec2(9, 8), 1}, {math::ivec2(8, 9), 1}, {math::ivec2(9, 9), 1}});
    check(landCells == 9, "authoring applyLandscapeUpdates");

    const QJsonObject dump = MapAuthoring::dumpMap(mapModel);
    check(dump.contains("layers"), "authoring dumpMap");

    const std::filesystem::path tmpMap = std::filesystem::temp_directory_path() / "epic_editor_smoke_map.json";
    mapModel.save(QString::fromStdString(tmpMap.string()));
    MapModel reloaded;
    reloaded.load(QString::fromStdString(tmpMap.string()));
    check(MapAuthoring::dumpMap(reloaded) == dump, "authoring save/load round-trip");
    std::filesystem::remove(tmpMap);

    qInfo().noquote() << (failures == 0 ? "TEST PASS: smoke scenario finished OK" : "TEST FAIL: smoke scenario failed");
    return failures == 0 ? 0 : 1;
}

int main(int argc, char* argv[])
{
    // Capture QML/Qt warnings into a local log file to debug early exits.
    QFile logFile("EpicMapEditor.log");
    if (logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        g_logFile = &logFile;
    }
    qInstallMessageHandler(qtMessageToFile);

    bool smoke = false;
    for (int i = 1; i < argc; i++)
    {
        if (std::string_view(argv[i]) == "--smoke") smoke = true;
    }

    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

#ifdef __APPLE__
    // Qt Quick's default surface format on macOS requests a legacy OpenGL 2.1
    // context, but the map is rendered by sokol GLCORE, which needs a core
    // profile (4.1 is the maximum Apple provides). Without this, sokol's init
    // queries (GL_MAJOR_VERSION & co, unknown to 2.1) poison glGetError and
    // the _sg_gl_init_limits assert fires on the first chapter tab open.
    QSurfaceFormat glFormat;
    glFormat.setVersion(4, 1);
    glFormat.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(glFormat);
#endif

    QApplication app(argc, argv);

    if (smoke)
    {
        return runSmokeTest();
    }

    QQuickStyle::setStyle("Material");

    // The map is rendered through sokol inside a Qt OpenGL context
    // (QQuickFramebufferObject), so Qt Quick must run on OpenGL.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    // Sokol is shared by all map views — shut it down globally, after items.
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &app, []() { shutdownEditorSokol(); });

    registreTypes();

    QQmlApplicationEngine engine;
    registerGlobalObject(engine);

    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &engine,
        [](const QList<QQmlError>& warnings)
        {
            for (const auto& w : warnings)
            {
                qWarning().noquote() << w.toString();
            }
        });

    engine.addImportPath(engine.importPathList()[0] + "/qml");
    //qDebug() << engine.importPathList();

#ifdef __EMSCRIPTEN__
    const QUrl url(u"qrc:/EpicMapEditor/qml/AppWindowCommon.qml"_qs);
#else
    const QUrl url(u"qrc:/EpicMapEditor/qml/AppWindowWindows.qml"_qs);
#endif
    
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
        &app, [url](QObject* obj, const QUrl& objUrl) {
            if (!obj && url == objUrl)
            {
                qCritical().noquote() << "Failed to create root object for:" << objUrl.toString();
                QCoreApplication::exit(-1);
            }
        }, Qt::QueuedConnection);

    
    engine.load(url);
    
    return app.exec();
}
