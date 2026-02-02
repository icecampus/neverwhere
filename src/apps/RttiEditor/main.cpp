#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <spdlog/spdlog.h>
#include <QTimer>
#include "GameModel.h"

int main(int argc, char* argv[])
{
    spdlog::set_level(spdlog::level::trace);
    spdlog::info("Starting RttiEditor application...");

    QGuiApplication app(argc, argv);
    
    GameModel model;

    QQmlApplicationEngine engine;
    
    // Expose model to QML
    engine.rootContext()->setContextProperty("gameModel", &model);
    engine.addImportPath(engine.importPathList()[0] + "/qml");
    
    const QUrl url(u"qrc:/RttiEditor/qml/main.qml"_qs);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
        &app, [url](QObject* obj, const QUrl& objUrl) {
            if (!obj && url == objUrl) {
                spdlog::critical("Failed to load main.qml");
                QCoreApplication::exit(-1);
            }
        }, Qt::QueuedConnection);

    engine.load(url);
    
    // Run self-test after a short delay
    // QTimer::singleShot(1000, &model, [&model](){
    //    model.runTestScenario();
    // });

    return app.exec();
}
