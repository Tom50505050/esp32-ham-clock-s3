#!/usr/bin/env python3
"""Konwertuje dowolny obraz (JPG/PNG/BMP/...) na BMP 16-bit RGB565 480x320.

Format wyjsciowy jest dokladnie tym, co firmware czyta w drawBmp16FromFS():
BMP 16-bit z maskami bitowymi (BI_BITFIELDS, compression=3), bottom-up,
wiersze wyrownane do 4 bajtow. Kolory sa pakowane jak w sterowniku ILI9488
(RGB565): RRRRRGGG GGGBBBBB - czyli "format optymalny dla wyswietlacza".
Plik jest 2x mniejszy od 24-bit BMP i jest wysylany do TFT bez konwersji.

Uzycie:
    python3 tools/img_to_bmp565.py obraz.jpg [wyjscie.bmp] [--fit contain|stretch|cover]

--fit:
    contain  - zachowaj proporcje, dopasuj w calosci, tlo czarne (domyslne)
    stretch  - rozciagnij dokladnie do 480x320 (znieksztalcenie)
    cover    - wypelnij caly ekran, przytnij nadmiar (bez znieksztalcen)

Wynik wgraj na urzadzenie:
    - jako ekran startowy:  /upload_splash (przycisk w WWW) lub skopiuj do data/
    - albo przez LittleFS:  pio run -e esp32-s3-n16r8 -t uploadfs
"""
import os
import struct
import sys

from PIL import Image

W, H = 480, 320
# Maski bitowe dla RGB565 (R=5 bit, G=6 bit, B=5 bit).
MASK_R, MASK_G, MASK_B = 0xF800, 0x07E0, 0x001F


def fit_image(img, mode):
    if mode == "stretch":
        return img.resize((W, H), Image.LANCZOS)
    if mode == "cover":
        src_ratio = img.width / img.height
        dst_ratio = W / H
        if src_ratio > dst_ratio:
            new_w = int(round(img.height * dst_ratio))
            left = (img.width - new_w) // 2
            img = img.crop((left, 0, left + new_w, img.height))
        else:
            new_h = int(round(img.width / dst_ratio))
            top = (img.height - new_h) // 2
            img = img.crop((0, top, img.width, top + new_h))
        return img.resize((W, H), Image.LANCZOS)
    # contain (domyslne): letterbox na czarnym tle
    canvas = Image.new("RGB", (W, H), (0, 0, 0))
    scaled = img.copy()
    scaled.thumbnail((W, H), Image.LANCZOS)
    canvas.paste(scaled, ((W - scaled.width) // 2, (H - scaled.height) // 2))
    return canvas


def to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def save_bmp565(filepath, img):
    px = img.load()
    row_size = ((16 * W + 31) // 32) * 4  # wyrownanie wiersza do 4 bajtow
    padding = row_size - W * 2
    pixel_data_size = row_size * H
    header_size = 14 + 40 + 12  # BITMAPFILEHEADER + BITMAPINFOHEADER + maski
    file_size = header_size + pixel_data_size
    with open(filepath, "wb") as f:
        # BITMAPFILEHEADER
        f.write(b"BM")
        f.write(struct.pack("<I", file_size))
        f.write(struct.pack("<HH", 0, 0))
        f.write(struct.pack("<I", header_size))
        # BITMAPINFOHEADER
        f.write(struct.pack("<I", 40))
        f.write(struct.pack("<i", W))
        f.write(struct.pack("<i", H))          # dodatnia = bottom-up
        f.write(struct.pack("<HH", 1, 16))     # planes=1, bpp=16
        f.write(struct.pack("<I", 3))          # BI_BITFIELDS
        f.write(struct.pack("<I", pixel_data_size))
        f.write(struct.pack("<i", 2835))
        f.write(struct.pack("<i", 2835))
        f.write(struct.pack("<I", 0))
        f.write(struct.pack("<I", 0))
        # maski R/G/B (12 bajtow)
        f.write(struct.pack("<III", MASK_R, MASK_G, MASK_B))
        # piksele: bottom-up, RGB565 little-endian
        pad = b"\x00" * padding
        for y in range(H - 1, -1, -1):
            row = bytearray()
            for x in range(W):
                r, g, b = px[x, y][:3]
                row += struct.pack("<H", to_rgb565(r, g, b))
            row += pad
            f.write(row)


def main():
    args = [a for a in sys.argv[1:]]
    mode = "contain"
    if "--fit" in args:
        i = args.index("--fit")
        mode = args[i + 1] if i + 1 < len(args) else "contain"
        del args[i:i + 2]
    if mode not in ("contain", "stretch", "cover"):
        print(f"Nieznany tryb --fit: {mode}")
        sys.exit(2)
    if not args:
        print(__doc__)
        sys.exit(1)
    src = args[0]
    out = args[1] if len(args) > 1 else os.path.splitext(os.path.basename(src))[0] + "_480x320.bmp"
    img = Image.open(src).convert("RGB")
    img = fit_image(img, mode)
    save_bmp565(out, img)
    print(f"Zapisano {out} ({os.path.getsize(out)} B, {W}x{H}, 16-bit RGB565 BMP, fit={mode})")


if __name__ == "__main__":
    main()
