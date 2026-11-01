#!/usr/bin/env python3
# Porownanie: NOWOCZESNY (bez zmian - ikony z firmware) vs KLASYCZNY (ikony aprs.fi).
# TYLKO MOCKUP - bez zmian w kodzie firmware.
import math
from PIL import Image, ImageDraw, ImageFont

T0 = Image.open("/workspace/project/.mock/aprs-symbols-64-0.png").convert("RGBA")
T1 = Image.open("/workspace/project/.mock/aprs-symbols-64-1.png").convert("RGBA")
FW_HOUSE = Image.open("/workspace/project/.mock/fw_house.png").convert("RGBA")

def rgb565(v):
    r=(v>>11)&0x1F; g=(v>>5)&0x3F; b=v&0x1F
    return (r*255//31, g*255//63, b*255//31)

BLACK=(0,0,0); ORANGE=rgb565(0xFD20); GREY=rgb565(0x8410)
LGREY=(190,195,200); DGREY=(70,78,90); WHITE=(255,255,255); YEL=(255,214,0)
RED=(214,44,44); BLUE=(44,96,200); LBLUE=(96,176,240)
BG=rgb565(0x0000); CARD=rgb565(0x0884); CARD2=rgb565(0x10E6)
ACCENT=rgb565(0x3697); BORDER=rgb565(0x298A); TEXT=rgb565(0xEF7E); FAINT=rgb565(0x6BF2)
GREEN=rgb565(0x07E0)

def font(sz,bold=False):
    for p in ["/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf"%("-Bold" if bold else ""),
              "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"]:
        try: return ImageFont.truetype(p,sz)
        except Exception: pass
    return ImageFont.load_default()

def sprite(code, table=0, size=64):
    i=ord(code)-33; sheet=T0 if table==0 else T1
    c,r=i%16,i//16
    im=sheet.crop((c*64,r*64,c*64+64,r*64+64))
    return im.resize((size,size), Image.LANCZOS) if size!=64 else im

# ---- firmware-owe ikony wektorowe (1:1 z kodu) ----
def fw_car(d,cx,topY):
    bodyW,bodyH,roofW,roofH=60,14,33,9
    bodyX=cx-bodyW//2; roofX=cx-roofW//2; roofY=topY; bodyY=topY+9
    d.rounded_rectangle([bodyX,bodyY,bodyX+bodyW,bodyY+bodyH],2,fill=RED)
    d.rounded_rectangle([roofX,roofY,roofX+roofW,roofY+roofH],2,fill=RED)
    for wx in (bodyX+13, bodyX+bodyW-13):
        d.ellipse([wx-6,bodyY+bodyH-6,wx+6,bodyY+bodyH+6],fill=DGREY)
        d.ellipse([wx-3,bodyY+bodyH-3,wx+3,bodyY+bodyH+3],fill=LGREY)
    d.ellipse([bodyX-1,bodyY+1,bodyX+5,bodyY+7],fill=YEL)
    d.ellipse([bodyX-1,bodyY+6,bodyX+5,bodyY+12],fill=YEL)

def fw_human(d,cx,topY):
    hx,hy=cx,topY+5
    d.ellipse([hx-4,hy-4,hx+4,hy+4],fill=BLUE)
    for k in range(4): d.line([(hx+k,hy+4),(hx+2+k,hy+18)],fill=RED,width=1)
    for k in range(3):
        d.line([(hx+1+k,hy+8),(hx-6+k,hy+12)],fill=RED,width=1)
        d.line([(hx+3+k,hy+8),(hx+10+k,hy+11)],fill=RED,width=1)
    for k in range(3):
        d.line([(hx+3+k,hy+18),(hx-4+k,hy+28)],fill=RED,width=1)
        d.line([(hx+3+k,hy+18),(hx+12+k,hy+27)],fill=RED,width=1)

def fw_house_vec(d,centerX,topY):
    bodyW,bodyH=39,24; bodyX=centerX-bodyW//2; bodyY=topY+10
    d.rectangle([bodyX,bodyY,bodyX+bodyW,bodyY+bodyH],fill=YEL)
    d.polygon([(centerX,topY),(bodyX-3,bodyY+1),(bodyX+bodyW+3,bodyY+1)],fill=GREEN)
    d.rectangle([centerX-4,bodyY+12,centerX+4,bodyY+24],fill=DGREY)
    d.rectangle([bodyX+4,bodyY+6,bodyX+10,bodyY+12],fill=LGREY)
    d.rectangle([bodyX+bodyW-10,bodyY+6,bodyX+bodyW-4,bodyY+12],fill=LGREY)

S=2
def draw_modern():
    scr=Image.new("RGB",(480*S,320*S),BG); d=ImageDraw.Draw(scr)
    def S2(v): return v*S
    def txt(x,y,t,col,sz,bold=False): d.text((S2(x),S2(y)),t,font=font(sz,bold),fill=col)
    d.rectangle([0,0,S2(480)-1,S2(2)-1],fill=ACCENT)
    d.rounded_rectangle([S2(10),S2(8),S2(470),S2(42)],S2(7),fill=CARD,outline=BORDER)
    d.rounded_rectangle([S2(12),S2(10),S2(17),S2(40)],S2(2),fill=ACCENT)
    txt(26,16,"APRS ALERT",TEXT,22,True); txt(330,20,"AKT 14:32Z",FAINT,12)
    d.rounded_rectangle([S2(430),S2(14),S2(462),S2(36)],S2(5),fill=ORANGE,outline=BLACK)
    d.line([S2(437),S2(21),S2(455),S2(29)],fill=BLACK,width=2); d.line([S2(455),S2(21),S2(437),S2(29)],fill=BLACK,width=2)
    d.rounded_rectangle([S2(10),S2(50),S2(470),S2(116)],S2(7),fill=CARD,outline=BORDER)
    txt(150,58,"SQ9ABC-9",TEXT,44,True)
    d.rounded_rectangle([S2(10),S2(122),S2(234),S2(154)],S2(7),fill=CARD2,outline=BORDER)
    txt(20,125,"FREQ",FAINT,12); txt(20,134,"145.500 MHz",ACCENT,18)
    d.rounded_rectangle([S2(246),S2(122),S2(470),S2(154)],S2(7),fill=CARD2,outline=BORDER)
    txt(256,125,"ODLEGLOSC",FAINT,12); txt(256,134,"12.4 km",TEXT,18)
    # STACJA - bez zmian: wbudowana bitmapa domu z firmware
    d.rounded_rectangle([S2(10),S2(160),S2(234),S2(272)],S2(7),fill=CARD,outline=BORDER)
    txt(22,168,"STACJA",FAINT,12)
    scr.paste(FW_HOUSE,(S2(90),S2(184)),FW_HOUSE)
    txt(52,250,"HOME STATION",TEXT,22,True)
    # NAMIAR
    d.rounded_rectangle([S2(246),S2(160),S2(470),S2(272)],S2(7),fill=CARD,outline=BORDER)
    txt(258,168,"NAMIAR",FAINT,12)
    ccx,ccy,cr=S2(358),S2(222),S2(36)
    d.ellipse([ccx-cr,ccy-cr,ccx+cr,ccy+cr],outline=ACCENT,width=2)
    for ang,l in [(0,"N"),(90,"E"),(180,"S"),(270,"W")]:
        x=ccx+int(cr*0.8*math.sin(math.radians(ang))); y=ccy-int(cr*0.8*math.cos(math.radians(ang)))
        d.text((x,y),l,font=font(11),fill=FAINT,anchor="mm")
    d.line([ccx,ccy,ccx+int(cr*0.9*math.sin(math.radians(45))),ccy-int(cr*0.9*math.cos(math.radians(45)))],fill=ACCENT,width=4)
    d.rounded_rectangle([S2(10),S2(278),S2(470),S2(310)],S2(7),fill=CARD,outline=BORDER)
    d.ellipse([S2(19),S2(289),S2(29),S2(299)],fill=GREEN)
    txt(38,282,"SYMBOL: /-   |   POZYCJA: OK",TEXT,12); txt(38,294,"AKTUALIZACJA: 14:32:07Z",FAINT,12)
    return scr

def draw_classic():
    scr=Image.new("RGB",(480*S,320*S),BLACK); d=ImageDraw.Draw(scr)
    def S2(v): return v*S
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
    # IKONA STACJI - prawdziwa ikona aprs.fi (symbol domu /-)
    icx,icy=340,175
    house=sprite('-',0,64)
    scr.paste(house,(S2(icx-32),S2(icy-32)),house)
    txt(icx-90,icy+55,"Home station",LGREY,24,True)
    return scr

modern=draw_modern(); classic=draw_classic()
GAP=40; HEAD=60
W=480*S*2+GAP*3; H=320*S+HEAD+GAP
out=Image.new("RGB",(W,H),rgb565(0x0000))
od=ImageDraw.Draw(out)
od.rectangle([0,0,W,HEAD],fill=rgb565(0x0884))
od.text((20,8),"APRS - ikony aprs.fi TYLKO w stylu klasycznym (nowoczesny bez zmian)",font=font(20,True),fill=TEXT)
od.text((20,34),"po lewej: nowoczesny (wbudowana bitmapa domu z firmware)  |  po prawej: klasyczny (ikony aprs.fi)",font=font(12),fill=FAINT)
out.paste(modern,(GAP,HEAD)); out.paste(classic,(GAP*2+480*S,HEAD))
od.text((GAP+480*S//2,HEAD+320*S+8),"NOWOCZESNY - bez zmian ikon",font=font(14,True),fill=FAINT,anchor="ma")
od.text((GAP*2+480*S+480*S//2,HEAD+320*S+8),"KLASYCZNY - ikony aprs.fi",font=font(14,True),fill=ORANGE,anchor="ma")
out.save("/workspace/share/aprs-compare-styles.png")
print("zapisano aprs-compare-styles.png",out.size)
