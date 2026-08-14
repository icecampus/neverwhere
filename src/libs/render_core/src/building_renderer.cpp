#include "render_core/building_renderer.h"

#include <cstring>

#include <spdlog/spdlog.h>

#include "render_core/depth_levels.h"
#include "render_core/gltf_mesh.h"
#include "render_core/image_loader.h"

namespace render_core {
namespace {

static const char* vs_glsl = R"(
#version 330
layout(location=0) in vec3 pos;
layout(location=1) in vec3 nrm;
layout(location=2) in vec2 uv;
layout(location=3) in vec2 cell;
uniform vec2 view_size;
uniform vec2 camera_offset;
uniform vec2 z_range;
uniform float camera_zoom;
uniform float height_scale;
uniform vec2 half_size;
out vec3 v_n;
out vec2 v_uv;
void main() {
    float mapX = cell.x + pos.x;
    float mapZ = cell.y + pos.z;
    float fieldX = (mapX - mapZ) * half_size.x + half_size.x;
    float fieldY = (mapX + mapZ) * half_size.y;
    vec2 field = vec2(fieldX, fieldY - pos.y * height_scale);
    vec2 screen = (field * camera_zoom) + camera_offset;
    vec2 clip = vec2((screen.x / view_size.x) * 2.0 - 1.0, 1.0 - (screen.y / view_size.y) * 2.0);
    float bakedZ = fieldY + pos.y * height_scale;
    gl_Position = vec4(clip, (z_range.x - bakedZ) * z_range.y, 1.0);
    v_n = nrm;
    v_uv = uv;
}
)";

static const char* fs_glsl = R"(
#version 330
in vec3 v_n;
in vec2 v_uv;
out vec4 frag_color;
uniform vec4 sun_dir;
uniform float ambient;
uniform float diffuse;
uniform sampler2D tex;
void main() {
    vec3 n = normalize(v_n);
    float ndl = max(dot(n, sun_dir.xyz), 0.0);
    vec3 albedo = texture(tex, v_uv).rgb;
    frag_color = vec4(albedo * (ambient + diffuse * ndl), 1.0);
}
)";

static const char* vs_hlsl = R"(
cbuffer vs_params: register(b0) {
    float2 view_size;
    float2 camera_offset;
    float2 z_range;
    float camera_zoom;
    float height_scale;
    float2 half_size;
};
struct VSIn {
    float3 pos: TEXCOORD0;
    float3 nrm: TEXCOORD1;
    float2 uv: TEXCOORD2;
    float2 cell: TEXCOORD3;
};
struct VSOut {
    float4 pos: SV_Position;
    float3 nrm: TEXCOORD0;
    float2 uv: TEXCOORD1;
};
VSOut main(VSIn inp) {
    VSOut o;
    float mapX = inp.cell.x + inp.pos.x;
    float mapZ = inp.cell.y + inp.pos.z;
    float fieldX = (mapX - mapZ) * half_size.x + half_size.x;
    float fieldY = (mapX + mapZ) * half_size.y;
    float2 field = float2(fieldX, fieldY - inp.pos.y * height_scale);
    float2 screen = (field * camera_zoom) + camera_offset;
    float2 clip;
    clip.x = (screen.x / view_size.x) * 2.0 - 1.0;
    clip.y = 1.0 - (screen.y / view_size.y) * 2.0;
    float bakedZ = fieldY + inp.pos.y * height_scale;
    o.pos = float4(clip, (z_range.x - bakedZ) * z_range.y, 1.0);
    o.nrm = inp.nrm;
    o.uv = inp.uv;
    return o;
}
)";

static const char* fs_hlsl = R"(
cbuffer fs_params: register(b1) {
    float4 sun_dir;
    float ambient;
    float diffuse;
};
Texture2D tex0: register(t0);
SamplerState smp0: register(s0);
struct PSIn {
    float4 pos: SV_Position;
    float3 nrm: TEXCOORD0;
    float2 uv: TEXCOORD1;
};
float4 main(PSIn inp): SV_Target {
    float3 n = normalize(inp.nrm);
    float ndl = max(dot(n, sun_dir.xyz), 0.0);
    float3 albedo = tex0.Sample(smp0, inp.uv).rgb;
    return float4(albedo * (ambient + diffuse * ndl), 1.0);
}
)";

static const char* vs_msl = R"(
#include <metal_stdlib>
using namespace metal;
struct VsParams {
    float2 view_size;
    float2 camera_offset;
    float2 z_range;
    float camera_zoom;
    float height_scale;
    float2 half_size;
};
struct VSIn {
    float3 pos [[attribute(0)]];
    float3 nrm [[attribute(1)]];
    float2 uv [[attribute(2)]];
    float2 cell [[attribute(3)]];
};
struct VSOut {
    float4 pos [[position]];
    float3 nrm;
    float2 uv;
};
vertex VSOut _main(VSIn in [[stage_in]], constant VsParams& p [[buffer(0)]]) {
    VSOut o;
    float mapX = in.cell.x + in.pos.x;
    float mapZ = in.cell.y + in.pos.z;
    float fieldX = (mapX - mapZ) * p.half_size.x + p.half_size.x;
    float fieldY = (mapX + mapZ) * p.half_size.y;
    float2 field = float2(fieldX, fieldY - in.pos.y * p.height_scale);
    float2 screen = (field * p.camera_zoom) + p.camera_offset;
    float2 clip = float2((screen.x / p.view_size.x) * 2.0 - 1.0, 1.0 - (screen.y / p.view_size.y) * 2.0);
    float bakedZ = fieldY + in.pos.y * p.height_scale;
    o.pos = float4(clip, (p.z_range.x - bakedZ) * p.z_range.y, 1.0);
    o.nrm = in.nrm;
    o.uv = in.uv;
    return o;
}
)";

static const char* fs_msl = R"(
#include <metal_stdlib>
using namespace metal;
struct FsParams {
    float4 sun_dir;
    float ambient;
    float diffuse;
};
struct PSIn {
    float4 pos [[position]];
    float3 nrm;
    float2 uv;
};
fragment float4 _main(PSIn in [[stage_in]], constant FsParams& p [[buffer(0)]],
    texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {
    float3 n = normalize(in.nrm);
    float ndl = max(dot(n, p.sun_dir.xyz), 0.0);
    float3 albedo = tex.sample(smp, in.uv).rgb;
    return float4(albedo * (p.ambient + p.diffuse * ndl), 1.0);
}
)";

void fillVsUniforms(sg_shader_uniform_block* block) {
    block->stage = SG_SHADERSTAGE_VERTEX;
    block->size = sizeof(BuildingRenderer::VsParams);
    block->hlsl_register_b_n = 0;
    block->msl_buffer_n = 0;
    block->wgsl_group0_binding_n = 0;
    block->spirv_set0_binding_n = 0;
    block->glsl_uniforms[0].glsl_name = "view_size";
    block->glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT2;
    block->glsl_uniforms[1].glsl_name = "camera_offset";
    block->glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT2;
    block->glsl_uniforms[2].glsl_name = "z_range";
    block->glsl_uniforms[2].type = SG_UNIFORMTYPE_FLOAT2;
    block->glsl_uniforms[3].glsl_name = "camera_zoom";
    block->glsl_uniforms[3].type = SG_UNIFORMTYPE_FLOAT;
    block->glsl_uniforms[4].glsl_name = "height_scale";
    block->glsl_uniforms[4].type = SG_UNIFORMTYPE_FLOAT;
    block->glsl_uniforms[5].glsl_name = "half_size";
    block->glsl_uniforms[5].type = SG_UNIFORMTYPE_FLOAT2;
}

void fillFsUniforms(sg_shader_uniform_block* block) {
    block->stage = SG_SHADERSTAGE_FRAGMENT;
    block->size = sizeof(BuildingRenderer::FsParams);
    block->hlsl_register_b_n = 1;
    block->msl_buffer_n = 0;
    block->wgsl_group0_binding_n = 1;
    block->spirv_set0_binding_n = 1;
    block->glsl_uniforms[0].glsl_name = "sun_dir";
    block->glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;
    block->glsl_uniforms[1].glsl_name = "ambient";
    block->glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT;
    block->glsl_uniforms[2].glsl_name = "diffuse";
    block->glsl_uniforms[2].type = SG_UNIFORMTYPE_FLOAT;
}

} // namespace

void BuildingRenderer::init(sg_pixel_format depthFormat_) {
    depthFormat = depthFormat_;
    sg_sampler_desc smp_desc = {};
    smp_desc.min_filter = SG_FILTER_LINEAR;
    smp_desc.mag_filter = SG_FILTER_LINEAR;
    smp_desc.mipmap_filter = SG_FILTER_LINEAR;
    smp_desc.wrap_u = SG_WRAP_REPEAT;
    smp_desc.wrap_v = SG_WRAP_REPEAT;
    smp_desc.max_anisotropy = 8;
    smp_desc.label = "render-core-building-smp";
    smp = sg_make_sampler(&smp_desc);
    if (depthFormat != SG_PIXELFORMAT_NONE) {
        ensurePipeline();
    }
}

void BuildingRenderer::shutdown() {
    for (auto& [uuid, gpu] : assets) {
        destroyAsset(gpu);
    }
    assets.clear();
    destroyPipeline();
    if (smp.id != SG_INVALID_ID) {
        sg_destroy_sampler(smp);
        smp = {};
    }
}

void BuildingRenderer::ensureBuildingAsset(const std::string& assetUuid, const BuildingParams& params) {
    AssetGpu& gpu = assets[assetUuid];
    const bool same = gpu.params.modelPath == params.modelPath
        && gpu.params.albedoPath == params.albedoPath
        && gpu.params.footprintWidth == params.footprintWidth
        && gpu.params.footprintHeight == params.footprintHeight
        && gpu.params.heightScale == params.heightScale
        && gpu.params.yawDegrees == params.yawDegrees;
    gpu.params = params;
    if (!same) {
        destroyAsset(gpu);
        gpu.loaded = false;
        gpu.loadFailed = false;
    }
}

void BuildingRenderer::ensurePipeline() {
    if (pip.id != SG_INVALID_ID) return;

    sg_shader_desc shd_desc = {};
    if (sg_query_backend() == SG_BACKEND_D3D11) {
        shd_desc.vertex_func.source = vs_hlsl;
        shd_desc.fragment_func.source = fs_hlsl;
        for (int i = 0; i < 4; ++i) {
            shd_desc.attrs[i].hlsl_sem_name = "TEXCOORD";
            shd_desc.attrs[i].hlsl_sem_index = i;
        }
    } else if (sg_query_backend() == SG_BACKEND_METAL_MACOS) {
        shd_desc.vertex_func.source = vs_msl;
        shd_desc.fragment_func.source = fs_msl;
    } else {
        shd_desc.vertex_func.source = vs_glsl;
        shd_desc.fragment_func.source = fs_glsl;
    }
    fillVsUniforms(&shd_desc.uniform_blocks[0]);
    fillFsUniforms(&shd_desc.uniform_blocks[1]);
    shd_desc.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.views[0].texture.image_type = SG_IMAGETYPE_2D;
    shd_desc.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    shd_desc.views[0].texture.hlsl_register_t_n = 0;
    shd_desc.views[0].texture.msl_texture_n = 0;
    shd_desc.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    shd_desc.samplers[0].hlsl_register_s_n = 0;
    shd_desc.samplers[0].msl_sampler_n = 0;
    shd_desc.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.texture_sampler_pairs[0].view_slot = 0;
    shd_desc.texture_sampler_pairs[0].sampler_slot = 0;
    shd_desc.texture_sampler_pairs[0].glsl_name = "tex";
    shd = sg_make_shader(&shd_desc);

    sg_pipeline_desc pip_desc = {};
    pip_desc.shader = shd;
    pip_desc.layout.buffers[0].stride = sizeof(Vertex);
    pip_desc.layout.buffers[1].stride = sizeof(Instance);
    pip_desc.layout.buffers[1].step_func = SG_VERTEXSTEP_PER_INSTANCE;
    pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
    pip_desc.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT2;
    pip_desc.layout.attrs[3].format = SG_VERTEXFORMAT_FLOAT2;
    pip_desc.layout.attrs[3].buffer_index = 1;
    pip_desc.index_type = SG_INDEXTYPE_UINT32;
    pip_desc.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    pip_desc.depth.pixel_format = depthFormat;
    pip_desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    pip_desc.depth.write_enabled = true;
    pip_desc.cull_mode = SG_CULLMODE_NONE;
    pip_desc.face_winding = SG_FACEWINDING_CCW;
    pip_desc.label = "render-core-building-pip";
    pip = sg_make_pipeline(&pip_desc);
}

void BuildingRenderer::destroyPipeline() {
    if (pip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(pip);
        pip = {};
    }
    if (shd.id != SG_INVALID_ID) {
        sg_destroy_shader(shd);
        shd = {};
    }
}

void BuildingRenderer::destroyAsset(AssetGpu& gpu) {
    if (gpu.vbuf.id != SG_INVALID_ID) sg_destroy_buffer(gpu.vbuf);
    if (gpu.ibuf.id != SG_INVALID_ID) sg_destroy_buffer(gpu.ibuf);
    if (gpu.instBuf.id != SG_INVALID_ID) sg_destroy_buffer(gpu.instBuf);
    if (gpu.albedoView.id != SG_INVALID_ID) sg_destroy_view(gpu.albedoView);
    if (gpu.albedo.id != SG_INVALID_ID) sg_destroy_image(gpu.albedo);
    gpu.vbuf = {};
    gpu.ibuf = {};
    gpu.instBuf = {};
    gpu.albedoView = {};
    gpu.albedo = {};
    gpu.instBufSize = 0;
    gpu.indexCount = 0;
    gpu.loaded = false;
}

void BuildingRenderer::loadAsset(AssetGpu& gpu) {
    if (gpu.loaded || gpu.loadFailed) return;
    std::string error;
    GltfMesh mesh;
    if (!loadGltfMesh(gpu.params.modelPath, mesh, &error)) {
        spdlog::error("BuildingRenderer: {}", error);
        gpu.loadFailed = true;
        return;
    }
    fitGltfMeshToFootprint(mesh, static_cast<float>(gpu.params.footprintWidth),
        static_cast<float>(gpu.params.footprintHeight), gpu.params.yawDegrees);

    std::vector<Vertex> verts(mesh.vertices.size());
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        verts[i].pos[0] = mesh.vertices[i].pos.x;
        verts[i].pos[1] = mesh.vertices[i].pos.y;
        verts[i].pos[2] = mesh.vertices[i].pos.z;
        verts[i].nrm[0] = mesh.vertices[i].normal.x;
        verts[i].nrm[1] = mesh.vertices[i].normal.y;
        verts[i].nrm[2] = mesh.vertices[i].normal.z;
        verts[i].uv[0] = mesh.vertices[i].uv.x;
        verts[i].uv[1] = mesh.vertices[i].uv.y;
    }

    sg_buffer_desc vdesc = {};
    vdesc.usage.vertex_buffer = true;
    vdesc.data = {verts.data(), verts.size() * sizeof(Vertex)};
    vdesc.label = "render-core-building-vbuf";
    gpu.vbuf = sg_make_buffer(&vdesc);

    sg_buffer_desc idesc = {};
    idesc.usage.index_buffer = true;
    idesc.data = {mesh.indices.data(), mesh.indices.size() * sizeof(std::uint32_t)};
    idesc.label = "render-core-building-ibuf";
    gpu.ibuf = sg_make_buffer(&idesc);
    gpu.indexCount = static_cast<int>(mesh.indices.size());

    ImageRGBA8 img;
    try {
        if (!gpu.params.albedoPath.empty()) {
            img = loadImageRGBA8(gpu.params.albedoPath);
        }
    } catch (const std::exception& e) {
        spdlog::warn("BuildingRenderer: albedo '{}': {}", gpu.params.albedoPath.string(), e.what());
    }
    if (img.pixels.empty()) {
        img.width = 1;
        img.height = 1;
        img.pixels = {255, 255, 255, 255};
    }
    const std::vector<ImageRGBA8> levels = buildMipChain(img);
    sg_image_desc img_desc = {};
    img_desc.width = img.width;
    img_desc.height = img.height;
    img_desc.num_mipmaps = static_cast<int>(levels.size());
    img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    for (std::size_t i = 0; i < levels.size(); ++i) {
        img_desc.data.mip_levels[i].ptr = levels[i].pixels.data();
        img_desc.data.mip_levels[i].size = levels[i].pixels.size();
    }
    img_desc.label = "render-core-building-albedo";
    gpu.albedo = sg_make_image(&img_desc);
    sg_view_desc view = {};
    view.texture.image = gpu.albedo;
    gpu.albedoView = sg_make_view(&view);

    gpu.loaded = gpu.vbuf.id != SG_INVALID_ID && gpu.ibuf.id != SG_INVALID_ID && gpu.albedo.id != SG_INVALID_ID;
    if (!gpu.loaded) {
        gpu.loadFailed = true;
        destroyAsset(gpu);
        return;
    }
    spdlog::info("BuildingRenderer: loaded {} ({} verts, {} indices)", gpu.params.modelPath.string(),
        verts.size(), mesh.indices.size());
}

void BuildingRenderer::render(
    const std::vector<BuildingInstance>& instances,
    const topology_core::DiamondIsometry& iso,
    const topology_core::Camera2D& camera,
    int viewWidth,
    int viewHeight,
    const SceneStitchSettings& stitch) {

    if (pip.id == SG_INVALID_ID || instances.empty() || viewWidth <= 0 || viewHeight <= 0) return;

    std::unordered_map<std::string, std::vector<const BuildingInstance*>> groups;
    for (const BuildingInstance& inst : instances) {
        auto it = assets.find(inst.assetUuid);
        if (it == assets.end()) continue;
        groups[inst.assetUuid].push_back(&inst);
    }
    if (groups.empty()) return;

    const glm::vec2 cellSz = iso.dims.cellSize();
    const float groundCenterY = camera.screenToWorld({viewWidth * 0.5f, viewHeight * 0.5f}).y;
    VsParams vs{};
    vs.view_size[0] = static_cast<float>(viewWidth);
    vs.view_size[1] = static_cast<float>(viewHeight);
    vs.camera_offset[0] = camera.offset.x;
    vs.camera_offset[1] = camera.offset.y;
    vs.z_range[0] = groundCenterY + kZFarOffset;
    vs.z_range[1] = kZScale;
    vs.camera_zoom = camera.zoom;
    vs.half_size[0] = cellSz.x * 0.5f;
    vs.half_size[1] = cellSz.y * 0.5f;

    const glm::vec3 sun = stitch.sunDirection();
    FsParams fs{};
    fs.sun_dir[0] = sun.x;
    fs.sun_dir[1] = sun.y;
    fs.sun_dir[2] = sun.z;
    fs.ambient = stitch.ambient;
    fs.diffuse = stitch.diffuse;

    for (auto& [uuid, group] : groups) {
        AssetGpu& gpu = assets.find(uuid)->second;
        loadAsset(gpu);
        if (!gpu.loaded || gpu.indexCount <= 0) continue;

        vs.height_scale = gpu.params.heightScale;
        scratchInst.clear();
        scratchInst.reserve(group.size());
        for (const BuildingInstance* inst : group) {
            Instance i{};
            i.cell[0] = static_cast<float>(inst->cell.x);
            i.cell[1] = static_cast<float>(inst->cell.y);
            scratchInst.push_back(i);
        }
        const std::size_t bytes = scratchInst.size() * sizeof(Instance);
        if (gpu.instBufSize < bytes) {
            if (gpu.instBuf.id != SG_INVALID_ID) sg_destroy_buffer(gpu.instBuf);
            sg_buffer_desc bdesc = {};
            bdesc.size = std::max(bytes, std::size_t{256});
            bdesc.usage.vertex_buffer = true;
            bdesc.usage.dynamic_update = true;
            bdesc.label = "render-core-building-inst";
            gpu.instBuf = sg_make_buffer(&bdesc);
            gpu.instBufSize = bdesc.size;
        }
        if (gpu.instBuf.id == SG_INVALID_ID) continue;
        sg_update_buffer(gpu.instBuf, sg_range{scratchInst.data(), bytes});

        sg_apply_pipeline(pip);
        sg_bindings bind{};
        bind.vertex_buffers[0] = gpu.vbuf;
        bind.vertex_buffers[1] = gpu.instBuf;
        bind.index_buffer = gpu.ibuf;
        bind.views[0] = gpu.albedoView;
        bind.samplers[0] = smp;
        sg_apply_bindings(bind);
        sg_apply_uniforms(0, sg_range{&vs, sizeof(vs)});
        sg_apply_uniforms(1, sg_range{&fs, sizeof(fs)});
        sg_draw(0, gpu.indexCount, static_cast<int>(scratchInst.size()));
    }
}

} // namespace render_core
