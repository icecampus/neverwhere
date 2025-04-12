#include "core_context.h"
#include <format>
#include <QQmlContext>
#include "assets_library/assets_pack_model.h"
#include "assets_library/assets_loader.h"
#include "assets_library/tools/pencil.h"
#include "assets_library/tools/eraser.h"

namespace fs = std::filesystem;

CoreContext::CoreContext(QQmlApplicationEngine& engine_):
    QObject(&engine_),
    engine(engine_),
    chaptersModel(this)
{

}

AssetsLibraryModel* CoreContext::getAssetsLibraty()
{
    return assetsLibrary.get();
}

ChaptersModel* CoreContext::getChaptersModel()
{
    return &chaptersModel;
}

void CoreContext::load()
{
    
    assetsLibrary.reset(new AssetsLibraryModel(&engine));
    loadAssets();

    imageProvider = new AssetImageProvider(assetsLibrary.get());
    imageProvider->loadAllImages();
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

void CoreContext::loadAssets()
{
    fs::path rootPath = "d:/campus/neverwhere/resources/assets";

    AssetsLoader::load(rootPath, *assetsLibrary.get());
}


