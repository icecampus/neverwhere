#include "noise_generator.h"
#include <noise/noise.h>

using namespace noise;

constexpr double baseNoiseScale = 40;

namespace
{
    double noisefunc(module::Perlin& perlin, double nx, double ny)
    {
        return perlin.GetValue(nx, ny, 0);
    }

    // Generated field rect (nodes); cells spanned are [0, w-1) x [0, h-1).
    constexpr int kGenWidth = 90;
    constexpr int kGenHeight = 160;
}


LandNodes PerlinGen::generate(float frequency, float secondOctave, float thirdOctave, float waterLevel)
{
    module::Perlin firstOctavePerlin;
    firstOctavePerlin.SetSeed(13234);

    module::Perlin secondOctavePerlin;
    secondOctavePerlin.SetSeed(43324);

    module::Perlin thirdOctavePerlin;
    thirdOctavePerlin.SetSeed(9945);

    spdlog::info(__FUNCTION__ );

    constexpr int width = kGenWidth;
    constexpr int height = kGenHeight;

    LandNodes result;

    double value[width][height];

    double k = baseNoiseScale * frequency;

    for (int x = 0; x < width; x++)
    {
        for (int y = 0; y < height; y++)
        {
            double nx = (double)x / (double)width;
            double ny = (double)y / (double)height;

            float e = noisefunc(firstOctavePerlin, k * nx, k * ny);
            e += secondOctave * noisefunc(secondOctavePerlin, 2.0f * k * nx, 2.0f * k * ny);
            e += thirdOctave * noisefunc(thirdOctavePerlin, 4.0f * k * nx, 4.0f * k * ny);
            value[x][y] = e;
        }
    }

    for (int x = 0; x < width; x++)
    {
        for (int y = 0; y < height; y++)
        {
            math::ivec2 curPoint(x, y);

            result[curPoint] = (value[x][y]) > waterLevel ? 1 : 0;
        }
    }

    return result;
}

//NoiseGenerator
Asset* NoiseGenerator::getCurrentAsset()
{
    return currentAsset;
}

void NoiseGenerator::setCurrentAsset(Asset* asset)
{
    if (asset != currentAsset)
    {
        currentAsset = asset;

        emit currentAssetChanged();
    }
}


void NoiseGenerator::generate(MapModel* mapModel)
{
    if (currentAsset && currentAsset->type == AssetTypes::slice)
    {
        SliceAsset* sliceAsset = dynamic_cast<SliceAsset*>(currentAsset);
        generate(mapModel, sliceAsset);
    }
    else
    {
        spdlog::error("Selected asset empty or nit 'slice' type");
    }
    
}

void NoiseGenerator::generate(MapModel* mapModel, SliceAsset* sliceAsset)
{
    LandNodes generatedNodes = PerlinGen::generate(1.0f, 1.0f, 1.0f, 0.0f);

    LayerModel* landLayer = mapModel->layer(LayerTypes::BaseLandscape);

    for (int x=0; x<kGenWidth-1; ++x)
    {
        for (int y=0; y<kGenHeight-1; ++y)
        {
            math::ivec2 cellPos(x, y);

            TileSet::TileType tileType = DiamondTiledLandscape::tileTypeAt(generatedNodes, cellPos);
            MapAuthoring::updateLandscapeCell(landLayer, sliceAsset, cellPos, tileType);
        }
    }
}
