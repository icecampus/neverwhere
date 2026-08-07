#pragma once

#include "render_core/sokol_config.h"
#include "topology_core/camera2d.h"

// Water-caustics background for the editor map view — the old QSG CustomItem
// shader (qml/Workspace/MapView.qml background) restored in the unified render
// path. Two passes:
//
// 1. renderBackground() — before the world, inside the world pass: fullscreen
//    screen-space quad; the fragment shader reconstructs the world position
//    from the camera, so the caustics stay world-anchored (camera flies over
//    the water on pan/zoom) and the coverage is infinite.
//
// 2. renderSurface() — after the world pass, into the same color attachment
//    but WITHOUT the depth attachment: the water surface sitting on the
//    ground plane (y = 0, the grid level). The fragment shader samples the
//    world depth buffer, recovers the visible geometry's depth below the
//    water plane from the shared z convention (render_core/depth_levels.h:
//    z = (zFar - (groundY + y*heightScale)) * kZScale, and the screen lift
//    makes the recovered value exactly proportional to y) and blends the
//    caustics over it with alpha growing with depth: land at the waterline
//    stays clear, deep submerged geometry drowns in opaque water. Fragments
//    where the depth buffer is CLOSER than the plane (raised geometry, and
//    the grid lines with their kGridZBias) are discarded, so the world above
//    the surface and the grid floating on it stay untouched. Open water
//    re-blends the same world-anchored pattern over itself — a no-op.
class WaterBackground {
public:
    void init(sg_pixel_format depthFormat);
    void shutdown();

    // Pass 1 (inside the world pass, before the world).
    void render(
        const topology_core::Camera2D& camera,
        int viewWidth,
        int viewHeight,
        float timeSeconds);

    // Pass 2 (own pass: color only, depth buffer of pass 1 as a texture).
    void renderSurface(
        const topology_core::Camera2D& camera,
        int viewWidth,
        int viewHeight,
        float timeSeconds,
        sg_view depthTexView);

    // Surface tuning (world units of the depth metric; 1 level = heightScale).
    float surfaceFadeDepth = 0.6f;  // depth at which the water turns opaque
    float surfaceMaxAlpha = 1.0f;

private:
    struct Vertex {
        float pos[2];
    };

    sg_pipeline pip{SG_INVALID_ID};
    sg_pipeline surfPip{SG_INVALID_ID};
    sg_buffer vbuf{SG_INVALID_ID};
    sg_sampler depthSampler{SG_INVALID_ID};
    sg_bindings bind{};
    sg_bindings surfBind{};
    sg_pixel_format depthFormat = SG_PIXELFORMAT_NONE;

    void ensurePipeline();
    void ensureSurfacePipeline();
    void destroyPipeline();
};
