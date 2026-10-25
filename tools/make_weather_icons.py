#!/usr/bin/env python3
"""Generuje domyslne ikony alertu pogodowego (data/icon50/burza.bmp i wiatr.bmp).

24-bit BMP 50x50, czarne tlo (jak pozostale ikony w data/icon50). Uruchom z
katalogu repozytorium:

    python3 tools/make_weather_icons.py

Ikony sa domyslnie uzywane przez ekran alertu pogodowego (firmware skaluje je
do ~170 px). Uzytkownik moze je zastapic wlasna grafika z panelu WWW.
"""
import os
import struct

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "data", "icon50")
SIZE = 50


def save_bmp24(img, path):
    """Zapisuje obraz jako 24-bit BMP (bottom-up, wyrownanie wiersza do 4 B)."""
    w, h = img.size
    row = (w * 3 + 3) & ~3
    pix = img.load()
    with open(path, "wb") as f:
        f.write(b"BM")
        f.write(struct.pack("<IHHI", 14 + 40 + row * h, 0, 0, 54))
        f.write(struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, row * h, 2835, 2835, 0, 0))
        pad = b"\x00" * (row - w * 3)
        for y in range(h - 1, -1, -1):  # bottom-up
            line = bytearray()
            for x in range(w):
                r, g, b = pix[x, y][:3]
                line += bytes((b, g, r))
            f.write(bytes(line) + pad)


def make_storm():
    """Chmura z zoltym piorunem (spójna z wektorowa ikona w firmware)."""
    img = Image.new("RGB", (SIZE, SIZE), (0, 0, 0))
    d = ImageDraw.Draw(img)
    grey = (200, 200, 200)
    d.ellipse([8, 8, 40, 34], fill=grey)          # glowna chmura
    d.ellipse([2, 18, 22, 36], fill=grey)          # lewa
    d.ellipse([28, 18, 48, 36], fill=grey)         # prawa
    d.rectangle([10, 20, 40, 32], fill=grey)       # dol chmury
    bolt = (255, 220, 0)
    d.polygon([(27, 24), (17, 38), (24, 38), (20, 50), (34, 34), (26, 34), (32, 24)], fill=bolt)
    return img


def make_wind():
    """Trzy smugi wiatru z zawijasami (spójna z wektorowa ikona w firmware)."""
    img = Image.new("RGB", (SIZE, SIZE), (0, 0, 0))
    d = ImageDraw.Draw(img)
    c = (0, 220, 255)
    d.rounded_rectangle([4, 12, 34, 17], radius=2, fill=c)
    d.ellipse([30, 10, 42, 22], outline=c, width=3)
    d.rounded_rectangle([2, 24, 42, 29], radius=2, fill=c)
    d.ellipse([38, 21, 50, 33], outline=c, width=3)
    d.rounded_rectangle([6, 36, 38, 41], radius=2, fill=c)
    d.ellipse([32, 34, 44, 46], outline=c, width=3)
    return img


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    for name, fn in (("burza.bmp", make_storm), ("wiatr.bmp", make_wind)):
        p = os.path.join(OUT_DIR, name)
        save_bmp24(fn(), p)
        print("zapisano", p, os.path.getsize(p), "B")


if __name__ == "__main__":
    main()
