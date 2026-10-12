#!/usr/bin/env python3
"""Konwertuje flagi PNG (data/flags/*.png) na 24-bit BMP (RGB888, bottom-up)
dla TFT. Firmware rysuje flagi przez drawBmpFromFS(), ktora ODRZUCA wszystko
co nie jest 24-bit BMP (sprawdza readBmp16()==24) - dlatego wczesniejsze
16-bit BMP (RGB565) nie byly wyswietlane. Format: dokladnie taki sam jak
data/mapa.bmp / splash.bmp. Wynik: data/tft_flags/*.bmp (nazwy UPPERCASE,
jakich oczekuje firmware: /flags/{CC}.bmp).

UWAGA: na ekranie i tak nie ma osobnego miejsca na duza flage - firmware
laduje tylko ten maly plik obok nazwy kraju. Rozmiar dobrany do layoutu.
"""
import os, struct, glob
from PIL import Image

FLAG_W, FLAG_H = 32, 21
SRC = "data/flags"
DST = "data/tft_flags"

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
        f.write(struct.pack("<i", h))         # positive = bottom-up (jak mapa.bmp)
        f.write(struct.pack("<HH", 1, 24))    # planes=1, bpp=24
        f.write(struct.pack("<I", 0))         # BI_RGB
        f.write(struct.pack("<I", pixel_data_size))
        f.write(struct.pack("<i", 2835))
        f.write(struct.pack("<i", 2835))
        f.write(struct.pack("<I", 0))
        f.write(struct.pack("<I", 0))
        # bottom-up: ostatni wiersz obrazu zapisujemy pierwszy, piksele BGR
        for y in range(h - 1, -1, -1):
            for x in range(w):
                r, g, b = px[x, y][:3]
                f.write(struct.pack("<BBB", b, g, r))
            f.write(b"\x00" * pad)

os.makedirs(DST, exist_ok=True)
n = 0
for src in sorted(glob.glob(os.path.join(SRC, "*.png"))):
    code = os.path.basename(src)[:-4].upper()
    img = Image.open(src).convert("RGB").resize((FLAG_W, FLAG_H), Image.LANCZOS)
    save_bmp_24bit(os.path.join(DST, code + ".bmp"), img)
    n += 1
total = sum(os.path.getsize(os.path.join(DST, f)) for f in os.listdir(DST))
print(f"flagi TFT (24-bit BMP): {n} plikow, {total/1024:.1f} KB")

