// Demo discovery: a shadertoy demo is a folder under docs/reference/shadertoy
// (see docs/reference/shadertoy/README.md). Conventions:
//   - passes:   Image.glsl, BufferA..D.glsl, Common.glsl (injected everywhere);
//   - textures: textures/iChannelN.(png|jpg) — the filename binds the channel;
//   - manifest: shadertoy.json (optional) — buffer inputs per pass:
//       {"passes": {"BufferB": {"iChannel0": "BufferB"},
//                   "Image": {"iChannel0": "BufferA", "iChannel2": "BufferB"}}}
//     (texture channels come from filenames, no manifest needed for them).
#pragma once

namespace shadertoy {

enum class PassKind { Image, BufferA, BufferB, BufferC, BufferD };

enum class ChannelKind { None, Buffer, Texture };

struct ChannelInput {
    ChannelKind kind = ChannelKind::None;
    PassKind buffer = PassKind::BufferA; // when kind == Buffer
    // texture sampler (kind == Texture): shadertoy defaults are mipmap+repeat
    bool mipmap = true;
    bool repeat = true;
};

struct DemoPass {
    PassKind kind = PassKind::Image;
    std::filesystem::path sourcePath;
    ChannelInput inputs[4];
};

struct Demo {
    std::string name;               // folder name
    std::filesystem::path dir;
    std::string description;        // first text block of README.md
    bool hasImage = false;          // false = stub folder (listed greyed out)
    std::string commonSource;       // Common.glsl content (may be empty)
    std::vector<DemoPass> passes;   // buffers A..D first, Image last
};

// Walks up from the cwd looking for docs/reference/shadertoy (cliDir overrides
// when non-empty). Returns an empty path when not found.
std::filesystem::path resolveShadertoyRoot(const std::string& cliDir);

// Scans root for demo folders (sorted by name). Stub folders (no Image.glsl)
// are included with hasImage=false.
std::vector<Demo> scanDemos(const std::filesystem::path& root);

} // namespace shadertoy
