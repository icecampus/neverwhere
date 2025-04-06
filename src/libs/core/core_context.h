#pragma once
#include <QQmlApplicationEngine>

#include "models/chapters_model.h"
#include "models/chapters_image_provider.h"
#include "assets_library/assets_library.h"
#include "assets_library/asset_image_provider.h"


class CoreContext: public QObject
{
    Q_OBJECT
    Q_PROPERTY(AssetsLibrary* assetsLibrary READ getAssetsLibraty CONSTANT);
    Q_PROPERTY(ChaptersModel* chapters READ getChaptersModel CONSTANT);

public:
    explicit CoreContext(QQmlApplicationEngine& engine);

    Q_INVOKABLE void load();

    AssetsLibrary* getAssetsLibraty();
    ChaptersModel* getChaptersModel();

private:
    void loadAssets();

    ChaptersModel chaptersModel;

    QQmlApplicationEngine& engine;
    std::unique_ptr<AssetsLibrary> assetsLibrary;
    AssetImageProvider* imageProvider;
    ChaptersImageProvider* chapterProvider;

};
