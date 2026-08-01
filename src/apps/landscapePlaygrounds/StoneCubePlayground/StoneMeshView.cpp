#include "pch.h"

#include "StoneMeshView.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <sokol_glue.h>
#include <spdlog/spdlog.h>

namespace stonecube {

namespace {

const char* meshVsSource() {
    return R"GLSL(
#version 330

uniform mat4 mvp;

layout(location=0) in vec3 pos;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;

out vec3 v_n;
out vec2 v_uv;

void main() {
    v_n = normal;
    v_uv = uv;
    gl_Position = mvp * vec4(pos, 1.0);
}
)GLSL";
}

const char* meshFsSource() {
    return R"GLSL(
#version 330

uniform sampler2D albedo_tex; // rgb = albedo, a = AO
uniform sampler2D normal_tex; // object-space normal (0.5+0.5n)
uniform vec4 light_dir;

in vec3 v_n;
in vec2 v_uv;
out vec4 fragColor;

void main() {
    vec4 alb = texture(albedo_tex, v_uv);
    vec3 n = normalize(texture(normal_tex, v_uv).rgb * 2.0 - 1.0);
    float dif = clamp(dot(n, normalize(light_dir.xyz)), 0.0, 1.0);
    vec3 col = alb.rgb * (0.25 + 1.1 * dif) * alb.a;
    fragColor = vec4(pow(col, vec3(0.4545)), 1.0);
}
)GLSL";
}

// ---------------------------------------------------------------------------
// Procedural variant: the raymarch twin's material/light block, fed by the
// rasterizer (interpolated world position + smooth normal) instead of a ray
// hit. No UVs, no textures: voro/fbm/bump/AO/soft-shadow are pure functions
// of the world position, so the detail is resolution-independent and every
// look param stays live.
// ---------------------------------------------------------------------------

const char* procVsSource() {
    return R"GLSL(
#version 330

uniform mat4 mvp;

layout(location=0) in vec3 pos;
layout(location=1) in vec3 normal;

out vec3 v_wp;
out vec3 v_n;

void main() {
    v_wp = pos; // model transform is identity: vertex position IS world position
    v_n = normal;
    gl_Position = mvp * vec4(pos, 1.0);
}
)GLSL";
}

const char* procFsSource() {
    return R"GLSL(
#version 330

uniform vec4 boxSize;   // xyz = half extents, w = bevel
uniform vec4 shape1;    // x = voroScale, y = jitter, z = grooveDepth, w = grooveK
uniform vec4 shape2;    // x = fbmAmp, y = fbmFreq, z = bumpStrength, w = seed
uniform vec4 look;      // x = lightYaw, y = lightPitch, z = moss, w = toneSeed
uniform vec4 light_dir; // xyz
uniform vec4 cam_pos;   // xyz

in vec3 v_wp;
in vec3 v_n;
out vec4 fragColor;

#define voroScale   shape1.x
#define jitter      shape1.y
#define grooveDepth shape1.z
#define grooveK     shape1.w
#define fbmAmp      shape2.x
#define fbmFreq     shape2.y
#define bumpStr     shape2.z
#define seed        shape2.w
#define mossAmt     look.z
#define toneSeed    look.w

// Hashes / voronoi / fbm — identical to the raymarch twin (StoneCubeSdfGlsl).
uvec3 pcg3d(uvec3 v) {
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    return v;
}

vec3 hash3f(vec3 p) {
    return vec3(pcg3d(uvec3(ivec3(floor(p))))) * (1.0 / 4294967296.0);
}

vec3 voro(vec3 x) {
    vec3 p = floor(x);
    vec3 f = fract(x);
    float id = 0.0;
    vec2 res = vec2(100.0);
    for (int k = -1; k <= 1; k++)
    for (int j = -1; j <= 1; j++)
    for (int i = -1; i <= 1; i++) {
        vec3 b = vec3(float(i), float(j), float(k));
        vec3 r = b - f + mix(vec3(0.5), hash3f(p + b), jitter);
        float d = dot(r, r);
        if (d < res.x) {
            id = dot(p + b, vec3(1.0, 57.0, 113.0));
            res = vec2(d, res.x);
        } else if (d < res.y) {
            res.y = d;
        }
    }
    return vec3(sqrt(res), abs(id));
}

float vnoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    const vec3 o = vec3(1.0, 0.0, 0.0);
    float n000 = hash3f(i).x;
    float n100 = hash3f(i + o.xyy).x;
    float n010 = hash3f(i + o.yxy).x;
    float n110 = hash3f(i + o.xxy).x;
    float n001 = hash3f(i + o.yyx).x;
    float n101 = hash3f(i + o.xyx).x;
    float n011 = hash3f(i + o.yxx).x;
    float n111 = hash3f(i + o.xxx).x;
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

float fbm(vec3 p) {
    float f = 0.0;
    float a = 0.5;
    for (int i = 0; i < 4; ++i) {
        f += a * vnoise(p);
        p = p * 2.0 + 11.7;
        a *= 0.5;
    }
    return f;
}

float sdRoundBox(vec3 p, vec3 b, float r) {
    vec3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0) - r;
}

// (dist, cellFactor, cellId) — the same map() the raymarch twin marches.
vec3 map(vec3 p) {
    float dBox = sdRoundBox(p, boxSize.xyz, boxSize.w);
    vec3 v = voro(voroScale * p + seed);
    float f = clamp(grooveK * (v.y - v.x), 0.0, 1.0);
    float d = dBox - grooveDepth * f;
    d += fbmAmp * (fbm(fbmFreq * p) - 0.5);
    return vec3(d, f, v.z);
}

float mapDist(vec3 p) { return map(p).x; }

vec3 bumpNormal(vec3 p, vec3 n) {
    // Canonical capped variant (stone_sdf.cpp): the raw fbm gradient reaches
    // tens of units and would randomize the normal instead of perturbing it.
    const float e = 0.002;
    float ref = fbm(fbmFreq * 4.0 * p);
    vec3 grad = vec3(fbm(fbmFreq * 4.0 * (p + vec3(e, 0.0, 0.0))) - ref,
                     fbm(fbmFreq * 4.0 * (p + vec3(0.0, e, 0.0))) - ref,
                     fbm(fbmFreq * 4.0 * (p + vec3(0.0, 0.0, e))) - ref) / e;
    grad -= n * dot(n, grad);
    float gl = length(grad);
    if (gl > 1e-6) {
        grad *= 1.0 / (1.0 + gl);
    }
    return normalize(n + bumpStr * grad);
}

float calcAO(vec3 p, vec3 n) {
    float occ = 0.0;
    float sca = 1.0;
    for (int i = 1; i <= 5; ++i) {
        float h = 0.01 + 0.05 * float(i);
        occ += (h - mapDist(p + n * h)) * sca;
        sca *= 0.75;
    }
    return clamp(1.0 - 2.0 * occ, 0.0, 1.0);
}

float softShadow(vec3 ro, vec3 rd) {
    float res = 1.0;
    float t = 0.02;
    for (int i = 0; i < 32; ++i) {
        float h = mapDist(ro + rd * t);
        res = min(res, 8.0 * h / t);
        t += clamp(h, 0.02, 0.2);
        if (res < 0.005 || t > 6.0) break;
    }
    return clamp(res, 0.0, 1.0);
}

void main() {
    vec3 wp = v_wp;
    vec3 n = normalize(v_n);
    n = bumpNormal(wp, n);

    // Material — same formulas as the raymarch twin's shading block.
    vec3 m = map(wp);
    float cellF = m.y;
    float idHash = fract(sin(m.z * 17.31 + toneSeed * 91.7) * 43758.5453);
    vec3 rockCol = mix(vec3(0.40, 0.40, 0.43), vec3(0.60, 0.57, 0.50), idHash);
    rockCol *= 0.85 + 0.30 * idHash;
    float mossMask = smoothstep(0.35, 0.75, n.y) *
        smoothstep(0.4, 0.6, fbm(2.0 * wp + toneSeed));
    rockCol = mix(rockCol, vec3(0.24, 0.36, 0.15), mossAmt * mossMask);
    rockCol *= 0.45 + 0.55 * cellF;

    vec3 lightDir = normalize(light_dir.xyz);
    float ao = calcAO(wp, n);
    float dif = clamp(dot(n, lightDir), 0.0, 1.0);
    float sha = softShadow(wp + n * 0.01, lightDir);
    float sky = 0.5 + 0.5 * n.y;

    vec3 col = rockCol * (0.15 + 0.30 * ao * sky) + rockCol * dif * sha * 1.15;

    fragColor = vec4(pow(col, vec3(0.4545)), 1.0);
}
)GLSL";
}

struct Vertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
};

// CPU box-filter mip chain (sokol does not generate mipmaps; the baked
// bump-normal detail is above texel frequency at minification and sparkles
// without mips).
sg_image makeTexture(int size, const std::vector<std::uint8_t>& rgba, const char* label) {
    int levels = 1;
    while ((size >> levels) > 1) {
        ++levels;
    }
    if (levels > SG_MAX_MIPMAPS) {
        levels = SG_MAX_MIPMAPS;
    }

    std::vector<std::vector<std::uint8_t>> mipData(static_cast<size_t>(levels));
    sg_image_desc desc = {};
    desc.width = size;
    desc.height = size;
    desc.num_mipmaps = levels;
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.data.mip_levels[0].ptr = rgba.data();
    desc.data.mip_levels[0].size = rgba.size();

    int pw = size;
    const std::uint8_t* prev = rgba.data();
    for (int level = 1; level < levels; ++level) {
        const int lw = std::max(1, pw / 2);
        auto& data = mipData[static_cast<size_t>(level)];
        data.resize(static_cast<size_t>(lw) * lw * 4);
        for (int y = 0; y < lw; ++y) {
            for (int x = 0; x < lw; ++x) {
                for (int c = 0; c < 4; ++c) {
                    int sum = 0;
                    for (int dy = 0; dy < 2; ++dy) {
                        for (int dx = 0; dx < 2; ++dx) {
                            const int sx = std::min(2 * x + dx, pw - 1);
                            const int sy = std::min(2 * y + dy, pw - 1);
                            sum += prev[(static_cast<size_t>(sy) * pw + sx) * 4 + c];
                        }
                    }
                    data[(static_cast<size_t>(y) * lw + x) * 4 + c] =
                        static_cast<std::uint8_t>(sum / 4);
                }
            }
        }
        desc.data.mip_levels[level].ptr = data.data();
        desc.data.mip_levels[level].size = data.size();
        prev = data.data();
        pw = lw;
    }

    desc.label = label;
    return sg_make_image(&desc);
}

sg_view makeTextureView(sg_image image) {
    sg_view_desc desc = {};
    desc.texture.image = image;
    return sg_make_view(&desc);
}

} // namespace

void StoneMeshView::init() {
    static_assert(sizeof(FsProcUniforms) == 96, "6 x vec4 std140 block");

    sg_sampler_desc smp = {};
    smp.min_filter = SG_FILTER_LINEAR;
    smp.mag_filter = SG_FILTER_LINEAR;
    smp.mipmap_filter = SG_FILTER_LINEAR; // trilinear: calm the baked bump detail
    smp.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    smp.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    m_sampler = sg_make_sampler(&smp);

    sg_shader_desc shd = {};
    shd.vertex_func.source = meshVsSource();
    shd.fragment_func.source = meshFsSource();
    shd.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd.uniform_blocks[0].size = sizeof(VsUniforms);
    shd.uniform_blocks[0].layout = SG_UNIFORMLAYOUT_STD140;
    shd.uniform_blocks[0].glsl_uniforms[0].glsl_name = "mvp";
    shd.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;
    shd.uniform_blocks[0].glsl_uniforms[0].array_count = 1;
    shd.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    shd.uniform_blocks[1].size = sizeof(FsUniforms);
    shd.uniform_blocks[1].layout = SG_UNIFORMLAYOUT_STD140;
    shd.uniform_blocks[1].glsl_uniforms[0].glsl_name = "light_dir";
    shd.uniform_blocks[1].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;
    shd.uniform_blocks[1].glsl_uniforms[0].array_count = 1;
    const char* texNames[] = {"albedo_tex", "normal_tex"};
    for (int i = 0; i < 2; ++i) {
        shd.views[i].texture.stage = SG_SHADERSTAGE_FRAGMENT;
        shd.views[i].texture.image_type = SG_IMAGETYPE_2D;
        shd.views[i].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
        shd.texture_sampler_pairs[i].stage = SG_SHADERSTAGE_FRAGMENT;
        shd.texture_sampler_pairs[i].view_slot = i;
        shd.texture_sampler_pairs[i].sampler_slot = 0;
        shd.texture_sampler_pairs[i].glsl_name = texNames[i];
    }
    shd.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    shd.label = "stonecube-mesh-shader";
    m_shader = sg_make_shader(&shd);

    sg_pipeline_desc pip = {};
    pip.shader = m_shader;
    pip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    pip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
    pip.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT2;
    pip.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    pip.index_type = SG_INDEXTYPE_UINT32;
    pip.cull_mode = SG_CULLMODE_BACK;
    pip.face_winding = SG_FACEWINDING_CCW;
    pip.depth.pixel_format = sglue_swapchain().depth_format;
    pip.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    pip.depth.write_enabled = true;
    pip.label = "stonecube-mesh-pipeline";
    m_pip = sg_make_pipeline(&pip);

    // Procedural variant: same VB (pos+normal only), material from uniforms.
    {
        sg_shader_desc pshd = {};
        pshd.vertex_func.source = procVsSource();
        pshd.fragment_func.source = procFsSource();
        pshd.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
        pshd.uniform_blocks[0].size = sizeof(VsUniforms);
        pshd.uniform_blocks[0].layout = SG_UNIFORMLAYOUT_STD140;
        pshd.uniform_blocks[0].glsl_uniforms[0].glsl_name = "mvp";
        pshd.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;
        pshd.uniform_blocks[0].glsl_uniforms[0].array_count = 1;
        pshd.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
        pshd.uniform_blocks[1].size = sizeof(FsProcUniforms);
        pshd.uniform_blocks[1].layout = SG_UNIFORMLAYOUT_STD140;
        const char* names[] = {"boxSize", "shape1", "shape2", "look",
            "light_dir", "cam_pos"};
        for (int i = 0; i < 6; ++i) {
            pshd.uniform_blocks[1].glsl_uniforms[i].glsl_name = names[i];
            pshd.uniform_blocks[1].glsl_uniforms[i].type = SG_UNIFORMTYPE_FLOAT4;
            pshd.uniform_blocks[1].glsl_uniforms[i].array_count = 1;
        }
        pshd.label = "stonecube-proc-shader";
        m_procShader = sg_make_shader(&pshd);

        sg_pipeline_desc ppip = {};
        ppip.shader = m_procShader;
        // The VB stride is wider than pos+normal: set it explicitly, or
        // sokol derives 24 from the two attrs and attribute fetch breaks.
        ppip.layout.buffers[0].stride = sizeof(Vertex);
        ppip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
        ppip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
        ppip.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
        ppip.index_type = SG_INDEXTYPE_UINT32;
        ppip.cull_mode = SG_CULLMODE_BACK;
        ppip.face_winding = SG_FACEWINDING_CCW;
        ppip.depth.pixel_format = sglue_swapchain().depth_format;
        ppip.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        ppip.depth.write_enabled = true;
        ppip.label = "stonecube-proc-pipeline";
        m_procPip = sg_make_pipeline(&ppip);
    }

    if (sg_query_shader_state(m_shader) != SG_RESOURCESTATE_VALID ||
        sg_query_pipeline_state(m_pip) != SG_RESOURCESTATE_VALID ||
        sg_query_shader_state(m_procShader) != SG_RESOURCESTATE_VALID ||
        sg_query_pipeline_state(m_procPip) != SG_RESOURCESTATE_VALID) {
        spdlog::error("StoneCubePlayground: mesh pipeline creation failed");
    }
}

void StoneMeshView::shutdown() {
    if (m_vbuf.id != SG_INVALID_ID) sg_destroy_buffer(m_vbuf);
    if (m_ibuf.id != SG_INVALID_ID) sg_destroy_buffer(m_ibuf);
    if (m_pip.id != SG_INVALID_ID) sg_destroy_pipeline(m_pip);
    if (m_shader.id != SG_INVALID_ID) sg_destroy_shader(m_shader);
    if (m_procPip.id != SG_INVALID_ID) sg_destroy_pipeline(m_procPip);
    if (m_procShader.id != SG_INVALID_ID) sg_destroy_shader(m_procShader);
    if (m_sampler.id != SG_INVALID_ID) sg_destroy_sampler(m_sampler);
    if (m_albedoView.id != SG_INVALID_ID) sg_destroy_view(m_albedoView);
    if (m_albedoImg.id != SG_INVALID_ID) sg_destroy_image(m_albedoImg);
    if (m_normalView.id != SG_INVALID_ID) sg_destroy_view(m_normalView);
    if (m_normalImg.id != SG_INVALID_ID) sg_destroy_image(m_normalImg);
}

void StoneMeshView::setMesh(const stone_gen::StoneMesh& mesh) {
    if (m_vbuf.id != SG_INVALID_ID) sg_destroy_buffer(m_vbuf);
    if (m_ibuf.id != SG_INVALID_ID) sg_destroy_buffer(m_ibuf);
    m_vbuf = {};
    m_ibuf = {};
    m_indexCount = 0;

    std::vector<Vertex> vertices(mesh.vertices.size());
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        const stone_gen::StoneMeshVertex& v = mesh.vertices[i];
        vertices[i] = {v.pos.x, v.pos.y, v.pos.z,
            v.normal.x, v.normal.y, v.normal.z, v.uv.x, v.uv.y};
    }

    sg_buffer_desc vb = {};
    vb.usage.vertex_buffer = true;
    vb.data.ptr = vertices.data();
    vb.data.size = vertices.size() * sizeof(Vertex);
    vb.label = "stonecube-mesh-vbuf";
    m_vbuf = sg_make_buffer(&vb);

    sg_buffer_desc ib = {};
    ib.usage.index_buffer = true;
    ib.data.ptr = mesh.indices.data();
    ib.data.size = mesh.indices.size() * sizeof(std::uint32_t);
    ib.label = "stonecube-mesh-ibuf";
    m_ibuf = sg_make_buffer(&ib);

    m_indexCount = static_cast<int>(mesh.indices.size());
}

void StoneMeshView::setTextures(int size, const std::vector<std::uint8_t>& albedo,
    const std::vector<std::uint8_t>& normal) {
    if (m_albedoView.id != SG_INVALID_ID) sg_destroy_view(m_albedoView);
    if (m_albedoImg.id != SG_INVALID_ID) sg_destroy_image(m_albedoImg);
    if (m_normalView.id != SG_INVALID_ID) sg_destroy_view(m_normalView);
    if (m_normalImg.id != SG_INVALID_ID) sg_destroy_image(m_normalImg);
    m_albedoImg = makeTexture(size, albedo, "stonecube-albedo");
    m_albedoView = makeTextureView(m_albedoImg);
    m_normalImg = makeTexture(size, normal, "stonecube-normal");
    m_normalView = makeTextureView(m_normalImg);
}

void StoneMeshView::draw(const Camera& cam, const float lightDir[3], int fbWidth,
    int fbHeight) const {
    if (m_indexCount == 0 || m_albedoView.id == SG_INVALID_ID) {
        return;
    }
    const float aspect = fbHeight > 0 ? static_cast<float>(fbWidth) / fbHeight : 1.0f;
    const glm::mat4 proj = glm::perspective(glm::radians(42.0f), aspect, 0.1f, 100.0f);
    const float cp = std::cos(cam.pitch);
    const float sp = std::sin(cam.pitch);
    const glm::vec3 eye(cam.target[0] + cam.dist * cp * std::sin(cam.yaw),
        cam.target[1] + cam.dist * sp,
        cam.target[2] + cam.dist * cp * std::cos(cam.yaw));
    const glm::mat4 view = glm::lookAt(eye,
        glm::vec3(cam.target[0], cam.target[1], cam.target[2]),
        glm::vec3(0.0f, 1.0f, 0.0f));

    VsUniforms vsu = {};
    const glm::mat4 mvp = proj * view;
    std::memcpy(vsu.mvp, &mvp, sizeof(vsu.mvp));
    FsUniforms fsu = {};
    fsu.lightDir[0] = lightDir[0];
    fsu.lightDir[1] = lightDir[1];
    fsu.lightDir[2] = lightDir[2];
    fsu.lightDir[3] = 0.0f;

    sg_apply_pipeline(m_pip);
    sg_bindings bind = {};
    bind.vertex_buffers[0] = m_vbuf;
    bind.index_buffer = m_ibuf;
    bind.views[0] = m_albedoView;
    bind.views[1] = m_normalView;
    bind.samplers[0] = m_sampler;
    sg_apply_bindings(&bind);
    const sg_range vsRange = {&vsu, sizeof(vsu)};
    sg_apply_uniforms(0, &vsRange);
    const sg_range fsRange = {&fsu, sizeof(fsu)};
    sg_apply_uniforms(1, &fsRange);
    sg_draw(0, m_indexCount, 1);
}

void StoneMeshView::drawProcedural(const Camera& cam,
    const stone_gen::StoneCubeParams& params, int fbWidth, int fbHeight) const {
    if (m_indexCount == 0 || m_procPip.id == SG_INVALID_ID) {
        return;
    }
    const float aspect = fbHeight > 0 ? static_cast<float>(fbWidth) / fbHeight : 1.0f;
    const glm::mat4 proj = glm::perspective(glm::radians(42.0f), aspect, 0.1f, 100.0f);
    const float cp = std::cos(cam.pitch);
    const float sp = std::sin(cam.pitch);
    const glm::vec3 eye(cam.target[0] + cam.dist * cp * std::sin(cam.yaw),
        cam.target[1] + cam.dist * sp,
        cam.target[2] + cam.dist * cp * std::cos(cam.yaw));
    const glm::mat4 view = glm::lookAt(eye,
        glm::vec3(cam.target[0], cam.target[1], cam.target[2]),
        glm::vec3(0.0f, 1.0f, 0.0f));

    VsUniforms vsu = {};
    const glm::mat4 mvp = proj * view;
    std::memcpy(vsu.mvp, &mvp, sizeof(vsu.mvp));

    FsProcUniforms fsu = {};
    std::memcpy(fsu.boxSize, params.boxSize, sizeof(fsu.boxSize));
    std::memcpy(fsu.shape1, params.shape1, sizeof(fsu.shape1));
    std::memcpy(fsu.shape2, params.shape2, sizeof(fsu.shape2));
    std::memcpy(fsu.look, params.look, sizeof(fsu.look));
    const float yaw = params.look[0];
    const float pitch = params.look[1];
    fsu.lightDir[0] = std::cos(pitch) * std::sin(yaw);
    fsu.lightDir[1] = std::sin(pitch);
    fsu.lightDir[2] = std::cos(pitch) * std::cos(yaw);
    fsu.lightDir[3] = 0.0f;
    fsu.camPos[0] = eye.x;
    fsu.camPos[1] = eye.y;
    fsu.camPos[2] = eye.z;
    fsu.camPos[3] = 0.0f;

    sg_apply_pipeline(m_procPip);
    sg_bindings bind = {};
    bind.vertex_buffers[0] = m_vbuf;
    bind.index_buffer = m_ibuf;
    sg_apply_bindings(&bind);
    const sg_range vsRange = {&vsu, sizeof(vsu)};
    sg_apply_uniforms(0, &vsRange);
    const sg_range fsRange = {&fsu, sizeof(fsu)};
    sg_apply_uniforms(1, &fsRange);
    sg_draw(0, m_indexCount, 1);
}

} // namespace stonecube
