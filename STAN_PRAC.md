# Stan prac — ESP32-HAM-CLOCK-S3

Ostatnia aktualizacja: 2026-10-13 (wieczor). Ostatni commit: `a1f9394`.

## Zrobione dzisiaj (PSK / mapa)

- Spoty **kumuluja sie** na mapie (dedup: znak+pasmo+tryb, `pskAddSpot()`),
  limit `PSK_MAX_SPOTS = 200`.
- **AUTO-RESET** po osiagnieciu 200 spotow — mapa czysci sie sama i startuje
  nowy cykl (log: `[PSK] AUTO-RESET: osiagnieto 200 spotow`).
- **Panel po prawej** (`drawPskSpotListPanel()`): licznik `SPOTY: N` + lista
  najnowszych (znak, pasmo, tryb). Obszar 310,40 166x250.
- **Odswiezanie przyrostowe** (`drawPskMapSpotsOnly()`): przy nowych spotach
  rysowane sa TYLKO spoty + panel + kropka + przycisk menu. Naglowek, pasek,
  ikona baterii, napisy, legenda i stopka pozostaja nietkniete (bez migania).
- MQTT startuje domyslnie (`pskMqttEnabled = true`, NVS `psk_mqtt_en = true`).
- Przycisk RESET w menu PSK czysci spoty (`resetPskMapSpots()`).
- Zapis rozmow: `tools/export_conversation.py` + `conversations/`.

Buildy: `esp32-s3-n16r8`, `-notouch`, `-altpins` — 3/3 SUCCESS.
Artefakty w `dist/` i `/workspace/share/` (merged-flash*.bin, ZIP).

## Do zrobienia rano

1. **Weryfikacja na urzadzeniu** — po flashowaniu potwierdzic, ze:
   - spoty pojawiaja sie na mapie i narastaja (`spotow=28/29/30 ...`),
   - przy odswiezaniu MQTT **nie mruga** naglowek/bateria/napisy,
   - po 200 spotach nastepuje AUTO-RESET,
   - RESET czysci mape do zera (`spotow=0`).
2. **Spojnosc przycisku ANULUJ** — sprawdzic, czy rysowanie i obszar dotyku
   zgadzaja sie (`x=300..380` vs `230..300` z AGENTS.md). W widocznych
   edycjach nie bylo zmiany wspolrzednych.
3. Ewentualne dopracowanie panelu po prawej (rozmiar/czcionka), jesli na
   ekranie wyglada za ciasno lub zaslania spoty.

## Uwagi techniczne

- `git commit` domyslnie wymaga podpisu GPG (`commit.gpgsign=true`). Klucz
  prywatny jest w `.gpg-release-key` (poza VCS). Bez klucza: `--no-gpg-sign`.
- NIE commitujemy pelnego zapisu rozmow ani `base_state.json` (klucz API).
