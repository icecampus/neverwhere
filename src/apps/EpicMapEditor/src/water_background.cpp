#include "pch.h"
#include "water_background.h"

#include "render_core/depth_levels.h"

// GLSL 330 only: this background lives in the editor shell, which is always
// GLCORE (the game client has its own clear-color background).
//
// Infinite water: a fullscreen screen-space quad; the fragment shader
// reconstructs the world position from the camera (screen = world*zoom +
// offset) and evaluates the caustics there, so the pattern is exactly as
// world-anchored as before (camera flies over the water on pan/zoom) but
// covers any coordinate — no 20000x20000 quad edge.
static const char* vs_src_glsl = R"(
#version 330
layout(location=0) in vec2 pos;
out vec2 v_screen;
uniform vec2 view_size;
void main() {
    gl_Position = vec4(pos, 0.0, 1.0);
    // Clip space is GL y-up, but the camera convention (screen = world*zoom +
    // offset) is Qt top-left / y-down — the same convention the world passes
    // use (clip.y = 1.0 - (pos.y / view_size.y) * 2.0). Without the Y flip the
    // reconstructed world.y runs against the world content and the water
    // drifts opposite to the grid when panning along Y.
    v_screen = (vec2(pos.x, -pos.y) * 0.5 + 0.5) * view_size;
}
)";

// The caustics themselves, shared by the background and the surface pass.
// Exact port of the old CustomItem fragment shader (shaders/mandelbrot.frag,
// active branch — the mandelbrot part was commented out), with the uv now
// derived from the world position instead of the quad's uv0 (the same tiling
// the old 20000x20000 quad had: uv 0..1 over 20000 px).
static const char* caustics_glsl = R"(
vec3 waterCaustics(vec2 world, float t) {
    vec2 uv = world * (1.0 / 20000.0);

    // Background color (as in the original)
    vec3 base = vec3(21.0/255.0, 37.0/255.0, 43.0/255.0);

    vec4 k = vec4(t) * 0.8;
    k.xy = uv * 700.0;

    mat3 m1 = mat3(
        vec3(-2.0, -1.0, 0.0),
        vec3(3.0, -1.0, 1.0),
        vec3(1.0, -1.0, -1.0)
    ) * 0.5;

    mat3 m2 = mat3(
        vec3(-2.0, -1.0, 0.0),
        vec3(3.0, -1.0, 1.0),
        vec3(1.0, -1.0, -1.0)
    ) * 0.2;

    mat3 m3 = mat3(
        vec3(-2.0, -1.0, 0.0),
        vec3(3.0, -1.0, 1.0),
        vec3(1.0, -1.0, -1.0)
    ) * 0.5;

    float val1 = length(0.5 - fract(k.xyw *= m1));
    float val2 = length(0.5 - fract(k.xyw *= m2));
    float val3 = length(0.5 - fract(k.xyw *= m3));

    return vec3(pow(min(min(val1, val2), val3), 7.0) * 3.0) + base;
}
)";

static const char* fs_src_glsl = R"(
#version 330
in vec2 v_screen;
out vec4 frag_color;
uniform float iTime;
uniform vec2 camera_offset;
uniform float camera_zoom;
__CAUSTICS__
void main()
{
    // World position of this fragment (screen = world * zoom + offset).
    vec2 world = (v_screen - camera_offset) / camera_zoom;
    frag_color = vec4(waterCaustics(world, iTime), 1.0);
}
)";

// Water surface on the ground plane (y = 0, the grid level). Runs in its own
// pass after the world: samples the world depth buffer and blends the
// caustics over submerged geometry with alpha growing with the depth below
// the plane. See the class comment for the model.
static const char* surf_fs_src_glsl = R"(
#version 330
in vec2 v_screen;
out vec4 frag_color;
uniform float iTime;
uniform vec2 camera_offset;
uniform float camera_zoom;
uniform float z_far;
uniform float z_scale;
uniform float inv_z_per_level;
uniform float fade_depth;
uniform float max_alpha;
uniform sampler2D depth_tex;
__CAUSTICS__
void main()
{
    vec2 world = (v_screen - camera_offset) / camera_zoom;

    // Plane-0 z of this fragment in the shared depth-levels convention
    // (z = (zFar - groundY) * kZScale, nearer = smaller, LESS_EQUAL). The GL
    // depth buffer stores (clipZ + 1) / 2, so bring the plane z into that
    // stored space before comparing with the sampled depth.
    float z_water = ((z_far - world.y) * z_scale + 1.0) * 0.5;

    // Depth buffer of the world pass (GL texture row 0 = bottom = NDC -1, so
    // gl_FragCoord maps to it directly, no flip).
    float z_geom = texture(depth_tex, gl_FragCoord.xy / vec2(textureSize(depth_tex, 0))).r;

    // Anything closer than the plane covers the surface: raised geometry
    // (y > 0), and the grid lines (kGridZBias closer than the plane, so the
    // grid keeps reading as floating on the water).
    if (z_geom < z_water - 1e-6) discard;

    // Depth below the surface in levels. The stored-space difference is half
    // the clip-space one (hence the * 2.0), and the clip z metric counts y
    // twice (the screen lift moves the fragment onto a ground row that already
    // absorbed y*heightScale, and the depth term adds y*heightScale *
    // kDepthHeightFactor on top), so one level is (1 + kDepthHeightFactor) *
    // heightScale * kZScale of clip z — folded into inv_z_per_level on the CPU
    // side. Cleared pixels (open water) come out huge, i.e. maximally opaque —
    // and re-blend the same world-anchored pattern over itself, a no-op.
    float depth_below = max(z_geom - z_water, 0.0) * 2.0 * inv_z_per_level;
    float alpha = smoothstep(0.0, fade_depth, depth_below) * max_alpha;
    if (alpha <= 0.0) discard;

    frag_color = vec4(waterCaustics(world, iTime), alpha);
}
)";

namespace {

// The depth-levels metric lifts one level by heightScale (CliffParams default)
// in BOTH the screen shift and the depth term (kDepthHeightFactor == 1).
constexpr float kWaterHeightScale = 96.0f;

std::string withCaustics(const char* fsTemplate) {
    std::string src = fsTemplate;
    const std::string tag = "__CAUSTICS__";
    src.replace(src.find(tag), tag.size(), caustics_glsl);
    return src;
}

} // namespace

void WaterBackground::init(sg_pixel_format depthFormat_) {
    depthFormat = depthFormat_;
    ensurePipeline();
    ensureSurfacePipeline();

    // Static fullscreen quad, filled once (both passes share it — sokol allows
    // only one sg_update_buffer per buffer per frame).
    const Vertex verts[6] = {
        {{-1.0f, -1.0f}},
        {{ 1.0f, -1.0f}},
        {{-1.0f,  1.0f}},

        {{-1.0f,  1.0f}},
        {{ 1.0f, -1.0f}},
        {{ 1.0f,  1.0f}},
    };
    sg_buffer_desc buf_desc = {};
    buf_desc.size = sizeof(verts);
    buf_desc.data.ptr = verts;
    buf_desc.data.size = sizeof(verts);
    buf_desc.label = "water-verts";
    vbuf = sg_make_buffer(&buf_desc);

    sg_sampler_desc smp_desc = {};
    smp_desc.min_filter = SG_FILTER_NEAREST;
    smp_desc.mag_filter = SG_FILTER_NEAREST;
    smp_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    smp_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    smp_desc.label = "water-depth-smp";
    depthSampler = sg_make_sampler(&smp_desc);

    bind.vertex_buffers[0] = vbuf;
    surfBind.vertex_buffers[0] = vbuf;
    surfBind.samplers[0] = depthSampler;
}

void WaterBackground::shutdown() {
    if (vbuf.id != SG_INVALID_ID) {
        sg_destroy_buffer(vbuf);
        vbuf.id = SG_INVALID_ID;
    }
    if (depthSampler.id != SG_INVALID_ID) {
        sg_destroy_sampler(depthSampler);
        depthSampler.id = SG_INVALID_ID;
    }
    destroyPipeline();
}

void WaterBackground::ensurePipeline() {
    if (pip.id != SG_INVALID_ID) return;

    sg_shader_desc shd_desc = {};
    const std::string fs_src = withCaustics(fs_src_glsl);
    shd_desc.vertex_func.source = vs_src_glsl;
    shd_desc.fragment_func.source = fs_src.c_str();

    shd_desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd_desc.uniform_blocks[0].size = sizeof(float) * 2;
    shd_desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "view_size";
    shd_desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT2;

    // Fragment block: iTime + camera (offset/zoom) for the world reconstruction.
    shd_desc.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.uniform_blocks[1].size = sizeof(float) * 4;
    shd_desc.uniform_blocks[1].glsl_uniforms[0].glsl_name = "iTime";
    shd_desc.uniform_blocks[1].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.uniform_blocks[1].glsl_uniforms[1].glsl_name = "camera_offset";
    shd_desc.uniform_blocks[1].glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT2;
    shd_desc.uniform_blocks[1].glsl_uniforms[2].glsl_name = "camera_zoom";
    shd_desc.uniform_blocks[1].glsl_uniforms[2].type = SG_UNIFORMTYPE_FLOAT;

    sg_shader shd = sg_make_shader(&shd_desc);

    sg_pipeline_desc pip_desc = {};
    pip_desc.shader = shd;
    pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2; // pos (clip space)
    pip_desc.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    // The Qt FBO depth attachment is not wrapped into the pass (see MapRenderItem).
    pip_desc.depth.pixel_format = depthFormat;
    pip_desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
    pip_desc.depth.write_enabled = false;
    pip_desc.label = "water-pipeline";
    pip = sg_make_pipeline(&pip_desc);
}

void WaterBackground::ensureSurfacePipeline() {
    if (surfPip.id != SG_INVALID_ID) return;

    sg_shader_desc shd_desc = {};
    const std::string fs_src = withCaustics(surf_fs_src_glsl);
    shd_desc.vertex_func.source = vs_src_glsl;
    shd_desc.fragment_func.source = fs_src.c_str();

    shd_desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd_desc.uniform_blocks[0].size = sizeof(float) * 2;
    shd_desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "view_size";
    shd_desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT2;

    shd_desc.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.uniform_blocks[1].size = sizeof(float) * 9;
    shd_desc.uniform_blocks[1].glsl_uniforms[0].glsl_name = "iTime";
    shd_desc.uniform_blocks[1].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.uniform_blocks[1].glsl_uniforms[1].glsl_name = "camera_offset";
    shd_desc.uniform_blocks[1].glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT2;
    shd_desc.uniform_blocks[1].glsl_uniforms[2].glsl_name = "camera_zoom";
    shd_desc.uniform_blocks[1].glsl_uniforms[2].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.uniform_blocks[1].glsl_uniforms[3].glsl_name = "z_far";
    shd_desc.uniform_blocks[1].glsl_uniforms[3].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.uniform_blocks[1].glsl_uniforms[4].glsl_name = "z_scale";
    shd_desc.uniform_blocks[1].glsl_uniforms[4].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.uniform_blocks[1].glsl_uniforms[5].glsl_name = "inv_z_per_level";
    shd_desc.uniform_blocks[1].glsl_uniforms[5].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.uniform_blocks[1].glsl_uniforms[6].glsl_name = "fade_depth";
    shd_desc.uniform_blocks[1].glsl_uniforms[6].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.uniform_blocks[1].glsl_uniforms[7].glsl_name = "max_alpha";
    shd_desc.uniform_blocks[1].glsl_uniforms[7].type = SG_UNIFORMTYPE_FLOAT;

    shd_desc.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.views[0].texture.image_type = SG_IMAGETYPE_2D;
    // UNFILTERABLE_FLOAT + NEAREST: the depth24 image is not "filterable", so
    // IMAGESAMPLETYPE_FLOAT would fail validation; this pair skips the format
    // check and binds the depth texture plainly (GL returns raw depth in .r).
    shd_desc.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_UNFILTERABLE_FLOAT;
    shd_desc.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.samplers[0].sampler_type = SG_SAMPLERTYPE_NONFILTERING;
    shd_desc.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.texture_sampler_pairs[0].view_slot = 0;
    shd_desc.texture_sampler_pairs[0].sampler_slot = 0;
    shd_desc.texture_sampler_pairs[0].glsl_name = "depth_tex";

    sg_shader shd = sg_make_shader(&shd_desc);

    sg_pipeline_desc pip_desc = {};
    pip_desc.shader = shd;
    pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
    pip_desc.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    // Own pass without a depth attachment: the world depth comes in as a
    // texture and the compare happens in the shader.
    pip_desc.depth.pixel_format = SG_PIXELFORMAT_NONE;
    pip_desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
    pip_desc.depth.write_enabled = false;
    // Classic over, with explicit alpha factors (sokol defaults to
    // src=ONE/dst=ZERO for alpha, which would punch holes into the FBO alpha).
    pip_desc.colors[0].blend.enabled = true;
    pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    pip_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    pip_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    pip_desc.label = "water-surface-pipeline";
    surfPip = sg_make_pipeline(&pip_desc);
}

void WaterBackground::destroyPipeline() {
    if (pip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(pip);
        pip.id = SG_INVALID_ID;
    }
    if (surfPip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(surfPip);
        surfPip.id = SG_INVALID_ID;
    }
}

void WaterBackground::render(
    const topology_core::Camera2D& camera,
    int viewWidth,
    int viewHeight,
    float timeSeconds) {

    if (pip.id == SG_INVALID_ID || vbuf.id == SG_INVALID_ID) return;

    sg_apply_pipeline(pip);

    float vs_params[2] = {(float)viewWidth, (float)viewHeight};
    sg_range vs_range = { &vs_params, sizeof(vs_params) };
    sg_apply_uniforms(0, &vs_range);

    float fs_params[4] = {timeSeconds, camera.offset.x, camera.offset.y, camera.zoom};
    sg_range fs_range = { &fs_params, sizeof(fs_params) };
    sg_apply_uniforms(1, &fs_range);

    sg_apply_bindings(&bind);
    sg_draw(0, 6, 1);
}

void WaterBackground::renderSurface(
    const topology_core::Camera2D& camera,
    int viewWidth,
    int viewHeight,
    float timeSeconds,
    sg_view depthTexView) {

    if (surfPip.id == SG_INVALID_ID || vbuf.id == SG_INVALID_ID) return;
    if (depthTexView.id == SG_INVALID_ID) return;

    surfBind.views[0] = depthTexView;

    sg_apply_pipeline(surfPip);

    float vs_params[2] = {(float)viewWidth, (float)viewHeight};
    sg_range vs_range = { &vs_params, sizeof(vs_params) };
    sg_apply_uniforms(0, &vs_range);

    // Same per-frame anchor the world passes use (world_renderer.cpp).
    const float zFar = camera.screenToWorld({viewWidth * 0.5f, viewHeight * 0.5f}).y
                       + render_core::kZFarOffset;
    const float invZPerLevel =
        1.0f / ((1.0f + render_core::kDepthHeightFactor) * kWaterHeightScale * render_core::kZScale);

    float fs_params[9] = {
        timeSeconds, camera.offset.x, camera.offset.y, camera.zoom,
        zFar, render_core::kZScale, invZPerLevel, surfaceFadeDepth, surfaceMaxAlpha,
    };
    sg_range fs_range = { &fs_params, sizeof(fs_params) };
    sg_apply_uniforms(1, &fs_range);

    sg_apply_bindings(&surfBind);
    sg_draw(0, 6, 1);
}
