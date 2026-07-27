// GLSL shim strings that turn a raw shadertoy pass (mainImage) into a full
// fragment shader: uniform declarations + channel samplers up front, a
// main() forwarding to mainImage at the end. Single GLSL source for every
// desktop backend (GLCORE, #version 330 — macOS gives 4.1, others 4.3).
#pragma once

namespace shadertoy {

// #version + shadertoy uniform/sampler declarations. Prepended to the
// optional Common.glsl + pass source.
const char* fragmentPreamble();

// Appended after the pass source: forwards to the demo's mainImage.
const char* fragmentEpilog();

// Shared fullscreen-triangle vertex shader (gl_VertexID, no vertex buffers).
const char* fullscreenVertexSource();

// Cube preview: samples the Image-pass texture on a rotating cube.
const char* cubeVertexSource();
const char* cubeFragmentSource();

// Fullscreen blit of the (possibly scaled) Image-pass texture to the
// swapchain: the VS emits a normalized uv (no uniforms needed).
const char* blitVertexSource();
const char* blitFragmentSource();

} // namespace shadertoy
