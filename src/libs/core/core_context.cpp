#include "core_context.h"
#include <format>
#include <QQmlContext>
#include "assets_library/assets_pack_model.h"
#include "assets_library/assets_loader.h"

namespace fs = std::filesystem;

std::filesystem::path baseDataPath = "d:/campus/neverwhere/resources";

CoreContext::CoreContext(QQmlApplicationEngine& engine_):
    QObject(&engine_),
    engine(engine_)
{

}

CoreContext::~CoreContext()
{

}

AssetsLibraryModel* CoreContext::getAssetsLibraty()
{
    return assetsLibrary.get();
}

ChaptersModel* CoreContext::getChaptersModel()
{
    return chaptersModel.get();
}

void CoreContext::load()
{
    chaptersModel.reset( new ChaptersModel(this));


    assetsLibrary.reset(new AssetsLibraryModel(this));
    loadAssets();

    imageProvider = new AssetImageProvider(assetsLibrary.get());
    imageProvider->loadAllImages();
    engine.addImageProvider("assetImages", imageProvider);


    chapterProvider = new ChaptersImageProvider();
    engine.addImageProvider("chaptersImage", chapterProvider);

    ChaptersModel::load(*chaptersModel, baseDataPath / "chapters");

    chapterProvider->load(*chaptersModel);
}

void CoreContext::loadAssets()
{
    fs::path rootPath = baseDataPath / "assets";

    AssetsLoader::load(rootPath, *assetsLibrary.get());
}


