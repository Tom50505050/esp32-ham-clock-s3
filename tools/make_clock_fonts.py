#!/usr/bin/env python3
"""Generator czcionek zegara (VLW) - wybor wygladu znaku i godziny.

Pobiera zestaw fontow (Google Fonts, licencja OFL) do cache i generuje dla
kazdego DWA pliki VLW (TFT_eSPI Smooth Font) w `data/fonts/`:

  clockNN-call.vlw  - mniejszy rozmiar dla znaku (callsign)
  clockNN-time.vlw  - wiekszy rozmiar dla duzego zegara cyfrowego

NN to numer opcji w menu (01..). Opcja 0 w firmware to wbudowana czcionka
TFT_eSPI (bez pliku). Rozmiary sa dobierane automatycznie tak, aby
"00:00:00" oraz dlugi znak zmiescily sie w szerokosci ekranu (480 px).

Uzycie:
  python3 tools/make_clock_fonts.py            # generuje wszystkie
  python3 tools/make_clock_fonts.py 3 5        # tylko opcje 3..5 (1-based)
"""
import os
import struct
import sys
import urllib.request

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "data", "fonts")
CACHE_DIR = os.path.join(ROOT, "tools", "fonts_src")

# Znaki potrzebne na ekranie zegara: cyfry, separator czasu, znaki wywolania.
CHARSET = "0123456789:./-+ ABCDEFGHIJKLMNOPQRSTUVWXYZ"

# Maksymalne wymiary (px) dla kazdego z dwoch rozmiarow.
TIME_MAX_H, TIME_MAX_W = 60, 400     # "00:00:00"
CALL_MAX_H, CALL_MAX_W = 38, 400     # najszerszy znak (same W)

GF = "https://raw.githubusercontent.com/google/fonts/main/ofl/"

# (etykieta, url, plik lokalny, wariant osi [variable], skala pozioma)
FONTS = [
    ("Sans Bold",  GF + "inter/Inter%5Bopsz%2Cwght%5D.ttf", "Inter.ttf", "Bold", 1.0),
    ("Orbitron",   GF + "orbitron/Orbitron%5Bwght%5D.ttf", "Orbitron.ttf", "Bold", 1.0),
    ("Oxanium",    GF + "oxanium/Oxanium%5Bwght%5D.ttf", "Oxanium.ttf", "Bold", 1.0),
    ("Michroma",   GF + "michroma/Michroma-Regular.ttf", "Michroma.ttf", None, 1.0),
    ("Audiowide",  GF + "audiowide/Audiowide-Regular.ttf", "Audiowide.ttf", None, 1.0),
    ("Russo One",  GF + "russoone/RussoOne-Regular.ttf", "RussoOne.ttf", None, 1.0),
    ("Tech Mono",  GF + "sharetechmono/ShareTechMono-Regular.ttf", "ShareTechMono.ttf", None, 1.0),
    ("Quantico",   GF + "quantico/Quantico-Bold.ttf", "Quantico-Bold.ttf", None, 1.0),
    ("Electrolize", GF + "electrolize/Electrolize-Regular.ttf", "Electrolize.ttf", None, 1.0),
    ("Bungee",     GF + "bungee/Bungee-Regular.ttf", "Bungee.ttf", None, 1.0),
    ("Nova Mono",  GF + "novamono/NovaMono.ttf", "NovaMono.ttf", None, 1.0),
    ("Iceland",    GF + "iceland/Iceland-Regular.ttf", "Iceland.ttf", None, 1.0),
    ("Serif Bold", "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf", None, None, 1.0),
    ("Mono Bold",  "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", None, None, 1.0),
]


def fetch(url: str, dest: str) -> str:
    if url.startswith("/"):
        return url
    os.makedirs(CACHE_DIR, exist_ok=True)
    if not os.path.exists(dest):
        print(f"  pobieranie {url}")
        urllib.request.urlretrieve(url, dest)
    return dest


def load_font(ttf: str, pt: int, variation):
    f = ImageFont.truetype(ttf, pt)
    if variation:
        try:
            f.set_variation_by_name(variation)
        except Exception:
            pass
    return f


def fits(f, text: str, max_w: int, max_h: int) -> bool:
    asc, desc = f.getmetrics()
    if asc + desc > max_h:
        return False
    return f.getlength(text) <= max_w


def pick_size(ttf: str, variation, text: str, max_w: int, max_h: int,
              lo: int, hi: int) -> int:
    best = lo
    for pt in range(lo, hi + 1):
        if fits(load_font(ttf, pt, variation), text, max_w, max_h):
            best = pt
    return best


def build_vlw(ttf: str, pt: int, variation, out_path: str, xscale: float = 1.0) -> None:
    font = load_font(ttf, pt, variation)
    ascent, descent = font.getmetrics()
    glyphs = []
    for ch in CHARSET:
        cp = ord(ch)
        try:
            x0, y0, x1, y1 = font.getbbox(ch)
        except Exception:
            x0 = y0 = x1 = y1 = 0
        w = max(0, x1 - x0)
        h = max(0, y1 - y0)
        adv = int(round(font.getlength(ch) * xscale))
        dx = int(round(x0 * xscale))
        dy = ascent - y0
        if w > 0 and h > 0:
            img = Image.new("L", (w, h), 0)
            ImageDraw.Draw(img).text((-x0, -y0), ch, fill=255, font=font)
            if xscale != 1.0:
                w2 = max(1, int(round(w * xscale)))
                img = img.resize((w2, h), Image.LANCZOS)
                w = w2
            data = img.tobytes()
        else:
            data = b""
            w = h = 0
        glyphs.append((cp, w, h, adv, dy, dx, data))

    with open(out_path, "wb") as f:
        f.write(struct.pack(">IIIIII", len(glyphs), 11, pt, 0, ascent, descent))
        for cp, w, h, adv, dy, dx, _ in glyphs:
            f.write(struct.pack(">IIIIiiI", cp, h, w, adv, dy, dx, 0))
        for _, _, _, _, _, _, data in glyphs:
            f.write(data)
        name = b"ClockFont"
        f.write(bytes([len(name)]) + name)
        f.write(bytes([len(name)]) + name)
        f.write(b"\x01")


def main() -> None:
    os.makedirs(OUT_DIR, exist_ok=True)
    wanted = None
    if len(sys.argv) == 3:
        wanted = set(range(int(sys.argv[1]), int(sys.argv[2]) + 1))

    total = 0
    for i, (label, url, fname, variation, xscale) in enumerate(FONTS, start=1):
        if wanted and i not in wanted:
            continue
        ttf = fetch(url, os.path.join(CACHE_DIR, fname)) if fname else url
        call_pt = pick_size(ttf, variation, "WWWWWWWWW", CALL_MAX_W, CALL_MAX_H, 22, 42)
        time_pt = pick_size(ttf, variation, "00:00:00", TIME_MAX_W, TIME_MAX_H, 34, 64)
        call_out = os.path.join(OUT_DIR, f"clock{i:02d}-call.vlw")
        time_out = os.path.join(OUT_DIR, f"clock{i:02d}-time.vlw")
        build_vlw(ttf, call_pt, variation, call_out, xscale)
        build_vlw(ttf, time_pt, variation, time_out, xscale)
        cs = os.path.getsize(call_out)
        ts = os.path.getsize(time_out)
        total += cs + ts
        print(f"[{i:02d}] {label:12s} call={call_pt}pt ({cs} B)  time={time_pt}pt ({ts} B)")
    print(f"Razem: {total} B ({total/1024/1024:.2f} MB)")


if __name__ == "__main__":
    main()
