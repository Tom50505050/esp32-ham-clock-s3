import numpy as np
from PIL import Image, ImageDraw, ImageFont

im = Image.open('yagi_src.jpg').convert('RGB')
a = np.asarray(im).astype(int)
bg = np.array([120, 171, 192])
d = np.abs(a - bg).sum(axis=2)
isbg = d < 40
cols = [(173, 1034), (1319, 2042), (2389, 3328), (3594, 4280)]
rows = [(300, 1048), (1283, 2090), (2430, 3050), (3315, 4289)]
cells = {}
for ri, (y0, y1) in enumerate(rows):
    for ci, (x0, x1) in enumerate(cols):
        sub = isbg[y0:y1 + 1, x0:x1 + 1]
        ys, xs = np.where(~sub)
        crop = im.crop((x0 + xs.min(), y0 + ys.min(), x0 + xs.max() + 1, y0 + ys.max() + 1))
        cells[(ri, ci)] = crop
        crop.save(f'cell_{ri}_{ci}.png')
        print(f'cell_{ri}_{ci}: {crop.size}')
CS, PAD = 220, 14
sheet = Image.new('RGB', (4 * (CS + PAD) + PAD, 4 * (CS + PAD) + PAD + 40), (5, 8, 16))
dr = ImageDraw.Draw(sheet)
try:
    f = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 22)
except Exception:
    f = ImageFont.load_default()
dr.text((PAD, 8), 'yagi.jpg - 16 ikon w siatce 4x4 (numer = wiersz_kol)', font=f, fill=(84, 214, 190))
for (ri, ci), crop in cells.items():
    x = PAD + ci * (CS + PAD); y = 40 + PAD + ri * (CS + PAD)
    dr.rectangle([x, y, x + CS, y + CS], outline=(80, 80, 120), width=2)
    c = crop.copy(); c.thumbnail((CS - 8, CS - 8), Image.LANCZOS)
    sheet.paste(c, (x + (CS - c.width) // 2, y + (CS - c.height) // 2))
    dr.rectangle([x, y, x + 70, y + 28], fill=(0, 0, 0))
    dr.text((x + 6, y + 2), f'{ri}_{ci}', font=f, fill=(255, 220, 80))
sheet.save('yagi_grid_labeled.png')
print('kontaktowka ok', sheet.size)
