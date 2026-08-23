#!/usr/bin/env python3
"""Print enclosed-red pixel count for one --flat PNG."""
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
from stone_tear_scan import enclosed_red  # noqa: E402

img = np.asarray(Image.open(sys.argv[1]).convert("RGB"), dtype=np.uint8)
r, g, b = img[..., 0], img[..., 1], img[..., 2]
red = (r > 120) & (g < 90) & (b < 90)
print(int(enclosed_red(red).sum()))
