from PIL import Image, ImageDraw, ImageFont

W, H = 1000, 620
img = Image.new('RGB', (W, H), (8, 12, 22))
d = ImageDraw.Draw(img)

def font(sz, b=False):
    p = "/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf" % ("-Bold" if b else "")
    try:
        return ImageFont.truetype(p, sz)
    except Exception:
        return ImageFont.load_default()

def mb(x):
    return x / 1048576

# layout: (nazwa, offset, size, kolor)
BEFORE = [
    ("bootloader/nvs", 0x0, 0x10000, (60, 70, 90)),
    ("app0 (fw)", 0x10000, 0x380000, (54, 151, 190)),
    ("app1 OTA\n(bezuzyteczny)", 0x390000, 0x370000, (170, 70, 80)),
    ("littlefs (dane+bufor)", 0x700000, 0x8F0000, (60, 140, 90)),
    ("core", 0xFF0000, 0x10000, (60, 70, 90)),
]
AFTER = [
    ("bootloader/nvs", 0x0, 0x10000, (60, 70, 90)),
    ("app0 (fw)", 0x10000, 0x3F0000, (54, 151, 190)),
    ("littlefs (dane+bufor)", 0x400000, 0xBF0000, (60, 140, 90)),
    ("core", 0xFF0000, 0x10000, (60, 70, 90)),
]

X0, X1 = 40, 960
TOTAL = 16 * 1024 * 1024
SCALE = (X1 - X0) / TOTAL

def draw_row(y, title, parts, note):
    d.text((X0, y - 30), title, font=font(18, True), fill=(239, 247, 239))
    for name, off, size, col in parts:
        x = X0 + off * SCALE
        w = size * SCALE
        d.rectangle([x, y, x + w, y + 70], fill=col, outline=(15, 20, 32), width=2)
        if w > 60:
            label = name
            bb = d.textbbox((0, 0), label, font=font(12, True))
            tw, th = bb[2] - bb[0], bb[3] - bb[1]
            d.multiline_text((x + (w - tw) / 2, y + (70 - th) / 2 - 2), label,
                             font=font(12, True), fill=(255, 255, 255), align='center')
    d.text((X0, y + 78), note, font=font(13), fill=(160, 180, 200))

d.text((X0, 24), "ESP32-S3 N16R8 (16 MB flash) - podzial partycji", font=font(22, True), fill=(84, 214, 190))
d.text((X0, 54), "Cel: wiekszy bufor w LittleFS na uploady z WWW (pogoda, splash, mapa)", font=font(14), fill=(160, 180, 200))

draw_row(120, "PRZED", BEFORE, "LittleFS 8.94 MB  |  wolne ~1.90 MB  |  slot app1 = 3.5 MB zmarnowany (OTA nieuzywane)")
draw_row(300, "PO", AFTER, "LittleFS 11.94 MB  |  wolne ~4.90 MB  |  +3.0 MB bufora, +0.44 MB na firmware")

# pasek legendy / bilans
y = 470
d.rounded_rectangle([X0, y, X1, y + 110], 10, fill=(14, 20, 34), outline=(41, 74, 88), width=2)
d.text((X0 + 20, y + 14), "Bilans:", font=font(16, True), fill=(84, 214, 190))
d.text((X0 + 20, y + 44), "bufor WWW (LittleFS wolne):   1.90 MB  ->  4.90 MB      (+158%)", font=font(15), fill=(239, 247, 239))
d.text((X0 + 20, y + 68), "firmware: 1.85 MB / slot 3.94 MB (44.9%)   |   plik BMP 480x320 (~450 kB) x ~11", font=font(15), fill=(200, 215, 230))
d.text((X0 + 20, y + 90), "UWAGA: zmiana tablicy -> wgraj CALY merged-flash (offset 0x0)", font=font(13), fill=(255, 190, 120))

img.save('/workspace/share/littlefs_buffer_before_after.png')
print('zapisano littlefs_buffer_before_after.png')
