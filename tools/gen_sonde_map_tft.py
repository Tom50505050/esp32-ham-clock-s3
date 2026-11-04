#!/usr/bin/env python3
"""Pre-render a Poland map strip for the RADIOSONDY TFT screen.

Source: tools/map_src/PL_drogowa_750tys_95x90cm_01_10_2019.jpg (1536x1455).
The geographic bbox of Poland is contained in the interior box B (same
georeferencing as tools/render_radiosonda_v3.py). We crop that box and
resize to the full-panel image rect (480x244) - plate-carree projection
matching sondeProject() in firmware, so the map fills the whole screen.

Output: data/map_tft.jpg (about 30 KB), flashed to LittleFS and drawn by
drawRadiosondeMap() via TJpgDec when present. Fallback: 1-bit mask.
"""
import os

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(HERE, "map_src", "PL_drogowa_750tys_95x90cm_01_10_2019.jpg")
_OUT_ = os.path.join(ROOT, "data", "map_tft.jpg")

# Interior box in the source image (same as render_radiosonda_v3.py).
B_X0, B_Y0, B_X1, B_Y1 = 21, 27, 1527, 1449
# On-screen image rect - full panel width (plate-carree, same as sondeProject()).
W, H = 480, 244
QUALITY = 88


def main():
    im = Image.open(SRC).convert("RGB")
    crop = im.crop((B_X0, B_Y0, B_X1, B_Y1))
    strip = crop.resize((W, H), Image.LANCZOS)
    strip.save(_OUT_, "JPEG", quality=QUALITY, optimize=True)
    print("Zapisano %s (%dx%d, %d B)" % (_OUT_, W, H, os.path.getsize(_OUT_)))


if __name__ == "__main__":
    main()