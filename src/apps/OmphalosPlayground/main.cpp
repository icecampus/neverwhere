// OmphalosPlayground — standalone sokol-плейграунд с raymarching-демкой
// "Omphalos" by dr2 (2019), https://www.shadertoy.com/view/ttXXDN
// License оригинального шейдера: Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported.
// Код шейдера портирован вручную на MSL (единственный поддерживаемый вариант), только для изучения.

#include "pch.h"

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

// Uniform-блок фрагментного шейдера (32 байта): resolution, time, mouse (аналог iMouse).
struct FsParams {
    float resolution[2];
    float timeSec;
    float pad0;
    float mouse[4]; // x, y (px, origin bottom-left), z = button held, w unused
};

struct AppState {
    std::uint64_t startTime = 0;
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    bool mouseDown = false;
    bool graphicsReady = false;
    bool pipelineOk = false;
    bool smokeMode = false;
    int smokeFrames = 0;
};

AppState g_state;
sg_pipeline g_pipeline{};

// ---------------------------------------------------------------------------
// MSL (ручной порт; mod -> gmod с GLSL-семантикой, swizzle e.xyy развёрнуты)
// ---------------------------------------------------------------------------

static const char* vs_src_msl = R"(
#include <metal_stdlib>
using namespace metal;

struct VSOut {
    float4 pos [[position]];
    float2 uv;
};

vertex VSOut _main(uint vid [[vertex_id]]) {
    VSOut o;
    float2 pos = float2(vid == 1 ? 3.0 : -1.0, vid == 2 ? 3.0 : -1.0);
    o.pos = float4(pos, 0.0, 1.0);
    o.uv = pos * 0.5 + 0.5;
    return o;
}
)";

static const char* fs_src_msl = R"(
#include <metal_stdlib>
using namespace metal;

struct FsParams {
    float2 resolution;
    float time_sec;
    float pad0;
    float4 mouse;
};

// Глобалы dr2-шейдера (ltDir/elAx/tCur/dstFar/idObj) — в MSL program-scope
// переменные должны быть constant, поэтому состояние живёт в контексте.
struct SceneCtx {
    float3 ltDir;
    float3 elAx;
    float tCur;
    float dstFar;
    int idObj;
};

// GLSL-семантика mod (floor-based), у metal::fmod она trunc-based.
float gmod(float x, float y) { return x - y * floor(x / y); }
float2 gmod(float2 x, float y) { return x - y * floor(x / y); }

float PrBoxDf(float3 p, float3 b);
float PrEllipsDf(float3 p, float3 r);
float SmoothMin(float a, float b, float r);
float SmoothMax(float a, float b, float r);
float2 Rot2D(float2 q, float a);
float Fbm2(float2 p);
float Fbm3(float3 p);
float3 VaryNf(float3 p, float3 n, float f);

constant float pi = 3.14159;

#define DMIN(ctx, id) if (d < dMin) { dMin = d;  ctx.idObj = id; }

float ObjDf(thread SceneCtx& ctx, float3 p)
{
  float3 q;
  float dMin, d;
  dMin = ctx.dstFar;
  q = p;
  d = PrEllipsDf(q, ctx.elAx + float3(0.05, 0., 0.05));
  q = p;
  q.xy = Rot2D(q.xy, pi / 5.);
  d = SmoothMax(d, abs(gmod(q.y + 0.1, 0.4) - 0.2) - 0.1, 0.02);
  q = p;
  q.xz = Rot2D(q.xz, 2.1 * pi / 3.);
  q.xy = Rot2D(q.xy, 0.9 * pi / 5.);
  d = SmoothMax(d, abs(gmod(q.y + 0.1, 0.4) - 0.2) - 0.1, 0.02);
  q = p;
  q.xz = Rot2D(q.xz, -2.05 * pi / 3.);
  q.xy = Rot2D(q.xy, 1.1 * pi / 5.);
  d = SmoothMax(d, abs(gmod(q.y + 0.1, 0.4) - 0.2) - 0.1, 0.02);
  q = p;
  d = SmoothMin(d, PrEllipsDf(q, ctx.elAx), 0.1);
  d = max(d, -p.y);
  DMIN(ctx, 1);
  q = p;
  q -= float3(2.1, -0.05, 0.);
  d = PrBoxDf(q, float3(3.4, 0.05, 1.3));
  DMIN(ctx, 2);
  q = p;
  q -= float3(4.2, 1., 0.);
  d = PrBoxDf(q, float3(1., 1., 1.));
  // Спиральные борозды как у омфалоса, но в локальных координатах куба;
  // SmoothMin с внутренним кубом — аналог внутреннего эллипсоида: борозды
  // режут только оболочку, ядро остаётся целым.
  float3 qc;
  qc = q;
  qc.xy = Rot2D(qc.xy, pi / 5.);
  d = SmoothMax(d, abs(gmod(qc.y + 0.1, 0.4) - 0.2) - 0.1, 0.02);
  qc = q;
  qc.xz = Rot2D(qc.xz, 2.1 * pi / 3.);
  qc.xy = Rot2D(qc.xy, 0.9 * pi / 5.);
  d = SmoothMax(d, abs(gmod(qc.y + 0.1, 0.4) - 0.2) - 0.1, 0.02);
  qc = q;
  qc.xz = Rot2D(qc.xz, -2.05 * pi / 3.);
  qc.xy = Rot2D(qc.xy, 1.1 * pi / 5.);
  d = SmoothMax(d, abs(gmod(qc.y + 0.1, 0.4) - 0.2) - 0.1, 0.02);
  d = SmoothMin(d, PrBoxDf(q, float3(0.82, 0.82, 0.82)), 0.1);
  DMIN(ctx, 3);
  return dMin;
}

float ObjRay(thread SceneCtx& ctx, float3 ro, float3 rd)
{
  float dHit, d;
  dHit = 0.;
  for (int j = 0; j < 120; j++) {
    d = ObjDf(ctx, ro + dHit * rd);
    if (d < 0.0005 || dHit > ctx.dstFar) break;
    dHit += d;
  }
  return dHit;
}

float3 ObjNf(thread SceneCtx& ctx, float3 p)
{
  float4 v;
  float2 e = float2(0.0002, -0.0002);
  v = float4(-ObjDf(ctx, p + e.xxx), ObjDf(ctx, p + e.xyy), ObjDf(ctx, p + e.yxy), ObjDf(ctx, p + e.yyx));
  return normalize(2. * v.yzw - dot(v, float4(1.)));
}

float ObjSShadow(thread SceneCtx& ctx, float3 ro, float3 rd)
{
  float sh, d, h;
  sh = 1.;
  d = 0.02;
  for (int j = 0; j < 30; j++) {
    h = ObjDf(ctx, ro + d * rd);
    sh = min(sh, smoothstep(0., 0.01 * d, h));
    d += 0.02;
    if (sh < 0.05) break;
  }
  return 0.5 + 0.5 * sh;
}

float3 ShowScene(thread SceneCtx& ctx, float3 ro, float3 rd)
{
  float4 col4;
  float3 col, vn;
  float dstObj, sh, f;
  ctx.elAx = float3(1., 3., 1.);
  dstObj = ObjRay(ctx, ro, rd);
  if (dstObj < ctx.dstFar) {
    ro += dstObj * rd;
    vn = ObjNf(ctx, ro);
    if (ctx.idObj == 1) {
      vn = VaryNf(16. * ro, vn, 4.);
      f = Fbm3(32. * ro);
      col4 = mix(float4(0.4, 0.4, 0.45, 0.05), float4(0.6, 0.5, 0.5, 0.3) +
         float4(1., 1., 0.5, 0.5) * step(0.8, f),
         smoothstep(1.005, 1.01, length(ro / ctx.elAx))) * (1. - 0.3 * f);
    } else if (ctx.idObj == 3) {
      // Тот же камень, но градиент от локального центра куба:
      // дно борозд (внутренний куб, ~0.82-0.9) -> тёмно-серое,
      // грани/рёбра (>=0.95) -> золотистые.
      vn = VaryNf(16. * ro, vn, 4.);
      f = Fbm3(32. * ro);
      col4 = mix(float4(0.4, 0.4, 0.45, 0.05), float4(0.6, 0.5, 0.5, 0.3) +
         float4(1., 1., 0.5, 0.5) * step(0.8, f),
         smoothstep(0.87, 0.95, length(ro - float3(4.2, 1., 0.)))) * (1. - 0.3 * f);
    } else if (ctx.idObj == 2) {
      col4 = mix(float4(0.6, 0.7, 0.6, 0.2), float4(0.65, 0.6, 0.6, 0.2),
         smoothstep(0.4, 0.6, Fbm2(2. * ro.xz))) * (0.5 +
         0.5 * smoothstep(1., 1.1, length(ro.xz))) * (0.5 + 0.5 * step(0.99, vn.y));
    }
    sh = ObjSShadow(ctx, ro, ctx.ltDir);
    col = col4.rgb * (0.2 + 0.1 * max(-dot(vn, ctx.ltDir), 0.) +
       0.8 * sh * max(dot(vn, ctx.ltDir), 0.)) +
       col4.a * step(0.95, sh) * pow(max(dot(normalize(ctx.ltDir - rd), vn), 0.), 16.);
  } else {
    col = float3(0.05, 0.05, 0.08);
  }
  return clamp(col, 0., 1.);
}

#define AA  1   // optional antialiasing

float4 mainImage(thread SceneCtx& ctx, float2 fragCoord, constant FsParams& fs)
{
  float3x3 vuMat;
  float4 mPtr;
  float3 ro, rd, col;
  float2 canvas, uv, ori, ca, sa;
  float el, az, zmFac, sr;
  canvas = fs.resolution;
  uv = 2. * fragCoord / canvas - 1.;
  uv.x *= canvas.x / canvas.y;
  ctx.tCur = fs.time_sec;
  mPtr = fs.mouse;
  mPtr.xy = mPtr.xy / canvas - 0.5;
  az = 0.;
  el = -0.1 * pi;
  if (mPtr.z > 0.) {
    az += 2. * pi * mPtr.x;
    el += pi * mPtr.y;
  } else {
    az -= 0.03 * pi * ctx.tCur;
    el -= 0.05 * pi * sin(0.02 * pi * ctx.tCur);
  }
  el = clamp(el, -0.4 * pi, 0.01 * pi);
  ori = float2(el, az);
  ca = cos(ori);
  sa = sin(ori);
  vuMat = float3x3(ca.y, 0., -sa.y, 0., 1., 0., sa.y, 0., ca.y) *
          float3x3(1., 0., 0., 0., ca.x, -sa.x, 0., sa.x, ca.x);
  ro = vuMat * float3(0., 1.2, -13.);
  zmFac = 6.;
  ctx.dstFar = 40.;
  ctx.ltDir = vuMat * normalize(float3(1., 1., -1.));
#if ! AA
  const float naa = 1.;
#else
  const float naa = 3.;
#endif
  col = float3(0.);
  sr = 2. * gmod(dot(gmod(floor(0.5 * (uv + 1.) * canvas), 2.), float2(1.)), 2.) - 1.;
  for (float a = 0.; a < naa; a++) {
    rd = vuMat * normalize(float3(uv + step(1.5, naa) * Rot2D(float2(0.5 / canvas.y, 0.),
       sr * (0.667 * a + 0.5) * pi), zmFac));
    col += (1. / naa) * ShowScene(ctx, ro, rd);
  }
  return float4(pow(col, float3(0.8)), 1.);
}

float PrBoxDf(float3 p, float3 b)
{
  float3 d;
  d = abs(p) - b;
  return min(max(d.x, max(d.y, d.z)), 0.) + length(max(d, float3(0.)));
}

float PrEllipsDf(float3 p, float3 r)
{
  return (length(p / r) - 1.) * min(r.x, min(r.y, r.z));
}

float SmoothMin(float a, float b, float r)
{
  float h;
  h = clamp(0.5 + 0.5 * (b - a) / r, 0., 1.);
  return mix(b, a, h) - r * h * (1. - h);
}

float SmoothMax(float a, float b, float r)
{
  return -SmoothMin(-a, -b, r);
}

float2 Rot2D(float2 q, float a)
{
  float2 cs;
  cs = sin(a + float2(0.5 * pi, 0.));
  return float2(dot(q, float2(cs.x, -cs.y)), dot(q.yx, cs));
}

constant float cHashM = 43758.54;

float2 Hashv2v2(float2 p)
{
  float2 cHashVA2 = float2(37., 39.);
  return fract(sin(float2(dot(p, cHashVA2), dot(p + float2(1., 0.), cHashVA2))) * cHashM);
}

float4 Hashv4v3(float3 p)
{
  float3 cHashVA3 = float3(37., 39., 41.);
  return fract(sin(float4(dot(p, cHashVA3), dot(p + float3(1., 0., 0.), cHashVA3),
     dot(p + float3(0., 1., 0.), cHashVA3), dot(p + float3(0., 0., 1.), cHashVA3))) * cHashM);
}

float Noisefv2(float2 p)
{
  float2 t, ip, fp;
  ip = floor(p);
  fp = fract(p);
  fp = fp * fp * (3. - 2. * fp);
  t = mix(Hashv2v2(ip), Hashv2v2(ip + float2(0., 1.)), fp.y);
  return mix(t.x, t.y, fp.x);
}

float Noisefv3(float3 p)
{
  float4 t;
  float3 ip, fp;
  ip = floor(p);
  fp = fract(p);
  fp *= fp * (3. - 2. * fp);
  t = mix(Hashv4v3(ip), Hashv4v3(ip + float3(0., 0., 1.)), fp.z);
  return mix(mix(t.x, t.y, fp.x), mix(t.z, t.w, fp.x), fp.y);
}

float Fbm2(float2 p)
{
  float f, a;
  f = 0.;
  a = 1.;
  for (int j = 0; j < 5; j++) {
    f += a * Noisefv2(p);
    a *= 0.5;
    p *= 2.;
  }
  return f * (1. / 1.9375);
}

float Fbm3(float3 p)
{
  float f, a;
  f = 0.;
  a = 1.;
  for (int i = 0; i < 5; i++) {
    f += a * Noisefv3(p);
    a *= 0.5;
    p *= 2.;
  }
  return f * (1. / 1.9375);
}

float Fbmn(float3 p, float3 n)
{
  float3 s;
  float a;
  s = float3(0.);
  a = 1.;
  for (int j = 0; j < 5; j++) {
    s += a * float3(Noisefv2(p.yz), Noisefv2(p.zx), Noisefv2(p.xy));
    a *= 0.5;
    p *= 2.;
  }
  return dot(s, abs(n));
}

float3 VaryNf(float3 p, float3 n, float f)
{
  float3 g;
  g = float3(Fbmn(p + float3(0.1, 0., 0.), n), Fbmn(p + float3(0., 0.1, 0.), n),
     Fbmn(p + float3(0., 0., 0.1), n)) - Fbmn(p, n);
  return normalize(n + f * (g - n * dot(n, g)));
}

struct VSOut {
    float4 pos [[position]];
    float2 uv;
};

fragment float4 _main(VSOut in [[stage_in]], constant FsParams& fs [[buffer(0)]]) {
    SceneCtx ctx;
    return mainImage(ctx, in.uv * fs.resolution, fs);
}
)";

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
    spdlog::info("OmphalosPlayground: init (smoke={})", g_state.smokeMode);
    stm_setup();
    g_state.startTime = stm_now();

    sg_desc graphicsDescription{};
    graphicsDescription.environment = sglue_environment();
    graphicsDescription.logger.func = slog_func;
    sg_setup(&graphicsDescription);
    g_state.graphicsReady = sg_isvalid();
    if (!g_state.graphicsReady) {
        spdlog::error("TEST FAIL: sg_setup failed");
        return;
    }

    // Только Metal (macOS): GLSL/HLSL варианты намеренно не поддерживаем.
    sg_shader_desc shdDesc{};
    shdDesc.vertex_func.source = vs_src_msl;
    shdDesc.fragment_func.source = fs_src_msl;
    shdDesc.uniform_blocks[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shdDesc.uniform_blocks[0].size = sizeof(FsParams);
    shdDesc.uniform_blocks[0].msl_buffer_n = 0;
    shdDesc.label = "omphalos-shader";

    sg_shader shader = sg_make_shader(&shdDesc);

    sg_pipeline_desc pipDesc{};
    pipDesc.shader = shader;
    pipDesc.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    pipDesc.label = "omphalos-pipeline";
    g_pipeline = sg_make_pipeline(&pipDesc);

    g_state.pipelineOk = sg_query_pipeline_state(g_pipeline) == SG_RESOURCESTATE_VALID;
    if (g_state.pipelineOk) {
        spdlog::info("TEST PASS: omphalos pipeline created (backend: {})", backendName());
    } else {
        spdlog::error("TEST FAIL: omphalos pipeline invalid (backend: {})", backendName());
    }
}

void frame() {
    if (!g_state.graphicsReady || !g_state.pipelineOk) {
        if (g_state.smokeMode) {
            sapp_quit();
        }
        return;
    }

    FsParams params{};
    params.resolution[0] = sapp_widthf();
    params.resolution[1] = sapp_heightf();
    params.timeSec = (float)stm_sec(stm_since(g_state.startTime));
    params.mouse[0] = g_state.mouseX;
    params.mouse[1] = params.resolution[1] - g_state.mouseY; // shadertoy origin — bottom-left
    params.mouse[2] = g_state.mouseDown ? 1.0f : 0.0f;
    params.mouse[3] = 0.0f;

    sg_pass_action action{};
    action.colors[0].load_action = SG_LOADACTION_CLEAR;
    action.colors[0].clear_value = {0.05f, 0.05f, 0.08f, 1.0f};
    sg_pass pass{};
    pass.action = action;
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);
    sg_apply_pipeline(g_pipeline);
    sg_apply_uniforms(0, SG_RANGE(params));
    sg_draw(0, 3, 1);
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
    spdlog::info("OmphalosPlayground: cleanup");
    if (g_pipeline.id != SG_INVALID_ID) {
        sg_destroy_pipeline(g_pipeline);
        g_pipeline.id = SG_INVALID_ID;
    }
    if (sg_isvalid()) {
        sg_shutdown();
    }
}

void event(const sapp_event* ev) {
    switch (ev->type) {
        case SAPP_EVENTTYPE_MOUSE_DOWN:
            if (ev->mouse_button == SAPP_MOUSEBUTTON_LEFT) {
                g_state.mouseDown = true;
                g_state.mouseX = ev->mouse_x;
                g_state.mouseY = ev->mouse_y;
            }
            break;
        case SAPP_EVENTTYPE_MOUSE_UP:
            if (ev->mouse_button == SAPP_MOUSEBUTTON_LEFT) {
                g_state.mouseDown = false;
            }
            break;
        case SAPP_EVENTTYPE_MOUSE_MOVE:
            g_state.mouseX = ev->mouse_x;
            g_state.mouseY = ev->mouse_y;
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
    description.width = g_state.smokeMode ? 320 : 1280;
    description.height = g_state.smokeMode ? 240 : 720;
    description.sample_count = 1;
    description.high_dpi = true;
    description.window_title = "OmphalosPlayground - \"Omphalos\" by dr2 (CC BY-NC-SA 3.0)";
#if defined(_WIN32)
    description.win32.console_utf8 = true;
    description.win32.console_attach = true;
#endif
    description.logger.func = slog_func;
    sapp_run(&description);
    return (g_state.graphicsReady && g_state.pipelineOk) ? 0 : 1;
}
