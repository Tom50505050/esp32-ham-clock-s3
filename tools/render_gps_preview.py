"""Podglad ekranu TFT "GPS - SATELITY" (480x320) 1:1 z main.cpp.

Renderuje realne bitmapy czcionek TFT_eSPI (Font 2 = Font16, Font 1 = GLCD 5x7),
wiec uklad zgadza sie z urzadzeniem. Wynik: PNG w /workspace/share/.

Uruchomienie:  python3 tools/render_gps_preview.py
"""
import math
import os
import re
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIB = os.path.join(ROOT, ".pio/libdeps/esp32-s3-n16r8/TFT_eSPI/Fonts/")
OUT = "/workspace/share"


def load_font(path, prefix, height, nchars):
    txt = open(path, encoding="latin-1").read()
    m = re.search(r"widtbl_%s\[%d\][^{]*\{(.*?)\};" % (prefix, nchars), txt, re.S)
    wid = [int(x) for x in re.findall(r"\b(\d+)\b", re.sub(r"//.*", "", m.group(1)))][:nchars]
    glyphs = {}
    for mm in re.finditer(r"chr_%s_([0-9A-Fa-f]{2})\[%d\]\s*=\s*(?://[^\n]*)?\s*\{([^}]*)\}" % (prefix, height), txt):
        code = int(mm.group(1), 16)
        body = re.sub(r"//.*", "", mm.group(2))
        vals = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]+)", body)]
        if len(vals) == height:
            glyphs[code] = vals
    return wid, glyphs


F2_W, F2_G = load_font(LIB + "Font16.c", "f16", 16, 96)


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

# Paleta ekranu GPS (main.cpp)
GPS_BG_DARK = c565(0x0000)
GPS_CARD = c565(0x0884)
GPS_ACCENT = c565(0x3697)
GPS_ACCENT2 = c565(0x4D5F)
GPS_ORANGE = c565(0xFB60)
GPS_ORANGE_BG = c565(0x6200)
GPS_TEXT = c565(0xEF7E)
GPS_FAINT = c565(0x6BF2)
GPS_OK = c565(0x07E0)
GPS_WARN = c565(0xFE4A)
GREY_UNUSED = c565(0x52AA)

CONS_COLORS = {0: GPS_OK, 1: c565(0xFD20), 2: GPS_ACCENT2, 3: c565(0xF81F), 4: c565(0xFFE0)}
CONS_LABELS = {0: "GPS", 1: "GLO", 2: "GAL", 3: "BDS", 4: "QZS"}

GPS_SCREEN_X, GPS_SCREEN_Y = 20, 20
GPS_SCREEN_W, GPS_SCREEN_H = 440, 280
GPS_CLOSE_X = GPS_SCREEN_X + GPS_SCREEN_W - 34
GPS_CLOSE_Y = GPS_SCREEN_Y + 6
GPS_CLOSE_W, GPS_CLOSE_H = 28, 28


def tw2(s):
    return sum(F2_W[ord(c) - 32] if 32 <= ord(c) <= 127 else 6 for c in s)


def f2(d, x, y, s, col):
    for ch in s:
        c = ord(ch)
        if c < 32 or c > 127:
            c = 32
        w = F2_W[c - 32]
        g = F2_G.get(c)
        if g:
            for row in range(16):
                bits = g[row]
                for col_i in range(w):
                    if bits & (0x80 >> col_i):
                        d.point((x + col_i, y + row), fill=col)
        x += w


def f1s(d, x, y, s, scale, col):
    for ch in s:
        g = GLCD.get(ord(ch))
        if g:
            for col_i in range(5):
                bits = g[col_i]
                for row in range(8):
                    if bits & (1 << row):
                        for dy in range(scale):
                            for dx in range(scale):
                                d.point((x + col_i * scale + dx, y + row * scale + dy), fill=col)
        x += 6 * scale
    return x


def f1(d, x, y, s, col):
    f1s(d, x, y, s, 1, col)


# --- przykladowe satelity (azymut, elewacja, snr, konstelacja, used) ---
SATS = [
    (12, 64, 44, 0, True), (88, 41, 38, 0, True), (150, 22, 27, 0, True),
    (211, 55, 41, 0, True), (300, 33, 31, 0, False), (345, 12, 18, 0, False),
    (75, 70, 36, 1, True), (240, 48, 29, 1, True), (190, 30, 22, 1, False),
    (120, 58, 33, 2, True), (280, 61, 35, 2, True), (35, 18, 20, 3, False),
]


def draw_sky_plot(d, cx, cy, radius, classic):
    ring = GPS_ORANGE_BG if classic else c565(0x2124)
    cross = c565(0x8410) if classic else c565(0x39E7)
    label = GPS_ORANGE if classic else GPS_ACCENT
    for rr in (radius, radius * 2 // 3, radius // 3):
        d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], outline=ring)
    d.line([cx - radius, cy, cx + radius, cy], fill=cross)
    d.line([cx, cy - radius, cx, cy + radius], fill=cross)
    f1(d, cx - 3, cy - radius - 10, "N", label)
    f1(d, cx - 3, cy + radius + 3, "S", label)
    f1(d, cx + radius + 3, cy - 3, "E", label)
    f1(d, cx - radius - 9, cy - 3, "W", label)
    for azim, elev, snr, cons, used in SATS:
        ang = math.radians(azim)
        rr = (90 - elev) / 90.0 * radius
        px = cx + int(math.sin(ang) * rr)
        py = cy - int(math.cos(ang) * rr)
        col = CONS_COLORS[cons] if used else GREY_UNUSED
        dotR = 2 + (2 if snr >= 40 else (1 if snr >= 25 else 0))
        d.ellipse([px - dotR, py - dotR, px + dotR, py + dotR], fill=col)
        if used:
            d.ellipse([px - dotR - 2, py - dotR - 2, px + dotR + 2, py + dotR + 2], outline=col)


def draw_gps_screen(classic, fix=True, hdop=0.9, pos="50.0614, 19.9366",
                    loc="JO90xc", module_ok=True, title_pl=True):
    card = GPS_ORANGE_BG if classic else GPS_CARD
    accent = GPS_ORANGE if classic else GPS_ACCENT
    border = GPS_ORANGE if classic else c565(0x298A)
    text = WHITE if classic else GPS_TEXT

    im = Image.new("RGB", (480, 320), BLACK)
    d = ImageDraw.Draw(im)

    # okno
    d.rounded_rectangle([GPS_SCREEN_X, GPS_SCREEN_Y,
                         GPS_SCREEN_X + GPS_SCREEN_W - 1, GPS_SCREEN_Y + GPS_SCREEN_H - 1],
                        radius=8, fill=card, outline=border)
    # pasek naglowka
    d.rectangle([GPS_SCREEN_X + 1, GPS_SCREEN_Y + 1, GPS_SCREEN_X + GPS_SCREEN_W - 2, GPS_SCREEN_Y + 34], fill=accent)
    f2(d, GPS_SCREEN_X + 12, GPS_SCREEN_Y + 10,
       "GPS - SATELITY" if title_pl else "GPS - SATELLITES", BLACK if classic else GPS_BG_DARK)

    # przycisk zamkniecia
    xc = BLACK if classic else GPS_BG_DARK
    d.rectangle([GPS_CLOSE_X, GPS_CLOSE_Y, GPS_CLOSE_X + GPS_CLOSE_W - 1, GPS_CLOSE_Y + GPS_CLOSE_H - 1], outline=xc)
    d.line([GPS_CLOSE_X + 8, GPS_CLOSE_Y + 8, GPS_CLOSE_X + GPS_CLOSE_W - 8, GPS_CLOSE_Y + GPS_CLOSE_H - 8], fill=xc)
    d.line([GPS_CLOSE_X + GPS_CLOSE_W - 8, GPS_CLOSE_Y + 8, GPS_CLOSE_X + 8, GPS_CLOSE_Y + GPS_CLOSE_H - 8], fill=xc)

    sat_used = sum(1 for s in SATS if s[4])
    sat_view = len(SATS)

    # box licznika
    boxX, boxY, boxW, boxH = GPS_SCREEN_X + 12, GPS_SCREEN_Y + 46, 150, 74
    d.rounded_rectangle([boxX, boxY, boxX + boxW - 1, boxY + boxH - 1], radius=6, fill=GPS_BG_DARK, outline=border)
    f1(d, boxX + 8, boxY + 6, "SATELITY W FIXIE", GPS_FAINT)
    f1s(d, boxX + 10, boxY + 20, str(sat_used), 4, GPS_OK if fix else GPS_WARN)
    bigw = len(str(sat_used)) * 24
    f2(d, boxX + 10 + bigw + 6, boxY + 34, "/ " + str(sat_view), text)
    if fix:
        f1(d, boxX + 8, boxY + 56, "FIX  HDOP %.1f" % hdop, GPS_OK)
    else:
        f1(d, boxX + 8, boxY + 56, "SZUKA FIXU...", GPS_WARN)

    # panel informacyjny
    infoX, infoY, infoW = boxX, boxY + boxH + 8, boxW
    infoH = GPS_SCREEN_H - (infoY - GPS_SCREEN_Y) - 12
    d.rounded_rectangle([infoX, infoY, infoX + infoW - 1, infoY + infoH - 1], radius=6, fill=GPS_BG_DARK, outline=border)
    ty = infoY + 6
    f1(d, infoX + 8, ty, "POZYCJA", GPS_FAINT); ty += 12
    f1(d, infoX + 8, ty, pos, text); ty += 12
    f1(d, infoX + 8, ty, "LOKATOR", GPS_FAINT); ty += 12
    f1(d, infoX + 8, ty, loc, text); ty += 14
    f1(d, infoX + 8, ty, "MODUL: OK" if module_ok else "MODUL: BRAK", GPS_FAINT)

    # sky plot + legenda
    plotR = 70
    cx = GPS_SCREEN_X + 190 + plotR + 10
    cy = GPS_SCREEN_Y + 46 + plotR + 6
    draw_sky_plot(d, cx, cy, plotR, classic)
    lx, ly = cx + plotR + 12, GPS_SCREEN_Y + 52
    for c in range(5):
        d.ellipse([lx, ly + 4, lx + 8, ly + 12], fill=CONS_COLORS[c])
        f1(d, lx + 14, ly, CONS_LABELS[c], text)
        ly += 14

    # paski SNR
    barX, barY = GPS_SCREEN_X + 190, cy + plotR + 14
    barW = GPS_SCREEN_W - 190 - 20
    f1(d, barX, barY - 11, "C/N0 [dBHz]", GPS_FAINT)
    shown = 0
    for azim, elev, snr, cons, used in SATS:
        if shown >= 12:
            break
        bw = max(0, min(barW - 26, snr * (barW - 26) // 50))
        by = barY + shown * 6
        col = CONS_COLORS[cons] if used else GREY_UNUSED
        f1(d, barX, by, "%02d" % (shown + 1), col)
        d.rectangle([barX + 20, by, barX + 20 + bw, by + 3], fill=col)
        shown += 1
    return im


def save(im, name):
    p = os.path.join(OUT, name)
    im.save(p)
    print("zapisano:", p, im.size)


os.makedirs(OUT, exist_ok=True)

mod = draw_gps_screen(False)
save(mod, "gps-1-nowoczesny.png")
cla = draw_gps_screen(True)
save(cla, "gps-2-klasyczny.png")
searching = draw_gps_screen(False, fix=False, hdop=99.9, pos="--", loc="--", module_ok=True)
save(searching, "gps-3-szuka-fixu.png")

sheet = Image.new("RGB", (480 + 20, 320 * 3 + 40), (40, 40, 40))
for i, img in enumerate([mod, cla, searching]):
    sheet.paste(img, (10, 10 + i * (320 + 10)))
save(sheet, "gps-podglad.png")


# --- Naglowek zegara z ikona GPS (kontekst: gdzie jest ikona) ---
def draw_gps_glyph(d, x, y, ink, bg):
    d.rectangle([x, y + 6, x + 8, y + 13], fill=ink)
    d.line([x + 4, y + 6, x + 4, y + 13], fill=bg)
    d.rectangle([x + 17, y + 6, x + 25, y + 13], fill=ink)
    d.line([x + 21, y + 6, x + 21, y + 13], fill=bg)
    d.polygon([(x + 9, y + 10), (x + 13, y + 6), (x + 17, y + 10)], fill=ink)
    d.polygon([(x + 9, y + 10), (x + 17, y + 10), (x + 13, y + 14)], fill=ink)
    d.line([x + 13, y + 2, x + 13, y + 5], fill=ink)
    d.ellipse([x + 12, y + 1, x + 14, y + 3], fill=ink)


GPS_ICON_X, GPS_ICON_Y, GPS_ICON_W, GPS_ICON_H = 372, 9, 26, 18
WIFI = WHITE
BATT_GREEN = c565(0x07E0)


def draw_wifi_battery(d):
    # paski WiFi (screenW-45, 8)
    for i, hh in enumerate([4, 7, 10, 13]):
        d.rectangle([435 + i * 5, 24 - hh, 438 + i * 5, 24], fill=WIFI)
    # bateria (screenW-70, 8)
    d.rectangle([410, 10, 427, 20], outline=WHITE)
    d.rectangle([428, 13, 430, 17], fill=WHITE)
    d.rectangle([412, 12, 424, 18], fill=BATT_GREEN)


def header_with_gps(classic, fix=True):
    im = Image.new("RGB", (480, 320), BLACK)
    d = ImageDraw.Draw(im)
    headerH = 40
    if classic:
        d.rectangle([0, 0, 479, headerH - 1], fill=GPS_ORANGE)
        d.line([0, headerH - 1, 479, headerH - 1], fill=c565(0x8410))
        f2(d, 190, 12, "ESP32", c565(0xFB60))
        f2(d, 190 + 6 * len("ESP32"), 12, "-HAM-CLOCK", BLACK)
        clock_col = BLACK
    else:
        d.rectangle([0, 0, 479, headerH - 1], fill=GPS_BG_DARK)
        d.line([0, headerH - 1, 479, headerH - 1], fill=GPS_ACCENT)
        f2(d, (480 - tw2("ESP32-HAM-CLOCK")) // 2, 12, "ESP32-HAM-CLOCK", GPS_TEXT)
        clock_col = c565(0xFFE0)
    draw_wifi_battery(d)
    # ikona GPS
    plate = c565(0x18E3) if classic else BLACK
    d.rounded_rectangle([GPS_ICON_X - 3, GPS_ICON_Y - 2,
                         GPS_ICON_X - 3 + GPS_ICON_W + 6, GPS_ICON_Y - 2 + GPS_ICON_H + 4],
                        radius=3, fill=plate)
    ink = GPS_OK if fix else GPS_WARN
    draw_gps_glyph(d, GPS_ICON_X, GPS_ICON_Y, ink, plate)
    # zegar HH:MM
    f2(d, 300, 12, "14:32", clock_col)
    return im


h_mod = header_with_gps(False)
save(h_mod, "gps-4-naglowek-nowoczesny.png")
h_cla = header_with_gps(True)
save(h_cla, "gps-5-naglowek-klasyczny.png")

