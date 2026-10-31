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
MELODIES = ["BUDZIK", "DZWONEK", "POBUDKA", "PIKANIE"]


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

    # Podglad godziny (duzy) + nazwa dzwieku
    t = "%02d:%02d" % (hour, minute)
    d.rectangle([0, 36, 479, 101], fill=bg)
    w = f1_w(t, 4)
    draw_f1(d, (480 - w) // 2, 52, t, (ORANGE if classic else UI_ACCENT), size=4)
    mw = f1_w(MELODIES[melody])
    draw_f1(d, (480 - mw) // 2, 92, MELODIES[melody], UI_DIM)

    # Wiersz +/- (godz, min)
    rowY = 116
    bw, bh = 46, 34
    for bx in (108, 196, 238, 326):
        d.rectangle([bx, rowY, bx + bw - 1, rowY + bh - 1],
                    fill=(DARKGREY if classic else UI_CARD), outline=WHITE)
    for bx, sign in ((108, "-"), (196, "+"), (238, "-"), (326, "+")):
        draw_f2(d, bx + bw // 2 - tw2(sign) // 2, rowY + bh // 2 - 8, sign, WHITE)
    draw_f1(d, 108 + (196 + bw - 108) // 2 - f1_w("GODZ") // 2, rowY - 12, "GODZ", UI_DIM)
    draw_f1(d, 238 + (326 + bw - 238) // 2 - f1_w("MIN") // 2, rowY - 12, "MIN", UI_DIM)

    # Etykiety sekcji
    draw_f1(d, 10, 148, "DNI ALARMU", UI_DIM)
    draw_f1(d, 10, 204, "DZWIEK", UI_DIM)

    # Dni tygodnia
    tw, th, gap = 54, 30, 8
    sx = (480 - (7 * tw + 6 * gap)) // 2
    for i in range(7):
        tile(d, sx + i * (tw + gap), 158, tw, th, DAYS_SHORT[i], bool(days_mask & (1 << i)), classic)

    # Dzwiek
    mtw, mth, mgap = 104, 30, 10
    msx = (480 - (4 * mtw + 3 * mgap)) // 2
    for i in range(4):
        tile(d, msx + i * (mtw + mgap), 214, mtw, mth, MELODIES[i], melody == i, classic)

    # Przyciski
    bW, bH, bY, bGap = 118, 30, 262, 12
    enableX = 12
    saveX = enableX + bW + bGap
    closeX = saveX + bW + bGap
    tile(d, enableX, bY, bW, bH, "ALARM: WL" if enabled else "ALARM: WYL", enabled, classic)
    tile(d, saveX, bY, bW, bH, "ZAPISZ", False, classic)
    tile(d, closeX, bY, bW, bH, "ZAMKNIJ", False, classic)

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
