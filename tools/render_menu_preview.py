"""Faithful preview of the new modern menu styling (TFT_eSPI Font 1 + Font 2).

Renders the POTA screen header, the TIME MODE menu and a POTA FILTERS menu using
the real TFT_eSPI font bitmaps so the layout matches the device 1:1.
"""
import re
from PIL import Image, ImageDraw

LIB = "/workspace/project/.pio/libdeps/esp32-s3-n16r8/TFT_eSPI/Fonts/"

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
    return wid, glyphs, height

F2 = load_font(LIB + "Font16.c", "f16", 16, 96)   # Font 2
F1 = load_font(LIB + "GLCDfont.c", "glcdfont", 8, 256) if False else None

def c565(v):
    return (((v >> 11) & 0x1F) * 255 // 31, ((v >> 5) & 0x3F) * 255 // 63, (v & 0x1F) * 255 // 31)

UI_BG = c565(0x0000); UI_CARD = c565(0x0884); UI_BORDER = c565(0x298A)
UI_ACCENT = c565(0x3697); UI_ACCENT2 = c565(0x4D5F); UI_WARN = c565(0xFE4A)
UI_TEXT = c565(0xEF7E); UI_DIM = c565(0x9D98); UI_FAINT = c565(0x6BF2)
WHITE = (255, 255, 255); DARKGREY = c565(0x7BEF); LIGHTGREY = c565(0xC618)
YELLOW = c565(0xFFE0); GREEN = c565(0x07E0); ALT = c565(0x0841)
ORANGE = c565(0xFD20)

def draw_f2(d, x, y, s, color, wid=None, glyphs=None, h=16):
    wid, glyphs = wid or F2[0], glyphs or F2[1]
    for ch in s:
        c = ord(ch)
        if c < 32 or c > 127: c = 32
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

def f1(d, x, y, s, col):
    d.text((x, y), s, fill=col)

# ---- Font 1 (5x7 Adafruit) needs its real glyphs too; use a pixel font fallback
def load_glcd():
    txt = open(LIB + "glcdfont.c", encoding="latin-1").read()
    body = txt[txt.index("{"):]
    vals = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]+)", body)]
    glyphs = {i: vals[i*5:i*5+5] for i in range(256)}
    return glyphs

GLCD = load_glcd()
def draw_f1(d, x, y, s, color):
    """Adafruit 5x7: each glyph 5 bytes, LSB = top row, 1 px advance after (6px)."""
    for ch in s:
        c = ord(ch)
        g = GLCD.get(c)
        if g:
            for col in range(5):
                bits = g[col]
                for row in range(8):
                    if bits & (1 << row):
                        d.point((x + col, y + row), fill=color)
        x += 6
    return x

def tile(d, x, y, w, h, label, active, classic=False):
    fill = (ORANGE if active else DARKGREY) if classic else (ORANGE if active else UI_CARD)
    border = (0,0,0) if classic else (ORANGE if active else UI_BORDER)
    txt = (0,0,0) if active else (WHITE if classic else UI_TEXT)
    d.rectangle([x, y, x + w - 1, y + h - 1], fill=fill, outline=border)
    lw = len(label) * 6
    draw_f1(d, x + (w - lw)//2, y + (h - 8)//2, label, txt)

def menu_header(d, title, classic):
    if classic:
        d.rectangle([0, 0, 479, 29], fill=ORANGE)
        d.line([0, 29, 479, 29], fill=(132, 132, 132))
        draw_f1(d, 10, 11, title, (0, 0, 0))
    else:
        d.rectangle([0, 0, 479, 29], fill=UI_CARD)
        d.line([0, 29, 479, 29], fill=ORANGE)
        draw_f1(d, 10, 11, title, UI_TEXT)

# ================= POTA screen header =================
def pota_screen(classic):
    im = Image.new("RGB", (480, 320), UI_BG); d = ImageDraw.Draw(im)
    if classic:
        d.rectangle([0, 0, 479, 25], fill=ORANGE)
        d.line([0, 25, 479, 25], fill=(132, 132, 132))
        # hamburger: czarna ikona na pomaranczowym
        d.rectangle([2, 1, 27, 25], fill=ORANGE, outline=(0, 0, 0))
        for i in range(3): d.rectangle([5, 5 + i*6, 25, 7 + i*6], fill=(0, 0, 0))
        t = "POTA.app"
        draw_f2(d, 240 - tw2(t)//2, 5, t, (0, 0, 0))
    else:
        d.rectangle([0, 0, 479, 25], fill=UI_CARD)
        d.line([0, 25, 479, 25], fill=UI_ACCENT)
        d.rectangle([2, 1, 27, 25], fill=UI_CARD, outline=UI_ACCENT2)
        for i in range(3): d.rectangle([5, 5 + i*6, 25, 7 + i*6], fill=UI_ACCENT2)
        t = "POTA.app"
        draw_f2(d, 240 - tw2(t)//2, 5, t, UI_TEXT)
    d.rectangle([420, 6, 437, 20], outline=(255, 255, 255))
    for i, hh in enumerate([4, 7, 10, 13]):
        d.rectangle([443 + i*5, 18 - hh, 446 + i*5, 18], fill=(255, 255, 255))
    y = 40
    for x, s in [(5, "UTC"), (50, "CALLSIGN"), (140, "MHz"), (260, "MODE"), (360, "COUNTRY")]:
        draw_f1(d, x, y, s, UI_FAINT)
    d.line([0, y + 10, 479, y + 10], fill=DARKGREY); y += 18
    for i, (t2, cs, fq, md, ct) in enumerate([
        ("12:34", "SP9TNV", "14.074", "SSB", "POLAND"),
        ("12:35", "DL1ABC", "7.032", "SSB", "GERMANY"),
        ("12:36", "K1ABC", "14.285", "SSB", "USA"),
        ("12:37", "JA1XYZ", "21.275", "SSB", "JAPAN"),
        ("12:38", "VK2ABC", "28.500", "SSB", "AUSTRALIA"),
        ("12:39", "F5ABC", "3.575", "SSB", "FRANCE"),
        ("12:40", "EA1ABC", "18.130", "SSB", "SPAIN")]):
        if i % 2 == 0: d.rectangle([0, y - 2, 479, y + 13], fill=ALT)
        draw_f1(d, 5, y, t2, LIGHTGREY); draw_f1(d, 50, y, cs, WHITE)
        draw_f1(d, 140, y, fq, YELLOW); draw_f1(d, 260, y, md, GREEN)
        draw_f1(d, 360, y, ct, UI_ACCENT)
        y += 16
    return im

# ================= TIME MODE menu =================
def time_mode_menu(classic):
    im = Image.new("RGB", (480, 320), UI_BG); d = ImageDraw.Draw(im)
    menu_header(d, "TIME MODE", classic)
    tw, th, gap = 110, 28, 10
    sx = (480 - (2*tw + gap))//2
    tile(d, sx, 60, tw, th, "UTC", True, classic)
    tile(d, sx + tw + gap, 60, tw, th, "LOCAL", False, classic)
    draw_f1(d, sx, 100, "WYGLAD EKRANOW (WSZYSTKIE)", LIGHTGREY)
    tile(d, sx, 114, tw, th, "NOWOCZESNY", not classic, classic)
    tile(d, sx + tw + gap, 114, tw, th, "KLASYCZNY", classic, classic)
    tile(d, (480-100)//2, 210, 100, 26, "CLOSE", False, classic)
    return im

# ================= POTA FILTERS menu =================
def pota_filters_menu(classic):
    im = Image.new("RGB", (480, 320), UI_BG); d = ImageDraw.Draw(im)
    menu_header(d, "POTA FILTERS", classic)
    draw_f1(d, 10, 36, "MODE", LIGHTGREY)
    for i, m in enumerate(["ALL", "SSB", "CW", "FT8"]):
        tile(d, 10 + i*80, 50, 70, 26, m, i == 1, classic)
    draw_f1(d, 10, 86, "BAND", LIGHTGREY)
    for i, b in enumerate(["ALL", "160m", "80m", "40m", "20m", "15m", "10m"]):
        tile(d, 10 + i*66, 100, 60, 26, b, i == 3, classic)
    return im

panels = [
    ("POTA screen - NOWOCZESNY", pota_screen(False)),
    ("POTA screen - KLASYCZNY", pota_screen(True)),
    ("TIME MODE menu - NOWOCZESNY", time_mode_menu(False)),
    ("TIME MODE menu - KLASYCZNY", time_mode_menu(True)),
    ("POTA FILTERS menu - NOWOCZESNY", pota_filters_menu(False)),
    ("POTA FILTERS menu - KLASYCZNY", pota_filters_menu(True)),
]
cols, rows = 2, 3
pad = 12
label_h = 18
cell_w = 480 + pad
cell_h = 320 + label_h + pad
sheet = Image.new("RGB", (cols * cell_w + pad, rows * cell_h + pad), (16, 16, 20))
dd = ImageDraw.Draw(sheet)
for i, (name, im) in enumerate(panels):
    r, c = divmod(i, cols)
    x = pad + c * cell_w
    y = pad + r * cell_h
    dd.text((x, y), name, fill=(200, 210, 225))
    sheet.paste(im, (x, y + label_h))
sheet = sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST)
sheet.save("/workspace/share/podglad-menu-nowoczesny.png")
print("saved podglad-menu-nowoczesny.png", sheet.size)

Image.new("RGB", (10, 10)).save("/tmp/_ok.png")
