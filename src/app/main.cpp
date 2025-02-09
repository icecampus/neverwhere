#include <iostream>
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QDir>


int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    
    QQmlApplicationEngine engine;
    



    engine.addImportPath("D:/campus/neverwhere/_intermediate_64/Debug/qml");
    qDebug() << engine.importPathList();


    const QUrl url(u"qrc:/App/main.qml"_qs);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
        &app, [url](QObject* obj, const QUrl& objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        }, Qt::QueuedConnection);

    
    engine.load(url);
    
    return app.exec();
}