#!/usr/bin/env python3
"""Generuje domyslne ikony alertu pogodowego (data/icon50/burza.bmp i wiatr.bmp).

24-bit BMP 480x320 - grafika wypelnia CALY ekran alertu (firmware skaluje
fit=cover). Czarne tlo nie jest potrzebne, bo obraz pokrywa ekran. Uruchom z
katalogu repozytorium:

    python3 tools/make_weather_icons.py

Uzytkownik moze zastapic je wlasna grafika z panelu WWW.
"""
import os
import struct

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "data", "icon50")
W, H = 480, 320


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


def vgrad(top, bottom):
    """Pionowy gradient tla wypelniajacy caly ekran."""
    img = Image.new("RGB", (W, H))
    px = img.load()
    for y in range(H):
        t = y / (H - 1)
        c = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        for x in range(W):
            px[x, y] = c
    return img


def make_storm():
    """Burzowe niebo: ciemny gradient + chmura + zolty piorun + deszcz."""
    img = vgrad((26, 30, 50), (74, 82, 108))
    d = ImageDraw.Draw(img)

    grey = (205, 210, 222)
    d.ellipse([120, 58, 360, 156], fill=grey)     # glowna chmura
    d.ellipse([54, 92, 214, 176], fill=grey)      # lewa
    d.ellipse([272, 92, 432, 176], fill=grey)     # prawa
    d.rectangle([118, 120, 362, 168], fill=grey)
    d.ellipse([130, 128, 350, 182], fill=(158, 164, 180))  # cien pod chmura

    bolt = (255, 220, 0)
    d.polygon([(262, 158), (212, 252), (250, 252), (228, 312),
               (302, 204), (262, 204), (296, 158)], fill=bolt)

    rain = (140, 168, 205)
    for x in range(120, 400, 44):
        d.line([x, 188, x - 26, 250], fill=rain, width=3)
    return img


def make_wind():
    """Wietrzne niebo: ciemny gradient + cyjanowe smugi z zawijasami."""
    img = vgrad((10, 24, 36), (22, 50, 68))
    d = ImageDraw.Draw(img)
    c = (0, 220, 255)

    def streak(x0, y0, x1, h, r):
        d.rounded_rectangle([x0, y0, x1, y0 + h], radius=r, fill=c)
        d.ellipse([x1 - 2, y0 - h, x1 + 2 * r, y0 + 2 * h], outline=c, width=max(4, h // 2))

    streak(40, 62, 300, 14, 7)
    streak(96, 132, 384, 16, 8)
    streak(30, 202, 336, 16, 8)
    streak(130, 268, 404, 16, 8)
    return img


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    for name, fn in (("burza.bmp", make_storm), ("wiatr.bmp", make_wind)):
        p = os.path.join(OUT_DIR, name)
        save_bmp24(fn(), p)
        print("zapisano", p, os.path.getsize(p), "B")


if __name__ == "__main__":
    main()
