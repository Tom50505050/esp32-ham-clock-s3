#!/usr/bin/env python3
# Arkusz: ikony NA CZARNYM TLE (bez szachownicy) - jak wygladaja na ekranie klasycznym.
import glob, os
from PIL import Image, ImageDraw, ImageFont

def rgb565(v):
    r=(v>>11)&0x1F; g=(v>>5)&0x3F; b=v&0x1F
    return (r*255//31, g*255//63, b*255//31)

BLACK=(0,0,0); ORANGE=rgb565(0xFD20); WHITE=(255,255,255); LGREY=(190,195,200)

def font(sz,bold=False):
    for p in ["/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf"%("-Bold" if bold else ""),
              "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"]:
        try: return ImageFont.truetype(p,sz)
        except Exception: pass
    return ImageFont.load_default()

files=sorted(glob.glob("/workspace/project/data/aprs_icons/black/*.png"))
cols=6; rows=(len(files)+cols-1)//cols
cw,ch=150,150
W=40+cols*cw; H=104+rows*ch
g=Image.new("RGB",(W,H),BLACK); d=ImageDraw.Draw(g)
d.rectangle([0,0,W,62],fill=ORANGE)
d.text((20,8),"APRS - ikony 48x48 NA CZARNYM TLE (styl klasyczny)",font=font(18,True),fill=BLACK)
d.text((20,32),"tlo czarne - bez szachownicy, dokladnie jak na ekranie ESP",font=font(12),fill=BLACK)
d.text((20,46),"data/aprs_icons/black/  (PNG + BMP 24-bit)",font=font(12),fill=BLACK)

for ri,f in enumerate(files):
    r,c=divmod(ri,cols)
    x0=20+c*cw; y0=76+r*ch
    ic=Image.open(f).convert("RGB")
    g.paste(ic,(x0+16,y0+6))
    name=os.path.splitext(os.path.basename(f))[0]
    d.text((x0+16,y0+ch-36),name,font=font(13,True),fill=WHITE)
    d.text((x0+16,y0+ch-20),"48x48 black",font=font(11),fill=LGREY)
g.save("/workspace/share/aprs-icons-48-black.png")
print("zapisano aprs-icons-48-black.png", g.size, "| plikow:", len(files))
