import re
import numpy as np
from PIL import Image, ImageDraw, ImageFont

src = open('aprs_modern_icons.h').read()
blocks = re.findall(r'APRS_MICON_(\w+)\[4096\] = \{(.*?)\};', src, re.S)

def to_rgb(v):
    v = v.astype(np.uint32)
    r = (v >> 11) & 0x1F; g = (v >> 5) & 0x3F; b = v & 0x1F
    return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], -1).astype(np.uint8)

def font(sz, b=False):
    p = "/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf" % ("-Bold" if b else "")
    try:
        return ImageFont.truetype(p, sz)
    except Exception:
        return ImageFont.load_default()

imgs = {}
for name, body in blocks:
    vals = np.array([int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{4})', body)], dtype=np.uint16)
    imgs[name] = Image.fromarray(to_rgb(vals.reshape(64, 64)))

# Arkusz wszystkich 24
COLS, TW, TH, PAD = 6, 108, 132, 10
rows = (len(blocks) + COLS - 1) // COLS
sheet = Image.new('RGB', (COLS * (TW + PAD) + PAD, 46 + rows * (TH + PAD) + PAD), (5, 8, 16))
d = ImageDraw.Draw(sheet)
d.text((PAD, 12), 'aprs_modern_icons.h - zdekodowane z RGB565 (dokladnie to, co pokaze sie na karcie)', font=font(15, True), fill=(84, 214, 190))
for i, (name, _) in enumerate(blocks):
    r, c = divmod(i, COLS)
    x, y = PAD + c * (TW + PAD), 46 + PAD + r * (TH + PAD)
    d.rounded_rectangle([x, y, x + TW, y + TH], 8, fill=(16, 26, 44), outline=(41, 74, 88), width=2)
    sheet.paste(imgs[name], (x + (TW - 64) // 2, y + 10))
    tw = d.textlength(name, font=font(11, True))
    d.text((x + (TW - tw) / 2, y + 84), name, font=font(11, True), fill=(239, 247, 239))
sheet.save('.mock/modern_icons_from_header.png')

# Realna karta STACJA 480x320 z YAGI i REPEATER
def screen(name, desc):
    img = Image.new('RGB', (480, 320), (0, 0, 0)); dr = ImageDraw.Draw(img)
    dr.rectangle([0, 0, 479, 2], fill=(54, 151, 190))
    dr.rounded_rectangle([10, 8, 469, 41], 7, fill=(11, 18, 32), outline=(41, 74, 88), width=2)
    dr.rounded_rectangle([12, 10, 16, 39], 2, fill=(54, 151, 190))
    dr.text((26, 18), 'APRS ALERT', font=font(16, True), fill=(239, 247, 239))
    dr.rounded_rectangle([10, 50, 469, 115], 7, fill=(11, 18, 32), outline=(41, 74, 88), width=2)
    cs = 'SP9TNV'
    dr.text((10 + (460 - dr.textlength(cs, font=font(30, True))) / 2, 62), cs, font=font(30, True), fill=(239, 247, 239))
    for x, lab, val, col in [(10, 'FREQ', '145.500 MHz', (54, 151, 190)), (246, 'ODLEGLOSC', '12.4 km', (239, 247, 239))]:
        dr.rounded_rectangle([x, 122, x + 223, 153], 7, fill=(16, 26, 44), outline=(41, 74, 88), width=2)
        dr.text((x + 10, 125), lab, font=font(9), fill=(107, 191, 242))
        dr.text((x + 10, 137), val, font=font(14, True), fill=col)
    dr.rounded_rectangle([10, 160, 233, 271], 7, fill=(11, 18, 32), outline=(41, 74, 88), width=2)
    dr.text((22, 168), 'STACJA', font=font(9), fill=(107, 191, 242))
    img.paste(imgs[name], (90, 184))
    tw = dr.textlength(desc, font=font(13, True))
    dr.text((10 + (224 - tw) / 2, 250), desc, font=font(13, True), fill=(239, 247, 239))
    dr.rounded_rectangle([246, 160, 469, 271], 7, fill=(11, 18, 32), outline=(41, 74, 88), width=2)
    dr.text((258, 168), 'NAMIAR', font=font(9), fill=(107, 191, 242))
    dr.ellipse([322, 186, 394, 258], outline=(41, 74, 88), width=2)
    dr.text((352, 214), 'N', font=font(13, True), fill=(54, 151, 190))
    return img

pair = Image.new('RGB', (480 * 2 + 30, 360), (5, 8, 16))
pd = ImageDraw.Draw(pair)
pd.text((10, 8), 'YAGI (#11 z yagi.jpg)', font=font(15, True), fill=(84, 214, 190))
pd.text((500, 8), 'REPEATER (#2 z yagi.jpg)', font=font(15, True), fill=(84, 214, 190))
pair.paste(screen('YAGI', 'YAGI ANTENNA'), (0, 30))
pair.paste(screen('REPEATER', 'REPEATER'), (500, 30))
pair.save('.mock/modern_icons_device_screen.png')
print('ok')
