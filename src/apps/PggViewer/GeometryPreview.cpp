#include "pch.h"

#include "GeometryPreview.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <spdlog/spdlog.h>
#include <sokol_app.h>  // sokol_imgui.h wants it first (declarations only; SOKOL_IMPL lives in main.cpp)
#include <util/sokol_imgui.h>

#include <pgg/src/eval/builtins.h>  // realizeInstances
#include <pgg/src/eval/sdf.h>       // meshFromSdfExtract

namespace {

// --- geometry conversion --------------------------------------------------------

std::string countsLabel(const pgg::Geo& g) {
    char buf[128];
    if (g.kind == pgg::GeoKind::Points || g.faceCount() == 0) {
        std::snprintf(buf, sizeof(buf), "points %zu pts", g.pointCount());
    } else {
        size_t tris = 0;
        for (size_t f = 0; f < g.faceCount(); ++f) {
            const int32_t n = (*g.faceOffsets)[f + 1] - (*g.faceOffsets)[f];
            if (n >= 3) tris += static_cast<size_t>(n - 2);
        }
        std::snprintf(buf, sizeof(buf), "mesh %zu pts, %zu tri", g.pointCount(), tris);
    }
    return buf;
}

void collectGroups(const pgg::Geo& g, std::vector<std::string>& out) {
    auto add = [&](const pgg::GroupSet* set, const char* domain) {
        if (!set) return;
        std::vector<std::string> names;
        for (const auto& [name, col] : set->columns) names.push_back(std::string(domain) + ":" + name);
        std::sort(names.begin(), names.end());
        out.insert(out.end(), names.begin(), names.end());
    };
    add(g.pointGroups.get(), "points");
    add(g.faceGroups.get(), "faces");
}

// Resolves "<domain>:<name>" to the group column (nullptr when absent).
pgg::ConstBoolColumnPtr groupColumn(const pgg::Geo& g, const std::string& key, pgg::Domain& outDomain) {
    const size_t colon = key.find(':');
    if (colon == std::string::npos) return nullptr;
    const std::string domain = key.substr(0, colon);
    const std::string name = key.substr(colon + 1);
    const pgg::GroupSet* set = nullptr;
    if (domain == "points") {
        set = g.pointGroups.get();
        outDomain = pgg::Domain::Points;
    } else if (domain == "faces") {
        set = g.faceGroups.get();
        outDomain = pgg::Domain::Faces;
    }
    return set ? set->find(name) : nullptr;
}

void extendBBox(PreviewGeometry& out) {
    if (out.vertices.empty()) return;
    out.bmin = out.bmax = out.vertices[0].pos;
    for (const PreviewVertex& v : out.vertices) {
        out.bmin = glm::min(out.bmin, v.pos);
        out.bmax = glm::max(out.bmax, v.pos);
    }
}

// Points as small octahedra (backend-agnostic: no point-size support on D3D11).
void appendPoints(const pgg::Geo& g, const pgg::ConstBoolColumnPtr& mask, PreviewGeometry& out) {
    glm::vec3 mn, mx;
    pgg::geoBBox(g, mn, mx);
    const float diag = glm::length(mx - mn);
    const float r = std::max(1e-3f, diag * 0.012f);
    static const glm::vec3 axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    // 8 faces of the octahedron: (±x, ±y, ±z) corner triples.
    static const int faces[8][3] = {{0, 2, 4}, {2, 1, 4}, {1, 3, 4}, {3, 0, 4},
                                    {2, 0, 5}, {1, 2, 5}, {3, 1, 5}, {0, 3, 5}};
    const std::vector<glm::vec3>& P = *g.positions;
    out.vertices.reserve(out.vertices.size() + P.size() * 24);
    out.indices.reserve(out.indices.size() + P.size() * 24);
    for (size_t i = 0; i < P.size(); ++i) {
        const float m = (mask && i < mask->size() && (*mask)[i]) ? 1.0f : 0.0f;
        for (const auto& f : faces) {
            const glm::vec3 a = P[i] + axes[f[0]] * r;
            const glm::vec3 b = P[i] + axes[f[1]] * r;
            const glm::vec3 c = P[i] + axes[f[2]] * r;
            const glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
            const uint32_t base = static_cast<uint32_t>(out.vertices.size());
            out.vertices.push_back({a, n, m});
            out.vertices.push_back({b, n, m});
            out.vertices.push_back({c, n, m});
            out.indices.insert(out.indices.end(), {base, base + 1, base + 2});
        }
    }
}

// compute_normals(mode = flat) writes faceted normals into a vec3 corner
// attribute "N"; nullptr when absent or malformed.
const std::vector<glm::vec3>* cornerNormals(const pgg::Geo& g) {
    const pgg::AttrSet* attrs = g.attrs(pgg::Domain::Corners);
    const pgg::AttrColumn* col = attrs ? attrs->find("N") : nullptr;
    if (!col) return nullptr;
    const auto* vec = std::get_if<std::shared_ptr<const std::vector<glm::vec3>>>(&col->data);
    if (!vec || !*vec || (*vec)->size() != g.cornerCount()) return nullptr;
    return vec->get();
}

void appendMesh(const pgg::Geo& g, const std::string& highlight, PreviewShading shading,
                PreviewGeometry& out) {
    pgg::Domain maskDomain = pgg::Domain::Points;
    const pgg::ConstBoolColumnPtr mask = groupColumn(g, highlight, maskDomain);
    const std::vector<glm::vec3>& P = *g.positions;
    const std::vector<int32_t>& CV = *g.cornerVerts;
    const std::vector<int32_t>& FO = *g.faceOffsets;

    const std::vector<glm::vec3>* cornerN =
        shading == PreviewShading::Auto ? cornerNormals(g) : nullptr;
    const bool smooth = !cornerN && shading != PreviewShading::Flat && g.normals &&
                        g.normals->size() == g.pointCount() &&
                        !(mask && maskDomain == pgg::Domain::Faces);

    // Mask of a face for the unwelded (corner / flat) paths.
    auto faceMask = [&](size_t f, int32_t begin, int32_t end) -> float {
        if (!mask) return 0.0f;
        if (maskDomain == pgg::Domain::Faces) return (f < mask->size() && (*mask)[f]) ? 1.0f : 0.0f;
        // Point group on an unwelded mesh: the face is lit when every corner is in.
        for (int32_t c = begin; c < end; ++c) {
            const size_t pi = static_cast<size_t>(CV[c]);
            if (!(pi < mask->size() && (*mask)[pi])) return 0.0f;
        }
        return 1.0f;
    };

    if (cornerN) {
        // Faceted normals per corner: one vertex per corner, fan-triangulated.
        for (size_t f = 0; f < g.faceCount(); ++f) {
            const int32_t begin = FO[f], end = FO[f + 1];
            if (end - begin < 3) continue;
            const float m = faceMask(f, begin, end);
            auto normalAt = [&](int32_t c) {
                const glm::vec3 n = (*cornerN)[static_cast<size_t>(c)];
                const float len = glm::length(n);
                return len > 1e-12f ? n / len : glm::normalize(pgg::faceNormal(g, f));
            };
            for (int32_t c = begin + 1; c + 1 < end; ++c) {
                const uint32_t base = static_cast<uint32_t>(out.vertices.size());
                out.vertices.push_back({P[CV[begin]], normalAt(begin), m});
                out.vertices.push_back({P[CV[c]], normalAt(c), m});
                out.vertices.push_back({P[CV[c + 1]], normalAt(c + 1), m});
                out.indices.insert(out.indices.end(), {base, base + 1, base + 2});
            }
        }
        return;
    }

    if (smooth) {
        // merge() zero-fills @N for operands that never had normals (e.g. a
        // realized instance mesh next to a box) — a zero normal shades black.
        // Fall back to accumulated face normals for those points.
        std::vector<glm::vec3> fixedNormals;
        const std::vector<glm::vec3>* N = g.normals.get();
        bool anyZero = false;
        for (const glm::vec3& n : *N)
            if (glm::dot(n, n) < 1e-12f) { anyZero = true; break; }
        if (anyZero) {
            fixedNormals.assign(P.size(), glm::vec3(0.0f));
            for (size_t f = 0; f < g.faceCount(); ++f) {
                const glm::vec3 fn = pgg::faceNormal(g, f);
                for (int32_t c = FO[f]; c < FO[f + 1]; ++c) fixedNormals[static_cast<size_t>(CV[c])] += fn;
            }
            for (size_t i = 0; i < P.size(); ++i) {
                const glm::vec3& n = (*N)[i];
                if (glm::dot(n, n) >= 1e-12f) {
                    fixedNormals[i] = n;
                } else {
                    const float len = glm::length(fixedNormals[i]);
                    fixedNormals[i] = len > 1e-12f ? fixedNormals[i] / len : glm::vec3(0, 1, 0);
                }
            }
            N = &fixedNormals;
        }
        out.vertices.reserve(P.size());
        for (size_t i = 0; i < P.size(); ++i) {
            const float m = (mask && i < mask->size() && (*mask)[i]) ? 1.0f : 0.0f;
            out.vertices.push_back({P[i], (*N)[i], m});
        }
        for (size_t f = 0; f < g.faceCount(); ++f) {
            const int32_t begin = FO[f], end = FO[f + 1];
            for (int32_t c = begin + 1; c + 1 < end; ++c)
                out.indices.insert(out.indices.end(), {static_cast<uint32_t>(CV[begin]),
                                                       static_cast<uint32_t>(CV[c]),
                                                       static_cast<uint32_t>(CV[c + 1])});
        }
        return;
    }
    // Flat: one vertex triple per triangle with the face normal.
    for (size_t f = 0; f < g.faceCount(); ++f) {
        const int32_t begin = FO[f], end = FO[f + 1];
        if (end - begin < 3) continue;
        glm::vec3 n = pgg::faceNormal(g, f);
        const float len = glm::length(n);
        n = len > 1e-12f ? n / len : glm::vec3(0, 1, 0);
        const float m = faceMask(f, begin, end);
        for (int32_t c = begin + 1; c + 1 < end; ++c) {
            const uint32_t base = static_cast<uint32_t>(out.vertices.size());
            out.vertices.push_back({P[CV[begin]], n, m});
            out.vertices.push_back({P[CV[c]], n, m});
            out.vertices.push_back({P[CV[c + 1]], n, m});
            out.indices.insert(out.indices.end(), {base, base + 1, base + 2});
        }
    }
}

}  // namespace

PreviewGeometry buildPreviewGeometry(const pgg::Value& value, const PreviewBuildOptions& opts) {
    PreviewGeometry out;
    const pgg::ScalarType base = pgg::valueBase(value);
    pgg::GeoPtr geo;
    std::string prefix;
    if (base == pgg::ScalarType::Geo) {
        geo = pgg::asGeo(value);
        if (geo && geo->kind == pgg::GeoKind::Instances) {
            const size_t anchors = geo->pointCount();
            geo = pgg::realizeInstances(*geo, opts.threads == 0 ? 1 : opts.threads);
            prefix = "instances " + std::to_string(anchors) + " anchors -> realized ";
        }
    } else if (base == pgg::ScalarType::Sdf) {
        pgg::SdfPtr sdf = pgg::asSdf(value);
        if (!sdf) {
            out.summary = "empty sdf";
            return out;
        }
        glm::vec3 mn, mx;
        sdf->conservativeBBox(mn, mx);
        const glm::vec3 ext = mx - mn;
        const float longest = std::max(ext.x, std::max(ext.y, ext.z));
        if (!(longest > 0.0f) || !std::isfinite(longest)) {
            out.summary = "sdf has no finite bbox; cannot mesh a preview";
            return out;
        }
        const float voxel = longest / static_cast<float>(std::max(8, opts.sdfResolution));
        pgg::MeshFromSdfResult res = pgg::meshFromSdfExtract(*sdf, voxel, 0.0f, opts.threads == 0 ? 1 : opts.threads);
        geo = res.mesh;
        char buf[96];
        std::snprintf(buf, sizeof(buf), "sdf (preview voxel %.4g) -> ", voxel);
        prefix = buf;
    } else {
        out.summary = std::string("value of type ") + pgg::scalarName(base) + " has no geometry";
        return out;
    }
    if (!geo || !geo->positions) {
        out.summary = prefix + "empty geometry";
        return out;
    }
    collectGroups(*geo, out.groups);
    if (geo->kind == pgg::GeoKind::Points || geo->faceCount() == 0) {
        pgg::Domain dom = pgg::Domain::Points;
        appendPoints(*geo, groupColumn(*geo, opts.highlightGroup, dom), out);
    } else {
        appendMesh(*geo, opts.highlightGroup, opts.shading, out);
    }
    extendBBox(out);
    out.summary = prefix + countsLabel(*geo);
    out.ok = !out.indices.empty();
    if (!out.ok) out.summary += " (nothing to draw)";
    return out;
}

// --- shaders (GLSL / HLSL / MSL) ------------------------------------------------

namespace {

const char* kVsGlsl = R"(
#version 330
uniform mat4 mvp;
layout(location=0) in vec3 pos;
layout(location=1) in vec3 normal;
layout(location=2) in float mask;
out vec3 v_n;
out float v_mask;
void main() {
    gl_Position = mvp * vec4(pos, 1.0);
    v_n = normal;
    v_mask = mask;
}
)";

const char* kFsGlsl = R"(
#version 330
uniform vec4 light_dir;
uniform vec4 highlight;
uniform vec4 base_color;
in vec3 v_n;
in float v_mask;
out vec4 frag_color;
void main() {
    vec3 n = normalize(v_n);
    if (!gl_FrontFacing) n = -n;
    vec3 l = normalize(light_dir.xyz);
    float dif = clamp(dot(n, l), 0.0, 1.0);
    float sky = 0.55 + 0.45 * n.y;
    vec3 albedo = mix(base_color.rgb, highlight.rgb, v_mask * highlight.a);
    vec3 col = albedo * (0.22 * sky + 0.85 * dif) + vec3(0.06) * pow(dif, 16.0);
    frag_color = vec4(pow(col, vec3(0.4545)), 1.0);
}
)";

const char* kVsHlsl = R"(
cbuffer vs_params: register(b0) { float4x4 mvp; };
struct VSIn { float3 pos: TEXCOORD0; float3 normal: TEXCOORD1; float mask: TEXCOORD2; };
struct VSOut { float4 pos: SV_Position; float3 n: TEXCOORD0; float mask: TEXCOORD1; };
VSOut main(VSIn inp) {
    VSOut o;
    o.pos = mul(mvp, float4(inp.pos, 1.0));
    o.n = inp.normal;
    o.mask = inp.mask;
    return o;
}
)";

const char* kFsHlsl = R"(
cbuffer fs_params: register(b0) { float4 light_dir; float4 highlight; float4 base_color; };
struct PSIn { float4 pos: SV_Position; float3 n: TEXCOORD0; float mask: TEXCOORD1; bool front: SV_IsFrontFace; };
float4 main(PSIn inp): SV_Target {
    float3 n = normalize(inp.n);
    if (!inp.front) n = -n;
    float3 l = normalize(light_dir.xyz);
    float dif = saturate(dot(n, l));
    float sky = 0.55 + 0.45 * n.y;
    float3 albedo = lerp(base_color.rgb, highlight.rgb, inp.mask * highlight.a);
    float3 col = albedo * (0.22 * sky + 0.85 * dif) + 0.06 * pow(dif, 16.0);
    return float4(pow(col, 0.4545), 1.0);
}
)";

const char* kVsMsl = R"(
#include <metal_stdlib>
using namespace metal;
struct VsParams { float4x4 mvp; };
struct VSIn { float3 pos [[attribute(0)]]; float3 normal [[attribute(1)]]; float mask [[attribute(2)]]; };
struct VSOut { float4 pos [[position]]; float3 n; float mask; };
vertex VSOut _main(VSIn in [[stage_in]], constant VsParams& p [[buffer(0)]]) {
    VSOut o;
    o.pos = p.mvp * float4(in.pos, 1.0);
    o.n = in.normal;
    o.mask = in.mask;
    return o;
}
)";

const char* kFsMsl = R"(
#include <metal_stdlib>
using namespace metal;
struct FsParams { float4 light_dir; float4 highlight; float4 base_color; };
struct PSIn { float4 pos [[position]]; float3 n; float mask; };
fragment float4 _main(PSIn in [[stage_in]], constant FsParams& p [[buffer(0)]], bool front [[front_facing]]) {
    float3 n = normalize(in.n);
    if (!front) n = -n;
    float3 l = normalize(p.light_dir.xyz);
    float dif = saturate(dot(n, l));
    float sky = 0.55 + 0.45 * n.y;
    float3 albedo = mix(p.base_color.rgb, p.highlight.rgb, in.mask * p.highlight.a);
    float3 col = albedo * (0.22 * sky + 0.85 * dif) + 0.06 * pow(dif, 16.0);
    return float4(pow(col, 0.4545), 1.0);
}
)";

constexpr int kMaxTarget = 4096;
constexpr sg_pixel_format kColorFormat = SG_PIXELFORMAT_RGBA8;
constexpr sg_pixel_format kDepthFormat = SG_PIXELFORMAT_DEPTH;

}  // namespace

// --- GeometryPreview -------------------------------------------------------------

void GeometryPreview::init() {
    static_assert(sizeof(FsParams) == 48, "3 x vec4 std140 block");
    sg_shader_desc shd = {};
    const sg_backend backend = sg_query_backend();
    if (backend == SG_BACKEND_D3D11) {
        shd.vertex_func.source = kVsHlsl;
        shd.fragment_func.source = kFsHlsl;
        shd.attrs[0].hlsl_sem_name = "TEXCOORD";
        shd.attrs[0].hlsl_sem_index = 0;
        shd.attrs[1].hlsl_sem_name = "TEXCOORD";
        shd.attrs[1].hlsl_sem_index = 1;
        shd.attrs[2].hlsl_sem_name = "TEXCOORD";
        shd.attrs[2].hlsl_sem_index = 2;
    } else if (backend == SG_BACKEND_METAL_MACOS || backend == SG_BACKEND_METAL_IOS ||
               backend == SG_BACKEND_METAL_SIMULATOR) {
        shd.vertex_func.source = kVsMsl;
        shd.fragment_func.source = kFsMsl;
    } else {
        shd.vertex_func.source = kVsGlsl;
        shd.fragment_func.source = kFsGlsl;
    }
    shd.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd.uniform_blocks[0].size = sizeof(VsParams);
    shd.uniform_blocks[0].layout = SG_UNIFORMLAYOUT_STD140;
    shd.uniform_blocks[0].hlsl_register_b_n = 0;
    shd.uniform_blocks[0].msl_buffer_n = 0;
    shd.uniform_blocks[0].glsl_uniforms[0].glsl_name = "mvp";
    shd.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;
    shd.uniform_blocks[0].glsl_uniforms[0].array_count = 1;
    shd.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    shd.uniform_blocks[1].size = sizeof(FsParams);
    shd.uniform_blocks[1].layout = SG_UNIFORMLAYOUT_STD140;
    shd.uniform_blocks[1].hlsl_register_b_n = 0;
    shd.uniform_blocks[1].msl_buffer_n = 0;
    const char* fsNames[3] = {"light_dir", "highlight", "base_color"};
    for (int i = 0; i < 3; ++i) {
        shd.uniform_blocks[1].glsl_uniforms[i].glsl_name = fsNames[i];
        shd.uniform_blocks[1].glsl_uniforms[i].type = SG_UNIFORMTYPE_FLOAT4;
        shd.uniform_blocks[1].glsl_uniforms[i].array_count = 1;
    }
    shd.label = "pggviewer-preview-shd";
    m_shader = sg_make_shader(&shd);

    sg_pipeline_desc pip = {};
    pip.shader = m_shader;
    pip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    pip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
    pip.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT;
    pip.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    pip.index_type = SG_INDEXTYPE_UINT32;
    pip.cull_mode = SG_CULLMODE_NONE;  // open meshes / arbitrary winding still read
    // pgg faces are CCW seen from outside; sokol defaults to CW, which would
    // make every outward face "back-facing" and the FS normal flip would light
    // the mesh from inside out.
    pip.face_winding = SG_FACEWINDING_CCW;
    pip.depth.pixel_format = kDepthFormat;
    pip.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    pip.depth.write_enabled = true;
    pip.colors[0].pixel_format = kColorFormat;
    pip.label = "pggviewer-preview-pip";
    m_pip = sg_make_pipeline(&pip);

    if (sg_query_shader_state(m_shader) != SG_RESOURCESTATE_VALID ||
        sg_query_pipeline_state(m_pip) != SG_RESOURCESTATE_VALID) {
        spdlog::error("GeometryPreview: pipeline creation failed");
        m_ok = false;
        return;
    }
    m_ok = true;
}

void GeometryPreview::shutdown() {
    clear();
    destroyTarget();
    if (m_pip.id != SG_INVALID_ID) sg_destroy_pipeline(m_pip);
    if (m_shader.id != SG_INVALID_ID) sg_destroy_shader(m_shader);
    m_pip = {};
    m_shader = {};
}

void GeometryPreview::clear() {
    if (m_vbuf.id != SG_INVALID_ID) sg_destroy_buffer(m_vbuf);
    if (m_ibuf.id != SG_INVALID_ID) sg_destroy_buffer(m_ibuf);
    m_vbuf = {};
    m_ibuf = {};
    m_indexCount = 0;
}

void GeometryPreview::setGeometry(const PreviewGeometry& geo, bool refit) {
    clear();
    m_summary = geo.summary;
    if (!geo.ok || !m_ok) return;

    sg_buffer_desc vb = {};
    vb.usage.vertex_buffer = true;
    vb.data.ptr = geo.vertices.data();
    vb.data.size = geo.vertices.size() * sizeof(PreviewVertex);
    vb.label = "pggviewer-preview-vb";
    m_vbuf = sg_make_buffer(&vb);

    sg_buffer_desc ib = {};
    ib.usage.index_buffer = true;
    ib.data.ptr = geo.indices.data();
    ib.data.size = geo.indices.size() * sizeof(uint32_t);
    ib.label = "pggviewer-preview-ib";
    m_ibuf = sg_make_buffer(&ib);
    m_indexCount = static_cast<int>(geo.indices.size());

    m_fitCenter = (geo.bmin + geo.bmax) * 0.5f;
    m_radius = std::max(1e-3f, glm::length(geo.bmax - geo.bmin) * 0.5f);
    if (refit) {
        m_center = m_fitCenter;
        m_distance = m_radius * 2.6f * m_fitZoom;
    }
}

void GeometryPreview::setOrbit(float yawDeg, float pitchDeg, float zoom) {
    m_yaw = glm::radians(yawDeg);
    m_pitch = std::clamp(glm::radians(pitchDeg), -1.55f, 1.55f);
    m_fitZoom = std::clamp(zoom, 0.05f, 50.0f);
    m_distance = m_radius * 2.6f * m_fitZoom;
}

void GeometryPreview::destroyTarget() {
    if (m_texView.id != SG_INVALID_ID) sg_destroy_view(m_texView);
    if (m_colorAttach.id != SG_INVALID_ID) sg_destroy_view(m_colorAttach);
    if (m_depthAttach.id != SG_INVALID_ID) sg_destroy_view(m_depthAttach);
    if (m_color.id != SG_INVALID_ID) sg_destroy_image(m_color);
    if (m_depth.id != SG_INVALID_ID) sg_destroy_image(m_depth);
    m_texView = m_colorAttach = m_depthAttach = {};
    m_color = m_depth = {};
    m_targetW = m_targetH = 0;
}

void GeometryPreview::ensureTarget(int w, int h) {
    // Clamp BOTH axes proportionally: clamping only the overflowing axis makes
    // the target aspect differ from the pane rect and AddImage then stretches
    // the image non-uniformly (HiDPI panes wider than kMaxTarget hit this).
    const float s = std::min(1.0f, std::min(static_cast<float>(kMaxTarget) / static_cast<float>(w),
                                            static_cast<float>(kMaxTarget) / static_cast<float>(h)));
    w = std::clamp(static_cast<int>(w * s), 16, kMaxTarget);
    h = std::clamp(static_cast<int>(h * s), 16, kMaxTarget);
    if (w == m_targetW && h == m_targetH) return;
    destroyTarget();

    sg_image_desc cd = {};
    cd.usage.color_attachment = true;
    cd.width = w;
    cd.height = h;
    cd.pixel_format = kColorFormat;
    cd.label = "pggviewer-preview-color";
    m_color = sg_make_image(&cd);

    sg_image_desc dd = {};
    dd.usage.depth_stencil_attachment = true;
    dd.width = w;
    dd.height = h;
    dd.pixel_format = kDepthFormat;
    dd.label = "pggviewer-preview-depth";
    m_depth = sg_make_image(&dd);

    sg_view_desc cv = {};
    cv.color_attachment.image = m_color;
    m_colorAttach = sg_make_view(&cv);
    sg_view_desc dv = {};
    dv.depth_stencil_attachment.image = m_depth;
    m_depthAttach = sg_make_view(&dv);
    sg_view_desc tv = {};
    tv.texture.image = m_color;
    m_texView = sg_make_view(&tv);

    m_targetW = w;
    m_targetH = h;
}

glm::mat4 GeometryPreview::viewMatrix() const {
    const glm::vec3 dir(std::cos(m_pitch) * std::sin(m_yaw), std::sin(m_pitch), std::cos(m_pitch) * std::cos(m_yaw));
    const glm::vec3 eye = m_center + dir * m_distance;
    return glm::lookAt(eye, m_center, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 GeometryPreview::viewProj(float aspect) const {
    const glm::mat4 view = viewMatrix();
    const float nearZ = std::max(1e-3f, m_distance * 0.01f);
    const float farZ = m_distance + m_radius * 4.0f + 1.0f;
    const sg_backend backend = sg_query_backend();
    const bool zeroToOne = backend == SG_BACKEND_D3D11 || backend == SG_BACKEND_METAL_MACOS ||
                           backend == SG_BACKEND_METAL_IOS || backend == SG_BACKEND_METAL_SIMULATOR ||
                           backend == SG_BACKEND_WGPU;
    const glm::mat4 proj = zeroToOne ? glm::perspectiveRH_ZO(glm::radians(40.0f), aspect, nearZ, farZ)
                                     : glm::perspectiveRH_NO(glm::radians(40.0f), aspect, nearZ, farZ);
    return proj * view;
}

void GeometryPreview::drawWindowContents() {
    // Toolbar.
    if (ImGui::SmallButton("Fit")) {
        m_distance = m_radius * 2.6f * m_fitZoom;
        m_center = m_fitCenter;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", m_summary.empty() ? "(no geometry)" : m_summary.c_str());

    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.x = std::max(avail.x, 64.0f);
    avail.y = std::max(avail.y, 64.0f);
    // Per-axis points->pixels: the axes' framebuffer scales may differ, and a
    // wrong axis here stretches the image (the camera aspect follows the
    // TARGET size, the blit follows the rect — they must match).
    const ImVec2 fbScale = ImGui::GetIO().DisplayFramebufferScale;
    m_wantW = static_cast<int>(avail.x * std::max(1.0f, fbScale.x));
    m_wantH = static_cast<int>(avail.y * std::max(1.0f, fbScale.y));
    ensureTarget(m_wantW, m_wantH);

    ImGui::InvisibleButton("##preview_canvas", avail,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const ImVec2 rmin = ImGui::GetItemRectMin();
    const ImVec2 rmax = ImGui::GetItemRectMax();
    if (m_texView.id != SG_INVALID_ID) {
        // GL render targets are stored bottom-up: flip V when the backend's
        // origin is bottom-left. (Verified on GLCORE: without the flip the
        // mesh is seen upside down — top faces land at the bottom of the image.)
        const bool topLeft = sg_query_features().origin_top_left;
        const ImVec2 uv0 = topLeft ? ImVec2(0, 0) : ImVec2(0, 1);
        const ImVec2 uv1 = topLeft ? ImVec2(1, 1) : ImVec2(1, 0);
        ImGui::GetWindowDrawList()->AddImage(simgui_imtextureid(m_texView), rmin, rmax, uv0, uv1);
    }
    if (!m_error.empty()) {
        // Wrapped red text over the (empty) canvas: a truncated one-liner in
        // the toolbar hid the reason from the user.
        const float wrapW = std::max(80.0f, rmax.x - rmin.x - 24.0f);
        const ImVec2 ts = ImGui::CalcTextSize(m_error.c_str(), nullptr, false, wrapW);
        ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                                            ImVec2(rmin.x + 12.0f, std::max(rmin.y + 12.0f, (rmin.y + rmax.y - ts.y) * 0.5f)),
                                            IM_COL32(255, 120, 100, 255), m_error.c_str(), nullptr, wrapW);
    } else if (!hasGeometry()) {
        const char* msg = "select a node and press Preview";
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        ImGui::GetWindowDrawList()->AddText(ImVec2((rmin.x + rmax.x - ts.x) * 0.5f, (rmin.y + rmax.y - ts.y) * 0.5f),
                                            IM_COL32(150, 150, 160, 255), msg);
    }

    // Orbit / pan / zoom while hovering or dragging the canvas.
    const ImGuiIO& io = ImGui::GetIO();
    const bool active = ImGui::IsItemActive();
    if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f)
        m_distance = std::clamp(m_distance * std::pow(0.9f, io.MouseWheel), m_radius * 0.05f, m_radius * 50.0f);
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        m_yaw -= io.MouseDelta.x * 0.01f;
        m_pitch = std::clamp(m_pitch + io.MouseDelta.y * 0.01f, -1.55f, 1.55f);
    }
    if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f) ||
                   ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))) {
        const glm::vec3 dir(std::cos(m_pitch) * std::sin(m_yaw), std::sin(m_pitch),
                            std::cos(m_pitch) * std::cos(m_yaw));
        const glm::vec3 right = glm::normalize(glm::cross(glm::vec3(0, 1, 0), dir));
        const glm::vec3 up = glm::normalize(glm::cross(dir, right));
        const float k = m_distance * 0.0025f;
        m_center += (-right * io.MouseDelta.x + up * io.MouseDelta.y) * k;
    }
}

void GeometryPreview::render() {
    if (!m_ok || m_colorAttach.id == SG_INVALID_ID) return;

    sg_pass pass = {};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = {0.14f, 0.15f, 0.18f, 1.0f};
    pass.action.depth.load_action = SG_LOADACTION_CLEAR;
    pass.action.depth.clear_value = 1.0f;
    pass.attachments.colors[0] = m_colorAttach;
    pass.attachments.depth_stencil = m_depthAttach;
    pass.label = "pggviewer-preview-pass";
    sg_begin_pass(&pass);
    if (hasGeometry()) {
        const float aspect = static_cast<float>(m_targetW) / static_cast<float>(std::max(1, m_targetH));
        const glm::mat4 mvp = viewProj(aspect);
        VsParams vs = {};
        std::memcpy(vs.mvp, glm::value_ptr(mvp), sizeof(vs.mvp));
        // Key light from the camera's upper-left, expressed in the *camera* frame so
        // it stays a headlight at every orbit. Mixing in world-up here is wrong: at
        // pitch -> -90 deg (looking up from below) it cancels the view direction and
        // the light turns grazing; and cross(worldUp, dir) degenerates at the poles.
        // The camera basis comes from the view matrix (lookAt already handles that).
        const glm::mat4 view = viewMatrix();
        const glm::vec3 camRight(view[0][0], view[1][0], view[2][0]);
        const glm::vec3 camUp(view[0][1], view[1][1], view[2][1]);
        const glm::vec3 camBack(view[0][2], view[1][2], view[2][2]);  // towards the eye
        const glm::vec3 light = glm::normalize(camBack * 0.7f - camRight * 0.5f + camUp * 0.6f);
        FsParams fs = {};
        fs.lightDir[0] = light.x;
        fs.lightDir[1] = light.y;
        fs.lightDir[2] = light.z;
        fs.highlight[0] = 1.0f;
        fs.highlight[1] = 0.55f;
        fs.highlight[2] = 0.15f;
        fs.highlight[3] = 1.0f;
        fs.base[0] = 0.66f;
        fs.base[1] = 0.64f;
        fs.base[2] = 0.61f;
        fs.base[3] = 0.0f;

        sg_apply_pipeline(m_pip);
        sg_bindings bind = {};
        bind.vertex_buffers[0] = m_vbuf;
        bind.index_buffer = m_ibuf;
        sg_apply_bindings(&bind);
        sg_apply_uniforms(0, SG_RANGE(vs));
        sg_apply_uniforms(1, SG_RANGE(fs));
        sg_draw(0, m_indexCount, 1);
    }
    sg_end_pass();
}
