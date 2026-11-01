#!/usr/bin/env python3
# Klasyczny APRS z ikonami aprs.fi w skali 48x48 + pasek porownania skal.
# TYLKO MOCKUP - bez zmian w kodzie firmware.
import math
from PIL import Image, ImageDraw, ImageFont

T0 = Image.open("/workspace/project/.mock/aprs-symbols-64-0.png").convert("RGBA")
T1 = Image.open("/workspace/project/.mock/aprs-symbols-64-1.png").convert("RGBA")

def rgb565(v):
    r=(v>>11)&0x1F; g=(v>>5)&0x3F; b=v&0x1F
    return (r*255//31, g*255//63, b*255//31)

BLACK=(0,0,0); ORANGE=rgb565(0xFD20); GREY=rgb565(0x8410)
LGREY=(190,195,200); DGREY=(70,78,90); WHITE=(255,255,255)
RED=(214,44,44)

def font(sz,bold=False):
    for p in ["/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf"%("-Bold" if bold else ""),
              "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"]:
        try: return ImageFont.truetype(p,sz)
        except Exception: pass
    return ImageFont.load_default()

def sprite(code, table=0, size=64, resample=Image.LANCZOS):
    i=ord(code)-33; sheet=T0 if table==0 else T1
    c,r=i%16,i//16
    im=sheet.crop((c*64,r*64,c*64+64,r*64+64))
    return im.resize((size,size), resample) if size!=64 else im

# ---------- 1) EKRAN KLASYCZNY z ikona 48x48 (skala 2x) ----------
S=2
def S2(v): return v*S
scr=Image.new("RGB",(480*S,320*S),BLACK); d=ImageDraw.Draw(scr)
def txt(x,y,t,col,sz,bold=False,anchor=None): d.text((S2(x),S2(y)),t,font=font(sz,bold),fill=col,anchor=anchor)

d.rectangle([0,0,S2(480)-1,S2(45)-1],fill=ORANGE)
d.line([0,S2(44),S2(480),S2(44)],fill=GREY,width=1)
txt(150,8,"APRS ALERT",BLACK,36,True)
d.rounded_rectangle([S2(449),S2(7),S2(475),S2(33)],S2(5),fill=ORANGE,outline=BLACK,width=2)
d.line([S2(456),S2(14),S2(468),S2(26)],fill=BLACK,width=3); d.line([S2(468),S2(14),S2(456),S2(26)],fill=BLACK,width=3)
d.rectangle([S2(10),S2(55),S2(470),S2(315)],outline=ORANGE,width=2)
d.rectangle([S2(11),S2(56),S2(469),S2(314)],outline=DGREY,width=1)

call="SQ9ABC"; cw=36
callX=10+(460-len(call)*cw)//2; callY=75
boxW=len(call)*cw+20; boxH=cw+10; boxX=10+(460-boxW)//2
d.rectangle([S2(boxX),S2(callY-5),S2(boxX+boxW),S2(callY-5+boxH)],outline=WHITE,width=2)
d.rectangle([S2(boxX+1),S2(callY-4),S2(boxX+boxW-1),S2(callY-4+boxH-1)],outline=ORANGE,width=1)
txt(callX,callY,call,WHITE,48,True)
txt(150,callY+boxH+10,"145.500 MHz",LGREY,24,True)

ccx,ccy,cr=90,185,40
d.ellipse([S2(ccx-cr),S2(ccy-cr),S2(ccx+cr),S2(ccy+cr)],outline=DGREY,width=2)
for ang,l in [(0,"N"),(90,"E"),(180,"S"),(270,"W")]:
    x=ccx+int(cr*0.78*math.sin(math.radians(ang))); y=ccy-int(cr*0.78*math.cos(math.radians(ang)))
    d.text((S2(x),S2(y)),l,font=font(12),fill=LGREY,anchor="mm")
for a in range(0,360,30):
    d.line([S2(ccx+int(cr*0.9*math.sin(math.radians(a)))),S2(ccy-int(cr*0.9*math.cos(math.radians(a)))),
            S2(ccx+int(cr*math.sin(math.radians(a)))),S2(ccy-int(cr*math.cos(math.radians(a))))],fill=DGREY,width=1)
d.line([S2(ccx),S2(ccy),S2(ccx+int(cr*0.9*math.sin(math.radians(45)))),S2(ccy-int(cr*0.9*math.cos(math.radians(45))))],fill=RED,width=4)
txt(ccx-40,ccy+cr+15,"12.4 km",ORANGE,24,True)

icx,icy=340,175
house=sprite('-',0,48)
scr.paste(house,(S2(icx-24),S2(icy-24)),house)
txt(icx-90,icy+50,"Home station",LGREY,24,True)
scr.save("/workspace/share/aprs-classic-48.png")
print("zapisano aprs-classic-48.png", scr.size)

# ---------- 2) PASEK POROWNANIA SKAL 64/48/32/24 ----------
codes=[('-',"DOM"),('>',"AUTO"),('b',"ROWER"),('a',"AMBUL"),('#',"DIGI"),('_',"WX"),
       ('s',"STATEK"),('h',"SZPITAL"),('k',"CIEZ"),('[',"PIESZY"),('r',"REP"),('y',"YAGI")]
scales=[64,48,32,24]
colw=200; rowh=110
W=40+colw*len(scales); H=70+rowh*len(codes)+50
g=Image.new("RGB",(W,H),BLACK); gd=ImageDraw.Draw(g)
gd.rectangle([0,0,W,56],fill=ORANGE)
gd.text((20,10),"APRS - ikony aprs.fi: porownanie skal (czytelnosc na ILI9488)",font=font(17,True),fill=BLACK)
gd.text((20,34),"te same ikony 64 / 48 / 32 / 24 px - im mniejsze, tym mniej detali widac",font=font(12),fill=BLACK)
for ci,sc in enumerate(scales):
    x0=40+ci*colw
    gd.text((x0+60,64),"%dx%d px"%((sc,sc)),font=font(13,True),fill=ORANGE,anchor="ma")
for ri,(code,name) in enumerate(codes):
    y0=95+ri*rowh
    gd.text((10,y0+10),name,font=font(11),fill=LGREY)
    for ci,sc in enumerate(scales):
        x0=40+ci*colw
        gd.rectangle([x0,y0+4,x0+sc+16,y0+4+sc+16],outline=DGREY,width=1)
        ic=sprite(code,0,sc)
        g.paste(ic,(x0+8,y0+12),ic)
g.save("/workspace/share/aprs-scale-compare.png")
print("zapisano aprs-scale-compare.png", g.size)
