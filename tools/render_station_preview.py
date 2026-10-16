"""Podglad ekranu TFT "STACJA" (480x320) 1:1 z main.cpp.

Renderuje realne bitmapy czcionek TFT_eSPI (Font 2 = Font16, Font 1 = GLCD 5x7),
wiec uklad zgadza sie z urzadzeniem. Wynik: PNG w /workspace/share/.
"""
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
    # setTextSize(2)+drawString(...,165,273) MC_DATUM => GLCD 2x wyśrodkowane
    f1sc(d, 165, 273, "SZUKAJ", 2, BLACK if classic else UI_BG)


def card(d, classic):
    d.rectangle([20, 60, 459, 299], fill=BLACK if classic else UI_CARD,
                outline=(BLUE if classic else UI_BORDER))
    d.rectangle([21, 61, 458, 298], outline=(NAVY if classic else UI_FAINT))


def results(d, classic, callsign, source, fields, distance=None, notfound=False):
    card(d, classic)
    text = WHITE if classic else UI_TEXT
    dim = LIGHTGREY if classic else UI_DIM
    accent = YELLOW if classic else UI_ACCENT
    if not callsign:
        # setTextSize(2) + drawString MC_DATUM (240,130) => GLCD 2x
        f1sc(d, 240, 130, "Podaj znak stacji", 2, accent)
        # setTextSize(1) + drawString MC_DATUM (240,165)
        f1c(d, 240, 165, "nacisnij SZUKAJ i wpisz znak", dim)
    else:
        f1s(d, 34, 72, callsign, 2, accent)              # setTextSize(2) TL (34,72)
        f1(d, 360, 74, "Zrodlo: " + (source or "-"), dim)  # setTextSize(1) TL (360,74)
        if notfound:
            f1s(d, 34, 120, "Nie znaleziono stacji", 2, UI_RED)
            f1(d, 34, 160, "Sprawdz poprawnosc znaku", dim)
            f1(d, 34, 185, "Zrodlo: " + (source or "-"), dim)
        else:
            y = 116
            for label, val in fields:
                f1s(d, 34, y, "%s %s" % (label, val if val else "---"), 2, text)
                y += 30
            if distance is not None:
                f1s(d, 34, y, "Dyst:  %d km" % distance, 2, accent)
    search_button(d, classic)


def loading(d, callsign, classic):
    card(d, classic)
    f1sc(d, 240, 110, callsign, 2, CYAN if classic else UI_ACCENT)
    f1c(d, 240, 150, "Pobieranie danych...", WHITE if classic else UI_TEXT)
    f1c(d, 240, 176, "kolejno: QRZ.com -> HamDB -> callook", LIGHTGREY if classic else UI_DIM)
    search_button(d, classic)


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
         ("Kraj: ", "Poland")], distance=42)
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
         ("Kraj: ", "Germany")], distance=510)
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
