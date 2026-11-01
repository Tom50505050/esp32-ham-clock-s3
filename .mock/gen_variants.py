#!/usr/bin/env python3
# Warianty ikon: (a) przezroczyste PNG, (b) na CZARNYM tle PNG, (c) BMP 24-bit na czarnym tle.
import os, glob
from PIL import Image

SRC = "/workspace/project/data/aprs_icons"
OUT = "/workspace/project/.mock/variants"
os.makedirs(OUT, exist_ok=True)

def to_bmp24(img_rgb, path):
    # BMP 24-bit, bottom-up, BGR, padding do 4 bajtow - format czytany przez drawBmpFromFS()
    w, h = img_rgb.size
    pad = (4 - (w * 3) % 4) % 4
    rowbytes = w * 3 + pad
    filesize = 54 + rowbytes * h
    import struct
    hdr = b"BM" + struct.pack("<IHHI", filesize, 0, 0, 54)
    dib = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, rowbytes * h, 2835, 2835, 0, 0)
    px = img_rgb.load()
    body = bytearray()
    for y in range(h - 1, -1, -1):        # bottom-up
        for x in range(w):
            r, g, b = px[x, y][:3]
            body += bytes((b, g, r))       # BGR
        body += b"\x00" * pad
    with open(path, "wb") as f:
        f.write(hdr + dib + bytes(body))

made = {"png_alpha": 0, "png_black": 0, "bmp_black": 0}
for f in sorted(glob.glob(SRC + "/*.png")):
    name = os.path.splitext(os.path.basename(f))[0]
    src = Image.open(f).convert("RGBA")

    # (b) PNG spłaszczony na czarnym tle
    bg = Image.new("RGBA", src.size, (0, 0, 0, 255))
    flat = Image.alpha_composite(bg, src).convert("RGB")
    flat.save(os.path.join(OUT, name + "_black.png"), "PNG", optimize=True)
    made["png_black"] += 1

    # (c) BMP 24-bit na czarnym tle (jak drawBmpFromFS)
    to_bmp24(flat, os.path.join(OUT, name + ".bmp"))
    made["bmp_black"] += 1

    made["png_alpha"] += 1

print("warianty:", made)

# --- DEMO: przezroczysty PNG na czarnym tle  ==  PNG spłaszczony na czarnym ---
S = 4
tiles = ["house", "ambul", "bike", "ship"]
tw = 48 * S
demo = Image.new("RGB", (tw * len(tiles) + 20 * (len(tiles) + 1), tw + 60), (0, 0, 0))
for i, t in enumerate(tiles):
    x = 20 + i * (tw + 20)
    # przezroczysty PNG na czarnym tle
    ic = Image.open(f"{SRC}/{t}.png").convert("RGBA").resize((tw, tw), Image.NEAREST)
    demo.paste(ic, (x, 20), ic)          # alpha -> czarne tlo pokazuje sie pod spodem
    demo.paste(Image.open(f"{OUT}/{t}_black.png").resize((tw, tw), Image.NEAREST), (x, tw + 40 - tw // 2 - 20))
from PIL import ImageDraw, ImageFont
d = ImageDraw.Draw(demo)
try:
    fnt = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 16)
    fnt2 = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 13)
except Exception:
    fnt = fnt2 = ImageFont.load_default()
d.text((20, tw + 30), "gora: przezroczysty PNG na czarnym tle  |  dol: PNG na czarnym tle (spłaszczony) - identyczny efekt na ekranie klasycznym", font=fnt2, fill=(200, 200, 200))
demo.save("/workspace/share/aprs-alpha-vs-black.png")
print("zapisano aprs-alpha-vs-black.png", demo.size)
