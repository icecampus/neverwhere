#include "AtlasRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <spdlog/spdlog.h>

#include "RaisedGeometry.h"
#include "RockWalls.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace {

static const char* kTexVsGlsl = R"(
#version 330
layout(location=0) in vec2 pos;
layout(location=1) in vec2 uv;
out vec2 v_uv;
uniform vec2 view_size;
uniform vec2 camera_offset;
uniform float camera_zoom;
void main() {
    vec2 screen = (pos * camera_zoom) + camera_offset;
    vec2 clip = vec2((screen.x / view_size.x) * 2.0 - 1.0, 1.0 - (screen.y / view_size.y) * 2.0);
    gl_Position = vec4(clip, 0.0, 1.0);
    v_uv = uv;
}
)";

static const char* kTexFsGlsl = R"(
#version 330
in vec2 v_uv;
out vec4 frag_color;
uniform sampler2D atlas_tex;
void main() {
    frag_color = texture(atlas_tex, v_uv);
    if (frag_color.a < 0.05) discard;
}
)";

static const char* kColorVsGlsl = R"(
#version 330
layout(location=0) in vec2 pos;
layout(location=1) in vec4 color;
out vec4 v_color;
uniform vec2 view_size;
uniform vec2 camera_offset;
uniform float camera_zoom;
void main() {
    vec2 screen = (pos * camera_zoom) + camera_offset;
    vec2 clip = vec2((screen.x / view_size.x) * 2.0 - 1.0, 1.0 - (screen.y / view_size.y) * 2.0);
    gl_Position = vec4(clip, 0.0, 1.0);
    v_color = color;
}
)";

static const char* kColorFsGlsl = R"(
#version 330
in vec4 v_color;
out vec4 frag_color;
void main() {
    frag_color = v_color;
}
)";

static const char* kTexVsHlsl = R"(
cbuffer vs_params: register(b0) {
    float2 view_size;
    float2 camera_offset;
    float camera_zoom;
    float3 _pad0;
};
struct VSIn {
    float2 pos: TEXCOORD0;
    float2 uv: TEXCOORD1;
};
struct VSOut {
    float4 pos: SV_Position;
    float2 uv: TEXCOORD0;
};
VSOut main(VSIn inp) {
    VSOut o;
    float2 screen = (inp.pos * camera_zoom) + camera_offset;
    float2 clip;
    clip.x = (screen.x / view_size.x) * 2.0 - 1.0;
    clip.y = 1.0 - (screen.y / view_size.y) * 2.0;
    o.pos = float4(clip, 0.0, 1.0);
    o.uv = inp.uv;
    return o;
}
)";

static const char* kTexFsHlsl = R"(
Texture2D atlas_tex: register(t0);
SamplerState smp: register(s0);
struct PSIn {
    float4 pos: SV_Position;
    float2 uv: TEXCOORD0;
};
float4 main(PSIn inp): SV_Target {
    float4 c = atlas_tex.Sample(smp, inp.uv);
    if (c.a < 0.05) discard;
    return c;
}
)";

static const char* kColorVsHlsl = R"(
cbuffer vs_params: register(b0) {
    float2 view_size;
    float2 camera_offset;
    float camera_zoom;
    float3 _pad0;
};
struct VSIn {
    float2 pos: TEXCOORD0;
    float4 color: TEXCOORD1;
};
struct VSOut {
    float4 pos: SV_Position;
    float4 color: TEXCOORD0;
};
VSOut main(VSIn inp) {
    VSOut o;
    float2 screen = (inp.pos * camera_zoom) + camera_offset;
    float2 clip;
    clip.x = (screen.x / view_size.x) * 2.0 - 1.0;
    clip.y = 1.0 - (screen.y / view_size.y) * 2.0;
    o.pos = float4(clip, 0.0, 1.0);
    o.color = inp.color;
    return o;
}
)";

static const char* kColorFsHlsl = R"(
struct PSIn {
    float4 pos: SV_Position;
    float4 color: TEXCOORD0;
};
float4 main(PSIn inp): SV_Target {
    return inp.color;
}
)";

void fillVsUniformDesc(sg_shader_uniform_block* block) {
    block->stage = SG_SHADERSTAGE_VERTEX;
    block->size = sizeof(AtlasRenderer::VsParams);
    block->hlsl_register_b_n = 0;
    block->msl_buffer_n = 0;
    block->wgsl_group0_binding_n = 0;
    block->spirv_set0_binding_n = 0;
    block->glsl_uniforms[0].glsl_name = "view_size";
    block->glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT2;
    block->glsl_uniforms[1].glsl_name = "camera_offset";
    block->glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT2;
    block->glsl_uniforms[2].glsl_name = "camera_zoom";
    block->glsl_uniforms[2].type = SG_UNIFORMTYPE_FLOAT;
}

} // namespace

void AtlasRenderer::init() {
    ensurePipelines();

    sg_buffer_desc texBuf = {};
    texBuf.size = 6 * 65536 * sizeof(TexVertex);
    texBuf.usage.dynamic_update = true;
    texBuf.label = "tileshape-tex-vbuf";
    m_texVbuf = sg_make_buffer(&texBuf);

    sg_buffer_desc colorBuf = {};
    colorBuf.size = 8 * 65536 * sizeof(ColorVertex);
    colorBuf.usage.dynamic_update = true;
    colorBuf.label = "tileshape-color-vbuf";
    m_colorVbuf = sg_make_buffer(&colorBuf);

    sg_sampler_desc smp = {};
    smp.min_filter = SG_FILTER_LINEAR;
    smp.mag_filter = SG_FILTER_LINEAR;
    smp.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    smp.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    smp.label = "tileshape-atlas-smp";
    m_sampler = sg_make_sampler(&smp);

    m_ready = true;
}

void AtlasRenderer::shutdown() {
    destroyPipelines();
    if (m_texVbuf.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_texVbuf);
        m_texVbuf = {};
    }
    if (m_colorVbuf.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_colorVbuf);
        m_colorVbuf = {};
    }
    if (m_sampler.id != SG_INVALID_ID) {
        sg_destroy_sampler(m_sampler);
        m_sampler = {};
    }
    destroySlot(m_slots[0]);
    destroySlot(m_slots[1]);
    m_ready = false;
}

void AtlasRenderer::destroySlot(AtlasSlot& slot) {
    if (slot.view.id != SG_INVALID_ID) {
        sg_destroy_view(slot.view);
        slot.view = {};
    }
    if (slot.image.id != SG_INVALID_ID) {
        sg_destroy_image(slot.image);
        slot.image = {};
    }
}

bool AtlasRenderer::uploadSlot(
    AtlasSlot& slot,
    const void* rgba,
    int width,
    int height,
    const char* label) {

    destroySlot(slot);

    sg_image_desc desc = {};
    desc.width = width;
    desc.height = height;
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.data.mip_levels[0].ptr = rgba;
    desc.data.mip_levels[0].size = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    desc.label = label;
    slot.image = sg_make_image(&desc);
    if (slot.image.id == SG_INVALID_ID) {
        spdlog::error("AtlasRenderer: sg_make_image failed ({})", label ? label : "?");
        return false;
    }

    sg_view_desc viewDesc = {};
    viewDesc.texture.image = slot.image;
    slot.view = sg_make_view(&viewDesc);
    return slot.view.id != SG_INVALID_ID;
}

bool AtlasRenderer::loadAtlasFromFile(AtlasKind kind, const std::string& path, int cols, int rows) {
    m_atlasCols = cols;
    m_atlasRows = rows;

    int w = 0, h = 0, comp = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &comp, 4);
    if (!pixels || w <= 0 || h <= 0) {
        spdlog::error("AtlasRenderer: failed to load '{}': {}", path, stbi_failure_reason());
        if (pixels) {
            stbi_image_free(pixels);
        }
        return false;
    }

    const char* label = (kind == AtlasKind::Grass) ? "grass-atlas" : "flat-atlas-file";
    const bool ok = uploadSlot(m_slots[static_cast<int>(kind)], pixels, w, h, label);
    stbi_image_free(pixels);

    if (ok) {
        spdlog::info("AtlasRenderer: loaded {} into slot {} ({}x{}, {}x{} tiles)",
            path,
            static_cast<int>(kind),
            w,
            h,
            cols,
            rows);
    }
    return ok;
}

bool AtlasRenderer::loadAtlasFromRgba(
    AtlasKind kind,
    const std::uint8_t* rgba,
    int width,
    int height,
    int cols,
    int rows) {

    if (!rgba || width <= 0 || height <= 0) {
        return false;
    }

    m_atlasCols = cols;
    m_atlasRows = rows;
    const char* label = (kind == AtlasKind::Flat) ? "flat-atlas" : "rgba-atlas";
    const bool ok = uploadSlot(m_slots[static_cast<int>(kind)], rgba, width, height, label);
    if (ok) {
        spdlog::info("AtlasRenderer: uploaded RGBA slot {} ({}x{}, {}x{} tiles)",
            static_cast<int>(kind),
            width,
            height,
            cols,
            rows);
    }
    return ok;
}

glm::vec4 AtlasRenderer::atlasUvRect(int tileIndex) const {
    if (tileIndex < 0) {
        return {};
    }
    const int col = tileIndex % m_atlasCols;
    const int row = tileIndex / m_atlasCols;
    const float u0 = static_cast<float>(col) / static_cast<float>(m_atlasCols);
    const float v0 = static_cast<float>(row) / static_cast<float>(m_atlasRows);
    const float u1 = static_cast<float>(col + 1) / static_cast<float>(m_atlasCols);
    const float v1 = static_cast<float>(row + 1) / static_cast<float>(m_atlasRows);
    return {u0, v0, u1, v1};
}

void AtlasRenderer::appendTileQuad(
    std::vector<TexVertex>& out,
    const DiamondIso& iso,
    glm::ivec2 cell,
    int tileIndex,
    float yOffset) {

    // Atlas tiles are square frames containing a 2:1 isometric diamond base
    // (plus grass sticking up). Display as a SQUARE of side cellWidth so the
    // embedded 2:1 base matches the cell diamond (height = cellWidth/2).
    // Using cellSize (cellWidth x cellHeight) squashes that base 2x on Y.
    const glm::vec4 uv = atlasUvRect(tileIndex);
    const glm::vec2 center = iso.mapToField(cell);
    const float side = iso.dims.cellWidth;
    const float half = side * 0.5f;

    const float x0 = center.x - half;
    const float x1 = center.x + half;
    const float y0 = center.y - half + yOffset;
    const float y1 = center.y + half + yOffset;

    const TexVertex tl{x0, y0, uv.x, uv.y};
    const TexVertex tr{x1, y0, uv.z, uv.y};
    const TexVertex br{x1, y1, uv.z, uv.w};
    const TexVertex bl{x0, y1, uv.x, uv.w};

    out.push_back(tl);
    out.push_back(tr);
    out.push_back(br);
    out.push_back(tl);
    out.push_back(br);
    out.push_back(bl);
}

void AtlasRenderer::appendDiamondOutline(
    std::vector<ColorVertex>& out,
    const DiamondIso& iso,
    glm::ivec2 cell,
    glm::vec4 color) {

    const auto corners = iso.cellDiamondCorners(cell);
    for (int i = 0; i < 4; ++i) {
        const glm::vec2 a = corners[i];
        const glm::vec2 b = corners[(i + 1) % 4];
        out.push_back({a.x, a.y, color.r, color.g, color.b, color.a});
        out.push_back({b.x, b.y, color.r, color.g, color.b, color.a});
    }
}

void AtlasRenderer::appendNodeMarker(
    std::vector<ColorVertex>& out,
    const DiamondIso& iso,
    glm::ivec2 node,
    glm::vec4 color,
    float yOffset) {

    const glm::vec2 p = iso.nodeToField(node);
    const float s = 6.0f;
    const glm::vec2 pts[4] = {
        {p.x - s, p.y + yOffset},
        {p.x, p.y - s * 0.5f + yOffset},
        {p.x + s, p.y + yOffset},
        {p.x, p.y + s * 0.5f + yOffset},
    };
    for (int i = 0; i < 4; ++i) {
        out.push_back({pts[i].x, pts[i].y, color.r, color.g, color.b, color.a});
        out.push_back({pts[(i + 1) % 4].x, pts[(i + 1) % 4].y, color.r, color.g, color.b, color.a});
    }
}

void AtlasRenderer::appendWallTriangles(
    std::vector<ColorVertex>& out,
    const DiamondIso& iso,
    glm::ivec2 cell,
    const std::array<bool, 4>& mask,
    float height) {

    // Vertical cliff under each contour segment: top edge follows the land
    // contour lifted by `height`, bottom edge is the same segment at ground.
    // Two brightness levels by grid axis fake iso lighting; bottom is darker.
    const auto segments = cellContourSegments(iso, cell, mask);
    for (const ContourSegment& seg : segments) {
        const glm::vec3 base = (seg.axis == 0)
            ? glm::vec3(0.62f, 0.45f, 0.22f)
            : glm::vec3(0.45f, 0.32f, 0.16f);
        const glm::vec4 top{base, 1.0f};
        const glm::vec4 bottom{base * 0.7f, 1.0f};

        const ColorVertex t0{seg.edgeMid.x, seg.edgeMid.y - height, top.r, top.g, top.b, top.a};
        const ColorVertex t1{seg.center.x, seg.center.y - height, top.r, top.g, top.b, top.a};
        const ColorVertex b0{seg.edgeMid.x, seg.edgeMid.y, bottom.r, bottom.g, bottom.b, bottom.a};
        const ColorVertex b1{seg.center.x, seg.center.y, bottom.r, bottom.g, bottom.b, bottom.a};

        out.push_back(t0);
        out.push_back(b0);
        out.push_back(b1);
        out.push_back(t0);
        out.push_back(b1);
        out.push_back(t1);
    }
}

void AtlasRenderer::render(
    const PaintLayerView* layers,
    int layerCount,
    const DiamondIso& iso,
    const topology_core::Camera2D& camera,
    int viewW,
    int viewH,
    glm::ivec2 hoverNode,
    bool hasHover,
    float raisedHeight,
    bool hoverRaised,
    const RockWallParams* rockWalls) {

    if (!m_ready || m_texPip.id == SG_INVALID_ID || !layers || layerCount <= 0 || !layers[0].brush) {
        return;
    }

    const int mapW = layers[0].brush->width();
    const int mapH = layers[0].brush->height();

    std::vector<std::pair<std::uint64_t, glm::ivec2>> drawOrder;
    drawOrder.reserve(static_cast<std::size_t>(mapW * mapH));
    for (int y = 0; y < mapH; ++y) {
        for (int x = 0; x < mapW; ++x) {
            drawOrder.emplace_back(iso.zOffset({x, y}), glm::ivec2{x, y});
        }
    }
    std::sort(drawOrder.begin(), drawOrder.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    // Every palette layer paints into the shared buffers; textured ranges are
    // drawn with the layer's own atlas. Raised tops reuse the same atlas quads
    // with a -height offset, so their contour matches the flat tiles exactly.
    struct TexRange {
        int base = 0;
        int count = 0;
        AtlasKind atlas = AtlasKind::Grass;
    };
    std::vector<TexRange> flatRanges;
    std::vector<TexRange> topRanges;
    std::vector<TexVertex> texVerts;
    texVerts.reserve(static_cast<std::size_t>(mapW * mapH * 6 * layerCount));

    std::vector<ColorVertex> wallVerts;
    wallVerts.reserve(static_cast<std::size_t>(mapW * mapH * 12));

    for (int li = 0; li < layerCount; ++li) {
        const PaintLayerView& layer = layers[li];
        if (!layer.brush || layer.raised) {
            continue;
        }
        TexRange range;
        range.base = static_cast<int>(texVerts.size());
        range.atlas = layer.atlas;
        for (const auto& [z, cell] : drawOrder) {
            (void)z;
            const auto type = layer.brush->cellTypeAt(cell);
            if (!landscape_core::tileTypeHasSurface(type)) {
                continue;
            }
            const int idx = LandBrush::atlasIndexByType(type);
            if (idx >= 0) {
                appendTileQuad(texVerts, iso, cell, idx);
            }
        }
        range.count = static_cast<int>(texVerts.size()) - range.base;
        if (range.count > 0) {
            flatRanges.push_back(range);
        }
    }

    for (int li = 0; li < layerCount; ++li) {
        const PaintLayerView& layer = layers[li];
        if (!layer.brush || !layer.raised) {
            continue;
        }
        if (rockWalls) {
            // Production-style rock cliffs: global contour chains with bevels,
            // noise-displaced terraces and baked colors.
            RockWallBuild wb = buildRockWalls(*layer.brush, raisedHeight, *rockWalls);
            wallVerts.insert(wallVerts.end(), wb.verts.begin(), wb.verts.end());
        } else {
            for (const auto& [z, cell] : drawOrder) {
                (void)z;
                const auto type = layer.brush->cellTypeAt(cell);
                if (!landscape_core::tileTypeHasSurface(type)) {
                    continue;
                }
                appendWallTriangles(wallVerts, iso, cell, layer.brush->nodeMaskAt(cell), raisedHeight);
            }
        }

        TexRange range;
        range.base = static_cast<int>(texVerts.size());
        range.atlas = layer.atlas;
        for (const auto& [z, cell] : drawOrder) {
            (void)z;
            const auto type = layer.brush->cellTypeAt(cell);
            if (!landscape_core::tileTypeHasSurface(type)) {
                continue;
            }
            const int idx = LandBrush::atlasIndexByType(type);
            if (idx >= 0) {
                appendTileQuad(texVerts, iso, cell, idx, -raisedHeight);
            }
        }
        range.count = static_cast<int>(texVerts.size()) - range.base;
        if (range.count > 0) {
            topRanges.push_back(range);
        }
    }

    std::vector<ColorVertex> lineVerts;
    lineVerts.reserve(static_cast<std::size_t>(mapW * mapH * 8 + 16));

    const glm::vec4 gridColor{0.45f, 0.48f, 0.52f, 0.55f};
    for (int y = 0; y < mapH; ++y) {
        for (int x = 0; x < mapW; ++x) {
            appendDiamondOutline(lineVerts, iso, {x, y}, gridColor);
        }
    }

    if (hasHover) {
        appendNodeMarker(
            lineVerts,
            iso,
            hoverNode,
            {1.0f, 0.25f, 0.2f, 1.0f},
            hoverRaised ? -raisedHeight : 0.0f);
    }

    VsParams vsParams{};
    vsParams.view_size[0] = static_cast<float>(viewW);
    vsParams.view_size[1] = static_cast<float>(viewH);
    vsParams.camera_offset[0] = camera.offset.x;
    vsParams.camera_offset[1] = camera.offset.y;
    vsParams.camera_zoom = camera.zoom;

    if (!texVerts.empty()) {
        sg_update_buffer(m_texVbuf, sg_range{texVerts.data(), texVerts.size() * sizeof(TexVertex)});
    }

    // Painter order: flat ground layers, then cliff walls, then raised tops.
    const auto drawTexRange = [&](const TexRange& range) {
        const AtlasSlot& slot = m_slots[static_cast<int>(range.atlas)];
        if (slot.view.id == SG_INVALID_ID) {
            return;
        }
        sg_bindings bind{};
        bind.vertex_buffers[0] = m_texVbuf;
        bind.views[0] = slot.view;
        bind.samplers[0] = m_sampler;

        sg_apply_pipeline(m_texPip);
        sg_apply_bindings(&bind);
        sg_apply_uniforms(0, sg_range{&vsParams, sizeof(vsParams)});
        sg_draw(range.base, range.count, 1);
    };

    for (const TexRange& range : flatRanges) {
        drawTexRange(range);
    }

    std::vector<ColorVertex> colorVerts;
    colorVerts.reserve(wallVerts.size() + lineVerts.size());
    colorVerts.insert(colorVerts.end(), wallVerts.begin(), wallVerts.end());
    colorVerts.insert(colorVerts.end(), lineVerts.begin(), lineVerts.end());
    if (!colorVerts.empty()) {
        sg_update_buffer(m_colorVbuf, sg_range{colorVerts.data(), colorVerts.size() * sizeof(ColorVertex)});
    }

    if (!wallVerts.empty()) {
        sg_bindings bind{};
        bind.vertex_buffers[0] = m_colorVbuf;

        sg_apply_pipeline(m_wallPip);
        sg_apply_bindings(&bind);
        sg_apply_uniforms(0, sg_range{&vsParams, sizeof(vsParams)});
        sg_draw(0, static_cast<int>(wallVerts.size()), 1);
    }

    for (const TexRange& range : topRanges) {
        drawTexRange(range);
    }

    if (!lineVerts.empty()) {
        sg_bindings bind{};
        bind.vertex_buffers[0] = m_colorVbuf;

        sg_apply_pipeline(m_colorPip);
        sg_apply_bindings(&bind);
        sg_apply_uniforms(0, sg_range{&vsParams, sizeof(vsParams)});
        sg_draw(static_cast<int>(wallVerts.size()), static_cast<int>(lineVerts.size()), 1);
    }
}

void AtlasRenderer::ensurePipelines() {
    if (m_texPip.id != SG_INVALID_ID) {
        return;
    }

    {
        sg_shader_desc shd = {};
#if defined(SOKOL_D3D11)
        shd.vertex_func.source = kTexVsHlsl;
        shd.fragment_func.source = kTexFsHlsl;
        shd.attrs[0].hlsl_sem_name = "TEXCOORD";
        shd.attrs[0].hlsl_sem_index = 0;
        shd.attrs[1].hlsl_sem_name = "TEXCOORD";
        shd.attrs[1].hlsl_sem_index = 1;
#else
        shd.vertex_func.source = kTexVsGlsl;
        shd.fragment_func.source = kTexFsGlsl;
#endif
        fillVsUniformDesc(&shd.uniform_blocks[0]);

        shd.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
        shd.views[0].texture.image_type = SG_IMAGETYPE_2D;
        shd.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
        shd.views[0].texture.hlsl_register_t_n = 0;
        shd.views[0].texture.msl_texture_n = 0;
        shd.views[0].texture.wgsl_group1_binding_n = 0;
        shd.views[0].texture.spirv_set1_binding_n = 0;

        shd.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
        shd.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
        shd.samplers[0].hlsl_register_s_n = 0;
        shd.samplers[0].msl_sampler_n = 0;
        shd.samplers[0].wgsl_group1_binding_n = 1;
        shd.samplers[0].spirv_set1_binding_n = 1;

        shd.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
        shd.texture_sampler_pairs[0].view_slot = 0;
        shd.texture_sampler_pairs[0].sampler_slot = 0;
        shd.texture_sampler_pairs[0].glsl_name = "atlas_tex";

        m_texShd = sg_make_shader(&shd);

        sg_pipeline_desc pip = {};
        pip.shader = m_texShd;
        pip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
        pip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
        pip.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
        pip.colors[0].blend.enabled = true;
        pip.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        pip.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        pip.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
        pip.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        pip.label = "tileshape-tex-pip";
        m_texPip = sg_make_pipeline(&pip);
    }

    {
        sg_shader_desc shd = {};
#if defined(SOKOL_D3D11)
        shd.vertex_func.source = kColorVsHlsl;
        shd.fragment_func.source = kColorFsHlsl;
        shd.attrs[0].hlsl_sem_name = "TEXCOORD";
        shd.attrs[0].hlsl_sem_index = 0;
        shd.attrs[1].hlsl_sem_name = "TEXCOORD";
        shd.attrs[1].hlsl_sem_index = 1;
#else
        shd.vertex_func.source = kColorVsGlsl;
        shd.fragment_func.source = kColorFsGlsl;
#endif
        fillVsUniformDesc(&shd.uniform_blocks[0]);
        m_colorShd = sg_make_shader(&shd);

        sg_pipeline_desc pip = {};
        pip.shader = m_colorShd;
        pip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
        pip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4;
        pip.primitive_type = SG_PRIMITIVETYPE_LINES;
        pip.colors[0].blend.enabled = true;
        pip.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        pip.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        pip.label = "tileshape-color-pip";
        m_colorPip = sg_make_pipeline(&pip);

        // Same vertex format as the line pipeline, but triangles — cliff walls.
        sg_pipeline_desc wallPip = {};
        wallPip.shader = m_colorShd;
        wallPip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
        wallPip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4;
        wallPip.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
        wallPip.label = "tileshape-wall-pip";
        m_wallPip = sg_make_pipeline(&wallPip);
    }
}

void AtlasRenderer::destroyPipelines() {
    if (m_texPip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_texPip);
        m_texPip = {};
    }
    if (m_colorPip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_colorPip);
        m_colorPip = {};
    }
    if (m_wallPip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_wallPip);
        m_wallPip = {};
    }
    if (m_texShd.id != SG_INVALID_ID) {
        sg_destroy_shader(m_texShd);
        m_texShd = {};
    }
    if (m_colorShd.id != SG_INVALID_ID) {
        sg_destroy_shader(m_colorShd);
        m_colorShd = {};
    }
}
