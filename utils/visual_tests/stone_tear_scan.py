#!/usr/bin/env python3
"""Tear detector for --flat shots: green rock on red background.

A tear = red pixels NOT reachable from the image border over red (i.e. holes
through the rock: enclosed by the silhouette). Silhouette bays are reachable
from the border and do not count. flood-fill via iterative binary dilation.
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image

MIN_HOLE_PX = 8  # ignore sub-pixel dust


def enclosed_red(red: np.ndarray) -> np.ndarray:
    reach = np.zeros_like(red)
    reach[0, :] = red[0, :]
    reach[-1, :] = red[-1, :]
    reach[:, 0] = red[:, 0]
    reach[:, -1] = red[:, -1]
    # Flood fill by repeated 4-neighbour dilation restricted to red.
    while True:
        grown = reach.copy()
        grown[1:, :] |= reach[:-1, :]
        grown[:-1, :] |= reach[1:, :]
        grown[:, 1:] |= reach[:, :-1]
        grown[:, :-1] |= reach[:, 1:]
        grown &= red
        if (grown == reach).all():
            break
        reach = grown
    return red & ~reach


def main() -> int:
    folder = Path(sys.argv[1] if len(sys.argv) > 1 else "logs/tear")
    results = []
    for png in sorted(folder.glob("*.png")):
        img = np.asarray(Image.open(png).convert("RGB"), dtype=np.uint8)
        r, g, b = img[..., 0], img[..., 1], img[..., 2]
        green = (g > 120) & (r < 90) & (b < 90)
        red = (r > 120) & (g < 90) & (b < 90)
        holes = enclosed_red(red)
        results.append((int(holes.sum()), png.name, int(green.sum())))
    results.sort(reverse=True)
    flagged = 0
    for hole_px, name, green_px in results:
        if hole_px >= MIN_HOLE_PX:
            flagged += 1
            print(f"TEAR {name:22s} hole_px={hole_px:6d} green_px={green_px:7d}")
    print(f"\n{flagged}/{len(results)} images flagged (>= {MIN_HOLE_PX} px)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
