#!/usr/bin/env python3
"""Buduje stronę wgrywania firmware przez przeglądarkę (ESP Web Tools).

Tworzy katalog flash/ z:
  index.html            - strona z <esp-web-install-button> (w repo, edytowalna)
  manifest-touch.json   - manifest dla merged-flash.bin (płytka z dotykiem)
  manifest-notouch.json - manifest dla merged-flash-notouch.bin (bez dotyku)
  merged-flash.bin      - skopiowany z dist/
  merged-flash-notouch.bin

Następnie publikuje całość do /workspace/share/flash/ (HTTPS) i download/flash/.

Uruchom PO 'bash tools/make_release.sh' (czyli po zbudowaniu dist/*.bin).
"""
import json
import os
import shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIST = os.path.join(ROOT, "dist")
FLASH = os.path.join(ROOT, "flash")
VERSION = "1.2d"

BUILDS = [
    ("touch", "merged-flash.bin", "ESP32-HAM-CLOCK v%s (dotyk XPT2046)" % VERSION),
    ("notouch", "merged-flash-notouch.bin", "ESP32-HAM-CLOCK v%s (bez dotyku)" % VERSION),
]


def manifest(name, bin_name):
    return {
        "name": name,
        "version": VERSION,
        # Przy pierwszym wgraniu (albo po zmianie tablicy partycji) wyczysc flash.
        "new_install_prompt_erase": True,
        "builds": [
            {
                "chipFamily": "ESP32-S3",
                "parts": [{"path": bin_name, "offset": 0}],
            }
        ],
    }


def main():
    os.makedirs(FLASH, exist_ok=True)
    for key, bin_name, name in BUILDS:
        src = os.path.join(DIST, bin_name)
        if not os.path.isfile(src):
            raise SystemExit("brak %s - uruchom najpierw tools/make_release.sh" % src)
        shutil.copy2(src, os.path.join(FLASH, bin_name))
        mpath = os.path.join(FLASH, "manifest-%s.json" % key)
        with open(mpath, "w", encoding="utf-8") as f:
            json.dump(manifest(name, bin_name), f, indent=2, ensure_ascii=False)
            f.write("\n")
        print("flash/manifest-%s.json + %s" % (key, bin_name))

    # Publikacja: /workspace/share/flash (HTTPS, z binarkami) + download/flash
    # (staging repo - TYLKO strona i manifesty; binarki sa w dist/ i release).
    published = []
    targets = [
        ("/workspace/share/flash", True),
        (os.path.join(ROOT, "download", "flash"), False),
    ]
    for dst_root, with_bins in targets:
        if not os.path.isdir(os.path.dirname(dst_root)):
            continue
        if os.path.isdir(dst_root):
            shutil.rmtree(dst_root)
        os.makedirs(dst_root)
        for name in os.listdir(FLASH):
            if not with_bins and name.endswith(".bin"):
                continue
            shutil.copy2(os.path.join(FLASH, name), os.path.join(dst_root, name))
        published.append(dst_root)
    print("opublikowano w:", ", ".join(published) if published else "(tylko flash/)")


if __name__ == "__main__":
    main()
