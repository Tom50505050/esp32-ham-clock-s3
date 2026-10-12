#!/usr/bin/env python3
"""Konwertuje obraz (PNG/JPG/BMP/...) na ekran startowy firmware.

Uzycie:
    python3 tools/make_splash.py sciezka/do/obraz.png [wyjscie.bmp]

Wynik: 24-bit BMP (RGB888, bottom-up, BI_RGB) o rozmiarze 480x320 px -
dokladnie taki, jaki firmware wczytuje jako ekran startowy (drawSplashScreen)
i jaki wyswietla drawBmpFromFS(). Domyślne wyjscie: data/splash.bmp.

Po wygenerowaniu wgraj LittleFS:  pio run -e esp32-s3-n16r8 -t uploadfs
"""
import os
import struct
import sys

from PIL import Image

W, H = 480, 320
DEFAULT_OUT = os.path.join("data", "splash.bmp")


def save_bmp_24bit(filepath, img):
    w, h = img.size
    row_bytes = w * 3
    pad = (4 - (row_bytes % 4)) % 4
    row_size = row_bytes + pad
    pixel_data_size = row_size * h
    file_size = 14 + 40 + pixel_data_size
    px = img.load()
    with open(filepath, "wb") as f:
        f.write(b"BM")
        f.write(struct.pack("<I", file_size))
        f.write(struct.pack("<HH", 0, 0))
        f.write(struct.pack("<I", 14 + 40))
        f.write(struct.pack("<I", 40))
        f.write(struct.pack("<i", w))
        f.write(struct.pack("<i", h))       # dodatnia wysokosc = bottom-up
        f.write(struct.pack("<HH", 1, 24))  # planes=1, bpp=24
        f.write(struct.pack("<I", 0))       # BI_RGB (bez kompresji)
        f.write(struct.pack("<I", pixel_data_size))
        f.write(struct.pack("<i", 2835))
        f.write(struct.pack("<i", 2835))
        f.write(struct.pack("<I", 0))
        f.write(struct.pack("<I", 0))
        for y in range(h - 1, -1, -1):
            for x in range(w):
                r, g, b = px[x, y][:3]
                f.write(struct.pack("<BBB", b, g, r))
            f.write(b"\x00" * pad)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    src = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_OUT
    img = Image.open(src).convert("RGB")
    if img.size != (W, H):
        print(f"Skaluje {img.size} -> ({W}, {H})")
        img = img.resize((W, H), Image.LANCZOS)
    save_bmp_24bit(out, img)
    print(f"Zapisano {out} ({os.path.getsize(out)} B, {W}x{H}, 24-bit BMP)")


if __name__ == "__main__":
    main()
