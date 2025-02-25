#include <iostream>
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "core/lib.h"


void registreTypes()
{
    qRegisterMetaType<math::ivec2>("math::ivec2");

    qmlRegisterType<MapModel>("Game", 1, 0, "MapModel");
    qmlRegisterType<LandTile>("Game", 1, 0, "LandTile");
    qmlRegisterType<Resource>("Game", 1, 0, "Resource");
    qmlRegisterType<Building>("Game", 1, 0, "Building");

    qmlRegisterType<StaggeredIsometryView>("Game", 1, 0, "StaggeredIsometryView");
}

void registerGlobalObject(QQmlApplicationEngine& engine)
{
    static StaggeredIsometryView isometry(StaggeredDimensions{ 128, 2.0f });

    static MapLoader* loader = new MapLoader();
    loader->loadMap();

    engine.rootContext()->setContextProperty("mapModel", loader->model());
    engine.rootContext()->setContextProperty("isometry", &isometry);
}


int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    
    registreTypes();

    QQmlApplicationEngine engine;
    registerGlobalObject(engine);

    engine.addImportPath(engine.importPathList()[0] + "/qml");
    qDebug() << engine.importPathList();

    const QUrl url(u"qrc:/Editor/qml/main.qml"_qs);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
        &app, [url](QObject* obj, const QUrl& objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        }, Qt::QueuedConnection);

    
    engine.load(url);
    
    return app.exec();
}