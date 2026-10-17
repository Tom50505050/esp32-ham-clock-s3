"""Podglad naglowkow TFT (480x320) 1:1 z main.cpp.

Renderuje realne bitmapy czcionek TFT_eSPI (Font 2 = Font16) oraz ikony
WiFi / baterii / GPS dokladnie tak, jak rysuje je firmware. Sluzy do
weryfikacji ukladu ikon w naglowku bez urzadzenia.

Uruchomienie:  python3 tools/render_header_preview.py
Wynik: /workspace/share/naglowek-klasyczny.png, naglowek-nowoczesny.png,
       naglowek-ui.png
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


def c565(v):
    return (((v >> 11) & 0x1F) * 255 // 31, ((v >> 5) & 0x3F) * 255 // 63, (v & 0x1F) * 255 // 31)


BLACK = (0, 0, 0)
WHITE = (255, 255, 255)
ORANGE = c565(0xFD20)       # TFT_RADIO_ORANGE
HC_BG = c565(0x0000)
HC_CYAN = c565(0x07FF)
UI_CARD = c565(0x0884)
UI_ACCENT = c565(0x3697)
UI_TEXT = c565(0xEF7E)


def draw_f2(d, x, y, s, color, bg=None):
    for ch in s:
        c = ord(ch)
        if c < 32 or c > 127:
            c = 32
        w = F2_W[c - 32]
        g = F2_G.get(c)
        if g:
            for row in range(16):
                bits = g[row]
                for col in range(w):
                    if bits & (0x80 >> col):
                        d.point((x + col, y + row), fill=color)
        x += w
    return x


def tw2(s):
    return sum(F2_W[ord(c) - 32] if 32 <= ord(c) <= 127 else 6 for c in s)


# --- Ikony (odwzorowanie funkcji z main.cpp) ---

def wifi_bars(d, x, y, strength=4):
    cols = [c565(0xF800), c565(0xFD20), c565(0xFFE0), c565(0x07E0)]
    empty = c565(0x7BEF)
    geo = [(0, 12, 6, 4), (8, 8, 6, 8), (16, 4, 6, 12), (24, 0, 6, 16)]
    for i, (dx, dy, w, h) in enumerate(geo):
        if strength >= i + 1:
            d.rectangle([x + dx, y + dy, x + dx + w - 1, y + dy + h - 1], fill=cols[i])
        else:
            d.rectangle([x + dx, y + dy, x + dx + w - 1, y + dy + h - 1], outline=empty)


def battery_icon(d, x, y, percentage, bg):
    """drawBatteryIcon(x,y): obudowa 20x10 @ (x, y+2) + terminal 3x6 @ (x+20, y+4)."""
    if percentage <= 20:
        col = c565(0xF800)
    elif percentage <= 50:
        col = c565(0xFD20)
    elif percentage <= 75:
        col = c565(0xFFE0)
    else:
        col = c565(0x07E0)
    bw, bh = 20, 10
    d.rectangle([x, y + 2, x + bw - 1, y + 2 + bh - 1], outline=WHITE)
    d.rectangle([x + bw, y + 4, x + bw + 2, y + 4 + 5], fill=WHITE)
    fillw = (bw - 4) * percentage // 100
    if fillw > 0:
        d.rectangle([x + 2, y + 4, x + 2 + fillw - 1, y + 4 + (bh - 4) - 1], fill=col)


def battery_quick(d, screenW, percentage, bg):
    """drawBatteryQuickUpdate(): x=screenW-120, y=10, 26x12 + tekst % @ x+32."""
    x, y, w, h = screenW - 120, 10, 26, 12
    d.rectangle([x - 2, y - 2, x - 2 + (w + 6) - 1, y - 2 + (h + 4) - 1], fill=bg)
    if percentage <= 20:
        col = c565(0xF800)
    elif percentage <= 50:
        col = c565(0xFD20)
    elif percentage <= 75:
        col = c565(0xFFE0)
    else:
        col = c565(0x07E0)
    d.rectangle([x, y, x + w - 1, y + h - 1], outline=WHITE)
    d.rectangle([x + w, y + h // 4, x + w + 2, y + h // 4 + h // 2 - 1], fill=WHITE)
    fillw = (w - 4) * percentage // 100
    if fillw > 0:
        d.rectangle([x + 2, y + 2, x + 2 + fillw - 1, y + 2 + (h - 4) - 1], fill=col)
    d.rectangle([x + 32, y + 3, x + 32 + 30 - 1, y + 3 + 8 - 1], fill=bg)
    # "%d%%" w Font 1 (5x7) ~ przyblizenie 6 px/znak
    draw_f1(d, x + 32, y + 3, "%d%%" % percentage, WHITE)


GLCD = None


def load_glcd():
    txt = open(LIB + "glcdfont.c", encoding="latin-1").read()
    body = txt[txt.index("{"):]
    vals = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]+)", body)]
    return {i: vals[i * 5:i * 5 + 5] for i in range(256)}


def draw_f1(d, x, y, s, color):
    global GLCD
    if GLCD is None:
        GLCD = load_glcd()
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


def gps_glyph(d, x, y, ink, bg):
    d.rectangle([x, y + 6, x + 8, y + 13], fill=ink)
    d.line([x + 4, y + 6, x + 4, y + 13], fill=bg)
    d.rectangle([x + 17, y + 6, x + 25, y + 13], fill=ink)
    d.line([x + 21, y + 6, x + 21, y + 13], fill=bg)
    d.polygon([(x + 9, y + 10), (x + 13, y + 6), (x + 17, y + 10)], fill=ink)
    d.polygon([(x + 9, y + 10), (x + 17, y + 10), (x + 13, y + 14)], fill=ink)
    d.line([x + 13, y + 2, x + 13, y + 5], fill=ink)
    d.ellipse([x + 12, y + 1, x + 14, y + 3], fill=ink)


def gps_icon(d, x, y, classic, bg):
    plate = c565(0x18E3) if classic else c565(0x0000)
    d.rectangle([x - 3, y - 2, x - 3 + (26 + 6) - 1, y - 2 + (18 + 4) - 1], fill=plate)
    gps_glyph(d, x, y, c565(0x07E0), plate)


# --- Naglowki ---

def classic_header():
    im = Image.new("RGB", (480, 40), ORANGE)
    d = ImageDraw.Draw(im)
    d.line([0, 39, 479, 39], fill=c565(0x8410))
    p1, p2 = "ESP32", "-HAM-CLOCK"
    startX = (480 - (tw2(p1) + tw2(p2))) // 2
    d.rectangle([startX - 6, 9, startX - 6 + tw2(p1) + 12 - 1, 9 + 22 - 1], fill=BLACK)
    draw_f2(d, startX, 12, p1, ORANGE)
    draw_f2(d, startX + tw2(p1), 12, p2, BLACK)
    wifi_bars(d, 435, 8)
    battery_icon(d, 410, 8, 60, ORANGE)
    gps_icon(d, 88, 9, True, ORANGE)
    d.rectangle([8, 10, 8 + 60 - 1, 10 + 20 - 1], fill=ORANGE)
    draw_f2(d, 10, 12, "12:34", BLACK)
    return im


def modern_header():
    im = Image.new("RGB", (480, 40), HC_BG)
    d = ImageDraw.Draw(im)
    d.line([0, 39, 479, 39], fill=HC_CYAN)
    logo = "ESP32-HAM-CLOCK"
    draw_f2(d, (480 - tw2(logo)) // 2, 12, logo, WHITE)
    wifi_bars(d, 435, 8)
    battery_icon(d, 410, 8, 60, HC_BG)
    gps_icon(d, 88, 9, False, HC_BG)
    draw_f2(d, 10, 12, "12:34", HC_CYAN)
    return im


def ui_header():
    im = Image.new("RGB", (480, 40), UI_CARD)
    d = ImageDraw.Draw(im)
    d.line([0, 25, 479, 25], fill=UI_ACCENT)
    title = "POTA.app"
    draw_f2(d, 240 - tw2(title) // 2, 5, title, UI_TEXT)
    d.rectangle([2, 1, 27, 25], fill=UI_CARD, outline=c565(0x4D5F))
    for i in range(3):
        d.rectangle([5, 5 + i * 6, 25, 7 + i * 6], fill=c565(0x4D5F))
    wifi_bars(d, 445, 1)
    battery_icon(d, 420, 1, 60, UI_CARD)
    return im


def with_battery_quick(im):
    """Naklada drawBatteryQuickUpdate() na naglowek zegara (jak w loop())."""
    d = ImageDraw.Draw(im)
    battery_quick(d, 480, 60, BLACK if im.getpixel((10, 12)) == HC_BG else ORANGE)
    return im


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, img in [("naglowek-klasyczny", classic_header()),
                      ("naglowek-nowoczesny", modern_header()),
                      ("naglowek-ui", ui_header())]:
        img.resize((1440, 120), Image.NEAREST).save(os.path.join(OUT, name + ".png"))
        print("zapisano", name + ".png")
    # Wersja z nakladka drawBatteryQuickUpdate (jak na zywo na zegarze)
    for name, img in [("naglowek-klasyczny-live", with_battery_quick(classic_header())),
                      ("naglowek-nowoczesny-live", with_battery_quick(modern_header()))]:
        img.resize((1440, 120), Image.NEAREST).save(os.path.join(OUT, name + ".png"))
        print("zapisano", name + ".png")


if __name__ == "__main__":
    main()
