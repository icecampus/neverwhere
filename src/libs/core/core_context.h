#pragma once
#include <QQmlApplicationEngine>

#include "models/chapters_model.h"
#include "models/chapters_image_provider.h"
#include "assets_library/assets_library.h"
#include "assets_library/asset_image_provider.h"
#include "assets_library/tools/tools_model.h"


class CoreContext: public QObject
{
    Q_OBJECT
    Q_PROPERTY(AssetsLibrary* assetsLibrary READ getAssetsLibraty CONSTANT);
    Q_PROPERTY(ChaptersModel* chapters READ getChaptersModel CONSTANT);
    Q_PROPERTY(ToolsModel* tools READ getTools CONSTANT);

public:
    explicit CoreContext(QQmlApplicationEngine& engine);

    Q_INVOKABLE void load();

    AssetsLibrary* getAssetsLibraty();
    ChaptersModel* getChaptersModel();
    ToolsModel* getTools();

private:
    void loadAssets();

    ChaptersModel chaptersModel;

    QQmlApplicationEngine& engine;
    std::unique_ptr<AssetsLibrary> assetsLibrary;
    AssetImageProvider* imageProvider;
    ChaptersImageProvider* chapterProvider;

    std::unique_ptr<ToolsModel> toolsModel;


};
