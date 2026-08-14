// CPU GLB loader used by BuildingRenderer. Guards the binary container parse,
// a one-triangle scene, and fitGltfMeshToFootprint (uniform scale into a cell
// footprint, XZ-centered, Y min at 0).
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

#include "render_core/gltf_mesh.h"

namespace {

void putU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    std::uint8_t b[4];
    std::memcpy(b, &v, 4);
    out.insert(out.end(), b, b + 4);
}

void pad4(std::vector<std::uint8_t>& bytes, std::uint8_t pad) {
    while (bytes.size() % 4 != 0) bytes.push_back(pad);
}

std::vector<std::uint8_t> makeGlb(const std::string& json, const std::vector<std::uint8_t>& bin) {
    std::vector<std::uint8_t> jsonBytes(json.begin(), json.end());
    pad4(jsonBytes, static_cast<std::uint8_t>(' '));
    std::vector<std::uint8_t> binBytes = bin;
    pad4(binBytes, 0);

    const std::uint32_t jsonLen = static_cast<std::uint32_t>(jsonBytes.size());
    const std::uint32_t binLen = static_cast<std::uint32_t>(binBytes.size());
    const std::uint32_t length = 12 + 8 + jsonLen + 8 + binLen;

    std::vector<std::uint8_t> out;
    out.reserve(length);
    putU32(out, 0x46546C67u); // glTF
    putU32(out, 2);
    putU32(out, length);
    putU32(out, jsonLen);
    putU32(out, 0x4E4F534Au); // JSON
    out.insert(out.end(), jsonBytes.begin(), jsonBytes.end());
    putU32(out, binLen);
    putU32(out, 0x004E4942u); // BIN\0
    out.insert(out.end(), binBytes.begin(), binBytes.end());
    return out;
}

std::vector<std::uint8_t> triangleGlb() {
    // Y-up triangle on y=1: (0,1,0) (4,1,0) (0,1,8). XZ AABB 4 x 8.
    std::vector<std::uint8_t> bin(36 + 6, 0);
    const float pos[9] = {0.f, 1.f, 0.f, 4.f, 1.f, 0.f, 0.f, 1.f, 8.f};
    std::memcpy(bin.data(), pos, sizeof(pos));
    const std::uint16_t idx[3] = {0, 1, 2};
    std::memcpy(bin.data() + 36, idx, sizeof(idx));

    const std::string json =
        R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],)"
        R"("nodes":[{"mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],)"
        R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,1,0],"max":[4,1,8]},)"
        R"({"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],)"
        R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],)"
        R"("buffers":[{"byteLength":42}]})";
    return makeGlb(json, bin);
}

} // namespace

TEST(GltfMesh, RejectsTruncatedAndBadMagic) {
    render_core::GltfMesh mesh;
    std::string error;
    EXPECT_FALSE(render_core::loadGltfMeshFromGlbBytes(nullptr, 0, mesh, &error));
    const std::uint8_t junk[16] = {1, 2, 3, 4};
    EXPECT_FALSE(render_core::loadGltfMeshFromGlbBytes(junk, sizeof(junk), mesh, &error));
}

TEST(GltfMesh, LoadsTriangleGlb) {
    const std::vector<std::uint8_t> bytes = triangleGlb();
    render_core::GltfMesh mesh;
    std::string error;
    ASSERT_TRUE(render_core::loadGltfMeshFromGlbBytes(bytes.data(), bytes.size(), mesh, &error)) << error;
    ASSERT_EQ(mesh.vertices.size(), 3u);
    ASSERT_EQ(mesh.indices.size(), 3u);
    EXPECT_FLOAT_EQ(mesh.vertices[0].pos.x, 0.f);
    EXPECT_FLOAT_EQ(mesh.vertices[0].pos.y, 1.f);
    EXPECT_FLOAT_EQ(mesh.vertices[1].pos.x, 4.f);
    EXPECT_FLOAT_EQ(mesh.vertices[2].pos.z, 8.f);
    EXPECT_EQ(mesh.indices[0], 0u);
    EXPECT_EQ(mesh.indices[2], 2u);
}

TEST(GltfMesh, FitToFootprintCentersAndSitsOnGround) {
    const std::vector<std::uint8_t> bytes = triangleGlb();
    render_core::GltfMesh mesh;
    std::string error;
    ASSERT_TRUE(render_core::loadGltfMeshFromGlbBytes(bytes.data(), bytes.size(), mesh, &error)) << error;

    render_core::fitGltfMeshToFootprint(mesh, 2.f, 2.f);

    glm::vec3 mn(1e9f), mx(-1e9f);
    for (const auto& v : mesh.vertices) {
        mn = glm::min(mn, v.pos);
        mx = glm::max(mx, v.pos);
    }
    EXPECT_NEAR(mn.y, 0.f, 1e-5f);
    // Uniform scale = min(2/4, 2/8) = 0.25 → X spans 1, Z spans 2, centered on 0.
    EXPECT_NEAR((mn.x + mx.x) * 0.5f, 0.f, 1e-5f);
    EXPECT_NEAR((mn.z + mx.z) * 0.5f, 0.f, 1e-5f);
    EXPECT_NEAR(mx.x - mn.x, 1.f, 1e-5f);
    EXPECT_NEAR(mx.z - mn.z, 2.f, 1e-5f);
    EXPECT_LE(mx.x - mn.x, 2.f + 1e-5f);
    EXPECT_LE(mx.z - mn.z, 2.f + 1e-5f);
}

TEST(GltfMesh, FitToFootprintYaw180NegatesXZ) {
    const std::vector<std::uint8_t> bytes = triangleGlb();
    render_core::GltfMesh mesh;
    std::string error;
    ASSERT_TRUE(render_core::loadGltfMeshFromGlbBytes(bytes.data(), bytes.size(), mesh, &error)) << error;

    render_core::fitGltfMeshToFootprint(mesh, 2.f, 2.f, 180.f);

    // Unrotated fit: (-0.5,0,-1), (0.5,0,-1), (-0.5,0,1). Yaw 180: x'=-x, z'=-z.
    EXPECT_NEAR(mesh.vertices[0].pos.x, 0.5f, 1e-5f);
    EXPECT_NEAR(mesh.vertices[0].pos.z, 1.0f, 1e-5f);
    EXPECT_NEAR(mesh.vertices[1].pos.x, -0.5f, 1e-5f);
    EXPECT_NEAR(mesh.vertices[1].pos.z, 1.0f, 1e-5f);
    EXPECT_NEAR(mesh.vertices[2].pos.x, 0.5f, 1e-5f);
    EXPECT_NEAR(mesh.vertices[2].pos.z, -1.0f, 1e-5f);
    EXPECT_NEAR(mesh.vertices[0].pos.y, 0.f, 1e-5f);
}
