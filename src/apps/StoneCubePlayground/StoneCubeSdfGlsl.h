// GLSL source of the stone-cube generator (SDF + raymarch), principles from
// iq's "Voronoi - rocks" (docs/reference/shadertoy/Voronoi - rocks): 3D
// voronoi (F1, F2, cellId), rocks bulge inside cells by clamp(k*(F2-F1)),
// fbm detail + bump, AO. Single source, #version 330 (GLCORE everywhere).
#pragma once

namespace stonecube {

// Full raymarch fragment shader: uniform block (7 x vec4, order matches the
// glsl_uniforms table in StoneCubeScene.cpp), SDF, shading, orbit-camera ray
// generation, main().
const char* raymarchFragmentSource();

// Shared fullscreen-triangle vertex shader (gl_VertexID, no vertex buffers).
const char* fullscreenVertexSource();

// Blit of the (scaled) render target to the swapchain.
const char* blitVertexSource();
const char* blitFragmentSource();

} // namespace stonecube
