#!/usr/bin/env python3
"""Tworzy skompresowane odpowiedniki plikow WWW w data/ (*.gz).

Firmware (streamStaticAsset w main.cpp) serwuje "<plik>.gz" gdy klient wysle
"Accept-Encoding: gzip", a WebServer sam doda naglowek Content-Encoding: gzip.
Uruchom PRZED 'pio run -t buildfs' - inaczej LittleFS nie bedzie mial wersji .gz
i firmware spadnie na wersje nieskompresowana (wolniejsze ladowanie WWW).
"""
import gzip
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "data")

TARGETS = [
    "index.html", "indexEN.html", "indexDE.html", "indexFR.html", "indexES.html",
    "indexRU.html", "style.css", "style.base.css", "style.desktop.css",
    "style.mobile.css", "style.modern.css", "satellite.js",
]

total_raw = total_gz = 0
for name in TARGETS:
    src = os.path.join(DATA, name)
    if not os.path.isfile(src):
        print("pominieto (brak):", name)
        continue
    dst = src + ".gz"
    with open(src, "rb") as f:
        raw = f.read()
    # mtime=0 -> deterministyczny plik (te same bajty dla tych samych danych).
    with open(dst, "wb") as f:
        f.write(gzip.compress(raw, compresslevel=9, mtime=0))
    gz = os.path.getsize(dst)
    total_raw += len(raw)
    total_gz += gz
    print("%-20s %7d -> %6d (%d%%)" % (name, len(raw), gz, round(gz * 100 / len(raw))))

print("RAZEM: %d -> %d bajtow" % (total_raw, total_gz))
