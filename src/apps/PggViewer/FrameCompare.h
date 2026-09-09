#pragma once

// Frame comparison and silhouette metrics (agent_tooling_plan F3) — pure CPU
// half of the RPC `render{compare:"prev"}` and `reference` commands, split
// from main.cpp so the smoke test can drive it on synthetic buffers without
// any GL. Everything here works on top-down RGBA8 buffers (the orientation
// capturePng normalizes every backend's readback to).

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Per-channel difference above which a pixel counts as changed (8/255).
inline constexpr int kFrameChangeThreshold = 8;
// Number of horizontal bands of the silhouette width profile.
inline constexpr int kSilhouetteRows = 10;

struct FrameCompareResult {
    bool available = false;   // false = the comparison could not run (reason explains)
    std::string reason;       // why !available ("size mismatch", ...)
    double changedPct = 0.0;  // share of changed pixels, percent 0..100
    // Bbox of the changed pixels, x1/y1 EXCLUSIVE; all zeros when nothing changed.
    int changeX0 = 0, changeY0 = 0, changeX1 = 0, changeY1 = 0;
    // w*h*4 RGBA: b dimmed to 30% with changed pixels masked bright magenta —
    // written as the diff PNG.
    std::vector<std::uint8_t> diffPixels;
};

// Compares two same-size frames pixel by pixel (alpha ignored). Buffers must
// hold exactly w*h*4 bytes each, otherwise available:false ("size mismatch").
FrameCompareResult compareFrames(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b,
                                 int w, int h);

struct SilhouetteMetrics {
    bool empty = true;  // no foreground pixels found (all fields stay zero)
    // Bbox of the foreground (pixels differing from the background by more
    // than the tolerance on any channel), in frame fractions 0..1;
    // x1/y1 are exclusive ((max+1)/size), so an empty silhouette is all zeros.
    float bboxX0 = 0.0f, bboxY0 = 0.0f, bboxX1 = 0.0f, bboxY1 = 0.0f;
    float wOverH = 0.0f;  // silhouette bbox width / height in pixels
    // Per horizontal band (kSilhouetteRows equal bands, top to bottom): the
    // mean silhouette width of the band's rows, in frame-width fractions.
    std::array<float, kSilhouetteRows> rows{};
};

// Foreground = pixels whose RGB differs from bgR/bgG/bgB by more than
// `tolerance` on any channel (alpha ignored).
SilhouetteMetrics silhouetteMetrics(const std::vector<std::uint8_t>& pixels, int w, int h,
                                    std::uint8_t bgR, std::uint8_t bgG, std::uint8_t bgB, int tolerance = 8);

// Background guess for a reference photo/render: the majority RGB of the four
// corner pixels (ties resolve to the first corner in TL, TR, BL, BR order).
std::array<std::uint8_t, 3> estimateBackground(const std::vector<std::uint8_t>& pixels, int w, int h);

struct SideBySideImage {
    bool ok = false;
    int width = 0, height = 0;
    int modelW = 0;  // width of the model half; the divider starts at modelW
    std::vector<std::uint8_t> pixels;  // width*height*4 RGBA
};

// Model frame on the left, the reference (bilinear-resized to the model
// height, aspect kept) on the right, a 4px divider between them. Both inputs
// must be w*h*4 RGBA with positive sizes, otherwise ok:false.
SideBySideImage composeSideBySide(const std::vector<std::uint8_t>& modelPix, int mw, int mh,
                                  const std::vector<std::uint8_t>& refPix, int rw, int rh);

// stb_image-based PNG/JPEG/... reader (always RGBA8). false when the file is
// missing or undecodable.
bool loadImageRgba(const std::string& path, std::vector<std::uint8_t>& out, int& w, int& h);
