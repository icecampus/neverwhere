#!/usr/bin/env python3
"""Heuristic visual checks for PolygonalGeneratedLandscape capture screenshots."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import cv2
import numpy as np


# ImGui arrow colors from MeshPreview.cpp (BGR order for OpenCV).
LIT_ARROW_BGR = np.array([128, 255, 96], dtype=np.uint8)
HINT_ARROW_BGR = np.array([72, 220, 255], dtype=np.uint8)
RAW_ARROW_BGR = np.array([96, 96, 255], dtype=np.uint8)


def wall_roi(shape: tuple[int, int, int]) -> tuple[slice, slice]:
    height, width = shape[:2]
    return (
        slice(int(height * 0.28), int(height * 0.88)),
        slice(int(width * 0.30), int(width * 0.78)),
    )


def arrow_mask(image_bgr: np.ndarray, target_bgr: np.ndarray, tolerance: int = 42) -> np.ndarray:
    lower = np.clip(target_bgr.astype(np.int16) - tolerance, 0, 255).astype(np.uint8)
    upper = np.clip(target_bgr.astype(np.int16) + tolerance, 0, 255).astype(np.uint8)
    return cv2.inRange(image_bgr, lower, upper)


def median_hue_degrees(mask: np.ndarray, hsv: np.ndarray) -> float | None:
    ys, xs = np.where(mask > 0)
    if len(xs) < 24:
        return None
    hues = hsv[ys, xs, 0].astype(np.float32)
    return float(np.median(hues) * 2.0)


def check_lit_image(path: Path, max_patchiness: float) -> None:
    image = cv2.imread(str(path), cv2.IMREAD_COLOR)
    if image is None:
        raise RuntimeError(f"failed to read image: {path}")

    roi = image[wall_roi(image.shape)]
    gray = cv2.cvtColor(roi, cv2.COLOR_BGR2GRAY)
    blurred = cv2.GaussianBlur(gray, (0, 0), sigmaX=8.0, sigmaY=8.0)
    residual = cv2.absdiff(gray, blurred)
    patchiness = float(np.std(residual))
    mean_luma = float(np.mean(gray))

    print(f"[lit] {path.name}: patchiness={patchiness:.2f}, mean_luma={mean_luma:.1f}")
    if patchiness > max_patchiness:
        raise RuntimeError(
            f"lit wall looks too patchy ({patchiness:.2f} > {max_patchiness:.2f}) in {path}"
        )
    if mean_luma < 20.0:
        raise RuntimeError(f"lit capture is too dark in {path}")


def check_normal_vectors(path: Path, max_hue_delta: float, min_overlap_ratio: float) -> None:
    image = cv2.imread(str(path), cv2.IMREAD_COLOR)
    if image is None:
        raise RuntimeError(f"failed to read image: {path}")

    roi_y, roi_x = wall_roi(image.shape)
    roi = image[roi_y, roi_x]
    hsv = cv2.cvtColor(roi, cv2.COLOR_BGR2HSV)

    color_tolerance = 50
    green = arrow_mask(roi, LIT_ARROW_BGR, color_tolerance)
    yellow = arrow_mask(roi, HINT_ARROW_BGR, color_tolerance)
    red = arrow_mask(roi, RAW_ARROW_BGR, color_tolerance)

    kernel5 = np.ones((5, 5), np.uint8)
    kernel3 = np.ones((3, 3), np.uint8)
    near_arrows = cv2.dilate(cv2.bitwise_or(yellow, red), kernel5)
    arrow_green = cv2.bitwise_and(green, near_arrows)
    overlap = cv2.bitwise_and(arrow_green, cv2.dilate(yellow, kernel3))

    green_count = int(cv2.countNonZero(arrow_green))
    yellow_count = int(cv2.countNonZero(yellow))
    red_count = int(cv2.countNonZero(red))
    overlap_count = int(cv2.countNonZero(overlap))

    overlap_ratio = overlap_count / max(1, min(green_count, yellow_count))
    overlap_hue = median_hue_degrees(overlap, hsv)
    green_hue = median_hue_degrees(arrow_green, hsv)
    yellow_hue = median_hue_degrees(yellow, hsv)
    if overlap_hue is not None:
        hue_delta = 0.0
    elif green_hue is not None and yellow_hue is not None:
        hue_delta = abs(green_hue - yellow_hue)
    else:
        hue_delta = 999.0

    print(
        f"[normal_vectors] {path.name}: green={green_count}, yellow={yellow_count}, "
        f"red={red_count}, overlap={overlap_count}, hue_delta={hue_delta:.1f}, "
        f"overlap_ratio={overlap_ratio:.2f}"
    )

    if green_count < 80 or yellow_count < 40:
        raise RuntimeError(f"not enough lit/hint arrows in {path}")
    if red_count > green_count * 2.0:
        raise RuntimeError(f"too many raw facet arrows vs lit in {path}")
    if overlap_ratio < min_overlap_ratio:
        raise RuntimeError(
            f"lit arrows do not align with outwardHint ({overlap_ratio:.2f} < {min_overlap_ratio:.2f}) in {path}"
        )
    if hue_delta > max_hue_delta:
        raise RuntimeError(
            f"lit and outwardHint arrow hues diverge ({hue_delta:.1f} > {max_hue_delta:.1f}) in {path}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "captures_dir",
        nargs="?",
        default="tools/visual_tests/captures",
        help="Directory with PNG captures from PolygonalGeneratedLandscape",
    )
    parser.add_argument("--max-lit-patchiness", type=float, default=18.0)
    parser.add_argument("--max-hue-delta", type=float, default=18.0)
    parser.add_argument("--min-overlap-ratio", type=float, default=0.12)
    args = parser.parse_args()

    captures_dir = Path(args.captures_dir)
    lit_candidates = [
        captures_dir / "lit_gpu.png",
        captures_dir / "lit_window.png",
    ]
    lit_path = next((path for path in lit_candidates if path.exists()), None)
    if lit_path is None:
        print(f"TEST FAIL missing lit capture in {captures_dir}", file=sys.stderr)
        return 1

    normal_vectors_path = captures_dir / "normal_vectors.png"
    if not normal_vectors_path.exists():
        print(f"TEST FAIL missing normal_vectors.png in {captures_dir}", file=sys.stderr)
        return 1

    try:
        check_lit_image(lit_path, args.max_lit_patchiness)
        check_normal_vectors(normal_vectors_path, args.max_hue_delta, args.min_overlap_ratio)
    except RuntimeError as error:
        print(f"TEST FAIL {error}", file=sys.stderr)
        return 1

    print("TEST PASS mesh preview visual checks")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
