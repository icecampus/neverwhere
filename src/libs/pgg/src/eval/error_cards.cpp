#include "../../pch.h"

#include "error_cards.h"

namespace pgg {
namespace {

// One row per engine-emitted code (grep `"E[0-9]{3}"` / `"W[0-9]{3}"` over
// src/libs/pgg/src to audit). Text mirrors the actual diagnostics and the
// §11.2 taxonomy; examples are minimal and self-contained.
const std::vector<ErrorCard>& cards() {
    static const std::vector<ErrorCard> kCards = {
        // --- E1xx: syntax ---------------------------------------------------
        {"E100",
         "malformed literal or type name",
         "A literal or type name has a shape the grammar allows but the core "
         "forbids (e.g. a vector literal with 1 or 5+ components, an unknown "
         "type name in a declaration).",
         {"a parenthesized scalar `(1)` was meant as a vector — vectors are vec2..vec4 only",
          "a vector literal with more than 4 components",
          "a misspelled type name in a param/def declaration"},
         "v = (1, 2, 3, 4, 5)",
         "v = (1, 2, 3)  # vec2..vec4 only"},
        {"E101",
         "syntax error",
         "The parser could not read the file at all; the line points at the "
         "first token that stopped the grammar.",
         {"an unclosed brace or paren above the reported line",
          "a malformed statement (missing '=', stray token)",
          "a comment without '#'"},
         "a = box(size = (1, 1, 1)\nout = set_position(a)",
         "a = box(size = (1, 1, 1))  # close the paren\nout = set_position(a)"},
        {"E102",
         "name redefined",
         "SSA: every name is defined exactly once per scope (spec §6.2). The "
         "same rule covers duplicate output declarations.",
         {"assigning to an already defined name instead of introducing a fresh one",
          "two `output` lines for the same name",
          "a zone body binding that shadows a visible name"},
         "a = box(size = (1, 1, 1))\na = smooth(a, iterations = 1)",
         "a = box(size = (1, 1, 1))\na_smooth = smooth(a, iterations = 1)"},
        {"E103",
         "name is not defined",
         "An identifier is read that no binding, param or import defines above "
         "this point (define-before-use, spec §6.2).",
         {"a typo in the name",
          "reading a name above its definition",
          "a missing import for a qualified symbol"},
         "rock = set_position(bse, offset = @N)",
         "base = ico_sphere(subdiv = 1, radius = 1.0)  # define before use\nrock = set_position(base, offset = @N)"},
        {"E105",
         "def body captures a file-level value",
         "Def bodies are hermetic (spec §7.6): they may read only their "
         "parameters, their own locals and pure builtins — not the file's "
         "top-level runtime values.",
         {"referencing a top-level binding inside a def body",
          "moving code into a def without threading its inputs through the signature"},
         "scale = 2.0\ndef grow(g: geo) -> (out: geo) {\n    out = transform(g, scale = vec3(scale))\n}",
         "def grow(g: geo, scale: f32) -> (out: geo) {\n    out = transform(g, scale = vec3(scale))\n}"},
        // --- E2xx: types ----------------------------------------------------
        {"E201",
         "unknown or unsupported operation",
         "The call name matches no builtin and no in-scope def, or the "
         "operation is registered but deferred past the current stage "
         "(import_mesh, raycast, transfer).",
         {"a typo in the operation name",
          "calling a def that was not imported or defined",
          "calling a deferred operation — build it from defs over MVP builtins"},
         "m = bevels(g, width = 0.1)",
         "m = bevel(g, width = 0.1)  # docs builtins lists the catalog"},
        {"E202",
         "wrong number of arguments or targets",
         "Arity mismatch: too many positional arguments, a destructuring that "
         "does not match the operation's output count, or a merge with fewer "
         "than two operands.",
         {"an extra positional argument",
          "a, b = op(...) where op returns one value (or three targets from two outputs)",
          "merge(a) — merge needs at least two geometries"},
         "m = merge(a)",
         "m = merge(a, b)  # variadic: merge(a, b, c) folds left"},
        {"E203",
         "unknown parameter name",
         "A keyword argument names no parameter of the called operation or def.",
         {"a typo in the parameter name",
          "using another operation's parameter name — check the signature "
          "(`docs builtin <name>` or `docs <file> <def>`)"},
         "m = smooth(g, iters = 2)",
         "m = smooth(g, iterations = 2)"},
        {"E204",
         "type or domain mismatch",
         "The generic contract failure: a parameter got an incompatible type, "
         "an operator does not accept its operands, or the geometry kind/domain "
         "is not supported by the operation (e.g. clip on geo<instances>).",
         {"passing geo<points>/geo<instances> where geo<mesh> is required — realize() or convert first",
          "arithmetic/comparison on incompatible operand types (bool masks need & |, numbers need ==)",
          "domain = faces on a points geometry",
          "exporting a field or rng via output (bind a concrete value root)"},
         "pts = mesh_line(count = 4, length = 2.0)\nm = clip(pts, origin = (0, 0, 0), normal = (0, 1, 0))",
         "m = clip(pts, origin = (0, 0, 0), normal = (0, 1, 0))  # geo<points>: kept points of the half-space\n# for meshes the same call caps the cut loops"},
        {"E205",
         "field where a value is expected",
         "A per-element field was passed to a parameter that takes a single "
         "value (spec §4.5): fields need an aggregator to cross field -> value.",
         {"passing @attr or a field expression to a value parameter",
          "a launch-param or enum position got a field"},
         "n = fbm(scale = 2.5, rng = root)\nbox(size = n, res = 1)",
         "n = fbm(scale = 2.5, rng = root)\ns = avg_of(n, on = base)  # reduce the field first\nbox(size = vec3(s), res = 1)"},
        {"E206",
         "invalid enum value",
         "An enum parameter got a literal outside its declared set (spec §13: "
         "bare idents in enum position read as literals).",
         {"a typo in the enum literal",
          "quoting the literal (enum values are bare words, not strings)"},
         "m = compute_normals(g, mode = \"flat\")",
         "m = compute_normals(g, mode = flat)"},
        // --- E3xx: attributes / contracts -----------------------------------
        {"E301",
         "attribute not usable this way",
         "The attribute exists, but the operation is not defined for it on this "
         "domain: reserved names (@P, @N) are not writable by set, string "
         "columns do not interpolate across domains, distance_to reads only "
         "the points domain.",
         {"set(g, \"P\", ...) — positions move via set_position, normals via compute_normals",
          "promote of a string attribute",
          "reading a string attribute as a field"},
         "g = set(base, \"P\", @P * 2)",
         "g = set_position(base, pos = @P * 2)"},
        {"E302",
         "attribute is not present on the geometry",
         "The field reads an attribute the geometry does not carry — checked "
         "against the static schema when provable (marked \"static schema\"), "
         "otherwise at runtime. The [at ...]/[inline chain: ...] suffix names "
         "the binding under check and how the field flowed there (spec §9.5).",
         {"the attribute was never written upstream — write it with set() first",
          "an SDF section dropped it (the attribute barrier, §8.4) or a rename_attr removed the old name",
          "@N read on a face-less geometry (points/instances) or before any compute_normals",
          "a def argument's field is checked in its CONSUMPTION context (§7.2) — follow the inline chain"},
         "rock = set_position(base, offset = @N * @slope)  # @slope never written",
         "tagged = set(base, \"slope\", dot(@N, (0, 1, 0)))\nrock = set_position(tagged, offset = @N * @slope)"},
        {"E303",
         "expect contract violated",
         "A def's precondition (expect, spec §7.4) failed for this call — the "
         "caller passed values outside the def's declared assumptions.",
         {"an argument out of the contracted range",
          "a required attribute missing on the incoming geometry (`has @attr` form)"},
         "def clamp01(x: f32) -> (out: f32) {\n    expect x >= 0.0, \"x must be non-negative\"\n    out = min(x, 1.0)\n}\nv = clamp01(-2.0)",
         "v = clamp01(max(x, 0.0))  # satisfy the precondition at the call site"},
        {"E304",
         "ensure contract violated",
         "A def's postcondition (ensure, spec §7.4) failed — the def produced a "
         "result outside its declared contract; the bug is inside the def.",
         {"the def body computed a value outside the ensured range/shape",
          "an ensured attribute was not written on the output geometry"},
         "def positive(x: f32) -> (out: f32) {\n    out = x - 2.0\n    ensure out > 0.0, \"out must be positive\"\n}\nv = positive(1.0)",
         "out = x  # fix the body so the ensure holds"},
        {"E305",
         "group does not exist",
         "An ingroup(\"name\") read names a group the geometry does not carry "
         "(provable statically on closed schemas, otherwise runtime).",
         {"a typo in the group name",
          "the mark() that creates the group sits on another branch or was clipped away",
          "an SDF section dropped the group (attribute barrier, §8.4)"},
         "m = mark(base, \"tops\", where = dot(@N, (0, 1, 0)) > 0.9)\nbad = delete(base, where = ingroup(\"top\"))",
         "bad = delete(m, where = ingroup(\"tops\"))  # read the group on the geometry that has it"},
        {"E306",
         "SDF voxel grid too fine",
         "The voxel grid implied by the field's bounds and the requested voxel "
         "size would exceed 4096 voxels along an axis (sdf_from_mesh / "
         "mesh_from_sdf guard).",
         {"voxel size too small for the bounding box",
          "a runaway conservative bbox (huge sdf_displace amplitude estimate)"},
         "m = mesh_from_sdf(s, voxel = 0.0001)  # bbox 1000 -> 10^7 voxels/axis",
         "m = mesh_from_sdf(s, voxel = 0.5)  # keep axis dims under 4096"},
        {"E307",
         "field not evaluable in the SDF sample context",
         "sdf_displace evaluates its amount field at sample points with only "
         "@P, constants and §6.3/fbm/vnoise/random* available (spec §11.2).",
         {"reading a user attribute or group inside an sdf_displace field",
          "referencing another binding's geometry from the displace field"},
         "s = sdf_displace(base_sdf, amount = @slope * 0.1)",
         "s = sdf_displace(base_sdf, amount = fbm(@P * 3.0, rng = root) * 0.1)"},
        // --- E4xx: reproducibility / RNG ------------------------------------
        {"E401",
         "stochastic def without an rng parameter",
         "A def whose body uses stochastic operations must declare an rng "
         "parameter and be called with one (spec §7.3); checked transitively "
         "over the def call graph.",
         {"the def calls fbm/vnoise/random*/point_cloud/distribute_points but has no rng parameter",
          "a callee def is stochastic and the caller def does not thread rng through"},
         "def rock(r: f32) -> (out: geo) {\n    out = set_position(ico_sphere(1, r), offset = @N * fbm(scale = 2.0, rng = rng_from_seed(1)))\n}",
         "def rock(r: f32, rng: rng) -> (out: geo) {\n    out = set_position(ico_sphere(1, r), offset = @N * fbm(scale = 2.0, rng = rng))\n}"},
        // --- E5xx: modules --------------------------------------------------
        {"E501",
         "module not found",
         "An import path resolved to no file under the import roots (spec §7.6): "
         "`<root>/<path>.pgg`, plus the importing file's own directory.",
         {"a typo in the import path",
          "a missing --lib root for a library import",
          "a moved/renamed module file"},
         "import lib/stone_wal",
         "import lib/stone_wall  # the file must exist as <root>/lib/stone_wall.pgg"},
        {"E502",
         "import cycle",
         "Modules import each other in a cycle; the import graph must be a DAG.",
         {"a.pgg imports b.pgg while b.pgg imports a.pgg (possibly through intermediates)"},
         "# a.pgg: import b\n# b.pgg: import a",
         "# break the cycle: move the shared defs into a third module both import"},
        {"E503",
         "def recursion",
         "A def directly or transitively calls itself; recursion is forbidden "
         "(expansion is static, spec §7.6).",
         {"a def calling its own name",
          "a cycle through several defs (a calls b, b calls a)"},
         "def tree(g: geo, n: int) -> (out: geo) {\n    out = n > 0 ? tree(g, n - 1) : g\n}",
         "# unroll with repeat:\nr = repeat(g, iterations = n) |g| {\n    g = grow_once(g)\n}"},
        {"E505",
         "unknown qualified symbol",
         "A `module.symbol` reference resolved the module but not the symbol "
         "in it (or vice versa).",
         {"a typo in the qualified name",
          "the def was renamed in the library",
          "a missing import alias for the qualifier"},
         "w = walls.make_bricken(...)",
         "w = walls.make_brick(...)  # check the module's def names (docs <file> <symbol>)"},
        {"E506",
         "library def without a docstring",
         "Library files (under the import roots) require every def to carry a "
         "docstring contract (spec §6.5/§7.5).",
         {"a def in a library file without a \"\"\"...\"\"\" block"},
         "def brick(size: vec3) -> (out: geo<mesh>) {\n    out = box(size = size)\n}",
         "def brick(size: vec3) -> (out: geo<mesh>) {\n    \"\"\"One brick: a box with the standard mortar contract.\"\"\"\n    out = box(size = size)\n}"},
        // --- E6xx: runtime / graph ------------------------------------------
        {"E601",
         "aggregator selection is empty (or not unique)",
         "An aggregator ran over zero selected elements — or, for value(...), "
         "over anything but exactly one element (spec §8.10).",
         {"the where mask selected nothing (check it with `path:coverage`)",
          "value(...) whose mask matched 2+ elements",
          "an empty input geometry"},
         "k = value(@wid, on = pts, where = @index > 5)",
         "k = value(@wid, on = pts, where = @index == 0)  # exactly one element"},
        {"E604",
         "launch parameter not bound",
         "A param without a default was not given a value at launch (spec §6.6).",
         {"a missing --param name=value on the command line / params call",
          "a param renamed in the file but not at the call site"},
         "param world_seed: int  # no default\noutput rock",
         "PggTool run rock.pgg --param world_seed=42"},
        {"E605",
         "executable file has no output",
         "A file run as a graph must declare at least one output root (spec "
         "§6.7); library modules of pure defs are exempt.",
         {"a missing `output <name>` line",
          "the output name not matching any top-level binding"},
         "rock = set_position(base, offset = @N)",
         "rock = set_position(base, offset = @N)\noutput rock"},
        {"E606",
         "probe or pull target invalid",
         "A --probe/pull spec failed to resolve: no such binding/instance/def, "
         "an unknown inspector, a bad parameter value, or a field target for a "
         "value-only pull (spec §9).",
         {"a typo in the probe path — list the graph with the schema inspector first",
          "an unknown inspector name (schema/stats/coverage/table/sample/slice/check)",
          "a malformed [param=value] section (vectors in parens, points separated by ';')",
          "probing an attribute terminal on a non-geo binding"},
         "PggTool run f.pgg --probe 'rockk:stats'",
         "PggTool run f.pgg --probe 'rock:stats'  # exact flat name or instance path"},
        {"E607",
         "zone runtime failure",
         "A repeat/foreach zone failed at run time (spec §5.4): negative "
         "iterations (clamped to 0), a foreach collection that is not "
         "geo<mesh>/geo<points>, or a body producing a wrong/mixed piece kind.",
         {"foreach over geo<instances> — realize() first or iterate the anchor points",
          "the foreach body rebinding the item port to a non-geometry value",
          "pieces of mixed kinds (mesh on one piece, points on another)"},
         "f = foreach piece in inst {\n    piece = smooth(piece, iterations = 1)\n}",
         "f = foreach piece in realize(inst) {\n    piece = smooth(piece, iterations = 1)\n}"},
        {"E608",
         "fracture cannot build a partition",
         "fracture got an input it cannot split (spec §8.11): no sites left "
         "after deduplication, an empty mesh, or a degenerate bounding box.",
         {"all plane points coincide (0 distinct sites)",
          "an empty input mesh",
          "a flat/degenerate bbox the voxelizer cannot grid"},
         "f = fracture(g, planes = mesh_line(count = 1, length = 0.0), rng = root)",
         "f = fracture(g, planes = sites, rng = root)  # >= 2 distinct sites, non-empty closed mesh"},
        {"E609",
         "attribute/group conflict in merge or realize",
         "A name present on both merge operands (or on several realize "
         "variants) must sit on the same domain set with the same typeinfo, "
         "and detail values must be equal (spec §8.3).",
         {"one side wrote @tint on points, the other on faces — promote to one domain",
          "different set(..., typeinfo = ...) tags for the same name",
          "different detail values of one attribute across operands"},
         "a = set(g1, \"k\", 1.0)              # detail f32\nb = set(g2, \"k\", @P.x)           # points f32\nm = merge(a, b)",
         "b = set(g2, \"k\", 1.0)              # same domain and typeinfo\nm = merge(a, b)"},
        {"E610",
         "typeinfo contract in set()",
         "Vector attributes under free names need an explicit typeinfo tag; the "
         "tag must fit the value type; reserved instance stamps (orient/tint/"
         "scale/variant) have fixed types (spec §4.3).",
         {"set(g, \"dir\", @N) without typeinfo = vector/normal/...",
          "typeinfo = quaternion on a vec3 (needs vec4)",
          "writing @scale as vec3 (must be f32) or @orient as vec3 (must be vec4)"},
         "g = set(base, \"up\", (0, 1, 0))",
         "g = set(base, \"up\", (0, 1, 0), typeinfo = vector)"},
        {"E611",
         "instance stamp contract violated",
         "geo<instances> carry placement as stamps: transform scale must be "
         "uniform (a non-uniform scale cannot be stamped), and @variant must "
         "index the variants list in realize (spec §8.8).",
         {"transform(inst, scale = (1, 2, 1)) — scale the source geometry or realize() first",
          "@variant outside [0, variants.size())"},
         "m = transform(inst, scale = (1, 2, 1))",
         "src = transform(src, scale = (1, 2, 1))\nm = instance_on_points(pts, source = src)  # non-uniform scale lives in the source"},
        {"E612",
         "plane normal must be a non-zero finite vector",
         "clip/mirror got a zero or non-finite normal (constant values are "
         "caught statically).",
         {"a computed normal that degenerated to zero",
          "NaN/inf components from an upstream division"},
         "m = clip(g, origin = (0, 0, 0), normal = (0, 0, 0))",
         "m = clip(g, origin = (0, 0, 0), normal = (0, 1, 0))"},
        // --- W0xx: warnings (the run continues) ------------------------------
        {"W001",
         "name defined but never used",
         "A binding nothing reads — a symptom of an unfinished chain (spec "
         "§6.5). Bindings declared as output count as used.",
         {"a leftover intermediate after refactoring",
          "a typo downstream that reads a different name"},
         "disp = @N * n * 0.35\nrock = set_position(base, offset = @N)",
         "disp = @N * n * 0.35\nrock = set_position(base, offset = disp)"},
        {"W002",
         "SDF surface reached the voxel grid boundary",
         "The iso-surface touches the conservative bbox of the voxel grid, so "
         "the extracted mesh may be clipped (spec §8.4 boundary policy).",
         {"a field whose true surface extends past the estimated bounds (large smooth k, displace amplitude)",
          "an off-center primitive whose bbox estimate is tight"},
         "m = mesh_from_sdf(sdf_union_smooth(a, b, k = 3.0), voxel = 0.1)",
         "m = mesh_from_sdf(sdf_union_smooth(a, b, k = 0.6), voxel = 0.1)  # smaller k, or clip after extraction"},
        {"W003",
         "one rng feeds several stochastic nodes",
         "Two or more stochastic operations share one generator — usually an "
         "unwanted correlation; independence is split_rng, intentional reuse is "
         "alias_rng (spec §6.5).",
         {"passing the same rng to fbm and to distribute_points",
          "reusing a root generator without split_rng"},
         "n = fbm(scale = 2.0, rng = root)\npts = distribute_points(g, density = 2.0, rng = root)",
         "noise_rng = split_rng(root, key = \"noise\")\nscatter_rng = split_rng(root, key = \"scatter\")"},
        {"W004",
         "stochastic op in repeat ignores @iteration",
         "A stochastic operation inside a repeat body whose rng does not depend "
         "on @iteration produces the identical draw every iteration — legal, "
         "but suspicious (spec §6.5).",
         {"split_rng(parent, key = @iteration) forgotten inside a repeat body"},
         "r = repeat(g, iterations = 8) |g| {\n    g = set_position(g, offset = @N * fbm(scale = 3.0, rng = root))\n}",
         "r = repeat(g, iterations = 8) |g| {\n    it_rng = split_rng(root, key = @iteration)\n    g = set_position(g, offset = @N * fbm(scale = 3.0, rng = it_rng))\n}"},
        {"W005",
         "stochastic op in foreach ignores @piece_index",
         "Same as W004 for foreach: the draw is identical for every piece "
         "unless the rng depends on @piece_index (spec §6.5).",
         {"split_rng(parent, key = @piece_index) forgotten inside a foreach body"},
         "f = foreach piece in m {\n    piece = set_position(piece, offset = @N * random(0.0, 1.0, rng = root))\n}",
         "f = foreach piece in m {\n    p_rng = split_rng(root, key = @piece_index)\n    piece = set_position(piece, offset = @N * random(0.0, 1.0, rng = p_rng))\n}"},
        {"W006",
         "stale @N read",
         "The points moved (set_position/smooth/subdivide/clip) after normals "
         "were stored and compute_normals did not follow, so the stored @N no "
         "longer matches the surface (spec §4.3).",
         {"reading @N after a displacement without recomputing normals",
          "a stale corner-N column from compute_normals(flat/auto) earlier in the chain"},
         "moved = set_position(base, offset = @N * 0.35)\nlit = set(moved, \"l\", dot(@N, sun))",
         "moved = set_position(base, offset = @N * 0.35)\nfixed = compute_normals(moved)\nlit = set(fixed, \"l\", dot(@N, sun))"},
    };
    return kCards;
}

}  // namespace

const ErrorCard* findErrorCard(const std::string& code) {
    for (const ErrorCard& c : cards())
        if (c.code == code) return &c;
    return nullptr;
}

std::vector<std::string> allErrorCodes() {
    std::vector<std::string> out;
    out.reserve(cards().size());
    for (const ErrorCard& c : cards()) out.push_back(c.code);
    return out;
}

}  // namespace pgg
