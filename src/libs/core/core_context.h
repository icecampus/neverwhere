#pragma once
#include <QQmlApplicationEngine>

#include "models/chapters_model.h"
#include "models/chapters_image_provider.h"
#include "assets_library/assets_library_model.h"
#include "assets_library/assets_image_provider.h"
#include "assets_library/tools/tools_model.h"


class CoreContext: public QObject
{
    Q_OBJECT
    Q_PROPERTY(AssetsLibraryModel* assetsLibrary READ getAssetsLibraty CONSTANT);
    Q_PROPERTY(ChaptersModel* chapters READ getChaptersModel CONSTANT);

public:
    explicit CoreContext(QQmlApplicationEngine& engine);
    virtual ~CoreContext();

    Q_INVOKABLE void load();

    AssetsLibraryModel* getAssetsLibraty();
    ChaptersModel* getChaptersModel();
    AssetToolsSelector* getTools();

private:
    void loadAssets();

    

    QQmlApplicationEngine& engine;
    
    std::unique_ptr<ChaptersModel> chaptersModel;
    std::unique_ptr<AssetsLibraryModel> assetsLibrary;

    AssetImageProvider* imageProvider;
    ChaptersImageProvider* chapterProvider;



};
