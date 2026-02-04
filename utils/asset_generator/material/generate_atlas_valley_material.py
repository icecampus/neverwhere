import argparse
import math
import os
import random

from PIL import Image

from atlas_material import (
    MaterialStyle,
    apply_alpha,
    apply_edge_noise,
    edge_band_mask,
    load_rgba,
    overlay_solid,
    paste_with_mask,
    polygon_mask,
    scatter_decals,
    split_sheet,
    tile_pattern,
)

# Shared helper (no package install required)
import sys
_SHARED = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "_shared"))
if _SHARED not in sys.path:
    sys.path.append(_SHARED)
from publish_utils import ensure_dir, now_run_id, publish_atlas_and_thumbnail  # noqa: E402


def normalize(v):
    l = math.sqrt(v[0] ** 2 + v[1] ** 2 + v[2] ** 2)
    if l == 0:
        return (0, 0, 1)
    return (v[0] / l, v[1] / l, v[2] / l)


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def triangle_brightness(p1, p2, p3, light_dir):
    v1 = sub(p2, p1)
    v2 = sub(p3, p1)
    n = cross(v1, v2)
    if n[2] < 0:
        n = (-n[0], -n[1], -n[2])
    n = normalize(n)
    intensity = dot(n, light_dir)
    bright = 0.3 + 1.4 * intensity
    bright = max(0.2, min(2.5, bright))
    return bright


def generate_png_atlas_material(output_path: str, style: MaterialStyle, repo_root: str) -> None:
    COLS, ROWS = 4, 6
    TILE_W = 256
    TILE_H = 220
    WIDTH, HEIGHT = COLS * TILE_W, ROWS * TILE_H
    ELEVATION = 26

    style.resolve_paths(repo_root)
    surface_tex = load_rgba(style.surface_albedo)
    side_soil_tex = load_rgba(style.side_soil_albedo)
    side_rock_tex = load_rgba(style.side_rock_albedo) if style.side_rock_albedo else None
    decals_sheet = load_rgba(style.edge_decals_sheet) if style.edge_decals_sheet else None
    decals = split_sheet(decals_sheet, style.edge_decals_cols, style.edge_decals_rows) if decals_sheet else []

    img = Image.new("RGBA", (WIDTH, HEIGHT), (0, 0, 0, 0))

    masks = {
        "Full": [1, 1, 1, 1, 1, 1, 1, 1, 1],
        "DownLack": [1, 1, 1, 1, 1, 0.5, 0, 0.5, 1],
        "LeftLack": [0, 0.5, 1, 1, 1, 1, 1, 0.5, 1],
        "UpLack": [1, 0.5, 0, 0.5, 1, 1, 1, 1, 1],
        "RightLack": [1, 1, 1, 0.5, 0, 0.5, 1, 1, 1],
        "UpCorner": [0, 0.5, 1, 0.5, 0, 0, 0, 0, 0],
        "RightCorner": [0, 0, 0, 0.5, 1, 0.5, 0, 0, 0],
        "DownCorner": [0, 0, 0, 0, 0, 0.5, 1, 0.5, 0],
        "LeftCorner": [1, 0.5, 0, 0, 0, 0, 0, 0.5, 0],
        "RightUpLine": [0, 0.5, 1, 1, 1, 0.5, 0, 0, 0.5],
        "RightDownLine": [0, 0, 0, 0.5, 1, 1, 1, 0.5, 0.5],
        "LeftDownLine": [1, 0.5, 0, 0, 0, 0.5, 1, 1, 0.5],
        "LeftUpLine": [1, 1, 1, 0.5, 0, 0, 0, 0.5, 0.5],
        "UpAndDownCorners": [0, 0.5, 1, 0.5, 0, 0.5, 1, 0.5, 1],
        "LeftRightCorners": [1, 0.5, 0, 0.5, 1, 0.5, 0, 0.5, 1],
        "Unknown": [0, 0, 0, 0, 0, 0, 0, 0, 0],
    }

    index_to_type = {i: "Full" for i in range(4)}
    index_to_type.update(
        {
            4: "DownLack",
            5: "LeftLack",
            6: "UpLack",
            7: "RightLack",
            8: "UpCorner",
            9: "RightCorner",
            10: "DownCorner",
            11: "LeftCorner",
            12: "RightUpLine",
            13: "RightDownLine",
            14: "LeftDownLine",
            15: "LeftUpLine",
            16: "RightUpLine",
            17: "RightDownLine",
            18: "LeftDownLine",
            19: "LeftUpLine",
            20: "UpAndDownCorners",
            21: "LeftRightCorners",
            22: "Unknown",
            23: "Unknown",
        }
    )

    coords_2d_logical = [(-1, 0), (0, 1), (1, 0), (0, -1), (0, 0)]
    Z_SCALE = 0.3
    light_dir = normalize((-1, 1, 1))

    pts_base = [(0, 110), (128, 46), (256, 110), (128, 174)]
    center_x = (pts_base[0][0] + pts_base[2][0]) / 2
    center_y = (pts_base[1][1] + pts_base[3][1]) / 2
    pt_center_base = (center_x, center_y)
    pts_base_all = pts_base + [pt_center_base]

    for i in range(COLS * ROWS):
        col, row = i % COLS, i // COLS
        x, y = col * TILE_W, row * TILE_H

        t_type = index_to_type.get(i, "Unknown")
        m = masks.get(t_type, masks["Unknown"])
        corner_heights = [m[0], m[2], m[4], m[6]]
        center_height = m[8]
        heights = corner_heights + [center_height]

        pts_surface = []
        for k in range(5):
            h = heights[k]
            px = pts_base_all[k][0]
            py = pts_base_all[k][1] - ELEVATION * h
            pts_surface.append((px, py))

        tile_walls = Image.new("RGBA", (TILE_W, TILE_H), (0, 0, 0, 0))

        rng = random.Random(style.seed + i * 10007)
        if style.surface_uv_jitter_px > 0:
            j = style.surface_uv_jitter_px
            surface_offset = (rng.randrange(-j, j + 1), rng.randrange(-j, j + 1))
        else:
            surface_offset = (i * 7, i * 11)
        tile_surface = tile_pattern(surface_tex, (TILE_W, TILE_H), offset=surface_offset)

        if style.side_uv_jitter_px > 0:
            j = style.side_uv_jitter_px
            side_offset = (rng.randrange(-j, j + 1), rng.randrange(-j, j + 1))
        else:
            side_offset = (0, 0)
        side_soil_pat = tile_pattern(side_soil_tex, (TILE_W, TILE_H), offset=side_offset)
        side_rock_pat = tile_pattern(side_rock_tex, (TILE_W, TILE_H), offset=side_offset) if side_rock_tex else None

        for j in [1, 0, 2, 3]:
            nj = (j + 1) % 4
            h_curr = heights[j]
            h_next = heights[nj]
            if h_curr <= 0 and h_next <= 0:
                continue

            wall_poly = [pts_base[j], pts_base[nj], pts_surface[nj], pts_surface[j]]
            wall_mask = polygon_mask((TILE_W, TILE_H), wall_poly)

            use_rock = False
            if style.side_mode == "rock":
                use_rock = side_rock_pat is not None
            elif style.side_mode == "mixed" and side_rock_pat is not None:
                use_rock = (h_curr == 0 and h_next > 0) or (h_next == 0 and h_curr > 0)

            pat = side_rock_pat if use_rock and side_rock_pat is not None else side_soil_pat
            paste_with_mask(tile_walls, pat, wall_mask)
            shade_alpha = 70 if j < 2 else 35
            overlay_solid(tile_walls, (0, 0, 0, shade_alpha), wall_mask)

        pts_3d = []
        for k in range(5):
            z = Z_SCALE * heights[k]
            pts_3d.append((coords_2d_logical[k][0], coords_2d_logical[k][1], z))

        if t_type in ["UpAndDownCorners", "LeftRightCorners"]:
            tris = [(4, 0, 1), (4, 1, 2), (4, 2, 3), (4, 3, 0)]
        elif t_type in ["UpCorner", "DownCorner", "LeftCorner", "RightCorner"]:
            tris = [(4, 0, 1), (4, 1, 2), (4, 2, 3), (4, 3, 0)]
        elif t_type == "DownLack" or t_type == "UpLack":
            tris = [(0, 1, 2), (0, 2, 3)]
        elif t_type == "LeftLack" or t_type == "RightLack":
            tris = [(0, 1, 3), (1, 2, 3)]
        else:
            tris = [(0, 1, 3), (1, 2, 3)]

        for (i1, i2, i3) in tris:
            if heights[i1] == 0 and heights[i2] == 0 and heights[i3] == 0:
                continue
            tri_poly = [pts_surface[i1], pts_surface[i2], pts_surface[i3]]
            tri_mask = polygon_mask((TILE_W, TILE_H), tri_poly)
            bright = triangle_brightness(pts_3d[i1], pts_3d[i2], pts_3d[i3], light_dir)
            if bright >= 1.0:
                a = int(max(0.0, min(120.0, (bright - 1.0) * 90.0)))
                overlay_solid(tile_surface, (255, 255, 255, a), tri_mask)
            else:
                a = int(max(0.0, min(160.0, (1.0 - bright) * 140.0)))
                overlay_solid(tile_surface, (0, 0, 0, a), tri_mask)

        hard = polygon_mask((TILE_W, TILE_H), [pts_surface[0], pts_surface[1], pts_surface[2], pts_surface[3]])
        soft, band = edge_band_mask(hard, style.edge_falloff_px)
        alpha = apply_edge_noise(soft, band, style.edge_noise_amplitude_px, style.edge_noise_scale, seed=style.seed + i * 17)
        tile_surface = apply_alpha(tile_surface, alpha)

        scatter_decals(
            tile_surface,
            decals,
            band,
            rng=rng,
            count=style.decals_per_tile,
            scale_min=style.decals_scale_min,
            scale_max=style.decals_scale_max,
        )

        tile = tile_walls
        tile.alpha_composite(tile_surface)
        img.alpha_composite(tile, (x, y))

    img.save(output_path)
    print(f"Material atlas (Valley) saved to {output_path}")


def main() -> int:
    ap = argparse.ArgumentParser(description="Generate material atlas (Valley variant). Writes temp output and publishes to resources.")
    ap.add_argument("output", nargs="?", default="", help="Optional explicit output PNG path. If omitted, writes to _intermediate_64/_asset_generator.")
    ap.add_argument("--config", default=os.path.join("utils", "asset_generator", "material", "material_style_example.json"), help="Path to JSON style config.")
    ap.add_argument("--repo-root", default=".", help="Repo root for resolving relative paths in config.")
    ap.add_argument("--materials-dir", default="", help="If set, overrides texture inputs (expects grass_albedo.png, soil_side.png, rock_side.png, edge_decals.png).")
    ap.add_argument("--run-id", default="", help="Run id for temp output folder. If omitted, auto timestamp is used.")
    ap.add_argument("--temp-root", default="_intermediate_64/_asset_generator", help="Temp root (ignored by git).")
    ap.add_argument("--thumb-width", type=int, default=256, help="Thumbnail width in px (tile0, keep aspect).")
    ap.add_argument("--no-publish", action="store_true", help="Do not publish into resources (temp only).")
    args = ap.parse_args()

    style = MaterialStyle.from_json(args.config)

    if args.materials_dir:
        md = args.materials_dir
        style.surface_albedo = os.path.join(md, "grass_albedo.png")
        style.side_soil_albedo = os.path.join(md, "soil_side.png")
        rock = os.path.join(md, "rock_side.png")
        decals = os.path.join(md, "edge_decals.png")
        style.side_rock_albedo = rock if os.path.exists(rock) else None
        style.edge_decals_sheet = decals if os.path.exists(decals) else None

    run_id = args.run_id or now_run_id()
    if args.output:
        out_path = args.output
    else:
        out_dir = os.path.join(args.temp_root, "material", "valley", run_id)
        ensure_dir(out_dir)
        out_path = os.path.join(out_dir, "atlas.png")

    generate_png_atlas_material(out_path, style, repo_root=args.repo_root)

    if not args.no_publish:
        publish_atlas_and_thumbnail(
            atlas_src_path=out_path,
            pack_dir=os.path.join("resources", "assets", "landscape", "MaterialGrassValley"),
            cols=4,
            rows=6,
            thumb_width=args.thumb_width,
            ensure_index=True,
        )
        print("Published to resources/assets/landscape/MaterialGrassValley (atlas.png + thumbnail.png + index.json)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

