#!/usr/bin/env python3
# Podglad ikon APRS w stylu ekranu alertu (nowoczesny). TYLKO MOCKUP - bez zmian w kodzie.
import math
from PIL import Image, ImageDraw, ImageFont

W, H = 760, 900
BG   = (0x05, 0x07, 0x0d)
CARD = (0x0b, 0x12, 0x20)
CARD2= (0x13, 0x1e, 0x33)
ACC  = (0x2a, 0xa8, 0xa8)
BORD = (0x1e, 0x3a, 0x5f)
TEXT = (0xee, 0xf2, 0xf7)
FAINT= (0x7f, 0xa0, 0xc0)

WHITE=(255,255,255); LGREY=(190,195,200); DGREY=(70,78,90)
YEL=(255,214,0); ORG=(255,140,0); RED=(214,44,44); BLUE=(44,96,200)
LBLUE=(96,176,240); GRN=(44,176,96); CYAN=(0,196,196); BRN=(150,92,44)
DARK=(22,28,42)

img = Image.new("RGB", (W, H), BG)
d = ImageDraw.Draw(img)

def font(sz, bold=False):
    for p in ["/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf" % ("-Bold" if bold else ""),
              "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"]:
        try: return ImageFont.truetype(p, sz)
        except Exception: pass
    return ImageFont.load_default()

F_TITLE = font(20, True); F_LBL = font(13, True); F_SYM = font(12); F_NOTE = font(12)

def i_ship(d,cx,cy):
    d.polygon([(cx-36,cy+14),(cx+36,cy+14),(cx+24,cy+30),(cx-24,cy+30)], fill=BLUE)
    d.rectangle([cx-26,cy+18,cx+26,cy+22], fill=WHITE)
    d.rectangle([cx-16,cy-6,cx+16,cy+14], fill=WHITE)
    d.rectangle([cx-16,cy-6,cx+16,cy-1], fill=LGREY)
    for wx in range(-11,12,7): d.rectangle([cx+wx-2,cy+3,cx+wx+2,cy+7], fill=BLUE)
    d.line([(cx,cy-6),(cx,cy-32)], fill=DGREY, width=3)
    d.line([(cx-9,cy-25),(cx+9,cy-25)], fill=DGREY, width=3)
    d.rectangle([cx+18,cy-2,cx+26,cy+14], fill=RED)

def i_ambul(d,cx,cy):
    d.rounded_rectangle([cx-40,cy-14,cx+40,cy+16], 5, fill=WHITE)
    d.rectangle([cx-40,cy+4,cx+40,cy+9], fill=RED)
    d.rectangle([cx-4,cy-11,cx+4,cy+3], fill=RED)
    d.rectangle([cx-11,cy-7,cx+11,cy-1], fill=RED)
    d.polygon([(cx+28,cy-14),(cx+40,cy-14),(cx+40,cy-4),(cx+28,cy-4)], fill=LBLUE)
    d.ellipse([cx-32,cy+9,cx-16,cy+25], fill=DGREY)
    d.ellipse([cx+14,cy+9,cx+30,cy+25], fill=DGREY)

def i_bike(d,cx,cy):
    # Wyrazny rower: cienkie kola ze szprychami + trojkatna rama + kierownica.
    r = 17
    rear  = (cx-24, cy+14)
    front = (cx+24, cy+14)
    bb    = (cx-2,  cy+16)   # suport (korba)
    seat  = (cx-14, cy-12)
    head  = (cx+16, cy-12)   # glowka ramy
    for hub in (rear, front):
        d.ellipse([hub[0]-r,hub[1]-r,hub[0]+r,hub[1]+r], outline=DGREY, width=3)
        for a in range(0,360,45):
            x1=hub[0]+int((r-2)*math.cos(math.radians(a))); y1=hub[1]+int((r-2)*math.sin(math.radians(a)))
            x2=hub[0]-int((r-2)*math.cos(math.radians(a))); y2=hub[1]-int((r-2)*math.sin(math.radians(a)))
            d.line([(x1,y1),(x2,y2)], fill=LGREY, width=1)
        d.ellipse([hub[0]-3,hub[1]-3,hub[0]+3,hub[1]+3], fill=DGREY)
    # rama (trojkaty) - grube, kolorowe rury
    for a,b in [(seat,bb),(bb,head),(seat,head),(bb,rear),(seat,rear),(head,front)]:
        d.line([a,b], fill=CYAN, width=4)
    # kierownica
    d.line([(head[0],head[1]),(head[0]+4,head[1]-9)], fill=DGREY, width=3)
    d.line([(head[0]-2,head[1]-10),(head[0]+10,head[1]-10)], fill=DGREY, width=3)
    # siodelko
    d.ellipse([seat[0]-7,seat[1]-5,seat[0]+5,seat[1]-1], fill=RED)
    # korba + pedal
    d.ellipse([bb[0]-4,bb[1]-4,bb[0]+4,bb[1]+4], fill=DGREY)
    d.line([(bb[0],bb[1]),(bb[0]+6,bb[1]+6)], fill=DGREY, width=3)

def i_plane(d,cx,cy):
    d.polygon([(cx-34,cy-6),(cx+34,cy-6),(cx+34,cy+2),(cx-34,cy+2)], fill=WHITE)
    d.polygon([(cx-4,cy-32),(cx+4,cy-32),(cx+4,cy+30),(cx-4,cy+30)], fill=LGREY)
    d.polygon([(cx-16,cy+22),(cx+16,cy+22),(cx+16,cy+30),(cx-16,cy+30)], fill=WHITE)
    d.polygon([(cx-4,cy-32),(cx+4,cy-32),(cx+2,cy-24),(cx-2,cy-24)], fill=LBLUE)
    d.line([(cx-34,cy-2),(cx+34,cy-2)], fill=LBLUE, width=2)

def i_truck(d,cx,cy):
    d.rectangle([cx-42,cy-16,cx+2,cy+12], fill=WHITE, outline=DGREY, width=2)
    d.rectangle([cx+2,cy-8,cx+40,cy+12], fill=BLUE)
    d.rectangle([cx+30,cy-8,cx+40,cy-2], fill=LBLUE)
    d.rectangle([cx-42,cy-16,cx+2,cy-12], fill=LGREY)
    for wx in (-30,-6,24):
        d.ellipse([cx+wx-8,cy+6,cx+wx+8,cy+22], fill=DGREY)

def i_hosp(d,cx,cy):
    d.rectangle([cx-34,cy-20,cx+34,cy-14], fill=LGREY)
    d.rectangle([cx-30,cy-14,cx+30,cy+26], fill=WHITE)
    d.rectangle([cx-4,cy-8,cx+4,cy+10], fill=RED)
    d.rectangle([cx-12,cy-1,cx+12,cy+7], fill=RED)
    d.rectangle([cx-8,cy+14,cx+8,cy+26], fill=DGREY)
    for wx in (-24,18): d.rectangle([cx+wx,cy-6,cx+wx+6,cy+2], fill=LBLUE)

def i_digi(d,cx,cy):
    d.line([(cx-18,cy+30),(cx-6,cy-18)], fill=DGREY, width=3)
    d.line([(cx+18,cy+30),(cx+6,cy-18)], fill=DGREY, width=3)
    for yy in range(-10,30,12):
        f=(yy+10)/40.0; hx=int(18-(24*f))
        d.line([(cx-hx,yy),(cx+hx,yy+8)], fill=DGREY, width=2)
    d.line([(cx,cy-18),(cx,cy-32)], fill=DGREY, width=3)
    d.ellipse([cx-3,cy-36,cx+3,cy-30], fill=RED)
    for r in (10,16,22):
        d.arc([cx-r,cy-30-r,cx+r,cy-30+r], 205, 335, fill=CYAN, width=2)

def i_wx(d,cx,cy):
    d.ellipse([cx+4,cy-34,cx+34,cy-4], fill=YEL)
    for a in range(0,360,45):
        x1=cx+19+int(21*math.cos(math.radians(a))); y1=cy-19+int(21*math.sin(math.radians(a)))
        x2=cx+19+int(28*math.cos(math.radians(a))); y2=cy-19+int(28*math.sin(math.radians(a)))
        d.line([(x1,y1),(x2,y2)], fill=YEL, width=3)
    d.ellipse([cx-38,cy-6,cx-2,cy+20], fill=WHITE)
    d.ellipse([cx-22,cy-20,cx+14,cy+16], fill=WHITE)
    d.ellipse([cx+0,cy-4,cx+34,cy+20], fill=WHITE)
    d.rectangle([cx-38,cy+8,cx+34,cy+20], fill=WHITE)
    d.rectangle([cx-38,cy+16,cx+34,cy+20], fill=LGREY)

def i_laptop(d,cx,cy):
    d.rectangle([cx-30,cy-26,cx+30,cy+6], fill=DGREY)
    d.rectangle([cx-26,cy-22,cx+26,cy+2], fill=LBLUE)
    d.polygon([(cx-38,cy+6),(cx+38,cy+6),(cx+32,cy+16),(cx-32,cy+16)], fill=LGREY)
    d.rectangle([cx-8,cy+9,cx+8,cy+13], fill=DGREY)

def i_node(d,cx,cy):
    pts=[(cx,cy-24),(cx+24,cy),(cx,cy+24),(cx-24,cy)]
    for px,py in pts: d.line([(cx,cy),(px,py)], fill=CYAN, width=3)
    for px,py in pts: d.ellipse([px-8,py-8,px+8,py+8], fill=BLUE)
    d.ellipse([cx-11,cy-11,cx+11,cy+11], fill=ACC)

def i_repeater(d,cx,cy):
    d.line([(cx,cy-30),(cx,cy+22)], fill=DGREY, width=4)
    d.polygon([(cx-16,cy-8),(cx+16,cy-8),(cx,cy+4)], outline=RED, width=3)
    d.polygon([(cx-11,cy+2),(cx+11,cy+2),(cx,cy+12)], outline=RED, width=3)
    d.line([(cx-20,cy+26),(cx+20,cy+26)], fill=DGREY, width=3)
    for r in (12,18):
        d.arc([cx-r,cy-30-r,cx+r,cy-30+r], 205, 335, fill=CYAN, width=2)

def i_yagi(d,cx,cy):
    d.line([(cx-32,cy+8),(cx+32,cy+8)], fill=DGREY, width=3)
    for ex,el in ((-24,10),(-10,18),(4,22),(18,14),(30,8)):
        d.line([(cx+ex,cy+8-el),(cx+ex,cy+8+el)], fill=LGREY, width=2)
    d.line([(cx-2,cy+8),(cx-2,cy+30)], fill=DGREY, width=4)

def i_fire(d,cx,cy):
    d.polygon([(cx,cy-32),(cx+18,cy-6),(cx+13,cy+18),(cx-13,cy+18),(cx-18,cy-6)], fill=ORG)
    d.polygon([(cx,cy-16),(cx+9,cy+2),(cx+7,cy+18),(cx-7,cy+18),(cx-9,cy+2)], fill=YEL)
    d.polygon([(cx,cy+0),(cx+4,cy+8),(cx+3,cy+18),(cx-3,cy+18),(cx-4,cy+8)], fill=WHITE)

def i_firetruck(d,cx,cy):
    d.rectangle([cx-40,cy-12,cx+40,cy+14], fill=RED)
    d.rectangle([cx-40,cy-2,cx+40,cy+3], fill=WHITE)
    d.rectangle([cx+28,cy-12,cx+40,cy-4], fill=LBLUE)
    for i in range(5):
        d.line([(cx-30+i*13,cy-18),(cx-20+i*13,cy-8)], fill=LGREY, width=3)
    d.line([(cx-34,cy-18),(cx+22,cy-8)], fill=LGREY, width=3)
    for wx in (-26,0,26):
        d.ellipse([cx+wx-8,cy+8,cx+wx+8,cy+24], fill=DGREY)

def i_van(d,cx,cy):
    d.rounded_rectangle([cx-38,cy-16,cx+26,cy+14], 7, fill=WHITE)
    d.polygon([(cx+26,cy-16),(cx+40,cy-4),(cx+40,cy+14),(cx+26,cy+14)], fill=LGREY)
    d.rectangle([cx-30,cy-10,cx+18,cy+0], fill=LBLUE)
    d.polygon([(cx+28,cy-12),(cx+38,cy-4),(cx+38,cy+0),(cx+28,cy+0)], fill=LBLUE)
    for wx in (-24,20):
        d.ellipse([cx+wx-9,cy+8,cx+wx+9,cy+24], fill=DGREY)

def i_rover(d,cx,cy):
    d.rounded_rectangle([cx-34,cy-2,cx+34,cy+18], 7, fill=YEL)
    d.rounded_rectangle([cx-22,cy-14,cx+16,cy-2], 5, fill=ORG)
    d.rectangle([cx-16,cy-10,cx+10,cy-4], fill=LBLUE)
    for wx in (-22,22):
        d.ellipse([cx+wx-9,cy+12,cx+wx+9,cy+28], fill=DGREY)
    d.line([(cx+24,cy-14),(cx+24,cy-34)], fill=DGREY, width=3)
    d.polygon([(cx+24,cy-34),(cx+40,cy-30),(cx+24,cy-26)], fill=RED)

ICONS = [
    ("/s", "STATEK",      "Ship",          i_ship),
    ("/a", "AMBULANS",    "Ambulans",      i_ambul),
    ("/b", "ROWER",       "Bicycle",       i_bike),
    ("/g", "SAMOLOT",     "Glider/Plane",  i_plane),
    ("/k", "CIEZAROWKA",  "Truck",         i_truck),
    ("/h", "SZPITAL",     "Hospital",      i_hosp),
    ("/#", "DIGI",        "Digipeater",    i_digi),
    ("/_", "WX CHMURKA",  "Weather",       i_wx),
    ("/l", "LAPTOP",      "Mobile/Laptop", i_laptop),
    ("/n", "WEZEL",       "Node",          i_node),
    ("/r", "REPEATER",    "Repeater",      i_repeater),
    ("/y", "YAGI",        "Yagi antenna",  i_yagi),
    ("/d", "POZAR",       "Fire",          i_fire),
    ("/f", "WOZ STRAZ",   "Fire truck",    i_firetruck),
    ("/v", "VAN",         "Van",           i_van),
    ("/p", "ROVER",       "Rover",         i_rover),
]

d.rectangle([0,0,W,52], fill=CARD)
d.rectangle([0,0,W,3], fill=ACC)
d.text((20,10), "APRS ALERT - PROPOZYCJA NOWYCH IKON (podglad)", font=F_TITLE, fill=TEXT)
d.text((20,32), "styl nowoczesny | symbol -> ikona -> nazwa", font=F_NOTE, fill=FAINT)

cols, rows = 4, 4
cellW, cellH = 180, 200
mx, my = (W - cols*cellW)//2, 70
for i,(sym,name,en,fn) in enumerate(ICONS):
    r, c = divmod(i, cols)
    x0 = mx + c*cellW
    y0 = my + r*cellH
    d.rounded_rectangle([x0+8,y0+8,x0+cellW-8,y0+cellH-8], 10, fill=CARD, outline=BORD)
    d.rounded_rectangle([x0+8,y0+8,x0+12,y0+cellH-8], 3, fill=ACC)
    cx = x0 + cellW//2
    cy = y0 + 84
    d.rounded_rectangle([cx-62,cy-58,cx+62,cy+58], 8, fill=CARD2, outline=BORD)
    fn(d, cx, cy)
    d.text((cx, y0+cellH-46), name, font=F_LBL, fill=TEXT, anchor="mm")
    d.text((cx, y0+cellH-28), "symbol %s  (%s)" % (sym, en), font=F_SYM, fill=FAINT, anchor="mm")

img.save("/workspace/share/aprs-alert-icons.png")
print("zapisano /workspace/share/aprs-alert-icons.png", img.size)
