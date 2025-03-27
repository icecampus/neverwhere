#include "global_context.h"
#include "layesrs/land_layer.h"
#include "layesrs/asset_layer.h"
#include <format>

GlobalContext::GlobalContext(QObject* parent /*= nullptr*/):
    QObject(parent),
    layerLibrary(this),
    chaptersModel(this)
{

}

void GlobalContext::load()
{
    layerLibrary.addElement<LandLayer>(&layerLibrary);
    layerLibrary.addElement<AssetLayer>("Decals", &layerLibrary);
    layerLibrary.addElement<AssetLayer>("Environment", &layerLibrary);
    layerLibrary.addElement<AssetLayer>("Buildings", &layerLibrary);

    chaptersModel.addElement<Chapter>("Base", this);
    for (int i=1; i< 100; ++i )
    {
        chaptersModel.addElement<Chapter>( std::format("Chapter {}", i).c_str(), this);
    }
    
    
    chaptersModel.addElement<Chapter>("Chapter 2", this);
    chaptersModel.addElement<Chapter>("Chapter 3", this);
    chaptersModel.addElement<Chapter>("Chapter 4", this);
    chaptersModel.addElement<Chapter>("Chapter 5", this);
    chaptersModel.addElement<Chapter>("Chapter 6", this);
    chaptersModel.addElement<Chapter>("Chapter 7", this);
    chaptersModel.addElement<Chapter>("Chapter 8", this);
    chaptersModel.addElement<Chapter>("Chapter 9", this);
}

LayersLibrary* GlobalContext::getLayersLibraty() 
{
    return &layerLibrary;
}

ChaptersModel* GlobalContext::getChaptersModel()
{
    return &chaptersModel;
}

