#!/usr/bin/env python3
"""Podglad ekranu zegara dla wszystkich czcionek (VLW) - kontrola QA.

Rysuje plansze 480x320 z znakiem i duzym zegarem dla kazdej opcji czcionki,
uzywajac tych samych plikow VLW co firmware. Wynik: /tmp/clock_fonts_preview.png
"""
import os
import struct

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_DIR = os.path.join(ROOT, "data", "fonts")

NAMES = ["DOMYSLNA", "SANS BOLD", "ORBITRON", "OXANIUM", "MICHROMA", "AUDIOWIDE",
         "RUSSO ONE", "TECH MONO", "QUANTICO", "ELECTROLIZE", "BUNGEE",
         "NOVA MONO", "ICELAND", "SERIF BOLD", "MONO BOLD"]

CALL = "SP5ABC"
TIME = "00:00:00"
# Kafel w pelnej rozdzielczosci ekranu (480x320) - dopiero po zlozeniu planszy
# jest zmniejszany, zeby geometria czcionek byla zgodna z firmware.
TILE_W, TILE_H = 480, 320
COLS = 2


def load_vlw(path):
    d = open(path, "rb").read()
    gcount, _ver, _pt, _mbox, ascent, descent = struct.unpack(">IIIIII", d[:24])
    off = 24
    bmp = 24 + gcount * 28
    glyphs = {}
    for _ in range(gcount):
        u, h, w, adv, dy, dx, _pad = struct.unpack(">IIIIiiI", d[off:off + 28])
        off += 28
        glyphs[u] = {"w": w, "h": h, "adv": adv, "dy": dy, "dx": dx, "off": bmp}
        bmp += w * h
    return {"ascent": ascent, "descent": descent, "glyphs": glyphs, "data": d}


def text_width(f, s):
    return sum(f["glyphs"][ord(c)]["adv"] for c in s if ord(c) in f["glyphs"])


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
                    r, gg, b = px[cx, cy]
                    px[cx, cy] = ((color[0] * a + r * (255 - a)) // 255,
                                  (color[1] * a + gg * (255 - a)) // 255,
                                  (color[2] * a + b * (255 - a)) // 255)
        x += g["adv"]
    return x


def ink_offset(f, s):
    """Pionowa korekta jak w firmware (clockInkOffset): srodek atramentu glifow."""
    max_asc = max_desc = 0
    for c in s:
        g = f["glyphs"].get(ord(c))
        if not g:
            continue
        max_asc = max(max_asc, g["dy"])
        max_desc = max(max_desc, g["h"] - g["dy"])
    return f["ascent"] + (max_desc - max_asc) // 2


def render_tile(idx):
    """Renderuje prawdziwy uklad ekranu 480x320: ramka + znak + duzy zegar."""
    img = Image.new("RGB", (TILE_W, TILE_H), (0, 0, 0))
    d = ImageDraw.Draw(img)
    d.text((6, 4), f"{idx}. {NAMES[idx]}", fill=(0, 255, 255))

    # Ramka jak w firmware: x=30, y=45, w=420, h=220.
    d.rectangle([30, 45, 30 + 420, 45 + 220], outline=(70, 70, 70))

    # Pas znaku: y=50..94 (srodek 72). Pas zegara: y=126..178 (srodek 152).
    call_cy, time_cy = 72, 152
    if idx == 0:
        fc = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 30)
        ft = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 58)
        d.text(((TILE_W - d.textlength(CALL, font=fc)) / 2, call_cy - 12), CALL, fill=(255, 255, 255), font=fc)
        d.text(((TILE_W - d.textlength(TIME, font=ft)) / 2, time_cy - 24), TIME, fill=(255, 224, 0), font=ft)
        return img
    fc = load_vlw(os.path.join(FONT_DIR, f"clock{idx:02d}-call.vlw"))
    ft = load_vlw(os.path.join(FONT_DIR, f"clock{idx:02d}-time.vlw"))
    cy = call_cy - ink_offset(fc, CALL)
    draw_text(img, fc, CALL, (TILE_W - text_width(fc, CALL)) // 2, cy, (255, 255, 255))
    cy = time_cy - ink_offset(ft, TIME)
    draw_text(img, ft, TIME, (TILE_W - text_width(ft, TIME)) // 2, cy, (255, 224, 0))
    return img


def main():
    rows = (len(NAMES) + COLS - 1) // COLS
    sheet = Image.new("RGB", (COLS * TILE_W, rows * TILE_H), (20, 20, 20))
    for i in range(len(NAMES)):
        sheet.paste(render_tile(i), ((i % COLS) * TILE_W, (i // COLS) * TILE_H))
    sheet = sheet.resize((sheet.width // 2, sheet.height // 2), Image.LANCZOS)
    out = "/tmp/clock_fonts_preview.png"
    sheet.save(out)
    print("zapisano", out, sheet.size)


if __name__ == "__main__":
    main()
