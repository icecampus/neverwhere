#include "../../pch.h"

#include "builtin_docs.h"

namespace pgg {
namespace {

std::string paramTypeText(const ParamSig& p) {
    if (!p.enumValues.empty()) {
        std::string out = "enum {";
        for (size_t i = 0; i < p.enumValues.size(); ++i) out += (i ? ", " : "") + p.enumValues[i];
        return out + "}";
    }
    if (p.base == ScalarType::Any && p.kind == ParamKind::Either) return {};  // dot(a, b)
    Type t{p.base, p.kind != ParamKind::Value, p.geoKind, p.isList};
    return typeName(t);
}

std::string paramText(const ParamSig& p) {
    std::string out = p.name;
    const std::string t = paramTypeText(p);
    if (!t.empty()) out += ": " + t;
    if (p.optional) out += "?";
    if (p.defPosition)
        out += " = position()";
    else if (p.defIndex)
        out += " = index()";
    else if (p.hasDefValue)
        out += " = " + valueToString(p.defValue);
    else if (p.optional)
        out += " = none";
    return out;
}

}  // namespace

std::string builtinSignatureText(const BuiltinSig& sig) {
    std::string out = std::string(sig.name) + "(";
    for (size_t i = 0; i < sig.params.size(); ++i) {
        if (i) out += ", ";
        out += paramText(sig.params[i]);
    }
    if (sig.variadic) out += sig.params.empty() ? "..." : ", ...";
    out += ")";
    if (!sig.results.empty()) {
        out += " -> (";
        for (size_t i = 0; i < sig.results.size(); ++i) {
            if (i) out += ", ";
            out += typeName(sig.results[i]);
        }
        out += ")";
    } else if (!sig.exprFunc && sig.result.base != ScalarType::None) {
        out += " -> " + typeName(sig.result);
    }
    return out;
}

namespace {

// One row per registry entry, in registry order (the completeness test pins
// this against builtinRegistry()). Summaries condense spec §8/§6.3; examples
// assume `root = rng_from_seed(1)` and a geometry `g` where needed.
const std::vector<BuiltinDoc>& docs() {
    static const std::vector<BuiltinDoc> kDocs = {
        // --- §8.1 sources ---------------------------------------------------
        {"ico_sphere", "sources",
         "Icosahedron-based sphere with uniform tessellation; the base shape for "
         "organics. Writes @P and a smooth vertex @N.",
         "base = ico_sphere(subdiv = 2, radius = 1.0)"},
        {"box", "sources",
         "Axis-aligned box centered at the origin with subdivided faces (res per "
         "side). Writes @P and smooth vertex @N (corners carry their octant "
         "diagonal).",
         "b = box(size = (2, 1, 1), res = 1)"},
        {"grid", "sources",
         "Flat subdivided sheet in the XZ plane (normals +Y); res = (nx, nz) cell "
         "counts per axis, a scalar broadcasts. Writes @uv (vec2, points) — the "
         "parametric coordinates for ribbons/leaves/heightfields.",
         "leaf = grid(size = (0.4, 1.0), res = (2, 24))"},
        {"mesh_line", "sources",
         "A row of count points from the origin along dir (not centered); the "
         "skeleton for instance chains. count = 1 — one point at the origin; "
         "count <= 0 — a legal empty geometry.",
         "pts = mesh_line(count = 5, length = 2.0, dir = (1, 0, 0))"},
        {"empty_mesh", "sources",
         "Explicit empty geo<mesh> — the identity of merge/repeat over meshes "
         "(mesh_line(count = 0) is the points equivalent).",
         "acc = empty_mesh()"},
        {"empty_points", "sources",
         "Explicit empty geo<points> — the identity of merge/repeat over points.",
         "acc = empty_points()"},
        {"point_cloud", "sources",
         "Random point cloud inside bounds, addressed by (point_index, xyz_lane) "
         "of the given rng.",
         "pts = point_cloud(count = 50, bounds = (2, 1, 2), rng = root)"},
        // --- §8.2 transforms --------------------------------------------------
        {"transform", "transforms",
         "Affine translate/rotate (Euler, degrees)/scale of the whole geometry. "
         "Attributes follow their typeinfo (vectors rotate+scale, normals use "
         "inverse-transpose, quaternions compose). On geo<instances> the scale "
         "must be uniform (E611).",
         "m = transform(g, translate = (0, 1, 0), rotate = (0, 45, 0))"},
        {"set_position", "transforms",
         "The main displacement node: moves or replaces @P per element by a "
         "field, under a mask (points domain).",
         "rock = set_position(base, offset = @N * fbm(scale = 2.5, rng = root) * 0.35)"},
        {"smooth", "transforms",
         "Laplacian smoothing of positions; topology unchanged.",
         "s = smooth(g, iterations = 2, factor = 0.5)"},
        {"compute_normals", "transforms",
         "Recomputes normals: smooth (point @N), by_angle (area/angle-weighted), "
         "flat (corner-N facets), auto (Blender auto smooth: edges sharper than "
         "angle degrees stay hard). flat/auto write the corner column and leave "
         "point @N untouched.",
         "n = compute_normals(g, mode = auto, angle = 30)"},
        // --- §8.5 rng ---------------------------------------------------------
        {"rng_from_seed", "rng",
         "Creates the root reproducible counter-based generator (spec §5.2).",
         "root = rng_from_seed(42)"},
        {"split_rng", "rng",
         "Derives a stable independent sub-sequence from a parent generator by an "
         "int or string key, without changing the parent.",
         "noise_rng = split_rng(root, key = \"surface\")"},
        {"alias_rng", "rng",
         "A named alias of the same generator: declares intentional reuse to the "
         "lint (W003), the sequence is identical.",
         "shared = alias_rng(root)"},
        // --- §8.5 field generators --------------------------------------------
        {"fbm", "fields",
         "Layered Perlin/simplex noise field from a stable generator, sampled at "
         "`at` (default: the context @P). octaves/lacunarity/gain shape the "
         "spectrum.",
         "n = fbm(scale = 2.5, octaves = 5, rng = noise_rng)"},
        {"vnoise", "fields",
         "Vector noise field (vec3) from a stable generator — swirls and domain "
         "warps.",
         "w = vnoise(scale = 1.5, rng = warp_rng)"},
        {"random", "fields",
         "Per-element uniform f32 in [lo, hi), addressed by (rng, counter, "
         "component); counter = @id survives reindexing, counter = 0 gives one "
         "value for the whole geometry.",
         "k = random(0.8, 1.2, rng = tint_rng)"},
        {"random_vec", "fields",
         "Per-element uniform vec3 with independent components — axis jitter of "
         "position/scale. For tints/brightness use scalar random and lift it to "
         "vec3 instead.",
         "j = random_vec((-0.1, 0, -0.1), (0.1, 0, 0.1), rng = jit_rng)"},
        {"random_int", "fields",
         "Per-element uniform int in [0, n), same addressing as random.",
         "v = random_int(3, rng = var_rng)"},
        {"distance_to", "fields",
         "Distance to the closest surface of another geometry (proximity field), "
         "evaluable on the points domain.",
         "d = distance_to(target = cliffs)"},
        {"position", "fields",
         "Explicit @P reader — for passing the position as a field into a def.",
         "w = fbm(at = position(), scale = 2.0, rng = root)"},
        {"normal", "fields",
         "Explicit @N reader — for passing the normal as a field into a def.",
         "m = displace(g, dir = normal(), amount = 0.2)"},
        {"index", "fields",
         "Explicit @index reader — the element index as a field.",
         "parity = index() % 2"},
        // --- §6.3 expression functions ------------------------------------------
        {"dot", "expr",
         "Scalar/vector dot product of two scalars or two vectors of the same "
         "width.",
         "slope = dot(@N, (0, 1, 0))"},
        {"cross", "expr", "Cross product of two vec3.",
         "t = cross(@N, (0, 0, 1))"},
        {"length", "expr", "Euclidean length of a vector (or absolute value of a scalar).",
         "r = length(@P - (0, 1, 0))"},
        {"normalize", "expr", "Unit-length copy of a vector.",
         "up = normalize((0.2, 1, 0))"},
        {"clamp", "expr", "Clamps x into [lo, hi], componentwise for vectors.",
         "k = clamp(@slope, 0.0, 1.0)"},
        {"smoothstep", "expr", "Smooth Hermite interpolation between e0/e1 edges at x.",
         "m = smoothstep(0.3, 0.6, @slope)"},
        {"mix", "expr", "Linear interpolation a->b by t (componentwise).",
         "c = mix(@Cd, (1, 1, 1), 0.2)"},
        {"abs", "expr", "Absolute value, componentwise for vectors.",
         "a = abs(@P.x)"},
        {"min", "expr", "Componentwise minimum of two scalars/vectors.",
         "h = min(@P.y, 2.0)"},
        {"max", "expr", "Componentwise maximum of two scalars/vectors.",
         "h = max(@P.y, 0.0)"},
        {"floor", "expr", "Largest integer not greater than x, componentwise.",
         "row = floor(@uv.y * 24)"},
        {"pow", "expr", "x raised to y, componentwise.",
         "k = pow(@slope, 2.0)"},
        {"sin", "expr", "Sine of x in RADIANS (degrees need radians(x) first); ints promote to f32, vectors componentwise.",
         "w = sin(@P.x * 2.0)"},
        {"cos", "expr", "Cosine of x in radians; componentwise.",
         "w = cos(@P.z * 2.0)"},
        {"tan", "expr", "Tangent of x in radians; componentwise.",
         "s = tan(@P.y)"},
        {"asin", "expr", "Arc sine (radians out); componentwise.",
         "a = asin(clamp(@N.y, -1.0, 1.0))"},
        {"acos", "expr", "Arc cosine (radians out); componentwise.",
         "a = acos(clamp(@N.y, -1.0, 1.0))"},
        {"atan", "expr", "Arc tangent (radians out); componentwise.",
         "a = atan(@slope)"},
        {"sqrt", "expr", "Square root; componentwise.",
         "d = sqrt(@P.x * @P.x + @P.z * @P.z)"},
        {"exp", "expr", "e raised to x; componentwise.",
         "k = exp(-@h)"},
        {"log", "expr", "Natural logarithm; componentwise.",
         "k = log(@area)"},
        {"ceil", "expr", "Smallest integer not less than x; componentwise.",
         "n = ceil(@uv.x * 8)"},
        {"round", "expr", "Rounds away from zero (round(2.5) = 3); componentwise.",
         "n = round(@t * 10)"},
        {"fract", "expr", "Fractional part x - floor(x); componentwise.",
         "f = fract(@uv.y * 24)"},
        {"radians", "expr", "Degrees to radians (angles in trig functions are radians; transform/orient_from_euler take degrees).",
         "w = sin(radians(45))"},
        {"degrees", "expr", "Radians to degrees.",
         "deg = degrees(atan(@slope))"},
        {"atan2", "expr", "Two-argument arc tangent of y/x (radians out).",
         "a = atan2(@P.z, @P.x)"},
        {"mod", "expr", "GLSL modulo a - b*floor(a/b) (sign of the divisor; the integer remainder is the % operator).",
         "stripe = mod(floor(@P.y * 4), 2.0)"},
        {"vec2", "expr", "Builds a vec2 from computed components (a vector literal holds numeric constants only).",
         "uv = vec2(u, v)"},
        {"vec3", "expr", "Builds a vec3 from computed components.",
         "c = vec3(s * 1.04, s, s * 0.96)"},
        {"vec4", "expr", "Builds a vec4 from computed components.",
         "q = vec4(x, y, z, w)"},
        {"int", "expr", "Casts a numeric scalar to int (truncation).",
         "i = int(@t * 10)"},
        {"f32", "expr", "Casts a numeric scalar to f32.",
         "x = f32(@index)"},
        {"bool", "expr", "Casts a numeric scalar to bool (0 is false).",
         "b = bool(@flag)"},
        {"orient_from_euler", "expr",
         "Euler angles (degrees) to a quaternion (x, y, z, w) — the way to author "
         "@orient for instancing (§8.8).",
         "o = orient_from_euler((0, @index * 15, 0))"},
        {"ramp", "expr",
         "Piecewise-linear color/value ramp: ramp(x, pos0, val0, pos1, val1, ...) "
         "maps x through the control points.",
         "c = ramp(@h, 0.0, (0.2, 0.1, 0.05), 1.0, (0.9, 0.9, 0.8))"},
        // --- §8.6 groups -----------------------------------------------------
        {"mark", "groups",
         "Creates/overwrites a named group (mask) on the geometry at domain "
         "points/corners/faces/detail.",
         "m = mark(g, \"flat_tops\", where = dot(@N, (0, 1, 0)) > 0.9)"},
        {"unmark", "groups", "Removes a named group.",
         "u = unmark(g, \"flat_tops\")"},
        {"ingroup", "groups",
         "Reads a group in an expression: field<bool>, true on the grouped "
         "elements.",
         "c = set(g, \"Cd\", ingroup(\"edge\") ? @Cd * 1.4 : @Cd)"},
        // --- §8.7 attributes --------------------------------------------------
        {"set", "attributes",
         "Writes a named attribute (materializes a field): domain from the "
         "explicit argument or inferred (constant -> detail, else points); "
         "typeinfo tags the geometric meaning of vectors (required for vec3/vec4 "
         "under free names — E610). The name is global: other domains drop the "
         "column.",
         "g = set(base, \"slope\", dot(@N, (0, 1, 0)))"},
        {"remove_attr", "attributes",
         "Drops an attribute column; remove_attr(g, \"N\") also drops the "
         "dedicated normals column (reads fall back to face-derived normals).",
         "c = remove_attr(g, \"N\")"},
        {"rename_attr", "attributes", "Renames an attribute column.",
         "r = rename_attr(g, \"old\", \"new\")"},
        {"promote", "attributes",
         "Explicit domain conversion of an attribute (points/corners/faces/"
         "detail) with mode sum/average/first; the default interpolation without "
         "promote is silent.",
         "f = promote(g, \"slope\", from = points, to = faces, mode = average)"},
        // --- §8.3 topology ----------------------------------------------------
        {"merge", "topology",
         "Concatenates two or more geometries of one kind (no vertex welding), "
         "variadic left fold. Columns/groups union by name with neutral fill; a "
         "name on different domains/typeinfo across operands is E609.",
         "m = merge(walls, roof, porch)"},
        {"select", "topology",
         "Picks one of two geometries by a value-level bool (a constant or a "
         "@param). A constant cond evaluates only the taken branch. Ternary "
         "? : does not accept geo — use select or separate defs.",
         "face = select(clock, a = hexa_clock, b = hexa_plain)"},
        {"delete", "topology",
         "Removes elements under a mask; the deletion cascades to incident "
         "higher-domain elements (point -> its faces and corners).",
         "m = delete(g, where = @P.y < 0, domain = points)"},
        {"clip", "topology",
         "Half-space clip (Houdini Clip / Blender Bisect): keeps the side the "
         "normal points to, caps closed cut loops with flat lids that inherit "
         "the first cut face's attributes and join cap_group. Instances need "
         "realize first; a zero normal is E612.",
         "half = clip(g, origin = (0, 1, 0), normal = (0, 1, 0), cap_group = \"cut\")"},
        {"extrude", "topology",
         "Extrudes selected faces along their normals: region (one sheet with "
         "side walls on the selection boundary) or individual (per face). "
         "distance/where are face fields; negative distance pushes inward.",
         "e = extrude(wall, distance = 0.2, where = ingroup(\"panels\"), side_group = \"jamb\")"},
        {"inset", "topology",
         "Per-face inset (Blender inset individual): an inner polygon offset by "
         "amount from the edges and pushed by depth along the normal, the rim "
         "becomes a quad ring. Panels, doors, window openings in one call.",
         "i = inset(facade, amount = 0.1, depth = -0.05, where = ingroup(\"windows\"))"},
        {"bevel", "topology",
         "One-segment edge chamfer (segments = 1): interior edges with both "
         "faces selected shrink the faces and fill the gap; bevel_group marks "
         "the chamfer faces (edge-wear idiom). Rounding = bevel + "
         "subdivide(catmull_clark).",
         "b = bevel(g, width = 0.03, bevel_group = \"edge\")"},
        {"separate", "topology",
         "Splits into (yes, no) halves by a mask on the given domain; both sides "
         "follow delete's cascade and keep the input schema.",
         "yes, no = separate(g, where = dot(@N, (0, 1, 0)) > 0.5)"},
        {"triangulate", "topology",
         "Triangulates polygons (fans for convex, ears for concave); face "
         "columns/groups copy onto each triangle, points untouched.",
         "t = triangulate(g)"},
        {"subdivide", "topology",
         "Subdivides the surface level times: linear (shape-preserving), "
         "catmull_clark (smoothing), loop (triangles only). Columns blend "
         "linearly; a stored @N goes stale under smoothing schemes (W006).",
         "s = subdivide(g, level = 2, scheme = catmull_clark)"},
        {"merge_by_distance", "topology",
         "Welds points closer than dist (union-find on a uniform grid; the "
         "lowest index represents the cluster — deterministic first-wins). Fixes "
         "seams after merge/fracture/mirror.",
         "w = merge_by_distance(g, dist = 0.001)"},
        {"mirror", "topology",
         "Appends the reflection across the (origin, normal) plane (Blender "
         "Mirror); points within weld of the plane are shared (a welded seam). "
         "geo<instances> is E204 (realize first).",
         "both = mirror(half, origin = (0, 0, 0), normal = (1, 0, 0), weld = 0.0001)"},
        {"circle", "topology",
         "sides points on a circle in the XY plane (z = 0, CCW from +X) — the "
         "profile source for sweep.",
         "prof = circle(sides = 8, radius = 0.05)"},
        {"bezier_points", "topology",
         "Cubic Bezier through four control points, count samples uniform by the "
         "parameter, writes @t — path authoring without polynomial fiddling.",
         "path = bezier_points(p0 = (0, 0, 0), p1 = (1, 1, 0), p2 = (2, 1, 0), p3 = (3, 0, 0), count = 16)"},
        {"resample_points", "topology",
         "Arc-length-uniform resampling of an ordered path: count points, point "
         "columns/groups lerp along the segment, writes @t (the length fraction).",
         "even = resample_points(path, count = 32)"},
        {"bake_ao", "topology",
         "Bakes ambient occlusion into an f32 attribute (default @ao): rays "
         "cosine-weighted hemisphere samples per element, hits closer than "
         "distance darken; one-sided (backface hits ignored). Subdivide large "
         "flat faces first — AO lands on vertices.",
         "a = bake_ao(g, rays = 32, distance = 1.5, rng = ao_rng)"},
        {"sweep", "topology",
         "Extrudes a profile (points in XY: circle, mesh_line) along an ordered "
         "path: tubes/beams/rails, and ribbons/leaves with profile_closed = "
         "false. The parallel-transport frame does not accumulate twist; the "
         "path's @scale/@profile_scale/@twist shape the profile per ring; ring "
         "points inherit the path's other columns and get @uv. A vertical path "
         "(along +Y) maps profile X to world Z (depth) and profile Y to world X "
         "(width), so profile_scale = (depth, width) not (width, depth).",
         "tube = sweep(path, profile = circle(8, radius = 0.05))"},
        {"islands", "topology",
         "Marks connected components; writes @island_id (int, faces).",
         "m = islands(g)"},
        // --- §8.8 scatter and instancing ----------------------------------------
        {"distribute_points", "scatter",
         "Scatters points over a mesh surface at a field-driven density; poisson "
         "keeps min_dist by stable tiled dart-throwing (thread-count "
         "independent). Points inherit surface attributes.",
         "pts = distribute_points(cliff, density = 3.0, min_dist = 0.2, rng = scatter_rng)"},
        {"instance_on_points", "scatter",
         "Instances meshes on points; copy-stamp reads the reserved point "
         "attributes @scale (f32), @orient (vec4 quaternion — author via "
         "orient_from_euler), @variant (int, picks from variants), @tint (vec3). "
         "Variability lives in the data, not the node.",
         "inst = instance_on_points(pts, source = rock)"},
        {"realize", "scatter",
         "Materializes geo<instances> into a mesh (the only place instances get "
         "expensive): stamps transform every domain by typeinfo, unions variant "
         "schemas (the merge rule — conflicts are E609), materializes @tint on "
         "points; @variant out of range is E611.",
         "m = realize(inst)"},
        // --- §8.10 aggregators ---------------------------------------------------
        {"bbox", "aggregators", "Axis-aligned bounds: (min, max) vec3 pair.",
         "lo, hi = bbox(g)"},
        {"extent", "aggregators", "bbox size as vec3.",
         "size = extent(g)"},
        {"centroid", "aggregators", "Mean position of the geometry's points.",
         "c = centroid(g)"},
        {"count", "aggregators", "Number of elements on the domain under the mask.",
         "n = count(g, domain = faces, where = @area > 0.5)"},
        {"value", "aggregators",
         "The field's value on exactly one selected element of `on`, in the "
         "field's own type (0 or 2+ selected is E601) — the row-model accessor "
         "of foreach over points. Inside an enclosing field the reads bind to "
         "`on`, not to the consuming geometry.",
         "w = value(@wid, on = row)"},
        {"min_of", "aggregators", "Minimum of an f32 field over the selected elements of `on`.",
         "h = min_of(@P.y, on = g)"},
        {"max_of", "aggregators", "Maximum of an f32 field over the selected elements of `on`.",
         "h = max_of(@P.y, on = g)"},
        {"avg_of", "aggregators", "Mean of an f32 field over the selected elements of `on`.",
         "m = avg_of(@slope, on = g)"},
        {"sum_of", "aggregators", "Sum of an f32 field over the selected elements, in strict @index order (deterministic).",
         "total = sum_of(@area, on = g)"},
        // --- §8.4 SDF ----------------------------------------------------------
        {"sdf_sphere", "sdf", "Analytic sphere SDF of radius r.",
         "s = sdf_sphere(r = 1.0)"},
        {"sdf_box", "sdf", "Analytic box SDF of the given size.",
         "s = sdf_box(size = (1, 0.5, 0.5))"},
        {"sdf_union", "sdf", "Hard union of two SDF fields.",
         "s = sdf_union(a, b)"},
        {"sdf_union_smooth", "sdf",
         "Union with blend radius k — always a valid result, unlike mesh booleans.",
         "s = sdf_union_smooth(a, b, k = 0.2)"},
        {"sdf_subtract", "sdf", "Subtracts field b from a — cavities and cracks without topology pain.",
         "s = sdf_subtract(a, b)"},
        {"sdf_subtract_smooth", "sdf", "Subtraction with blend radius k at the cut.",
         "s = sdf_subtract_smooth(a, b, k = 0.1)"},
        {"sdf_intersect", "sdf", "Intersection of two SDF fields.",
         "s = sdf_intersect(a, b)"},
        {"sdf_displace", "sdf",
         "Noise displacement right in the distance field; the amount field "
         "evaluates at the sample point (only @P/constants/§6.3/fbm/vnoise/"
         "random* are available there — E307).",
         "s = sdf_displace(a, amount = fbm(@P * 3.0, rng = root) * 0.1)"},
        {"sdf_instance_on_points", "sdf",
         "The SDF twin of instance_on_points: one immutable source field "
         "transformed by each point's @P/@scale/@orient and merged into one "
         "field (k = 0 hard union, k > 0 smooth).",
         "s = sdf_instance_on_points(pts, source = pebble_sdf, k = 0.05)"},
        {"sdf_from_mesh", "sdf", "Voxelizes a mesh into an SDF (booleans over imported/baked geometry).",
         "s = sdf_from_mesh(g, voxel = 0.05)"},
        {"mesh_from_sdf", "sdf",
         "Extracts the iso-surface by marching cubes. The lattice starts at the "
         "conservative bbox minus voxel*(1+0.381966) (irrational phase keeps "
         "rational feature planes off lattice nodes); features thinner than one "
         "voxel may vanish — keep walls >= 2*voxel. The mesh carries @P only "
         "(attribute barrier).",
         "m = mesh_from_sdf(s, voxel = 0.05)"},
        {"sdf_grind", "sdf",
         "Grinding: cuts a by the mutual-penetration mid-surface with b — "
         "max(a, a - b + gap); the symmetric pair leaves a uniform gap exactly "
         "centered on the contact surface.",
         "s = sdf_grind(stone_a, stone_b, gap = 0.01)"},
        // --- §8.3 islands / §8.11 fracture ---------------------------------------
        {"fracture", "fracture",
         "Voronoi splitting of a closed mesh by site points (SDF model v0): "
         "pieces come out watertight with @island_id (faces), ready for foreach. "
         "0 sites after dedup or an empty mesh is E608.",
         "pieces = fracture(g, planes = sites, rng = root)"},
        // --- deferred (registered, not supported at this stage) ------------------
        {"import_mesh", "deferred",
         "Deferred past this stage: fetch an immutable mesh through the host "
         "AssetProvider (no host asset contract yet).",
         "# not available at this stage"},
        {"raycast", "deferred",
         "Deferred past this stage: ray queries against a mesh (placement, "
         "normal capture).",
         "# not available at this stage"},
        {"transfer", "deferred",
         "Deferred past this stage: transfer named attributes between geometries "
         "(nearest sampling).",
         "# not available at this stage"},
    };
    return kDocs;
}

}  // namespace

const BuiltinDoc* findBuiltinDoc(const std::string& name) {
    for (const BuiltinDoc& d : docs())
        if (d.name == name) return &d;
    return nullptr;
}

std::vector<std::string> suggestBuiltinNames(const std::string& name, size_t cap) {
    std::vector<std::string> near;
    if (name.empty() || cap == 0) return near;
    for (const BuiltinDoc& d : docs())
        if (d.name.rfind(name, 0) == 0 || (name.size() >= 3 && name.rfind(d.name, 0) == 0))
            near.push_back(d.name);
    if (near.empty())
        for (const BuiltinDoc& d : docs())
            if (d.name.find(name) != std::string::npos) near.push_back(d.name);
    std::sort(near.begin(), near.end());
    if (near.size() > cap) near.resize(cap);
    return near;
}

const std::vector<BuiltinDoc>& allBuiltinDocs() { return docs(); }

}  // namespace pgg
