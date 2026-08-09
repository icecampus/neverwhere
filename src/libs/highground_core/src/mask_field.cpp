#include "pch.h"

#include "highground_core/mask_field.h"

#include <algorithm>
#include <cmath>

namespace mask {

ReliefMap reliefMapFromImage(const std::uint8_t* gray, int w, int h, int stride) {
    ReliefMap out;
    if (gray == nullptr || w <= 0 || h <= 0 || stride < 1) {
        return out;
    }
    out.w = w;
    out.h = h;
    out.gray.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
    for (size_t i = 0; i < out.gray.size(); ++i) {
        out.gray[i] = static_cast<float>(gray[i * static_cast<size_t>(stride)]) * (1.0f / 255.0f);
    }
    // Low-pass to the dune scale: the voxel grid cannot hold the fine
    // ripples (they turn into gravel-like lump noise), and those already
    // live in the material's normal map. Repeated 2x2 box halving with
    // REPEAT wrap keeps the raster tileable; reliefAt's bilinear smooths
    // the rest.
    while (out.w > 32 || out.h > 16) {
        const int nw = std::max(1, out.w / 2);
        const int nh = std::max(1, out.h / 2);
        std::vector<float> next(static_cast<size_t>(nw) * static_cast<size_t>(nh));
        for (int y = 0; y < nh; ++y) {
            const int y0 = (2 * y) % out.h;
            const int y1 = (2 * y + 1) % out.h;
            for (int x = 0; x < nw; ++x) {
                const int x0 = (2 * x) % out.w;
                const int x1 = (2 * x + 1) % out.w;
                next[static_cast<size_t>(y) * nw + x] = 0.25f *
                    (out.gray[static_cast<size_t>(y0) * out.w + x0] +
                     out.gray[static_cast<size_t>(y0) * out.w + x1] +
                     out.gray[static_cast<size_t>(y1) * out.w + x0] +
                     out.gray[static_cast<size_t>(y1) * out.w + x1]);
            }
        }
        out.w = nw;
        out.h = nh;
        out.gray = std::move(next);
    }
    return out;
}

MaskField::MaskField(
    const MaskFieldParams& params,
    const std::uint8_t* nodes,
    int nodesX,
    int nodesY)
    : m_params(params) {
    const bool hasNodes = nodes != nullptr && nodesX > 1 && nodesY > 1;
    m_nodesX = hasNodes ? nodesX : 0;
    m_nodesZ = hasNodes ? nodesY : 0;
    m_nodes.assign(static_cast<size_t>(m_nodesX) * m_nodesZ, 0);
    if (hasNodes) {
        std::copy(
            nodes,
            nodes + static_cast<size_t>(m_nodesX) * m_nodesZ,
            m_nodes.begin());
    }

    // The region spans (nodesX-1) x (nodesY-1) map cells; the field is
    // rectangular in XZ. sinkFraction of the height stands below the node
    // grid plane (y = 0): the slab spans y = -height*sink..+height*(1-sink),
    // so the field's Y range is [-height*sink - padY, height*(1-sink) + padY]
    // (the span itself does not change with sink, only the shift does). The
    // mask itself keeps F > 0 outside the region (fill = 0 there), so the XZ
    // pad is just slack for the optional blur.
    const float cell = params.cellSize;
    const float pad = params.padding;
    const float padY = 2.0f * cell;
    const float regionX = static_cast<float>(m_nodesX - 1);
    const float regionZ = static_cast<float>(m_nodesZ - 1);
    m_nx = static_cast<int>(std::ceil((regionX + 2.0f * pad) / cell));
    m_nz = static_cast<int>(std::ceil((regionZ + 2.0f * pad) / cell));
    m_ny = static_cast<int>(std::ceil((params.height + 2.0f * padY) / cell));
    m_origin = glm::vec3(-pad, -params.height * params.sinkFraction - padY, -pad);

    buildDistanceField();
}

float MaskField::nodeFill(int nx, int nz) const {
    const bool on = (nx >= 0 && nx < m_nodesX && nz >= 0 && nz < m_nodesZ) &&
        m_nodes[static_cast<size_t>(nz) * m_nodesX + nx] != 0;
    return on ? 1.0f : 0.0f;
}

float MaskField::fillAt(float x, float z) const {
    const int ix = static_cast<int>(std::floor(x));
    const int iz = static_cast<int>(std::floor(z));
    const float fx = x - static_cast<float>(ix);
    const float fz = z - static_cast<float>(iz);
    const float f00 = nodeFill(ix, iz);
    const float f10 = nodeFill(ix + 1, iz);
    const float f01 = nodeFill(ix, iz + 1);
    const float f11 = nodeFill(ix + 1, iz + 1);
    return f00 * (1.0f - fx) * (1.0f - fz) + f10 * fx * (1.0f - fz) +
        f01 * (1.0f - fx) * fz + f11 * fx * fz;
}

void MaskField::buildDistanceField() {
    m_dist.clear();
    m_distW = 0;
    m_distH = 0;
    if (m_nodesX < 2 || m_nodesZ < 2) {
        return;
    }
    // Raster signed distance to the bilinear fill = 0.5 contour, R texels
    // per cell: seeded with the fractional zero crossings of fill - 0.5 on
    // texel edges (the fill is linear along an axis-aligned segment, so the
    // crossings are exact), then propagated with a two-pass chamfer
    // (1, sqrt2) — the same trick as the contact-AO field in scene_stitch.
    // The sign stays per texel: inside the mask (fill >= 0.5) is negative.
    const int R = kDistTexelsPerCell;
    const float h = 1.0f / static_cast<float>(R); // texel size in cell units
    const int w = (m_nodesX - 1) * R + 1;
    const int hgt = (m_nodesZ - 1) * R + 1;
    const float kInf = 1e9f;
    std::vector<float> v(static_cast<size_t>(w) * hgt);
    std::vector<float> d(static_cast<size_t>(w) * hgt, kInf);
    for (int j = 0; j < hgt; ++j) {
        for (int i = 0; i < w; ++i) {
            v[static_cast<size_t>(j) * w + i] =
                fillAt(static_cast<float>(i) * h, static_cast<float>(j) * h) - 0.5f;
        }
    }
    // Seed: texels sitting on the contour, and texel edges whose endpoints'
    // values straddle zero — both endpoints get their fractional distance to
    // the crossing (in texel units).
    bool anySeed = false;
    auto seedEdge = [&v, &d, &anySeed, w](int i0, int j0, int i1, int j1) {
        const float v0 = v[static_cast<size_t>(j0) * w + i0];
        const float v1 = v[static_cast<size_t>(j1) * w + i1];
        if (v0 == v1 || ((v0 < 0.0f) == (v1 < 0.0f) && v0 != 0.0f && v1 != 0.0f)) {
            return;
        }
        const float t = v0 / (v0 - v1); // crossing at t of the way from 0 to 1
        float& d0 = d[static_cast<size_t>(j0) * w + i0];
        float& d1 = d[static_cast<size_t>(j1) * w + i1];
        d0 = std::min(d0, t);
        d1 = std::min(d1, 1.0f - t);
        anySeed = true;
    };
    for (int j = 0; j < hgt; ++j) {
        for (int i = 0; i < w; ++i) {
            if (v[static_cast<size_t>(j) * w + i] == 0.0f) {
                d[static_cast<size_t>(j) * w + i] = 0.0f;
                anySeed = true;
            }
            if (i + 1 < w) {
                seedEdge(i, j, i + 1, j);
            }
            if (j + 1 < hgt) {
                seedEdge(i, j, i, j + 1);
            }
        }
    }
    if (!anySeed) {
        // No contour at all (nothing painted): keep the field a constant
        // positive.
        m_dist.assign(static_cast<size_t>(w) * hgt, m_params.spreadDistance + 0.5f);
        m_distW = w;
        m_distH = hgt;
        return;
    }
    // Two-pass chamfer (1, sqrt2) over the magnitude, in texel units.
    const float kDiag = 1.41421356237f;
    for (int j = 0; j < hgt; ++j) {
        for (int i = 0; i < w; ++i) {
            float& cur = d[static_cast<size_t>(j) * w + i];
            if (i > 0) {
                cur = std::min(cur, d[static_cast<size_t>(j) * w + i - 1] + 1.0f);
            }
            if (j > 0) {
                cur = std::min(cur, d[static_cast<size_t>(j - 1) * w + i] + 1.0f);
                if (i > 0) {
                    cur = std::min(cur, d[static_cast<size_t>(j - 1) * w + i - 1] + kDiag);
                }
                if (i + 1 < w) {
                    cur = std::min(cur, d[static_cast<size_t>(j - 1) * w + i + 1] + kDiag);
                }
            }
        }
    }
    for (int j = hgt - 1; j >= 0; --j) {
        for (int i = w - 1; i >= 0; --i) {
            float& cur = d[static_cast<size_t>(j) * w + i];
            if (i + 1 < w) {
                cur = std::min(cur, d[static_cast<size_t>(j) * w + i + 1] + 1.0f);
            }
            if (j + 1 < hgt) {
                cur = std::min(cur, d[static_cast<size_t>(j + 1) * w + i] + 1.0f);
                if (i + 1 < w) {
                    cur = std::min(cur, d[static_cast<size_t>(j + 1) * w + i + 1] + kDiag);
                }
                if (i > 0) {
                    cur = std::min(cur, d[static_cast<size_t>(j + 1) * w + i - 1] + kDiag);
                }
            }
        }
    }
    m_dist.resize(static_cast<size_t>(w) * hgt);
    for (int j = 0; j < hgt; ++j) {
        for (int i = 0; i < w; ++i) {
            const size_t idx = static_cast<size_t>(j) * w + i;
            m_dist[idx] = (v[idx] >= 0.0f ? -1.0f : 1.0f) * d[idx] * h;
        }
    }
    m_distW = w;
    m_distH = hgt;
}

float MaskField::distanceAt(float x, float z) const {
    if (m_dist.empty()) {
        // No grid at all: outside, a constant beyond the spread.
        return m_params.spreadDistance + 0.5f;
    }
    const float R = static_cast<float>(kDistTexelsPerCell);
    const float gx = std::clamp(x * R, 0.0f, static_cast<float>(m_distW - 1));
    const float gz = std::clamp(z * R, 0.0f, static_cast<float>(m_distH - 1));
    const int ix = static_cast<int>(gx);
    const int iz = static_cast<int>(gz);
    const int ix1 = std::min(ix + 1, m_distW - 1);
    const int iz1 = std::min(iz + 1, m_distH - 1);
    const float fx = gx - static_cast<float>(ix);
    const float fz = gz - static_cast<float>(iz);
    const float d00 = m_dist[static_cast<size_t>(iz) * m_distW + ix];
    const float d10 = m_dist[static_cast<size_t>(iz) * m_distW + ix1];
    const float d01 = m_dist[static_cast<size_t>(iz1) * m_distW + ix];
    const float d11 = m_dist[static_cast<size_t>(iz1) * m_distW + ix1];
    return d00 * (1.0f - fx) * (1.0f - fz) + d10 * fx * (1.0f - fz) +
        d01 * (1.0f - fx) * fz + d11 * fx * fz;
}

float MaskField::reliefAt(float x, float z) const {
    const ReliefMap* rm = m_params.reliefMap;
    if (rm == nullptr || rm->w <= 0 || rm->h <= 0 || rm->gray.empty()) {
        return 0.5f; // neutral: no displacement
    }
    // REPEAT tiling, bilinear; V tiles twice as fast — the same 2:1 aspect
    // compensation as the material shader's matuv.
    const float u = x * m_params.reliefTiling;
    const float v = z * m_params.reliefTiling * 2.0f;
    const float gx = (u - std::floor(u)) * static_cast<float>(rm->w);
    const float gz = (v - std::floor(v)) * static_cast<float>(rm->h);
    const int ix = static_cast<int>(gx) % rm->w;
    const int iz = static_cast<int>(gz) % rm->h;
    const int ix1 = (ix + 1) % rm->w;
    const int iz1 = (iz + 1) % rm->h;
    const float fx = gx - std::floor(gx);
    const float fz = gz - std::floor(gz);
    const float g00 = rm->gray[static_cast<size_t>(iz) * rm->w + ix];
    const float g10 = rm->gray[static_cast<size_t>(iz) * rm->w + ix1];
    const float g01 = rm->gray[static_cast<size_t>(iz1) * rm->w + ix];
    const float g11 = rm->gray[static_cast<size_t>(iz1) * rm->w + ix1];
    return g00 * (1.0f - fx) * (1.0f - fz) + g10 * fx * (1.0f - fz) +
        g01 * (1.0f - fx) * fz + g11 * fx * fz;
}

float MaskField::eval(const glm::vec3& p) const {
    const float s = distanceAt(p.x, p.z);
    const float S = m_params.spreadDistance;
    const float topH = m_params.height * (1.0f - m_params.sinkFraction);
    const float botH = m_params.height * m_params.sinkFraction;
    // Top elevation: the core silhouette (the geometry spread 0 would draw)
    // keeps the full height; the spread band grows no full-height geometry
    // — it is a skirt whose height ramps linearly down with the distance
    // from the core contour. sinkFraction of the height stands below the
    // node grid plane (y = 0): the plate spans y = -botH..+topH and the
    // skirt foot reaches y = -botH.
    float top = topH;
    if (s > 0.0f) {
        top = S > 0.0f ?
            m_params.height * (std::max(1.0f - s / S, 0.0f) - m_params.sinkFraction) :
            -botH;
    }
    // Negative under the top surface above y = -botH within s <= S. At S = 0
    // this reduces exactly to a slab with a vertical wall on the fill = 0.5
    // contour.
    const float qTop = p.y - top;
    const float qBottom = -p.y - botH;
    const float qSide = s - S;
    float F = std::max({qTop, qBottom, qSide});
    // Micro relief (displacement map): shifts the iso surface by a centered,
    // tiling height sample — the dunes become real geometry with a
    // silhouette. The raster is low-passed at load (see reliefMapFromImage):
    // the voxel grid cannot hold the fine ripples (they live in the normal
    // map), only the dune-scale undulation. The relief ramps in over
    // reliefFade from the core contour, so walls and skirt keep a clean
    // silhouette. O(1); at reliefDepth = 0 or without a map the field is
    // bit-for-bit the smooth one.
    if (m_params.reliefDepth > 0.0f && m_params.reliefMap != nullptr) {
        float w = 1.0f;
        if (m_params.reliefFade > 0.0f) {
            const float t = std::clamp(-s / m_params.reliefFade, 0.0f, 1.0f);
            w = t * t * (3.0f - 2.0f * t);
        }
        F -= m_params.reliefDepth * (reliefAt(p.x, p.z) - 0.5f) * 2.0f * w;
    }
    return F;
}

void MaskField::sample(std::vector<float>& outValues) const {
    const float cell = m_params.cellSize;
    const int px = m_nx + 1;
    const int py = m_ny + 1;
    const int pz = m_nz + 1;
    outValues.resize(static_cast<size_t>(px) * py * pz);
    const glm::vec3 origin = m_origin;
    for (int iy = 0; iy < py; ++iy) {
        const float y = origin.y + iy * cell;
        for (int iz = 0; iz < pz; ++iz) {
            const float z = origin.z + iz * cell;
            float* row = &outValues[(static_cast<size_t>(iy) * pz + iz) * px];
            for (int ix = 0; ix < px; ++ix) {
                row[ix] = eval(glm::vec3(origin.x + ix * cell, y, z));
            }
        }
    }

    // Optional 3-tap blur over the sampled field: rounds the top lip (the
    // same anti-aliasing blur the other fields use, here as a "soften" knob).
    auto sampleAt = [&outValues, px, pz](int x, int y, int z) -> float& {
        return outValues[(static_cast<size_t>(y) * pz + z) * px + x];
    };
    std::vector<float> tmp(outValues.size());
    for (int pass = 0; pass < m_params.blurPasses; ++pass) {
        for (int axis = 0; axis < 3; ++axis) {
            const int dims[3] = {px, py, pz};
            for (int y = 0; y < py; ++y) {
                for (int z = 0; z < pz; ++z) {
                    for (int x = 0; x < px; ++x) {
                        int c[3] = {x, y, z};
                        float sum = 0.0f;
                        for (int k = -1; k <= 1; ++k) {
                            int q[3] = {c[0], c[1], c[2]};
                            q[axis] = std::clamp(q[axis] + k, 0, dims[axis] - 1);
                            sum += sampleAt(q[0], q[1], q[2]);
                        }
                        tmp[(static_cast<size_t>(y) * pz + z) * px + x] = sum / 3.0f;
                    }
                }
            }
            outValues = tmp;
        }
    }
}

cliff::ScalarFieldView MaskField::view() const {
    cliff::ScalarFieldView v;
    v.origin = m_origin;
    v.cellSize = m_params.cellSize;
    v.nx = m_nx;
    v.ny = m_ny;
    v.nz = m_nz;
    v.eval = [this](const glm::vec3& p) { return eval(p); };
    return v;
}

} // namespace mask
