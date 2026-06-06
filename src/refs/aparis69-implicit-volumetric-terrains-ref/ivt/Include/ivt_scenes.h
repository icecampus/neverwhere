#pragma once

// Viewer entry points: return terrain trees for custom MC resolution (IvtScene).
// Batch export uses upstream XxxScene() in *-scene.cpp (forward-declared in BatchExport.cpp).
class TTree;

TTree* BuildSeaTerrainTree();
TTree* BuildIslandTerrainTree();
TTree* BuildKarstTerrainTree();
