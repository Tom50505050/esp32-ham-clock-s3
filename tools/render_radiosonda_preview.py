#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Makieta proponowanego ekranu RADIOSONDY (480x320) dla ESP32-HAM-CLOCK.
Styl bedzie spójny z istniejacym ekranem ISS TRACKER:
  - czarne tlo, ciemne karty (0x0884 ~ #0b1220), teal akcent,
  - mapa swiata (uproszczona), znacznik QTH + linia do sondy,
  - naglowek z kropka "NA ZYWO".
Uzywa PIL z czcionka DejaVu (bez zapisu do firmware; tylko wizualizacja).
Wynik: /tmp/radiosonda_preview.png
"""
import os
import math
from PIL import Image, ImageDraw, ImageFont

W, H = 480, 320
ROOT = os.path.dirname(os.path.abspath(__file__))

# Paleta zgodna z ISS TRACKER (main.cpp ISS_COL_*)
C_BG      = (0, 0, 0)
C_CARD    = (11, 18, 32)     # 0x0884
C_BORDER  = (42, 51, 87)     # 0x298A
C_ACCENT  = (53, 208, 186)   # 0x3697 teal
C_ACCENT2 = (78, 168, 255)   # 0x4D5F blue
C_TEXT    = (232, 238, 247)  # 0xEF7E
C_FAINT   = (108, 125, 148)  # 0x6BF2
C_LAND    = (42, 74, 60)     # 0x2A47
C_WATER   = (16, 22, 38)     # pod karta mapy
C_RED     = (235, 68, 68)
C_RED_DIM = (90, 30, 30)
C_GREEN   = (74, 222, 128)
C_ORANGE  = (255, 168, 30)
C_YELLOW  = (255, 224, 0)

def F(size, bold=False):
    return ImageFont.truetype(
        "/usr/share/fonts/truetype/dejavu/DejaVu%s.ttf" % ("Sans-Bold" if bold else "Sans"),
        size)

def card(d, x, y, w, h, accent=True):
    d.rectangle([x, y, x + w - 1, y + h - 1], fill=C_CARD)
    d.rectangle([x, y, x + w - 1, y + h - 1], outline=C_BORDER)
    if accent:
        d.rectangle([x, y, x + 3, y + h - 1], fill=C_ACCENT)

def label(d, x, y, text, f=F(11)):
    d.text((x, y), text, font=f, fill=C_FAINT)

def value(d, x, y, text, col=C_TEXT, f=F(15, True)):
    d.text((x, y), text, font=f, fill=col)

# ---- mapa świata: 1-bit maska ladów z iss_worldmask.h (228x114) ----
def load_worldmask():
    # wyrzucamy linie z naglowkiem / #pragma; zostaja tylko bajty 0x..
    import re
    p = os.path.join(ROOT, "..", "iss_worldmask.h")
    if not os.path.exists(p):
        p = os.path.join(ROOT, "iss_worldmask.h")
    txt = open(p, "r", encoding="utf-8", errors="ignore").read()
    hexes = re.findall(r"0x([0-9A-Fa-f]{2})", txt)
    return bytes(int(h, 16) for h in hexes)

MWM_BYTES = load_worldmask()
MWM_W, MWM_H = 228, 114   # ISS_WORLDMASK_W/H

def world_map(d, mx, my, mw, mh):
    d.rectangle([mx, my, mx + mw - 1, my + mh - 1], fill=C_WATER)
    for i in range(MWM_W * MWM_H):
        by = MWM_BYTES[i >> 3]
        bit = (by >> (7 - (i & 7))) & 1
        if not bit:
            continue
        sx, sy = i % MWM_W, i // MWM_W
        x = mx + sx * mw // MWM_W
        y = my + sy * mh // MWM_H
        d.rectangle([x, y, x + max(1, mw // MWM_W) - 1, y + max(1, mh // MWM_H) - 1], fill=C_LAND)
    d.rectangle([mx, my, mx + mw - 1, my + mh - 1], outline=C_BORDER)

def proj(lng, lat, mx, my, mw, mh):
    x = mx + (lng + 180) / 360 * mw
    y = my + (90 - lat) / 180 * mh
    return int(x), int(y)

def haversine(lat1, lon1, lat2, lon2):
    R = 6371.0
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp = math.radians(lat2 - lat1)
    dl = math.radians(lon2 - lon1)
    a = math.sin(dp/2)**2 + math.cos(p1)*math.cos(p2)*math.sin(dl/2)**2
    return 2 * R * math.asin(math.sqrt(a))

# ---- BUDOWA EKRANU ----
img = Image.new("RGB", (W, H), C_BG)
d = ImageDraw.Draw(img)

# Naglowek
d.rectangle([0, 0, 479, 29], fill=C_CARD)
d.rectangle([0, 29, 479, 29], fill=C_ACCENT)
d.text((240 - d.textlength("ESP32-HAM-CLOCK", font=F(11))/2, 4), "ESP32-HAM-CLOCK", font=F(11), fill=C_FAINT)
f_title = F(16, True)
d.text((240 - d.textlength("RADIOSONDY", font=f_title)/2, 12), "RADIOSONDY", font=f_title, fill=C_TEXT)

# kropka NA ZYWO
d.ellipse([400, 9, 410, 19], fill=C_RED_DIM)
d.ellipse([403, 12, 407, 16], fill=C_RED)
d.ellipse([404, 13, 406, 15], outline=C_TEXT, width=1)
d.text((413, 9), "NA ZYWO", font=F(11), fill=C_ACCENT)

# ---- LEWA KOLUMNA METRYK ----
LX, LW = 4, 118
y0 = 34
rows = [
    ("SYSIAL", "RS41:2", C_ACCENT),
    ("KODOWANIE", "SGP", C_TEXT),
    ("NAD. MHz", "401.600", C_YELLOW),
    ("WYSOKOŚĆ", "32.4 km", C_TEXT),
    ("DIST QTH", "236 km", C_ACCENT),
    ("AZ/EL", "214/71", C_TEXT),
]
for i, (k, v, col) in enumerate(rows):
    y = y0 + i * 33
    card(d, LX, y, LW, 29)
    label(d, LX + 8, y + 3, k, F(10))
    value(d, LX + 8, y + 13, v, col, F(13, True))

# ---- PRAWA STRONA: MATA MAPA ----
MX, MY, MW, MH = 128, 34, 348, 170
world_map(d, MX, MY, MW, MH)

# QTH (Warszawa) i sonda
qth = (52.2, 21.0)      # SP9TNV QTH
sonde = (52.85, 17.0)   # przykładowa sonda
qx, qy = proj(qth[1], qth[0], MX, MY, MW, MH)
sx, sy = proj(sonde[1], sonde[0], MX, MY, MW, MH)
# linia QTH->sonda
d.line([qx, qy, sx, sy], fill=C_ACCENT, width=1)
# QTH - bialy punkt + pierscien
d.ellipse([qx-3, qy-3, qx+3, qy+3], fill=C_TEXT)
d.ellipse([qx-6, qy-6, qx+6, qy+6], outline=C_TEXT, width=1)
d.text((qx+8, qy-4), "QTH", font=F(10), fill=C_TEXT)
# Sonda - pomaranczowy znacznik + etykieta
d.ellipse([sx-4, sy-4, sx+4, sy+4], fill=C_ORANGE)
d.ellipse([sx-7, sy-7, sx+7, sy+7], outline=C_ORANGE, width=1)
d.text((sx+9, sy-6), "RS41 32.4km", font=F(10), fill=C_ORANGE)

# Pasek pod mapa: info o lat/odswiezeniu
d.text((MX+2, MY+MH+3), "Mapa: sondy w promieniu 500 km   •   odśwież 2 min", font=F(10), fill=C_FAINT)

# ---- DOLNA LISTA SOND (tabela) ----
TBX, TBY, TBW, TBH = 4, 232, 472, 84
d.rectangle([TBX, TBY, TBX+TBW-1, TBY+TBH-1], fill=C_CARD)
d.rectangle([TBX, TBY, TBX+TBW-1, TBY+TBH-1], outline=C_BORDER)
# nagłówek tabeli
d.rectangle([TBX, TBY, TBX+TBW-1, TBY+17], fill=(16, 24, 40))
heads = ["SYSIAL", "TYP", "WYS.", "TEMPERATURA", "DIST", "AZ", "NAD. MHz", "AKT"]
hx = [10, 90, 150, 210, 280, 320, 370, 430]
f_small = F(10, True)
f_row = F(11)
for x, h in zip(hx, heads):
    d.text((TBX+x, TBY+4), h, font=f_small, fill=C_FAINT)
row_cols = [
    ("X0334855", "RS41", "23.8 km", "-58.3°C", "212 km", "215°", "401.600", C_GREEN),
    ("709-2-11767", "M10", "31.4 km", "-48.1°C", "149 km", "247°", "404.200", C_GREEN),
    ("S5100325", "RS41", "11.2 km", "-37.0°C", "88 km", "182°", "403.501", C_GREEN),
    ("DFM21234", "DFM-17", "2.1 km", "+6.2°C", "41 km", "96°", "400.900", C_ACCENT),
]
row_y = TBY + 21
for r in row_cols:
    vals = list(r)
    txt_col = vals[7]
    # status: kropka zielona dla AKT, teal jesli w woreczku w gore (wznoszenie)
    d.text((TBX+10, row_y), vals[0], font=f_row, fill=C_TEXT)
    d.text((TBX+90, row_y), vals[1], font=f_row, fill=C_ACCENT if vals[1]=="M10" else C_TEXT)
    d.text((TBX+150, row_y), vals[2], font=f_row, fill=C_TEXT)
    d.text((TBX+210, row_y), vals[3], font=f_row, fill=C_TEXT)
    d.text((TBX+280, row_y), vals[4], font=f_row, fill=C_TEXT)
    d.text((TBX+320, row_y), vals[5], font=f_row, fill=C_TEXT)
    d.text((TBX+370, row_y), vals[6], font=f_row, fill=C_YELLOW)
    d.ellipse([TBX+430, row_y+4, TBX+436, row_y+10], fill=vals[7])
    d.text((TBX+442, row_y), "NAD", font=F(9), fill=C_FAINT)
    row_y += 15

# stopka
d.text((4, 318), "QTH: SP9TNV JO02 • aktualizacja: 08:20 UTC", font=F(9), fill=C_FAINT)

out = "/tmp/radiosonda_preview.png"
img.save(out)
print("Zapisano:", out, img.size)

# =========================================================================================
# EKRAN 2: RADAR SOND - lokalny zoom wokol QTH (promienie 50/100/200/400 km)
# Kolejny "tryb odwrócenia" (tap na mapie) - jak ISS ma tryb pelnej mapy.
# =========================================================================================
img2 = Image.new("RGB", (W, H), C_BG)
d = ImageDraw.Draw(img2)

# naglowek
d.rectangle([0, 0, 479, 29], fill=C_CARD)
d.rectangle([0, 29, 479, 29], fill=C_ACCENT)
d.text((240 - d.textlength("ESP32-HAM-CLOCK", font=F(11))/2, 4), "ESP32-HAM-CLOCK", font=F(11), fill=C_FAINT)
d.text((240 - d.textlength("RADAR SOND", font=f_title)/2, 12), "RADAR SOND", font=f_title, fill=C_TEXT)
d.ellipse([400, 9, 410, 19], fill=C_RED_DIM)
d.ellipse([403, 12, 407, 16], fill=C_RED)
d.ellipse([404, 13, 406, 15], outline=C_TEXT, width=1)
d.text((413, 9), "NA ZYWO", font=F(11), fill=C_ACCENT)

# Lewa kolumna: cel wybrany (X0334855)
LX2, LW2 = 4, 118
y2 = 34
rows2 = [
    ("CEL", "X0334855", C_ACCENT),
    ("TYP / ODKOD.", "RS41 / SGP", C_TEXT),
    ("WYSOKOSC", "23.8 km", C_TEXT),
    ("TEMPERATURA", "-58.3°C", C_ACCENT2),
    ("DIST / AZ", "212 km / 215°", C_TEXT),
    ("WEKTOR", "NE 47 km/h", C_ACCENT),
    ("NAD. MHz", "401.600", C_YELLOW),
    ("SNR / BATT", "9 dB / 2.7V", C_FAINT),
]
for i, (k, v, col) in enumerate(rows2):
    y = y2 + i * 26
    card(d, LX2, y, LW2, 22)
    label(d, LX2 + 7, y + 2, k, F(9))
    value(d, LX2 + 7, y + 10, v, col, F(11, True))

# Prawa: radar lokalny z promieniami - SZEROKOSC/DLUGOSC/QTH wzgledne
RX, RY, R = 320, 150, 105   # srodek radaru
d.ellipse([RX-R-10, RY-R-10, RX+R+10, RY+R+10], fill=C_WATER)
d.ellipse([RX-R, RY-R, RX+R, RY+R], outline=C_BORDER)
r_rings = [R, int(R*0.707), int(R*0.5), int(R*0.25)]
for rr in r_rings:
    d.ellipse([RX-rr, RY-rr, RX+rr, RY+rr], outline=C_BORDER)
# osie N/E/S/W
d.line([RX, RY-R, RX, RY+R], fill=C_BORDER)
d.line([RX-R, RY, RX+R, RY], fill=C_BORDER)
d.text((RX-4, RY-R-14), "N", font=F(10), fill=C_FAINT)
d.text((RX-4, RY+R+2), "S", font=F(10), fill=C_FAINT)
d.text((RX+R+4, RY-6), "E", font=F(10), fill=C_FAINT)
d.text((RX-R-12, RY-6), "W", font=F(10), fill=C_FAINT)
# etykiety zasiegu
d.text((RX+6, RY-R+2), "400", font=F(8), fill=C_FAINT)
d.text((RX+6, RY-int(R*0.5)+2), "200", font=F(8), fill=C_FAINT)

# QTH srodek
d.ellipse([RX-4, RY-4, RX+4, RY+4], fill=C_TEXT)
d.ellipse([RX-7, RY-7, RX+7, RY+7], outline=C_TEXT, width=1)
d.text((RX+10, RY-6), "QTH", font=F(9), fill=C_TEXT)
# sondy na radarze (pol. azymut/odleglosc -> wspolrzedne ekranu)
# sonda1: 212km @215, sonda2: 149km @247, sonda3: 88km @182, sonda4: 41km @96
def radar_point(dist_km, az, Rmax=400):
    r = dist_km / Rmax * R
    a = math.radians(az - 90)
    return RX + int(r * math.cos(a)), RY + int(r * math.sin(a))

for dist, az, name, col, isSel in [
        (212, 215, "X0334855", C_ORANGE, True),
        (149, 247, "709-2-11767", C_GREEN, False),
        (88, 182, "S5100325", C_GREEN, False),
        (41, 96, "DFM21234", C_ACCENT, False),
]:
    px, py = radar_point(dist, az)
    d.ellipse([px-3, py-3, px+3, py+3], fill=col)
    d.ellipse([px-6, py-6, px+6, py+6], outline=col, width=1 if isSel else 1)
    if isSel:
        d.line([RX, RY, px, py], fill=col, width=1)
        d.text((px+10, py-12), name + " 212km", font=F(9), fill=col)
    else:
        name_short = name[:5]
        d.text((px+7, py-6), name_short, font=F(8), fill=col)
# legenda
d.text((RX-R, RY+R+16), "mape: tap = globalna / radar", font=F(9), fill=C_FAINT)

# Dolna lista (podobna do ekranu 1, ale bez kolumny AKT - do wykorzystania w trybie radarowym)
TBX, TBY, TBW, TBH = 4, 232, 472, 84
d.rectangle([TBX, TBY, TBX+TBW-1, TBY+TBH-1], fill=C_CARD)
d.rectangle([TBX, TBY, TBX+TBW-1, TBY+TBH-1], outline=C_BORDER)
d.rectangle([TBX, TBY, TBX+TBW-1, TBY+17], fill=(16, 24, 40))
# naglowek
for x, h in zip([10, 90, 150, 210, 280, 320, 370, 430], ["SYSIAL","TYP","WYS.","TEMPERATURA","DIST","AZ","NAD. MHz","AKT"]):
    d.text((TBX+x, TBY+4), h, font=f_small, fill=C_FAINT)
row_y = TBY + 21
for r in row_cols:
    vals = list(r)
    d.text((TBX+10, row_y), vals[0], font=f_row, fill=C_TEXT)
    d.text((TBX+90, row_y), vals[1], font=f_row, fill=C_ACCENT if vals[1]=="M10" else C_TEXT)
    d.text((TBX+150, row_y), vals[2], font=f_row, fill=C_TEXT)
    d.text((TBX+210, row_y), vals[3], font=f_row, fill=C_TEXT)
    d.text((TBX+280, row_y), vals[4], font=f_row, fill=C_TEXT)
    d.text((TBX+320, row_y), vals[5], font=f_row, fill=C_TEXT)
    d.text((TBX+370, row_y), vals[6], font=f_row, fill=C_YELLOW)
    d.ellipse([TBX+430, row_y+4, TBX+436, row_y+10], fill=vals[7])
    d.text((TBX+442, row_y), "NAD", font=F(9), fill=C_FAINT)
    row_y += 15
d.text((4, 318), "QTH: SP9TNV JO02 • tryb radaru lokalnego", font=F(9), fill=C_FAINT)

out2 = "/tmp/radiosonda_radar_preview.png"
img2.save(out2)
print("Zapisano:", out2, img2.size)