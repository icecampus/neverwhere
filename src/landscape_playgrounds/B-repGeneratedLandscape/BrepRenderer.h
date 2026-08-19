#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

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

#include <sokol_gfx.h>

#include <landscape_mesh/landscape_mesh.h>
#include <topology_core/camera2d.h>
#include <topology_core/diamond_isometry.h>

// B-rep mesh vertex: field-space screen position with the baked z, the
// shading normal (mesh-authoritative litWallNormal for wall panels, the
// facet normal elsewhere), the world position (map cells, world height) for
// the material projections and the baked quad color.
struct BrepVertex {
    float x, y, z;
    float nx, ny, nz;
    float wx, wy, wz;
    float r, g, b, a;
};

// Baked depth anchor: a constant, NOT camera-derived. The grid overlay
// re-projects every frame with these same constants (GridRenderer.cpp), so
// the mesh stream baked once per rebuild stays in sync with it on pan
// (SDFGeneratedLandscape gotcha: a camera-derived anchor lets the grid hop
// over/under the mesh).
constexpr float kBrepZFar = 100000.0f;
constexpr float kBrepZScale = 1.0f / 200000.0f;

// Baked z for a vertex sitting liftPx screen px above its ground anchor
// (liftPx == worldY * heightScale, the exact lift applied to its screen y).
inline constexpr float brepBakedDepth(float fieldY, float liftPx) {
    return (kBrepZFar - (fieldY + liftPx)) * kBrepZScale;
}

// Triangulate one composed quad into a vertex stream (a-b-c, a-c-d):
// projects the mesh-world position into field space (origin = bbox min node,
// the composer runs at cellSize 1) and bakes z from the ground fieldY plus
// the height lift. Shared by the renderer's re-bake and the smoke test.
void appendBrepQuadVertices(
    const landscape_mesh::MeshQuad& quad,
    glm::ivec2 origin,
    float halfW,
    float halfH,
    float heightScale,
    std::vector<BrepVertex>& out);

// Generation params of the B-rep composer. Everything but heightScale feeds
// composeSolidMaskMesh (heavy: debounced mesh rebuild); heightScale only
// scales the vertex lift (cheap: stream re-bake of the cached quads).
struct BrepGenParams {
    float raisedHeight = 3.0f;   // plateau height, world units
    int rockSeed = 1337;
    float rockAmplitude = 0.28f;
    float cornerBevel = 0.3f;
    bool rockEnabled = true;
    int wallSubdivH = 16;        // wall subdivisions, horizontal
    int wallSubdivV = 16;        // wall subdivisions, vertical
    landscape_mesh::WallStyleId wallStyle = landscape_mesh::WallStyleId::Cyclopean;
    float heightScale = 96.0f;   // field px per 1.0 world height (stream-only)
};

// Fragment-shader uniforms of the B-rep pass (16-byte blocks; the GL backend
// validates that the declared uniforms sum exactly to the block size).
struct BrepFsParams {
    float lightDir[4];   // xyz: direction towards the sun
    float viewDir[4];    // xyz: constant iso view direction (viewer -> scene)
    float params0[4];    // ambient, diffuse, spec strength, spec power
    float params1[4];    // gamma, material V tiling factor (2 for 2:1 sets
                         // like Ground061, 1 for square like marble_cliff_01;
                         // measured from the map aspect at load), spare x2
    float params2[4];    // material tiling (repeats per world unit, multiplied
                         // by the V factor for V), albedo strength (0 = plain
                         // quad color), normal-map strength, AO strength
    float params3[4];    // roughness strength, spare x3
};

// Rebuild/render status for the UI and the --shot settle check.
struct BrepStats {
    bool pending = false;      // edits happened, waiting out the debounce
    double rebuildMs = 0.0;
    int quadCount = 0;
    int topQuadCount = 0;
    int wallQuadCount = 0;
    int vertexCount = 0;
    bool seamsPassed = true;
    int seamMismatches = 0;
    int seamCheckedEdges = 0;
};

// The B-rep pass: vertex nodes -> solid mask -> composeSolidMaskMesh
// (landscape_mesh, Cyclopean wall style by default) -> baked vertex stream.
// The composer runs on the CPU and is debounced (0.3 s) after the last edit;
// the stream upload is one sg_update_buffer per rebuild.
class BrepRenderer {
public:
    // VS uniforms: camera only (20 bytes).
    struct VsParams {
        float view_size[2];
        float camera_offset[2];
        float camera_zoom;
    };

    void init();
    void shutdown();

    // Loads a material set into texture views 0..3. Both naming conventions
    // are probed per channel: ambientCG (`<setName>_Color/_NormalGL/
    // _AmbientOcclusion/_Roughness.jpg`) and Poly Haven (`<setName>_diff_4k/
    // _nor_gl_4k/_ao_4k/_rough_4k.jpg|png`). Only stb-readable formats — EXR
    // sources must be converted to PNG first. Each missing/failed file keeps
    // its 1x1 placeholder bound (white color, flat normal, white AO/roughness),
    // so the shader stays valid and just falls back towards the quad color.
    // Returns the number of maps successfully loaded (0..4).
    int loadMaterialMaps(const std::string& dir, const std::string& setName = "Ground061");

    // V tiling factor measured from the loaded map aspect (2 for 2:1 sets, 1
    // for square ones); 2.0 until anything loads. Feed into params1[1].
    float materialVTile() const { return m_matVTile; }

    // Feed the current node field + generation params (call every frame; the
    // change detection lives inside). Node/generation changes mark the mesh
    // dirty (debounced composer rebuild in update()); a heightScale-only
    // change marks just the stream dirty (cheap vertex re-bake).
    void setContent(const std::uint8_t* nodes, int nodesX, int nodesY, const BrepGenParams& params);

    // Debounce driver: runs the pending rebuild/re-bake. Once per frame,
    // before render().
    void update(const topology_core::DiamondIsometry& iso, double nowSec);

    void render(const topology_core::Camera2D& camera, int viewW, int viewH, const BrepFsParams& fs);

    const BrepStats& stats() const { return m_stats; }

private:
    struct MatSlot {
        sg_image image{};
        sg_view view{};
    };

    void destroySlot(MatSlot& slot);
    // Uploads a full CPU-built mip chain (2x2 box filter) — the material maps
    // minify hard on the iso plane, and a single-level REPEAT texture aliases
    // into noise. Sampler must use LINEAR mipmaps.
    bool uploadSlotMipmapped(MatSlot& slot, const void* rgba, int width, int height, const char* label);
    void rebuildMesh(const topology_core::DiamondIsometry& iso);
    void rebakeStream(const topology_core::DiamondIsometry& iso);
    void uploadStream();

    sg_shader m_shader{};
    sg_pipeline m_pip{};
    sg_buffer m_vbuf{};
    std::size_t m_vbufSize = 0;
    sg_sampler m_matSampler{};
    MatSlot m_matMaps[4]{};

    // Last content fed by setContent (kept for the debounced rebuild).
    std::vector<std::uint8_t> m_nodes;
    int m_nodesX = 0;
    int m_nodesY = 0;
    BrepGenParams m_params;

    bool m_meshDirty = false;   // composer rebuild needed (heavy)
    bool m_streamDirty = false; // vertex re-bake only (heightScale edits)
    double m_pendingSince = -1.0;

    glm::ivec2 m_origin{0, 0}; // node coords of the cached bbox min corner
    std::vector<landscape_mesh::MeshQuad> m_quads;
    std::vector<BrepVertex> m_stream;
    BrepStats m_stats;
    float m_matVTile = 2.0f; // from the loaded map aspect (2:1 sets vs square)
    bool m_ready = false;
};
