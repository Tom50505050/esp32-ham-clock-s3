#!/usr/bin/env python3
"""Konwertuje 24-bit BMP (480x320) na 16-bit RGB565 BI_BITFIELDS.

Domyslne ikony alertu (wiatr/burza) byly 24-bit (460854 B kazda). Wgrywana z
WWW ikona jest zawsze 16-bit (307266 B), a LittleFS ma tylko ~600 kB wolnego -
zapis 307 kB konczyl sie brakiem miejsca (ENOSPC) i panic (IntegerDivideByZero)
w littlefs. 16-bit ikony domyslne sa 2x mniejsze i zwalniaja miejsce na upload.
"""
import struct
import sys


def convert(src, dst):
    d = open(src, 'rb').read()
    off = struct.unpack_from('<I', d, 10)[0]
    w = struct.unpack_from('<i', d, 18)[0]
    h = struct.unpack_from('<i', d, 22)[0]
    bpp = struct.unpack_from('<H', d, 28)[0]
    if bpp != 24:
        raise SystemExit(f"{src}: oczekiwano 24-bit, jest {bpp}")
    row_in = (w * 3 + 3) & ~3
    row_out = ((16 * w + 31) // 32) * 4
    hdr = 14 + 40 + 12
    size = hdr + row_out * h
    out = bytearray(size)
    # BITMAPFILEHEADER
    out[0:2] = b'BM'
    struct.pack_into('<I', out, 2, size)
    struct.pack_into('<I', out, 10, hdr)
    # BITMAPINFOHEADER
    struct.pack_into('<I', out, 14, 40)
    struct.pack_into('<i', out, 18, w)
    struct.pack_into('<i', out, 22, h)
    struct.pack_into('<H', out, 26, 1)
    struct.pack_into('<H', out, 28, 16)
    struct.pack_into('<I', out, 30, 3)          # BI_BITFIELDS
    struct.pack_into('<I', out, 34, row_out * h)
    struct.pack_into('<i', out, 38, 2835)
    struct.pack_into('<i', out, 42, 2835)
    # masks R/G/B
    struct.pack_into('<I', out, 54, 0xF800)
    struct.pack_into('<I', out, 58, 0x07E0)
    struct.pack_into('<I', out, 62, 0x001F)
    # pixels: bottom-up, RGB565 little-endian
    for y in range(h):
        s = off + (h - 1 - y) * row_in
        o = hdr + y * row_out
        for x in range(w):
            b = d[s + x * 3 + 0]
            g = d[s + x * 3 + 1]
            r = d[s + x * 3 + 2]
            v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            out[o + x * 2] = v & 0xFF
            out[o + x * 2 + 1] = (v >> 8) & 0xFF
    open(dst, 'wb').write(out)
    print(f"{src} -> {dst}: {len(d)} -> {len(out)} B")


if __name__ == '__main__':
    for p in sys.argv[1:]:
        convert(p, p)
