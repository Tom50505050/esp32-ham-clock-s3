# Stan prac — ESP32-HAM-CLOCK-S3

Ostatnia aktualizacja: 2026-10-18 (wieczor). Wersja firmware: **v1.2c**.

## v1.2c — poprawki PSK MQTT i nagłówka (18.10.2026)

- **PSK MQTT**: zmiana pasma/trybu nie ściągała spotów — subskrypcja budowała filtr
  6-segmentowy (`.../<pasmo>/<tryb>/+/+/+`), a realny temat brokera ma 11 segmentów.
  Poprawka: `.../<pasmo>/<tryb>/#`. Dodano mapowanie pasm 160/30/17/12 m (HTTP i
  MQTT), poprawiono indeks odbiorcy (5→6), po zapisie filtra czyszczone są spoty,
  resubskrypcja odsubskrybowuje stary temat.
- **Ikona baterii 12 px w lewo** na wszystkich ekranach (zegar `screenW-82`,
  `uiHeader` `408`), `rightLimit` 415→403, mirror w `drawBatteryQuickUpdate()`.
- **`tools/render_header_preview.py`** zsynchronizowany z firmware.
- **`FIRMWARE_VERSION` 1.2b → 1.2c**; zaktualizowane `data/index.html`,
  `data/indexEN.html`, `tools/make_qsl_splash.py`.

## Zrobione wcześniej (PSK / mapa)

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

Buildy: `esp32-s3-n16r8`, `-notouch` — SUCCESS. Artefakty w `dist/`, `download/`
i `/workspace/share/` (merged-flash*.bin, ZIP).

## Uwagi techniczne

- `git commit` domyslnie wymaga podpisu GPG (`commit.gpgsign=true`). Klucz
  prywatny jest w `.gpg-release-key` (poza VCS). Bez klucza: `--no-gpg-sign`.
- NIE commitujemy pelnego zapisu rozmow ani `base_state.json` (klucz API).
