import json
import os
import shutil
import uuid
from dataclasses import dataclass
from datetime import datetime
from typing import Optional, Tuple

from PIL import Image


def now_run_id(prefix: str = "", seed: Optional[int] = None, notes: str = "") -> str:
    ts = datetime.now().strftime("%Y%m%d-%H%M%S")
    parts = [ts]
    if seed is not None:
        parts.append(f"seed{seed}")
    if notes:
        safe = "".join(ch if ch.isalnum() or ch in "-_." else "_" for ch in notes)
        parts.append(safe)
    base = "_".join(parts)
    return f"{prefix}{base}" if prefix else base


def ensure_dir(path: str) -> None:
    os.makedirs(path, exist_ok=True)


def copy_file(src: str, dst: str) -> None:
    ensure_dir(os.path.dirname(dst))
    shutil.copyfile(src, dst)


def atlas_tile_size(atlas_w: int, atlas_h: int, cols: int, rows: int) -> Tuple[int, int]:
    return atlas_w // cols, atlas_h // rows


def extract_tile0_thumbnail(atlas_path: str, out_thumb_path: str, cols: int = 4, rows: int = 6, thumb_width: int = 256) -> None:
    img = Image.open(atlas_path).convert("RGBA")
    tw, th = atlas_tile_size(img.width, img.height, cols, rows)
    tile0 = img.crop((0, 0, tw, th))

    # Resize (keep aspect) if requested
    if thumb_width and thumb_width > 0 and tile0.width != thumb_width:
        scale = thumb_width / max(1, tile0.width)
        new_h = max(1, int(tile0.height * scale))
        tile0 = tile0.resize((thumb_width, new_h), resample=Image.Resampling.LANCZOS)

    ensure_dir(os.path.dirname(out_thumb_path))
    tile0.save(out_thumb_path)


@dataclass
class SliceIndex:
    uuid: str
    layerType: str = "BaseLandscape"
    pivot_x: float = 0.0
    pivot_y: float = 0.0
    thumbnail: str = "thumbnail.png"
    atlas: str = "atlas.png"

    def to_json(self) -> dict:
        return {
            "uuid": self.uuid,
            "layerType": self.layerType,
            "pivot": {"x": float(self.pivot_x), "y": float(self.pivot_y)},
            "slice": {"thumbnail": self.thumbnail, "atlas": self.atlas},
        }


def ensure_index_json(pack_dir: str, index_filename: str = "index.json") -> str:
    """
    Ensure pack has index.json with a UUID. Returns uuid.
    If index.json exists and contains uuid -> preserve.
    Otherwise create new.
    """
    index_path = os.path.join(pack_dir, index_filename)
    if os.path.exists(index_path):
        try:
            with open(index_path, "r", encoding="utf-8") as f:
                data = json.load(f)
            u = str(data.get("uuid", "")).strip()
            if u:
                return u
        except Exception:
            pass

    ensure_dir(pack_dir)
    u = str(uuid.uuid4())
    idx = SliceIndex(uuid=u)
    with open(index_path, "w", encoding="utf-8") as f:
        json.dump(idx.to_json(), f, indent=2)
        f.write("\n")
    return u


def publish_atlas_and_thumbnail(
    atlas_src_path: str,
    pack_dir: str,
    cols: int = 4,
    rows: int = 6,
    thumb_width: int = 256,
    ensure_index: bool = False,
) -> None:
    if ensure_index:
        ensure_index_json(pack_dir)

    atlas_dst = os.path.join(pack_dir, "atlas.png")
    thumb_dst = os.path.join(pack_dir, "thumbnail.png")

    copy_file(atlas_src_path, atlas_dst)
    extract_tile0_thumbnail(atlas_dst, thumb_dst, cols=cols, rows=rows, thumb_width=thumb_width)

