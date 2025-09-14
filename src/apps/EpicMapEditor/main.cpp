#include <iostream>
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <memory>
#include "core/lib.h"
#include "ui/lib.h"
#include <QQuickStyle>

void registreTypes()
{
#ifdef _WINDOWS
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


    qmlRegisterType<StaggeredIsometryView>("Game", 1, 0, "StaggeredIsometryView");
    qmlRegisterType<staggered_dimensions>("Game", 1, 0, "staggered_dimensions");

    qmlRegisterType<StaggeredGrid>("Game", 1, 0, "StaggeredGrid");
    qmlRegisterType<StaggeredCursor>("Game", 1, 0, "StaggeredCursor");

    
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

    qmlRegisterType<CustomItem>("Game", 1, 0, "CustomItem");
}

void registerGlobalObject(QQmlApplicationEngine& engine)
{
    MathFactory* mathFactory = new MathFactory(&engine);
    engine.rootContext()->setContextProperty("math", mathFactory);

    CoreContext* coreContext = new CoreContext(engine);
    engine.rootContext()->setContextProperty("core", coreContext);
}


int main(int argc, char* argv[])
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    


    QApplication app(argc, argv);
    QQuickStyle::setStyle("Material");
    
    registreTypes();

    QQmlApplicationEngine engine;
    registerGlobalObject(engine);

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
                QCoreApplication::exit(-1);
        }, Qt::QueuedConnection);

    
    engine.load(url);
    
    return app.exec();
}