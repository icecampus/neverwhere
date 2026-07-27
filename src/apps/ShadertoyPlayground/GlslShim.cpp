#include "pch.h"

#include "GlslShim.h"

namespace shadertoy {

const char* fragmentPreamble() {
    return R"GLSL(
#version 330

// Shadertoy uniforms (filled from the C++ side, std140 layout).
uniform vec3 iResolution;
uniform float iTime;
uniform float iTimeDelta;
uniform int iFrame;
uniform vec4 iMouse;
uniform vec4 iDate;
uniform vec4 iChannelResolution[4]; // vec4: std140 arrays only allow FLOAT4/INT4/MAT4

uniform sampler2D iChannel0;
uniform sampler2D iChannel1;
uniform sampler2D iChannel2;
uniform sampler2D iChannel3;

out vec4 fragColor;

// Apple's GLSL compiler reserves noise1..noise4 (legacy built-ins, removed
// from the spec in GLSL 1.30 but still reserved on macOS) and errors out on
// demo functions with those names. Rename them away via the preprocessor —
// shadertoy demos always define their own anyway.
#define noise1 shadertoy_noise1
#define noise2 shadertoy_noise2
#define noise3 shadertoy_noise3
#define noise4 shadertoy_noise4

)GLSL";
}

const char* fragmentEpilog() {
    return R"GLSL(

void main() {
    mainImage(fragColor, gl_FragCoord.xy);
}
)GLSL";
}

const char* fullscreenVertexSource() {
    return R"GLSL(
#version 330

void main() {
    vec2 pos = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);
    gl_Position = vec4(pos, 0.0, 1.0);
}
)GLSL";
}

const char* cubeVertexSource() {
    return R"GLSL(
#version 330

uniform mat4 mvp;

layout(location=0) in vec3 pos;
layout(location=1) in vec2 uv;

out vec2 v_uv;

void main() {
    v_uv = uv;
    gl_Position = mvp * vec4(pos, 1.0);
}
)GLSL";
}

const char* cubeFragmentSource() {
    return R"GLSL(
#version 330

uniform sampler2D demo_tex;

in vec2 v_uv;
out vec4 fragColor;

void main() {
    fragColor = vec4(texture(demo_tex, v_uv).rgb, 1.0);
}
)GLSL";
}

const char* blitVertexSource() {
    return R"GLSL(
#version 330

out vec2 v_uv;

void main() {
    vec2 pos = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);
    v_uv = pos * 0.5 + 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}
)GLSL";
}

const char* blitFragmentSource() {
    return R"GLSL(
#version 330

uniform sampler2D demo_tex;

in vec2 v_uv;
out vec4 fragColor;

void main() {
    fragColor = vec4(texture(demo_tex, v_uv).rgb, 1.0);
}
)GLSL";
}

} // namespace shadertoy
