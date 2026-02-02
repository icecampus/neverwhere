#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include "graphics/lib.h"
#include "EcsModel.h"
#include <spdlog/spdlog.h>
#include "GameView.h"

int main(int argc, char* argv[])
{
    spdlog::set_level(spdlog::level::trace);
    spdlog::info("Starting EcsPlayground application...");

    QGuiApplication app(argc, argv);
    
    // Force OpenGL for Sokol compatibility in this test
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    
    // Create the ECS Model
    spdlog::info("Initializing ECS Model...");
    // Graphics::init(); // Moved to GameView to ensure context is active
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
