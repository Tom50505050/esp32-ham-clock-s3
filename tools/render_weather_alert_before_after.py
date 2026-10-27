#!/usr/bin/env python3
"""Podglad PRZED/PO ekranu alertu pogodowego (burza + wiatr).

PRZED: czarny prostokat pod napisami.
PO:    sama ramka akcentu + czerwone napisy (grafika widoczna pod spodem).

Uruchom: python3 tools/render_weather_alert_before_after.py [wyjscie.png]
Domyslnie: weather_alert_przed_po.png w katalogu glownym.
"""
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from render_weather_alert_preview import ROOT, RED, ORANGE, load_font, fit_icon  # noqa: E402

LABEL_BG = (30, 30, 40)
LABEL_FG = (230, 230, 230)


def render(storm, icon_path, black_box):
    img = Image.new("RGB", (480, 320), (0, 0, 0))
    d = ImageDraw.Draw(img)
    accent = RED if storm else ORANGE

    ic = fit_icon(icon_path, 480, 320)
    img.paste(ic, (0, 0))

    title = "BURZA" if storm else "SILNY WIATR"
    gust = "-- m/s"
    in6h = "w ciagu 6h"
    f1, f2, f3 = load_font(32), load_font(24), load_font(16)
    box_w = int(max(d.textlength(title, font=f1), d.textlength(gust, font=f2),
                    d.textlength(in6h, font=f3))) + 28
    box_x = max(4, 240 - box_w // 2)
    box_y, box_h = 104, 128
    if black_box:
        d.rectangle([box_x, box_y, box_x + box_w, box_y + box_h], fill=(0, 0, 0), outline=accent)
    else:
        d.rectangle([box_x, box_y, box_x + box_w, box_y + box_h], outline=accent)
    d.text((240, 118), title, font=f1, fill=RED, anchor="mt")
    d.text((240, 164), gust, font=f2, fill=RED, anchor="mt")
    d.text((240, 202), in6h, font=f3, fill=RED, anchor="mt")

    d.rectangle([449, 7, 474, 32], fill=ORANGE, outline=(0, 0, 0))
    d.line([456, 14, 468, 25], fill=(0, 0, 0), width=2)
    d.line([468, 14, 456, 25], fill=(0, 0, 0), width=2)
    return img


def label(text):
    strip = Image.new("RGB", (480, 34), LABEL_BG)
    d = ImageDraw.Draw(strip)
    d.text((10, 9), text, fill=LABEL_FG, font=load_font(18))
    return strip


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "weather_alert_przed_po.png")
    storm = os.path.join(ROOT, "data", "icon50", "burza.bmp")
    wind = os.path.join(ROOT, "data", "icon50", "wiatr.bmp")
    rows = [
        label("PRZED: burza (czarne tlo pod napisami)"),
        render(True, storm, True),
        label("PO: burza (sama ramka + czerwone napisy)"),
        render(True, storm, False),
        label("PRZED: silny wiatr (czarne tlo pod napisami)"),
        render(False, wind, True),
        label("PO: silny wiatr (sama ramka + czerwone napisy)"),
        render(False, wind, False),
    ]
    h = sum(r.height for r in rows)
    sheet = Image.new("RGB", (480, h), LABEL_BG)
    y = 0
    for r in rows:
        sheet.paste(r, (0, y))
        y += r.height
    sheet.save(out)
    print("zapisano", out)


if __name__ == "__main__":
    main()
