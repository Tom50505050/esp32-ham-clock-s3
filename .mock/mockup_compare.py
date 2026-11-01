#!/usr/bin/env python3
# Porownanie: obecna ikona auta (z firmware) vs nowy rower. TYLKO MOCKUP.
import importlib.util, sys
from PIL import Image, ImageDraw

spec = importlib.util.spec_from_file_location("mi", "/workspace/project/.mock/mockup_icons.py")
# nie uruchamiamy calego skryptu (zapisuje plik) - kopiujemy tylko potrzebne funkcje
from PIL import ImageFont
BG=(0x05,0x07,0x0d); CARD=(0x0b,0x12,0x20); CARD2=(0x13,0x1e,0x33)
ACC=(0x2a,0xa8,0xa8); BORD=(0x1e,0x3a,0x5f); TEXT=(0xee,0xf2,0xf7); FAINT=(0x7f,0xa0,0xc0)
WHITE=(255,255,255); LGREY=(190,195,200); DGREY=(70,78,90); YEL=(255,214,0); RED=(214,44,44); CYAN=(0,196,196)

def font(sz,bold=False):
    for p in ["/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf"%("-Bold" if bold else ""),
              "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"]:
        try: return ImageFont.truetype(p,sz)
        except Exception: pass
    return ImageFont.load_default()

# --- auto: dokladnie jak drawAprsAlertCarIcon() z firmware ---
def car(d,cx,topY):
    bodyW,bodyH,roofW,roofH=60,14,33,9
    bodyX=cx-bodyW//2; roofX=cx-roofW//2
    roofY=topY; bodyY=topY+9
    d.rounded_rectangle([bodyX,bodyY,bodyX+bodyW,bodyY+bodyH],2,fill=RED)
    d.rounded_rectangle([roofX,roofY,roofX+roofW,roofY+roofH],2,fill=RED)
    for wx in (bodyX+13, bodyX+bodyW-13):
        d.ellipse([wx-6,bodyY+bodyH-6,wx+6,bodyY+bodyH+6],fill=DGREY)
        d.ellipse([wx-3,bodyY+bodyH-3,wx+3,bodyY+bodyH+3],fill=LGREY)
    d.ellipse([bodyX-1,bodyY+1,bodyX+5,bodyY+7],fill=YEL)
    d.ellipse([bodyX-1,bodyY+6,bodyX+5,bodyY+12],fill=YEL)

def bike(d,cx,cy):
    import math
    r=17; rear=(cx-24,cy+14); front=(cx+24,cy+14); bb=(cx-2,cy+16)
    seat=(cx-14,cy-12); head=(cx+16,cy-12)
    for hub in (rear,front):
        d.ellipse([hub[0]-r,hub[1]-r,hub[0]+r,hub[1]+r],outline=DGREY,width=3)
        for a in range(0,360,45):
            x1=hub[0]+int((r-2)*math.cos(math.radians(a))); y1=hub[1]+int((r-2)*math.sin(math.radians(a)))
            x2=hub[0]-int((r-2)*math.cos(math.radians(a))); y2=hub[1]-int((r-2)*math.sin(math.radians(a)))
            d.line([(x1,y1),(x2,y2)],fill=LGREY,width=1)
        d.ellipse([hub[0]-3,hub[1]-3,hub[0]+3,hub[1]+3],fill=DGREY)
    for a,b in [(seat,bb),(bb,head),(seat,head),(bb,rear),(seat,rear),(head,front)]:
        d.line([a,b],fill=CYAN,width=4)
    d.line([(head[0],head[1]),(head[0]+4,head[1]-9)],fill=DGREY,width=3)
    d.line([(head[0]-2,head[1]-10),(head[0]+10,head[1]-10)],fill=DGREY,width=3)
    d.ellipse([seat[0]-7,seat[1]-5,seat[0]+5,seat[1]-1],fill=RED)
    d.ellipse([bb[0]-4,bb[1]-4,bb[0]+4,bb[1]+4],fill=DGREY)
    d.line([(bb[0],bb[1]),(bb[0]+6,bb[1]+6)],fill=DGREY,width=3)

W,H=560,320
img=Image.new("RGB",(W,H),BG); d=ImageDraw.Draw(img)
d.rectangle([0,0,W,52],fill=CARD); d.rectangle([0,0,W,3],fill=ACC)
d.text((20,10),"AUTO vs ROWER - ikony APRS (podglad)",font=font(20,True),fill=TEXT)
d.text((20,32),"po lewej obecne auto z firmware, po prawej nowy rower",font=font(12),fill=FAINT)

cells=[("AUTO (symbol />)", "obecny w firmware", car, "car"),
       ("ROWER (symbol /b)", "nowy", bike, "bike")]
for i,(title,sub,fn,kind) in enumerate(cells):
    x0=20+i*265; y0=70
    d.rounded_rectangle([x0,y0,x0+245,y0+225],10,fill=CARD,outline=BORD)
    d.rounded_rectangle([x0,y0,x0+5,y0+225],3,fill=ACC)
    cx=x0+122
    d.rounded_rectangle([cx-78,y0+22,cx+78,y0+150],8,fill=CARD2,outline=BORD)
    if kind=="car": fn(d,cx,y0+62)
    else: fn(d,cx,y0+92)
    d.text((cx,y0+170),title,font=font(13,True),fill=TEXT,anchor="mm")
    d.text((cx,y0+190),sub,font=font(12),fill=FAINT,anchor="mm")

img.save("/workspace/share/aprs-car-vs-bike.png")
print("zapisano /workspace/share/aprs-car-vs-bike.png",img.size)
