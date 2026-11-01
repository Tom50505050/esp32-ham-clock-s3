#!/usr/bin/env python3
# Podglad ekranu nowoczesnego APRS z PRAWDZIWYMI ikonami aprs.fi (hessu/aprs-symbols).
# TYLKO MOCKUP - bez zmian w kodzie firmware.
from PIL import Image, ImageDraw, ImageFont

SPRITE0 = "/workspace/project/.mock/aprs-symbols-64-0.png"
SPRITE1 = "/workspace/project/.mock/aprs-symbols-64-1.png"
T0 = Image.open(SPRITE0).convert("RGBA")
T1 = Image.open(SPRITE1).convert("RGBA")

def rgb565(v):
    r=(v>>11)&0x1F; g=(v>>5)&0x3F; b=v&0x1F
    return (r*255//31, g*255//63, b*255//31)

BG=rgb565(0x0000); CARD=rgb565(0x0884); CARD2=rgb565(0x10E6)
ACCENT=rgb565(0x3697); BORDER=rgb565(0x298A); TEXT=rgb565(0xEF7E); FAINT=rgb565(0x6BF2)
WARN=rgb565(0xFE4A); GREEN=rgb565(0x07E0)

def font(sz,bold=False):
    for p in ["/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf"%("-Bold" if bold else ""),
              "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"]:
        try: return ImageFont.truetype(p,sz)
        except Exception: pass
    return ImageFont.load_default()

def icon(code, table=0, size=64):
    """Wytnij symbol APRS ze sprite'u. code = znak (np. '-','>','_')."""
    i=ord(code)-33
    sheet = T0 if table==0 else T1
    c,r = i%16, i//16
    img = sheet.crop((c*64,r*64,c*64+64,r*64+64))
    if size!=64: img=img.resize((size,size), Image.LANCZOS)
    return img

def panel(d, x,y,w,h, fill=CARD, border=BORDER, rad=7):
    d.rounded_rectangle([x,y,x+w-1,y+h-1], rad, fill=fill, outline=border)

def label(d, x,y, txt, col=FAINT):
    d.text((x,y), txt, font=font(12), fill=col)

def text(d, x,y, txt, col=TEXT, sz=16, bold=False):
    d.text((x,y), txt, font=font(sz,bold), fill=col)

# ---------- 1) PELNY EKRAN NOWOCZESNY (480x320) w skali 2x ----------
S=2
scr = Image.new("RGB",(480*S,320*S),BG)
d = ImageDraw.Draw(scr)

def S2(v): return v*S
def rr(x,y,w,h,fill,border,rad=7): d.rounded_rectangle([S2(x),S2(y),S2(x+w-1),S2(y+h-1)],S2(rad),fill=fill,outline=border)
def rect(x,y,w,h,fill): d.rectangle([S2(x),S2(y),S2(x+w-1),S2(y+h-1)],fill=fill)

rect(0,0,480,2,ACCENT)                       # gorny pasek akcentu
rr(10,8,460,34,CARD,BORDER)                  # naglowek
rr(12,10,5,30,ACCENT,ACCENT,2)               # pionowy akcent
text(d, S2(26),S2(16), "APRS ALERT", TEXT, 22, True)
text(d, S2(330),S2(20), "AKT 14:32Z", FAINT, 12)
rr(430,14,32,22, (0xF8,0x80,0x00),(0,0,0),5) # przycisk zamkniecia
d.line([S2(437),S2(21),S2(455),S2(29)],fill=(0,0,0),width=2); d.line([S2(455),S2(21),S2(437),S2(29)],fill=(0,0,0),width=2)

rr(10,50,460,66,CARD,BORDER)                 # znak wywolawczy
text(d, S2(150),S2(58), "SQ9ABC-9", TEXT, 44, True)

rr(10,122,224,32,CARD2,BORDER); label(d,S2(20),S2(125),"FREQ",FAINT)
text(d, S2(20),S2(134), "145.500 MHz", ACCENT, 18)
rr(246,122,224,32,CARD2,BORDER); label(d,S2(256),S2(125),"ODLEGLOSC",FAINT)
text(d, S2(256),S2(134), "12.4 km", TEXT, 18)

rr(10,160,224,112,CARD,BORDER); label(d,S2(22),S2(168),"STACJA",FAINT)
# ikona domu z aprs.fi, wysrodkowana w karcie (jak drawAprsAlertHouseBitmap(90,184,64))
house = icon('-',0,64)
scr.paste(house, (S2(90),S2(184)), house)
text(d, S2(60),S2(250), "HOME STATION", TEXT, 22, True)

rr(246,160,224,112,CARD,BORDER); label(d,S2(258),S2(168),"NAMIAR",FAINT)
# kompas
ccx,ccy,cr = S2(358),S2(222),S2(36)
d.ellipse([ccx-cr,ccy-cr,ccx+cr,ccy+cr],outline=rgb565(0x3697),width=2)
for ang,lbl in [(0,"N"),(90,"E"),(180,"S"),(270,"W")]:
    import math
    x=ccx+int(cr*0.8*math.sin(math.radians(ang))); y=ccy-int(cr*0.8*math.cos(math.radians(ang)))
    d.text((x,y),lbl,font=font(11),fill=FAINT,anchor="mm")
import math
ang=45
x=ccx+int(cr*0.9*math.sin(math.radians(ang))); y=ccy-int(cr*0.9*math.cos(math.radians(ang)))
d.line([ccx,ccy,x,y],fill=rgb565(0x3697),width=4)

rr(10,278,460,32,CARD,BORDER)                # pasek dolny
d.ellipse([S2(19),S2(289),S2(29),S2(299)],fill=GREEN)
text(d, S2(38),S2(282), "SYMBOL: /-   |   POZYCJA: OK", TEXT, 12)
text(d, S2(38),S2(294), "AKTUALIZACJA: 14:32:07Z", FAINT, 12)

scr.save("/workspace/share/aprs-modern-aprsfi.png")
print("zapisano aprs-modern-aprsfi.png", scr.size)

# ---------- 2) SIATKA ikon aprs.fi w kartach STACJA ----------
ICONS = [
    ('-',0,"DOM (QTH)"),('>',0,"AUTO"),('#',0,"DIGIPEATER"),('_',0,"STACJA WX"),
    ('[',0,"PIESZY"),('a',0,"AMBULANS"),('b',0,"ROWER"),('k',0,"CIEZAROWKA"),
    ('h',0,"SZPITAL"),('s',0,"STATEK"),('r',0,"REPEATER"),('y',0,"YAGI"),
    ('g',0,"SZYBOWIEC"),('U',0,"AUTOBUS"),('<',0,"MOTOCYKL"),('X',0,"HELIKOPTER"),
]
cols,rows=4,4; cw,ch=190,200
W,H=cols*cw+40, rows*ch+90
g=Image.new("RGB",(W,H),BG); gd=ImageDraw.Draw(g)
gd.rectangle([0,0,W,54],fill=CARD); gd.rectangle([0,0,W,3],fill=ACCENT)
gd.text((20,10),"APRS - ikony z aprs.fi (hessu/aprs-symbols) na ekranie nowoczesnym",font=font(17,True),fill=TEXT)
gd.text((20,32),"prawdziwe ikony APRS, styl karty STACJA",font=font(12),fill=FAINT)
for i,(code,tbl,name) in enumerate(ICONS):
    r,c=divmod(i,cols)
    x0=20+c*cw; y0=70+r*ch
    gd.rounded_rectangle([x0+6,y0+6,x0+cw-6,y0+ch-6],10,fill=CARD,outline=BORDER)
    gd.rounded_rectangle([x0+6,y0+6,x0+11,y0+ch-6],3,fill=ACCENT)
    gd.text((x0+20,y0+14),"STACJA",font=font(11),fill=FAINT)
    ic=icon(code,tbl,64)
    g.paste(ic,(x0+cw//2-32,y0+40),ic)
    gd.text((x0+cw//2,y0+ch-46),name,font=font(13,True),fill=TEXT,anchor="mm")
    gd.text((x0+cw//2,y0+ch-26),"symbol /%s"%code,font=font(12),fill=FAINT,anchor="mm")
g.save("/workspace/share/aprs-symbols-aprsfi.png")
print("zapisano aprs-symbols-aprsfi.png", g.size)
