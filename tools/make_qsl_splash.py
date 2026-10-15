#!/usr/bin/env python3
"""Generuje ekran startowy w stylu karty QSL (480x320, 24-bit BMP).

QSL = karta potwierdzenia lacznosci krotkofalarskiej: duzy znak wywolawczy,
"potwierdzam QSO", ramka airmail, godlo organizacji, dane stacji.
Wynik trafia do data/splash.bmp - firmware rysuje go w drawSplashScreen().

Uzycie:  python3 tools/make_qsl_splash.py [wyjscie.bmp]
"""
import os
import struct
import sys

from PIL import Image, ImageDraw, ImageFont, ImageFilter

W, H = 480, 320
OUT = "data/splash.bmp"

FONT_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
FONT_REG = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
FONT_SERIF_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf"

NAVY = (9, 16, 40)
NAVY2 = (16, 30, 66)
GOLD = (255, 199, 71)
GOLD_DK = (196, 140, 26)
CYAN = (86, 214, 236)
WHITE = (240, 246, 255)
RED = (206, 42, 58)
BLUE = (26, 66, 168)


def font(path, size):
    return ImageFont.truetype(path, size)


def airmail_border(im, x0, y0, x1, y1, band=7, stripe=10):
    """Klasyczna ramka airmail: skosne paski czerwono-niebieskie.

    Rysowana jako nakladka RGBA z przezroczystym srodkiem, wiec nie zaslania
    wnetrza karty.
    """
    ov = Image.new("RGBA", im.size, (0, 0, 0, 0))
    od = ImageDraw.Draw(ov)
    for i in range(-(y1 - y0), x1 - x0, stripe):
        col = RED if ((i // stripe) % 2 == 0) else BLUE
        od.line([(x0 + i, y1), (x0 + i + (y1 - y0), y0)], fill=col + (255,), width=stripe)
    # wyczysc srodek - zostaje tylko ramka o grubosci `band`
    od.rectangle([x0 + band, y0 + band, x1 - band, y1 - band], fill=(0, 0, 0, 0))
    im.paste(Image.alpha_composite(im.convert("RGBA"), ov).convert("RGB"), (0, 0))


def make_card(version="V1.2"):
    im = Image.new("RGB", (W, H), NAVY)
    d = ImageDraw.Draw(im)

    # --- tlo: pionowy gradient + delikatna poswiata ---
    for y in range(H):
        t = y / (H - 1)
        r = int(NAVY[0] + (NAVY2[0] - NAVY[0]) * t)
        g = int(NAVY[1] + (NAVY2[1] - NAVY[1]) * t)
        b = int(NAVY[2] + (NAVY2[2] - NAVY[2]) * t)
        d.line([(0, y), (W, y)], fill=(r, g, b))

    # --- delikatna mapa swiata w tle (dolna czesc) ---
    try:
        world = Image.open("data/Mapa swiata.bmp").convert("RGB")
        world = world.resize((W, H)).filter(ImageFilter.GaussianBlur(1.0))
        wpx = world.load()
        px = im.load()
        for y in range(H):
            for x in range(W):
                wr, wg, wb = wpx[x, y]
                lum = (wr + wg + wb) // 3
                if lum > 55:  # kontynenty
                    a = min(150, (lum - 55))  # mocniej niz wczesniej
                    pr, pg, pb = px[x, y]
                    px[x, y] = (pr + a // 4, pg + a // 2, pb + a)
    except Exception as e:
        print("mapa: pominieto", e)

    # --- ramka airmail ---
    airmail_border(im, 0, 0, W - 1, H - 1, band=7, stripe=10)
    d = ImageDraw.Draw(im)

    # --- wewnetrzna cienka zlota ramka ---
    d.rectangle([11, 11, W - 12, H - 12], outline=GOLD, width=1)

    # --- przygaszenie pasa na duzy napis (kontrast) ---
    band = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(band).rectangle([13, 84, W - 13, 168], fill=(0, 0, 0, 120))
    im.paste(Image.alpha_composite(im.convert("RGBA"), band).convert("RGB"), (0, 0))
    d = ImageDraw.Draw(im)

    # ================= naglowek =================
    f_sub = font(FONT_BOLD, 11)
    d.text((30, 22), "AMATEUR RADIO STATION", font=f_sub, fill=CYAN)

    # prawy gorny rog: flaga PL (bialy pas na gorze, czerwony na dole) + "POLAND"
    d.rectangle([W - 92, 22, W - 24, 40], fill=WHITE, outline=GOLD)
    d.rectangle([W - 91, 31, W - 25, 39], fill=RED)
    f_sm = font(FONT_BOLD, 10)
    d.text((W - 90, 44), "POLAND", font=f_sm, fill=WHITE)

    # ================= glowny emblemat: ESP32-HAM-CLOCK / VERSION =================
    f_big = font(FONT_SERIF_BOLD, 36)
    big = "ESP32-HAM-CLOCK"
    bw = d.textlength(big, font=f_big)
    bx = (W - bw) / 2
    glow = Image.new("RGB", (W, H), (0, 0, 0))
    ImageDraw.Draw(glow).text((bx, 92), big, font=f_big, fill=(70, 105, 45))
    glow = glow.filter(ImageFilter.GaussianBlur(6))
    im = Image.blend(im, Image.composite(glow, im, glow.convert("L")), 0.4)
    d = ImageDraw.Draw(im)
    # zloty cien (offset) + bialy napis na wierzchu
    d.text((bx + 2, 94), big, font=f_big, fill=GOLD, stroke_width=1, stroke_fill=GOLD_DK)
    d.text((bx, 92), big, font=f_big, fill=WHITE, stroke_width=1, stroke_fill=GOLD_DK)

    # pod nazwa: VERSION 1.2
    f_ver = font(FONT_BOLD, 15)
    ver = "VERSION " + version.lstrip("Vv")
    vw = d.textlength(ver, font=f_ver)
    d.text(((W - vw) / 2, 132), ver, font=f_ver, fill=CYAN)

    # ================= linia + podtytul =================
    d.line([(30, 176), (W - 30, 176)], fill=GOLD, width=2)
    f_tag = font(FONT_BOLD, 13)
    tag = "CONFIRMING TWO-WAY CONTACT  /  POTWIERDZAM QSO"
    tt = d.textlength(tag, font=f_tag)
    d.text(((W - tt) / 2, 182), tag, font=f_tag, fill=CYAN)

    # ================= dane stacji (jak pola na QSL) =================
    f_lbl = font(FONT_BOLD, 10)
    f_val = font(FONT_BOLD, 15)
    y0 = 208
    d.text((30, y0), "QTH", font=f_lbl, fill=GOLD)
    d.text((30, y0 + 13), "Poland", font=f_val, fill=WHITE)
    d.text((30, y0 + 36), "VERSION", font=f_lbl, fill=GOLD)
    d.text((30, y0 + 49), version, font=f_val, fill=WHITE)

    d.text((190, y0), "ORGANIZATION", font=f_lbl, fill=GOLD)
    d.text((190, y0 + 13), "PZK", font=f_val, fill=WHITE)
    d.text((190, y0 + 36), "BAND", font=f_lbl, fill=GOLD)
    d.text((190, y0 + 49), "HF / VHF / UHF", font=f_val, fill=WHITE)

    # prawa kolumna: godlo PZK
    try:
        logo = Image.open("data/pzk-logo.bmp").convert("RGB")
        # przeskaluj do wysokosci ~72 px
        lh = 72
        lw = int(logo.width * lh / logo.height)
        logo = logo.resize((lw, lh))
        im.paste(logo, (W - 30 - lw - 40, y0 - 4))
        f_pzk = font(FONT_BOLD, 12)
        d.text((W - 30 - lw - 40, y0 - 4 + lh + 2), "PZK", font=f_pzk, fill=CYAN)
    except Exception as e:
        print("pzk: pominieto", e)

    # ================= stopka =================
    d.line([(30, H - 30), (W - 30, H - 30)], fill=GOLD_DK, width=1)
    f_ft = font(FONT_BOLD, 12)
    d.text((30, H - 24), "ESP32-HAM-CLOCK", font=f_ft, fill=GOLD)
    f_ft2 = font(FONT_BOLD, 12)
    end = "73!"
    et = d.textlength(end, font=f_ft2)
    d.text((W - 30 - et, H - 24), end, font=f_ft2, fill=CYAN)

    return im


def save_bmp_24bit(filepath, img):
    w, h = img.size
    row_bytes = w * 3
    pad = (4 - (row_bytes % 4)) % 4
    row_size = row_bytes + pad
    pixel_data_size = row_size * h
    file_size = 14 + 40 + pixel_data_size
    px = img.load()
    with open(filepath, "wb") as f:
        f.write(b"BM")
        f.write(struct.pack("<I", file_size))
        f.write(struct.pack("<HH", 0, 0))
        f.write(struct.pack("<I", 14 + 40))
        f.write(struct.pack("<I", 40))
        f.write(struct.pack("<i", w))
        f.write(struct.pack("<i", h))       # dodatnia wysokosc = bottom-up
        f.write(struct.pack("<HH", 1, 24))  # planes=1, bpp=24
        f.write(struct.pack("<I", 0))       # BI_RGB
        f.write(struct.pack("<I", pixel_data_size))
        f.write(struct.pack("<i", 2835))
        f.write(struct.pack("<i", 2835))
        f.write(struct.pack("<I", 0))
        f.write(struct.pack("<I", 0))
        for y in range(h - 1, -1, -1):
            for x in range(w):
                r, g, b = px[x, y][:3]
                f.write(struct.pack("<BBB", b, g, r))
            f.write(b"\x00" * pad)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else OUT
    img = make_card()
    save_bmp_24bit(out, img)
    img.save("/workspace/share/podglad-qsl-splash.png")
    print(f"Zapisano {out} ({os.path.getsize(out)} B) + podglad-qsl-splash.png")
