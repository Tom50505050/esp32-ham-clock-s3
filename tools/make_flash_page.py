#!/usr/bin/env python3
"""Buduje stronę wgrywania firmware przez przeglądarkę (ESP Web Tools).

W repo flash/ trzyma stronę i manifesty (bez binarek - wyklucza je .gitignore).
Manifesty wskazują binarkę z katalogu nadrzędnego, bo na GitHub Pages flash/ jest
rodzeństwem download/ (binarki leżą w download/).

Następnie publikuje:
  /workspace/share/flash/  - kopia z binarkami (lokalny serwer HTTPS); manifesty
                             wskazują ../merged-flash*.bin
  download/flash/          - staging repo (strona + manifesty); manifesty i linki
                             wskazują ../merged-flash*.bin (binarki w download/)

Uruchom PO 'bash tools/make_release.sh' (czyli po zbudowaniu dist/*.bin).
"""
import json
import os
import shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIST = os.path.join(ROOT, "dist")
FLASH = os.path.join(ROOT, "flash")
VERSION = "1.2e BETA"

BUILDS = [
    ("touch", "merged-flash.bin", "ESP32-HAM-CLOCK v%s (dotyk XPT2046)" % VERSION),
    ("notouch", "merged-flash-notouch.bin", "ESP32-HAM-CLOCK v%s (bez dotyku)" % VERSION),
]


def manifest(name, bin_path):
    return {
        "name": name,
        "version": VERSION,
        # Przy pierwszym wgraniu (albo po zmianie tablicy partycji) wyczysc flash.
        "new_install_prompt_erase": True,
        "builds": [
            {
                "chipFamily": "ESP32-S3",
                "parts": [{"path": bin_path, "offset": 0}],
            }
        ],
    }


def write_manifests(dst_dir, bin_prefix):
    """Zapisuje manifesty w dst_dir, wskazujac binarke jako bin_prefix + nazwa."""
    for key, bin_name, name in BUILDS:
        with open(os.path.join(dst_dir, "manifest-%s.json" % key), "w", encoding="utf-8") as f:
            json.dump(manifest(name, bin_prefix + bin_name), f, indent=2, ensure_ascii=False)
            f.write("\n")


def rewrite_bin_links(path, src_prefix, dst_prefix):
    """Przepisuje href="src_prefix..." na href="dst_prefix..." w skopiowanej stronie."""
    if not os.path.isfile(path):
        return
    with open(path, encoding="utf-8") as f:
        html = f.read()
    with open(path, "w", encoding="utf-8") as f:
        f.write(html.replace('href="%s' % src_prefix, 'href="%s' % dst_prefix))


def main():
    os.makedirs(FLASH, exist_ok=True)
    for key, bin_name, name in BUILDS:
        src = os.path.join(DIST, bin_name)
        if not os.path.isfile(src):
            raise SystemExit("brak %s - uruchom najpierw tools/make_release.sh" % src)
        shutil.copy2(src, os.path.join(FLASH, bin_name))
    # flash/ w repo jest serwowane na GitHub Pages pod /flash/, jako rodzenstwo
    # /download/ - binarki sa w /download/, wiec sciezka to ../download/.
    write_manifests(FLASH, "../download/")
    print("flash/manifest-*.json (bin -> ../download/)")

    # Publikacja: /workspace/share/flash (lokalny serwer, binarki w katalogu
    # nadrzednym) + download/flash (staging repo, binarki w katalogu nadrzednym).
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
        write_manifests(dst_root, "../")
        rewrite_bin_links(os.path.join(dst_root, "index.html"), "../download/", "../")
        published.append(dst_root)
    print("opublikowano w:", ", ".join(published) if published else "(tylko flash/)")

    # Manifesty dla przycisku <esp-web-install-button> wbudowanego na stronie
    # projektu (download/index.html) - leza OBOK strony i binarek (sciezka pusta).
    for root_dir in ("/workspace/share", os.path.join(ROOT, "download")):
        if not os.path.isdir(root_dir):
            continue
        write_manifests(root_dir, "")
        print("manifesty strony projektu w:", root_dir)


if __name__ == "__main__":
    main()
