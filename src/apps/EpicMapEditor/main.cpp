#include <iostream>
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "core/lib.h"

void registreTypes()
{
    
    qmlRegisterType<Vec2Factory>("Game", 1, 0, "Vec2Factory");
    qmlRegisterUncreatableType<math::vec2>("Game", 1, 0, "math::vec2", "Use Vec2Factory to create");

    qmlRegisterType<IVec2Factory>("Game", 1, 0, "IVec2Factory");
    qmlRegisterUncreatableType<math::ivec2>("Game", 1, 0, "math::ivec2", "Use IVec2Factory to create");



    qRegisterMetaType<VisibleRegion>("VisibleRegion");

    qmlRegisterType<MapModel>("Game", 1, 0, "MapModel");
    qmlRegisterType<LandTile>("Game", 1, 0, "LandTile");
    qmlRegisterType<Resource>("Game", 1, 0, "Resource");
    qmlRegisterType<Building>("Game", 1, 0, "Building");

    qmlRegisterType<StaggeredIsometryView>("Game", 1, 0, "StaggeredIsometryView");
    qmlRegisterType<staggered_dimensions>("Game", 1, 0, "staggered_dimensions");
    
}

void registerGlobalObject(QQmlApplicationEngine& engine)
{
    static StaggeredIsometryView isometry(staggered_dimensions{ 128, 2.0f });

    static MapLoader* loader = new MapLoader();
    loader->loadMap();

    engine.rootContext()->setContextProperty("mapModel", loader->model());
    engine.rootContext()->setContextProperty("isoView", &isometry);


    Vec2Factory* vec2Factory = new Vec2Factory();
    engine.rootContext()->setContextProperty("vec2Factory", vec2Factory);

    IVec2Factory* ivec2Factory = new IVec2Factory();
    engine.rootContext()->setContextProperty("ivec2Factory", ivec2Factory);

}


int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    
    registreTypes();

    QQmlApplicationEngine engine;
    registerGlobalObject(engine);

    engine.addImportPath(engine.importPathList()[0] + "/qml");
    qDebug() << engine.importPathList();

    const QUrl url(u"qrc:/EpicMapEditor/qml/main.qml"_qs);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
        &app, [url](QObject* obj, const QUrl& objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        }, Qt::QueuedConnection);

    
    engine.load(url);
    
    return app.exec();
}