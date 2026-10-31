#!/usr/bin/env python3
"""Podglad 1:1 menu ALARMU (ekran Matrix, przytrzymanie 5 s).

Rysuje uklad z firmware drawAlarmMenu() (480x320) prawdziwymi bitmapami
czcionek TFT_eSPI (Font 1 = GLCD, Font 2 = Font16), w stylu nowoczesnym i
klasycznym. Uruchom z katalogu repozytorium:

    python3 tools/render_alarm_preview.py [katalog_wyjsciowy]

Domyslnie zapisuje alarm-nowoczesny.png i alarm-klasyczny.png w /workspace/share.
"""
import os
import re
import sys

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIB = os.path.join(ROOT, ".pio/libdeps/esp32-s3-n16r8/TFT_eSPI/Fonts/")
OUT_DIR = sys.argv[1] if len(sys.argv) > 1 else "/workspace/share"


def load_f2(path, prefix, height, nchars):
    txt = open(path, encoding="latin-1").read()
    m = re.search(r"widtbl_%s\[%d\][^{]*\{(.*?)\};" % (prefix, nchars), txt, re.S)
    wid = [int(x) for x in re.findall(r"\b(\d+)\b", re.sub(r"//.*", "", m.group(1)))][:nchars]
    glyphs = {}
    for mm in re.finditer(r"chr_%s_([0-9A-Fa-f]{2})\[%d\]\s*=\s*(?://[^\n]*)?\s*\{([^}]*)\}"
                          % (prefix, height), txt):
        code = int(mm.group(1), 16)
        vals = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]+)", re.sub(r"//.*", "", mm.group(2)))]
        if len(vals) == height:
            glyphs[code] = vals
    return wid, glyphs, height


def load_glcd():
    txt = open(LIB + "glcdfont.c", encoding="latin-1").read()
    body = txt[txt.index("{"):]
    vals = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]+)", body)]
    return {i: vals[i * 5:i * 5 + 5] for i in range(256)}


F2 = load_f2(LIB + "Font16.c", "f16", 16, 96)
GLCD = load_glcd()


def c565(v):
    return (((v >> 11) & 0x1F) * 255 // 31, ((v >> 5) & 0x3F) * 255 // 63, (v & 0x1F) * 255 // 31)


UI_BG = c565(0x0000)
UI_CARD = c565(0x0884)
UI_BORDER = c565(0x298A)
UI_ACCENT = c565(0x3697)
UI_DIM = c565(0x9D98)
UI_TEXT = c565(0xEF7E)
ORANGE = c565(0xFD20)
DARKGREY = c565(0x7BEF)
WHITE = (255, 255, 255)


def draw_f1(d, x, y, s, color, size=1):
    """GLCD 5x7 skalowany size razy (setTextSize)."""
    for ch in s:
        g = GLCD.get(ord(ch))
        if g:
            for col in range(5):
                bits = g[col]
                for row in range(8):
                    if bits & (1 << row):
                        if size == 1:
                            d.point((x + col, y + row), fill=color)
                        else:
                            d.rectangle([x + col * size, y + row * size,
                                         x + col * size + size - 1, y + row * size + size - 1], fill=color)
        x += 6 * size
    return x


def f1_w(s, size=1):
    return len(s) * 6 * size


def draw_f2(d, x, y, s, color):
    wid, glyphs, h = F2
    for ch in s:
        c = ord(ch)
        if c < 32 or c > 127:
            c = 32
        w = wid[c - 32]
        g = glyphs.get(c)
        if g:
            for row in range(h):
                bits = g[row]
                for col in range(w):
                    if bits & (0x80 >> col):
                        d.point((x + col, y + row), fill=color)
        x += w
    return x


def tw2(s):
    return sum(F2[0][ord(c) - 32] if 32 <= ord(c) <= 127 else 6 for c in s)


def tile(d, x, y, w, h, label, active, classic=False):
    fill = (ORANGE if active else DARKGREY) if classic else (ORANGE if active else UI_CARD)
    border = (0, 0, 0) if classic else (ORANGE if active else UI_BORDER)
    txt = (0, 0, 0) if active else (WHITE if classic else UI_TEXT)
    d.rectangle([x, y, x + w - 1, y + h - 1], fill=fill, outline=border)
    lw = f1_w(label)
    draw_f1(d, x + (w - lw) // 2, y + (h - 8) // 2, label, txt)


DAYS_SHORT = ["ND", "PN", "WT", "SR", "CZ", "PT", "SB"]
MELODIES = ["BUDZIK", "DZWONEK", "POBUDKA", "PIKANIE", "MORS CQ", "MORS SOS",
            "SYRENA", "NOKIA", "TETRIS", "KUKULKA", "PIPS", "KURANT"]

# Uklad zgodny z main.cpp (stale ALM_*).
PREVIEW_TIME_Y = 56
PREVIEW_NAME_Y = 78
ROW_Y = 90
BTN_W, BTN_H = 46, 26
HOUR_MINUS_X, HOUR_PLUS_X = 104, 192
MIN_MINUS_X, MIN_PLUS_X = 242, 330
ROW_LABEL_Y = 118
DAYS_LABEL_Y = 132
DAYS_Y, DAY_TILE_W, DAY_TILE_H, DAY_GAP = 142, 54, 26, 8
MEL_LABEL_Y = 174
MEL_GRID_Y = 184
MEL_COLS, MEL_TILE_W, MEL_TILE_H, MEL_GAP = 6, 72, 30, 6
BTN_ROW_Y, BTN_ROW_H, BTN_ROW_W, BTN_ROW_GAP = 256, 30, 118, 12


def render(classic, hour=7, minute=30, days_mask=0b0111110, melody=0, enabled=True):
    bg = c565(0x0000) if classic else UI_BG
    im = Image.new("RGB", (480, 320), bg)
    d = ImageDraw.Draw(im)

    # Naglowek menu (uiMenuHeader)
    if classic:
        d.rectangle([0, 0, 479, 29], fill=ORANGE)
        d.line([0, 29, 479, 29], fill=(132, 132, 132))
        hdr_fg = (0, 0, 0)
    else:
        d.rectangle([0, 0, 479, 29], fill=UI_CARD)
        d.line([0, 29, 479, 29], fill=ORANGE)
        hdr_fg = UI_TEXT
    draw_f1(d, 10, 11, "ALARM", hdr_fg)

    # Podglad godziny (duzy, size=3) + nazwa dzwieku
    t = "%02d:%02d" % (hour, minute)
    w = f1_w(t, 3)
    draw_f1(d, (480 - w) // 2, PREVIEW_TIME_Y - 12, t, (ORANGE if classic else UI_ACCENT), size=3)
    mw = f1_w(MELODIES[melody])
    draw_f1(d, (480 - mw) // 2, PREVIEW_NAME_Y, MELODIES[melody], UI_DIM)

    # Wiersz +/- (godz, min)
    for bx in (HOUR_MINUS_X, HOUR_PLUS_X, MIN_MINUS_X, MIN_PLUS_X):
        d.rectangle([bx, ROW_Y, bx + BTN_W - 1, ROW_Y + BTN_H - 1],
                    fill=(DARKGREY if classic else UI_CARD), outline=WHITE)
    for bx, sign in ((HOUR_MINUS_X, "-"), (HOUR_PLUS_X, "+"), (MIN_MINUS_X, "-"), (MIN_PLUS_X, "+")):
        draw_f2(d, bx + BTN_W // 2 - tw2(sign) // 2, ROW_Y + BTN_H // 2 - 8, sign, WHITE)
    draw_f1(d, HOUR_MINUS_X + (HOUR_PLUS_X + BTN_W - HOUR_MINUS_X) // 2 - f1_w("GODZ") // 2,
            ROW_LABEL_Y, "GODZ", UI_DIM)
    draw_f1(d, MIN_MINUS_X + (MIN_PLUS_X + BTN_W - MIN_MINUS_X) // 2 - f1_w("MIN") // 2,
            ROW_LABEL_Y, "MIN", UI_DIM)

    # Etykiety sekcji
    draw_f1(d, 10, DAYS_LABEL_Y, "DNI ALARMU", UI_DIM)
    draw_f1(d, 10, MEL_LABEL_Y, "DZWIEK", UI_DIM)

    # Dni tygodnia
    sx = (480 - (7 * DAY_TILE_W + 6 * DAY_GAP)) // 2
    for i in range(7):
        tile(d, sx + i * (DAY_TILE_W + DAY_GAP), DAYS_Y, DAY_TILE_W, DAY_TILE_H,
             DAYS_SHORT[i], bool(days_mask & (1 << i)), classic)

    # Melodie: 2 rzedy po 6
    grid_w = MEL_COLS * MEL_TILE_W + (MEL_COLS - 1) * MEL_GAP
    gx = (480 - grid_w) // 2
    for i, name in enumerate(MELODIES):
        col, row = i % MEL_COLS, i // MEL_COLS
        tile(d, gx + col * (MEL_TILE_W + MEL_GAP), MEL_GRID_Y + row * (MEL_TILE_H + MEL_GAP),
             MEL_TILE_W, MEL_TILE_H, name, melody == i, classic)

    # Przyciski
    enableX = 12
    saveX = enableX + BTN_ROW_W + BTN_ROW_GAP
    closeX = saveX + BTN_ROW_W + BTN_ROW_GAP
    tile(d, enableX, BTN_ROW_Y, BTN_ROW_W, BTN_ROW_H, "ALARM: WL" if enabled else "ALARM: WYL", enabled, classic)
    tile(d, saveX, BTN_ROW_Y, BTN_ROW_W, BTN_ROW_H, "ZAPISZ", False, classic)
    tile(d, closeX, BTN_ROW_Y, BTN_ROW_W, BTN_ROW_H, "ZAMKNIJ", False, classic)

    return im


def render_ring(hour=7, minute=30, melody=0):
    im = Image.new("RGB", (480, 320), (0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rectangle([6, 6, 473, 313], outline=c565(0xF800))
    d.rectangle([10, 10, 469, 309], outline=c565(0xF800))
    t = "ALARM!"
    w = f1_w(t, 3)
    draw_f1(d, (480 - w) // 2, 84, t, c565(0xF800), size=3)
    tt = "%02d:%02d" % (hour, minute)
    w = f1_w(tt, 4)
    draw_f1(d, (480 - w) // 2, 152, tt, WHITE, size=4)
    s = "Dotknij ekranu, aby zatrzymac"
    w = f1_w(s)
    draw_f1(d, (480 - w) // 2, 232, s, UI_DIM)
    m = MELODIES[melody]
    w = f1_w(m)
    draw_f1(d, (480 - w) // 2, 256, m, UI_DIM)
    return im


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    for classic, name in ((False, "alarm-nowoczesny.png"), (True, "alarm-klasyczny.png")):
        im = render(classic)
        path = os.path.join(OUT_DIR, name)
        im.save(path)
        print("zapisano", path)
    im = render_ring()
    path = os.path.join(OUT_DIR, "alarm-dzwoni.png")
    im.save(path)
    print("zapisano", path)


if __name__ == "__main__":
    main()
