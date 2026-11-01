from PIL import Image, ImageDraw, ImageFont
import os

house = Image.open(".mock/code_house_64.png").convert("RGBA")
CARD, ACCENT, BORDER, TEXT, FAINT = (11, 18, 32), (54, 151, 190), (41, 74, 88), (239, 247, 239), (107, 191, 242)

def font(sz, bold=False):
    p = "/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf" % ("-Bold" if bold else "")
    try:
        return ImageFont.truetype(p, sz)
    except Exception:
        return ImageFont.load_default()

def icon_variant(kind, size=128):
    """Zwraca obraz size x size z domem + napisem WX w danym stylu."""
    base = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    h = house.resize((size, size), Image.NEAREST)
    base.paste(h, (0, 0), h)
    d = ImageDraw.Draw(base)
    if kind == "badge":
        bw, bh = int(size * 0.52), int(size * 0.32)
        x, y = size - bw - int(size * 0.04), size - bh - int(size * 0.04)
        d.rounded_rectangle([x, y, x + bw, y + bh], int(size * 0.08), fill=ACCENT, outline=(255, 255, 255), width=2)
        f = font(int(bh * 0.72), True)
        tw = d.textlength("WX", font=f)
        d.text((x + (bw - tw) / 2, y + bh * 0.08), "WX", font=f, fill=(0, 0, 0))
    elif kind == "below":
        f = font(int(size * 0.30), True)
        tw = d.textlength("WX", font=f)
        d.rectangle([size * 0.18, size * 0.80, size * 0.82, size * 1.0], fill=CARD)
        d.text(((size - tw) / 2, size * 0.78), "WX", font=f, fill=ACCENT)
    elif kind == "center":
        f = font(int(size * 0.52), True)
        tw = d.textlength("WX", font=f)
        d.text(((size - tw) / 2, size * 0.30), "WX", font=f, fill=(255, 255, 255), stroke_width=3, stroke_fill=(0, 0, 0))
    elif kind == "corner":
        bw, bh = int(size * 0.44), int(size * 0.30)
        x, y = int(size * 0.02), int(size * 0.02)
        d.rounded_rectangle([x, y, x + bw, y + bh], int(size * 0.07), fill=(255, 255, 255), outline=ACCENT, width=2)
        f = font(int(bh * 0.72), True)
        tw = d.textlength("WX", font=f)
        d.text((x + (bw - tw) / 2, y + bh * 0.08), "WX", font=f, fill=(0, 0, 0))
    return base

variants = [("badge", "A: plakietka w rogu"), ("below", "B: napis pod domem"),
            ("center", "C: duze WX na domu"), ("corner", "D: tag w lewym gornym")]

CW, CH, PAD = 150, 200, 16
img = Image.new("RGB", (4 * (CW + PAD) + PAD, CH + 70), (5, 8, 16))
d = ImageDraw.Draw(img)
d.text((PAD, 12), "WX - warianty napisu na ikonie domu z kodu (podglad 128 px)", font=font(17, True), fill=ACCENT)
for i, (kind, label) in enumerate(variants):
    x = PAD + i * (CW + PAD)
    d.rounded_rectangle([x, 46, x + CW, 46 + CH], 10, fill=CARD, outline=BORDER, width=2)
    ic = icon_variant(kind, 128)
    img.paste(ic, (x + (CW - 128) // 2, 46 + 8), ic)
    d.text((x + 6, 46 + 146), label, font=font(12, True), fill=TEXT)
img.save(".mock/aprs-modern-wx-variants.png")

# dodatkowo: realny rozmiar 64 px na karcie STACJA dla wariantu A i C
def card(kind):
    c = Image.new("RGB", (224, 112), CARD)
    dc = ImageDraw.Draw(c)
    dc.rounded_rectangle([0, 0, 223, 111], 8, fill=CARD, outline=BORDER, width=2)
    dc.text((12, 8), "STACJA", font=font(11, True), fill=FAINT)
    ic = icon_variant(kind, 64)
    c.paste(ic, (80, 24), ic)
    dc.text((12, 90), "WEATHER STATION", font=font(13, True), fill=TEXT)
    return c

row = Image.new("RGB", (2 * 234 + 20, 132), (5, 8, 16))
row.paste(card("badge"), (10, 10))
row.paste(card("center"), (254, 10))
row.save(".mock/aprs-modern-wx-64.png")
print("ok")
