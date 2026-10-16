"""Podglad ekranu TFT "STACJA" (480x320) 1:1 z main.cpp.

Renderuje realne bitmapy czcionek TFT_eSPI (Font 2 = Font16, Font 1 = GLCD 5x7),
wiec uklad zgadza sie z urzadzeniem. Wynik: PNG w /workspace/share/.
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
RED = c565(0xF800)
GREEN = c565(0x07E0)
BLUE = c565(0x001F)
CYAN = c565(0x07FF)
YELLOW = c565(0xFFE0)
ORANGE = c565(0xFD20)
NAVY = c565(0x000F)
DARKGREY = c565(0x7BEF)
LIGHTGREY = c565(0xC618)
RADIO_ORANGE = c565(0xFD20)

UI_BG = c565(0x0000)
UI_CARD = c565(0x0884)
UI_BORDER = c565(0x298A)
UI_ACCENT = c565(0x3697)
UI_ACCENT2 = c565(0x4D5F)
UI_TEXT = c565(0xEF7E)
UI_DIM = c565(0x9D98)
UI_FAINT = c565(0x6BF2)
UI_RED = c565(0xF800)
ARP_GRID = c565(0x0320)  # ARP_COL_GRID - ciemna zielen siatki radaru APRS

# Kolory linii propagacji wg dystansu (main.cpp: propPolarityColor).
def POLARITY(km):
    if km < 500:
        return CYAN
    if km < 1500:
        return YELLOW
    return RED


def tw1(s):
    return len(s) * 6


def f1(d, x, y, s, col):
    for ch in s:
        c = ord(ch)
        g = GLCD.get(c)
        if g:
            for col_i in range(5):
                bits = g[col_i]
                for row in range(8):
                    if bits & (1 << row):
                        d.point((x + col_i, y + row), fill=col)
        x += 6


def f1c(d, cx, y, s, col):
    f1(d, cx - tw1(s) // 2, y, s, col)


def f1s(d, x, y, s, scale, col):
    """Font 1 (GLCD) skalowany przez setTextSize(scale) - tak rysuje drawString()
    bez parametru fontu (domyslny textfont=1)."""
    for ch in s:
        c = ord(ch)
        g = GLCD.get(c)
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


def f1sc(d, cx, y, s, scale, col):
    f1s(d, cx - (len(s) * 6 * scale) // 2, y, s, scale, col)


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


def f2c(d, cx, y, s, col):
    f2(d, cx - tw2(s) // 2, y, s, col)


def header(d, title, classic):
    bg = RADIO_ORANGE if classic else UI_CARD
    fg = BLACK if classic else UI_TEXT
    line = c565(0x8410) if classic else UI_ACCENT
    d.rectangle([0, 0, 479, 25], fill=bg)
    d.line([0, 25, 479, 25], fill=line)
    f2c(d, 240, 5, title, fg)
    # ikona baterii (420,1)
    d.rectangle([420, 6, 437, 20], outline=WHITE)
    d.rectangle([438, 10, 440, 16], fill=WHITE)
    d.rectangle([422, 8, 433, 18], fill=GREEN)
    # belki WiFi (445,1) - pelny zasieg
    for i, hh in enumerate([4, 7, 10, 13]):
        d.rectangle([443 + i * 5, 18 - hh, 446 + i * 5, 18], fill=WHITE)


def footer(d, classic):
    col = RADIO_ORANGE if classic else UI_ACCENT
    d.polygon([(14, 309), (24, 299), (24, 319)], fill=col)
    d.polygon([(466, 309), (456, 299), (456, 319)], fill=col)
    f1c(d, 240, 300, "SWITCH SCREEN", UI_FAINT)


KB_TOP, KB_W, KB_H, KB_GAP = 118, 32, 26, 4
ROWS = ["1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM.-/:"]


def key(d, x, y, label, fill, classic, txt=WHITE):
    d.rectangle([x, y, x + KB_W - 1, y + KB_H - 1], fill=fill,
                outline=(WHITE if classic else UI_FAINT))
    # main.cpp: setTextSize(1); setCursor(x+12, y+9); print(char)
    f1(d, x + 12, y + 9, label, txt)


def keyboard(d, buf, classic):
    card = c565(0x18E3) if classic else UI_CARD
    keyc = DARKGREY if classic else UI_BORDER
    d.rectangle([20, KB_TOP, 459, KB_TOP + 194], fill=card,
                outline=(WHITE if classic else UI_ACCENT))
    d.rectangle([90, KB_TOP + 8, 319, KB_TOP + 35], fill=BLACK)
    # setTextSize(2)+print() => GLCD 2x
    f1s(d, 95, KB_TOP + 14, (buf[-18:] if len(buf) > 18 else buf) + "_", 2,
        YELLOW if classic else UI_ACCENT)
    for r, row in enumerate(ROWS):
        for i, ch in enumerate(row):
            x = 84 + i * (KB_W + KB_GAP)
            y = KB_TOP + 44 + r * 26
            fill = (BLUE if classic else UI_ACCENT2) if (r == 0 or (r == 3 and i >= 7)) else keyc
            key(d, x, y, ch, fill, classic, WHITE if classic else UI_TEXT)
    # DEL/CLR: setCursor + print, GLCD 1x
    d.rectangle([330, KB_TOP + 8, 387, KB_TOP + 35], fill=RED,
                outline=(WHITE if classic else UI_FAINT))
    f1(d, 344, KB_TOP + 18, "DEL", WHITE)
    d.rectangle([396, KB_TOP + 8, 449, KB_TOP + 35], fill=ORANGE,
                outline=(WHITE if classic else UI_FAINT))
    f1(d, 407, KB_TOP + 18, "CLR", WHITE)
    # OK/X: setTextSize(2)+setCursor+print => GLCD 2x
    d.rectangle([250, KB_TOP + 158, 334, KB_TOP + 185], fill=GREEN if classic else UI_ACCENT,
                outline=(WHITE if classic else UI_ACCENT2))
    f1s(d, 278, KB_TOP + 164, "OK", 2, BLACK if classic else UI_BG)
    d.rectangle([350, KB_TOP + 158, 434, KB_TOP + 185], fill=RED,
                outline=(WHITE if classic else UI_FAINT))
    f1s(d, 386, KB_TOP + 164, "X", 2, WHITE)


def search_button(d, classic):
    d.rectangle([90, 258, 239, 287], fill=GREEN if classic else UI_ACCENT,
                outline=(WHITE if classic else UI_ACCENT2))
    # setTextSize(2)+drawString(...,165,273) MC_DATUM => GLCD 2x wyÃÂrodkowane
    f1sc(d, 165, 273, "SZUKAJ", 2, BLACK if classic else UI_BG)


def card(d, classic):
    d.rectangle([20, 60, 459, 299], fill=BLACK if classic else UI_CARD,
                outline=(BLUE if classic else UI_BORDER))
    d.rectangle([21, 61, 458, 298], outline=(NAVY if classic else UI_FAINT))


def blend(a, b, t, mx=255):
    """Mieszanie dwoch kolorow RGB (main.cpp: blend565)."""
    return tuple((a[i] * (mx - t) + b[i] * t) // mx for i in range(3))

def station_radio(d, x0, y0, w, h, call, col, bgcol):
    """Transceiver (main.cpp: stationDrawRadio) - korpus, wyswietlacz, antena."""
    soft = blend(col, bgcol, 165)
    d.rounded_rectangle([x0, y0, x0 + w - 1, y0 + h - 1], radius=7, fill=bgcol, outline=col)
    d.rounded_rectangle([x0 + 2, y0 + 2, x0 + w - 3, y0 + h - 3], radius=6, outline=soft)
    ax = x0 + w // 2
    d.line([ax, y0, ax, y0 - 22], fill=col)
    d.line([ax - 1, y0, ax - 1, y0 - 22], fill=soft)
    d.ellipse([ax - 3, y0 - 27, ax + 3, y0 - 21], fill=col)
    d.ellipse([ax - 5, y0 - 29, ax + 5, y0 - 19], outline=soft)
    dx, dy, dw, dh = x0 + 8, y0 + 9, w - 48, 22
    d.rounded_rectangle([dx, dy, dx + dw - 1, dy + dh - 1], radius=3, fill=col,
                        outline=blend(col, WHITE, 90))
    f1sc(d, dx + dw // 2, dy + dh // 2 - 5, call, 2, BLACK)
    d.ellipse([x0 + w - 23, y0 + 9, x0 + w - 11, y0 + 21], outline=col)
    d.ellipse([x0 + w - 19, y0 + 13, x0 + w - 15, y0 + 17], fill=col)
    d.ellipse([x0 + w - 23, y0 + 27, x0 + w - 11, y0 + 39], outline=soft)
    d.ellipse([x0 + w - 19, y0 + 31, x0 + w - 15, y0 + 35], fill=soft)
    for i in range(3):
        xx = x0 + 12 + i * 5
        d.line([xx, y0 + h - 10, xx, y0 + h - 6], fill=soft)


def results(d, classic, callsign, source, fields, distance=None, notfound=False,
            source_time="", bearing=None, mycall="SP9TNV"):
    bg = BLACK if classic else UI_BG
    panel = BLACK if classic else UI_CARD
    border = BLUE if classic else UI_BORDER
    text = WHITE if classic else UI_TEXT
    dim = LIGHTGREY if classic else UI_DIM
    accent = YELLOW if classic else UI_ACCENT
    dxcol = GREEN   # szukany znak (mocny zielony)
    mycol = RED     # moj znak (czerwony)

    if not classic:
        for i in range(12):
            band = blend(c565(0x0841), (0, 0, 0), i * 255 // 11)
            yb = 26 + i * 22
            d.rectangle([0, yb, 479, yb + 22], fill=band)

    px, py, pw, ph = 110, 30, 260, 180
    d.rounded_rectangle([px, py, px + pw - 1, py + ph - 1], radius=10, fill=panel, outline=border)
    d.rounded_rectangle([px + 1, py + 1, px + pw - 2, py + ph - 2], radius=9,
                        outline=blend(border, panel, 150))
    d.rounded_rectangle([px + 14, py + 6, px + pw - 15, py + 8], radius=2, fill=accent)

    have_dist = isinstance(distance, (int, float))
    wave = POLARITY(distance) if have_dist else dim
    xL, xR, yC = 74, 406, 226
    for off in (-9, 0, 9):
        c = wave if off == 0 else blend(wave, bg, 150)
        amp = 6.0 if off == 0 else 4.0
        px_, py_ = xL, yC + off
        for i in range(1, 65):
            x = xL + (xR - xL) * i // 64
            y = yC + off + int(math.sin(i * 0.45) * amp)
            d.line([px_, py_, x, y], fill=c)
            px_, py_ = x, y

    station_radio(d, 14, 248, 120, 46, mycall or "SP9TNV", mycol, bg)
    station_radio(d, 346, 248, 120, 46, callsign if callsign else "---", dxcol, bg)

    if not callsign:
        f1sc(d, 240, 108, "Podaj znak stacji", 2, accent)
        f1c(d, 240, 140, "nacisnij SZUKAJ i wpisz znak", dim)
    elif notfound:
        f1sc(d, 240, 104, "Nie znaleziono", 2, UI_RED)
        f1c(d, 240, 138, "Sprawdz poprawnosc znaku", dim)
        f1c(d, 240, 162, "Zrodlo: " + (source or "-"), dim)
    else:
        f1sc(d, 240, 44, callsign, 3, accent)
        d.line([px + 18, py + 78, px + pw - 18, py + 78], fill=border)
        labels = ["Imie", "QTH", "Grid", "Kraj"]
        vals = []
        for label, val in fields:
            vals.append(val if val else "---")
        while len(vals) < 4:
            vals.append("---")
        ly = py + 86
        for i in range(4):
            f1(d, px + 18, ly, labels[i], dim)
            f1(d, px + 84, ly, vals[i], text)
            ly += 16
        f1(d, px + 18, ly, "Dyst", dim)
        if have_dist:
            dirs = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]
            di = int((bearing + 22.5) / 45.0) & 7 if bearing is not None else 0
            dv = "%d km  %s" % (distance, dirs[di])
            f1(d, px + 84, ly, dv, wave)
        else:
            f1(d, px + 84, ly, "--- (brak QTH)", dim)
        ly += 18
        src = "Zrodlo: " + (source or "-")
        if source_time:
            src += "  " + source_time
        f1(d, px + 18, ly, src, dim)

    bx, by, bw, bh = 152, 256, 176, 28
    d.rounded_rectangle([bx, by, bx + bw - 1, by + bh - 1], radius=8, fill=accent)
    d.rounded_rectangle([bx, by, bx + bw - 1, by + bh // 2], radius=8,
                        fill=blend(accent, WHITE, 60))
    d.rounded_rectangle([bx, by, bx + bw - 1, by + bh - 1], radius=8,
                        outline=blend(accent, WHITE, 120))
    f1sc(d, bx + bw // 2, by + bh // 2 - 7, "SZUKAJ", 2, BLACK if classic else UI_BG)

def loading(d, callsign, classic):
    card(d, classic)
    f1sc(d, 240, 110, callsign, 2, CYAN if classic else UI_ACCENT)
    f1c(d, 240, 150, "Pobieranie danych...", WHITE if classic else UI_TEXT)
    f1c(d, 240, 176, "kolejno: QRZ.com -> HamDB -> callook", LIGHTGREY if classic else UI_DIM)



def render(classic):
    im = Image.new("RGB", (480, 320), BLACK)
    d = ImageDraw.Draw(im)
    return im, d


def save(im, name):
    p = os.path.join(OUT, name)
    im.save(p)
    print("zapisano:", p, im.size)


# --- 4 stany (nowoczesny) ---
im, d = render(False)
header(d, "STACJA", False)
results(d, False, "", "", [])
footer(d, False)
save(im, "stacja-1-start.png")

im, d = render(False)
header(d, "STACJA", False)
keyboard(d, "SP9T", False)
footer(d, False)
save(im, "stacja-2-klawiatura.png")

im, d = render(False)
header(d, "STACJA", False)
loading(d, "SP9TNV", False)
footer(d, False)
save(im, "stacja-3-szukanie.png")

im, d = render(False)
header(d, "STACJA", False)
results(d, False, "SP9TNV", "QRZ.com",
        [("Imie: ", "Jan Kowalski"), ("QTH:  ", "Krakow"), ("Grid: ", "KN09ax"),
         ("Kraj: ", "Poland")], distance=42, source_time="14:32", bearing=42)
footer(d, False)
save(im, "stacja-4-wynik.png")

# --- wariant klasyczny (start + wynik) ---
im, d = render(True)
header(d, "STACJA", True)
results(d, True, "", "", [])
footer(d, True)
save(im, "stacja-5-klasyczny-start.png")

im, d = render(True)
header(d, "STACJA", True)
results(d, True, "DL1ABC", "HamDB",
        [("Imie: ", "Max Mustermann"), ("QTH:  ", "Berlin"), ("Grid: ", "JO62qm"),
         ("Kraj: ", "Germany")], distance=510, source_time="09:07", bearing=287)
footer(d, True)
save(im, "stacja-6-klasyczny-wynik.png")

im, d = render(False)
header(d, "STACJA", False)
results(d, False, "SP9ZZZ", "-", [], notfound=True)
footer(d, False)
save(im, "stacja-7-nieznaleziony.png")

# --- sklejka 2x4 ---
names = ["stacja-1-start.png", "stacja-2-klawiatura.png", "stacja-3-szukanie.png",
         "stacja-4-wynik.png", "stacja-5-klasyczny-start.png", "stacja-6-klasyczny-wynik.png",
         "stacja-7-nieznaleziony.png"]
sheet = Image.new("RGB", (480 * 2 + 30, 320 * 4 + 40), (40, 40, 40))
for i, n in enumerate(names):
    img = Image.open(os.path.join(OUT, n))
    x = (i % 2) * (480 + 10) + 10
    y = (i // 2) * (320 + 10) + 10
    sheet.paste(img, (x, y))
save(sheet, "stacja-podglad.png")
