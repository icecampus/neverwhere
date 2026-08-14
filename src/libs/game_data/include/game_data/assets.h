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
    // Underwater dissolve: fragments below the water level (world y < 0) fade
    // out over this many world units (0 = off). One levelHeight dissolves the
    // whole shoreline shelf; anything at/above y=0 stays fully opaque.
    float underwaterFade = 0.0f;
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

// Stone3D assets — the stone-field generator parameter set (base slab =
// mirror of cliff::FieldParams, stone carve = mirror of
// stone_gen::StoneFieldParams; mirror of BaseData::Stone3dAssetData) +
// shading palette. No atlas; tiles on the StoneLandscape layer encode the
// vertex nodes. Field names shared by FieldParams and StoneFieldParams
// (blurPasses, grooveMaskWidth, fbmAmplitude, fbmFrequency) live in a single
// slot with the StoneFieldParams defaults.
struct Stone3dAssetData {
    std::string thumbnail; // optional palette preview
    std::string topTexture; // optional: tiled texture for the flat tops (world-space uv)
    float raisedHeight = 96.0f; // field px per 1.0 plateau height (heightScale)

    // Base slab (its grooves/fbm stay unused by the stone field; ground slab
    // off — the underlay is authored separately).
    float cellSize = 0.045f;
    float padding = 0.5f;
    float plateauHeight = 1.0f;
    float d2Scale = 0.5f;
    int blurRadiusCells = 3;
    int blurPasses = 2;
    float edgeRadius = 0.04f;
    float grooveMaskWidth = 0.25f;
    float grooveFadeK = 1.0f;
    float grooveRimFade = 0.12f;
    float fbmAmplitude = 0.02f;
    float fbmFrequency = 4.0f;
    int fbmOctaves = 2;
    float groundDepth = 0.3f;
    float groundMargin = 0.35f;
    float groundRounding = 0.1f;
    bool groundEnabled = false;
    float groovePeriod = 0.4f;
    float groovePhase = 0.1f;
    float grooveDepthMax = 0.1f;
    float grooveSmooth = 0.02f;
    std::array<std::array<float, 2>, 3> grooveAngles{{
        {0.6283185f, 0.0f},
        {2.1991149f, 0.5654867f},
        {-2.1467550f, 0.6911504f}
    }};

    // Stone carve.
    float voroScale = 2.0f;
    float cellJitter = 1.0f;
    float grooveDepth = 0.08f;
    float grooveK = 2.5f;
    float seed = 0.0f;
    bool flatTop = true;
    float flatTopLo = 0.55f;
    float flatTopHi = 0.85f;
    float rimWidth = 0.35f;
    float rimBulge = 1.0f;
    float rimNotch = 0.04f;

    Cliff3dShadingData shading;
    // Stone shading extras: grass reflection fade on the walls, rim gradient
    // strength on the flat top, top texture mix and tiling.
    float grassFade = 0.12f;
    float rimShade = 1.0f;
    float topTexMix = 1.0f;
    float topTexTiles = 1.0f;
};

// Tech3D assets — the tech-field generator parameter set (mirror of
// tech::TechFieldParams from highground_core: the TechnicalGrass ridge/valley
// tileset semantics as real geometry) + shading palette. No atlas; tiles on
// the TechLandscape layer encode the vertex nodes.
struct Tech3dAssetData {
    Tech3dAssetData() {
        // TechnicalGrass look: earth ramps, grassy tops, no veins, muted spec,
        // no bottom darkening (the ramps stay flat-lit like the 2D tileset).
        shading.darkColor = {0.25f, 0.18f, 0.12f};
        shading.goldColor = {0.58f, 0.44f, 0.29f};
        shading.veinThreshold = 2.0f;
        shading.specStrength = 0.15f;
        shading.bottomDarken = 0.0f;
    }

    std::string thumbnail; // optional palette preview
    float raisedHeight = 96.0f; // field px per 1.0 world height (heightScale)

    // Scalar field (defaults = tech::TechFieldParams).
    float cellSize = 0.06f;
    float padding = 0.5f;
    float levelHeight = 0.35f;  // world height of one level (the Python ELEVATION analog)
    float groundDepth = 0.05f;  // bottom slab thickness
    float style = 0.0f;         // 0 = Ridge .. 1 = Valley (center-height blend)
    float soften = 0.0f;        // 0 = linear ramps, 1 = smoothstep shoulders
    float creaseWidth = 0.05f;  // dark tile contour width (0 = off)
    int blurPasses = 1;         // sampled-field anti-terracing blur
    // Shoreline outline ("yellow around green"): the 8-neighborhood of the
    // painted land nodes (minus the land) forms a ring at
    // -outlineDepth * levelHeight — the ramps continue below the water
    // plane. 0 = off (land only).
    float outlineDepth = 0.0f;

    Cliff3dShadingData shading;
};

// Mask3D assets — the mask-field generator parameter set (mirror of
// mask::MaskFieldParams from highground_core: the painted node silhouette
// extruded into a plate with a sloped skirt, standing sinkFraction of its
// height below the water plane) + the PBR-lite material (an ambientCG-style
// map set in the bundle) + shading palette. No atlas; tiles on the
// MaskLandscape layer encode the vertex nodes.
struct Mask3dAssetData {
    Mask3dAssetData() {
        // Sand fallback look (the material covers it when the maps load):
        // warm light ramps, no veins, muted spec, no bottom darkening.
        shading.darkColor = {0.55f, 0.48f, 0.38f};
        shading.goldColor = {0.85f, 0.78f, 0.62f};
        shading.grassA = {0.82f, 0.76f, 0.60f};
        shading.grassB = {0.90f, 0.85f, 0.70f};
        shading.veinThreshold = 2.0f;
        shading.specStrength = 0.15f;
        shading.bottomDarken = 0.0f;
    }

    std::string thumbnail; // optional palette preview
    float raisedHeight = 96.0f; // field px per 1.0 world height (heightScale)

    // Scalar field (defaults = mask::MaskFieldParams).
    float cellSize = 0.04f;
    float padding = 0.5f;
    float height = 0.2f;        // core plate height in world units
    float spreadDistance = 0.0f; // skirt width in cells (the height ramp
                                 // rolling off to the bottom plane)
    float sinkFraction = 0.5f;  // share of the height below the water plane
    int blurPasses = 0;         // sampled-field blur (0 = crisp walls)

    // Micro relief (displacement): the <materialSet>_Displacement.jpg raster,
    // low-passed to the dune scale at load.
    float reliefDepth = 0.02f;  // dune amplitude in world units (0 = off)
    float reliefTiling = 1.0f;  // relief repeats per cell (kept in sync with
                                // matTiling so dunes align with the ripples)
    float reliefFade = 0.15f;   // fade-in distance from the core contour

    // PBR-lite material: the bundle holds <materialSet>_Color.jpg,
    // _NormalGL.jpg, _AmbientOcclusion.jpg, _Roughness.jpg (and
    // _Displacement.jpg for the relief). Empty = palette look.
    std::string materialSet;
    float matTiling = 1.0f;     // texture repeats per world unit
    float matAlbedo = 1.0f;     // channel strengths (0 = the channel falls
    float matNormal = 1.0f;     // back to the palette/flat shading)
    float matAo = 1.0f;
    float matRough = 0.7f;

    Cliff3dShadingData shading;
};

// Building3D: a placed GLB mesh occupying a cell footprint (default 3x3).
struct Building3dAssetData {
    std::string thumbnail;
    std::string model;
    std::string albedo;
    int footprintWidth = 3;
    int footprintHeight = 3;
    float heightScale = 96.0f;
    float yawDegrees = 0.0f;
    float scale = 1.0f; // uniform multiplier on top of the footprint fit
};

// Tiling-texture landscape brush (multi-texture blend layer).
struct Texture2dAssetData {
    std::string thumbnail;
    std::string texture;          // tiling texture file, relative to the bundle root
    float tilingRepeats = 1.0f;   // texture repeats per cell width
};

// Fence3D: fence brush (FenceLandscape layer, Fence piece objects). The bundle
// carries the four baked piece meshes by convention (fence_post/fence_corner/
// fence_section2/fence_section3 .obj+.mtl in the bundle root).
struct Fence3dAssetData {
    std::string thumbnail;
    float metersToPoints = 96.0f; // vertical lift of 1 m in screen points
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
    std::optional<Stone3dAssetData> stone3d;
    std::optional<Texture2dAssetData> texture2d;
    std::optional<Tech3dAssetData> tech3d;
    std::optional<Mask3dAssetData> mask3d;
    std::optional<Fence3dAssetData> fence3d;
    std::optional<Building3dAssetData> building3d;

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
    if (j.contains("underwaterFade")) j.at("underwaterFade").get_to(d.underwaterFade);
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

inline void from_json(const nlohmann::json& j, Stone3dAssetData& d) {
    if (j.contains("thumbnail")) j.at("thumbnail").get_to(d.thumbnail);
    if (j.contains("topTexture")) j.at("topTexture").get_to(d.topTexture);
    if (j.contains("raisedHeight")) j.at("raisedHeight").get_to(d.raisedHeight);
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
    if (j.contains("voroScale")) j.at("voroScale").get_to(d.voroScale);
    if (j.contains("cellJitter")) j.at("cellJitter").get_to(d.cellJitter);
    if (j.contains("grooveDepth")) j.at("grooveDepth").get_to(d.grooveDepth);
    if (j.contains("grooveK")) j.at("grooveK").get_to(d.grooveK);
    if (j.contains("seed")) j.at("seed").get_to(d.seed);
    if (j.contains("flatTop")) j.at("flatTop").get_to(d.flatTop);
    if (j.contains("flatTopLo")) j.at("flatTopLo").get_to(d.flatTopLo);
    if (j.contains("flatTopHi")) j.at("flatTopHi").get_to(d.flatTopHi);
    if (j.contains("rimWidth")) j.at("rimWidth").get_to(d.rimWidth);
    if (j.contains("rimBulge")) j.at("rimBulge").get_to(d.rimBulge);
    if (j.contains("rimNotch")) j.at("rimNotch").get_to(d.rimNotch);
    if (j.contains("shading")) d.shading = j["shading"].get<Cliff3dShadingData>();
    if (j.contains("grassFade")) j.at("grassFade").get_to(d.grassFade);
    if (j.contains("rimShade")) j.at("rimShade").get_to(d.rimShade);
    if (j.contains("topTexMix")) j.at("topTexMix").get_to(d.topTexMix);
    if (j.contains("topTexTiles")) j.at("topTexTiles").get_to(d.topTexTiles);
}

inline void from_json(const nlohmann::json& j, Texture2dAssetData& d) {
    if (j.contains("thumbnail")) j.at("thumbnail").get_to(d.thumbnail);
    if (j.contains("texture")) j.at("texture").get_to(d.texture);
    if (j.contains("tilingRepeats")) j.at("tilingRepeats").get_to(d.tilingRepeats);
}

inline void from_json(const nlohmann::json& j, Fence3dAssetData& d) {
    if (j.contains("thumbnail")) j.at("thumbnail").get_to(d.thumbnail);
    if (j.contains("metersToPoints")) j.at("metersToPoints").get_to(d.metersToPoints);
}

inline void from_json(const nlohmann::json& j, Tech3dAssetData& d) {
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
    // Field-wise apply: a partial shading block must not reset the retuned
    // TechnicalGrass palette to the cliff defaults (omitted keys keep them).
    if (j.contains("shading")) from_json(j["shading"], d.shading);
}

inline void from_json(const nlohmann::json& j, Mask3dAssetData& d) {
    if (j.contains("thumbnail")) j.at("thumbnail").get_to(d.thumbnail);
    if (j.contains("raisedHeight")) j.at("raisedHeight").get_to(d.raisedHeight);
    if (j.contains("cellSize")) j.at("cellSize").get_to(d.cellSize);
    if (j.contains("padding")) j.at("padding").get_to(d.padding);
    if (j.contains("height")) j.at("height").get_to(d.height);
    if (j.contains("spreadDistance")) j.at("spreadDistance").get_to(d.spreadDistance);
    if (j.contains("sinkFraction")) j.at("sinkFraction").get_to(d.sinkFraction);
    if (j.contains("blurPasses")) j.at("blurPasses").get_to(d.blurPasses);
    if (j.contains("reliefDepth")) j.at("reliefDepth").get_to(d.reliefDepth);
    if (j.contains("reliefTiling")) j.at("reliefTiling").get_to(d.reliefTiling);
    if (j.contains("reliefFade")) j.at("reliefFade").get_to(d.reliefFade);
    if (j.contains("materialSet")) j.at("materialSet").get_to(d.materialSet);
    if (j.contains("matTiling")) j.at("matTiling").get_to(d.matTiling);
    if (j.contains("matAlbedo")) j.at("matAlbedo").get_to(d.matAlbedo);
    if (j.contains("matNormal")) j.at("matNormal").get_to(d.matNormal);
    if (j.contains("matAo")) j.at("matAo").get_to(d.matAo);
    if (j.contains("matRough")) j.at("matRough").get_to(d.matRough);
    // Field-wise apply: a partial shading block must not reset the retuned
    // sand palette to the cliff defaults (omitted keys keep them).
    if (j.contains("shading")) from_json(j["shading"], d.shading);
}

inline void from_json(const nlohmann::json& j, Building3dAssetData& d) {
    if (j.contains("thumbnail")) j.at("thumbnail").get_to(d.thumbnail);
    if (j.contains("model")) j.at("model").get_to(d.model);
    if (j.contains("albedo")) j.at("albedo").get_to(d.albedo);
    if (j.contains("footprintWidth")) j.at("footprintWidth").get_to(d.footprintWidth);
    if (j.contains("footprintHeight")) j.at("footprintHeight").get_to(d.footprintHeight);
    if (j.contains("heightScale")) j.at("heightScale").get_to(d.heightScale);
    if (j.contains("yawDegrees")) j.at("yawDegrees").get_to(d.yawDegrees);
    if (j.contains("scale")) j.at("scale").get_to(d.scale);
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
    else if (layerStr == "StoneLandscape") a.layerType = LayerType::StoneLandscape;
    else if (layerStr == "TextureLandscape") a.layerType = LayerType::TextureLandscape;
    else if (layerStr == "TechLandscape") a.layerType = LayerType::TechLandscape;
    else if (layerStr == "MaskLandscape") a.layerType = LayerType::MaskLandscape;
    else if (layerStr == "FenceLandscape") a.layerType = LayerType::FenceLandscape;

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

    if (j.contains("stone3d") && !j["stone3d"].is_null()) {
        a.stone3d = j["stone3d"].get<Stone3dAssetData>();
    }

    if (j.contains("texture2d") && !j["texture2d"].is_null()) {
        a.texture2d = j["texture2d"].get<Texture2dAssetData>();
    }

    if (j.contains("tech3d") && !j["tech3d"].is_null()) {
        a.tech3d = j["tech3d"].get<Tech3dAssetData>();
    }

    if (j.contains("mask3d") && !j["mask3d"].is_null()) {
        a.mask3d = j["mask3d"].get<Mask3dAssetData>();
    }

    if (j.contains("fence3d") && !j["fence3d"].is_null()) {
        a.fence3d = j["fence3d"].get<Fence3dAssetData>();
    }
    if (j.contains("building3d") && !j["building3d"].is_null()) {
        a.building3d = j["building3d"].get<Building3dAssetData>();
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

    // Stone3D (stone-field) assets — voronoi-carved surface-nets plateau, no
    // atlas; the full generator + shading parameter set rides along.
    bool stone3d = false;
    Stone3dAssetData stone;

    // Texture2D assets — tiling-texture landscape brush (multi-texture blend
    // layer), no atlas; the texture path resolves against the bundle root.
    bool texture2d = false;
    Texture2dAssetData textureData;
    std::filesystem::path texturePath;

    // Tech3D (tech-field) assets — TechnicalGrass ridge/valley heightfield as
    // surface-nets geometry, no atlas; the generator + shading ride along.
    bool tech3d = false;
    Tech3dAssetData tech;

    // Mask3D (mask-field) assets — the node-mask plate with a sloped skirt
    // and a PBR-lite material, no atlas; the generator + material + shading
    // ride along. materialPrefix resolves <materialSet> against the bundle
    // root (the maps sit at <prefix>_Color.jpg etc.).
    bool mask3d = false;
    Mask3dAssetData mask;
    std::filesystem::path materialPrefix;

    // Fence3D assets — fence brush with baked piece meshes, no atlas; meshDir
    // is the bundle root holding fence_{post,corner,section2,section3}.obj.
    bool fence3d = false;
    Fence3dAssetData fence;
    std::filesystem::path meshDir;

    // Building3D — GLB mesh instances on GameplayInteractive.
    bool building3d = false;
    Building3dAssetData building;
    std::filesystem::path modelPath;
    std::filesystem::path albedoPath;

    bool isSlice() const { return !atlasPath.empty(); }
    bool isImage() const { return !imagePath.empty(); }
    bool isShape3d() const { return shape3d; }
    bool isCliff3d() const { return cliff3d; }
    bool isCyclopean3d() const { return cyclopean3d; }
    bool isStone3d() const { return stone3d; }
    bool isTexture2d() const { return texture2d; }
    bool isTech3d() const { return tech3d; }
    bool isMask3d() const { return mask3d; }
    bool isFence3d() const { return fence3d; }
    bool isBuilding3d() const { return building3d; }
};

struct AssetIndex {
    std::unordered_map<std::string, AssetIndexEntry> byUuid;

    static AssetIndex load(const std::filesystem::path& assetsRoot);
    const AssetIndexEntry* find(const std::string& uuid) const;
};

} // namespace game_data

