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

// Orthogonal neighbors (at diamond vertices)
vec2 getNeighborTop(vec2 cell) {
    return vec2(cell.x, cell.y - 2.0);
}
vec2 getNeighborBottom(vec2 cell) {
    return vec2(cell.x, cell.y + 2.0);
}
vec2 getNeighborLeft(vec2 cell) {
    return vec2(cell.x - 1.0, cell.y);
}
vec2 getNeighborRight(vec2 cell) {
    return vec2(cell.x + 1.0, cell.y);
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
    
    // Get material IDs for 4 orthogonal neighbors (at diamond vertices)
    float matTop    = getMaterialId(getNeighborTop(cellCoord));
    float matBottom = getMaterialId(getNeighborBottom(cellCoord));
    float matLeft   = getMaterialId(getNeighborLeft(cellCoord));
    float matRight  = getMaterialId(getNeighborRight(cellCoord));
    
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
    
    // Vertex blend factors: for orthogonal neighbors at diamond vertices
    // Use slightly wider blend for corners to ensure smooth transitions
    float cornerBlendWidth = edgeBlendWidth * 1.2;
    float bTop    = 1.0 - smoothstep(0.0, cornerBlendWidth, distToTop + noiseJitter);
    float bRight  = 1.0 - smoothstep(0.0, cornerBlendWidth, distToRight + noiseJitter);
    float bBottom = 1.0 - smoothstep(0.0, cornerBlendWidth, distToBottom + noiseJitter);
    float bLeft   = 1.0 - smoothstep(0.0, cornerBlendWidth, distToLeft + noiseJitter);
    
    // Sample materials (diagonal neighbors)
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
    
    // Sample materials (orthogonal neighbors - at vertices)
    int slotTop    = matIdToSlot(matTop);
    int slotBottom = matIdToSlot(matBottom);
    int slotLeft   = matIdToSlot(matLeft);
    int slotRight  = matIdToSlot(matRight);
    
    vec4 colTop    = sampleMaterial(slotTop, uv);
    vec4 colBottom = sampleMaterial(slotBottom, uv);
    vec4 colLeft   = sampleMaterial(slotLeft, uv);
    vec4 colRight  = sampleMaterial(slotRight, uv);
    
    // Height proxy from albedo luminance for height-aware blending
    float hC  = dot(colC.rgb, vec3(0.333));
    float hTL = dot(colTL.rgb, vec3(0.333));
    float hTR = dot(colTR.rgb, vec3(0.333));
    float hBL = dot(colBL.rgb, vec3(0.333));
    float hBR = dot(colBR.rgb, vec3(0.333));
    float hTop    = dot(colTop.rgb, vec3(0.333));
    float hRight  = dot(colRight.rgb, vec3(0.333));
    float hBottom = dot(colBottom.rgb, vec3(0.333));
    float hLeft   = dot(colLeft.rgb, vec3(0.333));
    
    // Height-aware adjustment: boost blend factor for "higher" neighbors
    // Diagonal neighbors
    bTL *= 1.0 + (hTL - hC) * height_influence;
    bTR *= 1.0 + (hTR - hC) * height_influence;
    bBL *= 1.0 + (hBL - hC) * height_influence;
    bBR *= 1.0 + (hBR - hC) * height_influence;
    // Orthogonal neighbors
    bTop    *= 1.0 + (hTop    - hC) * height_influence;
    bRight  *= 1.0 + (hRight  - hC) * height_influence;
    bBottom *= 1.0 + (hBottom - hC) * height_influence;
    bLeft   *= 1.0 + (hLeft   - hC) * height_influence;
    
    // Clamp blend factors
    bTL = clamp(bTL, 0.0, 1.0);
    bTR = clamp(bTR, 0.0, 1.0);
    bBL = clamp(bBL, 0.0, 1.0);
    bBR = clamp(bBR, 0.0, 1.0);
    bTop    = clamp(bTop, 0.0, 1.0);
    bRight  = clamp(bRight, 0.0, 1.0);
    bBottom = clamp(bBottom, 0.0, 1.0);
    bLeft   = clamp(bLeft, 0.0, 1.0);
    
    // MATERIAL PRIORITY: Only blend with neighbors that have HIGHER material ID
    // This prevents the "mirror" effect at boundaries
    // Higher ID materials "invade" lower ID materials, not vice versa
    // e.g., Sand(2) invades Grass(1), Rock(3) invades both
    
    // Diagonal neighbors
    if (matTL <= matC) bTL = 0.0;
    if (matTR <= matC) bTR = 0.0;
    if (matBL <= matC) bBL = 0.0;
    if (matBR <= matC) bBR = 0.0;
    
    // Orthogonal neighbors (at diamond vertices)
    if (matTop    <= matC) bTop    = 0.0;
    if (matRight  <= matC) bRight  = 0.0;
    if (matBottom <= matC) bBottom = 0.0;
    if (matLeft   <= matC) bLeft   = 0.0;
    
    // WEIGHTED BLENDING with 8 neighbors
    // Sum of all neighbor blend factors determines how much we blend away from center
    float sumNeighborBlend = bTL + bTR + bBL + bBR + bTop + bRight + bBottom + bLeft;
    
    // Center weight: 1 when no neighbors blend, decreases as neighbors contribute
    // Clamp sumNeighborBlend to prevent center from going negative
    float wC = max(0.0, 1.0 - sumNeighborBlend);
    
    // Neighbor weights are their blend factors
    float wTL = bTL;
    float wTR = bTR;
    float wBL = bBL;
    float wBR = bBR;
    float wTop    = bTop;
    float wRight  = bRight;
    float wBottom = bBottom;
    float wLeft   = bLeft;
    
    // Normalize weights to sum to 1
    float totalWeight = wC + wTL + wTR + wBL + wBR + wTop + wRight + wBottom + wLeft;
    if (totalWeight > 0.001) {
        float invTotal = 1.0 / totalWeight;
        wC      *= invTotal;
        wTL     *= invTotal;
        wTR     *= invTotal;
        wBL     *= invTotal;
        wBR     *= invTotal;
        wTop    *= invTotal;
        wRight  *= invTotal;
        wBottom *= invTotal;
        wLeft   *= invTotal;
    } else {
        // Fallback: just use center material
        wC = 1.0;
        wTL = wTR = wBL = wBR = 0.0;
        wTop = wRight = wBottom = wLeft = 0.0;
    }
    
    // Final weighted blend of all 9 materials (center + 4 diagonal + 4 orthogonal)
    vec4 col = colC * wC 
             + colTL * wTL + colTR * wTR + colBL * wBL + colBR * wBR
             + colTop * wTop + colRight * wRight + colBottom * wBottom + colLeft * wLeft;
    
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

// Orthogonal neighbors (at diamond vertices)
float2 getNeighborTop(float2 cell) {
    return float2(cell.x, cell.y - 2.0);
}
float2 getNeighborBottom(float2 cell) {
    return float2(cell.x, cell.y + 2.0);
}
float2 getNeighborLeft(float2 cell) {
    return float2(cell.x - 1.0, cell.y);
}
float2 getNeighborRight(float2 cell) {
    return float2(cell.x + 1.0, cell.y);
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
    
    // Get material IDs for 4 orthogonal neighbors (at diamond vertices)
    float matTop    = getMaterialId(getNeighborTop(cellCoord));
    float matBottom = getMaterialId(getNeighborBottom(cellCoord));
    float matLeft   = getMaterialId(getNeighborLeft(cellCoord));
    float matRight  = getMaterialId(getNeighborRight(cellCoord));
    
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
    
    // Edge blend factors (diagonal neighbors)
    float bTL = 1.0 - smoothstep(0.0, edgeBlendWidth, distTL + noiseJitter);
    float bTR = 1.0 - smoothstep(0.0, edgeBlendWidth, distTR + noiseJitter);
    float bBL = 1.0 - smoothstep(0.0, edgeBlendWidth, distBL + noiseJitter);
    float bBR = 1.0 - smoothstep(0.0, edgeBlendWidth, distBR + noiseJitter);
    
    // Vertex blend factors (orthogonal neighbors at vertices)
    float cornerBlendWidth = edgeBlendWidth * 1.2;
    float bTop    = 1.0 - smoothstep(0.0, cornerBlendWidth, distToTop + noiseJitter);
    float bRight  = 1.0 - smoothstep(0.0, cornerBlendWidth, distToRight + noiseJitter);
    float bBottom = 1.0 - smoothstep(0.0, cornerBlendWidth, distToBottom + noiseJitter);
    float bLeft   = 1.0 - smoothstep(0.0, cornerBlendWidth, distToLeft + noiseJitter);
    
    // Sample materials (diagonal neighbors)
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
    
    // Sample materials (orthogonal neighbors)
    int slotTop    = matIdToSlot(matTop);
    int slotBottom = matIdToSlot(matBottom);
    int slotLeft   = matIdToSlot(matLeft);
    int slotRight  = matIdToSlot(matRight);
    
    float4 colTop    = sampleMaterial(slotTop, uv);
    float4 colBottom = sampleMaterial(slotBottom, uv);
    float4 colLeft   = sampleMaterial(slotLeft, uv);
    float4 colRight  = sampleMaterial(slotRight, uv);
    
    // Height proxy
    float hC  = dot(colC.rgb, float3(0.333, 0.333, 0.333));
    float hTL = dot(colTL.rgb, float3(0.333, 0.333, 0.333));
    float hTR = dot(colTR.rgb, float3(0.333, 0.333, 0.333));
    float hBL = dot(colBL.rgb, float3(0.333, 0.333, 0.333));
    float hBR = dot(colBR.rgb, float3(0.333, 0.333, 0.333));
    float hTop    = dot(colTop.rgb, float3(0.333, 0.333, 0.333));
    float hRight  = dot(colRight.rgb, float3(0.333, 0.333, 0.333));
    float hBottom = dot(colBottom.rgb, float3(0.333, 0.333, 0.333));
    float hLeft   = dot(colLeft.rgb, float3(0.333, 0.333, 0.333));
    
    // Height-aware adjustment (diagonal)
    bTL *= 1.0 + (hTL - hC) * height_influence;
    bTR *= 1.0 + (hTR - hC) * height_influence;
    bBL *= 1.0 + (hBL - hC) * height_influence;
    bBR *= 1.0 + (hBR - hC) * height_influence;
    // Height-aware adjustment (orthogonal)
    bTop    *= 1.0 + (hTop    - hC) * height_influence;
    bRight  *= 1.0 + (hRight  - hC) * height_influence;
    bBottom *= 1.0 + (hBottom - hC) * height_influence;
    bLeft   *= 1.0 + (hLeft   - hC) * height_influence;
    
    // Clamp blend factors
    bTL = clamp(bTL, 0.0, 1.0);
    bTR = clamp(bTR, 0.0, 1.0);
    bBL = clamp(bBL, 0.0, 1.0);
    bBR = clamp(bBR, 0.0, 1.0);
    bTop    = clamp(bTop, 0.0, 1.0);
    bRight  = clamp(bRight, 0.0, 1.0);
    bBottom = clamp(bBottom, 0.0, 1.0);
    bLeft   = clamp(bLeft, 0.0, 1.0);
    
    // MATERIAL PRIORITY: Only blend with neighbors that have HIGHER material ID
    // This prevents the "mirror" effect at boundaries
    // Diagonal neighbors
    if (matTL <= matC) bTL = 0.0;
    if (matTR <= matC) bTR = 0.0;
    if (matBL <= matC) bBL = 0.0;
    if (matBR <= matC) bBR = 0.0;
    // Orthogonal neighbors
    if (matTop    <= matC) bTop    = 0.0;
    if (matRight  <= matC) bRight  = 0.0;
    if (matBottom <= matC) bBottom = 0.0;
    if (matLeft   <= matC) bLeft   = 0.0;
    
    // WEIGHTED BLENDING with 8 neighbors
    // Sum of all neighbor blend factors
    float sumNeighborBlend = bTL + bTR + bBL + bBR + bTop + bRight + bBottom + bLeft;
    
    // Center weight
    float wC = max(0.0, 1.0 - sumNeighborBlend);
    
    float wTL = bTL;
    float wTR = bTR;
    float wBL = bBL;
    float wBR = bBR;
    float wTop    = bTop;
    float wRight  = bRight;
    float wBottom = bBottom;
    float wLeft   = bLeft;
    
    // Normalize weights
    float totalWeight = wC + wTL + wTR + wBL + wBR + wTop + wRight + wBottom + wLeft;
    if (totalWeight > 0.001) {
        float invTotal = 1.0 / totalWeight;
        wC      *= invTotal;
        wTL     *= invTotal;
        wTR     *= invTotal;
        wBL     *= invTotal;
        wBR     *= invTotal;
        wTop    *= invTotal;
        wRight  *= invTotal;
        wBottom *= invTotal;
        wLeft   *= invTotal;
    } else {
        wC = 1.0;
        wTL = wTR = wBL = wBR = 0.0;
        wTop = wRight = wBottom = wLeft = 0.0;
    }
    
    // Final weighted blend of all 9 materials
    float4 col = colC * wC 
               + colTL * wTL + colTR * wTR + colBL * wBL + colBR * wBR
               + colTop * wTop + colRight * wRight + colBottom * wBottom + colLeft * wLeft;
    
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

// =============================================
// MSL Shaders (Metal)
// =============================================
// Entry point "_main" is sokol's default for Metal; vertex attributes map by
// index ([[attribute(N)]]). Uniform-block field layout must match the C++
// structs (packed_float3 keeps the VS tail padding at 12 bytes).
static const char* vs_src_msl = R"(
#include <metal_stdlib>
using namespace metal;

struct VsParams {
    float2 view_size;
    float2 camera_offset;
    float camera_zoom;
    packed_float3 _pad0;
};

struct VSIn {
    float2 pos [[attribute(0)]];
    float2 cellCoord [[attribute(1)]];
    float2 localNorm [[attribute(2)]];
};

struct VSOut {
    float4 pos [[position]];
    float2 worldPos;
    float2 cellCoord;
    float2 localNorm;
};

vertex VSOut _main(VSIn in [[stage_in]], constant VsParams& params [[buffer(0)]]) {
    VSOut o;
    float2 screen = (in.pos * params.camera_zoom) + params.camera_offset;
    float2 clip = float2(
        (screen.x / params.view_size.x) * 2.0 - 1.0,
        1.0 - (screen.y / params.view_size.y) * 2.0
    );
    o.pos = float4(clip, 0.0, 1.0);
    o.worldPos = in.pos;
    o.cellCoord = in.cellCoord;
    o.localNorm = in.localNorm;
    return o;
}
)";

static const char* fs_src_msl = R"(
#include <metal_stdlib>
using namespace metal;

struct FsParams {
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
    float4 pos [[position]];
    float2 worldPos;
    float2 cellCoord;
    float2 localNorm;
};

// Pseudo-random hash for per-tile UV randomization
float hash(float2 p) {
    return fract(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

float2 hash2(float2 p) {
    return float2(hash(p), hash(p + float2(13.7, 91.1)));
}

// Get material ID at cell coordinates (clamped)
float getMaterialId(float2 cellCoord,
                    constant FsParams& params,
                    texture2d<float> material_id_map,
                    sampler nearest_smp) {
    float2 uv = (cellCoord + 0.5) / params.map_size;
    uv = clamp(uv, float2(0.0), float2(1.0));
    return material_id_map.sample(nearest_smp, uv).r * 255.0;
}

// Convert material ID to texture slot index (0-3)
int matIdToSlot(float matId) {
    int id = int(matId + 0.5);
    if (id == 0) return 0;  // Empty renders as slot 0
    return clamp(id - 1, 0, 3);
}

// Sample a material texture by slot
float4 sampleMaterial(int slot, float2 uv,
                      texture2d<float> tex0,
                      texture2d<float> tex1,
                      texture2d<float> tex2,
                      texture2d<float> tex3,
                      sampler linear_smp) {
    if (slot == 0) return tex0.sample(linear_smp, uv);
    if (slot == 1) return tex1.sample(linear_smp, uv);
    if (slot == 2) return tex2.sample(linear_smp, uv);
    return tex3.sample(linear_smp, uv);
}

// Get neighbor cell coords for staggered isometry
// In staggered grid, 4 diagonal neighbors depend on row parity
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

// Orthogonal neighbors (at diamond vertices)
float2 getNeighborTop(float2 cell) {
    return float2(cell.x, cell.y - 2.0);
}
float2 getNeighborBottom(float2 cell) {
    return float2(cell.x, cell.y + 2.0);
}
float2 getNeighborLeft(float2 cell) {
    return float2(cell.x - 1.0, cell.y);
}
float2 getNeighborRight(float2 cell) {
    return float2(cell.x + 1.0, cell.y);
}

fragment float4 _main(PSIn in [[stage_in]],
                      constant FsParams& params [[buffer(1)]],
                      texture2d<float> tex0 [[texture(0)]],
                      texture2d<float> tex1 [[texture(1)]],
                      texture2d<float> tex2 [[texture(2)]],
                      texture2d<float> tex3 [[texture(3)]],
                      texture2d<float> noise_tex [[texture(4)]],
                      texture2d<float> material_id_map [[texture(5)]],
                      sampler linear_smp [[sampler(0)]],
                      sampler nearest_smp [[sampler(1)]]) {
    float2 cellCoord = floor(in.cellCoord + 0.5); // round to nearest int
    float2 local = in.localNorm;

    // Get material IDs for current cell and 4 diagonal neighbors (staggered)
    float matC  = getMaterialId(cellCoord, params, material_id_map, nearest_smp);
    float matTL = getMaterialId(getNeighborTL(cellCoord), params, material_id_map, nearest_smp);
    float matTR = getMaterialId(getNeighborTR(cellCoord), params, material_id_map, nearest_smp);
    float matBL = getMaterialId(getNeighborBL(cellCoord), params, material_id_map, nearest_smp);
    float matBR = getMaterialId(getNeighborBR(cellCoord), params, material_id_map, nearest_smp);

    // Get material IDs for 4 orthogonal neighbors (at diamond vertices)
    float matTop    = getMaterialId(getNeighborTop(cellCoord), params, material_id_map, nearest_smp);
    float matBottom = getMaterialId(getNeighborBottom(cellCoord), params, material_id_map, nearest_smp);
    float matLeft   = getMaterialId(getNeighborLeft(cellCoord), params, material_id_map, nearest_smp);
    float matRight  = getMaterialId(getNeighborRight(cellCoord), params, material_id_map, nearest_smp);

    // Calculate UV based on mode
    float2 uv;
    if (params.uv_mode == 0) {
        // WorldUV: continuous world-space UV
        uv = in.worldPos / params.world_uv_scale;
    } else {
        // RandomTileUV: per-tile randomized offset
        float2 rnd = hash2(cellCoord) * 2.0 - 1.0;
        float2 offset = rnd * params.random_uv_strength;
        uv = (local + 0.5) + offset;
    }

    // Apply macro variation (UV warp from noise)
    float n0 = noise_tex.sample(linear_smp, uv * params.macro_scale).r;
    float n1 = noise_tex.sample(linear_smp, uv * (params.macro_scale * 0.77) + float2(13.1, 7.7)).r;
    uv = uv + (float2(n0, n1) - 0.5) * params.macro_strength;
    uv *= params.tile_scale;

    // Sample noise for blending variation
    float noiseVal = noise_tex.sample(linear_smp, in.worldPos / params.cell_size.x * params.noise_scale * 0.1).r;

    // Diamond edge distances (boundary is |x|+|y|=0.5)
    // Distance to each edge (positive inside, 0 at edge)
    float distTL = 0.5 - (-local.x - local.y);  // top-left edge (toward top vertex)
    float distTR = 0.5 - ( local.x - local.y);  // top-right edge (toward right vertex)
    float distBL = 0.5 - (-local.x + local.y);  // bottom-left edge (toward left vertex)
    float distBR = 0.5 - ( local.x + local.y);  // bottom-right edge (toward bottom vertex)

    // Corner proximity - distance to each diamond vertex
    float distToTop    = length(local - float2(0.0, -0.5));
    float distToRight  = length(local - float2(0.5, 0.0));
    float distToBottom = length(local - float2(0.0, 0.5));
    float distToLeft   = length(local - float2(-0.5, 0.0));

    // Corner proximity factor (higher near vertices, enables smoother corner blending)
    float minCornerDist = min(min(distToTop, distToBottom), min(distToLeft, distToRight));
    float cornerProximity = 1.0 - smoothstep(0.0, 0.5, minCornerDist);

    // Adaptive blend width: wider near corners for smoother transitions
    float baseBlendWidth = mix(0.35, 0.08, params.blend_sharpness);
    float edgeBlendWidth = baseBlendWidth * (1.0 + cornerProximity * 0.6);

    // Add noise jitter to blend threshold
    float noiseJitter = (noiseVal - 0.5) * (1.0 - params.blend_sharpness) * 0.2;

    // Edge blend factors: how much each neighbor should contribute
    // Using smoothstep for smooth falloff from edge toward center
    float bTL = 1.0 - smoothstep(0.0, edgeBlendWidth, distTL + noiseJitter);
    float bTR = 1.0 - smoothstep(0.0, edgeBlendWidth, distTR + noiseJitter);
    float bBL = 1.0 - smoothstep(0.0, edgeBlendWidth, distBL + noiseJitter);
    float bBR = 1.0 - smoothstep(0.0, edgeBlendWidth, distBR + noiseJitter);

    // Vertex blend factors: for orthogonal neighbors at diamond vertices
    // Use slightly wider blend for corners to ensure smooth transitions
    float cornerBlendWidth = edgeBlendWidth * 1.2;
    float bTop    = 1.0 - smoothstep(0.0, cornerBlendWidth, distToTop + noiseJitter);
    float bRight  = 1.0 - smoothstep(0.0, cornerBlendWidth, distToRight + noiseJitter);
    float bBottom = 1.0 - smoothstep(0.0, cornerBlendWidth, distToBottom + noiseJitter);
    float bLeft   = 1.0 - smoothstep(0.0, cornerBlendWidth, distToLeft + noiseJitter);

    // Sample materials (diagonal neighbors)
    int slotC  = matIdToSlot(matC);
    int slotTL = matIdToSlot(matTL);
    int slotTR = matIdToSlot(matTR);
    int slotBL = matIdToSlot(matBL);
    int slotBR = matIdToSlot(matBR);

    float4 colC  = sampleMaterial(slotC, uv, tex0, tex1, tex2, tex3, linear_smp);
    float4 colTL = sampleMaterial(slotTL, uv, tex0, tex1, tex2, tex3, linear_smp);
    float4 colTR = sampleMaterial(slotTR, uv, tex0, tex1, tex2, tex3, linear_smp);
    float4 colBL = sampleMaterial(slotBL, uv, tex0, tex1, tex2, tex3, linear_smp);
    float4 colBR = sampleMaterial(slotBR, uv, tex0, tex1, tex2, tex3, linear_smp);

    // Sample materials (orthogonal neighbors - at vertices)
    int slotTop    = matIdToSlot(matTop);
    int slotBottom = matIdToSlot(matBottom);
    int slotLeft   = matIdToSlot(matLeft);
    int slotRight  = matIdToSlot(matRight);

    float4 colTop    = sampleMaterial(slotTop, uv, tex0, tex1, tex2, tex3, linear_smp);
    float4 colBottom = sampleMaterial(slotBottom, uv, tex0, tex1, tex2, tex3, linear_smp);
    float4 colLeft   = sampleMaterial(slotLeft, uv, tex0, tex1, tex2, tex3, linear_smp);
    float4 colRight  = sampleMaterial(slotRight, uv, tex0, tex1, tex2, tex3, linear_smp);

    // Height proxy from albedo luminance for height-aware blending
    float hC  = dot(colC.rgb, float3(0.333));
    float hTL = dot(colTL.rgb, float3(0.333));
    float hTR = dot(colTR.rgb, float3(0.333));
    float hBL = dot(colBL.rgb, float3(0.333));
    float hBR = dot(colBR.rgb, float3(0.333));
    float hTop    = dot(colTop.rgb, float3(0.333));
    float hRight  = dot(colRight.rgb, float3(0.333));
    float hBottom = dot(colBottom.rgb, float3(0.333));
    float hLeft   = dot(colLeft.rgb, float3(0.333));

    // Height-aware adjustment: boost blend factor for "higher" neighbors
    // Diagonal neighbors
    bTL *= 1.0 + (hTL - hC) * params.height_influence;
    bTR *= 1.0 + (hTR - hC) * params.height_influence;
    bBL *= 1.0 + (hBL - hC) * params.height_influence;
    bBR *= 1.0 + (hBR - hC) * params.height_influence;
    // Orthogonal neighbors
    bTop    *= 1.0 + (hTop    - hC) * params.height_influence;
    bRight  *= 1.0 + (hRight  - hC) * params.height_influence;
    bBottom *= 1.0 + (hBottom - hC) * params.height_influence;
    bLeft   *= 1.0 + (hLeft   - hC) * params.height_influence;

    // Clamp blend factors
    bTL = clamp(bTL, 0.0, 1.0);
    bTR = clamp(bTR, 0.0, 1.0);
    bBL = clamp(bBL, 0.0, 1.0);
    bBR = clamp(bBR, 0.0, 1.0);
    bTop    = clamp(bTop, 0.0, 1.0);
    bRight  = clamp(bRight, 0.0, 1.0);
    bBottom = clamp(bBottom, 0.0, 1.0);
    bLeft   = clamp(bLeft, 0.0, 1.0);

    // MATERIAL PRIORITY: Only blend with neighbors that have HIGHER material ID
    // This prevents the "mirror" effect at boundaries
    // Higher ID materials "invade" lower ID materials, not vice versa
    // e.g., Sand(2) invades Grass(1), Rock(3) invades both

    // Diagonal neighbors
    if (matTL <= matC) bTL = 0.0;
    if (matTR <= matC) bTR = 0.0;
    if (matBL <= matC) bBL = 0.0;
    if (matBR <= matC) bBR = 0.0;

    // Orthogonal neighbors (at diamond vertices)
    if (matTop    <= matC) bTop    = 0.0;
    if (matRight  <= matC) bRight  = 0.0;
    if (matBottom <= matC) bBottom = 0.0;
    if (matLeft   <= matC) bLeft   = 0.0;

    // WEIGHTED BLENDING with 8 neighbors
    // Sum of all neighbor blend factors determines how much we blend away from center
    float sumNeighborBlend = bTL + bTR + bBL + bBR + bTop + bRight + bBottom + bLeft;

    // Center weight: 1 when no neighbors blend, decreases as neighbors contribute
    // Clamp sumNeighborBlend to prevent center from going negative
    float wC = max(0.0, 1.0 - sumNeighborBlend);

    // Neighbor weights are their blend factors
    float wTL = bTL;
    float wTR = bTR;
    float wBL = bBL;
    float wBR = bBR;
    float wTop    = bTop;
    float wRight  = bRight;
    float wBottom = bBottom;
    float wLeft   = bLeft;

    // Normalize weights to sum to 1
    float totalWeight = wC + wTL + wTR + wBL + wBR + wTop + wRight + wBottom + wLeft;
    if (totalWeight > 0.001) {
        float invTotal = 1.0 / totalWeight;
        wC      *= invTotal;
        wTL     *= invTotal;
        wTR     *= invTotal;
        wBL     *= invTotal;
        wBR     *= invTotal;
        wTop    *= invTotal;
        wRight  *= invTotal;
        wBottom *= invTotal;
        wLeft   *= invTotal;
    } else {
        // Fallback: just use center material
        wC = 1.0;
        wTL = wTR = wBL = wBR = 0.0;
        wTop = wRight = wBottom = wLeft = 0.0;
    }

    // Final weighted blend of all 9 materials (center + 4 diagonal + 4 orthogonal)
    float4 col = colC * wC
               + colTL * wTL + colTR * wTR + colBL * wBL + colBR * wBR
               + colTop * wTop + colRight * wRight + colBottom * wBottom + colLeft * wLeft;

    // Edge darkening (pseudo-AO at material boundaries)
    float blendAmount = 1.0 - wC; // How much we're blending with neighbors
    float edgeFactor = smoothstep(0.0, params.edge_width, blendAmount);
    col.rgb *= (1.0 - params.edge_darkness * edgeFactor);

    // Debug output modes
    if (params.debug_mode == 1) {
        // Material ID visualization (different colors for different materials)
        return float4(matC / 4.0, float(slotC) / 3.0, 1.0 - matC / 4.0, 1.0);
    } else if (params.debug_mode == 2) {
        // UV visualization (RG = fract(uv))
        return float4(fract(uv), 0.5, 1.0);
    } else if (params.debug_mode == 3) {
        // Weight visualization (R=top weights, G=bottom weights, B=center)
        return float4(wTL + wTR, wBL + wBR, wC, 1.0);
    } else if (params.debug_mode == 4) {
        // Center material only (no blending) - tests texture loading
        return colC;
    }

    // Normal render
    return col;
}
)";

static sg_image make1x1Rgba8(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    const std::uint8_t px[4] = { r, g, b, a };
    sg_image_desc desc = {};
    desc.width = 1;
    desc.height = 1;
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.data.mip_levels[0].ptr = px;
    desc.data.mip_levels[0].size = sizeof(px);
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
    desc.data.mip_levels[0].ptr = pixels;
    desc.data.mip_levels[0].size = (size_t)w * (size_t)h * 4;
    desc.label = label;
    sg_image img = sg_make_image(&desc);

    stbi_image_free(pixels);
    return img;
}

static sg_view makeTextureView(sg_image image) {
    if (image.id == SG_INVALID_ID) return { SG_INVALID_ID };

    sg_view_desc desc = {};
    desc.texture.image = image;
    return sg_make_view(&desc);
}

} // namespace

void SplattingRenderer::init() {
    ensurePipeline();

    // Dynamic vertex buffer (6 verts per cell for 2 triangles)
    sg_buffer_desc buf_desc = {};
    buf_desc.size = 6 * 65536 * (int)sizeof(Vertex);
    buf_desc.usage.dynamic_update = true;
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
        materialViews[i] = fallbackWhiteView;
    }
    noiseTexture = fallbackNoise;
    noiseTextureView = fallbackNoiseView;
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
            if (materialViews[i].id != SG_INVALID_ID) {
                sg_destroy_view(materialViews[i]);
            }
            sg_destroy_image(materials[i]);
        }
        materials[i].id = SG_INVALID_ID;
        materialViews[i].id = SG_INVALID_ID;
    }
    if (noiseTexture.id != SG_INVALID_ID && noiseTexture.id != fallbackNoise.id) {
        if (noiseTextureView.id != SG_INVALID_ID) {
            sg_destroy_view(noiseTextureView);
        }
        sg_destroy_image(noiseTexture);
    }
    noiseTexture.id = SG_INVALID_ID;
    noiseTextureView.id = SG_INVALID_ID;

    if (materialIdMapView.id != SG_INVALID_ID) {
        sg_destroy_view(materialIdMapView);
        materialIdMapView.id = SG_INVALID_ID;
    }
    if (materialIdMap.id != SG_INVALID_ID) {
        sg_destroy_image(materialIdMap);
        materialIdMap.id = SG_INVALID_ID;
    }

    if (fallbackWhiteView.id != SG_INVALID_ID) {
        sg_destroy_view(fallbackWhiteView);
        fallbackWhiteView.id = SG_INVALID_ID;
    }
    if (fallbackWhite.id != SG_INVALID_ID) {
        sg_destroy_image(fallbackWhite);
        fallbackWhite.id = SG_INVALID_ID;
    }
    if (fallbackNoiseView.id != SG_INVALID_ID) {
        sg_destroy_view(fallbackNoiseView);
        fallbackNoiseView.id = SG_INVALID_ID;
    }
    if (fallbackNoise.id != SG_INVALID_ID) {
        sg_destroy_image(fallbackNoise);
        fallbackNoise.id = SG_INVALID_ID;
    }
}

bool SplattingRenderer::loadMaterial(int slot, const std::string& path) {
    if (slot < 0 || slot >= 4) return false;
    if (materials[slot].id != SG_INVALID_ID && materials[slot].id != fallbackWhite.id) {
        if (materialViews[slot].id != SG_INVALID_ID) {
            sg_destroy_view(materialViews[slot]);
        }
        sg_destroy_image(materials[slot]);
    }
    const std::string label = "material-" + std::to_string(slot);
    sg_image img = loadImageRgba8(path, label.c_str());
    if (img.id == SG_INVALID_ID) {
        materials[slot] = fallbackWhite;
        materialViews[slot] = fallbackWhiteView;
        return false;
    }
    materials[slot] = img;
    materialViews[slot] = makeTextureView(img);
    return true;
}

bool SplattingRenderer::loadNoiseTexture(const std::string& path) {
    if (noiseTexture.id != SG_INVALID_ID && noiseTexture.id != fallbackNoise.id) {
        if (noiseTextureView.id != SG_INVALID_ID) {
            sg_destroy_view(noiseTextureView);
        }
        sg_destroy_image(noiseTexture);
    }
    sg_image img = loadImageRgba8(path, "noise");
    if (img.id == SG_INVALID_ID) {
        noiseTexture = fallbackNoise;
        noiseTextureView = fallbackNoiseView;
        return false;
    }
    noiseTexture = img;
    noiseTextureView = makeTextureView(img);
    return true;
}

void SplattingRenderer::updateMaterialIdMap(const MaterialMap& map) {
    if (map.width <= 0 || map.height <= 0) return;

    const bool needRecreate = (materialIdMap.id == SG_INVALID_ID ||
                                materialIdMapWidth != map.width ||
                                materialIdMapHeight != map.height);

    if (needRecreate) {
        if (materialIdMapView.id != SG_INVALID_ID) {
            sg_destroy_view(materialIdMapView);
            materialIdMapView.id = SG_INVALID_ID;
        }
        if (materialIdMap.id != SG_INVALID_ID) {
            sg_destroy_image(materialIdMap);
            materialIdMap.id = SG_INVALID_ID;
        }

        // Create R8 texture (material IDs 0-255)
        // We store as RGBA8 for compatibility (R channel = material ID)
        materialIdData.resize((size_t)map.width * (size_t)map.height * 4);

        sg_image_desc desc = {};
        desc.width = map.width;
        desc.height = map.height;
        desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        desc.usage.dynamic_update = true;
        desc.label = "material-id-map";
        materialIdMap = sg_make_image(&desc);
        materialIdMapView = makeTextureView(materialIdMap);

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
    data.mip_levels[0].ptr = materialIdData.data();
    data.mip_levels[0].size = materialIdData.size();
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
    bind.views[0] = materialViews[0].id != SG_INVALID_ID ? materialViews[0] : fallbackWhiteView;
    bind.views[1] = materialViews[1].id != SG_INVALID_ID ? materialViews[1] : fallbackWhiteView;
    bind.views[2] = materialViews[2].id != SG_INVALID_ID ? materialViews[2] : fallbackWhiteView;
    bind.views[3] = materialViews[3].id != SG_INVALID_ID ? materialViews[3] : fallbackWhiteView;
    bind.views[4] = noiseTextureView.id != SG_INVALID_ID ? noiseTextureView : fallbackNoiseView;
    bind.views[5] = materialIdMapView.id != SG_INVALID_ID ? materialIdMapView : fallbackWhiteView;
    bind.samplers[0] = linearSampler;
    bind.samplers[1] = nearestSampler;

    sg_apply_pipeline(pip);
    sg_apply_bindings(&bind);

    VsParams vs = {};
    vs.viewSize[0] = (float)viewWidth;
    vs.viewSize[1] = (float)viewHeight;
    vs.cameraOffset[0] = camera.offset.x;
    vs.cameraOffset[1] = camera.offset.y;
    vs.cameraZoom = camera.zoom;
    sg_range vs_range = { &vs, sizeof(vs) };
    sg_apply_uniforms(0, &vs_range);

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
    sg_apply_uniforms(1, &fs_range);

    sg_draw(0, (int)vertices.size(), 1);
}

void SplattingRenderer::ensureFallbackTextures() {
    if (fallbackWhite.id == SG_INVALID_ID) {
        fallbackWhite = make1x1Rgba8(255, 255, 255, 255);
        fallbackWhiteView = makeTextureView(fallbackWhite);
    }
    if (fallbackNoise.id == SG_INVALID_ID) {
        fallbackNoise = make1x1Rgba8(128, 128, 128, 255);
        fallbackNoiseView = makeTextureView(fallbackNoise);
    }
}

void SplattingRenderer::ensurePipeline() {
    if (pip.id != SG_INVALID_ID) return;

    sg_shader_desc shd_desc = {};
#if defined(SOKOL_D3D11)
    shd_desc.vertex_func.source = vs_src_hlsl;
    shd_desc.fragment_func.source = fs_src_hlsl;
    shd_desc.attrs[0].hlsl_sem_name = "TEXCOORD";
    shd_desc.attrs[0].hlsl_sem_index = 0;
    shd_desc.attrs[1].hlsl_sem_name = "TEXCOORD";
    shd_desc.attrs[1].hlsl_sem_index = 1;
    shd_desc.attrs[2].hlsl_sem_name = "TEXCOORD";
    shd_desc.attrs[2].hlsl_sem_index = 2;
#elif defined(SOKOL_METAL)
    shd_desc.vertex_func.source = vs_src_msl;
    shd_desc.fragment_func.source = fs_src_msl;
#else
    shd_desc.vertex_func.source = vs_src_glsl;
    shd_desc.fragment_func.source = fs_src_glsl;
#endif

    // VS uniforms
    shd_desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd_desc.uniform_blocks[0].size = sizeof(VsParams);
    shd_desc.uniform_blocks[0].hlsl_register_b_n = 0;
    shd_desc.uniform_blocks[0].msl_buffer_n = 0;
    shd_desc.uniform_blocks[0].wgsl_group0_binding_n = 0;
    shd_desc.uniform_blocks[0].spirv_set0_binding_n = 0;
    shd_desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "view_size";
    shd_desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT2;
    shd_desc.uniform_blocks[0].glsl_uniforms[1].glsl_name = "camera_offset";
    shd_desc.uniform_blocks[0].glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT2;
    shd_desc.uniform_blocks[0].glsl_uniforms[2].glsl_name = "camera_zoom";
    shd_desc.uniform_blocks[0].glsl_uniforms[2].type = SG_UNIFORMTYPE_FLOAT;

    // FS uniforms
    shd_desc.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.uniform_blocks[1].size = sizeof(FsParams);
    shd_desc.uniform_blocks[1].hlsl_register_b_n = 0;
    shd_desc.uniform_blocks[1].msl_buffer_n = 1;
    shd_desc.uniform_blocks[1].wgsl_group0_binding_n = 1;
    shd_desc.uniform_blocks[1].spirv_set0_binding_n = 1;
    shd_desc.uniform_blocks[1].glsl_uniforms[0].glsl_name = "cell_size";
    shd_desc.uniform_blocks[1].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT2;
    shd_desc.uniform_blocks[1].glsl_uniforms[1].glsl_name = "map_size";
    shd_desc.uniform_blocks[1].glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT2;
    shd_desc.uniform_blocks[1].glsl_uniforms[2].glsl_name = "blend_sharpness";
    shd_desc.uniform_blocks[1].glsl_uniforms[2].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.uniform_blocks[1].glsl_uniforms[3].glsl_name = "noise_scale";
    shd_desc.uniform_blocks[1].glsl_uniforms[3].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.uniform_blocks[1].glsl_uniforms[4].glsl_name = "tile_scale";
    shd_desc.uniform_blocks[1].glsl_uniforms[4].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.uniform_blocks[1].glsl_uniforms[5].glsl_name = "macro_scale";
    shd_desc.uniform_blocks[1].glsl_uniforms[5].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.uniform_blocks[1].glsl_uniforms[6].glsl_name = "macro_strength";
    shd_desc.uniform_blocks[1].glsl_uniforms[6].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.uniform_blocks[1].glsl_uniforms[7].glsl_name = "height_influence";
    shd_desc.uniform_blocks[1].glsl_uniforms[7].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.uniform_blocks[1].glsl_uniforms[8].glsl_name = "edge_darkness";
    shd_desc.uniform_blocks[1].glsl_uniforms[8].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.uniform_blocks[1].glsl_uniforms[9].glsl_name = "edge_width";
    shd_desc.uniform_blocks[1].glsl_uniforms[9].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.uniform_blocks[1].glsl_uniforms[10].glsl_name = "world_uv_scale";
    shd_desc.uniform_blocks[1].glsl_uniforms[10].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.uniform_blocks[1].glsl_uniforms[11].glsl_name = "random_uv_strength";
    shd_desc.uniform_blocks[1].glsl_uniforms[11].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.uniform_blocks[1].glsl_uniforms[12].glsl_name = "uv_mode";
    shd_desc.uniform_blocks[1].glsl_uniforms[12].type = SG_UNIFORMTYPE_INT;
    shd_desc.uniform_blocks[1].glsl_uniforms[13].glsl_name = "debug_mode";
    shd_desc.uniform_blocks[1].glsl_uniforms[13].type = SG_UNIFORMTYPE_INT;

    // FS images (6: 4 materials + noise + materialIdMap)
    const char* glslNames[] = {"tex0", "tex1", "tex2", "tex3", "noise_tex", "material_id_map"};
    for (int i = 0; i < 6; i++) {
        shd_desc.views[i].texture.stage = SG_SHADERSTAGE_FRAGMENT;
        shd_desc.views[i].texture.image_type = SG_IMAGETYPE_2D;
        shd_desc.views[i].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
        shd_desc.views[i].texture.hlsl_register_t_n = (uint8_t)i;
        shd_desc.views[i].texture.msl_texture_n = (uint8_t)i;
        shd_desc.views[i].texture.wgsl_group1_binding_n = (uint8_t)i;
        shd_desc.views[i].texture.spirv_set1_binding_n = (uint8_t)i;
        shd_desc.texture_sampler_pairs[i].stage = SG_SHADERSTAGE_FRAGMENT;
        shd_desc.texture_sampler_pairs[i].view_slot = i;
        shd_desc.texture_sampler_pairs[i].sampler_slot = (i < 5) ? 0 : 1;
        shd_desc.texture_sampler_pairs[i].glsl_name = glslNames[i];
    }

    // 2 samplers
    shd_desc.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    shd_desc.samplers[0].hlsl_register_s_n = 0;
    shd_desc.samplers[0].msl_sampler_n = 0;
    shd_desc.samplers[0].wgsl_group1_binding_n = 6;
    shd_desc.samplers[0].spirv_set1_binding_n = 6;
    shd_desc.samplers[1].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.samplers[1].sampler_type = SG_SAMPLERTYPE_FILTERING;
    shd_desc.samplers[1].hlsl_register_s_n = 1;
    shd_desc.samplers[1].msl_sampler_n = 1;
    shd_desc.samplers[1].wgsl_group1_binding_n = 7;
    shd_desc.samplers[1].spirv_set1_binding_n = 7;

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
