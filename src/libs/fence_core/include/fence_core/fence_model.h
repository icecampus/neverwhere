#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

// Fence layout model shared by the FencePathPlayground and the editor
// (FenceLandscape layer authoring/render): a cell grid where every cell is
// empty or covered by one fence piece — a post (exactly 1 cell) or a section
// (1-2 adjacent cells along one axis, connecting exactly two posts). A fence
// is a connected component of the piece graph: posts are vertices, sections
// are edges, and pieces keep explicit links, so two fences running parallel in
// adjacent rows stay independent (connectivity by graph, not by cell
// adjacency). Pure C++ (no sokol/Qt), same contract style as LandBrush.
// Graduated from the FencePathPlayground into this lib for the editor port.
namespace fence_core {

enum class FencePieceKind : std::uint8_t { Post, Section };

struct FencePiece {
    int id = -1;                            // persistent piece id (survives edits)
    FencePieceKind kind = FencePieceKind::Post;
    glm::ivec2 cell{0, 0};                  // post: its cell; section: first covered cell along the axis
    glm::ivec2 axis{0, 0};                  // section: signed unit axis of the stroke ((±1,0)/(0,±1)); post: (0,0)
    int length = 1;                         // section: 1..2 covered cells; post: 1
    int fenceId = -1;                       // connected component id, recomputed on edit
    int postA = -1;                         // section endpoints (persistent piece ids)
    int postB = -1;
};

// Flat persisted form of one piece (editor map storage): ids, endpoint links
// and fence components are derived state and are rebuilt by loadPieces().
struct FencePieceData {
    FencePieceKind kind = FencePieceKind::Post;
    glm::ivec2 cell{0, 0};
    glm::ivec2 axis{0, 0}; // section: signed unit axis; post: (0,0)
    int length = 1;        // section: 1..2 covered cells; post: 1
};

class FenceModel {
public:
    // One planned piece of a stroke preview (absolute cells).
    struct StrokePiece {
        FencePieceKind kind = FencePieceKind::Post;
        glm::ivec2 cell{0, 0};
        glm::ivec2 axis{0, 0}; // section: signed stroke axis; post: (0,0)
        int length = 1;        // section: 1..2
    };
    struct StrokePlan {
        bool valid = false;
        bool extension = false;         // start cell holds an existing post (anchor)
        glm::ivec2 dir{0, 0};
        std::vector<StrokePiece> pieces;
        int connectPostId = -1;         // tail merge target (existing post piece id)
    };

    void reset(int width, int height);  // bounded grid (playground: strokes stop at the edge)
    void reset();                       // unbounded grid (editor maps are unbounded)
    void clear();

    int width() const { return m_width; }
    int height() const { return m_height; }

    const FencePiece* pieceAt(glm::ivec2 cell) const; // any piece covering the cell
    const FencePiece* pieceById(int id) const;
    const std::vector<FencePiece>& pieces() const { return m_pieces; }

    // Rebuilds the model from the flat persisted piece list. Deterministic:
    // piece ids are assigned 0..N-1 in list order, section endpoint links are
    // derived geometrically (posts at cell-axis and cell+axis*length), and the
    // fence components are renumbered by BFS — the same list always yields the
    // same piece/fence ids, so the editing tool and the frame builder agree.
    void loadPieces(const std::vector<FencePieceData>& pieces);

    // dir must be a unit axis ((±1,0)/(0,±1)); cells = cells to cover starting
    // at `start` (an existing anchor post is not covered — the run begins one
    // cell further). The plan stops before the first occupied cell; when that
    // cell is a post the last section connects to it (fence merge).
    StrokePlan planStroke(glm::ivec2 start, glm::ivec2 dir, int cells) const;
    bool applyStroke(glm::ivec2 start, glm::ivec2 dir, int cells);

    // Deletes the post and its incident sections -> the graph may split into
    // independent fences. A section always keeps both endpoint posts, so no
    // dangling section survives.
    bool erasePostAt(glm::ivec2 cell);

    int fenceAt(glm::ivec2 cell) const; // fenceId of the piece covering the cell, -1 when empty
    bool fenceExists(int fenceId) const;
    int fenceCount() const;

    bool canTranslate(int fenceId, glm::ivec2 delta) const;
    bool translateFence(int fenceId, glm::ivec2 delta);
    bool eraseFence(int fenceId);

    // Monotonic content version (LandBrush convention): bumped by every edit.
    std::uint64_t version() const { return m_version; }

private:
    bool inBounds(glm::ivec2 cell) const;
    static std::int64_t cellKey(glm::ivec2 cell);
    int indexOfPiece(int pieceId) const;
    void rebuildCells();
    void rebuildFences();

    bool m_bounded = true;
    int m_width = 0;
    int m_height = 0;
    std::uint64_t m_version = 0;
    std::vector<FencePiece> m_pieces;
    std::unordered_map<std::int64_t, int> m_cellPiece; // cell key -> persistent piece id
    int m_nextPieceId = 0;
    int m_nextFenceId = 0;
};

} // namespace fence_core
