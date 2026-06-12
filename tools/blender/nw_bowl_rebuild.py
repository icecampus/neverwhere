"""Neverwhere bowl landscape - standalone Blender rebuild script.

Recreates the casual block-cliff bowl prototype from scratch inside Blender:
  - NW_BowlLandscape (empty parent)
      - NW_BowlSurface : terraced grass tops (zone-tinted), with horizontal grass
                          overhang lips over every cliff edge (no vertical skirts).
      - NW_BowlCliffs  : casual stacked-stone block cliffs facing every level drop,
                          with a solid dark backing apron + cleaned-up top row.
  - CliffSun           : sun lamp.

This is a faithful Python port of the C++ landscape_core::generateLandscapeBowl
(see src/libs/landscape_core/src/landscape_logic.cpp). Re-running is idempotent.

Usage: open Blender -> Scripting workspace -> open this file -> Run Script.
The grass texture is optional: if PolyHaven 'aerial_grass_rock' images are present
in the .blend they are used, otherwise surfaces fall back to flat zone colors.
"""

import bpy
import math
import random

# ---------------------------------------------------------------------------
# Settings (mirror of LandscapeBowlSettings defaults in landscape_logic.h)
# ---------------------------------------------------------------------------
SETTINGS = {
    "gridWidth": 32,
    "gridHeight": 24,
    "seed": 2027,
    "clearingRadius": 5.5,
    "clearingSoftness": 2.2,
    "highGroundRadius": 9.5,
    "highGroundWidth": 3.5,
    "highGroundHeight": 3.2,
    "heightLevels": 4,
    "arcNoiseScale": 4.0,
    "arcNoiseAmplitude": 1.6,
    "hillCount": 5,
    "hillHeight": 1.2,
    "hillRadius": 2.6,
}

# World mapping
CELL_SIZE = 1.0
EXAG = 1.4  # height exaggeration

# Zones
LOWLAND, CLEARING, SLOPE, HIGHGROUND, HILL = 0, 1, 2, 3, 4
ZONE_ORDER = [LOWLAND, CLEARING, SLOPE, HIGHGROUND, HILL]
ZONE_NAME = {LOWLAND: "Lowland", CLEARING: "Clearing", SLOPE: "Slope",
             HIGHGROUND: "HighGround", HILL: "Hill"}


# ---------------------------------------------------------------------------
# C++ helpers ported (deterministic)
# ---------------------------------------------------------------------------
def clampf(v, lo, hi):
    if hi < lo:
        return lo
    return max(lo, min(hi, v))


def clampi(v, lo, hi):
    if hi < lo:
        return lo
    return max(lo, min(hi, v))


def smoothstep(e0, e1, x):
    t = clampf((x - e0) / max(0.0001, e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def det01(seed, index):
    v = seed & 0xFFFFFFFF
    v ^= (index * 0x9e3779b9) & 0xFFFFFFFF
    v &= 0xFFFFFFFF
    v ^= v >> 16
    v = (v * 0x7feb352d) & 0xFFFFFFFF
    v ^= v >> 15
    v = (v * 0x846ca68b) & 0xFFFFFFFF
    v ^= v >> 16
    return float(v & 0x00ffffff) / float(0x01000000)


def value_noise(seed, x, y):
    a = math.sin(x * 12.9898 + y * 78.233 + seed * 0.137) * 43758.5453
    return (a - math.floor(a)) * 2.0 - 1.0


def blended_noise(seed, x, y, scale):
    s = max(0.001, scale)
    nx = x / s
    ny = y / s
    return (value_noise(seed, nx, ny) * 0.58
            + value_noise(seed + 31, nx * 2.0 + 7.1, ny * 2.0 - 3.7) * 0.28
            + value_noise(seed + 73, nx * 4.0 - 11.0, ny * 4.0 + 5.0) * 0.14)


def sanitize(s):
    s = dict(s)
    s["gridWidth"] = clampi(s["gridWidth"], 8, 96)
    s["gridHeight"] = clampi(s["gridHeight"], 8, 96)
    s["clearingRadius"] = clampf(s["clearingRadius"], 1.0, 30.0)
    s["clearingSoftness"] = clampf(s["clearingSoftness"], 0.1, 12.0)
    s["highGroundRadius"] = clampf(s["highGroundRadius"], 2.0, 40.0)
    s["highGroundWidth"] = clampf(s["highGroundWidth"], 0.5, 16.0)
    s["highGroundHeight"] = clampf(s["highGroundHeight"], 0.5, 16.0)
    s["heightLevels"] = clampi(s["heightLevels"], 2, 8)
    s["arcNoiseScale"] = clampf(s["arcNoiseScale"], 0.25, 32.0)
    s["arcNoiseAmplitude"] = clampf(s["arcNoiseAmplitude"], 0.0, 8.0)
    s["hillCount"] = clampi(s["hillCount"], 0, 24)
    s["hillHeight"] = clampf(s["hillHeight"], 0.0, 8.0)
    s["hillRadius"] = clampf(s["hillRadius"], 0.5, 12.0)
    return s


def level_for_sample(s, px, py):
    centerX = s["gridWidth"] * 0.5
    centerY = s["gridHeight"] * 0.58
    dx = px - centerX
    dy = py - centerY
    distance = math.sqrt(dx * dx + dy * dy)
    topHalfMask = smoothstep(0.0, s["highGroundRadius"] * 0.5,
                             -dy + s["clearingRadius"] * 0.2)
    arcNoise = blended_noise(s["seed"] + 101, px, py, s["arcNoiseScale"])
    distortedRadius = s["highGroundRadius"] + arcNoise * s["arcNoiseAmplitude"]
    ringDistance = abs(distance - distortedRadius)
    ringMask = 1.0 - smoothstep(s["highGroundWidth"] * 0.35, s["highGroundWidth"], ringDistance)
    highGroundMask = clampf(topHalfMask * ringMask, 0.0, 1.0)
    clearingMask = 1.0 - smoothstep(s["clearingRadius"],
                                    s["clearingRadius"] + s["clearingSoftness"], distance)

    hillContribution = 0.0
    for hill in range(s["hillCount"]):
        angleT = 0.12 + det01(s["seed"], hill * 7 + 1) * 0.76
        angle = math.pi * angleT
        radius = s["highGroundRadius"] + (det01(s["seed"], hill * 7 + 2) - 0.5) * s["highGroundWidth"]
        hillX = centerX + math.cos(angle) * radius
        hillY = centerY - math.sin(angle) * radius
        hdx = px - hillX
        hdy = py - hillY
        hdsq = hdx * hdx + hdy * hdy
        lhr = s["hillRadius"] * (0.75 + det01(s["seed"], hill * 7 + 3) * 0.65)
        local = math.exp(-hdsq / max(0.001, lhr * lhr))
        hillContribution += local * s["hillHeight"] * (0.7 + det01(s["seed"], hill * 7 + 4) * 0.6)

    maxLevel = s["heightLevels"] - 1
    level = 0
    if clearingMask <= 0.62:
        hillLevelBoost = clampf(hillContribution / max(0.001, s["hillHeight"]), 0.0, 1.5)
        levelScore = highGroundMask * maxLevel + hillLevelBoost
        level = clampi(int(round(levelScore)), 0, maxLevel)
        if highGroundMask > 0.18:
            level = max(level, 1)
        if highGroundMask > 0.55:
            level = max(level, min(maxLevel, 2))
        if highGroundMask > 0.82 or hillLevelBoost > 1.05:
            level = max(level, maxLevel)

    zone = LOWLAND
    if clearingMask > 0.7:
        zone = CLEARING
        level = 0
    elif hillContribution > s["hillHeight"] * 0.35 and level > 0:
        zone = HILL
    elif level >= max(1, maxLevel - 1):
        zone = HIGHGROUND
    elif level > 0:
        zone = SLOPE
    return level, zone


# ---------------------------------------------------------------------------
# Grid generation
# ---------------------------------------------------------------------------
class Grid:
    def __init__(self, W, H, levelCount, levelHeight):
        self.W = W
        self.H = H
        self.levelCount = levelCount
        self.levelHeight = levelHeight
        self.cell = [0] * (W * H)
        self.zones = [LOWLAND] * (W * H)
        self.node = [0] * ((W + 1) * (H + 1))

    def ci(self, x, y):
        return y * self.W + x

    def ni(self, x, y):
        return y * (self.W + 1) + x

    def lvl(self, x, y):
        if x < 0 or y < 0 or x >= self.W or y >= self.H:
            return 0
        return self.cell[self.ci(x, y)]


def compute_clearing_distances(g):
    UNREACH = 1000000
    dist = [UNREACH] * (g.W * g.H)
    from collections import deque
    q = deque()
    for y in range(g.H):
        for x in range(g.W):
            i = g.ci(x, y)
            if g.zones[i] == CLEARING:
                dist[i] = 0
                q.append(i)
    while q:
        idx = q.popleft()
        x = idx % g.W
        y = idx // g.W
        nd = dist[idx] + 1
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if dx == 0 and dy == 0:
                    continue
                nx, ny = x + dx, y + dy
                if nx < 0 or ny < 0 or nx >= g.W or ny >= g.H:
                    continue
                ni = g.ci(nx, ny)
                if nd >= dist[ni]:
                    continue
                dist[ni] = nd
                q.append(ni)
    return dist


def enforce_pyramid_level_spacing(g):
    maxLevel = max(0, g.levelCount - 1)
    cd = compute_clearing_distances(g)
    for y in range(g.H):
        for x in range(g.W):
            i = g.ci(x, y)
            if g.zones[i] == CLEARING:
                g.cell[i] = 0
                continue
            if cd[i] < 1000000:
                g.cell[i] = min(g.cell[i], cd[i])
    for level in range(maxLevel, 1, -1):
        for y in range(g.H):
            for x in range(g.W):
                if g.lvl(x, y) < level:
                    continue
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        if dx == 0 and dy == 0:
                            continue
                        nx, ny = x + dx, y + dy
                        if nx < 0 or ny < 0 or nx >= g.W or ny >= g.H:
                            continue
                        ni = g.ci(nx, ny)
                        if g.zones[ni] == CLEARING:
                            continue
                        target = level - 1
                        if cd[ni] < 1000000:
                            target = min(target, cd[ni])
                        if g.cell[ni] < target:
                            g.cell[ni] = clampi(target, 0, maxLevel)


def remove_thin_level_features(g):
    maxLevel = max(0, g.levelCount - 1)
    orthoX = (-1, 1, 0, 0)
    orthoY = (0, 0, -1, 1)
    for _ in range(8):
        changed = False
        nxt = list(g.cell)
        for y in range(g.H):
            for x in range(g.W):
                i = g.ci(x, y)
                if g.zones[i] == CLEARING:
                    continue
                level = g.cell[i]
                lower = higher = 0
                maxLower = 0
                minHigher = maxLevel
                for k in range(4):
                    nx, ny = x + orthoX[k], y + orthoY[k]
                    if nx < 0 or ny < 0 or nx >= g.W or ny >= g.H:
                        continue
                    nb = g.lvl(nx, ny)
                    if nb < level:
                        lower += 1
                        maxLower = max(maxLower, nb)
                    elif nb > level:
                        higher += 1
                        minHigher = min(minHigher, nb)
                if lower >= 3:
                    nxt[i] = clampi(maxLower, 0, maxLevel)
                    changed = True
                elif higher >= 3:
                    nxt[i] = clampi(minHigher, 0, maxLevel)
                    changed = True
        g.cell = nxt
        if not changed:
            break


def derive_node_levels(g):
    g.node = [0] * ((g.W + 1) * (g.H + 1))
    for y in range(g.H + 1):
        for x in range(g.W + 1):
            level = 0
            for cy in (y - 1, y):
                for cx in (x - 1, x):
                    if cx < 0 or cy < 0 or cx >= g.W or cy >= g.H:
                        continue
                    level = max(level, g.cell[g.ci(cx, cy)])
            g.node[g.ni(x, y)] = level


def generate_bowl():
    s = sanitize(SETTINGS)
    levelHeight = s["highGroundHeight"] / max(1, s["heightLevels"] - 1)
    g = Grid(s["gridWidth"], s["gridHeight"], s["heightLevels"], levelHeight)
    for y in range(g.H):
        for x in range(g.W):
            level, zone = level_for_sample(s, x + 0.5, y + 0.5)
            i = g.ci(x, y)
            g.cell[i] = level
            g.zones[i] = zone
    enforce_pyramid_level_spacing(g)
    remove_thin_level_features(g)
    derive_node_levels(g)
    return g, s


# ---------------------------------------------------------------------------
# Materials
# ---------------------------------------------------------------------------
ROCK_PALETTE = [
    ("NW_CasualRock_Dark", (0.18, 0.10, 0.05, 1.0)),
    ("NW_CasualRock_Brown", (0.33, 0.20, 0.10, 1.0)),
    ("NW_CasualRock_Ochre", (0.52, 0.34, 0.16, 1.0)),
    ("NW_CasualRock_Tan", (0.66, 0.48, 0.27, 1.0)),
    ("NW_CasualRock_Sand", (0.79, 0.63, 0.40, 1.0)),
]

ZONE_BASE_COLOR = {
    LOWLAND: (0.30, 0.34, 0.16, 1.0),
    CLEARING: (0.44, 0.47, 0.22, 1.0),
    SLOPE: (0.34, 0.36, 0.18, 1.0),
    HIGHGROUND: (0.30, 0.33, 0.15, 1.0),
    HILL: (0.27, 0.31, 0.14, 1.0),
}

# Grass HueSaturation tint per zone: (hue, saturation, value)
ZONE_GRASS_HSV = {
    CLEARING: (0.50, 1.00, 1.08),
    LOWLAND: (0.49, 1.05, 0.95),
    SLOPE: (0.50, 1.00, 0.90),
    HIGHGROUND: (0.50, 0.95, 0.82),
    HILL: (0.51, 0.90, 0.75),
}


def ensure_rock_materials():
    mats = []
    for name, col in ROCK_PALETTE:
        mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
        mat.use_nodes = True
        b = mat.node_tree.nodes.get("Principled BSDF")
        if b:
            b.inputs["Base Color"].default_value = col
            b.inputs["Roughness"].default_value = 0.92
        mats.append(mat)
    return mats


def find_grass_image(keyword):
    for img in bpy.data.images:
        n = img.name.lower()
        if "aerial_grass_rock" in n and keyword in n:
            return img
    return None


def build_grass_material(mat, hsv):
    """Build a grass material using object-coord mapping. Falls back gracefully."""
    diff = find_grass_image("diff") or find_grass_image("diffuse") or find_grass_image("col")
    rough = find_grass_image("rough")
    disp = find_grass_image("disp")

    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    out.location = (700, 0)
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.location = (400, 0)
    nt.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])

    if diff is None:
        # flat fallback
        zone_col = (0.36, 0.40, 0.20, 1.0)
        bsdf.inputs["Base Color"].default_value = zone_col
        bsdf.inputs["Roughness"].default_value = 0.95
        return False

    tex = nt.nodes.new("ShaderNodeTexCoord")
    tex.location = (-800, 0)
    mapping = nt.nodes.new("ShaderNodeMapping")
    mapping.location = (-600, 0)
    mapping.inputs["Scale"].default_value = (0.22, 0.22, 0.22)
    nt.links.new(tex.outputs["Object"], mapping.inputs["Vector"])

    img_d = nt.nodes.new("ShaderNodeTexImage")
    img_d.location = (-380, 200)
    img_d.image = diff
    nt.links.new(mapping.outputs["Vector"], img_d.inputs["Vector"])

    huesat = nt.nodes.new("ShaderNodeHueSaturation")
    huesat.location = (-140, 200)
    huesat.inputs["Hue"].default_value = hsv[0]
    huesat.inputs["Saturation"].default_value = hsv[1]
    huesat.inputs["Value"].default_value = hsv[2]
    nt.links.new(img_d.outputs["Color"], huesat.inputs["Color"])

    # large-scale color variation
    var = nt.nodes.new("ShaderNodeTexNoise")
    var.location = (-380, -120)
    var.inputs["Scale"].default_value = 1.6
    nt.links.new(tex.outputs["Object"], var.inputs["Vector"])
    mix = nt.nodes.new("ShaderNodeMixRGB")
    mix.location = (120, 120)
    mix.blend_type = "MULTIPLY"
    mix.inputs["Fac"].default_value = 0.18
    nt.links.new(huesat.outputs["Color"], mix.inputs["Color1"])
    nt.links.new(var.outputs["Color"], mix.inputs["Color2"])
    nt.links.new(mix.outputs["Color"], bsdf.inputs["Base Color"])

    if rough is not None:
        rough.colorspace_settings.name = "Non-Color"
        img_r = nt.nodes.new("ShaderNodeTexImage")
        img_r.location = (-380, -360)
        img_r.image = rough
        nt.links.new(mapping.outputs["Vector"], img_r.inputs["Vector"])
        nt.links.new(img_r.outputs["Color"], bsdf.inputs["Roughness"])
    else:
        bsdf.inputs["Roughness"].default_value = 0.95

    if disp is not None:
        disp.colorspace_settings.name = "Non-Color"
        img_b = nt.nodes.new("ShaderNodeTexImage")
        img_b.location = (-380, -600)
        img_b.image = disp
        nt.links.new(mapping.outputs["Vector"], img_b.inputs["Vector"])
        bump = nt.nodes.new("ShaderNodeBump")
        bump.location = (120, -200)
        bump.inputs["Strength"].default_value = 0.25
        nt.links.new(img_b.outputs["Color"], bump.inputs["Height"])
        nt.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
    return True


def ensure_surface_materials():
    mats = []
    used_grass = False
    for z in ZONE_ORDER:
        name = "NW_BowlSurface_" + ZONE_NAME[z]
        mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
        hsv = ZONE_GRASS_HSV.get(z, (0.5, 1.0, 1.0))
        ok = build_grass_material(mat, hsv)
        if not ok:
            # ensure flat base color reflects zone
            b = mat.node_tree.nodes.get("Principled BSDF")
            if b:
                b.inputs["Base Color"].default_value = ZONE_BASE_COLOR[z]
        else:
            used_grass = True
        mats.append(mat)
    return mats, used_grass


# ---------------------------------------------------------------------------
# Geometry: surface (caps + horizontal grass lips, no vertical skirts)
# ---------------------------------------------------------------------------
OVER = 0.18  # horizontal grass overhang past cell edge over the wall top


def build_surface(g, wm, surf_mats):
    cs = wm["cellSize"]
    zh = wm["zh"]
    ox = wm["originX"]
    oy = wm["originY"]
    zone_slot = {z: i for i, z in enumerate(ZONE_ORDER)}

    def wx(x):
        return ox + x * cs

    def wy(y):
        return oy + y * cs

    verts = []
    faces = []
    fmats = []

    def add_quad(p0, p1, p2, p3, slot):
        b = len(verts)
        verts.extend([p0, p1, p2, p3])
        faces.append((b, b + 1, b + 2, b + 3))
        fmats.append(slot)

    sides = [("L", -1, 0), ("R", 1, 0), ("D", 0, -1), ("U", 0, 1)]
    for y in range(g.H):
        for x in range(g.W):
            i = g.ci(x, y)
            lv = g.cell[i]
            slot = zone_slot[g.zones[i]]
            z = lv * zh
            x0, x1 = wx(x), wx(x + 1)
            y0, y1 = wy(y), wy(y + 1)
            add_quad((x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z), slot)
            if lv == 0:
                continue
            for name, dx, dy in sides:
                if g.lvl(x + dx, y + dy) >= lv:
                    continue
                if name == "L":
                    lx = x0
                    add_quad((lx - OVER, y0, z), (lx, y0, z), (lx, y1, z), (lx - OVER, y1, z), slot)
                elif name == "R":
                    lx = x1
                    add_quad((lx, y0, z), (lx + OVER, y0, z), (lx + OVER, y1, z), (lx, y1, z), slot)
                elif name == "D":
                    ly = y0
                    add_quad((x0, ly - OVER, z), (x1, ly - OVER, z), (x1, ly, z), (x0, ly, z), slot)
                else:
                    ly = y1
                    add_quad((x0, ly, z), (x1, ly, z), (x1, ly + OVER, z), (x0, ly + OVER, z), slot)

    old = bpy.data.objects.get("NW_BowlSurface")
    parent = old.parent if old else None
    if old:
        md = old.data
        bpy.data.objects.remove(old, do_unlink=True)
        if md and md.users == 0:
            bpy.data.meshes.remove(md, do_unlink=True)
    mesh = bpy.data.meshes.new("NW_BowlSurfaceMesh")
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    for m in surf_mats:
        mesh.materials.append(m)
    for poly, mi in zip(mesh.polygons, fmats):
        poly.material_index = mi
    obj = bpy.data.objects.new("NW_BowlSurface", mesh)
    bpy.context.collection.objects.link(obj)
    if parent:
        obj.parent = parent
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.shade_flat()
    obj.select_set(False)
    return obj


# ---------------------------------------------------------------------------
# Geometry: cliffs (apron + clean tops + jittered stone blocks + boulders)
# ---------------------------------------------------------------------------
SIDE_SEED = {"L": 11, "R": 23, "D": 41, "U": 67}


def build_cliffs(g, wm, rock_mats):
    cs = wm["cellSize"]
    zh = wm["zh"]
    ox = wm["originX"]
    oy = wm["originY"]
    zMax = max(1e-5, (g.levelCount - 1) * zh)

    def wx(x):
        return ox + x * cs

    def wy(y):
        return oy + y * cs

    verts = []
    faces = []
    fmats = []

    def band_for_z(z, extra=0.0):
        t = z / zMax
        return max(0, min(4, int(round(t * 4 + extra))))

    def add_quad(p0, p1, p2, p3, mi):
        b = len(verts)
        verts.extend([p0, p1, p2, p3])
        faces.append((b, b + 1, b + 2, b + 3))
        fmats.append(mi)

    def add_rock_basis(base, along_u, up_u, out_u, u0, u1, w0, w1, front, back,
                       rough, splay, mi, top_clean=False):
        def J(a):
            return random.uniform(-a, a)

        def P(u, w, t, top=False):
            jt = J(rough * 0.8)
            if top and top_clean:
                ju = J(rough * 0.45)
                jw = J(rough * 0.35)
                sp = splay * 0.3
            else:
                ju = J(rough)
                jw = J(rough * (1.3 if top else 0.9))
                sp = (splay if top else 0.0)
            uu = u + ju + sp
            ww = w + jw
            tt = t + jt
            return (base[0] + along_u[0] * uu + up_u[0] * ww + out_u[0] * tt,
                    base[1] + along_u[1] * uu + up_u[1] * ww + out_u[1] * tt,
                    base[2] + along_u[2] * uu + up_u[2] * ww + out_u[2] * tt)

        b = len(verts)
        f0 = P(u0, w0, front)
        f1 = P(u1, w0, front)
        f2 = P(u1, w1, front, True)
        f3 = P(u0, w1, front, True)
        b0 = P(u0, w0, -back)
        b1 = P(u1, w0, -back)
        b2 = P(u1, w1, -back, True)
        b3 = P(u0, w1, -back, True)
        verts.extend([f0, f1, f2, f3, b0, b1, b2, b3])
        fs = [(b, b + 1, b + 2, b + 3), (b + 5, b + 4, b + 7, b + 6),
              (b + 4, b, b + 3, b + 7), (b + 1, b + 5, b + 6, b + 2),
              (b + 3, b + 2, b + 6, b + 7), (b + 4, b + 5, b + 1, b)]
        faces.extend(fs)
        dark = max(0, mi - 1)
        fmats.extend([mi, dark, dark, dark, mi, dark])

    sides = [
        ("L", -1, 0, (0.0, 1.0, 0.0), (-1.0, 0.0, 0.0)),
        ("R", 1, 0, (0.0, 1.0, 0.0), (1.0, 0.0, 0.0)),
        ("D", 0, -1, (1.0, 0.0, 0.0), (0.0, -1.0, 0.0)),
        ("U", 0, 1, (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)),
    ]

    edge_count = 0
    boulder_count = 0
    for y in range(g.H):
        for x in range(g.W):
            lv = g.lvl(x, y)
            if lv == 0:
                continue
            for name, dx, dy, along, out in sides:
                nb = g.lvl(x + dx, y + dy)
                if nb >= lv:
                    continue
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
                base0 = (bx, by, zb)
                along_u = along
                up_u = (0.0, 0.0, 1.0)
                out_u = out

                # SOLID APRON behind blocks (kills see-through gaps)
                def AP(u, w, t):
                    return (base0[0] + along_u[0] * u + out_u[0] * t,
                            base0[1] + along_u[1] * u + out_u[1] * t,
                            base0[2] + w)

                ap_t = -0.04
                add_quad(AP(0.0, -0.12, ap_t), AP(cs, -0.12, ap_t),
                         AP(cs, drop + 0.06, ap_t), AP(0.0, drop + 0.06, ap_t), 0)

                seed = (x * 73856093) ^ (y * 19349663) ^ (SIDE_SEED[name] * 83492791) ^ 0x5bd1e995
                random.seed(seed & 0x7fffffff)
                cols = 2
                rows = max(1, int(round(drop / 0.7)))
                for iz in range(rows):
                    w0 = (iz / rows) * drop
                    w1 = ((iz + 1) / rows) * drop
                    is_top = (iz == rows - 1)
                    gap_w = random.uniform(0.03, 0.07)
                    ww0 = w0 + gap_w * 0.5
                    ww1 = (drop + 0.06) if is_top else (w1 - gap_w * 0.5)
                    edges_u = [0.0, cs * 0.5 + random.uniform(-0.10, 0.10), cs]
                    for icur in range(cols):
                        u0 = edges_u[icur]
                        u1 = edges_u[icur + 1]
                        gap_u = random.uniform(0.03, 0.08)
                        uu0 = u0 + gap_u * 0.5
                        uu1 = u1 - gap_u * 0.5
                        if uu1 - uu0 < 0.18 or ww1 - ww0 < 0.12:
                            continue
                        zc = zb + 0.5 * (ww0 + ww1)
                        mi = band_for_z(zc, random.choice([-1, 0, 0, 0, 1]) * 0.5)
                        front = random.uniform(0.10, 0.28)
                        back = random.uniform(0.35, 0.6)
                        rough = min(0.14, 0.05 + (uu1 - uu0) * 0.05)
                        add_rock_basis(base0, along_u, up_u, out_u, uu0, uu1, ww0, ww1,
                                       front, back, rough, random.uniform(-0.05, 0.05),
                                       mi, top_clean=is_top)
                if drop >= zh * 0.9 and random.random() < 0.10:
                    boulder_count += 1
                    bw = random.uniform(0.35, 0.7) * cs
                    u0 = random.uniform(0.1, cs - bw - 0.1)
                    w0 = random.uniform(0.15 * drop, 0.5 * drop)
                    bh = random.uniform(0.25, 0.45) * drop
                    zc = zb + w0 + bh * 0.5
                    add_rock_basis(base0, along_u, up_u, out_u, u0, u0 + bw, w0, w0 + bh,
                                   random.uniform(0.45, 0.75), 0.3, 0.20,
                                   random.uniform(-0.15, 0.15), band_for_z(zc))

    old = bpy.data.objects.get("NW_BowlCliffs")
    parent = old.parent if old else None
    if old:
        md = old.data
        bpy.data.objects.remove(old, do_unlink=True)
        if md and md.users == 0:
            bpy.data.meshes.remove(md, do_unlink=True)
    mesh = bpy.data.meshes.new("NW_BowlCliffsMesh")
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    for m in rock_mats:
        mesh.materials.append(m)
    for poly, mi in zip(mesh.polygons, fmats):
        poly.material_index = mi
    obj = bpy.data.objects.new("NW_BowlCliffs", mesh)
    bpy.context.collection.objects.link(obj)
    if parent:
        obj.parent = parent
    bev = obj.modifiers.new("Chunky edge bevels", "BEVEL")
    bev.width = 0.04
    bev.segments = 1
    bev.affect = "EDGES"
    try:
        bev.harden_normals = True
    except Exception:
        pass
    wn = obj.modifiers.new("Weighted block normals", "WEIGHTED_NORMAL")
    try:
        wn.keep_sharp = True
    except Exception:
        pass
    return obj, edge_count, boulder_count


# ---------------------------------------------------------------------------
# Assembly
# ---------------------------------------------------------------------------
def assemble(surface, cliffs):
    parent = bpy.data.objects.get("NW_BowlLandscape")
    if parent is None:
        parent = bpy.data.objects.new("NW_BowlLandscape", None)
        parent.empty_display_type = "PLAIN_AXES"
        bpy.context.collection.objects.link(parent)
    surface.parent = parent
    cliffs.parent = parent

    sun = bpy.data.objects.get("CliffSun")
    if sun is None:
        sun_data = bpy.data.lights.new("CliffSun", "SUN")
        sun = bpy.data.objects.new("CliffSun", sun_data)
        bpy.context.collection.objects.link(sun)
    sun.data.energy = 2.4
    sun.data.angle = math.radians(3.0)
    sun.rotation_euler = (math.radians(52), math.radians(8), math.radians(-46))
    return parent


def main():
    g, s = generate_bowl()
    zh = g.levelHeight * EXAG
    wm = {
        "cellSize": CELL_SIZE,
        "exag": EXAG,
        "zh": zh,
        "originX": -g.W * CELL_SIZE * 0.5,
        "originY": -g.H * CELL_SIZE * 0.5,
    }
    # persist for incremental edits, matching previous sessions
    bpy.app.driver_namespace["nw_bowl"] = {
        "W": g.W, "H": g.H, "cell": g.cell, "zones": g.zones,
        "node": g.node, "levelHeight": g.levelHeight, "levelCount": g.levelCount,
        "cell_size": CELL_SIZE,
    }
    bpy.app.driver_namespace["nw_bowl_wm"] = {
        "cellSize": CELL_SIZE, "exag": EXAG, "originX": wm["originX"],
        "originY": wm["originY"], "zh": zh,
    }

    rock_mats = ensure_rock_materials()
    surf_mats, used_grass = ensure_surface_materials()

    surface = build_surface(g, wm, surf_mats)
    cliffs, edges, boulders = build_cliffs(g, wm, rock_mats)
    assemble(surface, cliffs)

    print("[NW] Bowl landscape rebuilt.")
    print("    grid {}x{} levels {} zh={:.3f}".format(g.W, g.H, g.levelCount, zh))
    print("    cliff edges {} boulders {}".format(edges, boulders))
    print("    grass textures used: {}".format(used_grass))
    if not used_grass:
        print("    NOTE: PolyHaven 'aerial_grass_rock' images not found; "
              "surfaces use flat zone colors. Download the texture, then re-run.")


if __name__ == "__main__":
    main()
