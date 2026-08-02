#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include <glm/glm.hpp>

// CPU half of the ground/highground stitching: the contact-AO distance field
// plus the shared sun/tone uniform block. No sokol here on purpose — the
// headless gtest suite exercises the same code as the renderer. Ported from
// HighgroundWithEffects (SceneStitch/LandBrush/AtlasRenderer); the shadow map
// stays behind in the playground.

namespace render_core {

// Distance beyond which the contact AO is fully gone (world cells). Also the
// scale the R8 field is normalized by, so the shader multiplies the texel back
// by this constant.
inline constexpr float kAoMaxDistanceCells = 4.0f;

// --- Sub-cell shape of a partially filled cell -----------------------------
// A cell is a diamond with a node in each corner, and a half-lit cell is
// filled only on the side of the nodes that are on. Anything derived from the
// footprint (contact AO) has to use this rule too, otherwise it works with
// whole cell squares and stops following the silhouette that is actually on
// screen.

// Diamond coordinates: Left (-1, 0), Up (0, -1), Right (1, 0), Down (0, 1);
// the cell is the L1 ball |x| + |y| <= 1.
//
// Coverage in [0, 1]: the node flags weighted by how close the point is to
// each corner, >= 0.5 counts as solid. Constant along rays from the cell
// centre, so the fill is a wedge — exactly what the preview atlas rasterizes.
// nodeMask is in cellCornerNodes slot order [Left, Up, Right, Down].
float diamondNodeFill(const std::array<bool, 4>& nodeMask, glm::vec2 diamond);

// The world-space unit square of a cell rotated into diamond coordinates. uv
// is the position inside the square, and node (cx, cy) — the Up corner — sits
// at its origin, which is what puts the diamond axes on the square's diagonals.
inline glm::vec2 cellSquareToDiamond(glm::vec2 uv) {
    return {uv.x - uv.y, uv.x + uv.y - 1.0f};
}

// Distance from the highground footprint over the map bbox plus a margin.
// Texel 0 = at the footprint, 255 = kAoMaxDistanceCells or further away.
struct ContactAoField {
    int width = 0;
    int height = 0;
    float originX = 0.0f; // world x of the field's left edge (cell units)
    float originZ = 0.0f; // world z of the field's top edge
    float cellsPerTexel = 1.0f;
    std::vector<std::uint8_t> texels;

    bool empty() const { return texels.empty(); }
    float extentX() const { return static_cast<float>(width) * cellsPerTexel; }
    float extentZ() const { return static_cast<float>(height) * cellsPerTexel; }

    // Distance in cells at a world position (kAoMaxDistanceCells outside).
    float distanceAt(float worldX, float worldZ) const;
};

// Node-grid footprint of one highground layer: (nodesX - 1) x (nodesY - 1)
// cells with a vertex node at every corner (the vertex-centric landscape
// contract). This is what the editor's layer node grids plug into.
struct AoFootprint {
    int nodesX = 0;
    int nodesY = 0;
    std::function<bool(int, int)> nodeOn; // out-of-bounds reads count as off
};

// Union of the given footprints turned into a chamfer distance transform.
// Footprints without a nodeOn callback are skipped; an empty union yields an
// empty field (the caller falls back to "no AO").
void buildContactAoField(
    const AoFootprint* footprints,
    int count,
    int texelsPerCell,
    int marginCells,
    ContactAoField& out);

// Scalar-field height (the mesh py) -> world height in cell units.
//
// The iso projection is anisotropic in field space: one map unit spans
// (halfW, halfH) screen px while one height unit spans heightScale px. This is
// the y scale that makes the two projection rows orthogonal AND equal length,
// i.e. the height a viewer actually reads off the screen.
float isoHeightToWorld(float halfW, float halfH, float heightScale);

// Scene-level stitching knobs owned by the app (UI state). The sun and the
// tone live here because BOTH passes need them: the ground used to be drawn
// as a raw texture while the highground was lit, which is what made the
// highground read as pasted on.
struct SceneStitchSettings {
    // Azimuth 3pi/4 puts the sun exactly across the iso diagonal: of the two
    // visible wall faces one is lit and one is not.
    float lightAzimuth = 2.356f;  // radians
    float lightElevation = 0.62f; // radians
    float ambient = 0.35f;
    float diffuse = 0.75f;
    float gamma = 0.85f;
    bool groundLit = true;
    // Contact AO: darkening of the ground around the highground footprint,
    // fading out over about a cell and a half. The outline is sub-cell (see
    // buildContactAoField) — with whole-cell coverage a band this wide traced
    // cell borders and read as a square blob rather than as the wall's shadow.
    bool aoEnabled = true;
    float aoStrength = 0.75f;
    float aoRadius = 1.4f; // cells
    // The same darkening climbing the wall itself (mesh units above the ground
    // plane). Without it the grass goes dark while the stone directly above it
    // stays lit, which draws the very seam the AO is there to hide.
    float aoWallFade = 0.35f;

    glm::vec3 sunDirection() const;
};

// Fragment-shader uniforms shared by the ground pass and the cliff pass. The
// cliff pass declares only the leading `kStitchCoreBytes` — it has no use for
// the AO rect, and a GL driver drops unused uniforms, which sokol then reports
// as a missing block member every startup.
struct SceneStitchParams {
    float sunDir[4];  // xyz: direction towards the sun
    float params0[4]; // ambient, diffuse, gamma, unused
    float params1[4]; // unused, unused, AO strength, AO radius
    float aoRect[4];  // xy: AO field origin (cells), zw: 1 / field extent
};

inline constexpr std::size_t kStitchCoreBytes = offsetof(SceneStitchParams, aoRect);

SceneStitchParams buildStitchParams(const SceneStitchSettings& settings, const ContactAoField& aoField);

// Seam materials of the cliff pass: how the wall meets the ground and how the
// plateau keeps its distance from the ground it shares the grass texture
// with. Uniform-only (no mesh rebuild). Defaults are the HighgroundWithEffects
// SeamParams; the grass skirt (skirtHeight/skirtFrequency/overgrowth) and the
// mesh sink are NOT ported at this stage, and the bounce band is the
// playground's default skirt height x4 (0.56 scalar-field units).
//
// The strength channels (rimContactAo, bounceStrength, skyStrength — plus
// SceneStitchSettings::aoWallFade carried alongside) gate in the shader: 0
// turns the term off bit-exactly. topBrightness/topTint are LEVELS, not
// strengths — their neutral value is 1 / white, not 0.
struct SeamParams {
    // Baked wall-proximity weight darkens the plateau edge (stone layers
    // carry the rim attribute; plain cliffs have 0 and stay untouched).
    float rimContactAo = 0.45f;
    // The plateau shares the ground texture; lift its tone so the two do not
    // read as one continuous plane (1 = neutral).
    float topBrightness = 1.12f;
    // Grass bounce from below, cool sky on the upward faces: the stone and
    // the ground palettes need something in common to share a scene.
    float bounceStrength = 0.35f;
    float bounceTint[3] = {0.72f, 0.95f, 0.62f};
    float skyStrength = 0.2f;
    float skyTint[3] = {0.80f, 0.88f, 1.10f};
    // Plateau top material: without its own tint and UV rotation the top
    // continues the ground and the silhouette flips between a mound and a pit.
    float topRotation = 0.6f; // radians
    float topTint[3] = {0.86f, 0.94f, 0.80f};
};

} // namespace render_core
