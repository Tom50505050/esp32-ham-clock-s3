#!/usr/bin/env python3
"""Renderuje podglad ekranu powitalnego 480x320 z realnego pliku VLW.

Odwzorowuje pozycjonowanie TFT_eSPI (Smooth Font): linia bazowa + dY/dX,
postep kursora o gxAdvance, centrowanie kazdej linii wg sumy gxAdvance.

Uzycie:
  python3 tools/render_welcome_preview.py [data/fonts/Welcome-Regular21.vlw] [out.png]
"""
import struct
import sys

from PIL import Image

W, H = 480, 320
LINES = [
    "ESP32-HAM-CLOCK v1.2",
    "Projekt bazowy: Konrad Wiśniewski SP3KON",
    "Adaptacja i rozwój: Tomasz SP9TNV.",
    "Kontakt: SP9TNV@gmail.com",
    "Projekt udostępniany na licencji MIT.",
]


def load_vlw(path):
    d = open(path, "rb").read()
    gcount, _ver, _size, _mbox, ascent, descent = struct.unpack(">IIIIII", d[0:24])
    ptr = 24
    meta = {}
    for _ in range(gcount):
        u, h, w, adv, dy, dx, _pad = struct.unpack(">IIIIiiI", d[ptr:ptr + 28])
        meta[u] = (h, w, adv, dy, dx)
        ptr += 28
    bitmaps = {}
    for u, (h, w, adv, dy, dx) in meta.items():
        bitmaps[u] = d[ptr:ptr + h * w]
        ptr += h * w
    return ascent, descent, meta, bitmaps


def main():
    vlw = sys.argv[1] if len(sys.argv) > 1 else "data/fonts/Welcome-Regular21.vlw"
    out = sys.argv[2] if len(sys.argv) > 2 else "/workspace/share/welcome-preview.png"

    ascent, descent, meta, bitmaps = load_vlw(vlw)
    space = (ascent + descent) * 2 // 7
    line_h = ascent + descent

    img = Image.new("L", (W, H), 0)
    px = img.load()

    base_y = (H - line_h * len(LINES)) // 2 + 18
    for i, line in enumerate(LINES):
        width = sum(space if ch == " " else meta[ord(ch)][2] for ch in line)
        cx = (W - width) // 2
        cy = base_y + i * line_h
        for ch in line:
            cp = ord(ch)
            if cp == 0x20:
                cx += space
                continue
            h, w, adv, dy, dx = meta[cp]
            bmp = bitmaps[cp]
            top = cy - dy
            left = cx + dx
            for row in range(h):
                for col in range(w):
                    a = bmp[row * w + col]
                    x, y = left + col, top + row
                    if a and 0 <= x < W and 0 <= y < H:
                        px[x, y] = max(px[x, y], a)
            cx += adv

    img.convert("RGB").save(out)
    print(f"{out}: lineH={line_h}, maxW={max(sum(space if c==' ' else meta[ord(c)][2] for c in l) for l in LINES)}")


if __name__ == "__main__":
    main()
