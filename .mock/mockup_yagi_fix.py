import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

bg = np.array([120, 171, 192])
CARD, ACCENT, BORDER, TEXT, FAINT = (11, 18, 32), (54, 151, 190), (41, 74, 88), (239, 247, 239), (107, 191, 242)

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

def tile(img, size):
    c = img.copy(); c.thumbnail((size, size), Image.LANCZOS); return c

# KOREKTA wg uzytkownika: #2 = repeater (cell_0_1), #11 = yagi (cell_2_2)
yagi_new = cutout('cell_2_2.png')     # #11
rep_new = cutout('cell_0_1.png')      # #2
cur_yagi = Image.open('../data/aprs_icons/yagi.png').convert('RGBA')
cur_rep = Image.open('../data/aprs_icons/repeater.png').convert('RGBA')

sheet = Image.new('RGB', (860, 470), (5, 8, 16))
d = ImageDraw.Draw(sheet)
d.text((16, 12), 'KOREKTA: #11 = YAGI (antena), #2 = REPEATER (maszt)', font=font(20, True), fill=ACCENT)

def block(y, title, cur, new, newlabel):
    d.text((16, y), title, font=font(16, True), fill=TEXT)
    d.text((16, y + 24), 'obecna ikona', font=font(11), fill=FAINT)
    t1 = tile(cur, 120); sheet.paste(t1, (16, y + 42), t1)
    d.text((180, y + 24), newlabel, font=font(11, True), fill=ACCENT)
    t2 = tile(new, 120); sheet.paste(t2, (180, y + 42), t2)
    # wiekszy podglad
    d.text((340, y + 24), 'powiekszenie 200 px', font=font(11), fill=FAINT)
    t3 = tile(new, 200); sheet.paste(t3, (340, y + 42), t3)

block(54, 'YAGI  ->  #11 (antena)', cur_yagi, yagi_new, 'NOWA #11')
block(250, 'REPEATER  ->  #2 (maszt)', cur_rep, rep_new, 'NOWA #2')

# kolumna z pozostalymi kandydatami do porownania
d.text((590, 24), 'wszystkie kandydaci:', font=font(12, True), fill=TEXT)
for i, (lab, fn) in enumerate([('#11 yagi', 'cell_2_2.png'), ('#2 repeater', 'cell_0_1.png'), ('#4', 'cell_0_3.png'), ('#13', 'cell_3_0.png')]):
    c = cutout(fn); t = tile(c, 150)
    y = 50 + i * 100
    sheet.paste(t, (590, y), t)
    d.text((748, y + 60), lab, font=font(11, True), fill=ACCENT)
sheet.save('yagi_fix_compare.png')

def card(icon, desc):
    c = Image.new('RGB', (224, 112), CARD); dc = ImageDraw.Draw(c)
    dc.rounded_rectangle([0, 0, 223, 111], 8, fill=CARD, outline=BORDER, width=2)
    dc.text((12, 8), 'STACJA', font=font(11, True), fill=FAINT)
    t = tile(icon, 64); c.paste(t, (112 - t.width // 2, 24 + (64 - t.height) // 2), t)
    tw = dc.textlength(desc, font=font(13, True))
    dc.text(((224 - tw) / 2, 90), desc, font=font(13, True), fill=TEXT)
    return c

row = Image.new('RGB', (2 * 234 + 30, 132), (5, 8, 16))
row.paste(card(yagi_new, 'YAGI ANTENNA'), (10, 10))
row.paste(card(rep_new, 'REPEATER / MAST'), (254, 10))
row.save('yagi_fix_cards.png')
print('ok')
