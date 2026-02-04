#include "SplattingRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <spdlog/spdlog.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace {

// =============================================
// GLSL Shaders (OpenGL Core 3.3)
// =============================================
static const char* vs_src_glsl = R"(
#version 330
layout(location=0) in vec2 pos;
layout(location=1) in vec2 cellCoord;
layout(location=2) in vec2 localNorm;

out vec2 v_worldPos;
out vec2 v_cellCoord;
out vec2 v_localNorm;

uniform vec2 view_size;
uniform vec2 camera_offset;
uniform float camera_zoom;

void main() {
    vec2 screen = (pos * camera_zoom) + camera_offset;
    vec2 clip_pos = vec2(
        (screen.x / view_size.x) * 2.0 - 1.0,
        1.0 - (screen.y / view_size.y) * 2.0
    );
    gl_Position = vec4(clip_pos, 0.0, 1.0);
    v_worldPos = pos;
    v_cellCoord = cellCoord;
    v_localNorm = localNorm;
}
)";

static const char* fs_src_glsl = R"(
#version 330
in vec2 v_worldPos;
in vec2 v_cellCoord;
in vec2 v_localNorm;
out vec4 frag_color;

uniform sampler2D tex0;       // Material 0 (Grass)
uniform sampler2D tex1;       // Material 1 (Sand)
uniform sampler2D tex2;       // Material 2 (Rock)
uniform sampler2D tex3;       // Material 3 (Reserved)
uniform sampler2D noise_tex;
uniform sampler2D material_id_map;

uniform vec2 cell_size;
uniform vec2 map_size;
uniform float blend_sharpness;
uniform float noise_scale;
uniform float tile_scale;
uniform float macro_scale;
uniform float macro_strength;
uniform float height_influence;
uniform float edge_darkness;
uniform float edge_width;
uniform float world_uv_scale;
uniform float random_uv_strength;
uniform int uv_mode;
uniform int debug_mode;

// Pseudo-random hash for per-tile UV randomization
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

vec2 hash2(vec2 p) {
    return vec2(hash(p), hash(p + vec2(13.7, 91.1)));
}

// Get material ID at cell coordinates (clamped)
float getMaterialId(vec2 cellCoord) {
    vec2 uv = (cellCoord + 0.5) / map_size;
    uv = clamp(uv, vec2(0.0), vec2(1.0));
    return texture(material_id_map, uv).r * 255.0;
}

// Convert material ID to texture slot index (0-3)
int matIdToSlot(float matId) {
    int id = int(matId + 0.5);
    if (id == 0) return 0;  // Empty renders as slot 0
    return clamp(id - 1, 0, 3);
}

// Sample a material texture by slot
vec4 sampleMaterial(int slot, vec2 uv) {
    if (slot == 0) return texture(tex0, uv);
    if (slot == 1) return texture(tex1, uv);
    if (slot == 2) return texture(tex2, uv);
    return texture(tex3, uv);
}

// Get neighbor cell coords for staggered isometry
// In staggered grid, 4 diagonal neighbors depend on row parity
vec2 getNeighborTL(vec2 cell) {
    float isOdd = mod(cell.y, 2.0);
    return vec2(cell.x - 1.0 + isOdd, cell.y - 1.0);
}
vec2 getNeighborTR(vec2 cell) {
    float isOdd = mod(cell.y, 2.0);
    return vec2(cell.x + isOdd, cell.y - 1.0);
}
vec2 getNeighborBL(vec2 cell) {
    float isOdd = mod(cell.y, 2.0);
    return vec2(cell.x - 1.0 + isOdd, cell.y + 1.0);
}
vec2 getNeighborBR(vec2 cell) {
    float isOdd = mod(cell.y, 2.0);
    return vec2(cell.x + isOdd, cell.y + 1.0);
}

void main() {
    vec2 cellCoord = floor(v_cellCoord + 0.5); // round to nearest int
    vec2 local = v_localNorm;
    
    // Get material IDs for current cell and 4 diagonal neighbors (staggered)
    float matC  = getMaterialId(cellCoord);
    float matTL = getMaterialId(getNeighborTL(cellCoord));
    float matTR = getMaterialId(getNeighborTR(cellCoord));
    float matBL = getMaterialId(getNeighborBL(cellCoord));
    float matBR = getMaterialId(getNeighborBR(cellCoord));
    
    // Calculate UV based on mode
    vec2 uv;
    if (uv_mode == 0) {
        // WorldUV: continuous world-space UV
        uv = v_worldPos / world_uv_scale;
    } else {
        // RandomTileUV: per-tile randomized offset
        vec2 rnd = hash2(cellCoord) * 2.0 - 1.0;
        vec2 offset = rnd * random_uv_strength;
        uv = (local + 0.5) + offset;
    }
    
    // Apply macro variation (UV warp from noise)
    float n0 = texture(noise_tex, uv * macro_scale).r;
    float n1 = texture(noise_tex, uv * (macro_scale * 0.77) + vec2(13.1, 7.7)).r;
    uv = uv + (vec2(n0, n1) - 0.5) * macro_strength;
    uv *= tile_scale;
    
    // Sample noise for blending variation
    float noiseVal = texture(noise_tex, v_worldPos / cell_size.x * noise_scale * 0.1).r;
    
    // Diamond edge distances (boundary is |x|+|y|=0.5)
    // Distance to each edge (positive inside, 0 at edge)
    float distTL = 0.5 - (-local.x - local.y);  // top-left edge (toward top vertex)
    float distTR = 0.5 - ( local.x - local.y);  // top-right edge (toward right vertex)
    float distBL = 0.5 - (-local.x + local.y);  // bottom-left edge (toward left vertex)
    float distBR = 0.5 - ( local.x + local.y);  // bottom-right edge (toward bottom vertex)
    
    // Corner proximity - distance to each diamond vertex
    float distToTop    = length(local - vec2(0.0, -0.5));
    float distToRight  = length(local - vec2(0.5, 0.0));
    float distToBottom = length(local - vec2(0.0, 0.5));
    float distToLeft   = length(local - vec2(-0.5, 0.0));
    
    // Corner proximity factor (higher near vertices, enables smoother corner blending)
    float minCornerDist = min(min(distToTop, distToBottom), min(distToLeft, distToRight));
    float cornerProximity = 1.0 - smoothstep(0.0, 0.5, minCornerDist);
    
    // Adaptive blend width: wider near corners for smoother transitions
    float baseBlendWidth = mix(0.35, 0.08, blend_sharpness);
    float edgeBlendWidth = baseBlendWidth * (1.0 + cornerProximity * 0.6);
    
    // Add noise jitter to blend threshold
    float noiseJitter = (noiseVal - 0.5) * (1.0 - blend_sharpness) * 0.2;
    
    // Edge blend factors: how much each neighbor should contribute
    // Using smoothstep for smooth falloff from edge toward center
    float bTL = 1.0 - smoothstep(0.0, edgeBlendWidth, distTL + noiseJitter);
    float bTR = 1.0 - smoothstep(0.0, edgeBlendWidth, distTR + noiseJitter);
    float bBL = 1.0 - smoothstep(0.0, edgeBlendWidth, distBL + noiseJitter);
    float bBR = 1.0 - smoothstep(0.0, edgeBlendWidth, distBR + noiseJitter);
    
    // Sample materials
    int slotC  = matIdToSlot(matC);
    int slotTL = matIdToSlot(matTL);
    int slotTR = matIdToSlot(matTR);
    int slotBL = matIdToSlot(matBL);
    int slotBR = matIdToSlot(matBR);
    
    vec4 colC  = sampleMaterial(slotC, uv);
    vec4 colTL = sampleMaterial(slotTL, uv);
    vec4 colTR = sampleMaterial(slotTR, uv);
    vec4 colBL = sampleMaterial(slotBL, uv);
    vec4 colBR = sampleMaterial(slotBR, uv);
    
    // Height proxy from albedo luminance for height-aware blending
    float hC  = dot(colC.rgb, vec3(0.333));
    float hTL = dot(colTL.rgb, vec3(0.333));
    float hTR = dot(colTR.rgb, vec3(0.333));
    float hBL = dot(colBL.rgb, vec3(0.333));
    float hBR = dot(colBR.rgb, vec3(0.333));
    
    // Height-aware adjustment: boost blend factor for "higher" neighbors
    bTL *= 1.0 + (hTL - hC) * height_influence;
    bTR *= 1.0 + (hTR - hC) * height_influence;
    bBL *= 1.0 + (hBL - hC) * height_influence;
    bBR *= 1.0 + (hBR - hC) * height_influence;
    
    // Clamp blend factors
    bTL = clamp(bTL, 0.0, 1.0);
    bTR = clamp(bTR, 0.0, 1.0);
    bBL = clamp(bBL, 0.0, 1.0);
    bBR = clamp(bBR, 0.0, 1.0);
    
    // Zero out blend factors for neighbors with same material as center
    if (slotTL == slotC) bTL = 0.0;
    if (slotTR == slotC) bTR = 0.0;
    if (slotBL == slotC) bBL = 0.0;
    if (slotBR == slotC) bBR = 0.0;
    
    // WEIGHTED BLENDING
    // Sum of neighbor blend factors determines how much we blend away from center
    float sumNeighborBlend = bTL + bTR + bBL + bBR;
    
    // Center weight: 1 when no neighbors blend, decreases as neighbors contribute
    // Clamp sumNeighborBlend to prevent center from going negative
    float wC = max(0.0, 1.0 - sumNeighborBlend);
    
    // Neighbor weights are their blend factors
    float wTL = bTL;
    float wTR = bTR;
    float wBL = bBL;
    float wBR = bBR;
    
    // Normalize weights to sum to 1
    float totalWeight = wC + wTL + wTR + wBL + wBR;
    if (totalWeight > 0.001) {
        float invTotal = 1.0 / totalWeight;
        wC  *= invTotal;
        wTL *= invTotal;
        wTR *= invTotal;
        wBL *= invTotal;
        wBR *= invTotal;
    } else {
        // Fallback: just use center material
        wC = 1.0;
        wTL = wTR = wBL = wBR = 0.0;
    }
    
    // Final weighted blend of all 5 materials
    vec4 col = colC * wC + colTL * wTL + colTR * wTR + colBL * wBL + colBR * wBR;
    
    // Edge darkening (pseudo-AO at material boundaries)
    float blendAmount = 1.0 - wC; // How much we're blending with neighbors
    float edgeFactor = smoothstep(0.0, edge_width, blendAmount);
    col.rgb *= (1.0 - edge_darkness * edgeFactor);
    
    // Debug output modes
    if (debug_mode == 1) {
        // Material ID visualization (different colors for different materials)
        frag_color = vec4(matC / 4.0, float(slotC) / 3.0, 1.0 - matC / 4.0, 1.0);
    } else if (debug_mode == 2) {
        // UV visualization (RG = fract(uv))
        frag_color = vec4(fract(uv), 0.5, 1.0);
    } else if (debug_mode == 3) {
        // Weight visualization (R=top weights, G=bottom weights, B=center)
        frag_color = vec4(wTL + wTR, wBL + wBR, wC, 1.0);
    } else if (debug_mode == 4) {
        // Center material only (no blending) - tests texture loading
        frag_color = colC;
    } else {
        // Normal render
        frag_color = col;
    }
}
)";

// =============================================
// HLSL Shaders (D3D11)
// =============================================
static const char* vs_src_hlsl = R"(
cbuffer vs_params: register(b0) {
    float2 view_size;
    float2 camera_offset;
    float camera_zoom;
    float3 _pad0;
};
struct VSIn {
    float2 pos: TEXCOORD0;
    float2 cellCoord: TEXCOORD1;
    float2 localNorm: TEXCOORD2;
};
struct VSOut {
    float4 pos: SV_Position;
    float2 worldPos: TEXCOORD0;
    float2 cellCoord: TEXCOORD1;
    float2 localNorm: TEXCOORD2;
};
VSOut main(VSIn inp) {
    VSOut o;
    float2 screen = (inp.pos * camera_zoom) + camera_offset;
    float2 clip;
    clip.x = (screen.x / view_size.x) * 2.0 - 1.0;
    clip.y = 1.0 - (screen.y / view_size.y) * 2.0;
    o.pos = float4(clip, 0.0, 1.0);
    o.worldPos = inp.pos;
    o.cellCoord = inp.cellCoord;
    o.localNorm = inp.localNorm;
    return o;
}
)";

static const char* fs_src_hlsl = R"(
Texture2D tex0: register(t0);
Texture2D tex1: register(t1);
Texture2D tex2: register(t2);
Texture2D tex3: register(t3);
Texture2D noise_tex: register(t4);
Texture2D material_id_map: register(t5);
SamplerState linear_smp: register(s0);
SamplerState nearest_smp: register(s1);

cbuffer fs_params: register(b0) {
    float2 cell_size;
    float2 map_size;
    float blend_sharpness;
    float noise_scale;
    float tile_scale;
    float macro_scale;
    float macro_strength;
    float height_influence;
    float edge_darkness;
    float edge_width;
    float world_uv_scale;
    float random_uv_strength;
    int uv_mode;
    int debug_mode;
};

struct PSIn {
    float4 pos: SV_Position;
    float2 worldPos: TEXCOORD0;
    float2 cellCoord: TEXCOORD1;
    float2 localNorm: TEXCOORD2;
};

// Pseudo-random hash
float hash(float2 p) {
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

float2 hash2(float2 p) {
    return float2(hash(p), hash(p + float2(13.7, 91.1)));
}

// Get material ID at cell coordinates
float getMaterialId(float2 cellCoord) {
    float2 uv = (cellCoord + 0.5) / map_size;
    uv = clamp(uv, float2(0.0, 0.0), float2(1.0, 1.0));
    return material_id_map.Sample(nearest_smp, uv).r * 255.0;
}

// Convert material ID to texture slot
int matIdToSlot(float matId) {
    int id = (int)(matId + 0.5);
    if (id == 0) return 0;
    return clamp(id - 1, 0, 3);
}

// Sample a material texture by slot
float4 sampleMaterial(int slot, float2 uv) {
    if (slot == 0) return tex0.Sample(linear_smp, uv);
    if (slot == 1) return tex1.Sample(linear_smp, uv);
    if (slot == 2) return tex2.Sample(linear_smp, uv);
    return tex3.Sample(linear_smp, uv);
}

// Get neighbor cell coords for staggered isometry
float2 getNeighborTL(float2 cell) {
    float isOdd = fmod(cell.y, 2.0);
    return float2(cell.x - 1.0 + isOdd, cell.y - 1.0);
}
float2 getNeighborTR(float2 cell) {
    float isOdd = fmod(cell.y, 2.0);
    return float2(cell.x + isOdd, cell.y - 1.0);
}
float2 getNeighborBL(float2 cell) {
    float isOdd = fmod(cell.y, 2.0);
    return float2(cell.x - 1.0 + isOdd, cell.y + 1.0);
}
float2 getNeighborBR(float2 cell) {
    float isOdd = fmod(cell.y, 2.0);
    return float2(cell.x + isOdd, cell.y + 1.0);
}

float4 main(PSIn inp): SV_Target0 {
    float2 cellCoord = floor(inp.cellCoord + 0.5);
    float2 local = inp.localNorm;
    
    // Get material IDs for current cell and 4 diagonal neighbors (staggered)
    float matC  = getMaterialId(cellCoord);
    float matTL = getMaterialId(getNeighborTL(cellCoord));
    float matTR = getMaterialId(getNeighborTR(cellCoord));
    float matBL = getMaterialId(getNeighborBL(cellCoord));
    float matBR = getMaterialId(getNeighborBR(cellCoord));
    
    // Calculate UV based on mode
    float2 uv;
    if (uv_mode == 0) {
        uv = inp.worldPos / world_uv_scale;
    } else {
        float2 rnd = hash2(cellCoord) * 2.0 - 1.0;
        float2 offset = rnd * random_uv_strength;
        uv = (local + 0.5) + offset;
    }
    
    // Apply macro variation
    float n0 = noise_tex.Sample(linear_smp, uv * macro_scale).r;
    float n1 = noise_tex.Sample(linear_smp, uv * (macro_scale * 0.77) + float2(13.1, 7.7)).r;
    uv = uv + (float2(n0, n1) - 0.5) * macro_strength;
    uv *= tile_scale;
    
    // Sample noise for blending variation
    float noiseVal = noise_tex.Sample(linear_smp, inp.worldPos / cell_size.x * noise_scale * 0.1).r;
    
    // Diamond edge distances (boundary is |x|+|y|=0.5)
    float distTL = 0.5 - (-local.x - local.y);  // top-left edge
    float distTR = 0.5 - ( local.x - local.y);  // top-right edge
    float distBL = 0.5 - (-local.x + local.y);  // bottom-left edge
    float distBR = 0.5 - ( local.x + local.y);  // bottom-right edge
    
    // Corner proximity - distance to each diamond vertex
    float distToTop    = length(local - float2(0.0, -0.5));
    float distToRight  = length(local - float2(0.5, 0.0));
    float distToBottom = length(local - float2(0.0, 0.5));
    float distToLeft   = length(local - float2(-0.5, 0.0));
    
    // Corner proximity factor (higher near vertices)
    float minCornerDist = min(min(distToTop, distToBottom), min(distToLeft, distToRight));
    float cornerProximity = 1.0 - smoothstep(0.0, 0.5, minCornerDist);
    
    // Adaptive blend width: wider near corners
    float baseBlendWidth = lerp(0.35, 0.08, blend_sharpness);
    float edgeBlendWidth = baseBlendWidth * (1.0 + cornerProximity * 0.6);
    
    // Noise jitter
    float noiseJitter = (noiseVal - 0.5) * (1.0 - blend_sharpness) * 0.2;
    
    // Edge blend factors
    float bTL = 1.0 - smoothstep(0.0, edgeBlendWidth, distTL + noiseJitter);
    float bTR = 1.0 - smoothstep(0.0, edgeBlendWidth, distTR + noiseJitter);
    float bBL = 1.0 - smoothstep(0.0, edgeBlendWidth, distBL + noiseJitter);
    float bBR = 1.0 - smoothstep(0.0, edgeBlendWidth, distBR + noiseJitter);
    
    // Sample materials
    int slotC  = matIdToSlot(matC);
    int slotTL = matIdToSlot(matTL);
    int slotTR = matIdToSlot(matTR);
    int slotBL = matIdToSlot(matBL);
    int slotBR = matIdToSlot(matBR);
    
    float4 colC  = sampleMaterial(slotC, uv);
    float4 colTL = sampleMaterial(slotTL, uv);
    float4 colTR = sampleMaterial(slotTR, uv);
    float4 colBL = sampleMaterial(slotBL, uv);
    float4 colBR = sampleMaterial(slotBR, uv);
    
    // Height proxy
    float hC  = dot(colC.rgb, float3(0.333, 0.333, 0.333));
    float hTL = dot(colTL.rgb, float3(0.333, 0.333, 0.333));
    float hTR = dot(colTR.rgb, float3(0.333, 0.333, 0.333));
    float hBL = dot(colBL.rgb, float3(0.333, 0.333, 0.333));
    float hBR = dot(colBR.rgb, float3(0.333, 0.333, 0.333));
    
    // Height-aware adjustment
    bTL *= 1.0 + (hTL - hC) * height_influence;
    bTR *= 1.0 + (hTR - hC) * height_influence;
    bBL *= 1.0 + (hBL - hC) * height_influence;
    bBR *= 1.0 + (hBR - hC) * height_influence;
    
    // Clamp blend factors
    bTL = clamp(bTL, 0.0, 1.0);
    bTR = clamp(bTR, 0.0, 1.0);
    bBL = clamp(bBL, 0.0, 1.0);
    bBR = clamp(bBR, 0.0, 1.0);
    
    // Zero out blend factors for same material
    if (slotTL == slotC) bTL = 0.0;
    if (slotTR == slotC) bTR = 0.0;
    if (slotBL == slotC) bBL = 0.0;
    if (slotBR == slotC) bBR = 0.0;
    
    // WEIGHTED BLENDING
    // Sum of neighbor blend factors
    float sumNeighborBlend = bTL + bTR + bBL + bBR;
    
    // Center weight
    float wC = max(0.0, 1.0 - sumNeighborBlend);
    
    float wTL = bTL;
    float wTR = bTR;
    float wBL = bBL;
    float wBR = bBR;
    
    // Normalize weights
    float totalWeight = wC + wTL + wTR + wBL + wBR;
    if (totalWeight > 0.001) {
        float invTotal = 1.0 / totalWeight;
        wC  *= invTotal;
        wTL *= invTotal;
        wTR *= invTotal;
        wBL *= invTotal;
        wBR *= invTotal;
    } else {
        wC = 1.0;
        wTL = wTR = wBL = wBR = 0.0;
    }
    
    // Final weighted blend
    float4 col = colC * wC + colTL * wTL + colTR * wTR + colBL * wBL + colBR * wBR;
    
    // Edge darkening
    float blendAmount = 1.0 - wC;
    float edgeFactor = smoothstep(0.0, edge_width, blendAmount);
    col.rgb *= (1.0 - edge_darkness * edgeFactor);
    
    // Debug output modes
    if (debug_mode == 1) {
        // Material ID visualization
        return float4(matC / 4.0, (float)slotC / 3.0, 1.0 - matC / 4.0, 1.0);
    } else if (debug_mode == 2) {
        // UV visualization
        return float4(frac(uv), 0.5, 1.0);
    } else if (debug_mode == 3) {
        // Weight visualization
        return float4(wTL + wTR, wBL + wBR, wC, 1.0);
    } else if (debug_mode == 4) {
        // Center material only
        return colC;
    }
    
    return col;
}
)";

static sg_image make1x1Rgba8(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    const std::uint8_t px[4] = { r, g, b, a };
    sg_image_desc desc = {};
    desc.width = 1;
    desc.height = 1;
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.data.subimage[0][0].ptr = px;
    desc.data.subimage[0][0].size = sizeof(px);
    desc.label = "fallback-1x1";
    return sg_make_image(&desc);
}

static sg_image loadImageRgba8(const std::string& path, const char* label) {
    int w = 0, h = 0, comp = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &comp, 4);
    if (!pixels || w <= 0 || h <= 0) {
        spdlog::error("Failed to load image: path='{}' error='{}'", path, stbi_failure_reason());
        if (pixels) stbi_image_free(pixels);
        return { SG_INVALID_ID };
    }

    sg_image_desc desc = {};
    desc.width = w;
    desc.height = h;
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.data.subimage[0][0].ptr = pixels;
    desc.data.subimage[0][0].size = (size_t)w * (size_t)h * 4;
    desc.label = label;
    sg_image img = sg_make_image(&desc);

    stbi_image_free(pixels);
    return img;
}

} // namespace

void SplattingRenderer::init() {
    ensurePipeline();

    // Dynamic vertex buffer (6 verts per cell for 2 triangles)
    sg_buffer_desc buf_desc = {};
    buf_desc.size = 6 * 65536 * (int)sizeof(Vertex);
    buf_desc.usage = SG_USAGE_DYNAMIC;
    buf_desc.label = "splatting-verts";
    vbuf = sg_make_buffer(&buf_desc);
    bind.vertex_buffers[0] = vbuf;

    // Linear sampler for material textures
    {
        sg_sampler_desc smp_desc = {};
        smp_desc.min_filter = SG_FILTER_LINEAR;
        smp_desc.mag_filter = SG_FILTER_LINEAR;
        smp_desc.wrap_u = SG_WRAP_REPEAT;
        smp_desc.wrap_v = SG_WRAP_REPEAT;
        smp_desc.label = "linear-sampler";
        linearSampler = sg_make_sampler(&smp_desc);
    }

    // Nearest sampler for material ID map
    {
        sg_sampler_desc smp_desc = {};
        smp_desc.min_filter = SG_FILTER_NEAREST;
        smp_desc.mag_filter = SG_FILTER_NEAREST;
        smp_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
        smp_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
        smp_desc.label = "nearest-sampler";
        nearestSampler = sg_make_sampler(&smp_desc);
    }

    ensureFallbackTextures();

    for (int i = 0; i < 4; i++) {
        materials[i] = fallbackWhite;
    }
    noiseTexture = fallbackNoise;
}

void SplattingRenderer::shutdown() {
    if (pip.id != SG_INVALID_ID) destroyPipeline();

    if (vbuf.id != SG_INVALID_ID) {
        sg_destroy_buffer(vbuf);
        vbuf.id = SG_INVALID_ID;
    }
    if (linearSampler.id != SG_INVALID_ID) {
        sg_destroy_sampler(linearSampler);
        linearSampler.id = SG_INVALID_ID;
    }
    if (nearestSampler.id != SG_INVALID_ID) {
        sg_destroy_sampler(nearestSampler);
        nearestSampler.id = SG_INVALID_ID;
    }
    for (int i = 0; i < 4; i++) {
        if (materials[i].id != SG_INVALID_ID && materials[i].id != fallbackWhite.id) {
            sg_destroy_image(materials[i]);
        }
        materials[i].id = SG_INVALID_ID;
    }
    if (noiseTexture.id != SG_INVALID_ID && noiseTexture.id != fallbackNoise.id) {
        sg_destroy_image(noiseTexture);
    }
    noiseTexture.id = SG_INVALID_ID;

    if (materialIdMap.id != SG_INVALID_ID) {
        sg_destroy_image(materialIdMap);
        materialIdMap.id = SG_INVALID_ID;
    }

    if (fallbackWhite.id != SG_INVALID_ID) {
        sg_destroy_image(fallbackWhite);
        fallbackWhite.id = SG_INVALID_ID;
    }
    if (fallbackNoise.id != SG_INVALID_ID) {
        sg_destroy_image(fallbackNoise);
        fallbackNoise.id = SG_INVALID_ID;
    }
}

bool SplattingRenderer::loadMaterial(int slot, const std::string& path) {
    if (slot < 0 || slot >= 4) return false;
    if (materials[slot].id != SG_INVALID_ID && materials[slot].id != fallbackWhite.id) {
        sg_destroy_image(materials[slot]);
    }
    const std::string label = "material-" + std::to_string(slot);
    sg_image img = loadImageRgba8(path, label.c_str());
    if (img.id == SG_INVALID_ID) {
        materials[slot] = fallbackWhite;
        return false;
    }
    materials[slot] = img;
    return true;
}

bool SplattingRenderer::loadNoiseTexture(const std::string& path) {
    if (noiseTexture.id != SG_INVALID_ID && noiseTexture.id != fallbackNoise.id) {
        sg_destroy_image(noiseTexture);
    }
    sg_image img = loadImageRgba8(path, "noise");
    if (img.id == SG_INVALID_ID) {
        noiseTexture = fallbackNoise;
        return false;
    }
    noiseTexture = img;
    return true;
}

void SplattingRenderer::updateMaterialIdMap(const MaterialMap& map) {
    if (map.width <= 0 || map.height <= 0) return;

    const bool needRecreate = (materialIdMap.id == SG_INVALID_ID ||
                                materialIdMapWidth != map.width ||
                                materialIdMapHeight != map.height);

    if (needRecreate) {
        if (materialIdMap.id != SG_INVALID_ID) {
            sg_destroy_image(materialIdMap);
        }

        // Create R8 texture (material IDs 0-255)
        // We store as RGBA8 for compatibility (R channel = material ID)
        materialIdData.resize((size_t)map.width * (size_t)map.height * 4);

        sg_image_desc desc = {};
        desc.width = map.width;
        desc.height = map.height;
        desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        desc.usage = SG_USAGE_DYNAMIC;
        desc.label = "material-id-map";
        materialIdMap = sg_make_image(&desc);

        materialIdMapWidth = map.width;
        materialIdMapHeight = map.height;

        spdlog::info("Created materialIdMap {}x{}", map.width, map.height);
    }

    // Copy material IDs into RGBA8 buffer (R = materialId, GBA = 0)
    for (int y = 0; y < map.height; y++) {
        for (int x = 0; x < map.width; x++) {
            const size_t idx = ((size_t)y * (size_t)map.width + (size_t)x) * 4;
            const MaterialId m = map.at(x, y);
            materialIdData[idx + 0] = m;      // R
            materialIdData[idx + 1] = 0;      // G
            materialIdData[idx + 2] = 0;      // B
            materialIdData[idx + 3] = 255;    // A
        }
    }

    sg_image_data data = {};
    data.subimage[0][0].ptr = materialIdData.data();
    data.subimage[0][0].size = materialIdData.size();
    sg_update_image(materialIdMap, &data);
}

void SplattingRenderer::render(
    const MaterialMap& map,
    const topology_core::StaggeredIsometry& iso,
    const topology_core::Camera2D& camera,
    int viewWidth,
    int viewHeight,
    const SplattingParams& params
) {
    if (pip.id == SG_INVALID_ID || vbuf.id == SG_INVALID_ID) return;
    if (map.width <= 0 || map.height <= 0) return;

    // Update material ID map texture
    updateMaterialIdMap(map);

    buildMesh(map, iso);
    if (vertices.empty()) return;

    sg_range range = { vertices.data(), vertices.size() * sizeof(Vertex) };
    sg_update_buffer(vbuf, &range);

    // Bind textures
    bind.fs.images[0] = materials[0].id != SG_INVALID_ID ? materials[0] : fallbackWhite;
    bind.fs.images[1] = materials[1].id != SG_INVALID_ID ? materials[1] : fallbackWhite;
    bind.fs.images[2] = materials[2].id != SG_INVALID_ID ? materials[2] : fallbackWhite;
    bind.fs.images[3] = materials[3].id != SG_INVALID_ID ? materials[3] : fallbackWhite;
    bind.fs.images[4] = noiseTexture.id != SG_INVALID_ID ? noiseTexture : fallbackNoise;
    bind.fs.images[5] = materialIdMap.id != SG_INVALID_ID ? materialIdMap : fallbackWhite;
    bind.fs.samplers[0] = linearSampler;
    bind.fs.samplers[1] = nearestSampler;

    sg_apply_pipeline(pip);
    sg_apply_bindings(&bind);

    VsParams vs = {};
    vs.viewSize[0] = (float)viewWidth;
    vs.viewSize[1] = (float)viewHeight;
    vs.cameraOffset[0] = camera.offset.x;
    vs.cameraOffset[1] = camera.offset.y;
    vs.cameraZoom = camera.zoom;
    sg_range vs_range = { &vs, sizeof(vs) };
    sg_apply_uniforms(SG_SHADERSTAGE_VS, 0, &vs_range);

    const glm::vec2 cellSize = iso.dims.cellSize();

    FsParams fs = {};
    fs.cellSize[0] = cellSize.x;
    fs.cellSize[1] = cellSize.y;
    fs.mapSize[0] = (float)map.width;
    fs.mapSize[1] = (float)map.height;
    fs.blendSharpness = std::clamp(params.blendSharpness, 0.0f, 1.0f);
    fs.noiseScale = std::max(0.001f, params.noiseScale);
    fs.tileScale = std::max(0.001f, params.tileScale);
    fs.macroScale = std::max(0.001f, params.macroScale);
    fs.macroStrength = std::max(0.0f, params.macroStrength);
    fs.heightInfluence = std::max(0.0f, params.heightInfluence);
    fs.edgeDarkness = std::max(0.0f, params.edgeDarkness);
    fs.edgeWidth = std::max(0.001f, params.edgeWidth);
    fs.worldUvScale = std::max(1.0f, params.worldUvScale);
    fs.randomUvStrength = std::max(0.0f, params.randomUvStrength);
    fs.uvMode = static_cast<int>(params.uvMode);
    fs.debugMode = params.debugMode;
    sg_range fs_range = { &fs, sizeof(fs) };
    sg_apply_uniforms(SG_SHADERSTAGE_FS, 0, &fs_range);

    sg_draw(0, (int)vertices.size(), 1);
}

void SplattingRenderer::ensureFallbackTextures() {
    if (fallbackWhite.id == SG_INVALID_ID) {
        fallbackWhite = make1x1Rgba8(255, 255, 255, 255);
    }
    if (fallbackNoise.id == SG_INVALID_ID) {
        fallbackNoise = make1x1Rgba8(128, 128, 128, 255);
    }
}

void SplattingRenderer::ensurePipeline() {
    if (pip.id != SG_INVALID_ID) return;

    sg_shader_desc shd_desc = {};
#if defined(SOKOL_D3D11)
    shd_desc.vs.source = vs_src_hlsl;
    shd_desc.fs.source = fs_src_hlsl;
    shd_desc.attrs[0].sem_name = "TEXCOORD";
    shd_desc.attrs[0].sem_index = 0;
    shd_desc.attrs[1].sem_name = "TEXCOORD";
    shd_desc.attrs[1].sem_index = 1;
    shd_desc.attrs[2].sem_name = "TEXCOORD";
    shd_desc.attrs[2].sem_index = 2;
#else
    shd_desc.vs.source = vs_src_glsl;
    shd_desc.fs.source = fs_src_glsl;
#endif

    // VS uniforms
    shd_desc.vs.uniform_blocks[0].size = sizeof(VsParams);
    shd_desc.vs.uniform_blocks[0].uniforms[0].name = "view_size";
    shd_desc.vs.uniform_blocks[0].uniforms[0].type = SG_UNIFORMTYPE_FLOAT2;
    shd_desc.vs.uniform_blocks[0].uniforms[1].name = "camera_offset";
    shd_desc.vs.uniform_blocks[0].uniforms[1].type = SG_UNIFORMTYPE_FLOAT2;
    shd_desc.vs.uniform_blocks[0].uniforms[2].name = "camera_zoom";
    shd_desc.vs.uniform_blocks[0].uniforms[2].type = SG_UNIFORMTYPE_FLOAT;

    // FS uniforms
    shd_desc.fs.uniform_blocks[0].size = sizeof(FsParams);
    shd_desc.fs.uniform_blocks[0].uniforms[0].name = "cell_size";
    shd_desc.fs.uniform_blocks[0].uniforms[0].type = SG_UNIFORMTYPE_FLOAT2;
    shd_desc.fs.uniform_blocks[0].uniforms[1].name = "map_size";
    shd_desc.fs.uniform_blocks[0].uniforms[1].type = SG_UNIFORMTYPE_FLOAT2;
    shd_desc.fs.uniform_blocks[0].uniforms[2].name = "blend_sharpness";
    shd_desc.fs.uniform_blocks[0].uniforms[2].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.fs.uniform_blocks[0].uniforms[3].name = "noise_scale";
    shd_desc.fs.uniform_blocks[0].uniforms[3].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.fs.uniform_blocks[0].uniforms[4].name = "tile_scale";
    shd_desc.fs.uniform_blocks[0].uniforms[4].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.fs.uniform_blocks[0].uniforms[5].name = "macro_scale";
    shd_desc.fs.uniform_blocks[0].uniforms[5].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.fs.uniform_blocks[0].uniforms[6].name = "macro_strength";
    shd_desc.fs.uniform_blocks[0].uniforms[6].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.fs.uniform_blocks[0].uniforms[7].name = "height_influence";
    shd_desc.fs.uniform_blocks[0].uniforms[7].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.fs.uniform_blocks[0].uniforms[8].name = "edge_darkness";
    shd_desc.fs.uniform_blocks[0].uniforms[8].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.fs.uniform_blocks[0].uniforms[9].name = "edge_width";
    shd_desc.fs.uniform_blocks[0].uniforms[9].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.fs.uniform_blocks[0].uniforms[10].name = "world_uv_scale";
    shd_desc.fs.uniform_blocks[0].uniforms[10].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.fs.uniform_blocks[0].uniforms[11].name = "random_uv_strength";
    shd_desc.fs.uniform_blocks[0].uniforms[11].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.fs.uniform_blocks[0].uniforms[12].name = "uv_mode";
    shd_desc.fs.uniform_blocks[0].uniforms[12].type = SG_UNIFORMTYPE_INT;
    shd_desc.fs.uniform_blocks[0].uniforms[13].name = "debug_mode";
    shd_desc.fs.uniform_blocks[0].uniforms[13].type = SG_UNIFORMTYPE_INT;

    // FS images (6: 4 materials + noise + materialIdMap)
    for (int i = 0; i < 6; i++) {
        shd_desc.fs.images[i].used = true;
        shd_desc.fs.images[i].image_type = SG_IMAGETYPE_2D;
    }

    // 2 samplers
    shd_desc.fs.samplers[0].used = true;
    shd_desc.fs.samplers[0].sampler_type = SG_SAMPLERTYPE_SAMPLE;
    shd_desc.fs.samplers[1].used = true;
    shd_desc.fs.samplers[1].sampler_type = SG_SAMPLERTYPE_SAMPLE;

    // Image-sampler pairs
    for (int i = 0; i < 5; i++) {
        shd_desc.fs.image_sampler_pairs[i].used = true;
        shd_desc.fs.image_sampler_pairs[i].image_slot = i;
        shd_desc.fs.image_sampler_pairs[i].sampler_slot = 0;
    }
    shd_desc.fs.image_sampler_pairs[5].used = true;
    shd_desc.fs.image_sampler_pairs[5].image_slot = 5;
    shd_desc.fs.image_sampler_pairs[5].sampler_slot = 1;

    sg_shader shd = sg_make_shader(&shd_desc);

    sg_pipeline_desc pip_desc = {};
    pip_desc.shader = shd;
    // 3 vertex attributes: pos (float2), cellCoord (float2), localNorm (float2)
    pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
    pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
    pip_desc.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT2;
    pip_desc.colors[0].blend.enabled = true;
    pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    pip_desc.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    pip_desc.depth.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    pip_desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
    pip_desc.depth.write_enabled = false;
    pip_desc.label = "splatting-pipeline";
    pip = sg_make_pipeline(&pip_desc);
}

void SplattingRenderer::destroyPipeline() {
    if (pip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(pip);
        pip.id = SG_INVALID_ID;
    }
}

void SplattingRenderer::buildMesh(const MaterialMap& map, const topology_core::StaggeredIsometry& iso) {
    vertices.clear();
    vertices.reserve((size_t)map.width * (size_t)map.height * 6);

    const glm::vec2 cellSize = iso.dims.cellSize();
    const float halfW = cellSize.x * 0.5f;
    const float halfH = cellSize.y * 0.5f;

    for (int y = 0; y < map.height; y++) {
        for (int x = 0; x < map.width; x++) {
            const MaterialId m = map.at(x, y);
            if (m == 0) continue;

            const glm::ivec2 cell(x, y);
            const glm::vec2 center = iso.mapToField(cell);

            // Diamond vertices (isometric cell)
            // top: (0, -0.5), right: (0.5, 0), bottom: (0, 0.5), left: (-0.5, 0)
            const glm::vec2 top    = center + glm::vec2(0.0f, -halfH);
            const glm::vec2 right  = center + glm::vec2(halfW, 0.0f);
            const glm::vec2 bottom = center + glm::vec2(0.0f, halfH);
            const glm::vec2 left   = center + glm::vec2(-halfW, 0.0f);

            const float cx = (float)x;
            const float cy = (float)y;

            // Local normalized coords for diamond vertices
            // top: (0, -0.5), right: (0.5, 0), bottom: (0, 0.5), left: (-0.5, 0)
            
            // Triangle 1: top -> right -> bottom
            vertices.push_back({ top.x, top.y, cx, cy, 0.0f, -0.5f });
            vertices.push_back({ right.x, right.y, cx, cy, 0.5f, 0.0f });
            vertices.push_back({ bottom.x, bottom.y, cx, cy, 0.0f, 0.5f });

            // Triangle 2: bottom -> left -> top
            vertices.push_back({ bottom.x, bottom.y, cx, cy, 0.0f, 0.5f });
            vertices.push_back({ left.x, left.y, cx, cy, -0.5f, 0.0f });
            vertices.push_back({ top.x, top.y, cx, cy, 0.0f, -0.5f });
        }
    }
}
