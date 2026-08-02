#pragma once
#include <QObject>
#include <array>
#include "boost_uuids_serialization.h"
#include <filesystem>
#include "maps.h"
#include "math/lib.h"

namespace AssetTypes
{
    Q_NAMESPACE;
    enum Type
    {
        image,
        slice,
        shape3d,
        cliff3d,
        cyclopean3d,
        stone3d
    };
    Q_ENUM_NS(Type);
}

namespace BaseData
{
    struct ImageAssetData
    {
        float width{0};
        std::string imageFilename;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(ImageAssetData, imageFilename, width);
    };

    struct SliceAssetData
    {
     
        std::string thumbnail;
        std::string atlas;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(SliceAssetData, thumbnail, atlas);
    };

    // Shape3D: slice atlas + raised presentation params (ported from
    // SDFGeneratedLandscape). Objects are plain Landscape tiles on the
    // RaisedLandscape layer; these params drive the renderer only.
    struct Shape3dAssetData
    {
        std::string thumbnail;
        std::string atlas;
        std::string topTexture; // optional: tiled ground texture for the raised top
        float raisedHeight{32.0f};
        bool rockWalls{true};
        float rockAmplitude{0.28f};
        float rockBevel{0.3f};

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Shape3dAssetData,
            thumbnail, atlas, topTexture, raisedHeight, rockWalls, rockAmplitude, rockBevel);
    };

    // Cliff3D shading palette/light (mirror of the SDFGeneratedLandscape
    // ShadingParams; uniforms only — no geometry rebuild on change).
    struct Cliff3dShadingData
    {
        float lightAzimuth{2.23f};   // radians
        float lightElevation{0.85f}; // radians
        std::array<float, 3> darkColor{0.38f, 0.38f, 0.42f};
        std::array<float, 3> goldColor{0.75f, 0.62f, 0.5f};
        std::array<float, 3> grassA{0.4f, 0.62f, 0.35f};
        std::array<float, 3> grassB{0.6f, 0.65f, 0.4f};
        float veinThreshold{0.8f};
        float ambient{0.35f};
        float diffuse{0.75f};
        float backLight{0.1f};
        float specStrength{0.5f};
        float specPower{24.0f};
        float gamma{0.85f};
        // Top texture tiling: uv = world map cells * texScale (unused when the
        // asset has no topTexture — the top falls back to the procedural
        // grassA/grassB mix).
        float texScale{1.0f};
        // Bottom blend (stitches the walls with the underlay): the wall
        // darkens to (1 - bottomDarken) at ground level, fading out over
        // bottomBand * plateauHeight.
        float bottomDarken{0.55f};
        float bottomBand{0.35f};
        // Sediment strata banding on the walls (0 = off).
        float strataStrength{0.0f};

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Cliff3dShadingData,
            lightAzimuth, lightElevation, darkColor, goldColor, grassA, grassB,
            veinThreshold, ambient, diffuse, backLight, specStrength, specPower, gamma,
            texScale, bottomDarken, bottomBand, strataStrength);
    };

    // Cliff3D: the whole cliff-field generator parameter set (mirror of
    // cliff::FieldParams from highground_core) + shading palette. No atlas —
    // objects are plain Landscape tiles on the CliffLandscape layer whose
    // tileIndex encodes the vertex nodes; these params drive the renderer.
    struct Cliff3dAssetData
    {
        std::string thumbnail; // optional palette preview
        std::string topTexture; // optional: tiled texture for the flat tops (world-space uv)
        float raisedHeight{96.0f}; // field px per 1.0 plateau height (heightScale)
        // Wall flare: the walls bulge outward by up to flareAmount map cells at
        // ground level, tapering off over flareBand * plateauHeight. Helps the
        // base merge with the underlay (0 = vertical walls).
        float flareAmount{0.0f};
        float flareBand{0.3f};

        // Scalar field (defaults = cliff::FieldParams; ground slab off — the
        // underlay is authored separately).
        float cellSize{0.045f};
        float padding{0.5f};
        float plateauHeight{1.0f};
        float d2Scale{0.5f};
        int blurRadiusCells{3};
        int blurPasses{3};
        float edgeRadius{0.04f};
        float grooveMaskWidth{0.25f};
        float grooveFadeK{1.0f};
        float grooveRimFade{0.12f};
        float fbmAmplitude{0.03f};
        float fbmFrequency{5.0f};
        int fbmOctaves{2};
        float groundDepth{0.3f};
        float groundMargin{0.35f};
        float groundRounding{0.1f};
        bool groundEnabled{false};
        float groovePeriod{0.4f};
        float groovePhase{0.1f};
        float grooveDepthMax{0.1f};
        float grooveSmooth{0.02f};
        std::array<std::array<float, 2>, 3> grooveAngles{{
            {0.6283185f, 0.0f},       // pi/5
            {2.1991149f, 0.5654867f}, // 2.1*pi/3, 0.9*pi/5
            {-2.1467550f, 0.6911504f} // -2.05*pi/3, 1.1*pi/5
        }};

        Cliff3dShadingData shading;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Cliff3dAssetData,
            thumbnail, topTexture, raisedHeight, flareAmount, flareBand, cellSize, padding, plateauHeight, d2Scale,
            blurRadiusCells, blurPasses, edgeRadius, grooveMaskWidth, grooveFadeK,
            grooveRimFade, fbmAmplitude, fbmFrequency, fbmOctaves, groundDepth,
            groundMargin, groundRounding, groundEnabled, groovePeriod, groovePhase,
            grooveDepthMax, grooveSmooth, grooveAngles, shading);
    };

    // Cyclopean3D: landscape_mesh solid-mask composer params (Cyclopean wall
    // style, mirror of MeshBuildSettings). No atlas — objects are plain
    // Landscape tiles on the CyclopeanLandscape layer whose tileIndex encodes
    // the vertex nodes; these params drive the CyclopeanRenderer.
    struct Cyclopean3dAssetData
    {
        std::string thumbnail; // optional preview
        float raisedHeight{3.0f}; // plateau top height in world units (topHeight)
        int rockSeed{1337};
        float rockAmplitude{0.28f};
        bool rockEnabled{true};
        float cornerBevel{0.3f};
        int wallSubdivH{16}; // wallHorizontalSubdivisions (clamped to [1,16])
        int wallSubdivV{16}; // wallVerticalSubdivisions (clamped to [1,16])

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Cyclopean3dAssetData,
            thumbnail, raisedHeight, rockSeed, rockAmplitude, rockEnabled,
            cornerBevel, wallSubdivH, wallSubdivV);
    };

    // Stone3D: the stone-field generator parameter set (base slab = mirror of
    // cliff::FieldParams, stone carve = mirror of stone_gen::StoneFieldParams)
    // + shading palette. No atlas — objects are plain Landscape tiles on the
    // StoneLandscape layer whose tileIndex encodes the vertex nodes; these
    // params drive the renderer. Field names shared by FieldParams and
    // StoneFieldParams (blurPasses, grooveMaskWidth, fbmAmplitude,
    // fbmFrequency) live in a single slot with the StoneFieldParams defaults.
    struct Stone3dAssetData
    {
        std::string thumbnail; // optional palette preview
        std::string topTexture; // optional: tiled texture for the flat tops (world-space uv)
        float raisedHeight{96.0f}; // field px per 1.0 plateau height (heightScale)

        // Base slab (defaults = cliff::FieldParams; its grooves/fbm stay
        // unused by the stone field; ground slab off — the underlay is
        // authored separately).
        float cellSize{0.045f};
        float padding{0.5f};
        float plateauHeight{1.0f};
        float d2Scale{0.5f};
        int blurRadiusCells{3};
        int blurPasses{2};
        float edgeRadius{0.04f};
        float grooveMaskWidth{0.25f};
        float grooveFadeK{1.0f};
        float grooveRimFade{0.12f};
        float fbmAmplitude{0.02f};
        float fbmFrequency{4.0f};
        int fbmOctaves{2};
        float groundDepth{0.3f};
        float groundMargin{0.35f};
        float groundRounding{0.1f};
        bool groundEnabled{false};
        float groovePeriod{0.4f};
        float groovePhase{0.1f};
        float grooveDepthMax{0.1f};
        float grooveSmooth{0.02f};
        std::array<std::array<float, 2>, 3> grooveAngles{{
            {0.6283185f, 0.0f},       // pi/5
            {2.1991149f, 0.5654867f}, // 2.1*pi/3, 0.9*pi/5
            {-2.1467550f, 0.6911504f} // -2.05*pi/3, 1.1*pi/5
        }};

        // Stone carve (defaults = stone_gen::StoneFieldParams).
        float voroScale{2.0f};
        float cellJitter{1.0f};
        float grooveDepth{0.08f};
        float grooveK{2.5f};
        float seed{0.0f};
        bool flatTop{true};
        float flatTopLo{0.55f};
        float flatTopHi{0.85f};
        float rimWidth{0.35f};
        float rimBulge{1.0f};
        float rimNotch{0.04f};

        Cliff3dShadingData shading;
        // Stone shading extras (uniforms only): grass reflection fade on the
        // walls, rim gradient strength on the flat top, top texture mix and
        // tiling.
        float grassFade{0.12f};
        float rimShade{1.0f};
        float topTexMix{1.0f};
        float topTexTiles{1.0f};

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Stone3dAssetData,
            thumbnail, topTexture, raisedHeight, cellSize, padding, plateauHeight, d2Scale,
            blurRadiusCells, blurPasses, edgeRadius, grooveMaskWidth, grooveFadeK,
            grooveRimFade, fbmAmplitude, fbmFrequency, fbmOctaves, groundDepth,
            groundMargin, groundRounding, groundEnabled, groovePeriod, groovePhase,
            grooveDepthMax, grooveSmooth, grooveAngles, voroScale, cellJitter,
            grooveDepth, grooveK, seed, flatTop, flatTopLo, flatTopHi, rimWidth,
            rimBulge, rimNotch, shading, grassFade, rimShade, topTexMix, topTexTiles);
    };

    //Asset
    struct AssetData
    {
        std::filesystem::path indexPath;
        boost::uuids::uuid uuid;
        std::string name;
        LayerTypes::Type layerType{ LayerTypes::Decoration };
        math::vec2 pivot{ 0.5, -1.0f };

        std::optional<ImageAssetData> imageData;
        std::optional<SliceAssetData> sliceData;
        std::optional<Shape3dAssetData> shape3dData;
        std::optional<Cliff3dAssetData> cliff3dData;
        std::optional<Cyclopean3dAssetData> cyclopean3dData;
        std::optional<Stone3dAssetData> stone3dData;

        static AssetData load(const std::filesystem::path& assetsPath);
        static void save(const AssetData& assetData, const std::filesystem::path& assetsPath);

        std::filesystem::path root() const;
    };

    void to_json(nlohmann::json& j, const AssetData& obj);
    void from_json(const nlohmann::json& j, AssetData& obj);

    //AsssetsPack
    struct AssetsPack: public std::deque<AssetData> 
    {
        static AssetsPack load(const std::filesystem::path& assetsPath);
        static void save(const AssetsPack& assetsPack, const std::filesystem::path& assetsPath);

        std::filesystem::path path;
   
    };

    //Assets
    struct Assets: public std::deque<AssetsPack>
    {
        static Assets load(const std::filesystem::path& assetsPath);
        static void save(const Assets& assets, const std::filesystem::path& assetsPath);
    };

}