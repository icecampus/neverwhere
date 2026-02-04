import argparse
import math
import os
import random

from PIL import Image, ImageChops, ImageDraw, ImageFilter


def _clamp(x: float, lo: float, hi: float) -> float:
    return lo if x < lo else hi if x > hi else x


def _lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def _smoothstep(t: float) -> float:
    t = _clamp(t, 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def _hash2(x: int, y: int, seed: int) -> int:
    n = x * 374761393 + y * 668265263 + seed * 1442695040888963407
    n = (n ^ (n >> 13)) * 1274126177
    return n ^ (n >> 16)


def tileable_value_noise(size: int, freq: int, seed: int) -> Image.Image:
    grid = [[(_hash2(ix, iy, seed) & 0xFFFF) / 65535.0 for ix in range(freq)] for iy in range(freq)]
    out = Image.new("L", (size, size), 0)
    px = out.load()

    for y in range(size):
        fy = (y / size) * freq
        y0 = int(math.floor(fy)) % freq
        y1 = (y0 + 1) % freq
        ty = fy - math.floor(fy)
        sy = _smoothstep(ty)

        for x in range(size):
            fx = (x / size) * freq
            x0 = int(math.floor(fx)) % freq
            x1 = (x0 + 1) % freq
            tx = fx - math.floor(fx)
            sx = _smoothstep(tx)

            v00 = grid[y0][x0]
            v10 = grid[y0][x1]
            v01 = grid[y1][x0]
            v11 = grid[y1][x1]
            v0 = _lerp(v00, v10, sx)
            v1 = _lerp(v01, v11, sx)
            v = _lerp(v0, v1, sy)
            px[x, y] = int(_clamp(v, 0.0, 1.0) * 255)

    return out


def tileable_fbm(size: int, seed: int, base_freq: int = 8, octaves: int = 5) -> Image.Image:
    acc = Image.new("L", (size, size), 0)
    amp = 1.0
    amp_sum = 0.0
    freq = base_freq
    for o in range(octaves):
        n = tileable_value_noise(size, freq=freq, seed=seed + 1013 * o)
        n = n.point(lambda p, a=amp: int(p * a))
        acc = ImageChops.add(acc, n)
        amp_sum += amp
        amp *= 0.5
        freq *= 2

    if amp_sum > 0:
        acc = acc.point(lambda p, s=amp_sum: int(_clamp(p / s, 0, 255)))
    return acc


def make_grass_albedo(size: int, seed: int) -> Image.Image:
    rng = random.Random(seed)
    base = Image.new("RGBA", (size, size), (60, 170, 85, 255))
    noise = tileable_fbm(size, seed=seed, base_freq=6, octaves=5).filter(ImageFilter.GaussianBlur(radius=0.6))
    tint = Image.new("RGBA", (size, size), (20, 50, 20, 255))
    tint.putalpha(noise.point(lambda p: int(p * 0.45)))
    base = Image.alpha_composite(base, tint)

    draw = ImageDraw.Draw(base)
    for _ in range(int(size * size * 0.00012)):
        x = rng.randrange(size)
        y = rng.randrange(size)
        c = (rng.randrange(120, 180), rng.randrange(200, 240), rng.randrange(120, 180), 160)
        draw.ellipse((x, y, x + 1, y + 1), fill=c)
    return base


def make_dirt_albedo(size: int, seed: int) -> Image.Image:
    rng = random.Random(seed)
    base = Image.new("RGBA", (size, size), (150, 115, 80, 255))
    noise = tileable_fbm(size, seed=seed, base_freq=7, octaves=5).filter(ImageFilter.GaussianBlur(radius=0.8))
    shade = Image.new("RGBA", (size, size), (60, 40, 20, 255))
    shade.putalpha(noise.point(lambda p: int(p * 0.50)))
    base = Image.alpha_composite(base, shade)

    draw = ImageDraw.Draw(base)
    for _ in range(int(size * size * 0.00008)):
        x = rng.randrange(size)
        y = rng.randrange(size)
        r = rng.choice([1, 1, 2])
        g = rng.randrange(90, 150)
        c = (g, g, g, 190)
        draw.ellipse((x - r, y - r, x + r, y + r), fill=c)
    return base


def make_soil_side(size: int, seed: int) -> Image.Image:
    base = Image.new("RGBA", (size, size), (120, 90, 60, 255))
    noise = tileable_fbm(size, seed=seed, base_freq=8, octaves=4).filter(ImageFilter.GaussianBlur(radius=0.9))

    grad = Image.new("L", (1, size), 0)
    for y in range(size):
        t = y / max(1, size - 1)
        grad.putpixel((0, y), int(_lerp(15, 90, t)))
    grad = grad.resize((size, size))

    shade = Image.new("RGBA", (size, size), (40, 25, 15, 255))
    shade.putalpha(grad)
    out = Image.alpha_composite(base, shade)

    bands = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(bands)
    for y in range(0, size, 24):
        alpha = 30 + (y % 48) * 0.4
        d.rectangle((0, y, size, y + 2), fill=(50, 35, 20, int(alpha)))
    out = Image.alpha_composite(out, bands)

    nshade = Image.new("RGBA", (size, size), (30, 20, 10, 255))
    nshade.putalpha(noise.point(lambda p: int(p * 0.40)))
    out = Image.alpha_composite(out, nshade)
    return out


def make_rock_side(size: int, seed: int) -> Image.Image:
    rng = random.Random(seed)
    base = Image.new("RGBA", (size, size), (120, 125, 135, 255))
    noise = tileable_fbm(size, seed=seed, base_freq=10, octaves=5).filter(ImageFilter.GaussianBlur(radius=0.7))
    shade = Image.new("RGBA", (size, size), (30, 30, 35, 255))
    shade.putalpha(noise.point(lambda p: int(p * 0.65)))
    out = Image.alpha_composite(base, shade)

    cracks = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(cracks)
    for _ in range(120):
        x = rng.randrange(size)
        y = rng.randrange(size)
        x2 = x + rng.randrange(-60, 60)
        y2 = y + rng.randrange(-30, 30)
        d.line((x, y, x2, y2), fill=(30, 30, 30, 25), width=1)
    out = Image.alpha_composite(out, cracks)

    moss = Image.new("RGBA", (size, size), (60, 120, 70, 255))
    moss_mask = tileable_value_noise(size, freq=18, seed=seed + 999)
    moss.putalpha(moss_mask.point(lambda p: 0 if p < 210 else int((p - 210) * 1.2)))
    out = Image.alpha_composite(out, moss)
    return out


def make_edge_decals_sheet(size: int, seed: int, grid: int = 4) -> Image.Image:
    rng = random.Random(seed)
    sheet = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    cell = size // grid
    for gy in range(grid):
        for gx in range(grid):
            cx = gx * cell
            cy = gy * cell
            img = Image.new("RGBA", (cell, cell), (0, 0, 0, 0))
            d = ImageDraw.Draw(img)

            kind = (gy * grid + gx) % 3
            if kind == 0:
                for _ in range(rng.randrange(10, 20)):
                    x0 = rng.randrange(int(cell * 0.2), int(cell * 0.8))
                    y0 = rng.randrange(int(cell * 0.55), int(cell * 0.85))
                    x1 = x0 + rng.randrange(-20, 20)
                    y1 = y0 - rng.randrange(20, 60)
                    col = (rng.randrange(40, 80), rng.randrange(140, 190), rng.randrange(50, 90), rng.randrange(160, 230))
                    d.line((x0, y0, x1, y1), fill=col, width=rng.choice([1, 1, 2]))
            elif kind == 1:
                for _ in range(rng.randrange(8, 14)):
                    x = rng.randrange(int(cell * 0.2), int(cell * 0.8))
                    y = rng.randrange(int(cell * 0.35), int(cell * 0.85))
                    r = rng.choice([2, 3, 4])
                    g = rng.randrange(110, 170)
                    d.ellipse((x - r, y - r, x + r, y + r), fill=(g, g, g, rng.randrange(140, 210)))
            else:
                for _ in range(rng.randrange(6, 10)):
                    x = rng.randrange(int(cell * 0.2), int(cell * 0.8))
                    y = rng.randrange(int(cell * 0.35), int(cell * 0.85))
                    r = rng.randrange(6, 18)
                    d.ellipse((x - r, y - r, x + r, y + r), fill=(120, 90, 60, rng.randrange(50, 110)))
                img = img.filter(ImageFilter.GaussianBlur(radius=1.0))

            sheet.alpha_composite(img, (cx, cy))
    return sheet


def main() -> int:
    ap = argparse.ArgumentParser(description="Generate procedural tileable materials (material pipeline).")
    ap.add_argument("--out-dir", default="resources/assets/landscape/_materials", help="Output directory (relative or absolute).")
    ap.add_argument("--size", type=int, default=1024, help="Texture size (square).")
    ap.add_argument("--seed", type=int, default=1337, help="Random seed.")
    args = ap.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    grass = make_grass_albedo(args.size, seed=args.seed + 1)
    dirt = make_dirt_albedo(args.size, seed=args.seed + 2)
    soil = make_soil_side(args.size, seed=args.seed + 3)
    rock = make_rock_side(args.size, seed=args.seed + 4)
    decals = make_edge_decals_sheet(args.size, seed=args.seed + 5, grid=4)

    grass.save(os.path.join(args.out_dir, "grass_albedo.png"))
    dirt.save(os.path.join(args.out_dir, "dirt_albedo.png"))
    soil.save(os.path.join(args.out_dir, "soil_side.png"))
    rock.save(os.path.join(args.out_dir, "rock_side.png"))
    decals.save(os.path.join(args.out_dir, "edge_decals.png"))
    print(f"Wrote procedural materials to: {args.out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

