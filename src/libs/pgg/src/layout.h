#pragma once

// Deterministic auto-layout for the node projection (stage E8, spec §10) plus
// the layout-hint format. Hints are optional trailing comments
// (`rock = set_position(...)  # @pos 340 120`), never part of the semantics:
// the IDE appends them on manual drags, generators write without them (auto
// layout applies), and a removed or dirty hint breaks nothing — it is ignored
// silently. Machine edits of the text keep them (they are plain comments, and
// the canonical formatter preserves comments).
//
// Hint format (pinned): `@pos X Y` — two signed integers — anywhere inside
// the trailing comment of the binding's line; the first match wins; a dirty
// or truncated hint (`@pos abc`, `@pos 1`, `@position 1 2`) is no hint at
// all. Coordinates are the node's center in scope space.
//
// Auto-layout is Sugiyama-lite and deterministic (the reproducibility culture
// of N1): layers by longest path from the sources (zone state loops excluded
// from layering), barycenter ordering with a few sweeps, ties broken by the
// source span, coordinates on a grid of (layer, row). Zones lay out
// recursively, the frame wrapping its children with padding. A node with a
// valid hint keeps its hinted position instead of the layout one (author
// intent wins; local overlaps are acceptable).

#include <cstdint>
#include <string>

#include "graph.h"

namespace pgg {

// Parses `@pos X Y` anywhere in a comment's text. On success returns true and
// sets x/y; anything malformed yields false with no diagnostics.
bool parsePosHint(const std::string& comment, int& x, int& y);

// Rewrites the layout hint of the statement on `line` (1-based) in `text`:
// when the line already carries a valid hint, only its coordinates are
// replaced (the rest of the comment stays byte-identical); otherwise
// `  # @pos X Y` is appended at the end of the line — after any existing
// trailing comment, which the anywhere-in-comment parse covers. Returns the
// updated text. Applying it twice with the same coordinates is idempotent,
// and re-parsing the result yields an AST equal to the original (comments do
// not touch the AST).
std::string applyPosHint(const std::string& text, int32_t line, int x, int y);

struct LayoutParams {
    float nodeW = 200.0f;       // uniform node width
    float headerH = 42.0f;      // node header strip (title + op)
    float pinRowH = 18.0f;      // height per pin row
    float layerGapX = 80.0f;    // horizontal gap between layers
    float rowGapY = 26.0f;      // vertical gap between rows
    float zonePadX = 42.0f;     // frame side padding
    float zonePadTop = 64.0f;   // frame top padding (the header's title strip)
    float zonePadBottom = 26.0f;
};

// Lays out one scope in place: assigns node centers (x/y), layers and zone
// frame rectangles in scope coordinates. Deterministic for a given graph.
void layoutScope(GraphScope& scope, const LayoutParams& params = {});

// Lays out the top-level scope and every instance body scope.
void layoutProject(GraphProject& project, const LayoutParams& params = {});

}  // namespace pgg
