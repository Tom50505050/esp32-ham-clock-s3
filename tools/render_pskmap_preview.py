"""Podglad ekranu PSK MAP z ramka spotow przeniesiona na lewa strone.

Uzywa prawdziwej mapy (data/Mapa swiata.bmp) i czcionki GLCD z TFT_eSPI.
Wynik: /workspace/share/pskmap-podglad.png
"""
import os
import re
from PIL import Image, ImageDraw

ROOT = "/workspace/project"
LIB = os.path.join(ROOT, ".pio/libdeps/esp32-s3-n16r8/TFT_eSPI/Fonts/")
OUT = "/workspace/share"


def load_glcd():
    txt = open(LIB + "glcdfont.c", encoding="latin-1").read()
    body = txt[txt.index("{"):]
    vals = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]+)", body)]
    return {i: vals[i * 5:i * 5 + 5] for i in range(256)}


GLCD = load_glcd()


def c565(v):
    return (((v >> 11) & 0x1F) * 255 // 31, ((v >> 5) & 0x3F) * 255 // 63, (v & 0x1F) * 255 // 31)


BLACK = (0, 0, 0)
WHITE = (255, 255, 255)
RED = c565(0xF800)
GREEN = c565(0x07E0)
BLUE = c565(0x001F)
CYAN = c565(0x07FF)
YELLOW = c565(0xFFE0)
ORANGE = c565(0xFD20)
MAGENTA = c565(0xF81F)
RADIO_ORANGE = c565(0xFD20)
UI_CARD = c565(0x0884)
UI_ACCENT = c565(0x3697)
UI_TEXT = c565(0xEF7E)
UI_FAINT = c565(0x6BF2)
UI_WARN = c565(0xFE60)


def f1(d, x, y, s, col):
    for ch in s:
        g = GLCD.get(ord(ch))
        if g:
            for c in range(5):
                for r in range(8):
                    if g[c] & (1 << r):
                        d.point((x + c, y + r), fill=col)
        x += 6


def f1c(d, cx, y, s, col):
    f1(d, cx - len(s) * 6 // 2, y, s, col)


def header(d):
    d.rectangle([0, 0, 479, 25], fill=UI_CARD)
    d.line([0, 25, 479, 25], fill=UI_ACCENT)
    d.rectangle([2, 2, 45, 37], fill=RADIO_ORANGE)
    for i in range(3):
        d.rectangle([10, 11 + i * 7, 37, 14 + i * 7], fill=BLACK)
    f1c(d, 240, 9, "PSKReporter MAP", UI_TEXT)
    f1(d, 12, 30, "Spoty: 63", UI_ACCENT)
    f1(d, 322, 30, "AUTO:5min", UI_WARN)
    d.rectangle([420, 6, 437, 20], outline=WHITE)
    for i, hh in enumerate([4, 7, 10, 13]):
        d.rectangle([443 + i * 5, 18 - hh, 446 + i * 5, 18], fill=WHITE)


def footer(d):
    d.polygon([(14, 309), (24, 299), (24, 319)], fill=UI_ACCENT)
    d.polygon([(466, 309), (456, 299), (456, 319)], fill=UI_ACCENT)
    f1c(d, 240, 300, "SWITCH SCREEN", UI_FAINT)


def legend(d):
    y, bs, gap = 300, 8, 45
    bands = ["80m", "40m", "20m", "15m", "10m", "6m"]
    cols = [RED, ORANGE, GREEN, CYAN, BLUE, MAGENTA]
    for i in range(6):
        d.rectangle([10 + i * gap, y, 10 + i * gap + bs, y + bs], fill=cols[i])
        f1(d, 10 + i * gap + 12, y, bands[i], WHITE)
    f1(d, 300, y, "Tap: Refresh", YELLOW)


BANDCOL = {80: RED, 40: ORANGE, 20: GREEN, 15: CYAN, 10: BLUE, 6: MAGENTA}
SPOTS = [("SP9TNV", 20, "SSB"), ("DL1ABC", 40, "CW"), ("K1ABC", 20, "FT8"),
         ("JA1XYZ", 15, "FT8"), ("VK2ABC", 10, "SSB"), ("F5ABC", 40, "CW"),
         ("EA1ABC", 20, "FT4"), ("W6ABC", 20, "FT8"), ("UA3ABC", 15, "CW"),
         ("OH2ABC", 20, "FT8"), ("PY2ABC", 10, "SSB"), ("ZS1ABC", 15, "CW")]


def panel_new(d):
    px, py, pw, ph = 4, 126, 128, 160
    bg = c565(0x18E3)
    d.rectangle([px, py, px + pw - 1, py + ph - 1], fill=bg, outline=UI_ACCENT)
    f1(d, px + 4, py + 5, "SPOTY: %d" % len(SPOTS), UI_ACCENT)
    for i, (cs, band, mode) in enumerate(reversed(SPOTS[:14])):
        ry = py + 18 + i * 10
        d.rectangle([px + 3, ry + 1, px + 8, ry + 6], fill=BANDCOL[band])
        row = cs.ljust(8) + " %dm %s" % (band, mode)
        f1(d, px + 11, ry, row, WHITE)


def panel_old(d):
    px, py, pw, ph = 360, 40, 116, 250
    bg = c565(0x18E3)
    d.rectangle([px, py, px + pw - 1, py + ph - 1], fill=bg, outline=UI_ACCENT)
    f1(d, px + 4, py + 5, "SPOTY: %d" % len(SPOTS), UI_ACCENT)
    for i, (cs, band, mode) in enumerate(reversed(SPOTS[:22])):
        ry = py + 18 + i * 10
        if ry + 8 > py + ph:
            break
        d.rectangle([px + 3, ry + 1, px + 8, ry + 6], fill=BANDCOL[band])
        f1(d, px + 11, ry, cs.ljust(8) + " %dm %s" % (band, mode), WHITE)


def base_map():
    return Image.open(ROOT + "/data/Mapa swiata.bmp").convert("RGB")


def render(panel_fn, mark_continents=False):
    im = base_map()
    d = ImageDraw.Draw(im)
    if mark_continents:
        pass
    header(d)
    panel_fn(d)
    legend(d)
    footer(d)
    return im


im1 = render(panel_old)
im1.save(OUT + "/pskmap-przed.png")

im2 = render(panel_new)
# nakladka: podswietl obszar, ktory byl zasloniety (prawa strona)
d = ImageDraw.Draw(im2)
d.rectangle([360, 40, 476, 290], outline=(255, 80, 80), width=1)
im2.save(OUT + "/pskmap-po.png")

# sklejka
sheet = Image.new("RGB", (480 * 2 + 30, 320 + 40), (40, 40, 40))
sheet.paste(Image.open(OUT + "/pskmap-przed.png"), (10, 10))
sheet.paste(Image.open(OUT + "/pskmap-po.png"), (500, 10))
sd = ImageDraw.Draw(sheet)
sd.text((12, 330), "PRZED: ramka po prawej (zaslania Azje/Australie)", fill=(255, 255, 255))
sd.text((502, 330), "PO: ramka po lewej, nad oceanem", fill=(255, 255, 255))
sheet.save(OUT + "/pskmap-podglad.png")
print("zapisano:", OUT + "/pskmap-podglad.png")
