#!/usr/bin/env python3
"""Podglad poprawki centrowania zegara (VLW) - kontrola QA.

Dla kilku czcionek rysuje duzy zegar tak, jak robi to firmware: STARY sposob
(srodek wg textWidth = suma szerokosci glifow) i NOWY (stale komorki monospace).
Pokazuje, ze stary zegar przesuwal sie przy waskiej cyfrze "1" i byl zle
wysrodkowany, a nowy jest zawsze rowny.

Wynik: weather_alert... nie - clock_center_preview.png w katalogu glownym.
Uruchom: python3 tools/render_clock_center_preview.py [wyjscie.png]
"""
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from render_clock_fonts_preview import FONT_DIR, load_vlw  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

TIME_A = "11:11:11"
TIME_B = "08:08:08"
FONTS = ["clock01", "clock02", "clock05", "clock06"]

RED = (255, 60, 60)
WHITE = (255, 255, 255)
GREY = (150, 150, 150)
BG = (0, 0, 0)
ACCENT = (60, 60, 80)


def cell_width(f):
    return max(f["glyphs"][ord(c)]["adv"] for c in "0123456789:")


def old_width(f, s):
    # Stary firmware: tft.textWidth() = suma szerokosci glifow (gWidth).
    return sum(f["glyphs"][ord(c)]["w"] for c in s)


def draw_text(img, f, s, x, y, color):
    px = img.load()
    for c in s:
        g = f["glyphs"].get(ord(c))
        if not g:
            continue
        base = g["off"]
        for row in range(g["h"]):
            for col in range(g["w"]):
                a = f["data"][base + row * g["w"] + col]
                if a == 0:
                    continue
                cx = x + col + g["dx"]
                cy = y + (f["ascent"] - g["dy"]) + row
                if 0 <= cx < img.width and 0 <= cy < img.height:
                    px[cx, cy] = color


def draw_new(img, f, s, center_x, y, color):
    cell = cell_width(f)
    cx = center_x - (len(s) * cell) // 2
    for c in s:
        gw = f["glyphs"][ord(c)]["w"] if ord(c) in f["glyphs"] else 0
        draw_text(img, f, c, cx + (cell - gw) // 2, y, color)
        cx += cell


def draw_old(img, f, s, center_x, y, color):
    x = center_x - old_width(f, s) // 2
    draw_text(img, f, s, x, y, color)


def render_font(name):
    fc = load_vlw(os.path.join(FONT_DIR, f"{name}-call.vlw"))
    ft = load_vlw(os.path.join(FONT_DIR, f"{name}-time.vlw"))
    tile = Image.new("RGB", (480, 320), BG)
    d = ImageDraw.Draw(tile)
    # Linia srodka ekranu - punkt odniesienia.
    d.line([240, 40, 240, 280], fill=ACCENT)
    y0 = 60
    dy = 55
    draw_old(tile, ft, TIME_A, 240, y0, GREY)
    draw_old(tile, ft, TIME_B, 240, y0 + dy, GREY)
    draw_new(tile, ft, TIME_A, 240, y0 + 2 * dy, RED)
    draw_new(tile, ft, TIME_B, 240, y0 + 3 * dy, WHITE)
    d.text((6, 6), f"{name}  (szare=STARE, czerwone/biale=NOWE)", fill=(200, 200, 200))
    return tile


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "clock_center_preview.png")
    tiles = [render_font(n) for n in FONTS]
    cols = 2
    rows = (len(tiles) + cols - 1) // cols
    sheet = Image.new("RGB", (480 * cols, 320 * rows), (25, 25, 35))
    for i, t in enumerate(tiles):
        sheet.paste(t, ((i % cols) * 480, (i // cols) * 320))
    sheet.save(out)
    print("zapisano", out)


if __name__ == "__main__":
    main()
