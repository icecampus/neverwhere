#include "core_context.h"
#include <QQmlContext>
#include "assets_library/asset_pack.h"
#include <format>

CoreContext::CoreContext(QQmlApplicationEngine& engine_):
    QObject(&engine_),
    engine(engine_),
    chaptersModel(this)
{

}

void CoreContext::load()
{
    fs::path rootPath = "d:/campus/neverwhere/resources/assets";
    assetManager.reset(new AssetManager(rootPath));
    assetsLibrary.reset(new AssetsLibrary(&engine));

    imageProvider = new AssetImageProvider(assetsLibrary.get());
    engine.addImageProvider("assetImages", imageProvider);

    chapterProvider = new ChaptersImageProvider();
    engine.addImageProvider("chaptersImage", chapterProvider);

    chaptersModel.addElement<Chapter>("Chapter 2", this);
    chaptersModel.addElement<Chapter>("Chapter 3", this);
    chaptersModel.addElement<Chapter>("Chapter 4", this);
    chaptersModel.addElement<Chapter>("Chapter 5", this);
    chaptersModel.addElement<Chapter>("Chapter 6", this);
    chaptersModel.addElement<Chapter>("Chapter 7", this);
    chaptersModel.addElement<Chapter>("Chapter 8", this);
    chaptersModel.addElement<Chapter>("Chapter 9", this);

}

AssetsLibrary* CoreContext::getAssetsLibraty() 
{
    return assetsLibrary.get();
}

ChaptersModel* CoreContext::getChaptersModel()
{
    return &chaptersModel;
}

