"""Neverwhere bowl landscape - ROCKY walls variant (clifs_rock.blend).

Combines the bowl landscape structure (grass terraces, ported C++
generateLandscapeBowl) with vertical cliff walls rendered like the
Rock_Gen_2.0 generator: a solid wall band run through a coarse-to-fine
"voxel remesh -> sides-only vertical-noise displace" modifier stack, then
the 'Shader Attribs' geometry nodes group that bakes edge/overhang/dot
attributes and assigns the stratified 'Rock' material.

The bowl algorithm and the grass terraces are reused from
tools/blender/nw_bowl_rebuild.py (single source of truth). The rock node
groups (Displace, Shader Attribs) and the Rock material are APPENDED from
assets/blender/Rock_Gen_2.0.blend, not reimplemented.

Usage: open assets/blender/clifs_rock.blend in Blender ->
Scripting -> open this file -> Run Script. Or via MCP:
exec the file, then call main(limit=12) for a quick subset test or
main() for the full bowl.
"""

import bpy
import os

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
BASE_SCRIPT = os.path.join(_THIS_DIR, "nw_bowl_rebuild.py")
REPO_ROOT = os.path.dirname(os.path.dirname(_THIS_DIR))
ROCK_GEN_BLEND = os.path.join(REPO_ROOT, "assets", "blender", "Rock_Gen_2.0.blend")

ROCK_NODE_GROUPS = ("Displace", "Shader Attribs")
ROCK_MATERIAL = "Rock"

# Wall band cross-section (relative to the cliff edge, in metres).
BAND_OUT = 0.12        # forward past the cell edge (> grass lip OVER=0.05)
BAND_BACK = 0.65       # back into the higher cell, keeps the band solid
BAND_BELOW = 0.30      # sink below the lower terrace so no seam shows
BAND_ABOVE = 0.06      # poke above the grass cap like a rocky rim
BAND_OVERLAP = 0.06    # lengthwise overlap so neighbouring boxes fuse

# Coarse-to-fine erosion passes: (voxel_size, displace params dict).
# Direction menu: 0=Even, 1=Horizontal, 2=Vertical. Filter menu: 0=None,
# 1=High Contrast(?), per Rock_Gen file pass2 used value 1 with a wave look.
# Voxel sizes are coarser than the original Rock_Gen rock (0.03/0.015/0.01)
# because the bowl has ~250 wall edges; ~0.035 finest keeps the full build
# around 1.5-2M faces while strata still read at landscape camera distance.
ROCK_PASSES = [
    (0.08, {"Sides Only": True, "Strength": 0.35, "Noise Scale": 3.0,
            "Noise Filter": 0, "Midlevel": 0.35, "Direction": 2, "Seed": 1}),
    (0.05, {"Sides Only": True, "Strength": 0.12, "Noise Scale": 4.0,
            "Noise Filter": 1, "Midlevel": 0.3, "Direction": 1, "Seed": 113}),
    (0.035, {"Sides Only": False, "Strength": 0.06, "Noise Scale": 5.0,
             "Noise Filter": 0, "Midlevel": 0.4, "Direction": 0, "Seed": 0}),
]


def load_base():
    """Exec the block-variant script without running its main()."""
    ns = {"__name__": "nw_bowl_base", "__file__": BASE_SCRIPT}
    with open(BASE_SCRIPT, "r", encoding="utf-8") as f:
        src = f.read()
    exec(compile(src, BASE_SCRIPT, "exec"), ns)
    return ns


def append_rock_assets():
    """Append Displace/Shader Attribs node groups and Rock material once."""
    missing_groups = [n for n in ROCK_NODE_GROUPS if bpy.data.node_groups.get(n) is None]
    need_material = bpy.data.materials.get(ROCK_MATERIAL) is None
    if not missing_groups and not need_material:
        print("[NW] Rock assets already present, skipping append.")
        return
    if not os.path.exists(ROCK_GEN_BLEND):
        raise RuntimeError("Rock_Gen_2.0.blend not found at " + ROCK_GEN_BLEND)
    with bpy.data.libraries.load(ROCK_GEN_BLEND, link=False) as (src, dst):
        dst.node_groups = [n for n in src.node_groups if n in missing_groups]
        if need_material:
            dst.materials = [m for m in src.materials if m == ROCK_MATERIAL]
    print("[NW] Appended from Rock_Gen_2.0:", missing_groups,
          (["Rock material"] if need_material else []))


def build_rock_walls(g, wm, limit=None):
    """One joined mesh of solid overlapping boxes along every level drop.

    The voxel remesh unions the overlapping boxes into a single watertight
    wall band, which the displace passes then erode.
    """
    cs = wm["cellSize"]
    zh = wm["zh"]
    ox = wm["originX"]
    oy = wm["originY"]

    def wx(x):
        return ox + x * cs

    def wy(y):
        return oy + y * cs

    verts = []
    faces = []

    def add_box(base, along_u, out_u, u0, u1, w0, w1, t_out, t_back):
        def P(u, w, t):
            return (base[0] + along_u[0] * u + out_u[0] * t,
                    base[1] + along_u[1] * u + out_u[1] * t,
                    base[2] + w)

        b = len(verts)
        verts.extend([
            P(u0, w0, t_out), P(u1, w0, t_out), P(u1, w1, t_out), P(u0, w1, t_out),
            P(u0, w0, -t_back), P(u1, w0, -t_back), P(u1, w1, -t_back), P(u0, w1, -t_back),
        ])
        faces.extend([
            (b, b + 1, b + 2, b + 3),
            (b + 7, b + 6, b + 5, b + 4),
            (b + 4, b, b + 3, b + 7),
            (b + 1, b + 5, b + 6, b + 2),
            (b + 3, b + 2, b + 6, b + 7),
            (b + 4, b + 5, b + 1, b),
        ])

    sides = [
        ("L", -1, 0, (0.0, 1.0, 0.0), (-1.0, 0.0, 0.0)),
        ("R", 1, 0, (0.0, 1.0, 0.0), (1.0, 0.0, 0.0)),
        ("D", 0, -1, (1.0, 0.0, 0.0), (0.0, -1.0, 0.0)),
        ("U", 0, 1, (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)),
    ]

    edge_count = 0
    for y in range(g.H):
        for x in range(g.W):
            lv = g.lvl(x, y)
            if lv == 0:
                continue
            for name, dx, dy, along, out in sides:
                nb = g.lvl(x + dx, y + dy)
                if nb >= lv:
                    continue
                if limit is not None and edge_count >= limit:
                    break
                edge_count += 1
                zb = nb * zh
                zt = lv * zh
                drop = zt - zb
                if name == "L":
                    bx, by = wx(x), wy(y)
                elif name == "R":
                    bx, by = wx(x + 1), wy(y)
                elif name == "D":
                    bx, by = wx(x), wy(y)
                else:
                    bx, by = wx(x), wy(y + 1)
                add_box((bx, by, zb), along, out,
                        -BAND_OVERLAP, cs + BAND_OVERLAP,
                        -BAND_BELOW, drop + BAND_ABOVE,
                        BAND_OUT, BAND_BACK)

    old = bpy.data.objects.get("NW_BowlRockWalls")
    parent = old.parent if old else None
    if old:
        md = old.data
        bpy.data.objects.remove(old, do_unlink=True)
        if md and md.users == 0:
            bpy.data.meshes.remove(md, do_unlink=True)
    mesh = bpy.data.meshes.new("NW_BowlRockWallsMesh")
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    obj = bpy.data.objects.new("NW_BowlRockWalls", mesh)
    bpy.context.collection.objects.link(obj)
    if parent:
        obj.parent = parent
    print("[NW] Rock wall band built: edges", edge_count, "boxes", edge_count)
    return obj, edge_count


def set_gn_input(mod, ng, name, value):
    """Set a geometry-nodes modifier input by socket NAME."""
    for item in ng.interface.items_tree:
        if getattr(item, "item_type", "") == "SOCKET" and item.in_out == "INPUT" \
                and item.name == name:
            mod[item.identifier] = value
            return True
    raise RuntimeError("socket '%s' not found on %s" % (name, ng.name))


def apply_rock_stack(obj):
    """Rock_Gen-style coarse-to-fine erosion stack + shader attributes."""
    obj.modifiers.clear()
    displace_ng = bpy.data.node_groups.get("Displace")
    attribs_ng = bpy.data.node_groups.get("Shader Attribs")
    if displace_ng is None or attribs_ng is None:
        raise RuntimeError("Rock node groups missing; run append_rock_assets() first")

    for i, (voxel, params) in enumerate(ROCK_PASSES):
        rm = obj.modifiers.new("Remesh voxel pass %d" % (i + 1), "REMESH")
        rm.mode = "VOXEL"
        rm.voxel_size = voxel
        rm.use_smooth_shade = True
        dm = obj.modifiers.new("Rock displace pass %d" % (i + 1), "NODES")
        dm.node_group = displace_ng
        for k, v in params.items():
            set_gn_input(dm, displace_ng, k, v)

    am = obj.modifiers.new("Rock shader attribs", "NODES")
    am.node_group = attribs_ng
    print("[NW] Rock modifier stack applied:", len(obj.modifiers), "modifiers")


def evaluated_stats(obj):
    deps = bpy.context.evaluated_depsgraph_get()
    ev = obj.evaluated_get(deps)
    me = ev.to_mesh()
    counts = (len(me.vertices), len(me.polygons))
    ev.to_mesh_clear()
    return counts


def main(limit=None):
    print("[NW] file:", bpy.data.filepath)
    base = load_base()
    append_rock_assets()

    g, s = base["generate_bowl"]()
    zh = g.levelHeight * base["EXAG"]
    wm = {
        "cellSize": base["CELL_SIZE"],
        "exag": base["EXAG"],
        "zh": zh,
        "originX": -g.W * base["CELL_SIZE"] * 0.5,
        "originY": -g.H * base["CELL_SIZE"] * 0.5,
    }
    bpy.app.driver_namespace["nw_bowl"] = {
        "W": g.W, "H": g.H, "cell": g.cell, "zones": g.zones,
        "node": g.node, "levelHeight": g.levelHeight, "levelCount": g.levelCount,
        "cell_size": base["CELL_SIZE"],
    }
    bpy.app.driver_namespace["nw_bowl_wm"] = {
        "cellSize": base["CELL_SIZE"], "exag": base["EXAG"],
        "originX": wm["originX"], "originY": wm["originY"], "zh": zh,
    }

    surf_mats, used_grass = base["ensure_surface_materials"]()
    surface = base["build_surface"](g, wm, surf_mats)

    walls, edges = build_rock_walls(g, wm, limit=limit)
    apply_rock_stack(walls)

    base["assemble"](surface, walls)

    v, f = evaluated_stats(walls)
    print("[NW] Rocky bowl built. edges %s evaluated walls: %d verts %d faces" % (edges, v, f))
    print("    grass textures used:", used_grass)
    if not used_grass:
        print("    NOTE: download PolyHaven 'aerial_grass_rock' then re-run for grass.")
    return walls


if __name__ == "__main__":
    main()
