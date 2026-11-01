#!/usr/bin/env python3
# Generuje gotowe ikony APRS 48x48 PNG (aprs.fi / hessu, CC BY-SA 4.0) do data/aprs_icons/.
# Nazwy plikow = kody symboli z getAPRSSymbolShort() w main.cpp.
import os
from PIL import Image

SRC64_0 = "/workspace/project/.mock/aprs-symbols-64-0.png"
SRC64_1 = "/workspace/project/.mock/aprs-symbols-64-1.png"
OUT = "/workspace/project/data/aprs_icons"
SIZE = 48

os.makedirs(OUT, exist_ok=True)
T0 = Image.open(SRC64_0).convert("RGBA")
T1 = Image.open(SRC64_1).convert("RGBA")

def sprite(code, table):
    i = ord(code) - 33
    sheet = T0 if table == 0 else T1
    c, r = i % 16, i // 16
    return sheet.crop((c*64, r*64, c*64+64, r*64+64)).resize((SIZE, SIZE), Image.LANCZOS)

# (nazwa pliku, znak symbolu, tabela) - mapowanie wg getAPRSSymbolShort()
ICONS = [
    ("house",    '-', 0),  # /-  House (QTH)
    ("car",      '>', 0),  # />  Car
    ("digi",     '#', 0),  # /#  Digipeater
    ("repeater", 'r', 0),  # /r  Repeater
    ("human",    '[', 0),  # /[  Pedestrian / jogger
    ("laptop",   'L', 0),  # /L  Laptop / PC user
    ("wx",       '_', 0),  # /_  Weather station
    ("ship",     's', 0),  # /s  Ship / power boat
    ("ambul",    'a', 0),  # /a  Ambulance
    ("bike",     'b', 0),  # /b  Bicycle
    ("icp",      'c', 0),  # /c  Incident command post
    ("fire",     'd', 0),  # /d  Fire department
    ("firetrk",  'f', 0),  # /f  Fire truck
    ("hosp",     'h', 0),  # /h  Hospital
    ("yagi",     'y', 0),  # /y  Yagi at QTH
    ("glider",   'g', 0),  # /g  Glider
    ("truck",    'k', 0),  # /k, /u  Truck
    ("van",      'v', 0),  # /v  Van
    ("rover",    'p', 0),  # /p  Rover
    ("eoc",      'o', 0),  # /o  Emergency operations center
    ("trkstop",  't', 0),  # /t  Truck stop
    ("micerpt",  'm', 0),  # /m  Mic-E repeater
    ("node",     'n', 0),  # /n  Node
    ("grid",     'q', 0),  # /q  Grid square
]

made = []
for name, code, tbl in ICONS:
    img = sprite(code, tbl)
    path = os.path.join(OUT, name + ".png")
    img.save(path, "PNG", optimize=True)
    made.append((name, code, os.path.getsize(path)))

# LoRa - tabela dodatkowa (L z overlaye). Dwa warianty.
for name, code in [("lora_a", 'a'), ("lora_hash", '#')]:
    img = sprite(code, 1)
    path = os.path.join(OUT, name + ".png")
    img.save(path, "PNG", optimize=True)
    made.append((name, "\\" + code, os.path.getsize(path)))

print("Wygenerowano %d plikow PNG %dx%d w %s:" % (len(made), SIZE, SIZE, OUT))
for n, c, s in made:
    print("  %-12s symbol %-3s  %5d B" % (n, c, s))
