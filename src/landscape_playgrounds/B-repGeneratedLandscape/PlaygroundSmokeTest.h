#pragma once

// CPU smoke test (--smoke): the B-rep pipeline checks (nodes -> solid mask ->
// composeSolidMaskMesh -> baked vertex stream) plus the node-field contract
// and the material-set presence. No window/GPU needed.
bool runBrepSmokeTest();
