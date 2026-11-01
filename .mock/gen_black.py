#!/usr/bin/env python3
# Wersje ikon NA CZARNYM TLE -> data/aprs_icons/black/ (PNG + BMP 24-bit).
import os, glob, struct
from PIL import Image

SRC = "/workspace/project/data/aprs_icons"
OUT = "/workspace/project/data/aprs_icons/black"
os.makedirs(OUT, exist_ok=True)

def to_bmp24(img_rgb, path):
    w, h = img_rgb.size
    pad = (4 - (w * 3) % 4) % 4
    rowbytes = w * 3 + pad
    filesize = 54 + rowbytes * h
    hdr = b"BM" + struct.pack("<IHHI", filesize, 0, 0, 54)
    dib = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, rowbytes * h, 2835, 2835, 0, 0)
    px = img_rgb.load()
    body = bytearray()
    for y in range(h - 1, -1, -1):
        for x in range(w):
            r, g, b = px[x, y][:3]
            body += bytes((b, g, r))
        body += b"\x00" * pad
    open(path, "wb").write(hdr + dib + bytes(body))

n = 0
for f in sorted(glob.glob(SRC + "/*.png")):
    name = os.path.splitext(os.path.basename(f))[0]
    src = Image.open(f).convert("RGBA")
    bg = Image.new("RGBA", src.size, (0, 0, 0, 255))
    flat = Image.alpha_composite(bg, src).convert("RGB")
    flat.save(os.path.join(OUT, name + ".png"), "PNG", optimize=True)
    to_bmp24(flat, os.path.join(OUT, name + ".bmp"))
    n += 1
print("na czarnym tle:", n, "ikon -> ", OUT, "(PNG + BMP 24-bit)")
