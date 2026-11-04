#!/usr/bin/env python3
"""Makieta v3 ekranu RADIOSONDY (480x320) w stylu kl.field 'klasyczny' i 'nowoczesny'.

Układ:
  - pelna mapa Polski (bez przycinania, contain) pod naglowkiem
  - naglowek: [menu] ESP32-HAM-CLOCK [centrum] RADIOSONDY + [sondehub.org] przycisk
  - lewy panel: QTH, zasieg/km, tryb (LIVE / AMATORSKIE / LOWCY)
  - prawy panel: najblizsza sonda + "min od ramki" + odleglosc od odbiornika
  - dolny pasek: tabela sond (serial, typ, dystans, azymut, MHz, SNR, LIVE, alt + strzalka)
  - znaczniki: balon (sonda, 3D z Fluent Emoji) / auto (lowca, 3D)

Warianty wizualne:
  klasyczny : pomaranczowy naglowek TFT_RADIO_ORANGE, czarny tekst, biale karty
  nowoczesny: ciemna karta (0x0884-ish), teal akcent, bialy tekst

Dane: SondeHub live (api.v2.sondehub.org). Bez sieci -> dane zastępcze.
Wyjscia w /tmp:
  radiosonda_klasyczny_main.png   /tmp/radiosonda_klasyczny_main.png
  radiosonda_nowoczesny_main.png
  radiosonda_klasyczny_menu.png   (menu ustawien)
  radiosonda_nowoczesny_menu.png
  radiosonda_klasyczny_detail.png (ekran detali po przycisku sondehub.org)
  radiosonda_nowoczesny_detail.png
"""
import math
import os
import json
import urllib.request
from PIL import Image, ImageDraw, ImageFont, ImageFilter

W, H = 480, 320
ROOT = os.path.dirname(os.path.abspath(__file__))
MOCK = os.path.join(ROOT, '..', '.mock')
DATA_SRC = os.path.join(ROOT, '..', 'data', 'icon50', 'PL_drogowa_750tys_95x90cm_01_10_2019.jpg')
MAP_SRC = "/tmp/PL_map_raw.jpg"
MAP_URL = ("https://raw.githubusercontent.com/Tom50505050/esp32-ham-clock-s3/"
           "main/data/icon50/PL_drogowa_750tys_95x90cm_01_10_2019.jpg")

# georeferencja (granice Polski, bez obcina)
LAT_TOP, LAT_BOT = 54.84, 49.0
LON_LEFT, LON_RIGHT = 14.12, 24.15
B_X0, B_Y0, B_X1, B_Y1 = 21, 27, 1527, 1449
CBW, CBH = B_X1 - B_X0, B_Y1 - B_Y0
MAP_W, MAP_H = 1536, 1455

C_ORANGE = (255, 138, 0)      # TFT_RADIO_ORANGE 0xFD20
C_DARK   = (8, 16, 33)        # tlo nowoczesnej karty (0x0884)
C_TEAL   = (53, 208, 184)
C_TEAL_D = (46, 168, 150)
C_FAINT  = (108, 125, 148)
C_WHITE  = (255, 255, 255)
C_BLACK  = (0, 0, 0)
C_GREY   = (120, 128, 140)
C_RED    = (235, 68, 68)
C_GREEN  = (74, 222, 128)
C_YELLOW = (255, 224, 0)
C_BORDER = (42, 51, 87)
C_SOFT   = (150, 160, 175)

# ============================================================ dane
def haversine_km(lat1, lon1, lat2, lon2):
    R = 6371.0
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp = math.radians(lat2 - lat1); dl = math.radians(lon2 - lon1)
    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return R * 2 * math.asin(math.sqrt(a))

def bearing(lat1, lon1, lat2, lon2):
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dl = math.radians(lon2 - lon1)
    x = math.sin(dl) * math.cos(p2)
    y = math.cos(p1) * math.sin(p2) - math.sin(p1) * math.cos(p2) * math.cos(dl)
    return (math.degrees(math.atan2(x, y)) + 360) % 360

def now_minutes_ago(iso):
    """ile minut minelo od timestampu ISO do teraz (UTC)."""
    try:
        from datetime import datetime, timezone
        dt = datetime.fromisoformat(iso.replace('Z', '+00:00'))
        now = datetime.now(timezone.utc)
        return int((now - dt).total_seconds() // 60)
    except Exception:
        return 0

def fetch_live(lat=52.0, lon=19.5, radius_km=700, last_h=2):
    try:
        url = ("https://api.v2.sondehub.org/sondes?lat=%.3f&lon=%.3f"
               "&distance=%d&duration=%dh" % (lat, lon, radius_km * 1000, last_h))
        req = urllib.request.Request(url, headers={"Accept": "application/json"})
        with urllib.request.urlopen(req, timeout=20) as r:
            data = json.loads(r.read().decode())
        out = []
        for serial, v in data.items():
            la = float(v.get("lat", 0)); lo = float(v.get("lon", 0))
            if not (LON_LEFT - 0.2 <= lo <= LON_RIGHT + 0.2 and LAT_BOT - 0.2 <= la <= LAT_TOP + 0.2):
                continue
            up = v.get("uploader_position", "")
            u_lat = u_lon = None
            try:
                if ',' in up:
                    u_lat, u_lon = map(float, up.split(','))
            except Exception:
                pass
            s = {
                "serial": serial, "type": v.get("type", "?"),
                "subtype": v.get("subtype", ""),
                "lat": la, "lon": lo,
                "alt": float(v.get("alt", 0) or 0),
                "freq": v.get("frequency"),
                "snr": v.get("snr"),
                "uploader": v.get("uploader_callsign", ""),
                "vel_v": v.get("vel_v"), "vel_h": v.get("vel_h"),
                "temp": v.get("temp"), "batt": v.get("batt"),
                "datetime": v.get("datetime", ""),
                "time_received": v.get("time_received", ""),
                "u_lat": u_lat, "u_lon": u_lon,
            }
            s["dist"] = haversine_km(lat, lon, la, lo)
            s["az"] = bearing(lat, lon, la, lo)
            s["last_min"] = now_minutes_ago(s["time_received"])
            s["dist_st"] = (haversine_km(u_lat, u_lon, la, lo) if u_lat is not None else None)
            out.append(s)
        out.sort(key=lambda s: s["dist"])
        print("[SondeHub] %d sond (%.0f km/%.1f/%.1f)" % (len(out), radius_km, lat, lon))
        return out
    except Exception as e:
        print("[SondeHub] blad:", e)
        return fallback()

def fallback():
    src = [
        ("Y0353721", 52.205, 14.043, 2974, 405.8, 6.1, "SR3DWR", 2.9, 2.6, "LIVE"),
        ("709-2-11767", 51.113, 19.804, 472, 404.1985, 7.2, "MARCIN", 2.7, -1.1, "LIVE"),
        ("X0334855", 48.855, 20.689, 958, 401.600625, 8.2, "SKYMAX4", 2.7, 5.2, "LIVE"),
        ("MEC800268", 50.95, 16.71, 35200, 402.0, 3.0, "SO50", 2.8, 4.4, "AMAT"),
        ("411-3-01389", 54.53, 17.13, 15770, 400.0, 4.0, "GDANSK", 3.0, 6.0, "AMAT"),
        ("X1452344", 49.49, 16.89, 670, 403.0, 5.0, "BRNO", 2.9, -0.8, "AMAT"),
        ("507-2-02350", 48.35, 16.47, 243, 404.0, 2.5, "W21A", 2.8, -1.2, "LOW"),
        ("W4824524", 48.23, 16.19, 500, 404.6, 3.0, "RYSZARD", 2.7, 1.4, "LOW"),
    ]
    out = []
    for sn, la, lo, alt, f, snr, up, bt, vel, kind in src:
        s = {"serial": sn, "type": "RS41" if sn.startswith(("Y","X")) else ("M10" if sn.startswith("709") else "M20"),
             "subtype": "", "lat": la, "lon": lo, "alt": alt, "freq": f, "snr": snr,
             "uploader": up, "vel_v": vel, "vel_h": 14.0, "temp": 1.0, "batt": bt,
             "datetime": "", "time_received": "", "u_lat": la - 0.2, "u_lon": lo + 0.2,
             "dist": 0, "az": 0, "last_min": 3, "dist_st": 40, "kind": kind}
        out.append(s)
    for s in out:
        s["dist"] = haversine_km(52.0, 19.5, s["lat"], s["lon"])
        s["az"] = bearing(52.0, 19.5, s["lat"], s["lon"])
    out.sort(key=lambda s: s["dist"])
    return out

# ============================================================ fonty / ikony
def F(size, bold=False):
    base = "/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf"
    return ImageFont.truetype(base % ("-Bold" if bold else ""), size)

def load_icon(name):
    p = os.path.join(MOCK, 'fluent3d', name)
    if os.path.exists(p):
        return Image.open(p).convert('RGBA')
    return None

def balloon_icon(size):
    p = "/tmp/balloon_3d.png"
    if os.path.exists(p):
        im = Image.open(p).convert('RGBA')
        return im.resize((size, size), Image.LANCZOS)
    im = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    r = size // 2
    cx, cy = size // 2, size // 2
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(235, 68, 68, 255))
    d.line([cx, cy + r - 1, cx, cy + r + size // 3], fill=(120, 50, 50, 255), width=2)
    return im

def auto_icon(size):
    im = load_icon('automobile.png')
    if im:
        return im.resize((size, size), Image.LANCZOS)
    im = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([2, size//3, size-2, size*2//3], radius=6, fill=(54, 150, 247, 255))
    d.ellipse([size//5, size*2//3-2, size*2//5, size-2], fill=(0,0,0,200))
    d.ellipse([size*3//5, size*2//3-2, size*4//5, size-2], fill=(0,0,0,200))
    return im

# ============================================================ mapa
def load_map():
    for p in (DATA_SRC, MAP_SRC):
        if os.path.exists(p):
            return Image.open(p).convert('RGB')
    with urllib.request.urlopen(MAP_URL, timeout=30) as r:
        with open(MAP_SRC, 'wb') as f:
            f.write(r.read())
    return Image.open(MAP_SRC).convert('RGB')

def project(lat, lon):
    x = B_X0 + (lon - LON_LEFT) / (LON_RIGHT - LON_LEFT) * CBW
    y = B_Y0 + (LAT_TOP - lat) / (LAT_TOP - LAT_BOT) * CBH
    return x, y

class MapView:
    """Wklada mape w prostokat (x0,y0)-(x1,y1) metodą contain (bez uciecia)."""
    def __init__(self, mp, rect):
        self.mp = mp
        self.rect = rect
        x0, y0, x1, y1 = rect
        mw, mh = x1 - x0, y1 - y0
        sw, sh = mp.size
        scale = min(mw / sw, mh / sh)
        nw, nh = int(sw * scale), int(sh * scale)
        self.nw, self.nh = nw, nh
        self.ox = x0 + (mw - nw) // 2
        self.oy = y0 + (mh - nh) // 2
        self.scale = scale
        self.map_img = mp.resize((nw, nh), Image.LANCZOS)
    def draw_onto(self, img):
        img.paste(self.map_img, (self.ox, self.oy))
    def to_screen(self, lat, lon):
        px, py = project(lat, lon)
        return self.ox + int(px * self.scale), self.oy + int(py * self.scale)
    def km_to_px(self, km):
        """ile px to 'km' w poblizu (lat 52)."""
        dpx = 1 / (km) * 0
        # 1 stopien lon ~ 68.3 km przy lat 52; 1 stopien w x = (CBW/7.48deg)*scale px
        deg_per_px_x = (LON_RIGHT - LON_LEFT) / (CBW * self.scale)
        return km * deg_per_px_x / (68.3 * 1.0) * 0 + km * (CBW * self.scale / (LON_RIGHT - LON_LEFT)) / 68.3

# ============================================================ UI atoms
def panel(d, x0, y0, x1, y1, fill, outline=None, accent=None):
    d.rounded_rectangle([x0, y0, x1, y1], radius=6, fill=fill,
                        outline=outline or (0, 0, 0, 0))
    if accent:
        d.rectangle([x0, y0, x0 + 3, y1], fill=accent)

def draw_header(img, style, title="ESP32-HAM-CLOCK", right="RADIOSONDY"):
    d = ImageDraw.Draw(img)
    classic = (style == 'classic')
    hh = 32
    bg = C_ORANGE if classic else C_DARK
    fg = C_BLACK if classic else C_WHITE
    # pasek
    d.rectangle([0, 0, W - 1, hh - 1], fill=bg)
    d.rectangle([0, hh - 2, W - 1, hh - 1], fill=C_BORDER if not classic else C_BLACK)
    # menu (hamburger)
    mx, my, mw = 8, 8, 14
    if classic:
        d.rounded_rectangle([4, 5, 24, 26], radius=4, fill=C_WHITE, outline=C_BLACK)
        ink = C_BLACK
    else:
        d.rounded_rectangle([4, 5, 24, 26], radius=4, fill=C_TEAL_D, outline=C_TEAL)
        ink = C_DARK
    for i in range(3):
        y = my + i * 5
        d.rectangle([mx, y, mx + mw, y + 2], fill=ink)
    # tytul (centrum)
    tf = F(9, True)
    tw = d.textlength(title, font=tf)
    d.text((240 - tw // 2, 9), title, font=tf, fill=fg)
    # prawa: przycisk sondehub.org (po nim ekran detali)
    btn = "sondehub.org >"
    bf = F(9, True)
    bw = d.textlength(btn, font=bf) + 14
    bx = W - bw - 10
    # prawa: "RADIOSONDY" wyrównany do prawej, tuż przed przyciskiem (bez kolizji z tytułem)
    rf = F(9, True)
    rw = d.textlength(right, font=rf)
    d.text((bx - rw - 8, 4), right, font=rf, fill=fg if not classic else C_BLACK)
    if classic:
        d.rounded_rectangle([bx, 7, W - 5, 25], radius=4, fill=C_BLACK, outline=C_WHITE)
        d.text((bx + 7, 9), btn, font=bf, fill=C_ORANGE)
    else:
        d.rounded_rectangle([bx, 7, W - 5, 25], radius=4, fill=C_TEAL, outline=(28, 40, 60))
        d.text((bx + 7, 9), btn, font=bf, fill=C_BLACK)
    return hh

def draw_mode_icons(d, mode, x0, y0, dx):
    """Legenda znacznikow u dolu: balon = sonda, auto = lowca."""
    bal = balloon_icon(16); aut = auto_icon(16)
    ic = {'LIVE': bal, 'AMAT': bal, 'LOW': aut}[mode]
    d.bitmap((x0, y0), ic)
    label = {"LIVE": "SONDY LIVE", "AMAT": "SONDY AMAT.", "LOW": "LOWCY"}[mode]
    d.text((x0 + 20, y0 + 2), label, font=F(9), fill=C_FAINT)

def marker(d, ic, x, y, size=26, halo=False, selected=False):
    if selected:
        d.ellipse([x - size//2 - 5, y - size//2 - 5, x + size//2 + 5, y + size//2 + 5],
                  outline=C_YELLOW, width=2)
    elif halo:
        d.ellipse([x - size//2 - 3, y - size//2 - 3, x + size//2 + 3, y + size//2 + 3],
                  outline=(255,255,255), width=2)
    d.bitmap((x - size//2, y - size//2), ic if isinstance(ic, Image.Image) else ic)

# ============================================================ ekrany
def render_main(style, sondes, mode='LIVE', rng=600):
    """Ekran glowny RADIOSONDY: pelna mapa Polski, panele boczne nad nia."""
    classic = (style == 'classic')
    mp = load_map()
    img = Image.new('RGB', (W, H), (255, 255, 255) if classic else C_DARK)
    hh = draw_header(img, style)
    d = ImageDraw.Draw(img)

    # ===== Mapa pelnoekranowa (pod obszarem naglowka) =====
    map_rect = (0, hh + 2, W, H)
    # tlo pod mape (marginesy) - kolor jak tlo karty
    bg_map = (235, 232, 225) if classic else (10, 18, 34)
    d.rectangle(map_rect, fill=bg_map)
    # zawęzamy mapę do wnetrza pomiędzy panelami (jeszcze większa)
    map_x0, map_x1 = 110, 370
    mv = MapView(mp, (map_x0, hh + 8, map_x1, H - 6))
    mv.draw_onto(img)
    d.rectangle(map_rect, outline=None)

    # QTH i kola zasiegu
    qlat, qlon = 52.0, 19.5
    qx, qy = mv.to_screen(qlat, qlon)
    if rng:
        r_px = mv.km_to_px(rng)
        col = C_TEAL if not classic else C_ORANGE
        d.ellipse([qx - r_px, qy - r_px, qx + r_px, qy + r_px], outline=col, width=1)
    for km in (100, 200, 400):
        if km >= rng * 0.4:
            r_px = mv.km_to_px(km)
            d.ellipse([qx - r_px, qy - r_px, qx + r_px, qy + r_px],
                      outline=(120, 135, 160) if not classic else (170, 130, 70), width=1)
    d.ellipse([qx - 4, qy - 4, qx + 4, qy + 4], fill=C_YELLOW)
    d.text((qx + 8, qy - 7), "QTH", font=F(9, True), fill=C_YELLOW)

    # ===== markery =====
    bal = balloon_icon(28)
    aut = auto_icon(28)
    markers_placed = []
    if mode == 'LOW':
        # LOWCY: samochody (lowcy) na pozycjach uploaderow; do tego maly balon
        # w miejscu samej sondy (cel).
        placed_cars = set()
        for i, s in enumerate(sondes):
            if s['dist'] > rng * 1.3:
                continue
            ulat, ulon = s.get('u_lat'), s.get('u_lon')
            if ulat is None or ulon is None:
                continue
            ux, uy = mv.to_screen(ulat, ulon)
            key = (ux // 8, uy // 8)
            if key in placed_cars:
                continue
            placed_cars.add(key)
            markers_placed.append((aut, ux, uy))
            # cel sondy malym balonem
            sx, sy = mv.to_screen(s['lat'], s['lon'])
            markers_placed.append((balloon_icon(16), sx, sy))
        # podpisz najblizszego lowce
        for i, s in enumerate(sondes):
            if s.get('u_lat') is None or s['dist'] > rng * 1.3:
                continue
            ux, uy = mv.to_screen(s['u_lat'], s['u_lon'])
            d.text((ux + 18, uy - 9), s['uploader'], font=F(10, True),
                   fill=C_BLACK if classic else C_WHITE)
            break
    else:
        # LIVE / AMAT: balony w pozycjach sond
        for i, s in enumerate(sondes):
            if s['dist'] > rng * 1.3:
                continue
            sx, sy = mv.to_screen(s['lat'], s['lon'])
            markers_placed.append((bal, sx, sy))
        if sondes:
            s0 = sondes[0]
            sx, sy = mv.to_screen(s0['lat'], s0['lon'])
            lbl = "%s %s %skm" % (s0['serial'], s0['type'], int(s0['dist']))
            d.text((sx + 18, sy - 9), lbl, font=F(10, True),
                   fill=C_BLACK if classic else C_WHITE)
    for idx, (ic, x, y) in enumerate(markers_placed):
        marker(d, ic, x, y, 28, selected=(idx == 0), halo=True)

    # ===== lewy panel (QTH / ZASIEG / TRYB) =====
    px0, py0, px1, py1 = 6, hh + 12, 104, H - 6
    pfill = (250, 248, 244, 242) if classic else (10, 18, 34, 235)
    ov = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    od = ImageDraw.Draw(ov)
    od.rounded_rectangle([px0, py0, px1, py1], radius=8, fill=pfill)
    img = Image.alpha_composite(img.convert('RGBA'), ov).convert('RGB')
    d = ImageDraw.Draw(img)
    ac = C_ORANGE if classic else C_TEAL
    bc = C_WHITE if classic else C_BORDER
    d.rounded_rectangle([px0, py0, px1, py1], radius=8, outline=ac)
    d.rectangle([px0, py0, px0 + 3, py1], fill=ac)
    tx, y = px0 + 8, py0 + 8
    col = C_BLACK if classic else C_WHITE
    d.text((tx, y), "MOJE QTH", font=F(8, True), fill=C_FAINT if not classic else (90, 90, 90)); y += 11
    d.text((tx, y), "52.00N\n19.50E", font=F(8, True), fill=col); y += 22
    y += 2
    d.text((tx, y), "ZASIEG", font=F(8, True), fill=C_FAINT if not classic else (90, 90, 90)); y += 11
    d.text((tx, y), "%d km" % rng, font=F(13, True), fill=ac); y += 20
    d.text((tx, y), "TRYB", font=F(8, True), fill=C_FAINT if not classic else (90, 90, 90)); y += 11
    modes = [('LIVE', 'NA ZYWO'), ('AMAT', 'AMATORSKIE'), ('LOW', 'LOWCY')]
    for m, mlab in modes:
        if m == mode:
            d.rounded_rectangle([tx - 3, y, px1 - 4, y + 13], radius=3, fill=ac)
            tc = C_BLACK
        else:
            tc = C_FAINT if not classic else (70, 70, 70)
        d.text((tx, y + 1), mlab, font=F(8, True), fill=tc)
        y += 15
    y += 2
    # legenda ikon
    if mode == 'LOW':
        d.text((tx, y), "LOWCY = auta", font=F(8), fill=col)
    else:
        d.text((tx, y), "SONDY = balon", font=F(8), fill=col)

    # ===== prawy panel (najblizsza sonda) =====
    rx0, ry0, rx1, ry1 = 376, hh + 12, W - 6, H - 6
    ov = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    od = ImageDraw.Draw(ov)
    od.rounded_rectangle([rx0, ry0, rx1, ry1], radius=8, fill=pfill)
    img = Image.alpha_composite(img.convert('RGBA'), ov).convert('RGB')
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([rx0, ry0, rx1, ry1], radius=8, outline=ac)
    d.rectangle([rx0, ry0, rx0 + 3, ry1], fill=ac)
    tx, y = rx0 + 8, ry0 + 8
    d.text((tx, y), "NAJBLIŻSZA SONDA", font=F(8, True), fill=C_FAINT if not classic else (90, 90, 90)); y += 13
    s0 = sondes[0] if sondes else None
    if s0:
        d.text((tx, y), s0['serial'], font=F(14, True), fill=ac); y += 17
        d.text((tx, y), "%s   %dm" % (s0['type'], int(s0['alt'])), font=F(9), fill=col); y += 14
        # strzalka wysokosc obok
        if s0.get('vel_v') is not None:
            vv = s0['vel_v']
            arr = u'\u2191' if vv >= 0 else u'\u2193'
            d.text((tx + 70, y - 13), arr, font=F(14, True),
                   fill=C_RED if vv < 0 else (C_GREEN if not classic else C_ORANGE))
        d.text((tx, y), "od QTH: %d km" % int(s0['dist']), font=F(9, True), fill=col); y += 12
        if s0.get('dist_st') is not None:
            d.text((tx, y), "od odb.: %d km" % int(s0['dist_st']), font=F(9), fill=col); y += 12
        d.text((tx, y), "AZ: %03d  %s" % (int(s0['az']), ("%.3fMHz" % s0['freq']) if s0.get('freq') else "--"),
               font=F(9), fill=col); y += 12
        y += 3
        d.text((tx, y), "OSTATNIA RAMKA", font=F(8, True), fill=C_FAINT if not classic else (90, 90, 90)); y += 11
        last = s0.get('last_min', 2)
        d.text((tx, y), "%d min temu" % last, font=F(12, True),
               fill=(C_RED if last > 10 else (C_GREEN if not classic else C_ORANGE))); y += 15

    return img


def render_menu(style):
    """Menu ustawien RADIOSONDY (okno na ekranie)."""
    classic = (style == 'classic')
    mp = load_map()
    img = Image.new('RGB', (W, H), (255, 255, 255) if classic else C_DARK)
    mv = MapView(mp, (0, 0, 480, 320))
    img.paste(mv.map_img, (mv.ox, mv.oy))
    # przyciemnienie
    ov = Image.new('RGBA', (W, H), (0, 0, 0, 140) if not classic else (0, 0, 0, 60))
    img = Image.alpha_composite(img.convert('RGBA'), ov).convert('RGB')
    d = ImageDraw.Draw(img)

    # okno menu
    wx0, wy0, wx1, wy1 = 40, 30, 440, 290
    bgc = (15, 22, 40) if not classic else (250, 248, 244)
    d.rounded_rectangle([wx0, wy0, wx1, wy1], radius=10, fill=bgc,
                        outline=C_TEAL if not classic else C_ORANGE, width=2)
    d.rounded_rectangle([wx0, wy0, wx1, wy0 + 30], radius=10, fill=C_TEAL if not classic else C_ORANGE)
    d.text((wx0 + 14, wy0 + 7), "RADIOSONDY – USTAWIENIA", font=F(12, True),
           fill=C_BLACK)
    d.text((wx1 - 38, wy0 + 5), "[X]", font=F(13, True), fill=C_BLACK)

    col = C_BLACK if classic else C_WHITE
    y = wy0 + 46
    lab = C_FAINT if not classic else (80, 80, 80)
    d.text((wx0 + 18, y), "ZASIEG WYSZUKIWANIA", font=F(9, True), fill=lab); y += 14
    # suwak zasiegu
    d.rectangle([wx0 + 18, y, wx1 - 18, y + 4], fill=C_FAINT)
    knob_x = wx0 + 18 + int((600 - 50) / (1000 - 50) * (wx1 - 36))
    d.ellipse([knob_x - 7, y - 5, knob_x + 7, y + 9], fill=C_ORANGE if classic else C_TEAL)
    d.text((wx0 + 18, y + 8), "50", font=F(9), fill=lab)
    d.text((wx1 - 30, y + 8), "1000 km", font=F(9), fill=lab)
    y += 30
    d.text((wx0 + 18, y), "600 km", font=F(13, True), fill=C_ORANGE if classic else C_TEAL); y += 24

    d.text((wx0 + 18, y), "TYP SOND / STACJI", font=F(9, True), fill=lab); y += 14
    modes = [('LIVE', 'NA ZYWO'), ('AMAT', 'AMATORSKIE'), ('LOW', 'LOWCY')]
    for i, (mcode, mlab) in enumerate(modes):
        bx = wx0 + 18 + i * 134
        if mcode == 'LIVE':
            c = C_TEAL if not classic else C_ORANGE
            d.rounded_rectangle([bx, y, bx + 124, y + 24], radius=5, fill=c)
            d.text((bx + 12, y + 6), mlab, font=F(10, True), fill=C_BLACK)
        else:
            d.rounded_rectangle([bx, y, bx + 124, y + 24], radius=5, fill=(0,0,0,0),
                                outline=C_FAINT)
            d.text((bx + 12, y + 6), mlab, font=F(10, True), fill=col)
    y += 40

    d.text((wx0 + 18, y), "POKAZUJ NA MAPIE", font=F(9, True), fill=lab); y += 14
    if style == 'classic':
        d.text((wx0 + 18, y), "BALON  = sonda        AUTO = lowca", font=F(10),
               fill=(60, 60, 60))
    else:
        bal = balloon_icon(18); aut = auto_icon(18)
        d.bitmap((wx0 + 18, y), bal)
        d.text((wx0 + 40, y + 2), "sonda", font=F(9), fill=col)
        d.bitmap((wx0 + 110, y), aut)
        d.text((wx0 + 132, y + 2), "lowca", font=F(9), fill=col)
    y += 30

    # stopka: zapisz
    d.rounded_rectangle([wx0 + 18, wy1 - 34, wx1 - 18, wy1 - 12], radius=5,
                        fill=C_ORANGE if classic else C_TEAL)
    d.text((wx0 + 18 + 130, wy1 - 30), "ZAPISZ", font=F(10, True), fill=C_BLACK)
    return img


def render_detail(style, s):
    """Ekran detali po wcisnieciu [sondehub.org>]."""
    classic = (style == 'classic')
    img = Image.new('RGB', (W, H), (250, 248, 244) if classic else C_DARK)
    d = ImageDraw.Draw(img)
    hh = draw_header(img, style)
    col = C_BLACK if classic else C_WHITE
    lab = (90, 90, 90) if classic else C_FAINT

    y = hh + 12
    # karta sondy
    d.rounded_rectangle([8, y, W - 8, y + 86], radius=8,
                        fill=(255, 255, 255) if classic else (14, 24, 46),
                        outline=C_ORANGE if classic else C_BORDER)
    d.text((22, y + 8), "SONDA: " + s['serial'], font=F(15, True),
           fill=C_ORANGE if classic else C_TEAL)
    d.text((22, y + 28), "%s  %s  alt %.0f m" % (s['type'], s.get('subtype', ''), s['alt']),
           font=F(11), fill=col)
    vv = s.get('vel_v') or 0
    arr = u'\u2191' if vv >= 0 else u'\u2193'
    if vv is None: arr = '-'
    d.text((22, y + 46), "Trend wys. %s  %.1f m/s" % (arr, abs(vv)), font=F(11, True),
           fill=(C_RED if vv < 0 else C_GREEN) if vv is not None else col)
    d.text((22, y + 64), "Odbiorca: %s" % (s.get('uploader') or '-'), font=F(10), fill=col)
    y += 96

    # grid informacji
    items = [
        ("DYSTANS od QTH", "%d km" % int(s['dist'])),
        ("AZYMUT od QTH", "%03d" % int(s['az'])),
        ("CZESTOTLIWOSC", ("%.3f MHz" % s['freq']) if s.get('freq') else "--"),
        ("SNR", ("%d dB" % s['snr']) if s.get('snr') else "--"),
        ("ODL. od odbiorcy", ("%d km" % int(s['dist_st'])) if s.get('dist_st') is not None else "--"),
        ("OST. RAMKA", "%d min" % (s.get('last_min', 0))),
        ("TEMP", ("%.1f C" % s['temp']) if s.get('temp') is not None else "--"),
        ("BATERIA", ("%.1f V" % s['batt']) if s.get('batt') is not None else "--"),
    ]
    cw, ch = (W - 32) // 2, 30
    for i, (k, v) in enumerate(items):
        cx = 16 + (i % 2) * (cw + 1)
        cy = y + (i // 2) * (ch + 6)
        d.rounded_rectangle([cx, cy, cx + cw - 2, cy + ch - 2], radius=5,
                            fill=(255, 255, 255) if classic else (14, 24, 46),
                            outline=C_ORANGE if classic else C_BORDER)
        d.text((cx + 8, cy + 4), k, font=F(8, True), fill=lab)
        d.text((cx + 8, cy + 14), v, font=F(11, True), fill=col)

    # status LIVE
    y2 = y + 4 * (ch + 6) + 6
    live = s.get('last_min', 0) <= 10
    dot = C_GREEN if live else C_RED
    d.ellipse([18, y2 + 4, 30, y2 + 16], fill=dot)
    d.text((36, y2 + 2), "NA ZYWO" if live else ("SZUKAM RAMKI… %d min" % s.get('last_min', 0)),
           font=F(12, True), fill=C_BLACK if classic else C_WHITE)
    return img


def main():
    sondes = fetch_live()
    if not sondes:
        print("BRAK sond!");
        return

    # EKRANY GŁÓWNE
    im_k = render_main('classic', sondes, mode='LIVE', rng=600)
    im_k.save('/tmp/radiosonda_klasyczny_main.png')
    im_n = render_main('modern', sondes, mode='LIVE', rng=600)
    im_n.save('/tmp/radiosonda_nowoczesny_main.png')

    # MENU
    render_menu('classic').save('/tmp/radiosonda_klasyczny_menu.png')
    render_menu('modern').save('/tmp/radiosonda_nowoczesny_menu.png')

    # DETAIL
    s0 = sondes[0]
    render_detail('classic', s0).save('/tmp/radiosonda_klasyczny_detail.png')
    render_detail('modern', s0).save('/tmp/radiosonda_nowoczesny_detail.png')

    # wariant LOWCY (auta na mapie) w obu stylach
    render_main('classic', sondes, mode='LOW', rng=400).save('/tmp/radiosonda_klasyczny_lowcy.png')
    render_main('modern', sondes, mode='LOW', rng=400).save('/tmp/radiosonda_nowoczesny_lowcy.png')

    # wariant AMATORSKIE
    render_main('classic', sondes, mode='AMAT', rng=400).save('/tmp/radiosonda_klasyczny_amatorskie.png')
    render_main('modern', sondes, mode='AMAT', rng=400).save('/tmp/radiosonda_nowoczesny_amatorskie.png')

    print("Wszystkie podglady zapisane w /tmp")


if __name__ == '__main__':
    main()