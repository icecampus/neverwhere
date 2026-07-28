#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "game_data/types.h"

namespace game_data {

struct SliceAssetData {
    std::string thumbnail;
    std::string atlas;
};

struct ImageAssetData {
    std::string imageFilename;
    float width = 1.0f; // width in map cells (screenWidth = cellWidth * width)
};

// Shape3D (raised) assets — slice atlas + raised presentation params
// (mirror of BaseData::Shape3dAssetData; renderer-only fields).
struct Shape3dAssetData {
    std::string thumbnail;
    std::string atlas;
    std::string topTexture; // optional: tiled ground texture for the raised top
    float raisedHeight = 32.0f;
    bool rockWalls = true;
    float rockAmplitude = 0.28f;
    float rockBevel = 0.3f;
};

// Cliff3D shading palette/light (mirror of BaseData::Cliff3dShadingData).
struct Cliff3dShadingData {
    float lightAzimuth = 2.23f;   // radians
    float lightElevation = 0.85f; // radians
    std::array<float, 3> darkColor{0.38f, 0.38f, 0.42f};
    std::array<float, 3> goldColor{0.75f, 0.62f, 0.5f};
    std::array<float, 3> grassA{0.4f, 0.62f, 0.35f};
    std::array<float, 3> grassB{0.6f, 0.65f, 0.4f};
    float veinThreshold = 0.8f;
    float ambient = 0.35f;
    float diffuse = 0.75f;
    float backLight = 0.1f;
    float specStrength = 0.5f;
    float specPower = 24.0f;
    float gamma = 0.85f;
    // Top texture tiling: uv = world map cells * texScale (unused without a
    // topTexture — the top falls back to the procedural grassA/grassB mix).
    float texScale = 1.0f;
    // Bottom blend: the wall darkens to (1 - bottomDarken) at ground level,
    // fading out over bottomBand * plateauHeight (stitches with the underlay).
    float bottomDarken = 0.55f;
    float bottomBand = 0.35f;
    // Sediment strata banding on the walls (0 = off).
    float strataStrength = 0.0f;
};

// Cliff3D assets — the whole cliff-field generator parameter set (mirror of
// BaseData::Cliff3dAssetData / cliff::FieldParams) + shading palette.
// No atlas; tiles on the CliffLandscape layer encode the vertex nodes.
struct Cliff3dAssetData {
    std::string thumbnail; // optional palette preview
    std::string topTexture; // optional: tiled texture for the flat tops (world-space uv)
    float raisedHeight = 96.0f; // field px per 1.0 plateau height (heightScale)
    // Wall flare: the walls bulge outward by up to flareAmount map cells at
    // ground level, tapering off over flareBand * plateauHeight.
    float flareAmount = 0.0f;
    float flareBand = 0.3f;

    float cellSize = 0.045f;
    float padding = 0.5f;
    float plateauHeight = 1.0f;
    float d2Scale = 0.5f;
    int blurRadiusCells = 3;
    int blurPasses = 3;
    float edgeRadius = 0.04f;
    float grooveMaskWidth = 0.25f;
    float grooveFadeK = 1.0f;
    float grooveRimFade = 0.12f;
    float fbmAmplitude = 0.03f;
    float fbmFrequency = 5.0f;
    int fbmOctaves = 2;
    float groundDepth = 0.3f;
    float groundMargin = 0.35f;
    float groundRounding = 0.1f;
    bool groundEnabled = false; // underlay is authored separately
    float groovePeriod = 0.4f;
    float groovePhase = 0.1f;
    float grooveDepthMax = 0.1f;
    float grooveSmooth = 0.02f;
    std::array<std::array<float, 2>, 3> grooveAngles{{
        {0.6283185f, 0.0f},
        {2.1991149f, 0.5654867f},
        {-2.1467550f, 0.6911504f}
    }};

    Cliff3dShadingData shading;
};

// Cyclopean3D assets — landscape_mesh solid-mask composer params (Cyclopean
// wall style; mirror of BaseData::Cyclopean3dAssetData). No atlas; tiles on
// the CyclopeanLandscape layer encode the vertex nodes.
struct Cyclopean3dAssetData {
    std::string thumbnail; // optional preview
    float raisedHeight = 3.0f; // plateau top height in world units (topHeight)
    int rockSeed = 1337;
    float rockAmplitude = 0.28f;
    bool rockEnabled = true;
    float cornerBevel = 0.3f;
    int wallSubdivH = 16;
    int wallSubdivV = 16;
};

struct AssetData {
    std::filesystem::path indexPath;
    std::string uuid;
    LayerType layerType{LayerType::Decoration};
    glm::vec2 pivot{0.0f, 0.0f};

    std::optional<SliceAssetData> slice;
    std::optional<ImageAssetData> image;
    std::optional<Shape3dAssetData> shape3d;
    std::optional<Cliff3dAssetData> cliff3d;
    std::optional<Cyclopean3dAssetData> cyclopean3d;

    std::filesystem::path root() const { return indexPath.parent_path(); }
};

inline void from_json(const nlohmann::json& j, SliceAssetData& d) {
    j.at("thumbnail").get_to(d.thumbnail);
    j.at("atlas").get_to(d.atlas);
}

inline void from_json(const nlohmann::json& j, Shape3dAssetData& d) {
    j.at("thumbnail").get_to(d.thumbnail);
    j.at("atlas").get_to(d.atlas);
    if (j.contains("topTexture")) j.at("topTexture").get_to(d.topTexture);
    if (j.contains("raisedHeight")) j.at("raisedHeight").get_to(d.raisedHeight);
    if (j.contains("rockWalls")) j.at("rockWalls").get_to(d.rockWalls);
    if (j.contains("rockAmplitude")) j.at("rockAmplitude").get_to(d.rockAmplitude);
    if (j.contains("rockBevel")) j.at("rockBevel").get_to(d.rockBevel);
}

inline void from_json(const nlohmann::json& j, ImageAssetData& d) {
    j.at("imageFilename").get_to(d.imageFilename);
    j.at("width").get_to(d.width);
}

inline void from_json(const nlohmann::json& j, Cliff3dShadingData& d) {
    if (j.contains("lightAzimuth")) j.at("lightAzimuth").get_to(d.lightAzimuth);
    if (j.contains("lightElevation")) j.at("lightElevation").get_to(d.lightElevation);
    if (j.contains("darkColor")) j.at("darkColor").get_to(d.darkColor);
    if (j.contains("goldColor")) j.at("goldColor").get_to(d.goldColor);
    if (j.contains("grassA")) j.at("grassA").get_to(d.grassA);
    if (j.contains("grassB")) j.at("grassB").get_to(d.grassB);
    if (j.contains("veinThreshold")) j.at("veinThreshold").get_to(d.veinThreshold);
    if (j.contains("ambient")) j.at("ambient").get_to(d.ambient);
    if (j.contains("diffuse")) j.at("diffuse").get_to(d.diffuse);
    if (j.contains("backLight")) j.at("backLight").get_to(d.backLight);
    if (j.contains("specStrength")) j.at("specStrength").get_to(d.specStrength);
    if (j.contains("specPower")) j.at("specPower").get_to(d.specPower);
    if (j.contains("gamma")) j.at("gamma").get_to(d.gamma);
    if (j.contains("texScale")) j.at("texScale").get_to(d.texScale);
    if (j.contains("bottomDarken")) j.at("bottomDarken").get_to(d.bottomDarken);
    if (j.contains("bottomBand")) j.at("bottomBand").get_to(d.bottomBand);
    if (j.contains("strataStrength")) j.at("strataStrength").get_to(d.strataStrength);
}

inline void from_json(const nlohmann::json& j, Cliff3dAssetData& d) {
    if (j.contains("thumbnail")) j.at("thumbnail").get_to(d.thumbnail);
    if (j.contains("topTexture")) j.at("topTexture").get_to(d.topTexture);
    if (j.contains("raisedHeight")) j.at("raisedHeight").get_to(d.raisedHeight);
    if (j.contains("flareAmount")) j.at("flareAmount").get_to(d.flareAmount);
    if (j.contains("flareBand")) j.at("flareBand").get_to(d.flareBand);
    if (j.contains("cellSize")) j.at("cellSize").get_to(d.cellSize);
    if (j.contains("padding")) j.at("padding").get_to(d.padding);
    if (j.contains("plateauHeight")) j.at("plateauHeight").get_to(d.plateauHeight);
    if (j.contains("d2Scale")) j.at("d2Scale").get_to(d.d2Scale);
    if (j.contains("blurRadiusCells")) j.at("blurRadiusCells").get_to(d.blurRadiusCells);
    if (j.contains("blurPasses")) j.at("blurPasses").get_to(d.blurPasses);
    if (j.contains("edgeRadius")) j.at("edgeRadius").get_to(d.edgeRadius);
    if (j.contains("grooveMaskWidth")) j.at("grooveMaskWidth").get_to(d.grooveMaskWidth);
    if (j.contains("grooveFadeK")) j.at("grooveFadeK").get_to(d.grooveFadeK);
    if (j.contains("grooveRimFade")) j.at("grooveRimFade").get_to(d.grooveRimFade);
    if (j.contains("fbmAmplitude")) j.at("fbmAmplitude").get_to(d.fbmAmplitude);
    if (j.contains("fbmFrequency")) j.at("fbmFrequency").get_to(d.fbmFrequency);
    if (j.contains("fbmOctaves")) j.at("fbmOctaves").get_to(d.fbmOctaves);
    if (j.contains("groundDepth")) j.at("groundDepth").get_to(d.groundDepth);
    if (j.contains("groundMargin")) j.at("groundMargin").get_to(d.groundMargin);
    if (j.contains("groundRounding")) j.at("groundRounding").get_to(d.groundRounding);
    if (j.contains("groundEnabled")) j.at("groundEnabled").get_to(d.groundEnabled);
    if (j.contains("groovePeriod")) j.at("groovePeriod").get_to(d.groovePeriod);
    if (j.contains("groovePhase")) j.at("groovePhase").get_to(d.groovePhase);
    if (j.contains("grooveDepthMax")) j.at("grooveDepthMax").get_to(d.grooveDepthMax);
    if (j.contains("grooveSmooth")) j.at("grooveSmooth").get_to(d.grooveSmooth);
    if (j.contains("grooveAngles")) j.at("grooveAngles").get_to(d.grooveAngles);
    if (j.contains("shading")) d.shading = j["shading"].get<Cliff3dShadingData>();
}

inline void from_json(const nlohmann::json& j, Cyclopean3dAssetData& d) {
    if (j.contains("thumbnail")) j.at("thumbnail").get_to(d.thumbnail);
    if (j.contains("raisedHeight")) j.at("raisedHeight").get_to(d.raisedHeight);
    if (j.contains("rockSeed")) j.at("rockSeed").get_to(d.rockSeed);
    if (j.contains("rockAmplitude")) j.at("rockAmplitude").get_to(d.rockAmplitude);
    if (j.contains("rockEnabled")) j.at("rockEnabled").get_to(d.rockEnabled);
    if (j.contains("cornerBevel")) j.at("cornerBevel").get_to(d.cornerBevel);
    if (j.contains("wallSubdivH")) j.at("wallSubdivH").get_to(d.wallSubdivH);
    if (j.contains("wallSubdivV")) j.at("wallSubdivV").get_to(d.wallSubdivV);
}

inline void from_json(const nlohmann::json& j, AssetData& a) {
    j.at("uuid").get_to(a.uuid);
    // pivot may be omitted in older assets; default (0,0)
    if (j.contains("pivot")) {
        j.at("pivot").at("x").get_to(a.pivot.x);
        j.at("pivot").at("y").get_to(a.pivot.y);
    }

    // layerType is a string, same as in base_data/assets.cpp
    std::string layerStr;
    j.at("layerType").get_to(layerStr);
    if (layerStr == "Decoration") a.layerType = LayerType::Decoration;
    else if (layerStr == "BaseLandscape") a.layerType = LayerType::BaseLandscape;
    else if (layerStr == "GameplayInteractive") a.layerType = LayerType::GameplayInteractive;
    else if (layerStr == "RaisedLandscape") a.layerType = LayerType::RaisedLandscape;
    else if (layerStr == "CliffLandscape") a.layerType = LayerType::CliffLandscape;
    else if (layerStr == "CyclopeanLandscape") a.layerType = LayerType::CyclopeanLandscape;

    if (j.contains("slice") && !j["slice"].is_null()) {
        a.slice = j["slice"].get<SliceAssetData>();
    }

    if (j.contains("image") && !j["image"].is_null()) {
        a.image = j["image"].get<ImageAssetData>();
    }

    if (j.contains("shape3d") && !j["shape3d"].is_null()) {
        a.shape3d = j["shape3d"].get<Shape3dAssetData>();
    }

    if (j.contains("cliff3d") && !j["cliff3d"].is_null()) {
        a.cliff3d = j["cliff3d"].get<Cliff3dAssetData>();
    }

    if (j.contains("cyclopean3d") && !j["cyclopean3d"].is_null()) {
        a.cyclopean3d = j["cyclopean3d"].get<Cyclopean3dAssetData>();
    }
}

struct AssetIndexEntry {
    std::string uuid;
    LayerType layerType{LayerType::Decoration};
    glm::vec2 pivot{0.0f, 0.0f};

    // Slice (atlas) assets — landscape tiles.
    std::filesystem::path atlasPath;
    int cols = 4;
    int rows = 6;

    // Plain image assets — Tile2D sprites.
    std::filesystem::path imagePath;
    float widthCells = 1.0f;

    // Shape3D (raised) assets — raised landscape tiles with cliff walls.
    bool shape3d = false;
    float raisedHeight = 32.0f;
    bool rockWalls = true;
    float rockAmplitude = 0.28f;
    float rockBevel = 0.3f;
    std::filesystem::path topTexturePath; // optional tiled texture for the raised top

    // Cliff3D (cliff-field) assets — surface-nets cliffs, no atlas; the full
    // generator + shading parameter set rides along.
    bool cliff3d = false;
    Cliff3dAssetData cliff;

    // Cyclopean3D (landscape_mesh) assets — solid-mask plateau with Cyclopean
    // walls, no atlas.
    bool cyclopean3d = false;
    Cyclopean3dAssetData cyclopean;

    bool isSlice() const { return !atlasPath.empty(); }
    bool isImage() const { return !imagePath.empty(); }
    bool isShape3d() const { return shape3d; }
    bool isCliff3d() const { return cliff3d; }
    bool isCyclopean3d() const { return cyclopean3d; }
};

struct AssetIndex {
    std::unordered_map<std::string, AssetIndexEntry> byUuid;

    static AssetIndex load(const std::filesystem::path& assetsRoot);
    const AssetIndexEntry* find(const std::string& uuid) const;
};

} // namespace game_data

