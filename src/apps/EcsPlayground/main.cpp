#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include "EcsModel.h"
#include <spdlog/spdlog.h>

int main(int argc, char* argv[])
{
    spdlog::info("Starting EcsPlayground application...");

    QGuiApplication app(argc, argv);
    
    // Create the ECS Model
    spdlog::info("Initializing ECS Model...");
    EcsModel model;

    QQmlApplicationEngine engine;
    
    // Expose model to QML
    engine.rootContext()->setContextProperty("ecsModel", &model);
    engine.addImportPath(engine.importPathList()[0] + "/qml");
    spdlog::info("EcsModel exposed to QML context");

    // Load QML
    const QUrl url(u"qrc:/EcsPlayground/qml/main.qml"_qs);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
        &app, [url](QObject* obj, const QUrl& objUrl) {
            if (!obj && url == objUrl) {
                spdlog::critical("Failed to load main.qml");
                QCoreApplication::exit(-1);
            } else {
                spdlog::info("QML Interface loaded successfully");
            }
        }, Qt::QueuedConnection);

    engine.load(url);

    spdlog::info("Entering Application Event Loop");
    return app.exec();
}
