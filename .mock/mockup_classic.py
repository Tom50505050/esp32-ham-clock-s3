#!/usr/bin/env python3
# Podglad ekranu KLASYCZNEGO APRS z ikonami aprs.fi (hessu/aprs-symbols).
# TYLKO MOCKUP - bez zmian w kodzie firmware.
import math
from PIL import Image, ImageDraw, ImageFont

T0 = Image.open("/workspace/project/.mock/aprs-symbols-64-0.png").convert("RGBA")
T1 = Image.open("/workspace/project/.mock/aprs-symbols-64-1.png").convert("RGBA")

def rgb565(v):
    r=(v>>11)&0x1F; g=(v>>5)&0x3F; b=v&0x1F
    return (r*255//31, g*255//63, b*255//31)

BLACK=(0,0,0); ORANGE=rgb565(0xFD20); GREY=rgb565(0x8410)
LGREY=(190,195,200); DGREY=(70,78,90); WHITE=(255,255,255); YEL=(255,214,0)
RED=(214,44,44); BLUE=(44,96,200)

def font(sz,bold=False):
    for p in ["/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf"%("-Bold" if bold else ""),
              "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"]:
        try: return ImageFont.truetype(p,sz)
        except Exception: pass
    return ImageFont.load_default()

def icon(code, table=0, size=64):
    i=ord(code)-33
    sheet=T0 if table==0 else T1
    c,r=i%16,i//16
    img=sheet.crop((c*64,r*64,c*64+64,r*64+64))
    return img.resize((size,size), Image.LANCZOS) if size!=64 else img

# ---------- 1) PELNY EKRAN KLASYCZNY (480x320) w skali 2x ----------
S=2
scr=Image.new("RGB",(480*S,320*S),BLACK); d=ImageDraw.Draw(scr)
def S2(v): return v*S
def txt(x,y,t,col,sz,bold=False,anchor=None): d.text((S2(x),S2(y)),t,font=font(sz,bold),fill=col,anchor=anchor)

# gorny pasek (pomaranczowy) + linia
d.rectangle([0,0,S2(480)-1,S2(45)-1],fill=ORANGE)
d.line([0,S2(44),S2(480),S2(44)],fill=GREY,width=1)
txt(150,8,"APRS ALERT",BLACK,36,True)
# przycisk zamkniecia (jak drawAprsAlertCloseButton)
d.rounded_rectangle([S2(449),S2(7),S2(449+26),S2(7+26)],S2(5),fill=ORANGE,outline=BLACK,width=2)
d.line([S2(456),S2(14),S2(468),S2(26)],fill=BLACK,width=3); d.line([S2(468),S2(14),S2(456),S2(26)],fill=BLACK,width=3)

# glowna ramka
d.rectangle([S2(10),S2(55),S2(10+460),S2(55+260)],outline=ORANGE,width=2)
d.rectangle([S2(11),S2(56),S2(11+458),S2(56+258)],outline=DGREY,width=1)

# znak wywolawczy (size 6, <=6 znakow) + ramka
call="SQ9ABC"; cw=36; csz=6
callX=10+(460-len(call)*cw)//2; callY=75
boxW=len(call)*cw+20; boxH=cw+10; boxX=10+(460-boxW)//2
d.rectangle([S2(boxX),S2(callY-5),S2(boxX+boxW),S2(callY-5+boxH)],outline=WHITE,width=2)
d.rectangle([S2(boxX+1),S2(callY-4),S2(boxX+boxW-1),S2(callY-4+boxH-1)],outline=ORANGE,width=1)
txt(callX, callY, call, WHITE, 48, True)

# czestotliwosc
txt(150, callY+boxH+10, "145.500 MHz", LGREY, 24, True)

# kompas (lewo)
ccx,ccy,cr=90,185,40
d.ellipse([S2(ccx-cr),S2(ccy-cr),S2(ccx+cr),S2(ccy+cr)],outline=DGREY,width=2)
for ang,lbl in [(0,"N"),(90,"E"),(180,"S"),(270,"W")]:
    x=ccx+int(cr*0.78*math.sin(math.radians(ang))); y=ccy-int(cr*0.78*math.cos(math.radians(ang)))
    d.text((S2(x),S2(y)),lbl,font=font(12),fill=LGREY,anchor="mm")
for a in range(0,360,30):
    x1=ccx+int(cr*0.9*math.sin(math.radians(a))); y1=ccy-int(cr*0.9*math.cos(math.radians(a)))
    x2=ccx+int(cr*math.sin(math.radians(a))); y2=ccy-int(cr*math.cos(math.radians(a)))
    d.line([S2(x1),S2(y1),S2(x2),S2(y2)],fill=DGREY,width=1)
ang=45
d.line([S2(ccx),S2(ccy),S2(ccx+int(cr*0.9*math.sin(math.radians(ang)))),S2(ccy-int(cr*0.9*math.cos(math.radians(ang))))],fill=RED,width=4)
txt(ccx-40, ccy+cr+15, "12.4 km", ORANGE, 24, True)

# ikona stacji (prawo) - PRAWDZIWA ikona domu z aprs.fi
icx,icy=340,175
house=icon('-',0,64)
scr.paste(house,(S2(icx-32),S2(icy-32)),house)
txt(icx-90, icy+55, "Home station", LGREY, 24, True)

scr.save("/workspace/share/aprs-classic-aprsfi.png")
print("zapisano aprs-classic-aprsfi.png", scr.size)

# ---------- 2) SIATKA ikon aprs.fi w stylu klasycznym ----------
ICONS=[('-',0,"DOM (QTH)"),('>',0,"AUTO"),('#',0,"DIGIPEATER"),('_',0,"STACJA WX"),
       ('[',0,"PIESZY"),('a',0,"AMBULANS"),('b',0,"ROWER"),('k',0,"CIEZAROWKA"),
       ('h',0,"SZPITAL"),('s',0,"STATEK"),('r',0,"REPEATER"),('y',0,"YAGI"),
       ('g',0,"SZYBOWIEC"),('U',0,"AUTOBUS"),('<',0,"MOTOCYKL"),('X',0,"HELIKOPTER")]
cols,rows=4,4; cw2,ch2=190,200
W,H=cols*cw2+40, rows*ch2+90
g=Image.new("RGB",(W,H),BLACK); gd=ImageDraw.Draw(g)
gd.rectangle([0,0,W,54],fill=ORANGE)
gd.text((20,10),"APRS - ikony aprs.fi w STYLU KLASYCZNYM",font=font(17,True),fill=BLACK)
gd.text((20,32),"czarne tlo, pomaranczowa ramka, biala etykieta",font=font(12),fill=BLACK)
for i,(code,tbl,name) in enumerate(ICONS):
    r,c=divmod(i,cols)
    x0=20+c*cw2; y0=70+r*ch2
    gd.rectangle([x0+6,y0+6,x0+cw2-6,y0+ch2-6],fill=BLACK,outline=ORANGE,width=2)
    gd.rectangle([x0+7,y0+7,x0+cw2-7,y0+ch2-7],outline=DGREY,width=1)
    ic=icon(code,tbl,64)
    g.paste(ic,(x0+cw2//2-32,y0+36),ic)
    gd.text((x0+cw2//2,y0+ch2-46),name,font=font(13,True),fill=WHITE,anchor="mm")
    gd.text((x0+cw2//2,y0+ch2-26),"symbol /%s"%code,font=font(12),fill=LGREY,anchor="mm")
g.save("/workspace/share/aprs-symbols-classic.png")
print("zapisano aprs-symbols-classic.png", g.size)
