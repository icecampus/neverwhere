#include "pch.h"

#include "DemoDiscovery.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace shadertoy {

namespace {

struct PassFile {
    const char* fileName;
    PassKind kind;
};

const PassFile kPassFiles[] = {
    {"Image.glsl", PassKind::Image},
    {"BufferA.glsl", PassKind::BufferA},
    {"BufferB.glsl", PassKind::BufferB},
    {"BufferC.glsl", PassKind::BufferC},
    {"BufferD.glsl", PassKind::BufferD},
};

const char* kBufferNames[] = {"BufferA", "BufferB", "BufferC", "BufferD"};

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool passKindFromName(const std::string& name, PassKind& out) {
    if (name == "Image") {
        out = PassKind::Image;
        return true;
    }
    for (int i = 0; i < 4; ++i) {
        if (name == kBufferNames[i]) {
            out = static_cast<PassKind>(static_cast<int>(PassKind::BufferA) + i);
            return true;
        }
    }
    return false;
}

// First meaningful text block of the demo README: skip the title, link and
// bullet lines; take the first plain paragraph (truncated).
std::string readDescription(const std::filesystem::path& readmePath) {
    std::ifstream in(readmePath);
    if (!in) {
        return {};
    }
    std::string block;
    std::string line;
    while (std::getline(in, line)) {
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos) {
            if (!block.empty()) {
                break; // end of the first text block
            }
            continue;
        }
        const char c = line[first];
        if (c == '#' || c == '-' || c == '|') {
            continue;
        }
        if (!block.empty()) {
            block += ' ';
        }
        block += line.substr(first);
        if (block.size() > 400) {
            block.resize(400);
            block += "...";
            break;
        }
    }
    return block;
}

// textures/iChannelN.(png|jpg) -> channel index, or -1 when not matching.
int textureChannelFromName(const std::string& stem) {
    const std::string prefix = "iChannel";
    if (stem.size() != prefix.size() + 1 || stem.compare(0, prefix.size(), prefix) != 0) {
        return -1;
    }
    const char c = stem.back();
    return (c >= '0' && c <= '3') ? c - '0' : -1;
}

void applyTextureInputs(Demo& demo) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path texDir = demo.dir / "textures";
    if (!fs::is_directory(texDir, ec)) {
        return;
    }
    for (const auto& entry : fs::directory_iterator(texDir, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const int channel = textureChannelFromName(entry.path().stem().string());
        if (channel < 0) {
            continue;
        }
        for (DemoPass& pass : demo.passes) {
            ChannelInput& in = pass.inputs[channel];
            if (in.kind == ChannelKind::None) {
                in.kind = ChannelKind::Texture; // manifest buffer bindings win
            }
        }
    }
}

void applyManifest(Demo& demo) {
    const std::filesystem::path manifestPath = demo.dir / "shadertoy.json";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(manifestPath, ec)) {
        return;
    }
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(readTextFile(manifestPath));
    } catch (const std::exception& e) {
        spdlog::warn("shadertoy.json parse failed in '{}': {}", demo.name, e.what());
        return;
    }
    if (!json.contains("passes") || !json["passes"].is_object()) {
        return;
    }
    for (DemoPass& pass : demo.passes) {
        const char* passName = (pass.kind == PassKind::Image)
            ? "Image"
            : kBufferNames[static_cast<int>(pass.kind) - static_cast<int>(PassKind::BufferA)];
        const auto it = json["passes"].find(passName);
        if (it == json["passes"].end() || !it->is_object()) {
            continue;
        }
        for (const auto& [channelName, value] : it->items()) {
            int channel = -1;
            if (channelName.size() == 8 && channelName.compare(0, 8, "iChannel") == 0) {
                const char c = channelName[7];
                channel = (c >= '0' && c <= '3') ? c - '0' : -1;
            }
            if (channel < 0 || !value.is_string()) {
                continue;
            }
            PassKind bufferKind;
            if (!passKindFromName(value.get<std::string>(), bufferKind) ||
                bufferKind == PassKind::Image) {
                spdlog::warn("{}: {} -> '{}' is not a buffer pass", demo.name, channelName,
                    value.get<std::string>());
                continue;
            }
            pass.inputs[channel].kind = ChannelKind::Buffer;
            pass.inputs[channel].buffer = bufferKind;
        }
    }
}

} // namespace

std::filesystem::path resolveShadertoyRoot(const std::string& cliDir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!cliDir.empty()) {
        fs::path dir = fs::weakly_canonical(fs::path(cliDir), ec);
        return fs::is_directory(dir, ec) ? dir : fs::path{};
    }
    fs::path dir = fs::weakly_canonical(fs::current_path(ec), ec);
    for (int i = 0; i < 16 && !dir.empty(); ++i) {
        const fs::path candidate = dir / "docs" / "reference" / "shadertoy";
        if (fs::is_directory(candidate, ec)) {
            return candidate;
        }
        const fs::path parent = dir.parent_path();
        if (parent == dir) {
            break;
        }
        dir = parent;
    }
    return {};
}

std::vector<Demo> scanDemos(const std::filesystem::path& root) {
    namespace fs = std::filesystem;
    std::vector<Demo> demos;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        return demos;
    }
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (!entry.is_directory(ec)) {
            continue;
        }
        Demo demo;
        demo.name = entry.path().filename().string();
        demo.dir = entry.path();
        demo.description = readDescription(demo.dir / "README.md");
        demo.commonSource = readTextFile(demo.dir / "Common.glsl");

        for (const PassFile& pf : kPassFiles) {
            const fs::path passPath = demo.dir / pf.fileName;
            if (!fs::is_regular_file(passPath, ec)) {
                continue;
            }
            DemoPass pass;
            pass.kind = pf.kind;
            pass.sourcePath = passPath;
            demo.passes.push_back(pass);
            demo.hasImage = demo.hasImage || pf.kind == PassKind::Image;
        }
        // Folders without any pass are kept too: they are manual stubs
        // (наполняются вручную) — the UI lists them greyed out.
        if (demo.passes.empty()) {
            demo.hasImage = false;
            demos.push_back(std::move(demo));
            continue;
        }
        // Buffers first (A..D), Image last — shadertoy execution order.
        std::sort(demo.passes.begin(), demo.passes.end(), [](const DemoPass& a, const DemoPass& b) {
            auto rank = [](PassKind k) { return k == PassKind::Image ? 1 : 0; };
            if (rank(a.kind) != rank(b.kind)) {
                return rank(a.kind) < rank(b.kind);
            }
            return static_cast<int>(a.kind) < static_cast<int>(b.kind);
        });

        applyManifest(demo);
        applyTextureInputs(demo);
        demos.push_back(std::move(demo));
    }
    std::sort(demos.begin(), demos.end(), [](const Demo& a, const Demo& b) {
        return a.name < b.name;
    });
    return demos;
}

} // namespace shadertoy
