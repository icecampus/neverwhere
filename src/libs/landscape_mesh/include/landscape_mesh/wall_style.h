#pragma once

#include "landscape_mesh/landscape_mesh.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace landscape_mesh {

// Which part of the transition band a sample belongs to. The topology owns the
// vertex positions and seam contract; the style only decides how each part bends
// and is coloured. Crest = upper-surface band at the rim, Face = vertical wall,
// Foot/CornerCap = lower-surface fillers (currently shared, reserved here).
enum class WallPart : std::uint8_t {
    Crest,
    Face,
    Foot,
    CornerCap,
};

// One base (undisplaced) vertex handed to the style. worldPos is in mesh world
// units so position-driven fields stay coherent across neighbouring segments
// (shared vertices sample identically -> watertight).
struct WallStyleSample {
    Vec3 worldPos;
    Vec3 normal;        // outward, horizontal: displacement direction
    float heightT = 1.0f;   // 0 at the base, 1 at the top
    float edgeWeight = 1.0f; // 1 where the style is active, 0 on pinned/flat vertices
    bool fadeAtBottom = false;
    WallPart part = WallPart::Face;
};

// Style output per vertex. offset is the signed displacement along the normal
// BEFORE the shared anti-fold clamp; field is a [-1,1]-ish scalar reused for
// per-panel colour and relief.
struct WallShade {
    float offset = 0.0f;
    float field = 0.0f;
};

// Per-level context. Carries the (already clamped) build settings plus the level
// band so styles can seed/colour deterministically.
struct WallStyleContext {
    MeshBuildSettings settings;
    std::uint8_t level = 1;
    std::uint8_t lowerLevel = 0;
    std::uint8_t maxLevel = 1;
    landscape_core::LandscapeZone zone = landscape_core::LandscapeZone::Lowland;
    int seed = 0;
};

// Pluggable wall skin. Seam contract every style MUST honour: offset goes to 0 at
// the pinned edges (heightT -> 0 at the foot, and heightT -> 1 on the crest / when
// fadeAtBottom at the top) so the band stitches to the flat surfaces and to its
// neighbours. The shared composer applies the uniform anti-fold clamp on top.
class IWallStyle {
public:
    virtual ~IWallStyle() = default;

    // Build noise nodes / cache parameters once per level band.
    virtual void prepare(const WallStyleContext& ctx) = 0;

    // Batch-shade base vertices (lets styles batch-sample their fields).
    virtual void shade(const std::vector<WallStyleSample>& samples, std::vector<WallShade>& out) const = 0;

    // Colour for one panel of the given part. panelField is the averaged vertex field.
    virtual ColorRgba color(WallPart part, BoundarySide side, float heightT, float panelField) const = 0;

    // Reserved hook for future styles that ADD geometry (talus, stacked blocks, ...).
    virtual std::vector<MeshQuad> augment(const WallStyleContext& ctx) const {
        (void)ctx;
        return {};
    }
};

std::unique_ptr<IWallStyle> makeWallStyle(WallStyleId id);

} // namespace landscape_mesh
