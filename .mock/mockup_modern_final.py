import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

bg = np.array([120, 171, 192])
BG, CARD, CARD2, ACCENT, BORDER, TEXT, FAINT = (0, 0, 0), (11, 18, 32), (16, 26, 44), (54, 151, 190), (41, 74, 88), (239, 247, 239), (107, 191, 242)

def font(sz, bold=False):
    p = "/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf" % ("-Bold" if bold else "")
    try:
        return ImageFont.truetype(p, sz)
    except Exception:
        return ImageFont.load_default()

def cutout(fn):
    im = Image.open(fn).convert('RGB')
    a = np.asarray(im).astype(int)
    d = np.abs(a - bg).sum(axis=2)
    mask = (d > 60).astype(np.uint8) * 255
    m = Image.fromarray(mask, 'L').filter(ImageFilter.MaxFilter(5))
    out = im.convert('RGBA'); out.putalpha(m)
    return out.crop(m.getbbox())

def fit(img, size):
    c = img.copy(); c.thumbnail((size, size), Image.LANCZOS)
    return c

house_code = Image.open(".mock/code_house_64.png").convert("RGBA")
yagi11 = cutout(".mock/cell_2_2.png")   # #11 yagi
rep2 = cutout(".mock/cell_0_1.png")     # #2 repeater

def icon_for(code, size):
    if code == "YAGI":
        return fit(yagi11, size)
    if code == "REPEATER":
        return fit(rep2, size)
    return house_code.resize((size, size), Image.NEAREST)

def full_screen(code, desc):
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
    d.rounded_rectangle([10, 160, 233, 271], 7, fill=CARD, outline=BORDER, width=2)
    d.text((22, 168), "STACJA", font=font(9), fill=FAINT)
    ic = icon_for(code, 64)
    img.paste(ic, (90 + (64 - ic.width) // 2, 184 + (64 - ic.height) // 2), ic)
    tw = d.textlength(desc, font=font(13, True))
    d.text((10 + (224 - tw) / 2, 250), desc, font=font(13, True), fill=TEXT)
    # karta NAMIAR
    d.rounded_rectangle([246, 160, 469, 271], 7, fill=CARD, outline=BORDER, width=2)
    d.text((258, 168), "NAMIAR", font=font(9), fill=FAINT)
    d.ellipse([322, 186, 394, 258], outline=BORDER, width=2)
    d.text((352, 214), "N", font=font(13, True), fill=ACCENT)
    return img

a = full_screen("YAGI", "YAGI ANTENNA")
b = full_screen("REPEATER", "REPEATER")
sheet = Image.new("RGB", (480 * 2 + 30, 320 + 40), (5, 8, 16))
sd = ImageDraw.Draw(sheet)
sd.text((10, 10), "YAGI -> #11 (z yagi.jpg)", font=font(15, True), fill=ACCENT)
sd.text((500, 10), "REPEATER -> #2 (z yagi.jpg)", font=font(15, True), fill=ACCENT)
sheet.paste(a, (0, 34))
sheet.paste(b, (500, 34))
sheet.save("aprs-modern-final.png")
print("ok")
