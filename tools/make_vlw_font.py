#!/usr/bin/env python3
"""Generator fontu VLW (TFT_eSPI Smooth Font) z pliku TTF.

Uzycie:
  python3 tools/make_vlw_font.py <ttf> <rozmiar_pt> <wyjscie.vlw> [xscale]

`xscale` (domyslnie 1.0) zweza glify w poziomie - pozwala uzyc wiekszego
rozmiaru pionowego (wyzsze litery) i nadal zmiescic dlugie napisy w 480 px.

Format VLW (wszystko big-endian):
  naglowek 6 x uint32: gCount, wersja(11), rozmiar_pt, 0, ascent, descent
  metryki gCount x 7 x int32: unicode, h, w, xAdvance, dY, dX, 0
  bitmapy: kolejno w h*w bajtow (alpha 8-bit)
  stopka: nazwa (len+tekst), nazwa postscript (len+tekst), 1 bajt anti-aliased
"""
import struct
import sys

from PIL import Image, ImageDraw, ImageFont


def build(ttf_path: str, pt: int, out_path: str, xscale: float = 1.0) -> None:
    font = ImageFont.truetype(ttf_path, pt)
    ascent, descent = font.getmetrics()

    # Zakres: ASCII + Latin-1 + Latin Extended-A (polskie znaki).
    codepoints = list(range(0x20, 0x7F)) + list(range(0xA0, 0x100)) + list(range(0x100, 0x180))
    codepoints = sorted(set(codepoints))

    glyphs = []  # (unicode, w, h, adv, dy, dx, bitmap_bytes)
    for cp in codepoints:
        ch = chr(cp)
        try:
            bbox = font.getbbox(ch)
        except Exception:
            bbox = (0, 0, 0, 0)
        x0, y0, x1, y1 = bbox
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
            w = 0
            h = 0
        glyphs.append((cp, w, h, adv, dy, dx, data))

    with open(out_path, "wb") as f:
        f.write(struct.pack(">IIIIII", len(glyphs), 11, pt, 0, ascent, descent))
        for cp, w, h, adv, dy, dx, _ in glyphs:
            f.write(struct.pack(">IIIIiiI", cp, h, w, adv, dy, dx, 0))
        for _, _, _, _, _, _, data in glyphs:
            f.write(data)
        name = b"DejaVuSansCustom"
        f.write(bytes([len(name)]) + name)
        f.write(bytes([len(name)]) + name)
        f.write(b"\x01")  # anti-aliased

    print(f"{out_path}: {len(glyphs)} glifow, ascent={ascent}, descent={descent}, xscale={xscale}")


if __name__ == "__main__":
    if len(sys.argv) not in (4, 5):
        print(__doc__)
        sys.exit(1)
    scale = float(sys.argv[4]) if len(sys.argv) == 5 else 1.0
    build(sys.argv[1], int(sys.argv[2]), sys.argv[3], scale)
