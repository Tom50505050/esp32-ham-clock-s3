from PIL import Image, ImageDraw, ImageFont
import os
import numpy as np

F3 = ".mock/fluent3d"
CARD, CARD2, ACCENT, BORDER, TEXT, FAINT = (11, 18, 32), (16, 26, 44), (54, 151, 190), (41, 74, 88), (239, 247, 239), (107, 191, 242)

def font(sz, bold=False):
    p = "/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf" % ("-Bold" if bold else "")
    try:
        return ImageFont.truetype(p, sz)
    except Exception:
        return ImageFont.load_default()

def cutout(fn):
    bg = np.array([120, 171, 192])
    im = Image.open(fn).convert('RGB')
    a = np.asarray(im).astype(int)
    d = np.abs(a - bg).sum(axis=2)
    mask = (d > 60).astype(np.uint8) * 255
    from PIL import ImageFilter
    m = Image.fromarray(mask, 'L').filter(ImageFilter.MaxFilter(5))
    out = im.convert('RGBA'); out.putalpha(m)
    return out.crop(m.getbbox())

house_code = Image.open(".mock/code_house_64.png").convert("RGBA")
yagi_new = cutout(".mock/cell_2_2.png")   # #11
rep_new = cutout(".mock/cell_0_1.png")    # #2

# (kod APRS, plik 3D, opis) - None => ikona z kodu
SYMS = [
 ("HOUSE",   "code",                 "Home station"),
 ("CAR",     "automobile.png",        "CAR"),
 ("TRUCK",   "delivery_truck.png",    "Truck"),
 ("VAN",     "minibus.png",           "Van"),
 ("ROVER",   "automobile.png",        "Rover"),
 ("BIKE",    "bicycle.png",           "Bicycle"),
 ("HUMAN",   "pedestrian.png",        "Pedestrian"),
 ("SHIP",    "passenger_ship.png",    "Ship"),
 ("GLIDER",  "parachute.png",         "Glider"),
 ("AMBUL",   "ambulance.png",         "Ambulans"),
 ("FIRETRK", "fire_engine.png",       "Fire truck"),
 ("FIRE",    "fire.png",              "Fire"),
 ("HOSP",    "hospital.png",          "Hospital"),
 ("WX",      "code",                 "Weather station"),
 ("DIGI",    "antenna_bars.png",      "Digipeater"),
 ("REPEATER","yagi_rep",             "Repeater (#2)"),
 ("YAGI",    "yagi_ant",             "Yagi (#11)"),
 ("MICERPT", "radio.png",             "Symbol: MICERPT"),
 ("NODE",    "satellite.png",         "Node"),
 ("LORA",    "radio.png",             "LoRa"),
 ("LAPTOP",  "laptop.png",            "Mobile station"),
 ("LOGIN",   "mobile_phone.png",      "Login point"),
 ("GRID",    "globe_with_meridians.png","Symbol: GRID"),
 ("EOC",     "office_building.png",   "Symbol: EOC"),
 ("ICP",     "hut.png",               "Symbol: ICP"),
 ("TRKSTOP", "fuel_pump.png",         "Symbol: TRKSTOP"),
]

def icon_img(rec, size):
    code, src, _ = rec
    if code == "WX":
        base = house_code.resize((size, size), Image.NEAREST).copy()
        d = ImageDraw.Draw(base)
        bw, bh = int(size * 0.52), int(size * 0.32)
        x, y = size - bw - int(size * 0.03), size - bh - int(size * 0.03)
        d.rounded_rectangle([x, y, x + bw, y + bh], int(size * 0.08), fill=ACCENT, outline=(255, 255, 255), width=2)
        f = font(int(bh * 0.70), True)
        tw = d.textlength("WX", font=f)
        d.text((x + (bw - tw) / 2, y + bh * 0.07), "WX", font=f, fill=(0, 0, 0))
        return base
    if src == "code":
        return house_code.resize((size, size), Image.NEAREST)
    if src == "yagi_ant":
        im = yagi_new
    elif src == "yagi_rep":
        im = rep_new
    else:
        im = Image.open(os.path.join(F3, src)).convert("RGBA")
    im = im.copy(); im.thumbnail((size, size), Image.LANCZOS)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    canvas.paste(im, ((size - im.width) // 2, (size - im.height) // 2), im)
    return canvas

# ---- Arkusz: wszystkie symbole jako kafle 64 px na tle karty ----
COLS, TW, TH, PAD = 7, 128, 150, 12
rows = (len(SYMS) + COLS - 1) // COLS
W = COLS * (TW + PAD) + PAD
H = 74 + rows * (TH + PAD) + PAD
img = Image.new("RGB", (W, H), (5, 8, 16))
d = ImageDraw.Draw(img)
d.text((PAD, 12), "APRS ALERT (nowoczesny) - WSZYSTKIE ikony (64 px)", font=font(20, True), fill=ACCENT)
d.text((PAD, 42), "HOUSE/WX = ikona z kodu   |   YAGI #11 + REPEATER #2 = z yagi.jpg   |   pozostale = Fluent Emoji 3D (MIT)", font=font(12), fill=FAINT)
for i, rec in enumerate(SYMS):
    r, c = divmod(i, COLS)
    x = PAD + c * (TW + PAD); y = 74 + PAD + r * (TH + PAD)
    special = rec[1] in ("code", "yagi_ant", "yagi_rep")
    d.rounded_rectangle([x, y, x + TW, y + TH], 10, fill=CARD2 if special else CARD,
                        outline=ACCENT if special else BORDER, width=2)
    ic = icon_img(rec, 64)
    img.paste(ic, (x + (TW - 64) // 2, y + 10), ic)
    f = font(14, True)
    tw = d.textlength(rec[0], font=f)
    d.text((x + (TW - tw) / 2, y + 82), rec[0], font=f, fill=TEXT)
    fs = font(10)
    tw = d.textlength(rec[2], font=fs)
    d.text((x + (TW - tw) / 2, y + 100), rec[2], font=fs, fill=FAINT)
img.save(".mock/aprs-modern-all-icons.png")

# ---- Arkusz: realne karty STACJA dla HOUSE i WX (wariant A) ----
def station_card(rec, w=224, h=112):
    c = Image.new("RGB", (w, h), CARD)
    dc = ImageDraw.Draw(c)
    dc.rounded_rectangle([0, 0, w - 1, h - 1], 8, fill=CARD, outline=BORDER, width=2)
    dc.text((12, 8), "STACJA", font=font(11, True), fill=FAINT)
    ic = icon_img(rec, 64)
    c.paste(ic, (80, 24), ic)
    desc = rec[2].upper()
    tw = dc.textlength(desc, font=font(13, True))
    dc.text(((w - tw) / 2, 90), desc, font=font(13, True), fill=TEXT)
    return c

row = Image.new("RGB", (2 * 234 + 30, 132), (5, 8, 16))
row.paste(station_card(SYMS[0]), (10, 10))
row.paste(station_card(SYMS[13]), (254, 10))
row.save(".mock/aprs-modern-house-wx-cards.png")
print("ok", img.size)
