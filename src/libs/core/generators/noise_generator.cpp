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

    constexpr int width = 90;
    constexpr int height = 160;

    LandNodes result;
    result.init(width, height);

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

//
void NoiseGenerator::generatre(MapModel* mapModel)
{

}
