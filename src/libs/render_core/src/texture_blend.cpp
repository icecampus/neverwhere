#include "render_core/texture_blend.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include <landscape_core/landscape_logic.h>
#include <topology_core/diamond_isometry.h>

#include "atlas_tile_types.h"

namespace render_core {

namespace {

// Votes for one node: a handful of (uuid, count) pairs plus the tile order of
// each candidate's first vote (the tie-break clock).
struct NodeVotes {
    static constexpr int kMaxCandidates = 4;
    std::array<std::string, kMaxCandidates> uuids{};
    std::array<int, kMaxCandidates> counts{};
    std::array<std::size_t, kMaxCandidates> firstTile{};
    int size = 0;

    void add(const std::string& uuid, std::size_t tileOrder) {
        for (int i = 0; i < size; ++i) {
            if (uuids[i] == uuid) {
                ++counts[static_cast<std::size_t>(i)];
                return;
            }
        }
        if (size < kMaxCandidates) {
            uuids[static_cast<std::size_t>(size)] = uuid;
            counts[static_cast<std::size_t>(size)] = 1;
            firstTile[static_cast<std::size_t>(size)] = tileOrder;
            ++size;
        }
    }

    const std::string& winner() const {
        int best = 0;
        for (int i = 1; i < size; ++i) {
            const auto iIdx = static_cast<std::size_t>(i);
            const auto bIdx = static_cast<std::size_t>(best);
            if (counts[iIdx] > counts[bIdx] ||
                (counts[iIdx] == counts[bIdx] && firstTile[iIdx] > firstTile[bIdx])) {
                best = i;
            }
        }
        return uuids[static_cast<std::size_t>(best)];
    }
};

} // namespace

std::vector<TextureBlendCell> buildTextureBlendCells(const std::vector<LandscapeTile>& tiles) {
    // Pass 1: every tile votes with its assetUuid for its on corner nodes.
    std::unordered_map<std::uint64_t, NodeVotes> votes;
    votes.reserve(tiles.size() * 2);
    for (std::size_t ti = 0; ti < tiles.size(); ++ti) {
        const LandscapeTile& tile = tiles[ti];
        const auto mask = landscape_core::tileTypeToNodeMask(tileTypeFromAtlasIndex(tile.tileIndex));
        const auto corners = topology_core::DiamondIsometry::cellCornerNodes(tile.cell);
        for (int i = 0; i < 4; ++i) {
            if (mask[static_cast<std::size_t>(i)]) {
                votes[nodeKey(corners[static_cast<std::size_t>(i)])].add(tile.assetUuid, ti);
            }
        }
    }

    // Pass 2: per cell, collect the distinct winning textures of its on
    // corners (first-seen in corner order) and the one-hot corner weights.
    // Off corners take the first candidate's slot so the interpolation stays
    // inside the blend — their region is faded out by the fill weight anyway.
    std::vector<TextureBlendCell> out;
    out.reserve(tiles.size());
    for (const LandscapeTile& tile : tiles) {
        const auto mask = landscape_core::tileTypeToNodeMask(tileTypeFromAtlasIndex(tile.tileIndex));
        const auto corners = topology_core::DiamondIsometry::cellCornerNodes(tile.cell);

        TextureBlendCell cellBlend;
        cellBlend.cell = tile.cell;
        const std::string* cornerTexture[4] = {nullptr, nullptr, nullptr, nullptr};
        for (int i = 0; i < 4; ++i) {
            if (!mask[static_cast<std::size_t>(i)]) {
                continue;
            }
            const auto it = votes.find(nodeKey(corners[static_cast<std::size_t>(i)]));
            if (it == votes.end()) {
                continue; // unreachable: this tile just voted for the node
            }
            cornerTexture[i] = &it->second.winner();
            bool seen = false;
            for (int k = 0; k < cellBlend.candidateCount; ++k) {
                if (cellBlend.candidateUuids[static_cast<std::size_t>(k)] == *cornerTexture[i]) {
                    seen = true;
                    break;
                }
            }
            if (!seen && cellBlend.candidateCount < 4) {
                cellBlend.candidateUuids[static_cast<std::size_t>(cellBlend.candidateCount)] = *cornerTexture[i];
                ++cellBlend.candidateCount;
            }
        }
        if (cellBlend.candidateCount == 0) {
            continue;
        }
        for (int i = 0; i < 4; ++i) {
            int slot = 0;
            if (cornerTexture[i] != nullptr) {
                for (int k = 0; k < cellBlend.candidateCount; ++k) {
                    if (cellBlend.candidateUuids[static_cast<std::size_t>(k)] == *cornerTexture[i]) {
                        slot = k;
                        break;
                    }
                }
            }
            cellBlend.cornerWeights[static_cast<std::size_t>(i)][static_cast<std::size_t>(slot)] = 1.0f;
            cellBlend.cornerFill[static_cast<std::size_t>(i)] =
                mask[static_cast<std::size_t>(i)] ? 1.0f : 0.0f;
        }
        out.push_back(std::move(cellBlend));
    }
    return out;
}

} // namespace render_core
