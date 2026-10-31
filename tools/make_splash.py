#!/usr/bin/env python3
"""Konwertuje obraz (PNG/JPG/BMP/...) na ekran startowy firmware.

Uzycie:
    python3 tools/make_splash.py sciezka/do/obraz.png [wyjscie.bmp]

Wynik: 16-bit BMP (RGB565, BI_BITFIELDS, bottom-up) o rozmiarze 480x320 px -
dokladnie taki, jaki firmware wczytuje jako ekran startowy (drawSplashScreen)
i jaki wyswietla drawBmpFromFS(). 16-bit jest 2x mniejszy od 24-bit i zostawia
miejsce w LittleFS na grafike alertu wgrywana z WWW (patrz tools/bmp24to16.py).
Domyslne wyjscie: data/splash.bmp.

Po wygenerowaniu wgraj LittleFS:  pio run -e esp32-s3-n16r8 -t uploadfs
"""
import os
import struct
import sys

from PIL import Image

W, H = 480, 320
DEFAULT_OUT = os.path.join("data", "splash.bmp")


def save_bmp565(filepath, img):
    w, h = img.size
    row_size = ((16 * w + 31) // 32) * 4
    pixel_data_size = row_size * h
    hdr = 14 + 40 + 12  # + maski R/G/B
    file_size = hdr + pixel_data_size
    px = img.load()
    with open(filepath, "wb") as f:
        f.write(b"BM")
        f.write(struct.pack("<I", file_size))
        f.write(struct.pack("<HH", 0, 0))
        f.write(struct.pack("<I", hdr))
        f.write(struct.pack("<I", 40))
        f.write(struct.pack("<i", w))
        f.write(struct.pack("<i", h))       # dodatnia wysokosc = bottom-up
        f.write(struct.pack("<HH", 1, 16))  # planes=1, bpp=16
        f.write(struct.pack("<I", 3))       # BI_BITFIELDS
        f.write(struct.pack("<I", pixel_data_size))
        f.write(struct.pack("<i", 2835))
        f.write(struct.pack("<i", 2835))
        f.write(struct.pack("<I", 0))
        f.write(struct.pack("<I", 0))
        f.write(struct.pack("<I", 0xF800))  # maska R
        f.write(struct.pack("<I", 0x07E0))  # maska G
        f.write(struct.pack("<I", 0x001F))  # maska B
        for y in range(h - 1, -1, -1):
            row = bytearray()
            for x in range(w):
                r, g, b = px[x, y][:3]
                v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                row += struct.pack("<H", v)
            f.write(row)
            f.write(b"\x00" * (row_size - len(row)))


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
    save_bmp565(out, img)
    print(f"Zapisano {out} ({os.path.getsize(out)} B, {W}x{H}, 16-bit BMP)")


if __name__ == "__main__":
    main()
