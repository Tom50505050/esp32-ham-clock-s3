#!/usr/bin/env python3
"""Makieta ekranu RADIOSONDY dla ESP32-HAM-CLOCK (480x320).

Tlo = prawdziwa mapa Polski ("data/icon50/PL_drogowa_750tys_95x90cm_01_10_2019.jpg",
pobierana do /tmp jesli nie ma lokalnie). Georeferencja prostokata tresci:
  bbox (x21..1527, y27..1449) w oryginalnym 1536x1455
  LAT_TOP 55.2N  LAT_BOT 48.4N  LON_LEFT 13.5E  LON_RIGHT 25.2E
Markery sond = DANE NA ZYWO z https://api.v2.sondehub.org/sondes
(W razie braku sieci uzywa przykladow).

Wyjscie:
  /tmp/radiosonda_preview.png      - ekran mapy z UI (480x320)
  /tmp/radiosonda_map_only.png     - sama mapa z markerami
"""
import json
import math
import os
import urllib.request

from PIL import Image, ImageDraw, ImageFont

W, H = 480, 320
ROOT = os.path.dirname(os.path.abspath(__file__))

C_BG     = (0, 0, 0)
C_CARD   = (10, 18, 32)
C_CARD2  = (16, 24, 40)
C_BORDER = (42, 51, 87)
C_ACCENT = (54, 150, 247)
C_TEXT   = (239, 254, 255)
C_FAINT  = (108, 125, 148)
C_YELLOW = (255, 224, 0)
C_GREEN  = (74, 222, 128)
C_RED    = (235, 68, 68)
C_ORANGE = (255, 168, 30)
C_WHITE  = (255, 255, 255)

LAT_TOP, LAT_BOT = 54.84, 49.0
LON_LEFT, LON_RIGHT = 14.12, 24.15
B_X0, B_Y0, B_X1, B_Y1 = 21, 27, 1527, 1449
CBW, CBH = B_X1 - B_X0, B_Y1 - B_Y0
MAP_W, MAP_H = 1536, 1455

MAP_SRC = "/tmp/PL_map_raw.jpg"
DATA_SRC = os.path.join("data", "icon50", "PL_drogowa_750tys_95x90cm_01_10_2019.jpg")
MAP_URL = ("https://raw.githubusercontent.com/Tom50505050/esp32-ham-clock-s3/"
           "main/data/icon50/PL_drogowa_750tys_95x90cm_01_10_2019.jpg")


def load_map():
    for p in (os.path.join(ROOT, DATA_SRC), MAP_SRC):
        if os.path.exists(p):
            return Image.open(p).convert("RGB")
    print("Pobieram mape:", MAP_URL)
    with urllib.request.urlopen(MAP_URL, timeout=30) as r:
        with open(MAP_SRC, "wb") as f:
            f.write(r.read())
    return Image.open(MAP_SRC).convert("RGB")


def proj(lat, lon):
    """projekcja geograficzna na piksele ORYGINALNEGO obrazka (1536x1455)."""
    x = B_X0 + (lon - LON_LEFT) / (LON_RIGHT - LON_LEFT) * CBW
    y = B_Y0 + (LAT_TOP - lat) / (LAT_TOP - LAT_BOT) * CBH
    return x, y


def haversine_km(lat1, lon1, lat2, lon2):
    R = 6371.0
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp = math.radians(lat2 - lat1)
    dl = math.radians(lon2 - lon1)
    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return R * 2 * math.asin(math.sqrt(a))


def bearing(lat1, lon1, lat2, lon2):
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dl = math.radians(lon2 - lon1)
    x = math.sin(dl) * math.cos(p2)
    y = math.cos(p1) * math.sin(p2) - math.sin(p1) * math.cos(p2) * math.cos(dl)
    return (math.degrees(math.atan2(x, y)) + 360) % 360


def sondes_get_live(qth_lat=52.0, qth_lon=19.5, dist_km=600, last_h=2):
    try:
        url = ("https://api.v2.sondehub.org/sondes?lat=%.3f&lon=%.3f"
               "&distance=%d&duration=%dh" % (qth_lat, qth_lon, dist_km * 1000, last_h))
        req = urllib.request.Request(url, headers={"Accept": "application/json"})
        with urllib.request.urlopen(req, timeout=20) as r:
            data = json.loads(r.read().decode())
        out = []
        for serial, v in data.items():
            s = {
                "serial": serial, "type": v.get("type", "?"),
                "subtype": v.get("subtype", ""), "manufacturer": v.get("manufacturer", ""),
                "lat": float(v.get("lat", 0)), "lon": float(v.get("lon", 0)),
                "alt": float(v.get("alt", 0)), "temp": v.get("temp"),
                "humidity": v.get("humidity"), "pressure": v.get("pressure"),
                "freq": v.get("frequency"), "snr": v.get("snr"),
                "uploader": v.get("uploader_callsign", ""),
                "heading": v.get("heading"), "vel_h": v.get("vel_h"),
                "batt": v.get("batt"),
            }
            s["dist"] = haversine_km(qth_lat, qth_lon, s["lat"], s["lon"])
            s["az"] = bearing(qth_lat, qth_lon, s["lat"], s["lon"])
            out.append(s)
        out.sort(key=lambda s: s["dist"])
        print("[SondeHub] %d sond (%.0f km od QTH %.2f/%.2f)" % (len(out), dist_km, qth_lat, qth_lon))
        return out
    except Exception as e:
        print("[SondeHub] blad pobierania:", e)
        return fallback_sondes(qth_lat, qth_lon)


def fallback_sondes(qth_lat, qth_lon):
    base = [
        {"serial": "Y0353721", "type": "RS41", "subtype": "RS41-SGP", "manufacturer": "Vaisala",
         "lat": 52.205, "lon": 14.043, "alt": 2974, "temp": -13.5, "freq": 405.8, "snr": 6.1,
         "uploader": "SR3DWR", "batt": 2.9},
        {"serial": "709-2-11767", "type": "M10", "subtype": "RADIOSONDE", "manufacturer": "Meteomodem",
         "lat": 51.113, "lon": 19.804, "alt": 472, "temp": 6.2, "freq": 404.1985, "snr": 7.2,
         "uploader": "MARCIN", "batt": 2.7},
        {"serial": "X0334855", "type": "RS41", "subtype": "RS41-SGP", "manufacturer": "Vaisala",
         "lat": 48.855, "lon": 20.689, "alt": 958, "temp": 1.9, "freq": 401.600625, "snr": 8.2,
         "uploader": "SKYMAX4", "batt": 2.7},
    ]
    for s in base:
        s["dist"] = haversine_km(qth_lat, qth_lon, s["lat"], s["lon"])
        s["az"] = bearing(qth_lat, qth_lon, s["lat"], s["lon"])
    return sorted(base, key=lambda s: s["dist"])


def F(size, bold=False):
    path = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
    if bold:
        path = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
    return ImageFont.truetype(path, size)


def card(d, x, y, w, h, accent=True):
    d.rectangle([x, y, x + w - 1, y + h - 1], fill=C_CARD)
    d.rectangle([x, y, x + w - 1, y + h - 1], outline=C_BORDER)
    if accent:
        d.rectangle([x, y, x + 3, y + h - 1], fill=C_ACCENT)


def paste_into(dst, src, x0, y0, x1, y1):
    mw, mh = x1 - x0, y1 - y0
    sw, sh = src.size
    scale = max(mw / sw, mh / sh)
    nw, nh = int(sw * scale), int(sh * scale)
    m = src.resize((nw, nh), Image.LANCZOS)
    ox, oy = x0 + (mw - nw) // 2, y0 + (mh - nh) // 2
    dst.paste(m, (ox, oy))


def main():
    qth = (52.0, 19.5)
    sondes = sondes_get_live(qth[0], qth[1], dist_km=600, last_h=2)
    mp = load_map()
    print("Mapa:", mp.size, mp.mode)

    # wspolne parametry "cover" dla mapy na 480x320
    sw, sh = mp.size
    sc = max(W / sw, H / sh)
    nw, nh = int(sw * sc), int(sh * sc)
    ox, oy = (W - nw) // 2, (H - nh) // 2
    kx, ky = sc, sc          # przelicznik pikseli mapy -> ekran

    def px_latlon(lat, lon):
        px, py = proj(lat, lon)
        return ox + int(px * kx), oy + int(py * ky)

    # sondy mieszczace sie w granicach mapy (z lekka tolerancja)
    on_map = [s for s in sondes
              if LON_LEFT - 0.3 <= s["lon"] <= LON_RIGHT + 0.3
              and LAT_BOT - 0.3 <= s["lat"] <= LAT_TOP + 0.3]

    # ======= EKRAN 1: mapa + UI =======
    img = Image.new("RGB", (W, H), C_BG)
    paste_into(img, mp, 0, 0, W, H)
    d = ImageDraw.Draw(img)

    # naglowek
    d.rectangle([0, 0, W - 1, 29], fill=(0, 0, 0))
    d.rectangle([0, 29, W - 1, 29], fill=C_ACCENT)
    d.rectangle([4, 4, 63, 11], fill=None)
    d.text((4, 4), "ESP32-HAM-CLOCK", font=F(9), fill=C_FAINT)
    d.text((8, 12), "RADIOSONDY", font=F(14, True), fill=C_TEXT)
    d.text((W - 132, 9), "sondehub.org", font=F(9), fill=C_ACCENT)
    d.rectangle([W - 27, 8, W - 17, 18], fill=(90, 30, 30))
    d.rectangle([W - 25, 10, W - 19, 16], fill=C_RED)
    d.text((W - 14, 9), "LIVE", font=F(9, True), fill=C_RED)

    # QTH
    qx, qy = px_latlon(qth[0], qth[1])
    d.ellipse([qx - 4, qy - 4, qx + 4, qy + 4], fill=C_YELLOW)
    d.ellipse([qx - 8, qy - 8, qx + 8, qy + 8], outline=C_YELLOW, width=1)
    d.text((qx + 11, qy - 8), "QTH", font=F(10, True), fill=C_YELLOW)

    colors = [C_RED, C_ORANGE, C_GREEN, C_ACCENT]
    # wszystkie sondy na mapie: mala kropka
    for s in on_map[4:]:
        sx, sy = px_latlon(s["lat"], s["lon"])
        d.ellipse([sx - 2, sy - 2, sx + 2, sy + 2], fill=C_WHITE)
        d.ellipse([sx - 4, sy - 4, sx + 4, sy + 4], outline=(80, 100, 130), width=1)
    # cztery najblizsze: duze markery + etykiety
    for i, s in enumerate(on_map[:4]):
        sx, sy = px_latlon(s["lat"], s["lon"])
        col = colors[i % len(colors)]
        d.ellipse([sx - 4, sy - 4, sx + 4, sy + 4], fill=col)
        d.ellipse([sx - 9, sy - 9, sx + 9, sy + 9], outline=col, width=2)
        if i == 0:
            lbl = "%s %s %skm" % (s["serial"], s["type"], int(s["dist"]))
            d.text((sx + 12, sy - 10), lbl, font=F(11, True), fill=C_WHITE)
        else:
            d.text((sx + 10, sy - 5), s["serial"], font=F(8, True), fill=C_WHITE)

    # dolna lista
    TBX, TBY, TBW, TBH = 4, 232, 472, 84
    d.rectangle([TBX, TBY, TBX + TBW - 1, TBY + TBH - 1], fill=C_CARD)
    d.rectangle([TBX, TBY, TBX + TBW - 1, TBY + TBH - 1], outline=C_BORDER)
    d.rectangle([TBX, TBY, TBX + TBW - 1, TBY + 17], fill=C_CARD2)
    for x, hd in zip([10, 92, 152, 212, 285, 330, 380, 430],
                     ["SERIAL", "TYP", "WYS [m]", "DIST", "AZ", "MHz", "SNR", "LIVE"]):
        d.text((TBX + x, TBY + 4), hd, font=F(8), fill=C_FAINT)
    row_y = TBY + 21
    for r in on_map[:4]:
        d.text((TBX + 10, row_y), r["serial"], font=F(11, True), fill=C_WHITE)
        d.text((TBX + 92, row_y), r["type"], font=F(11), fill=C_ACCENT)
        d.text((TBX + 152, row_y), "%.0f" % r["alt"], font=F(11), fill=C_TEXT)
        d.text((TBX + 212, row_y), "%.0fkm" % r["dist"], font=F(11), fill=C_TEXT)
        d.text((TBX + 285, row_y), "%03.0f." % r["az"], font=F(11), fill=C_TEXT)
        freq = r.get("freq")
        d.text((TBX + 330, row_y), ("%.3f" % freq) if freq else "--", font=F(11), fill=C_YELLOW)
        snr = r.get("snr")
        d.text((TBX + 380, row_y), ("%.0fdB" % snr) if snr else "--", font=F(11), fill=C_TEXT)
        d.ellipse([TBX + 430, row_y + 4, TBX + 436, row_y + 10], fill=C_GREEN)
        d.text((TBX + 442, row_y), "ON", font=F(8), fill=C_FAINT)
        row_y += 15
    d.text((4, H - 12), "QTH: %.2fN %.2fE  •  dane: sondehub.org" % qth, font=F(8), fill=C_FAINT)

    out = "/tmp/radiosonda_preview.png"
    img.save(out)
    print("Zapisano:", out, img.size)

    # ======= EKRAN 2: mapa + markery, bez UI =======
    imgm = Image.new("RGB", (W, H), C_BG)
    paste_into(imgm, mp, 0, 0, W, H)
    dm = ImageDraw.Draw(imgm)
    qx, qy = px_latlon(qth[0], qth[1])
    dm.ellipse([qx - 4, qy - 4, qx + 4, qy + 4], fill=C_YELLOW)
    dm.ellipse([qx - 8, qy - 8, qx + 8, qy + 8], outline=C_YELLOW, width=1)
    for s in on_map[4:]:
        sx, sy = px_latlon(s["lat"], s["lon"])
        dm.ellipse([sx - 2, sy - 2, sx + 2, sy + 2], fill=C_WHITE)
        dm.ellipse([sx - 4, sy - 4, sx + 4, sy + 4], outline=(80, 100, 130), width=1)
    for i, s in enumerate(on_map[:4]):
        sx, sy = px_latlon(s["lat"], s["lon"])
        col = colors[i % len(colors)]
        dm.ellipse([sx - 4, sy - 4, sx + 4, sy + 4], fill=col)
        dm.ellipse([sx - 9, sy - 9, sx + 9, sy + 9], outline=col, width=2)
    out2 = "/tmp/radiosonda_map_only.png"
    imgm.save(out2)
    print("Zapisano:", out2, imgm.size)


if __name__ == "__main__":
    main()
