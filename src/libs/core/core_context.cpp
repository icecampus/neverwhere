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
    qInfo().noquote() << "CoreContext::load baseDataPath =" << QString::fromStdString(baseDataPath.string());
    try
    {
        chaptersModel.reset(new ChaptersModel(this));

        assetsLibrary.reset(new AssetsLibraryModel(this));
        loadAssets();

        imageProvider = new AssetImageProvider(assetsLibrary.get());
        imageProvider->loadAllImages();
        engine.addImageProvider("assetImages", imageProvider);

        chapterProvider = new ChaptersImageProvider();
        engine.addImageProvider("chaptersImage", chapterProvider);

        ChaptersModel::load(*chaptersModel, baseDataPath / "chapters");
        chapterProvider->load(*chaptersModel);

        qInfo().noquote() << "CoreContext::load done"
                          << "packs=" << assetsLibrary->size()
                          << "chapters=" << chaptersModel->size();
    }
    catch (const std::exception& e)
    {
        qCritical().noquote() << "CoreContext::load exception:" << e.what();
    }
    catch (...)
    {
        qCritical().noquote() << "CoreContext::load unknown exception";
    }
}

void CoreContext::loadAssets()
{
    fs::path rootPath = baseDataPath / "assets";
    qInfo().noquote() << "CoreContext::loadAssets rootPath =" << QString::fromStdString(rootPath.string());
    AssetsLoader::load(rootPath, *assetsLibrary.get());
}


