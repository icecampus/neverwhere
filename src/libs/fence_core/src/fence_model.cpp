#include "pch.h"

#include "fence_core/fence_model.h"

namespace fence_core {

namespace {

bool isUnitAxis(glm::ivec2 d) {
    return ((d.x == 0) != (d.y == 0)) && (std::abs(d.x) + std::abs(d.y) == 1);
}

} // namespace

void FenceModel::reset(int width, int height) {
    m_bounded = true;
    m_width = width;
    m_height = height;
    m_pieces.clear();
    m_cellPiece.clear();
    m_nextPieceId = 0;
    m_nextFenceId = 0;
    ++m_version;
}

void FenceModel::reset() {
    m_bounded = false;
    m_width = 0;
    m_height = 0;
    m_pieces.clear();
    m_cellPiece.clear();
    m_nextPieceId = 0;
    m_nextFenceId = 0;
    ++m_version;
}

void FenceModel::clear() {
    m_pieces.clear();
    m_cellPiece.clear();
    ++m_version;
}

bool FenceModel::inBounds(glm::ivec2 cell) const {
    if (!m_bounded) {
        return true;
    }
    return cell.x >= 0 && cell.y >= 0 && cell.x < m_width && cell.y < m_height;
}

std::int64_t FenceModel::cellKey(glm::ivec2 cell) {
    return (static_cast<std::int64_t>(cell.y) << 32) ^
        static_cast<std::int64_t>(static_cast<std::uint32_t>(cell.x));
}

int FenceModel::indexOfPiece(int pieceId) const {
    for (std::size_t i = 0; i < m_pieces.size(); ++i) {
        if (m_pieces[i].id == pieceId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const FencePiece* FenceModel::pieceById(int id) const {
    const int idx = indexOfPiece(id);
    return idx >= 0 ? &m_pieces[idx] : nullptr;
}

const FencePiece* FenceModel::pieceAt(glm::ivec2 cell) const {
    if (!inBounds(cell)) {
        return nullptr;
    }
    const auto it = m_cellPiece.find(cellKey(cell));
    return it != m_cellPiece.end() ? pieceById(it->second) : nullptr;
}

void FenceModel::loadPieces(const std::vector<FencePieceData>& pieces) {
    m_pieces.clear();
    m_pieces.reserve(pieces.size());
    m_nextPieceId = 0;
    m_nextFenceId = 0;

    // First pass: instantiate pieces, remembering where the posts sit.
    std::unordered_map<std::int64_t, int> postAt; // cell key -> piece id
    for (const FencePieceData& d : pieces) {
        FencePiece piece;
        piece.id = m_nextPieceId++;
        piece.kind = d.kind;
        piece.cell = d.cell;
        piece.axis = (d.kind == FencePieceKind::Section) ? d.axis : glm::ivec2{0, 0};
        piece.length = (d.kind == FencePieceKind::Section) ? std::max(1, d.length) : 1;
        if (piece.kind == FencePieceKind::Post) {
            postAt[cellKey(piece.cell)] = piece.id;
        }
        m_pieces.push_back(piece);
    }
    // Second pass: wire section endpoints geometrically (a section covers
    // [cell, cell+axis*(length-1)] and links the posts one cell before and
    // one cell past its run).
    for (FencePiece& piece : m_pieces) {
        if (piece.kind != FencePieceKind::Section) {
            continue;
        }
        const auto lookup = [&postAt](glm::ivec2 cell) -> int {
            const auto it = postAt.find(cellKey(cell));
            return it != postAt.end() ? it->second : -1;
        };
        piece.postA = lookup(piece.cell - piece.axis);
        piece.postB = lookup(piece.cell + piece.axis * piece.length);
    }

    rebuildCells();
    rebuildFences();
    ++m_version;
}

FenceModel::StrokePlan FenceModel::planStroke(glm::ivec2 start, glm::ivec2 dir, int cells) const {
    StrokePlan plan;
    plan.dir = dir;
    if (!isUnitAxis(dir) || cells < 1 || !inBounds(start)) {
        return plan;
    }
    const FencePiece* startPiece = pieceAt(start);
    if (startPiece && startPiece->kind != FencePieceKind::Post) {
        return plan; // a stroke can only start on empty ground or on a post
    }
    const bool leadPost = (startPiece == nullptr);
    plan.extension = !leadPost;
    const glm::ivec2 runStart = leadPost ? start : start + dir;

    // Free run: consecutive empty in-bounds cells, capped by the request.
    int n = 0;
    while (n < cells && inBounds(runStart + dir * n) && pieceAt(runStart + dir * n) == nullptr) {
        ++n;
    }
    // Blocker right past the run: a post there becomes the tail connection.
    const glm::ivec2 blockerCell = runStart + dir * n;
    if (n < cells && inBounds(blockerCell)) {
        const FencePiece* blocker = pieceAt(blockerCell);
        if (blocker && blocker->kind == FencePieceKind::Post) {
            plan.connectPostId = blocker->id;
        }
    }
    const bool tailPost = plan.connectPostId >= 0;

    // Minimum run: P S P for a new free-ended fence, P S->B for a new
    // connecting one, S P / S->B for an extension.
    const int minCells = leadPost ? (tailPost ? 2 : 3) : (tailPost ? 1 : 2);
    if (n < minCells) {
        return plan;
    }

    // Segmentation: k sections with lengths in {1,2}, posts between (and at
    // the free end). Post-cell count as a function of k:
    //   new fence, free end:   P (S P)*k        -> k+1
    //   new fence, tail post:  P (S P)*(k-1) S  -> k
    //   extension, free end:   (S P)*k          -> k
    //   extension, tail post:  (S P)*(k-1) S    -> k-1
    // Section cells sum to n - P; the fewest sections (longest mean) win.
    const auto postsFor = [&](int k) {
        return leadPost ? (tailPost ? k : k + 1) : (tailPost ? k - 1 : k);
    };
    int k = -1;
    int sectionSum = 0;
    for (int cand = 1; cand <= n; ++cand) {
        const int sum = n - postsFor(cand);
        if (sum >= cand && sum <= 2 * cand) {
            k = cand;
            sectionSum = sum;
            break;
        }
    }
    if (k < 1) {
        return plan;
    }
    // Prefer 2-cell sections, the longer ones first.
    const int extra = sectionSum - k; // sections that grow to 2 cells

    glm::ivec2 cur = runStart;
    if (leadPost) {
        plan.pieces.push_back({FencePieceKind::Post, start, glm::ivec2{0, 0}, 1});
        cur += dir; // the section run starts past the lead post
    }
    for (int i = 0; i < k; ++i) {
        const int len = 1 + (i < extra ? 1 : 0);
        plan.pieces.push_back({FencePieceKind::Section, cur, dir, len});
        cur += dir * len;
        const bool lastSection = (i == k - 1);
        if (!lastSection || !tailPost) {
            plan.pieces.push_back({FencePieceKind::Post, cur, glm::ivec2{0, 0}, 1});
            cur += dir;
        }
    }
    plan.valid = true;
    return plan;
}

bool FenceModel::applyStroke(glm::ivec2 start, glm::ivec2 dir, int cells) {
    const StrokePlan plan = planStroke(start, dir, cells);
    if (!plan.valid) {
        return false;
    }

    int prevPostId = -1;
    int pendingSection = -1; // index in m_pieces of a section waiting for postB
    if (plan.extension) {
        prevPostId = pieceAt(start)->id;
    }
    for (const StrokePiece& sp : plan.pieces) {
        FencePiece piece;
        piece.id = m_nextPieceId++;
        piece.kind = sp.kind;
        piece.cell = sp.cell;
        piece.length = sp.length;
        piece.axis = (sp.kind == FencePieceKind::Section) ? sp.axis : glm::ivec2{0, 0};
        if (sp.kind == FencePieceKind::Section) {
            piece.postA = prevPostId;
        }
        m_pieces.push_back(piece);
        if (sp.kind == FencePieceKind::Post) {
            if (pendingSection >= 0) {
                m_pieces[pendingSection].postB = piece.id;
                pendingSection = -1;
            }
            prevPostId = piece.id;
        } else {
            pendingSection = static_cast<int>(m_pieces.size()) - 1;
        }
    }
    if (pendingSection >= 0) {
        // Tail-connecting section: its far post is the existing blocker post.
        m_pieces[pendingSection].postB = plan.connectPostId;
    }

    rebuildCells();
    rebuildFences();
    ++m_version;
    return true;
}

bool FenceModel::erasePostAt(glm::ivec2 cell) {
    const FencePiece* piece = pieceAt(cell);
    if (!piece || piece->kind != FencePieceKind::Post) {
        return false;
    }
    const int postId = piece->id;
    m_pieces.erase(
        std::remove_if(
            m_pieces.begin(),
            m_pieces.end(),
            [&](const FencePiece& p) {
                return p.id == postId ||
                    (p.kind == FencePieceKind::Section && (p.postA == postId || p.postB == postId));
            }),
        m_pieces.end());
    rebuildCells();
    rebuildFences();
    ++m_version;
    return true;
}

int FenceModel::fenceAt(glm::ivec2 cell) const {
    const FencePiece* piece = pieceAt(cell);
    return piece ? piece->fenceId : -1;
}

bool FenceModel::fenceExists(int fenceId) const {
    for (const FencePiece& piece : m_pieces) {
        if (piece.fenceId == fenceId) {
            return true;
        }
    }
    return false;
}

int FenceModel::fenceCount() const {
    int count = 0;
    std::vector<int> seen;
    for (const FencePiece& piece : m_pieces) {
        if (std::find(seen.begin(), seen.end(), piece.fenceId) == seen.end()) {
            seen.push_back(piece.fenceId);
            ++count;
        }
    }
    return count;
}

bool FenceModel::canTranslate(int fenceId, glm::ivec2 delta) const {
    if (!fenceExists(fenceId)) {
        return false;
    }
    for (const FencePiece& piece : m_pieces) {
        if (piece.fenceId != fenceId) {
            continue;
        }
        for (int i = 0; i < piece.length; ++i) {
            const glm::ivec2 target = piece.cell + piece.axis * i + delta;
            if (!inBounds(target)) {
                return false;
            }
            const auto it = m_cellPiece.find(cellKey(target));
            if (it != m_cellPiece.end()) {
                const FencePiece* other = pieceById(it->second);
                if (other && other->fenceId != fenceId) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool FenceModel::translateFence(int fenceId, glm::ivec2 delta) {
    if (delta == glm::ivec2{0, 0}) {
        return true;
    }
    if (!canTranslate(fenceId, delta)) {
        return false;
    }
    for (FencePiece& piece : m_pieces) {
        if (piece.fenceId == fenceId) {
            piece.cell += delta;
        }
    }
    rebuildCells();
    rebuildFences();
    ++m_version;
    return true;
}

bool FenceModel::eraseFence(int fenceId) {
    if (!fenceExists(fenceId)) {
        return false;
    }
    m_pieces.erase(
        std::remove_if(
            m_pieces.begin(),
            m_pieces.end(),
            [&](const FencePiece& p) { return p.fenceId == fenceId; }),
        m_pieces.end());
    rebuildCells();
    ++m_version;
    return true;
}

void FenceModel::rebuildCells() {
    m_cellPiece.clear();
    for (const FencePiece& piece : m_pieces) {
        for (int i = 0; i < piece.length; ++i) {
            const glm::ivec2 cell = piece.cell + piece.axis * i;
            if (inBounds(cell)) {
                m_cellPiece[cellKey(cell)] = piece.id;
            }
        }
    }
}

void FenceModel::rebuildFences() {
    // BFS over the piece graph: posts reach their incident sections (and vice
    // versa) through the stored endpoint links. Ids are kept stable across
    // rebuilds: a component keeps the smallest fenceId its pieces already
    // carried; only genuinely new components (fresh strokes, split-off parts)
    // draw a fresh id.
    std::vector<char> visited(m_pieces.size(), 0);
    std::vector<char> reusedIds(m_pieces.size() + m_nextFenceId + 1, 0);

    for (std::size_t seed = 0; seed < m_pieces.size(); ++seed) {
        if (visited[seed]) {
            continue;
        }
        // Collect the component.
        std::vector<int> component;
        std::vector<int> queue;
        queue.push_back(static_cast<int>(seed));
        visited[seed] = 1;
        while (!queue.empty()) {
            const int cur = queue.back();
            queue.pop_back();
            component.push_back(cur);
            const FencePiece& piece = m_pieces[cur];
            if (piece.kind == FencePieceKind::Section) {
                for (const int postId : {piece.postA, piece.postB}) {
                    const int idx = indexOfPiece(postId);
                    if (idx >= 0 && !visited[idx]) {
                        visited[idx] = 1;
                        queue.push_back(idx);
                    }
                }
            } else {
                for (std::size_t i = 0; i < m_pieces.size(); ++i) {
                    const FencePiece& other = m_pieces[i];
                    if (other.kind == FencePieceKind::Section &&
                        (other.postA == piece.id || other.postB == piece.id) && !visited[i]) {
                        visited[i] = 1;
                        queue.push_back(static_cast<int>(i));
                    }
                }
            }
        }

        int candidate = -1;
        for (const int idx : component) {
            const int oldId = m_pieces[idx].fenceId;
            if (oldId >= 0 && (candidate < 0 || oldId < candidate)) {
                candidate = oldId;
            }
        }
        int fenceId = -1;
        if (candidate >= 0 && candidate < static_cast<int>(reusedIds.size()) && !reusedIds[candidate]) {
            fenceId = candidate;
            reusedIds[candidate] = 1;
        } else {
            fenceId = m_nextFenceId++;
        }
        for (const int idx : component) {
            m_pieces[idx].fenceId = fenceId;
        }
    }
}

} // namespace fence_core
