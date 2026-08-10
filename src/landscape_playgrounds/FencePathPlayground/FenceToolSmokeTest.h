#pragma once

// CPU smoke test of the fence tool model (FenceModel): stroke segmentation,
// merge/split, translate, erase. Logs TEST PASS / TEST FAIL per check.
bool runFenceToolSmokeTest();

// CPU smoke test of the baked 3D fence pieces (FenceMesh): OBJ/MTL loading
// from resources/models/fence, piece AABBs, corner classification and the
// instancing/projection contract. Logs TEST PASS / TEST FAIL per check.
bool runFenceMeshSmokeTest();
