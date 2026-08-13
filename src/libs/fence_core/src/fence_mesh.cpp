#include "pch.h"

#include "fence_core/fence_mesh.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <tuple>
#include <unordered_map>

namespace fence_core {

namespace {

// --- OBJ/MTL parsing ---------------------------------------------------------
// Reader for the exact ShapeML Exporter output (exporter.cc): "v/vn/vt" lists,
// "usemtl" groups of triangular faces ("f v/t/n" or "f v//n"), and a sibling
// MTL with "newmtl"/"Kd". No negative indices, no polygons, no textures in
// our bakes.

using MaterialTable = std::unordered_map<std::string, glm::vec3>;

MaterialTable parseMtl(const std::string& path) {
    MaterialTable table;
    std::ifstream in(path);
    if (!in) {
        return table;
    }
    std::string current;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string tag;
        ls >> tag;
        if (tag == "newmtl") {
            ls >> current;
        } else if (tag == "Kd" && !current.empty()) {
            glm::vec3 kd{1.0f};
            ls >> kd.r >> kd.g >> kd.b;
            table[current] = kd;
        }
    }
    return table;
}

// Face token "v/t/n" or "v//n" (1-based). Returns false on a malformed token.
bool parseFaceToken(const std::string& token, int* vIdx, int* nIdx) {
    const size_t s1 = token.find('/');
    if (s1 == std::string::npos) {
        *vIdx = std::atoi(token.c_str());
        *nIdx = 0;
        return *vIdx > 0;
    }
    const size_t s2 = token.find('/', s1 + 1);
    *vIdx = std::atoi(token.substr(0, s1).c_str());
    if (s2 == std::string::npos) {
        // "v/t" — no normal; should not happen in ShapeML exports.
        *nIdx = 0;
    } else {
        *nIdx = std::atoi(token.substr(s2 + 1).c_str());
    }
    return *vIdx > 0 && *nIdx > 0;
}

} // namespace

bool loadFenceMeshObj(const std::string& objPath, FenceMesh* out, std::string* error) {
    std::ifstream in(objPath);
    if (!in) {
        if (error) {
            *error = "cannot open " + objPath;
        }
        return false;
    }

    // The material library sits next to the OBJ (mtllib line, plain stem in
    // the ShapeML writer).
    const std::string mtlPath = objPath.substr(0, objPath.size() - 4) + ".mtl";
    const MaterialTable materials = parseMtl(mtlPath);

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::map<std::tuple<int, int, int>, std::uint32_t> remap;
    glm::vec3 curAlbedo{0.8f, 0.8f, 0.8f};
    int matIdx = 0;
    int matCounter = 0;

    FenceMesh mesh;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string tag;
        ls >> tag;
        if (tag == "v") {
            glm::vec3 p;
            ls >> p.x >> p.y >> p.z;
            positions.push_back(p);
        } else if (tag == "vn") {
            glm::vec3 n;
            ls >> n.x >> n.y >> n.z;
            normals.push_back(n);
        } else if (tag == "usemtl") {
            std::string name;
            ls >> name;
            const auto it = materials.find(name);
            curAlbedo = it != materials.end() ? it->second : glm::vec3{0.8f};
            matIdx = matCounter++;
        } else if (tag == "f") {
            std::uint32_t tri[3];
            bool faceOk = true;
            for (int i = 0; i < 3; ++i) {
                std::string token;
                if (!(ls >> token)) {
                    faceOk = false;
                    break;
                }
                int v = 0;
                int n = 0;
                if (!parseFaceToken(token, &v, &n) || v > static_cast<int>(positions.size()) ||
                    n > static_cast<int>(normals.size())) {
                    faceOk = false;
                    break;
                }
                const auto key = std::make_tuple(v, n, matIdx);
                const auto it = remap.find(key);
                if (it != remap.end()) {
                    tri[i] = it->second;
                } else {
                    FenceMesh::Vertex vert;
                    vert.pos = positions[v - 1];
                    vert.nrm = normals[n - 1];
                    vert.rgb = curAlbedo;
                    tri[i] = static_cast<std::uint32_t>(mesh.vertices.size());
                    mesh.vertices.push_back(vert);
                    remap.emplace(key, tri[i]);
                }
            }
            if (faceOk) {
                mesh.indices.push_back(tri[0]);
                mesh.indices.push_back(tri[1]);
                mesh.indices.push_back(tri[2]);
            }
        }
    }

    if (mesh.indices.empty()) {
        if (error) {
            *error = "no faces in " + objPath;
        }
        return false;
    }

    mesh.aabbMin = glm::vec3(std::numeric_limits<float>::max());
    mesh.aabbMax = glm::vec3(std::numeric_limits<float>::lowest());
    for (const FenceMesh::Vertex& v : mesh.vertices) {
        mesh.aabbMin = glm::min(mesh.aabbMin, v.pos);
        mesh.aabbMax = glm::max(mesh.aabbMax, v.pos);
    }

    *out = std::move(mesh);
    return true;
}

bool loadFenceMeshSet(const std::string& dir, FenceMeshSet* out, std::string* error) {
    FenceMeshSet set;
    const std::pair<const char*, FenceMesh*> files[4] = {
        {"fence_post.obj", &set.post},
        {"fence_corner.obj", &set.corner},
        {"fence_section2.obj", &set.section2},
        {"fence_section3.obj", &set.section3},
    };
    for (const auto& [name, slot] : files) {
        if (!loadFenceMeshObj(dir + "/" + name, slot, error)) {
            return false;
        }
    }
    set.ok = true;
    *out = std::move(set);
    return true;
}

std::string findRepoRoot() {
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::current_path(ec);
    if (ec) {
        return ".";
    }
    for (int i = 0; i < 12; ++i) {
        if (std::filesystem::exists(dir / ".git", ec)) {
            return dir.string();
        }
        if (!dir.has_parent_path() || dir == dir.parent_path()) {
            break;
        }
        dir = dir.parent_path();
    }
    return ".";
}

bool isCornerPost(const FenceModel& model, int postPieceId) {
    glm::ivec2 firstAxis{0, 0};
    int incident = 0;
    for (const FencePiece& piece : model.pieces()) {
        if (piece.kind != FencePieceKind::Section ||
            (piece.postA != postPieceId && piece.postB != postPieceId)) {
            continue;
        }
        if (incident == 0) {
            firstAxis = piece.axis;
        }
        ++incident;
    }
    if (incident != 2) {
        return false;
    }
    // Exactly two sections: a corner when they are not collinear. Compare
    // against the first axis found (re-walk to keep it simple).
    for (const FencePiece& piece : model.pieces()) {
        if (piece.kind != FencePieceKind::Section ||
            (piece.postA != postPieceId && piece.postB != postPieceId)) {
            continue;
        }
        const int dot = piece.axis.x * firstAxis.x + piece.axis.y * firstAxis.y;
        if (dot == 0) {
            return true;
        }
    }
    return false;
}

void appendFenceInstance(
    const FenceMesh& mesh,
    glm::vec3 offset,
    float yawDeg,
    FenceInstanceShading shading,
    glm::vec4 flatColor,
    std::vector<FenceWorldVertex>& out) {

    // Fixed sun + hemisphere ambient, baked into the vertex color.
    const glm::vec3 sun = glm::normalize(glm::vec3{-0.55f, 0.80f, -0.35f});

    const float rad = yawDeg * 3.14159265358979f / 180.0f;
    const float cs = std::cos(rad);
    const float sn = std::sin(rad);

    out.reserve(out.size() + mesh.indices.size());
    for (const std::uint32_t idx : mesh.indices) {
        const FenceMesh::Vertex& v = mesh.vertices[idx];
        // rotY: x' = x*c + z*s, z' = -x*s + z*c.
        const glm::vec3 p{
            v.pos.x * cs + v.pos.z * sn + offset.x,
            v.pos.y + offset.y,
            -v.pos.x * sn + v.pos.z * cs + offset.z};

        glm::vec4 rgba = flatColor;
        if (shading != FenceInstanceShading::Flat) {
            const glm::vec3 n{
                v.nrm.x * cs + v.nrm.z * sn,
                v.nrm.y,
                -v.nrm.x * sn + v.nrm.z * cs};
            const float diff = std::max(glm::dot(n, sun), 0.0f);
            const float up = n.y * 0.5f + 0.5f;
            const float light = std::min(0.40f + 0.30f * up + 0.55f * diff, 1.25f);
            glm::vec3 rgb = v.rgb * light;
            if (shading == FenceInstanceShading::Selected) {
                rgb = glm::mix(rgb, glm::vec3{0.95f, 0.75f, 0.30f}, 0.5f);
            }
            rgba = {rgb.r, rgb.g, rgb.b, 1.0f};
        }
        out.push_back({{p.x, p.y, p.z}, {rgba.r, rgba.g, rgba.b, rgba.a}});
    }
}

std::vector<FenceWorldVertex> buildFenceWorldTriangles(
    const FenceModel& model,
    const FenceMeshSet& meshes,
    int selectedFence) {

    std::vector<FenceWorldVertex> out;

    for (const FencePiece& piece : model.pieces()) {
        const FenceMesh* mesh = nullptr;
        glm::vec3 offset{0.0f};
        float yawDeg = 0.0f;

        if (piece.kind == FencePieceKind::Post) {
            mesh = isCornerPost(model, piece.id) ? &meshes.corner : &meshes.post;
            // Cell center is the integer world point (cx,cz) — the same
            // convention mapToField uses (a +0.5 shift would land the post on
            // the cell's Down corner node instead of inside the diamond).
            offset = {static_cast<float>(piece.cell.x), 0.0f, static_cast<float>(piece.cell.y)};
            // Deterministic quarter-turn per post: cheap variety for the
            // random boulder scars.
            yawDeg = static_cast<float>((piece.id % 4) * 90);
        } else {
            const FencePiece* postA = model.pieceById(piece.postA);
            const FencePiece* postB = model.pieceById(piece.postB);
            if (!postA || !postB) {
                continue;
            }
            mesh = piece.length >= 2 ? &meshes.section3 : &meshes.section2;
            offset = {static_cast<float>(postA->cell.x), 0.0f, static_cast<float>(postA->cell.y)};
            // The section mesh spans +x from the first post axis; yaw maps +x
            // onto the postA->postB direction (rotY: +x -> +z at -90 deg).
            const glm::ivec2 d = postB->cell - postA->cell;
            if (d.x > 0) {
                yawDeg = 0.0f;
            } else if (d.x < 0) {
                yawDeg = 180.0f;
            } else if (d.y > 0) {
                yawDeg = -90.0f;
            } else {
                yawDeg = 90.0f;
            }
        }

        const bool selected = piece.fenceId == selectedFence && selectedFence >= 0;
        appendFenceInstance(
            *mesh, offset, yawDeg,
            selected ? FenceInstanceShading::Selected : FenceInstanceShading::Lit,
            glm::vec4{0.0f}, out);
    }
    return out;
}

} // namespace fence_core
