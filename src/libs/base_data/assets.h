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
        cliff3d
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
    // TileShapePlayground). Objects are plain Landscape tiles on the
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

    // Cliff3D shading palette/light (mirror of the TileShapePlayground
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

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Cliff3dShadingData,
            lightAzimuth, lightElevation, darkColor, goldColor, grassA, grassB,
            veinThreshold, ambient, diffuse, backLight, specStrength, specPower, gamma);
    };

    // Cliff3D: the whole cliff-field generator parameter set (mirror of
    // cliff::FieldParams from highground_core) + shading palette. No atlas —
    // objects are plain Landscape tiles on the CliffLandscape layer whose
    // tileIndex encodes the vertex nodes; these params drive the renderer.
    struct Cliff3dAssetData
    {
        std::string thumbnail; // optional palette preview
        float raisedHeight{96.0f}; // field px per 1.0 plateau height (heightScale)

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
            thumbnail, raisedHeight, cellSize, padding, plateauHeight, d2Scale,
            blurRadiusCells, blurPasses, edgeRadius, grooveMaskWidth, grooveFadeK,
            grooveRimFade, fbmAmplitude, fbmFrequency, fbmOctaves, groundDepth,
            groundMargin, groundRounding, groundEnabled, groovePeriod, groovePhase,
            grooveDepthMax, grooveSmooth, grooveAngles, shading);
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