"""Podglad ekranu TFT "ESP32 HAM LOGGER" (480x320) 1:1 z main.cpp.

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
    f1(d, cx - len(s) * 6 // 2, y, s, col)


def f1s(d, x, y, s, scale, col):
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
    d.rectangle([420, 6, 437, 20], outline=WHITE)
    d.rectangle([438, 10, 440, 16], fill=WHITE)
    d.rectangle([422, 8, 433, 18], fill=GREEN)
    for i, hh in enumerate([4, 7, 10, 13]):
        d.rectangle([443 + i * 5, 18 - hh, 446 + i * 5, 18], fill=WHITE)


def footer(d, classic):
    col = RADIO_ORANGE if classic else UI_ACCENT
    d.polygon([(14, 309), (24, 299), (24, 319)], fill=col)
    d.polygon([(466, 309), (456, 299), (456, 319)], fill=col)
    f1c(d, 240, 300, "SWITCH SCREEN", UI_FAINT)


BANDS = ["160m", "80m", "40m", "30m", "20m", "17m", "15m", "10m", "6m"]
MODES = ["SSB", "CW", "FT8", "RTTY", "AM", "FM"]


def form(d, classic, call="", rsts="59", rstr="57", band="40m", freq="7.145",
         mode="SSB", name="", qth="", stamp="2026-10-17 10:59 UTC", status="",
         recent=None):
    bg = BLACK if classic else UI_BG
    panel = BLACK if classic else UI_CARD
    border = BLUE if classic else UI_BORDER
    text = WHITE if classic else UI_TEXT
    dim = LIGHTGREY if classic else UI_DIM
    accent = YELLOW if classic else UI_ACCENT
    accent2 = CYAN if classic else UI_ACCENT2

    d.rectangle([0, 26, 479, 299], fill=bg)
    d.rounded_rectangle([8, 28, 471, 233], radius=8, fill=panel, outline=border)

    def label(l, x, y):
        f1(d, x, y, l, dim)

    def value(v, x, y, col):
        f1(d, x, y, v, col)

    label("CALLSIGN", 18, 38)
    d.rectangle([90, 33, 297, 52], outline=YELLOW)
    f1s(d, 96, 35, call if call else "____", 2, YELLOW)

    label("RST (S)", 18, 62)
    value(rsts, 90, 62, text)
    label("RST (R)", 160, 62)
    value(rstr, 232, 62, text)

    label("BAND", 18, 84)
    value(band, 90, 84, accent)
    label("FREQ", 160, 84)
    value(freq + " MHz", 210, 84, text)
    label("MODE", 340, 84)
    value(mode, 390, 84, accent)

    label("NAME", 18, 106)
    value(name if name else "---", 90, 106, text)
    label("QTH", 250, 106)
    value(qth if qth else "---", 300, 106, text)

    label("DATE/TIME", 18, 128)
    value(stamp, 90, 128, dim)

    if status:
        f1(d, 18, 148, status, accent2)

    d.line([18, 164, 461, 164], fill=border)
    f1(d, 18, 168, "Ostatnie lacznosci (Recent QSO)", accent)
    f1(d, 18, 184, "CALL", dim)
    f1(d, 130, 184, "FREQ", dim)
    f1(d, 250, 184, "MODE", dim)
    f1(d, 380, 184, "UTC", dim)
    recent = recent or []
    for i, q in enumerate(recent[:3]):
        ry = 198 + i * 12
        f1(d, 18, ry, q[0], text)
        f1(d, 130, ry, q[1], text)
        f1(d, 250, ry, q[2], text)
        f1(d, 380, ry, q[3], text)
    if not recent:
        f1(d, 18, 198, "(brak wpisow - zapisz pierwsze QSO)", dim)

    by, bh, bw = 248, 38, 111
    bx = [8, 125, 242, 359]
    bt = ["SAVE QSO", "CLEAR", "EXPORT ADIF", "SETTINGS"]
    bc = [(GREEN if classic else UI_ACCENT), (ORANGE if classic else ORANGE),
          (CYAN if classic else UI_ACCENT2), (DARKGREY if classic else UI_BORDER)]
    for i in range(4):
        d.rounded_rectangle([bx[i], by, bx[i] + bw - 1, by + bh - 1], radius=8, fill=bc[i],
                            outline=(WHITE if classic else UI_FAINT))
        size = 2 if len(bt[i]) * 6 * 2 <= bw - 10 else 1
        f1sc(d, bx[i] + bw // 2, by + bh // 2 - (7 if size == 2 else 4), bt[i], size,
             BLACK if classic else UI_BG)


KB_TOP, KB_W, KB_H, KB_GAP = 118, 32, 26, 4
ROWS = ["1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM.-/:"]


def key(d, x, y, label, fill, classic, txt=WHITE):
    d.rectangle([x, y, x + KB_W - 1, y + KB_H - 1], fill=fill,
                outline=(WHITE if classic else UI_FAINT))
    f1(d, x + 12, y + 9, label, txt)


def keyboard(d, buf, field, classic):
    card = c565(0x18E3) if classic else UI_CARD
    keyc = DARKGREY if classic else UI_BORDER
    d.rectangle([20, KB_TOP, 459, KB_TOP + 194], fill=card,
                outline=(WHITE if classic else UI_ACCENT))
    f1(d, 28, KB_TOP + 16, field, WHITE if classic else UI_DIM)
    d.rectangle([90, KB_TOP + 8, 319, KB_TOP + 35], fill=BLACK)
    f1s(d, 95, KB_TOP + 14, (buf[-18:] if len(buf) > 18 else buf) + "_", 2,
        YELLOW if classic else UI_ACCENT)
    for r, row in enumerate(ROWS):
        for i, ch in enumerate(row):
            x = 84 + i * (KB_W + KB_GAP)
            y = KB_TOP + 44 + r * 26
            fill = (BLUE if classic else UI_ACCENT2) if (r == 0 or (r == 3 and i >= 7)) else keyc
            key(d, x, y, ch, fill, classic, WHITE if classic else UI_TEXT)
    d.rectangle([330, KB_TOP + 8, 387, KB_TOP + 35], fill=RED,
                outline=(WHITE if classic else UI_FAINT))
    f1(d, 344, KB_TOP + 18, "DEL", WHITE)
    d.rectangle([396, KB_TOP + 8, 449, KB_TOP + 35], fill=ORANGE,
                outline=(WHITE if classic else UI_FAINT))
    f1(d, 407, KB_TOP + 18, "CLR", WHITE)
    d.rectangle([250, KB_TOP + 158, 334, KB_TOP + 185], fill=GREEN if classic else UI_ACCENT,
                outline=(WHITE if classic else UI_ACCENT2))
    f1s(d, 278, KB_TOP + 164, "OK", 2, BLACK if classic else UI_BG)
    d.rectangle([350, KB_TOP + 158, 434, KB_TOP + 185], fill=RED,
                outline=(WHITE if classic else UI_FAINT))
    f1s(d, 386, KB_TOP + 164, "X", 2, WHITE)


def render(classic):
    im = Image.new("RGB", (480, 320), BLACK)
    d = ImageDraw.Draw(im)
    return im, d


def save(im, name):
    p = os.path.join(OUT, name)
    im.save(p)
    print("zapisano:", p, im.size)


RECENT = [("SQ9ABC", "7.145", "SSB", "10:59"),
          ("OK1KRF", "14.200", "CW", "10:41"),
          ("G4DFL", "21.200", "FT8", "10:12")]

# --- nowoczesny: pusty, wypelniony, klawiatura ---
im, d = render(False)
header(d, "ESP32 HAM LOGGER", False)
form(d, False)
footer(d, False)
save(im, "logger-1-nowy.png")

im, d = render(False)
header(d, "ESP32 HAM LOGGER", False)
form(d, False, call="SQ9ABC", rsts="59", rstr="57", band="40m", freq="7.145", mode="SSB",
     name="Janusz", qth="Knurow", status="Zapisano: SQ9ABC  (3 QSO)", recent=RECENT)
footer(d, False)
save(im, "logger-2-wpisane.png")

im, d = render(False)
header(d, "ESP32 HAM LOGGER", False)
keyboard(d, "SQ9AB", "CALLSIGN", False)
footer(d, False)
save(im, "logger-3-klawiatura.png")

# --- klasyczny ---
im, d = render(True)
header(d, "ESP32 HAM LOGGER", True)
form(d, True, call="DL1ABC", rsts="59", rstr="59", band="20m", freq="14.200", mode="CW",
     name="Max", qth="Berlin", status="Zapisano: DL1ABC  (1 QSO)",
     recent=[("DL1ABC", "14.200", "CW", "09:07")])
footer(d, True)
save(im, "logger-4-klasyczny.png")

im, d = render(True)
header(d, "ESP32 HAM LOGGER", True)
keyboard(d, "DL1AB", "CALLSIGN", True)
footer(d, True)
save(im, "logger-5-klasyczny-klawiatura.png")

# --- sklejka 2x2 ---
names = ["logger-1-nowy.png", "logger-2-wpisane.png", "logger-3-klawiatura.png", "logger-4-klasyczny.png"]
sheet = Image.new("RGB", (480 * 2 + 30, 320 * 2 + 30), (40, 40, 40))
for i, n in enumerate(names):
    img = Image.open(os.path.join(OUT, n))
    x = (i % 2) * (480 + 10) + 10
    y = (i // 2) * (320 + 10) + 10
    sheet.paste(img, (x, y))
save(sheet, "logger-podglad.png")

# --- porownanie: nowoczesny (lewo) vs klasyczny (prawo), te same dane ---
FILL = dict(call="SQ9ABC", rsts="59", rstr="57", band="40m", freq="7.145", mode="SSB",
            name="Janusz", qth="Knurow", status="Zapisano: SQ9ABC  (3 QSO)", recent=RECENT)

im, d = render(False)
header(d, "ESP32 HAM LOGGER", False)
form(d, False, **FILL)
footer(d, False)
save(im, "_cmp-now-form.png")

im, d = render(True)
header(d, "ESP32 HAM LOGGER", True)
form(d, True, **FILL)
footer(d, True)
save(im, "_cmp-kl-form.png")

im, d = render(False)
header(d, "ESP32 HAM LOGGER", False)
keyboard(d, "SQ9AB", "CALLSIGN", False)
footer(d, False)
save(im, "_cmp-now-kb.png")

im, d = render(True)
header(d, "ESP32 HAM LOGGER", True)
keyboard(d, "SQ9AB", "CALLSIGN", True)
footer(d, True)
save(im, "_cmp-kl-kb.png")

cmp_names = ["_cmp-now-form.png", "_cmp-kl-form.png", "_cmp-now-kb.png", "_cmp-kl-kb.png"]
cmp = Image.new("RGB", (480 * 2 + 30, 320 * 2 + 30), (40, 40, 40))
for i, n in enumerate(cmp_names):
    img = Image.open(os.path.join(OUT, n))
    x = (i % 2) * (480 + 10) + 10
    y = (i // 2) * (320 + 10) + 10
    cmp.paste(img, (x, y))
save(cmp, "logger-nowoczesny-vs-klasyczny.png")
for n in cmp_names:
    os.remove(os.path.join(OUT, n))
