import os, math, struct
BLK = 4096
tot = sum(math.ceil(os.path.getsize(os.path.join(r, f)) / BLK) * BLK
          for r, _, fs in os.walk('data') for f in fs)
raw = sum(os.path.getsize(os.path.join(r, f)) for r, _, fs in os.walk('data') for f in fs)
n = sum(len(fs) for _, _, fs in os.walk('data'))
PART = 0xBF0000
print(f"data/: {n} plikow, {raw/1048576:.2f} MB (po blokach {tot/1048576:.2f} MB)")
print(f"LittleFS: {PART/1048576:.2f} MB")
print(f"BUFOR: {(PART-tot)/1048576:.2f} MB  (wczesniej 1.90 MB)")
print(f"-> plikow BMP 480x320 (~450 kB): ~{int((PART-tot)//(460800+BLK))}")

m = open('dist/merged-flash.bin', 'rb').read()
print("\nmagic littlefs @ 0x401008:", m[0x401008:0x401010])
pt = m[0x8000:0x8000 + 0xC00]
print("tablica partycji:")
for i in range(0, len(pt), 32):
    e = pt[i:i + 32]
    if e[0] == 0xAA and e[1] == 0x50:
        typ, sub = e[2], e[3]
        off, size = struct.unpack('<II', e[4:12])
        name = e[12:28].split(b'\x00')[0].decode(errors='replace')
        print(f"  {name:10s} type={typ} sub={sub:#x} off={off:#x} size={size:#x} ({size/1048576:.2f} MB)")
