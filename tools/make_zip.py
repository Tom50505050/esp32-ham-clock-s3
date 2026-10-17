#!/usr/bin/env python3
"""Buduje paczkę esp32-ham-clock-s3.zip (źródła + data/ + gotowe .bin)."""
import glob
import os
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)

ROOT_FILES = [
    "flash_touch_test.bat", "platformio.ini", "flash_touch_test.sh", "AGENTS.md",
    "partitions.csv", ".gitignore", "main.cpp", "flash.bat", "tft_setup.h",
    "iss_worldmask.h", "flash.sh", "README_S3.md", "PINOUT.md", "INSTRUKCJA.md",
]
# Do paczki trafiaja tylko scalone obrazy (merged-flash*.bin) - zawieraja
# bootloader, partycje, aplikacje i LittleFS w jednym pliku (flash pod 0x0).
# Wariant "notouch" dla plytek bez podlaczonego XPT2046.
DIST_BINS = [
    "merged-flash.bin",
    "merged-flash-notouch.bin",
]

files = [f for f in ROOT_FILES if os.path.isfile(f)]
files += [f for f in glob.glob("data/**", recursive=True) if os.path.isfile(f)]
files += [f for f in glob.glob("tools/touch_test/**", recursive=True) if os.path.isfile(f)]
files += [f for f in glob.glob("tools/*.py") if os.path.isfile(f)]
files += [os.path.join("dist", b) for b in DIST_BINS if os.path.isfile(os.path.join("dist", b))]

with zipfile.ZipFile("esp32-ham-clock-s3.zip", "w", zipfile.ZIP_DEFLATED) as z:
    for f in files:
        arc = os.path.basename(f) if f.startswith("dist/") else f
        z.write(f, arc)

print("zip entries:", len(zipfile.ZipFile("esp32-ham-clock-s3.zip").namelist()))
