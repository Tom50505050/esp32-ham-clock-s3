import numpy as np
from PIL import Image, ImageDraw, ImageFont

rows = [(300, 1048), (1283, 2090), (2430, 3050), (3315, 4289)]
cols = [(173, 1034), (1319, 2042), (2389, 3328), (3594, 4280)]
CS, PAD = 280, 16
sheet = Image.new('RGB', (4 * (CS + PAD) + PAD, 4 * (CS + PAD) + PAD + 60), (5, 8, 16))
dr = ImageDraw.Draw(sheet)
try:
    fb = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 30)
    ft = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 22)
except Exception:
    fb = ft = ImageFont.load_default()
dr.text((PAD, 12), 'yagi.jpg - 16 ikon (numery 1-16). Ktora to ANTENA, ktora MASZT?', font=ft, fill=(84, 214, 190))
n = 0
for ri, (y0, y1) in enumerate(rows):
    for ci, (x0, x1) in enumerate(cols):
        n += 1
        crop = Image.open(f'cell_{ri}_{ci}.png')
        x = PAD + ci * (CS + PAD); y = 60 + PAD + ri * (CS + PAD)
        dr.rectangle([x, y, x + CS, y + CS], fill=(150, 200, 215), outline=(80, 80, 120), width=2)
        c = crop.copy(); c.thumbnail((CS - 12, CS - 12), Image.LANCZOS)
        sheet.paste(c, (x + (CS - c.width) // 2, y + (CS - c.height) // 2))
        dr.rectangle([x + 4, y + 4, x + 74, y + 46], fill=(0, 0, 0), outline=(255, 220, 80), width=2)
        dr.text((x + 16, y + 8), str(n), font=fb, fill=(255, 220, 80))
        dr.text((x + CS - 60, y + CS - 30), f'{crop.width}x{crop.height}', font=fb, fill=(0, 0, 0))
sheet.save('yagi_cells_numbered.png')
print('ok', sheet.size)
