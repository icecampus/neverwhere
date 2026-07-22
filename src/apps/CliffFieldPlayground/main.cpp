// CliffFieldPlayground — standalone sokol prototype of a cliff/highground mesh:
// scalar field (blurred height nodes + omphalos-style grooves) -> naive surface
// nets -> watertight mesh with vertex-baked groove depth. Metal/MSL only.
// Groove idea and stone palette follow "Omphalos" by dr2 (CC BY-NC-SA 3.0),
// https://www.shadertoy.com/view/ttXXDN — for study only.

#include "pch.h"

#include "CliffField.h"
#include "MiniMath.h"
#include "SurfaceNets.h"

#define SOKOL_IMPL
#define SOKOL_NO_ENTRY

#if !defined(SOKOL_D3D11) && !defined(SOKOL_METAL) && !defined(SOKOL_GLES3) && !defined(SOKOL_GLCORE)
    #if defined(_WIN32)
        #define SOKOL_D3D11
    #elif defined(__APPLE__)
        #define SOKOL_METAL
    #elif defined(__EMSCRIPTEN__)
        #define SOKOL_GLES3
    #else
        #define SOKOL_GLCORE
    #endif
#endif

#include <spdlog/spdlog.h>

#include <sokol_app.h>
#include <sokol_gfx.h>
#include <sokol_glue.h>
#include <sokol_log.h>
#include <sokol_time.h>

namespace {

struct VsParams {
    float mvp[16];
};

struct FsParams {
    float lightDir[4]; // xyz: direction towards the sun
    float camPos[4];   // xyz: camera world position
};

struct AppState {
    std::uint64_t startTime = 0;
    bool graphicsReady = false;
    bool pipelineOk = false;
    bool smokeMode = false;
    bool scenarioOk = true;
    int smokeFrames = 0;
    // Orbit camera.
    float azimuth = 0.7f;
    float elevation = 0.55f;
    float distance = 6.5f;
    bool dragging = false;
    bool userOrbited = false; // first drag disables the auto-orbit
    float lastMouseX = 0.0f;
    float lastMouseY = 0.0f;
    int indexCount = 0;
};

AppState g_state;
sg_pipeline g_pipeline{};
sg_bindings g_bindings{};
cliff::Mesh g_mesh;

// ---------------------------------------------------------------------------
// MSL shaders (Metal is the only backend for this prototype)
// ---------------------------------------------------------------------------

static const char* vs_src_msl = R"(
#include <metal_stdlib>
using namespace metal;

struct VsParams {
    float4x4 mvp;
};

struct VsIn {
    float3 pos [[attribute(0)]];
    float3 normal [[attribute(1)]];
    float groove [[attribute(2)]];
};

struct VsOut {
    float4 pos [[position]];
    float3 world_pos;
    float3 normal;
    float groove;
};

vertex VsOut _main(VsIn in [[stage_in]], constant VsParams& vs [[buffer(0)]]) {
    VsOut o;
    o.pos = vs.mvp * float4(in.pos, 1.0);
    o.world_pos = in.pos;
    o.normal = in.normal;
    o.groove = in.groove;
    return o;
}
)";

static const char* fs_src_msl = R"(
#include <metal_stdlib>
using namespace metal;

struct FsParams {
    float4 light_dir;
    float4 cam_pos;
};

float4 Hashv4v3(float3 p) {
    float3 cHashVA3 = float3(37.0, 39.0, 41.0);
    return fract(sin(float4(dot(p, cHashVA3), dot(p + float3(1.0, 0.0, 0.0), cHashVA3),
        dot(p + float3(0.0, 1.0, 0.0), cHashVA3), dot(p + float3(0.0, 0.0, 1.0), cHashVA3))) * 43758.54);
}

float Noisefv3(float3 p) {
    float4 t;
    float3 ip = floor(p);
    float3 fp = fract(p);
    fp *= fp * (3.0 - 2.0 * fp);
    t = mix(Hashv4v3(ip), Hashv4v3(ip + float3(0.0, 0.0, 1.0)), fp.z);
    return mix(mix(t.x, t.y, fp.x), mix(t.z, t.w, fp.x), fp.y);
}

float Fbm3(float3 p) {
    float f = 0.0;
    float a = 1.0;
    for (int i = 0; i < 5; i++) {
        f += a * Noisefv3(p);
        a *= 0.5;
        p *= 2.0;
    }
    return f * (1.0 / 1.9375);
}

float2 Hashv2v2(float2 p) {
    float2 cHashVA2 = float2(37.0, 39.0);
    return fract(sin(float2(dot(p, cHashVA2), dot(p + float2(1.0, 0.0), cHashVA2))) * 43758.54);
}

float Noisefv2(float2 p) {
    float2 ip = floor(p);
    float2 fp = fract(p);
    fp = fp * fp * (3.0 - 2.0 * fp);
    float2 t = mix(Hashv2v2(ip), Hashv2v2(ip + float2(0.0, 1.0)), fp.y);
    return mix(t.x, t.y, fp.x);
}

float Fbm2(float2 p) {
    float f = 0.0;
    float a = 1.0;
    for (int j = 0; j < 5; j++) {
        f += a * Noisefv2(p);
        a *= 0.5;
        p *= 2.0;
    }
    return f * (1.0 / 1.9375);
}

struct VsOut {
    float4 pos [[position]];
    float3 world_pos;
    float3 normal;
    float groove;
};

fragment float4 _main(VsOut in [[stage_in]], constant FsParams& fs [[buffer(0)]]) {
    float3 n = normalize(in.normal);
    float3 p = in.world_pos;
    // Omphalos stone palette: dark grey at groove floors, gold + veins on the shell.
    float f = Fbm3(32.0 * p);
    float shell = 1.0 - smoothstep(0.005, 0.05, in.groove);
    float3 gold = float3(0.75, 0.62, 0.5) + float3(1.0, 0.9, 0.4) * step(0.8, f);
    float3 rock = mix(float3(0.38, 0.38, 0.42), gold, shell) * (1.0 - 0.3 * f);
    // Grassy flat tops (omphalos idObj==2 style).
    float gm = smoothstep(0.4, 0.6, Fbm2(2.0 * p.xz));
    float3 grass = mix(float3(0.4, 0.62, 0.35), float3(0.6, 0.65, 0.4), gm);
    float topMask = smoothstep(0.7, 0.9, n.y);
    float3 base = mix(rock, grass, topMask);
    // Cheap sun lambert + wrap ambient + spec.
    float3 l = normalize(fs.light_dir.xyz);
    float3 rd = normalize(p - fs.cam_pos.xyz);
    float ndl = dot(n, l);
    float3 col = base * (0.35 + 0.1 * max(-ndl, 0.0) + 0.75 * max(ndl, 0.0));
    float specAmt = mix(0.05, 0.5, shell) * (1.0 - topMask);
    col += specAmt * pow(max(dot(normalize(l - rd), n), 0.0), 24.0);
    return float4(pow(clamp(col, 0.0, 1.0), float3(0.85)), 1.0);
}
)";

// ---------------------------------------------------------------------------
// Scenario: field -> mesh -> watertight check (used by both GUI and --smoke)
// ---------------------------------------------------------------------------

bool runTestScenario() {
    using Clock = std::chrono::steady_clock;
    bool ok = true;

    cliff::FieldParams params;
    const auto t0 = Clock::now();
    cliff::CliffField field(params);
    std::vector<float> samples;
    field.sample(samples);
    const auto t1 = Clock::now();
    spdlog::info("TEST PASS: field sampled {}x{}x{} points ({} voxels) in {:.1f} ms",
        field.sizeX() + 1, field.sizeY() + 1, field.sizeZ() + 1,
        field.sizeX() * field.sizeY() * field.sizeZ(),
        std::chrono::duration<double, std::milli>(t1 - t0).count());

    // Resolve checkerboard (saddle) grid faces: without them the surface nets
    // mesh is manifold by construction.
    cliff::RegularizeStats regStats;
    cliff::regularizeSigns(field, samples, &regStats);
    if (regStats.remaining == 0) {
        spdlog::info("TEST PASS: sign regularization ({} saddle faces, {} flips, {} passes)",
            regStats.saddleFaces, regStats.flips, regStats.passes);
    } else {
        spdlog::error("TEST FAIL: sign regularization left {} saddle faces ({} flips, {} passes)",
            regStats.remaining, regStats.flips, regStats.passes);
        ok = false;
    }

    cliff::ExtractStats stats;
    g_mesh = cliff::extractSurfaceNets(field, samples, &stats);
    const auto t2 = Clock::now();

    // The padding must keep the field strictly positive on the whole grid
    // border, otherwise the solid is clipped and the mesh comes out open.
    {
        const int px = field.sizeX() + 1;
        const int py = field.sizeY() + 1;
        const int pz = field.sizeZ() + 1;
        auto val = [&](int x, int y, int z) -> float {
            return samples[(static_cast<size_t>(y) * pz + z) * px + x];
        };
        float borderMin = 1e9f;
        for (int y = 0; y < py; ++y) {
            for (int z = 0; z < pz; ++z) {
                for (int x = 0; x < px; ++x) {
                    if (x == 0 || y == 0 || z == 0 || x == px - 1 || y == py - 1 || z == pz - 1) {
                        borderMin = std::min(borderMin, val(x, y, z));
                    }
                }
            }
        }
        if (borderMin > 0.0f) {
            spdlog::info("TEST PASS: field positive on grid border (min {:.4f})", borderMin);
        } else {
            spdlog::error("TEST FAIL: field leaks onto grid border (min {:.4f})", borderMin);
            ok = false;
        }
    }

    const std::size_t triCount = g_mesh.indices.size() / 3;
    if (!g_mesh.vertices.empty() && triCount > 0) {
        spdlog::info("TEST PASS: mesh extracted ({} vertices, {} triangles, {} sign voxels) "
            "in {:.1f} ms (verts {:.1f}, quads {:.1f})",
            g_mesh.vertices.size(), triCount, stats.signVoxels,
            std::chrono::duration<double, std::milli>(t2 - t1).count(),
            stats.vertexMs, stats.quadMs);
    } else {
        spdlog::error("TEST FAIL: extracted mesh is empty");
        ok = false;
    }

    const cliff::WatertightReport report = cliff::checkWatertight(g_mesh);
    if (report.ok()) {
        spdlog::info("TEST PASS: mesh watertight ({} undirected edges, {} half-edges, "
            "{} degenerate triangles)",
            report.undirectedEdges, report.halfEdges, report.degenerateTriangles);
    } else {
        spdlog::error("TEST FAIL: mesh not watertight ({} bad of {} undirected edges: "
            "{} with 1 half-edge, {} with 3, {} with 4+; {} degenerate triangles)",
            report.badEdges, report.undirectedEdges, report.edgesWith1Half,
            report.edgesWith3Half, report.edgesWith4Plus, report.degenerateTriangles);
        cliff::debugDumpBadEdges(field, samples, g_mesh, 10);
        ok = false;
    }

    float gMin = 1e9f;
    float gMax = -1e9f;
    for (const cliff::MeshVertex& v : g_mesh.vertices) {
        gMin = std::min(gMin, v.groove);
        gMax = std::max(gMax, v.groove);
    }
    if (gMax > 0.02f) {
        spdlog::info("TEST PASS: groove attribute range [{:.4f}, {:.4f}]", gMin, gMax);
    } else {
        spdlog::error("TEST FAIL: groove attribute range [{:.4f}, {:.4f}] — no carve detected",
            gMin, gMax);
        ok = false;
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Sokol app
// ---------------------------------------------------------------------------

const char* backendName() {
    switch (sg_query_backend()) {
        case SG_BACKEND_METAL_MACOS: return "Metal";
        case SG_BACKEND_D3D11: return "D3D11";
        case SG_BACKEND_GLCORE: return "GLCore";
        case SG_BACKEND_GLES3: return "GLES3";
        default: return "unknown";
    }
}

void init() {
    spdlog::set_level(spdlog::level::info);
    spdlog::flush_on(spdlog::level::info);
    spdlog::info("CliffFieldPlayground: init (smoke={})", g_state.smokeMode);
    stm_setup();
    g_state.startTime = stm_now();

    g_state.scenarioOk = runTestScenario();
    g_state.indexCount = static_cast<int>(g_mesh.indices.size());

    sg_desc graphicsDescription{};
    graphicsDescription.environment = sglue_environment();
    graphicsDescription.logger.func = slog_func;
    sg_setup(&graphicsDescription);
    g_state.graphicsReady = sg_isvalid();
    if (!g_state.graphicsReady) {
        spdlog::error("TEST FAIL: sg_setup failed");
        return;
    }

    sg_buffer_desc vbufDesc{};
    vbufDesc.size = g_mesh.vertices.size() * sizeof(cliff::MeshVertex);
    vbufDesc.usage.vertex_buffer = true;
    vbufDesc.usage.immutable = true;
    vbufDesc.data.ptr = g_mesh.vertices.data();
    vbufDesc.data.size = vbufDesc.size;
    vbufDesc.label = "cliff-vertices";
    g_bindings.vertex_buffers[0] = sg_make_buffer(&vbufDesc);

    sg_buffer_desc ibufDesc{};
    ibufDesc.size = g_mesh.indices.size() * sizeof(std::uint32_t);
    ibufDesc.usage.index_buffer = true;
    ibufDesc.usage.immutable = true;
    ibufDesc.data.ptr = g_mesh.indices.data();
    ibufDesc.data.size = ibufDesc.size;
    ibufDesc.label = "cliff-indices";
    g_bindings.index_buffer = sg_make_buffer(&ibufDesc);

    // Metal only: GLSL/HLSL variants intentionally not provided.
    sg_shader_desc shdDesc{};
    shdDesc.vertex_func.source = vs_src_msl;
    shdDesc.fragment_func.source = fs_src_msl;
    shdDesc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shdDesc.uniform_blocks[0].size = sizeof(VsParams);
    shdDesc.uniform_blocks[0].msl_buffer_n = 0;
    shdDesc.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    shdDesc.uniform_blocks[1].size = sizeof(FsParams);
    shdDesc.uniform_blocks[1].msl_buffer_n = 0;
    shdDesc.label = "cliff-shader";
    sg_shader shader = sg_make_shader(&shdDesc);

    sg_pipeline_desc pipDesc{};
    pipDesc.shader = shader;
    pipDesc.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    pipDesc.index_type = SG_INDEXTYPE_UINT32;
    pipDesc.layout.buffers[0].stride = sizeof(cliff::MeshVertex);
    pipDesc.layout.attrs[0].buffer_index = 0;
    pipDesc.layout.attrs[0].offset = offsetof(cliff::MeshVertex, px);
    pipDesc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    pipDesc.layout.attrs[1].buffer_index = 0;
    pipDesc.layout.attrs[1].offset = offsetof(cliff::MeshVertex, nx);
    pipDesc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
    pipDesc.layout.attrs[2].buffer_index = 0;
    pipDesc.layout.attrs[2].offset = offsetof(cliff::MeshVertex, groove);
    pipDesc.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT;
    pipDesc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    pipDesc.depth.write_enabled = true;
    pipDesc.cull_mode = SG_CULLMODE_BACK;
    pipDesc.face_winding = SG_FACEWINDING_CCW;
    pipDesc.label = "cliff-pipeline";
    g_pipeline = sg_make_pipeline(&pipDesc);

    g_state.pipelineOk = sg_query_pipeline_state(g_pipeline) == SG_RESOURCESTATE_VALID;
    if (g_state.pipelineOk) {
        spdlog::info("TEST PASS: cliff pipeline created (backend: {})", backendName());
    } else {
        spdlog::error("TEST FAIL: cliff pipeline invalid (backend: {})", backendName());
    }
}

void frame() {
    if (!g_state.graphicsReady || !g_state.pipelineOk) {
        if (g_state.smokeMode) {
            sapp_quit();
        }
        return;
    }

    const float t = static_cast<float>(stm_sec(stm_since(g_state.startTime)));
    if (!g_state.userOrbited) {
        // Auto-orbit (omphalos style): slow azimuth spin + elevation sway.
        g_state.azimuth = 0.7f + 0.25f * t;
        g_state.elevation = 0.55f + 0.25f * std::sin(0.11f * t);
    }

    const cfm::Vec3 target(3.0f, 0.45f, 3.0f);
    const float ce = std::cos(g_state.elevation);
    const cfm::Vec3 eye = target + cfm::Vec3(ce * std::sin(g_state.azimuth),
        std::sin(g_state.elevation), ce * std::cos(g_state.azimuth)) * g_state.distance;
    const float aspect = sapp_widthf() / sapp_heightf();
    const cfm::Mat4 view = cfm::Mat4::lookAt(eye, target, cfm::Vec3(0.0f, 1.0f, 0.0f));
    const cfm::Mat4 proj = cfm::Mat4::perspective(50.0f * 3.14159265f / 180.0f, aspect, 0.1f, 80.0f);
    const cfm::Mat4 mvp = proj * view;

    VsParams vsParams{};
    std::memcpy(vsParams.mvp, mvp.m, sizeof(vsParams.mvp));
    FsParams fsParams{};
    const cfm::Vec3 sun = cfm::normalize(cfm::Vec3(0.9f, 1.3f, -0.7f));
    fsParams.lightDir[0] = sun.x;
    fsParams.lightDir[1] = sun.y;
    fsParams.lightDir[2] = sun.z;
    fsParams.lightDir[3] = 0.0f;
    fsParams.camPos[0] = eye.x;
    fsParams.camPos[1] = eye.y;
    fsParams.camPos[2] = eye.z;
    fsParams.camPos[3] = 0.0f;

    sg_pass_action action{};
    action.colors[0].load_action = SG_LOADACTION_CLEAR;
    action.colors[0].clear_value = {0.05f, 0.05f, 0.08f, 1.0f};
    action.depth.load_action = SG_LOADACTION_CLEAR;
    action.depth.clear_value = 1.0f;
    sg_pass pass{};
    pass.action = action;
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);
    sg_apply_pipeline(g_pipeline);
    sg_apply_bindings(&g_bindings);
    sg_apply_uniforms(0, SG_RANGE(vsParams));
    sg_apply_uniforms(1, SG_RANGE(fsParams));
    sg_draw(0, g_state.indexCount, 1);
    sg_end_pass();
    sg_commit();

    if (g_state.smokeMode) {
        ++g_state.smokeFrames;
        if (g_state.smokeFrames == 30) {
            spdlog::info("TEST PASS: rendered {} frames", g_state.smokeFrames);
            spdlog::info("TEST PASS: smoke scenario finished OK");
            sapp_quit();
        }
    }
}

void cleanup() {
    spdlog::info("CliffFieldPlayground: cleanup");
    if (g_pipeline.id != SG_INVALID_ID) {
        sg_destroy_pipeline(g_pipeline);
        g_pipeline.id = SG_INVALID_ID;
    }
    if (g_bindings.vertex_buffers[0].id != SG_INVALID_ID) {
        sg_destroy_buffer(g_bindings.vertex_buffers[0]);
        g_bindings.vertex_buffers[0].id = SG_INVALID_ID;
    }
    if (g_bindings.index_buffer.id != SG_INVALID_ID) {
        sg_destroy_buffer(g_bindings.index_buffer);
        g_bindings.index_buffer.id = SG_INVALID_ID;
    }
    if (sg_isvalid()) {
        sg_shutdown();
    }
    if (g_state.smokeMode) {
        // On macOS sokol_app quits via [NSApp terminate], which exits the
        // process with 0 and main() never returns — so report the honest
        // smoke result from here instead.
        const bool framesOk = g_state.smokeFrames >= 30;
        const int code = (g_state.graphicsReady && g_state.pipelineOk &&
            g_state.scenarioOk && framesOk) ? 0 : 1;
        spdlog::info("CliffFieldPlayground: smoke exit code {}", code);
        std::exit(code);
    }
}

void event(const sapp_event* ev) {
    switch (ev->type) {
        case SAPP_EVENTTYPE_MOUSE_DOWN:
            if (ev->mouse_button == SAPP_MOUSEBUTTON_LEFT) {
                g_state.dragging = true;
                g_state.userOrbited = true;
                g_state.lastMouseX = ev->mouse_x;
                g_state.lastMouseY = ev->mouse_y;
            }
            break;
        case SAPP_EVENTTYPE_MOUSE_UP:
            if (ev->mouse_button == SAPP_MOUSEBUTTON_LEFT) {
                g_state.dragging = false;
            }
            break;
        case SAPP_EVENTTYPE_MOUSE_MOVE:
            if (g_state.dragging) {
                g_state.azimuth -= (ev->mouse_x - g_state.lastMouseX) * 0.008f;
                g_state.elevation = cfm::clamp(
                    g_state.elevation - (ev->mouse_y - g_state.lastMouseY) * 0.008f, 0.12f, 1.45f);
                g_state.lastMouseX = ev->mouse_x;
                g_state.lastMouseY = ev->mouse_y;
            }
            break;
        case SAPP_EVENTTYPE_MOUSE_SCROLL:
            g_state.distance = cfm::clamp(
                g_state.distance * std::pow(1.1f, -ev->scroll_y), 4.0f, 16.0f);
            break;
        default:
            break;
    }
}

} // namespace

int main(int argc, char* argv[]) {
    for (int index = 1; index < argc; ++index) {
        if (std::string_view(argv[index]) == "--smoke") {
            g_state.smokeMode = true;
        }
    }

    sapp_desc description{};
    description.init_cb = init;
    description.frame_cb = frame;
    description.cleanup_cb = cleanup;
    description.event_cb = event;
    description.width = g_state.smokeMode ? 640 : 1280;
    description.height = g_state.smokeMode ? 400 : 720;
    description.sample_count = 1;
    description.high_dpi = true;
    description.window_title = "CliffFieldPlayground - cliff field + surface nets";
#if defined(_WIN32)
    description.win32.console_utf8 = true;
    description.win32.console_attach = true;
#endif
    description.logger.func = slog_func;
    sapp_run(&description);
    return (g_state.graphicsReady && g_state.pipelineOk && g_state.scenarioOk) ? 0 : 1;
}
