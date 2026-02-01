#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "EcsModel.h"

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    
    // Create the ECS Model
    EcsModel model;

    QQmlApplicationEngine engine;
    
    // Expose model to QML
    engine.rootContext()->setContextProperty("ecsModel", &model);
    engine.addImportPath(engine.importPathList()[0] + "/qml");

    // Load QML
    const QUrl url(u"qrc:/EcsPlayground/qml/main.qml"_qs);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
        &app, [url](QObject* obj, const QUrl& objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        }, Qt::QueuedConnection);

    engine.load(url);

    return app.exec();
}
