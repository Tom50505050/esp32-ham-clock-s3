from PIL import Image, ImageDraw, ImageFont
import os
D = ".mock/fluent3d"
sel = [
 ("HOUSE",   "house.png",            "House"),
 ("CAR",     "automobile.png",       "Automobile"),
 ("TRUCK",   "delivery_truck.png",   "Delivery truck"),
 ("VAN",     "minibus.png",          "Minibus"),
 ("BIKE",    "bicycle.png",          "Bicycle"),
 ("HUMAN",   "pedestrian.png",       "Man walking"),
 ("SHIP",    "passenger_ship.png",   "Passenger ship"),
 ("GLIDER",  "parachute.png",        "Parachute"),
 ("AMBUL",   "ambulance.png",        "Ambulance"),
 ("FIRETRK", "fire_engine.png",      "Fire engine"),
 ("FIRE",    "fire.png",             "Fire"),
 ("HOSP",    "hospital.png",         "Hospital"),
 ("WX",      "cloud_with_rain.png",  "Cloud with rain"),
 ("DIGI",    "antenna_bars.png",     "Antenna bars"),
 ("YAGI",    "satellite_antenna.png","Satellite antenna"),
 ("LORA",    "radio.png",            "Radio"),
 ("GRID",    "globe_with_meridians.png","Globe w/ meridians"),
 ("EOC",     "office_building.png",  "Office building"),
 ("ICP",     "hut.png",              "Hut"),
 ("TRKSTOP", "fuel_pump.png",        "Fuel pump"),
 ("LAPTOP",  "laptop.png",           "Laptop"),
]
CELL, PAD, COLS = 150, 16, 5
rows = (len(sel) + COLS - 1) // COLS
W = COLS * (CELL + PAD) + PAD
H = rows * (CELL + 46 + PAD) + PAD + 70
img = Image.new("RGB", (W, H), (11, 18, 32))
dr = ImageDraw.Draw(img)
try:
    f = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 18)
    fs = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 15)
    ft = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 26)
except Exception:
    f = fs = ft = ImageFont.load_default()
dr.text((PAD, 18), "APRS ALERT (nowoczesny) - propozycja ikon 3D", font=ft, fill=(84, 214, 190))
dr.text((PAD, 50), "Microsoft Fluent Emoji 3D (MIT) - podglad 96 px", font=fs, fill=(150, 170, 190))
for i, (code, fn, desc) in enumerate(sel):
    r, c = divmod(i, COLS)
    x = PAD + c * (CELL + PAD); y = 70 + PAD + r * (CELL + 46 + PAD)
    dr.rounded_rectangle([x, y, x + CELL, y + CELL], 10, fill=(16, 26, 44), outline=(41, 74, 88), width=2)
    p = os.path.join(D, fn)
    if os.path.exists(p):
        ic = Image.open(p).convert("RGBA").resize((96, 96), Image.LANCZOS)
        img.paste(ic, (x + (CELL - 96) // 2, y + (CELL - 96) // 2), ic)
    dr.text((x + 8, y + CELL + 4), code, font=f, fill=(240, 240, 240))
    dr.text((x + 8, y + CELL + 24), desc, font=fs, fill=(150, 170, 190))
img.save(".mock/aprs-modern-3d-proposal.png")
print("zapisano", img.size)
