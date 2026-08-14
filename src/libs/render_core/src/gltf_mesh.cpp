#include "render_core/gltf_mesh.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace render_core {
namespace {

constexpr int kCompByte = 5120;
constexpr int kCompUByte = 5121;
constexpr int kCompShort = 5122;
constexpr int kCompUShort = 5123;
constexpr int kCompUInt = 5125;
constexpr int kCompFloat = 5126;

int typeComponentCount(const std::string& type) {
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4") return 4;
    if (type == "MAT4") return 16;
    return 0;
}

int componentSize(int componentType) {
    switch (componentType) {
    case kCompByte:
    case kCompUByte:
        return 1;
    case kCompShort:
    case kCompUShort:
        return 2;
    case kCompUInt:
    case kCompFloat:
        return 4;
    default:
        return 0;
    }
}

float readComponent(const std::uint8_t* p, int componentType) {
    switch (componentType) {
    case kCompByte:
        return static_cast<float>(*reinterpret_cast<const std::int8_t*>(p));
    case kCompUByte:
        return static_cast<float>(*p);
    case kCompShort: {
        std::int16_t v = 0;
        std::memcpy(&v, p, 2);
        return static_cast<float>(v);
    }
    case kCompUShort: {
        std::uint16_t v = 0;
        std::memcpy(&v, p, 2);
        return static_cast<float>(v);
    }
    case kCompUInt: {
        std::uint32_t v = 0;
        std::memcpy(&v, p, 4);
        return static_cast<float>(v);
    }
    case kCompFloat: {
        float v = 0.0f;
        std::memcpy(&v, p, 4);
        return v;
    }
    default:
        return 0.0f;
    }
}

bool accessorPointer(const nlohmann::json& j, int accessorIndex, const std::uint8_t* bin, std::size_t binSize,
    const std::uint8_t** ptr, std::size_t* count, std::size_t* stride, int* componentType, int* ncomp, std::string* error) {
    const auto& accessors = j["accessors"];
    if (accessorIndex < 0 || accessorIndex >= static_cast<int>(accessors.size())) {
        if (error) *error = "accessor index out of range";
        return false;
    }
    const auto& acc = accessors[accessorIndex];
    *componentType = acc.at("componentType").get<int>();
    *count = acc.at("count").get<std::size_t>();
    *ncomp = typeComponentCount(acc.at("type").get<std::string>());
    if (*ncomp <= 0 || *count == 0) {
        if (error) *error = "empty or unknown accessor type";
        return false;
    }
    if (!acc.contains("bufferView")) {
        if (error) *error = "accessor missing bufferView";
        return false;
    }
    const int viewIndex = acc.at("bufferView").get<int>();
    const auto& views = j["bufferViews"];
    if (viewIndex < 0 || viewIndex >= static_cast<int>(views.size())) {
        if (error) *error = "bufferView index out of range";
        return false;
    }
    const auto& view = views[viewIndex];
    const std::size_t viewOffset = view.value("byteOffset", 0);
    const std::size_t accOffset = acc.value("byteOffset", 0);
    const std::size_t viewLength = view.at("byteLength").get<std::size_t>();
    const int elemSize = componentSize(*componentType) * *ncomp;
    const std::size_t viewStride = view.value("byteStride", elemSize);
    if (viewStride < static_cast<std::size_t>(elemSize)) {
        if (error) *error = "bufferView stride smaller than element";
        return false;
    }
    const std::size_t start = viewOffset + accOffset;
    const std::size_t need = start + (*count - 1) * viewStride + static_cast<std::size_t>(elemSize);
    if (start > binSize || need > binSize || accOffset + (*count - 1) * viewStride + elemSize > viewLength) {
        if (error) *error = "accessor reads past the BIN chunk";
        return false;
    }
    *ptr = bin + start;
    *stride = viewStride;
    return true;
}

glm::vec3 readVec3(const std::uint8_t* ptr, std::size_t stride, int componentType, std::size_t i) {
    const std::uint8_t* p = ptr + i * stride;
    return glm::vec3(readComponent(p, componentType), readComponent(p + componentSize(componentType), componentType),
        readComponent(p + 2 * componentSize(componentType), componentType));
}

glm::vec2 readVec2(const std::uint8_t* ptr, std::size_t stride, int componentType, std::size_t i) {
    const std::uint8_t* p = ptr + i * stride;
    return glm::vec2(readComponent(p, componentType), readComponent(p + componentSize(componentType), componentType));
}

std::uint32_t readIndex(const std::uint8_t* ptr, std::size_t stride, int componentType, std::size_t i) {
    return static_cast<std::uint32_t>(readComponent(ptr + i * stride, componentType));
}

float jsonNumber(const nlohmann::json& v) {
    if (v.is_number_float()) return static_cast<float>(v.get<double>());
    if (v.is_number_unsigned()) return static_cast<float>(v.get<std::uint64_t>());
    if (v.is_number_integer()) return static_cast<float>(v.get<std::int64_t>());
    return 0.0f;
}

glm::mat4 nodeMatrix(const nlohmann::json& node) {
    if (node.contains("matrix")) {
        const auto& m = node["matrix"];
        glm::mat4 out(1.0f);
        for (int i = 0; i < 16 && i < static_cast<int>(m.size()); ++i) {
            out[i / 4][i % 4] = jsonNumber(m[i]);
        }
        return out;
    }
    glm::vec3 t(0.0f);
    glm::vec3 s(1.0f);
    glm::quat r(1.0f, 0.0f, 0.0f, 0.0f);
    if (node.contains("translation")) {
        t.x = jsonNumber(node["translation"][0]);
        t.y = jsonNumber(node["translation"][1]);
        t.z = jsonNumber(node["translation"][2]);
    }
    if (node.contains("scale")) {
        s.x = jsonNumber(node["scale"][0]);
        s.y = jsonNumber(node["scale"][1]);
        s.z = jsonNumber(node["scale"][2]);
    }
    if (node.contains("rotation")) {
        // glTF quaternion is xyzw.
        r.x = jsonNumber(node["rotation"][0]);
        r.y = jsonNumber(node["rotation"][1]);
        r.z = jsonNumber(node["rotation"][2]);
        r.w = jsonNumber(node["rotation"][3]);
    }
    glm::mat4 out(1.0f);
    out = glm::translate(out, t);
    out *= glm::mat4_cast(r);
    out = glm::scale(out, s);
    return out;
}

bool appendPrimitive(const nlohmann::json& j, const nlohmann::json& prim, const std::uint8_t* bin, std::size_t binSize,
    const glm::mat4& world, GltfMesh& out, std::string* error) {
    const int mode = prim.value("mode", 4);
    if (mode != 4) {
        if (error) *error = "only TRIANGLES primitives are supported";
        return false;
    }
    const auto& attrs = prim.at("attributes");
    if (!attrs.contains("POSITION")) {
        if (error) *error = "primitive missing POSITION";
        return false;
    }

    const std::uint8_t* posPtr = nullptr;
    const std::uint8_t* nrmPtr = nullptr;
    const std::uint8_t* uvPtr = nullptr;
    std::size_t posCount = 0, posStride = 0, nrmCount = 0, nrmStride = 0, uvCount = 0, uvStride = 0;
    int posType = 0, nrmType = 0, uvType = 0, posN = 0, nrmN = 0, uvN = 0;
    if (!accessorPointer(j, attrs["POSITION"].get<int>(), bin, binSize, &posPtr, &posCount, &posStride, &posType, &posN, error)) {
        return false;
    }
    if (posN != 3) {
        if (error) *error = "POSITION must be VEC3";
        return false;
    }

    bool hasNrm = false;
    if (attrs.contains("NORMAL")) {
        if (!accessorPointer(j, attrs["NORMAL"].get<int>(), bin, binSize, &nrmPtr, &nrmCount, &nrmStride, &nrmType, &nrmN, error)) {
            return false;
        }
        hasNrm = nrmN == 3 && nrmCount >= posCount;
    }
    bool hasUv = false;
    if (attrs.contains("TEXCOORD_0")) {
        if (!accessorPointer(j, attrs["TEXCOORD_0"].get<int>(), bin, binSize, &uvPtr, &uvCount, &uvStride, &uvType, &uvN, error)) {
            return false;
        }
        hasUv = uvN == 2 && uvCount >= posCount;
    }

    const glm::mat3 normalMat = glm::mat3(glm::transpose(glm::inverse(world)));
    const std::uint32_t base = static_cast<std::uint32_t>(out.vertices.size());
    out.vertices.reserve(out.vertices.size() + posCount);
    for (std::size_t i = 0; i < posCount; ++i) {
        GltfVertex v;
        const glm::vec4 wp = world * glm::vec4(readVec3(posPtr, posStride, posType, i), 1.0f);
        v.pos = glm::vec3(wp);
        if (hasNrm) {
            v.normal = glm::normalize(normalMat * readVec3(nrmPtr, nrmStride, nrmType, i));
        }
        if (hasUv) {
            v.uv = readVec2(uvPtr, uvStride, uvType, i);
        }
        out.vertices.push_back(v);
    }

    if (prim.contains("indices")) {
        const std::uint8_t* idxPtr = nullptr;
        std::size_t idxCount = 0, idxStride = 0;
        int idxType = 0, idxN = 0;
        if (!accessorPointer(j, prim["indices"].get<int>(), bin, binSize, &idxPtr, &idxCount, &idxStride, &idxType, &idxN, error)) {
            return false;
        }
        out.indices.reserve(out.indices.size() + idxCount);
        for (std::size_t i = 0; i < idxCount; ++i) {
            out.indices.push_back(base + readIndex(idxPtr, idxStride, idxType, i));
        }
    } else {
        out.indices.reserve(out.indices.size() + posCount);
        for (std::size_t i = 0; i < posCount; ++i) {
            out.indices.push_back(base + static_cast<std::uint32_t>(i));
        }
    }
    return true;
}

bool walkNode(const nlohmann::json& j, int nodeIndex, const glm::mat4& parent, const std::uint8_t* bin, std::size_t binSize,
    GltfMesh& out, std::string* error) {
    const auto& nodes = j["nodes"];
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(nodes.size())) {
        if (error) *error = "node index out of range";
        return false;
    }
    const auto& node = nodes[nodeIndex];
    const glm::mat4 world = parent * nodeMatrix(node);
    if (node.contains("mesh")) {
        const int meshIndex = node["mesh"].get<int>();
        const auto& meshes = j["meshes"];
        if (meshIndex < 0 || meshIndex >= static_cast<int>(meshes.size())) {
            if (error) *error = "mesh index out of range";
            return false;
        }
        for (const auto& prim : meshes[meshIndex]["primitives"]) {
            if (!appendPrimitive(j, prim, bin, binSize, world, out, error)) {
                return false;
            }
        }
    }
    if (node.contains("children")) {
        for (const auto& child : node["children"]) {
            if (!walkNode(j, child.get<int>(), world, bin, binSize, out, error)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

bool loadGltfMeshFromGlbBytes(const std::uint8_t* data, std::size_t size, GltfMesh& out, std::string* error) {
    out = GltfMesh{};
    if (size < 12) {
        if (error) *error = "GLB too small";
        return false;
    }
    std::uint32_t magic = 0, version = 0, length = 0;
    std::memcpy(&magic, data, 4);
    std::memcpy(&version, data + 4, 4);
    std::memcpy(&length, data + 8, 4);
    if (magic != 0x46546C67u) { // "glTF"
        if (error) *error = "not a GLB (missing glTF magic)";
        return false;
    }
    if (version != 2 || length > size) {
        if (error) *error = "unsupported GLB version or truncated file";
        return false;
    }

    const nlohmann::json* jsonDoc = nullptr;
    nlohmann::json parsed;
    const std::uint8_t* bin = nullptr;
    std::size_t binSize = 0;

    std::size_t off = 12;
    while (off + 8 <= size) {
        std::uint32_t chunkLen = 0, chunkType = 0;
        std::memcpy(&chunkLen, data + off, 4);
        std::memcpy(&chunkType, data + off + 4, 4);
        off += 8;
        if (off + chunkLen > size) {
            if (error) *error = "GLB chunk overruns the file";
            return false;
        }
        if (chunkType == 0x4E4F534Au) { // JSON
            try {
                parsed = nlohmann::json::parse(data + off, data + off + chunkLen);
                jsonDoc = &parsed;
            } catch (const nlohmann::json::exception& e) {
                if (error) *error = std::string("GLB JSON: ") + e.what();
                return false;
            }
        } else if (chunkType == 0x004E4942u) { // BIN\0
            bin = data + off;
            binSize = chunkLen;
        }
        off += chunkLen;
        off = (off + 3u) & ~std::size_t{3};
    }
    if (!jsonDoc) {
        if (error) *error = "GLB missing JSON chunk";
        return false;
    }
    if (!bin) {
        if (error) *error = "GLB missing BIN chunk";
        return false;
    }

    const auto& j = *jsonDoc;
    if (!j.contains("nodes") || !j.contains("meshes")) {
        if (error) *error = "GLB has no meshes";
        return false;
    }

    std::vector<int> roots;
    if (j.contains("scenes") && j.contains("scene")) {
        const int sceneIndex = j["scene"].get<int>();
        const auto& scenes = j["scenes"];
        if (sceneIndex >= 0 && sceneIndex < static_cast<int>(scenes.size()) && scenes[sceneIndex].contains("nodes")) {
            for (const auto& n : scenes[sceneIndex]["nodes"]) {
                roots.push_back(n.get<int>());
            }
        }
    }
    if (roots.empty()) {
        roots.push_back(0);
    }

    try {
        for (int root : roots) {
            if (!walkNode(j, root, glm::mat4(1.0f), bin, binSize, out, error)) {
                return false;
            }
        }
    } catch (const nlohmann::json::exception& e) {
        if (error) *error = std::string("GLB scene: ") + e.what();
        return false;
    }
    if (out.vertices.empty() || out.indices.empty()) {
        if (error) *error = "GLB produced an empty mesh";
        return false;
    }
    return true;
}

bool loadGltfMesh(const std::filesystem::path& path, GltfMesh& out, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error) *error = "cannot open " + path.string();
        return false;
    }
    file.seekg(0, std::ios::end);
    const auto sz = static_cast<std::size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(sz);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(sz));
    if (!file) {
        if (error) *error = "failed reading " + path.string();
        return false;
    }
    return loadGltfMeshFromGlbBytes(bytes.data(), bytes.size(), out, error);
}

void fitGltfMeshToFootprint(GltfMesh& mesh, float footprintW, float footprintH, float yawDegrees) {
    if (mesh.vertices.empty()) return;
    glm::vec3 mn(std::numeric_limits<float>::max());
    glm::vec3 mx(std::numeric_limits<float>::lowest());
    for (const GltfVertex& v : mesh.vertices) {
        mn = glm::min(mn, v.pos);
        mx = glm::max(mx, v.pos);
    }
    const glm::vec3 size = mx - mn;
    const float fpW = footprintW > 1e-4f ? footprintW : 1.0f;
    const float fpH = footprintH > 1e-4f ? footprintH : 1.0f;
    const float sx = size.x > 1e-6f ? fpW / size.x : 1.0f;
    const float sz = size.z > 1e-6f ? fpH / size.z : 1.0f;
    const float scale = std::min(sx, sz);
    const glm::vec3 center((mn.x + mx.x) * 0.5f, mn.y, (mn.z + mx.z) * 0.5f);
    const glm::mat3 yaw = std::fabs(yawDegrees) > 1e-4f
        ? glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f)))
        : glm::mat3(1.0f);
    for (GltfVertex& v : mesh.vertices) {
        v.pos = yaw * ((v.pos - center) * scale);
        if (glm::length(v.normal) > 1e-6f) {
            v.normal = glm::normalize(yaw * v.normal);
        } else {
            v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }
}

} // namespace render_core
