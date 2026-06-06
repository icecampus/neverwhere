#include "ttree.h"

/*!
\brief This scene is an example of one of the "Floating Islands" figure shown in the paper.
Every island was defined analytically by combining multiple noise function with our volumetric heightfield
primitive. For more details, please refer to the paper.
*/
TTree* BuildIslandTerrainTree()
{
	TNode* major = new TBlend(
		new TFloatingIsland(Vector3(5.0, 0.0, 0.0), 50.0, 15.0, 10),
		new TFloatingIsland(Vector3(-25.0, 30.0, 15.0), 35.0, 5.0, 5.0),
		new TFloatingIsland(Vector3(45.0, 0.0, 35.0), 35.0, 10.0, 5.0)
	);
	return new TTree(major);
}

void FloatingIsland()
{
	std::cout << "Floating Islands" << std::endl;
	TTree* terrainTree = BuildIslandTerrainTree();
	marching_cube("islands.obj", terrainTree, 100);
	delete terrainTree;
	std::cout << std::endl;
}
