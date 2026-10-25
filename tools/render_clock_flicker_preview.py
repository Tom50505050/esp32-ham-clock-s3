#!/usr/bin/env python3
"""Podglad migotania zegara VLW - kontrola QA.

Symuluje odswiezanie zegara sekunda po sekunde i podswietla na czerwono komorki,
ktore sa przerysowywane. Dwa warianty:
  STARE - czyszczenie i rysowanie WSZYSTKICH komorek co sekunde (migotanie),
  NOWE  - dotkniecie TYLKO komorek, ktore zmienily sie miedzy sekundami.

Liczba przerysowanych pikseli jest wypisana pod kazda klatka.

Wynik: clock_flicker_preview.png.
Uruchom: python3 tools/render_clock_flicker_preview.py [wyjscie.png]
"""
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from render_clock_fonts_preview import FONT_DIR, load_vlw  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

FONTS = ["clock10", "clock14", "clock01"]
TIMES = ["11:11:11", "11:11:12", "11:11:13", "11:12:00", "11:12:01", "12:00:00"]
REF = "0123456789:"

BAND_H = 64
HOT = (170, 30, 30)


def ink_offset(f, s):
    asc = f["ascent"]
    ma = md = 0
    for c in s:
        g = f["glyphs"].get(ord(c))
        if not g:
            continue
        ma = max(ma, g["dy"])
        md = max(md, g["h"] - g["dy"])
    return asc + (md - ma) // 2


def cell_width(f):
    return max(f["glyphs"][ord(c)]["adv"] for c in REF)


def draw_time(img, f, s, center_x, y):
    cell = cell_width(f)
    cx = center_x - (len(s) * cell) // 2
    px = img.load()
    for c in s:
        g = f["glyphs"].get(ord(c))
        if g:
            for row in range(g["h"]):
                for col in range(g["w"]):
                    if f["data"][g["off"] + row * g["w"] + col] == 0:
                        continue
                    x = cx + (cell - g["w"]) // 2 + col + g["dx"]
                    yy = y + (f["ascent"] - g["dy"]) + row
                    if 0 <= x < img.width and 0 <= yy < img.height:
                        px[x, yy] = (235, 235, 235)
        cx += cell


def touched(f, prev, cur, full):
    """Zwraca zbior indeksow komorek dotknietych przy przejsciu prev -> cur."""
    n = len(cur)
    if full:
        return set(range(n))
    return {i for i in range(n) if i >= len(prev) or prev[i] != cur[i]}


def render_font(name):
    f = load_vlw(os.path.join(FONT_DIR, f"{name}-time.vlw"))
    row_h = 92
    tile = Image.new("RGB", (480, 26 + row_h * len(TIMES)), (18, 18, 26))
    d = ImageDraw.Draw(tile)
    d.text((6, 5), f"{name}: czerwone = komorki przerysowane przy zmianie sekundy",
           fill=(215, 215, 215))
    cell = cell_width(f)
    cx0 = 240 - 8 * cell // 2
    y_ink = 40
    prev = ""
    for i, t in enumerate(TIMES):
        top = 26 + i * row_h
        img = Image.new("RGB", (480, BAND_H), (0, 0, 0))
        draw_time(img, f, t, 240, y_ink - ink_offset(f, t))
        px = img.load()
        old_cells = touched(f, prev, t, full=True)
        new_cells = touched(f, prev, t, full=(i == 0))
        for ci in new_cells:
            for x in range(cx0 + ci * cell, cx0 + (ci + 1) * cell):
                for y in range(BAND_H):
                    if 0 <= x < 480:
                        r, g, b = px[x, y]
                        px[x, y] = (min(255, r // 3 + HOT[0]), g // 3, b // 3)
        tile.paste(img, (0, top))
        d.text((8, top + 70),
               f"STARE: {len(old_cells)*cell*BAND_H} px   NOWE: {len(new_cells)*cell*BAND_H} px   {t}",
               fill=(255, 200, 90))
        prev = t
    return tile


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "clock_flicker_preview.png")
    tiles = [render_font(n) for n in FONTS]
    h = sum(t.height for t in tiles)
    sheet = Image.new("RGB", (480, h), (25, 25, 35))
    y = 0
    for t in tiles:
        sheet.paste(t, (0, y))
        y += t.height
    sheet.save(out)
    print("zapisano", out)


if __name__ == "__main__":
    main()
