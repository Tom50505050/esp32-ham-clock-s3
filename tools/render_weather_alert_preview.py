#!/usr/bin/env python3
"""Renderuje podglad ekranu alertu pogodowego (burza + wiatr) do PNG.

Odwzorowuje uklad z firmware drawWeatherAlertScreen() (480x320), z domyslnymi
ikonami z data/icon50. Uruchom z katalogu repozytorium:

    python3 tools/render_weather_alert_preview.py [wyjscie.png]

Domyslny plik wyjsciowy: weather_alert_preview.png w katalogu glownym.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

RED = (255, 0, 0)
ORANGE = (255, 136, 0)
GREY = (85, 85, 85)
DARKGREY = (128, 128, 128)


def load_font(size, bold=True):
    for name in (("DejaVuSans-Bold.ttf", "DejaVuSans.ttf") if bold else ("DejaVuSans.ttf",)):
        for base in ("/usr/share/fonts/truetype/dejavu/", "/usr/share/fonts/"):
            p = base + name
            if os.path.exists(p):
                return ImageFont.truetype(p, size)
    return ImageFont.load_default()


def fit_icon(path, box_w, box_h):
    """Skaluje grafike proporcjonalnie (cover, nearest) do okna box_w x box_h.

    Pokrywa cale okno bez czarnych pasow; nadmiar jest przycinany symetrycznie
    (odpowiada fit=cover w firmware).
    """
    im = Image.open(path).convert("RGB")
    iw, ih = im.size
    s = max(box_w / iw, box_h / ih)
    nw, nh = max(1, round(iw * s)), max(1, round(ih * s))
    im = im.resize((nw, nh), Image.NEAREST)
    if nw > box_w or nh > box_h:
        x = (nw - box_w) // 2
        y = (nh - box_h) // 2
        im = im.crop((x, y, x + box_w, y + box_h))
    return im


def render(storm, icon_path, out_path):
    img = Image.new("RGB", (480, 320), (0, 0, 0))
    d = ImageDraw.Draw(img)
    accent = RED if storm else ORANGE

    # Grafika wypelnia CALY ekran (fit=cover), brak czarnych pasow.
    ic = fit_icon(icon_path, 480, 320)
    img.paste(ic, (0, 0))

    # Czerwony napis alertu wysrodkowany na ekranie - bez czarnego tla, tylko
    # ramka akcentu + napisy (grafika widoczna pod spodem).
    title = "BURZA" if storm else "SILNY WIATR"
    gust = "-- m/s"
    in6h = "w ciagu 6h"
    f1, f2, f3 = load_font(32), load_font(24), load_font(16)
    w1 = d.textlength(title, font=f1)
    w2 = d.textlength(gust, font=f2)
    w3 = d.textlength(in6h, font=f3)
    box_w = int(max(w1, w2, w3)) + 28
    box_x = 240 - box_w // 2
    if box_x < 4:
        box_x = 4
    box_y, box_h = 104, 128
    d.rectangle([box_x, box_y, box_x + box_w, box_y + box_h], outline=accent)
    d.text((240, 118), title, font=f1, fill=RED, anchor="mt")
    d.text((240, 164), gust, font=f2, fill=RED, anchor="mt")
    d.text((240, 202), in6h, font=f3, fill=RED, anchor="mt")

    # Przycisk zamkniecia (X) w prawym gornym rogu.
    d.rectangle([449, 7, 474, 32], fill=ORANGE, outline=(0, 0, 0))
    d.line([456, 14, 468, 25], fill=(0, 0, 0), width=2)
    d.line([468, 14, 456, 25], fill=(0, 0, 0), width=2)

    return img


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "weather_alert_preview.png")
    storm = render(True, os.path.join(ROOT, "data", "icon50", "burza.bmp"), out)
    wind = render(False, os.path.join(ROOT, "data", "icon50", "wiatr.bmp"), out)
    canvas = Image.new("RGB", (480, 320 * 2 + 20), (30, 30, 40))
    canvas.paste(storm, (0, 0))
    canvas.paste(wind, (0, 340))
    canvas.save(out)
    print("zapisano", out)


if __name__ == "__main__":
    main()
