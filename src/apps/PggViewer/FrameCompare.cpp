#include "pch.h"

#include "FrameCompare.h"

#include <cmath>
#include <cstring>

#include <spdlog/spdlog.h>

// The viewer binary has no other stb_image implementation TU (main.cpp owns
// only stb_image_write's), so the implementation macro lives here.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace {

// Deterministic bilinear resize (stb_image_resize2 is not used: a hand-rolled
// kernel keeps the output byte-exact across stb versions and platforms).
void resizeBilinear(const std::uint8_t* src, int sw, int sh, std::uint8_t* dst, int dw, int dh) {
    for (int y = 0; y < dh; ++y) {
        const float fy = (static_cast<float>(y) + 0.5f) * static_cast<float>(sh) / static_cast<float>(dh) - 0.5f;
        const int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, sh - 1);
        const int y1 = std::clamp(y0 + 1, 0, sh - 1);
        const float ty = std::clamp(fy - static_cast<float>(y0), 0.0f, 1.0f);
        for (int x = 0; x < dw; ++x) {
            const float fx =
                (static_cast<float>(x) + 0.5f) * static_cast<float>(sw) / static_cast<float>(dw) - 0.5f;
            const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, sw - 1);
            const int x1 = std::clamp(x0 + 1, 0, sw - 1);
            const float tx = std::clamp(fx - static_cast<float>(x0), 0.0f, 1.0f);
            const std::uint8_t* p00 = src + (static_cast<size_t>(y0) * sw + x0) * 4;
            const std::uint8_t* p10 = src + (static_cast<size_t>(y0) * sw + x1) * 4;
            const std::uint8_t* p01 = src + (static_cast<size_t>(y1) * sw + x0) * 4;
            const std::uint8_t* p11 = src + (static_cast<size_t>(y1) * sw + x1) * 4;
            std::uint8_t* out = dst + (static_cast<size_t>(y) * dw + x) * 4;
            for (int c = 0; c < 4; ++c) {
                const float top = p00[c] + (p10[c] - p00[c]) * tx;
                const float bot = p01[c] + (p11[c] - p01[c]) * tx;
                out[c] = static_cast<std::uint8_t>(std::lround(top + (bot - top) * ty));
            }
        }
    }
}

bool isBackground(const std::uint8_t* px, std::uint8_t bgR, std::uint8_t bgG, std::uint8_t bgB, int tolerance) {
    return std::abs(static_cast<int>(px[0]) - bgR) <= tolerance &&
           std::abs(static_cast<int>(px[1]) - bgG) <= tolerance &&
           std::abs(static_cast<int>(px[2]) - bgB) <= tolerance;
}

}  // namespace

FrameCompareResult compareFrames(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b,
                                 int w, int h) {
    FrameCompareResult res;
    const size_t need = w > 0 && h > 0 ? static_cast<size_t>(w) * h * 4 : 0;
    if (need == 0 || a.size() != need || b.size() != need) {
        res.reason = "size mismatch";
        return res;
    }
    res.available = true;
    res.diffPixels.resize(need);
    size_t changed = 0;
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = (static_cast<size_t>(y) * w + x) * 4;
            const int dR = std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
            const int dG = std::abs(static_cast<int>(a[i + 1]) - static_cast<int>(b[i + 1]));
            const int dB = std::abs(static_cast<int>(a[i + 2]) - static_cast<int>(b[i + 2]));
            std::uint8_t* out = res.diffPixels.data() + i;
            if (std::max(dR, std::max(dG, dB)) > kFrameChangeThreshold) {
                ++changed;
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x);
                y1 = std::max(y1, y);
                out[0] = 255;  // magenta mask
                out[1] = 0;
                out[2] = 255;
                out[3] = 255;
            } else {
                // Dimmed new frame as the backdrop of the mask.
                out[0] = static_cast<std::uint8_t>((b[i] * 3) / 10);
                out[1] = static_cast<std::uint8_t>((b[i + 1] * 3) / 10);
                out[2] = static_cast<std::uint8_t>((b[i + 2] * 3) / 10);
                out[3] = 255;
            }
        }
    }
    res.changedPct = 100.0 * static_cast<double>(changed) / static_cast<double>(need / 4);
    if (changed > 0) {
        res.changeX0 = x0;
        res.changeY0 = y0;
        res.changeX1 = x1 + 1;  // exclusive
        res.changeY1 = y1 + 1;
    }
    return res;
}

SilhouetteMetrics silhouetteMetrics(const std::vector<std::uint8_t>& pixels, int w, int h,
                                    std::uint8_t bgR, std::uint8_t bgG, std::uint8_t bgB, int tolerance) {
    SilhouetteMetrics m;
    if (w <= 0 || h <= 0 || pixels.size() < static_cast<size_t>(w) * h * 4) return m;
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    std::array<size_t, kSilhouetteRows> bandFg{};
    for (int y = 0; y < h; ++y) {
        size_t rowFg = 0;
        for (int x = 0; x < w; ++x) {
            const std::uint8_t* px = pixels.data() + (static_cast<size_t>(y) * w + x) * 4;
            if (isBackground(px, bgR, bgG, bgB, tolerance)) continue;
            ++rowFg;
            x0 = std::min(x0, x);
            y0 = std::min(y0, y);
            x1 = std::max(x1, x);
            y1 = std::max(y1, y);
        }
        if (rowFg > 0) bandFg[std::min(kSilhouetteRows - 1, y * kSilhouetteRows / h)] += rowFg;
    }
    if (x1 < 0) return m;  // empty silhouette
    m.empty = false;
    m.bboxX0 = static_cast<float>(x0) / static_cast<float>(w);
    m.bboxY0 = static_cast<float>(y0) / static_cast<float>(h);
    m.bboxX1 = static_cast<float>(x1 + 1) / static_cast<float>(w);
    m.bboxY1 = static_cast<float>(y1 + 1) / static_cast<float>(h);
    m.wOverH = static_cast<float>(x1 + 1 - x0) / static_cast<float>(std::max(1, y1 + 1 - y0));
    for (int band = 0; band < kSilhouetteRows; ++band) {
        const int bandY0 = band * h / kSilhouetteRows;
        const int bandY1 = (band + 1) * h / kSilhouetteRows;
        const size_t bandPixels = static_cast<size_t>(std::max(1, bandY1 - bandY0)) * w;
        m.rows[band] = static_cast<float>(bandFg[band]) / static_cast<float>(bandPixels);
    }
    return m;
}

std::array<std::uint8_t, 3> estimateBackground(const std::vector<std::uint8_t>& pixels, int w, int h) {
    std::array<std::uint8_t, 3> bg{0, 0, 0};
    if (w <= 0 || h <= 0 || pixels.size() < static_cast<size_t>(w) * h * 4) return bg;
    const int xs[4] = {0, w - 1, 0, w - 1};  // TL, TR, BL, BR
    const int ys[4] = {0, 0, h - 1, h - 1};
    int bestIdx = 0, bestCount = 0;
    for (int i = 0; i < 4; ++i) {
        const std::uint8_t* pi = pixels.data() + (static_cast<size_t>(ys[i]) * w + xs[i]) * 4;
        int count = 0;
        for (int j = 0; j < 4; ++j) {
            const std::uint8_t* pj = pixels.data() + (static_cast<size_t>(ys[j]) * w + xs[j]) * 4;
            count += pi[0] == pj[0] && pi[1] == pj[1] && pi[2] == pj[2] ? 1 : 0;
        }
        if (count > bestCount) {
            bestCount = count;
            bestIdx = i;
        }
    }
    const std::uint8_t* p = pixels.data() + (static_cast<size_t>(ys[bestIdx]) * w + xs[bestIdx]) * 4;
    bg = {p[0], p[1], p[2]};
    return bg;
}

SideBySideImage composeSideBySide(const std::vector<std::uint8_t>& modelPix, int mw, int mh,
                                  const std::vector<std::uint8_t>& refPix, int rw, int rh) {
    SideBySideImage out;
    if (mw <= 0 || mh <= 0 || rw <= 0 || rh <= 0 ||
        modelPix.size() < static_cast<size_t>(mw) * mh * 4 ||
        refPix.size() < static_cast<size_t>(rw) * rh * 4)
        return out;
    constexpr int kDivider = 4;
    const int newRw = std::max(1, static_cast<int>(std::lround(static_cast<double>(rw) * mh / rh)));
    out.width = mw + kDivider + newRw;
    out.height = mh;
    out.modelW = mw;
    out.pixels.assign(static_cast<size_t>(out.width) * out.height * 4, 255);
    // Model half.
    for (int y = 0; y < mh; ++y)
        std::memcpy(out.pixels.data() + static_cast<size_t>(y) * out.width * 4,
                    modelPix.data() + static_cast<size_t>(y) * mw * 4, static_cast<size_t>(mw) * 4);
    // Divider: neutral mid-grey, visibly darker than both usual backgrounds.
    for (int y = 0; y < out.height; ++y)
        for (int x = mw; x < mw + kDivider; ++x) {
            std::uint8_t* px = out.pixels.data() + (static_cast<size_t>(y) * out.width + x) * 4;
            px[0] = px[1] = px[2] = 90;
        }
    // Reference half, resized to the model height.
    std::vector<std::uint8_t> refResized(static_cast<size_t>(newRw) * mh * 4);
    resizeBilinear(refPix.data(), rw, rh, refResized.data(), newRw, mh);
    for (int y = 0; y < mh; ++y)
        std::memcpy(out.pixels.data() + (static_cast<size_t>(y) * out.width + mw + kDivider) * 4,
                    refResized.data() + static_cast<size_t>(y) * newRw * 4,
                    static_cast<size_t>(newRw) * 4);
    out.ok = true;
    return out;
}

bool loadImageRgba(const std::string& path, std::vector<std::uint8_t>& out, int& w, int& h) {
    out.clear();
    w = h = 0;
    int channels = 0;
    stbi_uc* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!data || w <= 0 || h <= 0) {
        spdlog::error("loadImageRgba: stbi_load failed for {} ({})", path, stbi_failure_reason());
        return false;
    }
    out.assign(data, data + static_cast<size_t>(w) * h * 4);
    stbi_image_free(data);
    return true;
}
