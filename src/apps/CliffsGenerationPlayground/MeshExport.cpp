#include "MeshExport.h"

#include <cstdio>
#include <filesystem>

#include <spdlog/spdlog.h>

namespace render_playground {

bool exportModelToObj(const RockFractureModel& model, const std::string& path) {
    if (model.meshVertices.empty() || model.meshIndices.empty()) {
        spdlog::error("exportModelToObj: empty mesh");
        return false;
    }

    std::error_code ec;
    const std::filesystem::path outPath(path);
    if (outPath.has_parent_path()) {
        std::filesystem::create_directories(outPath.parent_path(), ec);
    }

    FILE* file = std::fopen(path.c_str(), "w");
    if (file == nullptr) {
        spdlog::error("exportModelToObj: failed to open {}", path);
        return false;
    }

    std::fprintf(file, "# CliffsGenerationPlayground export\n");
    std::fprintf(file, "# vertices=%d triangles=%d\n", model.vertexCount, model.triangleCount);

    for (const Vec3& v : model.meshVertices) {
        std::fprintf(file, "v %.6f %.6f %.6f\n", v.x, v.y, v.z);
    }

    const bool hasNormals = model.meshNormals.size() == model.meshVertices.size();
    if (hasNormals) {
        for (const Vec3& n : model.meshNormals) {
            std::fprintf(file, "vn %.6f %.6f %.6f\n", n.x, n.y, n.z);
        }
    }

    for (size_t i = 0; i + 2 < model.meshIndices.size(); i += 3) {
        const std::uint32_t i0 = model.meshIndices[i] + 1;
        const std::uint32_t i1 = model.meshIndices[i + 1] + 1;
        const std::uint32_t i2 = model.meshIndices[i + 2] + 1;
        if (hasNormals) {
            std::fprintf(file, "f %u//%u %u//%u %u//%u\n", i0, i0, i1, i1, i2, i2);
        } else {
            std::fprintf(file, "f %u %u %u\n", i0, i1, i2);
        }
    }

    std::fclose(file);
    spdlog::info("exportModelToObj: wrote {} ({} tris)", path, model.triangleCount);
    return true;
}

} // namespace render_playground
