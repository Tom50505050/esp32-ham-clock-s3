#!/usr/bin/env python3
# Arkusz podgladowy: 26 ikon 48x48 wygenerowanych do data/aprs_icons/.
import glob, os
from PIL import Image, ImageDraw, ImageFont

def rgb565(v):
    r=(v>>11)&0x1F; g=(v>>5)&0x3F; b=v&0x1F
    return (r*255//31, g*255//63, b*255//31)

BLACK=(0,0,0); ORANGE=rgb565(0xFD20); DGREY=(70,78,90); WHITE=(255,255,255); LGREY=(190,195,200)

def font(sz,bold=False):
    for p in ["/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf"%("-Bold" if bold else ""),
              "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"]:
        try: return ImageFont.truetype(p,sz)
        except Exception: pass
    return ImageFont.load_default()

files=sorted(glob.glob("/workspace/project/data/aprs_icons/*.png"))
cols=6; rows=(len(files)+cols-1)//cols
cw,ch=150,150
W=40+cols*cw; H=110+rows*ch
g=Image.new("RGB",(W,H),BLACK); d=ImageDraw.Draw(g)
d.rectangle([0,0,W,66],fill=ORANGE)
d.text((20,8),"APRS - gotowe ikony 48x48 PNG (data/aprs_icons/)",font=font(18,True),fill=BLACK)
d.text((20,32),"aprs.fi / hessu (CC BY-SA 4.0) | 26 plikow, styl klasyczny",font=font(12),fill=BLACK)
d.text((20,48),"na ILI9488 48px = ~7,4 mm - dobrze czytelne",font=font(12),fill=BLACK)

# tlo do sprawdzenia przezroczystosci: szachownica
for ri,f in enumerate(files):
    r,c=divmod(ri,cols)
    x0=20+c*cw; y0=80+r*ch
    # szachownica
    for yy in range(0,ch-40,10):
        for xx in range(0,cw-20,10):
            col=(40,40,40) if ((xx//10+yy//10)%2==0) else (25,25,25)
            d.rectangle([x0+xx,y0+yy,x0+xx+10,y0+yy+10],fill=col)
    ic=Image.open(f).convert("RGBA")
    g.paste(ic,(x0+16,y0+8),ic)
    name=os.path.splitext(os.path.basename(f))[0]
    d.text((x0+16,y0+ch-34),name,font=font(13,True),fill=WHITE)
    d.text((x0+16,y0+ch-18),"48x48 PNG",font=font(11),fill=LGREY)
g.save("/workspace/share/aprs-icons-48-ready.png")
print("zapisano aprs-icons-48-ready.png", g.size, "| plikow:", len(files))
