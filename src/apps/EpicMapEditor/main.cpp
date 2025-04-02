#include <iostream>
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <memory>
#include "core/lib.h"
#include "ui/lib.h"

class EditorCore
{
public:
    EditorCore(QQmlApplicationEngine& engine)
    {
        fs::path rootPath = "d:/campus/neverwhere/resources/assets";
        assetManager.reset(new AssetManager(rootPath));
        assetModel.reset(new AssetModel(&engine));
        assetManager->loadGroup("buildings", assetModel.get());

        imageProvider = new AssetImageProvider(assetModel.get());

        //register in engine
        engine.rootContext()->setContextProperty("assetModel", assetModel.get());
        engine.addImageProvider("assetImages", imageProvider);

        chapterProvider = new ChaptersImageProvider();
        engine.addImageProvider("chaptersImage", chapterProvider);
    }

private:
    std::unique_ptr<AssetManager> assetManager;
    std::unique_ptr<AssetModel> assetModel;
    AssetImageProvider* imageProvider;
    ChaptersImageProvider* chapterProvider;
};


void registreTypes()
{
    qmlRegisterType<EpicEditorWindow>("UI", 1, 0, "EpicEditorWindow");
    qmlRegisterType<TabsModel>("UI", 1, 0, "TabsModel");

    qmlRegisterUncreatableType<math::vec2>("Game", 1, 0, "vec2", "Use Vec2Factory to create");

    qRegisterMetaType<VisibleRegion>("VisibleRegion");

    qmlRegisterType<MapModel>("Game", 1, 0, "MapModel");
    qmlRegisterType<GameObject>("Game", 1, 0, "GameObject");
    qmlRegisterType<LandTile>("Game", 1, 0, "LandTile");
    qmlRegisterType<Resource>("Game", 1, 0, "Resource");
    qmlRegisterType<Building>("Game", 1, 0, "Building");

    qmlRegisterType<StaggeredIsometryView>("Game", 1, 0, "StaggeredIsometryView");
    qmlRegisterType<staggered_dimensions>("Game", 1, 0, "staggered_dimensions");

    qmlRegisterType<StaggeredGrid>("Game", 1, 0, "StaggeredGrid");
    qmlRegisterType<StaggeredCursor>("Game", 1, 0, "StaggeredCursor");
    
}

void registerGlobalObject(QQmlApplicationEngine& engine)
{
    static StaggeredIsometryView isometry(staggered_dimensions{ 128, 2.0f });

    static MapLoader* loader = new MapLoader();
    loader->loadMap();

    engine.rootContext()->setContextProperty("mapModel", loader->model());
    engine.rootContext()->setContextProperty("isoView", &isometry);


    MathFactory* mathFactory = new MathFactory(&engine);
    engine.rootContext()->setContextProperty("math", mathFactory);

    GlobalContext* globalContext = new GlobalContext(&engine);
    engine.rootContext()->setContextProperty("core", globalContext);

}


int main(int argc, char* argv[])
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);


    QApplication app(argc, argv);
    
    registreTypes();

    QQmlApplicationEngine engine;
    registerGlobalObject(engine);
    EditorCore core(engine);

    engine.addImportPath(engine.importPathList()[0] + "/qml");
    //qDebug() << engine.importPathList();

    const QUrl url(u"qrc:/EpicMapEditor/qml/main.qml"_qs);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
        &app, [url](QObject* obj, const QUrl& objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        }, Qt::QueuedConnection);

    
    engine.load(url);
    
    return app.exec();
}