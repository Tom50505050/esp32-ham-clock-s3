import re, os
from PIL import Image, ImageDraw, ImageFont

SRC = "main.cpp"
txt = open(SRC, encoding="utf-8", errors="surrogateescape").read()
m = re.search(r"APRS_HOUSE_ICON\[4096\]\s*=\s*\{(.*?)\};", txt, re.S)
vals = re.findall(r"0x([0-9A-Fa-f]{4})", m.group(1))
assert len(vals) == 4096, len(vals)
W = H = 64
TRANSP = 0xF81F

def rgb565(c):
    r = ((c >> 11) & 0x1F) << 3
    g = ((c >> 5) & 0x3F) << 2
    b = (c & 0x1F) << 3
    return (r, g, b, 255)

house = Image.new("RGBA", (W, H), (0, 0, 0, 0))
px = house.load()
for i, v in enumerate(vals):
    c = int(v, 16)
    if c == TRANSP:
        continue
    px[i % W, i // W] = rgb565(c)
house.save(".mock/code_house_64.png")

# Kolory ekranu nowoczesnego
BG, CARD, CARD2, ACCENT, BORDER, TEXT, FAINT = (
    (0, 0, 0), (11, 18, 32), (16, 26, 44), (54, 151, 190), (41, 74, 88), (239, 247, 239), (107, 191, 242))

def font(sz, bold=False):
    p = "/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf" % ("-Bold" if bold else "")
    try:
        return ImageFont.truetype(p, sz)
    except Exception:
        return ImageFont.load_default()

def station_card(wx_mode):
    """Renderuje karte STACJA 224x112 (jak w firmware) i zwraca obraz 224x112."""
    img = Image.new("RGB", (224, 112), CARD)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, 223, 111], 8, fill=CARD, outline=BORDER, width=2)
    d.text((12, 8), "STACJA", font=font(11, True), fill=FAINT)
    # ikona domu 64x64 z bitmapy kodu, w (90-10=80, 184-160=24) w ukladzie karty
    ic = house.resize((64, 64), Image.NEAREST)
    img.paste(ic, (80, 24), ic)
    if wx_mode:
        # napis WX na ikonie: badge w prawym-dolnym rogu ikony
        d2 = ImageDraw.Draw(img)
        bx, by = 80 + 64 - 34, 24 + 64 - 22
        d2.rounded_rectangle([bx, by, bx + 32, by + 20], 4, fill=ACCENT, outline=(255, 255, 255))
        d2.text((bx + 5, by + 3), "WX", font=font(13, True), fill=(0, 0, 0))
    desc = "WEATHER STATION" if wx_mode else "HOME STATION"
    tw = d.textlength(desc, font=font(13, True))
    d.text((112 - tw / 2, 90), desc, font=font(13, True), fill=TEXT)
    return img

def full_screen(wx_mode):
    img = Image.new("RGB", (480, 320), BG)
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, 479, 2], fill=ACCENT)
    d.rounded_rectangle([10, 8, 469, 41], 7, fill=CARD, outline=BORDER, width=2)
    d.rounded_rectangle([12, 10, 16, 39], 2, fill=ACCENT)
    d.text((26, 18), "APRS ALERT", font=font(16, True), fill=TEXT)
    d.text((330, 24), "AKT 12:00Z", font=font(9), fill=FAINT)
    d.rounded_rectangle([10, 50, 469, 115], 7, fill=CARD, outline=BORDER, width=2)
    cs = "SP9TNV"
    d.text((10 + (460 - d.textlength(cs, font=font(30, True))) / 2, 62), cs, font=font(30, True), fill=TEXT)
    for x, lab, val, col in [(10, "FREQ", "145.500 MHz", ACCENT), (246, "ODLEGLOSC", "12.4 km", TEXT)]:
        d.rounded_rectangle([x, 122, x + 223, 153], 7, fill=CARD2, outline=BORDER, width=2)
        d.text((x + 10, 125), lab, font=font(9), fill=FAINT)
        d.text((x + 10, 137), val, font=font(14, True), fill=col)
    # karta STACJA
    card = station_card(wx_mode)
    img.paste(card, (10, 160))
    # karta NAMIAR
    d.rounded_rectangle([246, 160, 469, 271], 7, fill=CARD, outline=BORDER, width=2)
    d.text((258, 168), "NAMIAR", font=font(9), fill=FAINT)
    d.ellipse([322, 186, 394, 258], outline=BORDER, width=2)
    d.text((358 - 6, 222 - 8), "N", font=font(13, True), fill=ACCENT)
    return img

sheet = Image.new("RGB", (480 * 2 + 30, 320 + 60), (5, 8, 16))
d = ImageDraw.Draw(sheet)
d.text((10, 8), "HOUSE = ikona z kodu", font=font(15, True), fill=ACCENT)
d.text((480 + 20, 8), "WX = ikona z kodu + napis WX", font=font(15, True), fill=ACCENT)
sheet.paste(full_screen(False), (0, 40))
sheet.paste(full_screen(True), (480 + 30, 40))
sheet.save(".mock/aprs-modern-house-vs-wx.png")

# zoom na ikony
z = Image.new("RGB", (2 * 200 + 30, 240), (5, 8, 16))
dz = ImageDraw.Draw(z)
dz.text((10, 8), "HOUSE (z kodu)", font=font(14, True), fill=ACCENT)
dz.text((220, 8), "WX (z kodu + WX)", font=font(14, True), fill=ACCENT)
hi = house.resize((160, 160), Image.NEAREST)
z.paste(hi, (20, 40), hi)
z.paste(hi, (230, 40), hi)
bx, by = 230 + 160 - 60, 40 + 160 - 44
dz.rounded_rectangle([bx, by, bx + 58, by + 40], 6, fill=ACCENT, outline=(255, 255, 255), width=2)
dz.text((bx + 10, by + 6), "WX", font=font(26, True), fill=(0, 0, 0))
z.save(".mock/aprs-modern-house-wx-zoom.png")
print("ok")
