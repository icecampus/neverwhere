import json
import math
import os
import random
from dataclasses import dataclass
from typing import List, Optional, Tuple

from PIL import Image, ImageChops, ImageDraw, ImageFilter


def _clamp_int(x: int, lo: int, hi: int) -> int:
    return lo if x < lo else hi if x > hi else x


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


def tileable_value_noise(w: int, h: int, freq: int, seed: int) -> Image.Image:
    """
    Tileable 2D value noise (wraps on edges). Output is L image (0..255).
    freq: number of grid cells across width/height.
    """
    freq = max(1, int(freq))
    grid = [[(_hash2(ix, iy, seed) & 0xFFFF) / 65535.0 for ix in range(freq)] for iy in range(freq)]
    out = Image.new("L", (w, h), 0)
    px = out.load()

    for y in range(h):
        fy = (y / h) * freq
        y0 = int(math.floor(fy)) % freq
        y1 = (y0 + 1) % freq
        ty = fy - math.floor(fy)
        sy = _smoothstep(ty)
        for x in range(w):
            fx = (x / w) * freq
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


def load_rgba(path: str) -> Image.Image:
    img = Image.open(path)
    return img.convert("RGBA")


def tile_pattern(src: Image.Image, size: Tuple[int, int], offset: Tuple[int, int] = (0, 0)) -> Image.Image:
    w, h = size
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    sw, sh = src.size
    ox, oy = offset
    ox = ox % sw
    oy = oy % sh
    for y in range(-oy, h, sh):
        for x in range(-ox, w, sw):
            out.alpha_composite(src, (x, y))
    return out


def polygon_mask(size: Tuple[int, int], pts: List[Tuple[float, float]]) -> Image.Image:
    m = Image.new("L", size, 0)
    d = ImageDraw.Draw(m)
    d.polygon(pts, fill=255)
    return m


def apply_alpha(img: Image.Image, alpha: Image.Image) -> Image.Image:
    out = img.copy()
    r, g, b, a = out.split()
    a = ImageChops.multiply(a, alpha)
    out.putalpha(a)
    return out


def paste_with_mask(dst: Image.Image, src: Image.Image, mask: Image.Image) -> None:
    tmp = src.copy()
    tmp.putalpha(mask)
    dst.alpha_composite(tmp)


def overlay_solid(dst: Image.Image, color_rgba: Tuple[int, int, int, int], mask: Image.Image) -> None:
    solid = Image.new("RGBA", dst.size, color_rgba)
    paste_with_mask(dst, solid, mask)


def edge_band_mask(surface_mask: Image.Image, falloff_px: int) -> Tuple[Image.Image, Image.Image]:
    falloff_px = max(0, int(falloff_px))
    if falloff_px <= 0:
        soft = surface_mask.copy()
        band = Image.new("L", surface_mask.size, 0)
        return soft, band
    soft = surface_mask.filter(ImageFilter.GaussianBlur(radius=falloff_px))
    band = ImageChops.subtract(soft, surface_mask)
    return soft, band


def apply_edge_noise(alpha: Image.Image, band: Image.Image, amp_px: int, scale: float, seed: int) -> Image.Image:
    amp_px = max(0, int(amp_px))
    if amp_px <= 0:
        return alpha
    strength = _clamp_int(int(amp_px * 10), 0, 120)
    if strength <= 0:
        return alpha

    w, h = alpha.size
    freq = int(_clamp(1.0 / max(1e-4, scale), 4.0, 64.0))
    n = tileable_value_noise(w, h, freq=freq, seed=seed).point(lambda p: int(p * strength / 255))
    n = ImageChops.multiply(n, band)
    out = ImageChops.subtract(alpha, n)
    return out


def split_sheet(sheet: Image.Image, cols: int, rows: int) -> List[Image.Image]:
    cols = max(1, int(cols))
    rows = max(1, int(rows))
    w, h = sheet.size
    cw = w // cols
    ch = h // rows
    out: List[Image.Image] = []
    for ry in range(rows):
        for cx in range(cols):
            out.append(sheet.crop((cx * cw, ry * ch, cx * cw + cw, ry * ch + ch)))
    return out


def scatter_decals(
    dst: Image.Image,
    decals: List[Image.Image],
    band: Image.Image,
    rng: random.Random,
    count: int,
    scale_min: float,
    scale_max: float,
) -> None:
    if not decals or count <= 0:
        return
    w, h = dst.size
    band_px = band.load()

    placed = 0
    tries = 0
    while placed < count and tries < count * 80:
        tries += 1
        x = rng.randrange(w)
        y = rng.randrange(h)
        if band_px[x, y] < 8:
            continue

        dec = rng.choice(decals)
        s = rng.uniform(scale_min, scale_max)
        dw, dh = dec.size
        dw2 = max(1, int(dw * s))
        dh2 = max(1, int(dh * s))
        dec2 = dec.resize((dw2, dh2), resample=Image.Resampling.BICUBIC)
        angle = rng.uniform(-18.0, 18.0)
        dec2 = dec2.rotate(angle, resample=Image.Resampling.BICUBIC, expand=True)
        px = x - dec2.size[0] // 2
        py = y - dec2.size[1] // 2
        dst.alpha_composite(dec2, (px, py))
        placed += 1


@dataclass
class MaterialStyle:
    surface_albedo: str
    side_soil_albedo: str
    side_rock_albedo: Optional[str] = None
    edge_decals_sheet: Optional[str] = None
    edge_decals_cols: int = 4
    edge_decals_rows: int = 4

    seed: int = 1337
    edge_falloff_px: int = 10
    edge_noise_amplitude_px: int = 4
    edge_noise_scale: float = 0.08
    decals_per_tile: int = 6
    decals_scale_min: float = 0.55
    decals_scale_max: float = 1.1
    surface_uv_jitter_px: int = 32
    side_uv_jitter_px: int = 16
    side_mode: str = "mixed"  # soil|rock|mixed
    outline_enabled: bool = False

    @staticmethod
    def from_json(path: str) -> "MaterialStyle":
        with open(path, "r", encoding="utf-8") as f:
            cfg = json.load(f)
        mats = cfg.get("materials", {})
        st = cfg.get("style", {})
        grid = mats.get("edge_decals_grid", {}) or {}
        return MaterialStyle(
            surface_albedo=mats["surface_albedo"],
            side_soil_albedo=mats["side_soil_albedo"],
            side_rock_albedo=mats.get("side_rock_albedo"),
            edge_decals_sheet=mats.get("edge_decals_sheet"),
            edge_decals_cols=int(grid.get("cols", 4)),
            edge_decals_rows=int(grid.get("rows", 4)),
            seed=int(st.get("seed", 1337)),
            edge_falloff_px=int(st.get("edge_falloff_px", 10)),
            edge_noise_amplitude_px=int(st.get("edge_noise_amplitude_px", 4)),
            edge_noise_scale=float(st.get("edge_noise_scale", 0.08)),
            decals_per_tile=int(st.get("decals_per_tile", 6)),
            decals_scale_min=float(st.get("decals_scale_min", 0.55)),
            decals_scale_max=float(st.get("decals_scale_max", 1.1)),
            surface_uv_jitter_px=int(st.get("surface_uv_jitter_px", 32)),
            side_uv_jitter_px=int(st.get("side_uv_jitter_px", 16)),
            side_mode=str(st.get("side_mode", "mixed")),
            outline_enabled=bool(st.get("outline_enabled", False)),
        )

    def resolve_paths(self, repo_root: str) -> None:
        def _res(p: Optional[str]) -> Optional[str]:
            if not p:
                return None
            if os.path.isabs(p):
                return p
            return os.path.normpath(os.path.join(repo_root, p))

        self.surface_albedo = _res(self.surface_albedo) or self.surface_albedo
        self.side_soil_albedo = _res(self.side_soil_albedo) or self.side_soil_albedo
        self.side_rock_albedo = _res(self.side_rock_albedo)
        self.edge_decals_sheet = _res(self.edge_decals_sheet)

