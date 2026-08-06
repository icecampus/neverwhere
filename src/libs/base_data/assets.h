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
        stone3d,
        texture2d,
        tech3d
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

    // Texture2D: tiling-texture landscape brush (multi-texture blend layer).
    // No atlas — objects are plain Landscape tiles on the TextureLandscape
    // layer whose tileIndex encodes the vertex nodes (same convention as
    // slice/raised); the texture file + tiling drive the renderer.
    struct Texture2dAssetData
    {
        std::string thumbnail;
        std::string texture;          // tiling texture file, relative to the bundle root
        float tilingRepeats{1.0f};    // texture repeats per cell width

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Texture2dAssetData,
            thumbnail, texture, tilingRepeats);
    };

    // Tech3D: the tech-field generator parameter set (mirror of
    // tech::TechFieldParams from highground_core — the TechnicalGrass
    // ridge/valley tileset semantics as real geometry) + shading palette.
    // No atlas — objects are plain Landscape tiles on the TechLandscape layer
    // whose tileIndex encodes the vertex nodes; these params drive the
    // renderer.
    struct Tech3dAssetData
    {
        Tech3dAssetData()
        {
            // TechnicalGrass look: earth ramps, grassy tops, no veins, muted
            // spec, no bottom darkening (flat-lit ramps like the 2D tileset).
            shading.darkColor = {0.25f, 0.18f, 0.12f};
            shading.goldColor = {0.58f, 0.44f, 0.29f};
            shading.veinThreshold = 2.0f;
            shading.specStrength = 0.15f;
            shading.bottomDarken = 0.0f;
        }

        std::string thumbnail; // optional palette preview
        float raisedHeight{96.0f}; // field px per 1.0 world height (heightScale)

        // Scalar field (defaults = tech::TechFieldParams).
        float cellSize{0.06f};
        float padding{0.5f};
        float levelHeight{0.35f};  // world height of one level (the Python ELEVATION analog)
        float groundDepth{0.05f};  // bottom slab thickness
        float style{0.0f};         // 0 = Ridge .. 1 = Valley (center-height blend)
        float soften{0.0f};        // 0 = linear ramps, 1 = smoothstep shoulders
        float creaseWidth{0.05f};  // dark tile contour width (0 = off)
        int blurPasses{0};         // sampled-field anti-terracing blur
        // Shoreline outline ("yellow around green"): the 8-neighborhood of
        // the painted land nodes (minus the land) forms a ring at
        // -outlineDepth * levelHeight — the ramps continue below the water
        // plane. 0 = off (land only).
        float outlineDepth{0.0f};

        Cliff3dShadingData shading;

        friend void to_json(nlohmann::json& j, const Tech3dAssetData& d)
        {
            j = nlohmann::json{
                {"thumbnail", d.thumbnail},
                {"raisedHeight", d.raisedHeight},
                {"cellSize", d.cellSize},
                {"padding", d.padding},
                {"levelHeight", d.levelHeight},
                {"groundDepth", d.groundDepth},
                {"style", d.style},
                {"soften", d.soften},
                {"creaseWidth", d.creaseWidth},
                {"blurPasses", d.blurPasses},
                {"outlineDepth", d.outlineDepth},
                {"shading", d.shading},
            };
        }

        friend void from_json(const nlohmann::json& j, Tech3dAssetData& d)
        {
            // Field-by-field: omitted keys keep the Tech3dAssetData defaults.
            if (j.contains("thumbnail")) j.at("thumbnail").get_to(d.thumbnail);
            if (j.contains("raisedHeight")) j.at("raisedHeight").get_to(d.raisedHeight);
            if (j.contains("cellSize")) j.at("cellSize").get_to(d.cellSize);
            if (j.contains("padding")) j.at("padding").get_to(d.padding);
            if (j.contains("levelHeight")) j.at("levelHeight").get_to(d.levelHeight);
            if (j.contains("groundDepth")) j.at("groundDepth").get_to(d.groundDepth);
            if (j.contains("style")) j.at("style").get_to(d.style);
            if (j.contains("soften")) j.at("soften").get_to(d.soften);
            if (j.contains("creaseWidth")) j.at("creaseWidth").get_to(d.creaseWidth);
            if (j.contains("blurPasses")) j.at("blurPasses").get_to(d.blurPasses);
            if (j.contains("outlineDepth")) j.at("outlineDepth").get_to(d.outlineDepth);
            if (j.contains("shading"))
            {
                // Field-wise apply: a partial shading block must not reset
                // the retuned TechnicalGrass palette to the cliff defaults.
                const nlohmann::json& s = j.at("shading");
                if (s.contains("lightAzimuth")) s.at("lightAzimuth").get_to(d.shading.lightAzimuth);
                if (s.contains("lightElevation")) s.at("lightElevation").get_to(d.shading.lightElevation);
                if (s.contains("darkColor")) s.at("darkColor").get_to(d.shading.darkColor);
                if (s.contains("goldColor")) s.at("goldColor").get_to(d.shading.goldColor);
                if (s.contains("grassA")) s.at("grassA").get_to(d.shading.grassA);
                if (s.contains("grassB")) s.at("grassB").get_to(d.shading.grassB);
                if (s.contains("veinThreshold")) s.at("veinThreshold").get_to(d.shading.veinThreshold);
                if (s.contains("ambient")) s.at("ambient").get_to(d.shading.ambient);
                if (s.contains("diffuse")) s.at("diffuse").get_to(d.shading.diffuse);
                if (s.contains("backLight")) s.at("backLight").get_to(d.shading.backLight);
                if (s.contains("specStrength")) s.at("specStrength").get_to(d.shading.specStrength);
                if (s.contains("specPower")) s.at("specPower").get_to(d.shading.specPower);
                if (s.contains("gamma")) s.at("gamma").get_to(d.shading.gamma);
                if (s.contains("texScale")) s.at("texScale").get_to(d.shading.texScale);
                if (s.contains("bottomDarken")) s.at("bottomDarken").get_to(d.shading.bottomDarken);
                if (s.contains("bottomBand")) s.at("bottomBand").get_to(d.shading.bottomBand);
                if (s.contains("strataStrength")) s.at("strataStrength").get_to(d.shading.strataStrength);
            }
        }
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
        std::optional<Texture2dAssetData> texture2dData;
        std::optional<Tech3dAssetData> tech3dData;

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