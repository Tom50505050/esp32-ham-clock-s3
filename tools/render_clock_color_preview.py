"""Podglad ustawienia "KOLOR ZEGARA" (styl nowoczesny, ekran HAM CLOCK).

Renderuje 3 panele 1:1 (480x320) realnymi bitmapami fontow TFT_eSPI:
  1. USTAWIENIA TFT - suwak "KOLOR ZEGARA" (zielony) + kropka-podglad,
  2. USTAWIENIA TFT - ten sam suwak z domyslnym zoltym (0xFFE0),
  3. Menu pelnego koloru (HSV) - naglowek "KOLOR ZEGARA" + 3 suwaki + przyciski.
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


F2 = load_font(LIB + "Font16.c", "f16", 16, 96)


def load_glcd():
    txt = open(LIB + "glcdfont.c", encoding="latin-1").read()
    body = txt[txt.index("{"):]
    vals = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]+)", body)]
    return {i: vals[i * 5:i * 5 + 5] for i in range(256)}


GLCD = load_glcd()


def draw_f1(d, x, y, s, color):
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


def draw_f2(d, x, y, s, color):
    wid, glyphs = F2[0], F2[1]
    for ch in s:
        c = ord(ch)
        if c < 32 or c > 127:
            c = 32
        w = wid[c - 32]
        g = glyphs.get(c)
        if g:
            for row in range(16):
                bits = g[row]
                for col in range(w):
                    if bits & (0x80 >> col):
                        d.point((x + col, y + row), fill=color)
        x += w
    return x


def c565(v):
    return (((v >> 11) & 0x1F) * 255 // 31, ((v >> 5) & 0x3F) * 255 // 63, (v & 0x1F) * 255 // 31)


def color_wheel(hue):
    # Odpowiednik colorWheel() z TFT_eSPI: HSV(hue,255,255) -> RGB565 -> RGB.
    h = (hue % 256) * 360 // 256
    f = h % 60
    s = 255
    v = 255
    p = v * (255 - s) // 255
    q = v * (255 - (s * f) // 60) // 255
    t = v * (255 - (s * (60 - f)) // 60) // 255
    if h < 60:
        r, g, b = v, t, p
    elif h < 120:
        r, g, b = q, v, p
    elif h < 180:
        r, g, b = p, v, t
    elif h < 240:
        r, g, b = p, q, v
    elif h < 300:
        r, g, b = t, p, v
    else:
        r, g, b = v, p, q
    rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    return c565(rgb565)


def rgb565_to_hue(v):
    # Przyblizony hue (0..255) z RGB565 - do ustawienia pozycji galki.
    r = ((v >> 11) & 0x1F) * 255 // 31
    g = ((v >> 5) & 0x3F) * 255 // 63
    b = (v & 0x1F) * 255 // 31
    mx, mn = max(r, g, b), min(r, g, b)
    d = mx - mn
    if d == 0:
        return 0
    if mx == r:
        h = (60 * (g - b) // d) % 360
    elif mx == g:
        h = 60 * (b - r) // d + 120
    else:
        h = 60 * (r - g) // d + 240
    return (h % 360) * 256 // 360


UI_BG = c565(0x0000)
UI_CARD = c565(0x0884)
UI_BORDER = c565(0x298A)
UI_ACCENT = c565(0x3697)
UI_ACCENT2 = c565(0x4D5F)
UI_TEXT = c565(0xEF7E)
WHITE = (255, 255, 255)
LIGHTGREY = c565(0xC618)
YELLOW = c565(0xFFE0)
GREEN = c565(0x07E0)
BLACK = (0, 0, 0)


def menu_header(d, title):
    d.rectangle([0, 0, 479, 29], fill=UI_CARD)
    d.line([0, 29, 479, 29], fill=UI_ACCENT)
    draw_f1(d, 10, 11, title, UI_TEXT)


def tile(d, x, y, w, h, label, active):
    fill = UI_ACCENT if active else UI_CARD
    border = UI_ACCENT if active else UI_BORDER
    txt = UI_BG if active else UI_TEXT
    d.rectangle([x, y, x + w - 1, y + h - 1], fill=fill, outline=border)
    lw = len(label) * 6
    draw_f1(d, x + (w - lw) // 2, y + (h - 8) // 2, label, txt)


def slider(d, y, mode, hue, sat, val, knob_value, maxv, preview565):
    sx, sw, sh, knobW = 40, 400, 14, 6
    d.rectangle([sx - knobW, y - 6, sx + sw + knobW - 1, y + sh + 5], fill=BLACK)
    for i in range(sw):
        if mode == 0:
            c = color_wheel((i * 255) // (sw - 1))
        elif mode == 1:
            c = color_wheel(hue)  # uproszczenie: pasek nasycenia
            # realnie hsv565(hue, i*100/(sw-1), val) - przybliz jasnosc
            base = color_wheel(hue)
            c = tuple(int(base[k] * (0.25 + 0.75 * (i / (sw - 1)))) for k in range(3))
        else:
            base = color_wheel(hue)
            c = tuple(int(base[k] * (i / (sw - 1))) for k in range(3))
        d.point((sx + i, y + sh // 2), fill=c)
        d.line([sx + i, y + 1, sx + i, y + sh - 2], fill=c)
    d.rectangle([sx, y, sx + sw - 1, y + sh - 1], outline=WHITE)
    knobX = sx + (knob_value * (sw - knobW)) // maxv
    d.rectangle([knobX, y - 4, knobX + knobW - 1, y + sh + 3], fill=WHITE)


def settings_panel(clock565, label):
    im = Image.new("RGB", (480, 320), UI_BG)
    d = ImageDraw.Draw(im)
    menu_header(d, "USTAWIENIA TFT")
    draw_f1(d, 10, 40, "Przytrzymaj 3 sek = Kalibracja", LIGHTGREY)
    # Suwak jasnosci
    draw_f1(d, 10, 70, "JASNOSC:", LIGHTGREY)
    draw_f1(d, 120, 70, "100%", LIGHTGREY)
    d.rectangle([40, 84, 439, 95], fill=BLACK)
    d.rectangle([41, 85, 41 + 398, 94], fill=c565(0xFD20))
    d.rectangle([434, 86, 439, 97], fill=WHITE)
    # Suwak KOLOR ZEGARA
    draw_f1(d, 10, 110, label, LIGHTGREY)
    slider(d, 130, 0, rgb565_to_hue(clock565), 100, 100,
           rgb565_to_hue(clock565), 255, clock565)
    d.rectangle([448, 124, 473, 141], fill=c565(clock565), outline=WHITE)
    draw_f1(d, 10, 150, "JEZYK", LIGHTGREY)
    draw_f1(d, 195, 240, "PL", WHITE)
    draw_f1(d, 275, 240, "EN", WHITE)
    d.rectangle([195, 240, 264, 261], outline=UI_BORDER)
    d.rectangle([275, 240, 344, 261], outline=UI_BORDER)
    for i, lab in enumerate(["ZAPISZ", "DOMYSLNE", "USPIENIE", "ZAMKNIJ"]):
        tile(d, 10 + i * 100, 190, 90, 26, lab, False)
    return im


def color_menu_panel(clock565):
    im = Image.new("RGB", (480, 320), UI_BG)
    d = ImageDraw.Draw(im)
    menu_header(d, "KOLOR ZEGARA")
    # Podglad zegara (cyfry duze - przyblizone)
    d.rectangle([0, 36, 479, 85], fill=BLACK)
    draw_f2(d, 190, 44, "12:34:56", c565(clock565))
    hue = rgb565_to_hue(clock565)
    draw_f1(d, 10, 106, "ODCIEN", LIGHTGREY)
    draw_f1(d, 10, 146, "NASYCENIE", LIGHTGREY)
    draw_f1(d, 10, 186, "JASNOSC", LIGHTGREY)
    slider(d, 120, 0, hue, 100, 100, hue, 255, clock565)
    slider(d, 160, 1, hue, 100, 100, 100, 100, clock565)
    slider(d, 200, 2, hue, 100, 100, 100, 100, clock565)
    tile(d, 15, 250, 110, 28, "ZAPISZ", False)
    tile(d, 140, 250, 110, 28, "DOMYSLNE", False)
    tile(d, 265, 250, 110, 28, "ZAMKNIJ", False)
    return im


GREEN565 = 0x07E0  # zielony zegar
YELLOW565 = 0xFFE0  # domyslny zolty zegar

panels = [
    ("USTAWIENIA TFT - KOLOR ZEGARA (zielony)", settings_panel(GREEN565, "KOLOR ZEGARA")),
    ("USTAWIENIA TFT - KOLOR ZEGARA (zolty, domyslny)", settings_panel(YELLOW565, "KOLOR ZEGARA")),
    ("Menu KOLOR ZEGARA (przytrzymanie zegara)", color_menu_panel(GREEN565)),
]

cols, rows = 2, 2
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
sheet.save("/workspace/share/podglad-kolor-zegara.png")
sheet.convert("RGB").save("/workspace/share/podglad-kolor-zegara.jpg", quality=90)
print("saved podglad-kolor-zegara.png", sheet.size)
