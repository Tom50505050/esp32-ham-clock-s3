import numpy as np
from PIL import Image, ImageDraw, ImageFont

bg = np.array([120, 171, 192])
CARD, CARD2, ACCENT, BORDER, TEXT, FAINT = (11, 18, 32), (16, 26, 44), (54, 151, 190), (41, 74, 88), (239, 247, 239), (107, 191, 242)

def font(sz, bold=False):
    p = "/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf" % ("-Bold" if bold else "")
    try:
        return ImageFont.truetype(p, sz)
    except Exception:
        return ImageFont.load_default()

def cutout(fn):
    """Wycina obiekt z komorki (bez tla) i zwraca RGBA."""
    im = Image.open(fn).convert('RGB')
    a = np.asarray(im).astype(int)
    d = np.abs(a - bg).sum(axis=2)
    mask = (d > 60).astype(np.uint8) * 255
    m = Image.fromarray(mask, 'L').filter(__import__('PIL.ImageFilter', fromlist=['x']).MaxFilter(5))
    out = im.convert('RGBA')
    out.putalpha(m)
    bbox = m.getbbox()
    return out.crop(bbox)

def tile(img, size):
    c = img.copy(); c.thumbnail((size, size), Image.LANCZOS)
    return c

CELLS = {'#4 yagi_0_3': 'cell_0_3.png', '#13 mast_3_0': 'cell_3_0.png', '#16 mast_3_3': 'cell_3_3.png', '#10 mast_2_1': 'cell_2_1.png'}

# --- porownanie: obecna ikona vs propozycja ---
cur_yagi = Image.open('../data/aprs_icons/yagi.png').convert('RGBA')
cur_rep = Image.open('../data/aprs_icons/repeater.png').convert('RGBA')

sheet = Image.new('RGB', (760, 470), (5, 8, 16))
d = ImageDraw.Draw(sheet)
d.text((16, 12), 'YAGI i REPEATER - obecne ikony vs propozycja z yagi.jpg', font=font(20, True), fill=ACCENT)

# wiersz 1: YAGI
d.text((16, 54), 'YAGI', font=font(16, True), fill=TEXT)
d.text((16, 78), 'obecna (z kodu/LittleFS)', font=font(11), fill=FAINT)
sheet.paste(tile(cur_yagi, 110), (16, 96), tile(cur_yagi, 110))
prop = cutout(CELLS['#4 yagi_0_3'])
sheet.paste(tile(prop, 110), (150, 96), tile(prop, 110))
d.text((150, 78), 'PROPOZYCJA #4 (antena)', font=font(11, True), fill=ACCENT)

# wiersz 2: REPEATER
d.text((16, 250), 'REPEATER', font=font(16, True), fill=TEXT)
d.text((16, 274), 'obecna (z kodu/LittleFS)', font=font(11), fill=FAINT)
sheet.paste(tile(cur_rep, 110), (16, 292), tile(cur_rep, 110))
prop2 = cutout(CELLS['#13 mast_3_0'])
sheet.paste(tile(prop2, 110), (150, 292), tile(prop2, 110))
d.text((150, 274), 'PROPOZYCJA #13 (maszt)', font=font(11, True), fill=ACCENT)

# prawa strona: wszystkie kandydaci masztu
d.text((330, 54), 'kandydaci na MASZT:', font=font(13, True), fill=TEXT)
x = 330
for i, (lab, fn) in enumerate([('#13', 'cell_3_0.png'), ('#16', 'cell_3_3.png'), ('#10', 'cell_2_1.png')]):
    c = cutout(fn); t = tile(c, 120)
    sheet.paste(t, (x, 78 + (120 - t.height) // 2), t)
    d.text((x, 200), lab, font=font(13, True), fill=ACCENT)
    x += 140
sheet.save('yagi_proposal_compare.png')

# --- realna karta STACJA z proponowanymi ikonami ---
def card(icon, desc, col=ACCENT):
    c = Image.new('RGB', (224, 112), CARD)
    dc = ImageDraw.Draw(c)
    dc.rounded_rectangle([0, 0, 223, 111], 8, fill=CARD, outline=BORDER, width=2)
    dc.text((12, 8), 'STACJA', font=font(11, True), fill=FAINT)
    t = tile(icon, 64)
    c.paste(t, (112 - t.width // 2, 24 + (64 - t.height) // 2), t)
    tw = dc.textlength(desc, font=font(13, True))
    dc.text(((224 - tw) / 2, 90), desc, font=font(13, True), fill=col)
    return c

row = Image.new('RGB', (2 * 234 + 30, 132), (5, 8, 16))
row.paste(card(prop, 'YAGI ANTENNA'), (10, 10))
row.paste(card(prop2, 'REPEATER / MAST'), (254, 10))
row.save('yagi_proposal_cards.png')
print('ok')
