#include "global_context.h"
#include "layesrs/land_layer.h"
#include "layesrs/asset_layer.h"

GlobalContext::GlobalContext(QObject* parent /*= nullptr*/):
    QObject(parent),
    layerLibrary(this)
{

}

void GlobalContext::load()
{
    layerLibrary.addElement<LandLayer>(&layerLibrary);
    layerLibrary.addElement<AssetLayer>("Decals", &layerLibrary);
    layerLibrary.addElement<AssetLayer>("Environment", &layerLibrary);
    layerLibrary.addElement<AssetLayer>("Buildings", &layerLibrary);
}

LayersLibrary* GlobalContext::getLayersLibraty() 
{
    return &layerLibrary;
}

