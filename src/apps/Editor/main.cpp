#include <iostream>
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "core/lib.h"


int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    

    qmlRegisterType<MapModel>("Game", 1, 0, "MapModel");
    qmlRegisterType<LandTile>("Game", 1, 0, "LandTile");
    qmlRegisterType<Resource>("Game", 1, 0, "Resource");
    qmlRegisterType<Building>("Game", 1, 0, "Building");

    MapLoader* loader = new MapLoader();
    loader->loadMap();

    QQmlApplicationEngine engine;

    engine.rootContext()->setContextProperty("mapModel", loader->model());

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