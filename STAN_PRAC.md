# Stan prac — ESP32-HAM-CLOCK-S3

Ostatnia aktualizacja: 2026-10-27. Wersja firmware: **v1.2d**.

## Precyzyjny pomiar baterii — kalibracja ADC + krzywa Li-ion (27.10.2026)

- **Kalibracja eFuse**: `readBatteryVoltage()` używa `analogReadMilliVolts()`
  (kalibracja Two Point / Vref z eFuse) zamiast surowego `analogRead()`; w `setup()`
  `analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db)`.
- **Średnia obcięta** `batteryTrimmedMeanMv(12, 2)` — 12 próbek, sortowanie, odrzut
  2 skrajnych z każdej strony.
- **Korekcja dzielnika**: `batteryDividerCal` (NVS `bat_div_cal`),
  `batteryVoltOffset` (NVS `bat_off`);
  `Vbat = (Vpin_mV/1000) * 2.0 * bat_div_cal + bat_off`.
- **Krzywa Li-ion**: `kBatteryCurve[]` (15 pkt, 3.40-4.20 V) + interpolacja w
  `calculateBatteryPercentage()` (3.80 V → 47%, 3.70 V → 33%, 3.60 V → 18%).
- **Histereza ładowania**: >4.25 V włącza, <4.10 V wyłącza `batteryCharging`.
- **WWW**: zakładka System → „Kalibracja baterii" (`bat_cal_v`, `bat_div_cal`,
  `bat_off`); `/api/config` eksportuje `bat_div_cal`, `bat_off`, `sys_batt_pct`,
  `sys_batt_chg`. Naprawiona literówka `ENABLE_BATTERY_MONITORING`.
- **Ikona (ujednolicona)**: jedna `drawBatteryIcon(x, y, percentage = -1,
  bg = TFT_BLACK)` z wspólnymi `drawBatteryFill()` + `drawBatteryBolt()`; kolor z
  `getBatteryColor()` (histereza ładowania), wnętrze czyszczone `bg` (poziom może
  maleć bez starego wypełnienia), błyskawica przy ładowaniu. Układ zgodny z
  `drawBatteryQuickUpdate` (ramka `y+2`, wnętrze `y+4`). Wywołania: zegar
  klasyczny `TFT_RADIO_ORANGE`, nowoczesny `HC_BG`, `uiHeader` `bg`. Usunięto
  martwe `drawBatteryIcon(x,y,w,h)` i `drawBatteryStatus()`.
  `tools/render_header_preview.py` zsynchronizowany (`_batt_color`).
- **Build**: `esp32-s3-n16r8` + `-notouch` SUCCESS (RAM 34.0%, Flash 38.7%).
- **Pola dotyku (audyt)**: wspolny `handleScreenSwitchTouch()`; PSK/STACJA/
  LOGGER nie sa juz przechwytywane przez nawigacje w rogach (przyciski dzialaja),
  nawigacja tylko w pasie stopki `y>=296`; etykieta jezyka w menu jasnosci
  208 -> 220.
- **Paski WiFi**: `rssiToBars()` (progi -55/-67/-78/-88) wspolne dla naglowka i
  monitora; `drawWifiSignalBars(x,y,strength,bg)` czysci tlo; `drawWifiBarsQuickUpdate()`
  co 5 s w `loop()`; `RSSI==0` -> 1 kreska zamiast falszywego braku zasiegu.

## v1.2d — czcionki zegara, alert pogodowy, migotanie, pin baterii (23.10.2026)

- **Grafika alertu (burza/wiatr) na całym ekranie**: `WX_ICON_TARGET_W` 408→**480**
  (panel 408×320 → pełne 480×320), skalowanie **fit=contain → fit=cover** (pokrycie
  ekranu bez czarnych pasów, nadmiar przycinany symetrycznie, małe obrazy
  powiększane). Domyślne `data/icon50/burza.bmp` / `wiatr.bmp` generowane od nowa w
  **480×320** (`tools/make_weather_icons.py`). WWW (6 języków) i narzędzia podglądu
  zsynchronizowane (480×320, cover); komentarze „170×170 / contain / 408" usunięte.

- **Wybór czcionki znaku i zegara**: 14 krojów VLW + wbudowana, menu TIME MODE
  (siatka 5×3), zapis NVS `clock_font`. Rysowanie `drawString()` + `TC_DATUM`,
  centrowanie w stałych komórkach (monospace), pion od rzeczywistego atramentu.
- **Alert pogodowy — własne grafiki (burza/wiatr)**: pliki w LittleFS, ścieżki w
  NVS `wxal_ic_s/wxal_ic_w`, trasy `/upload_wxicon_storm|wind`, `/wxicon_preview`,
  `/wxicon_reset`; podgląd całego ekranu 480×320 w WWW.
- **Naprawa migotania cyfr zegara (wszystkie 15 czcionek)**: dirty-cells w
  `clockDrawMonospace()` (tylko zmienione komórki, ~67% mniej pikseli na SPI),
  `clockTimeInkOffset()` (stały pionowy offset `"0123456789:"` — koniec drgania
  1 px), guard przerysowania w `updateScreen1ClockClassic()`.
- **`FIRMWARE_VERSION` 1.2c → 1.2d**; `data/index*.html` (6 języków),
  `tools/lang_pages.json`, `platformio.ini`, README/RELEASE_NOTES zaktualizowane.

- **Ekran DX SPOTS — nazwy krajów (`dxspots.com`)**: dodano
  `countryFromCallsignPrefix()` (kraj z prefiksu znaku, dopasowanie najdłuższego
  prefiksu) wywoływane w `parseDXSpot()`, analogicznie do `potaCountryFromLocationDesc`
  na ekranie POTA. Kraj pojawia się od razu, bez czekania na QRZ; QRZ nadal może
  go nadpisać/uzupełnić.
- **Pin dzielnika napięcia baterii: GPIO 1 → GPIO 7** (ADC1_CH0 → ADC1_CH6);
  `BATTERY_ADC_PIN` = **7** w `main.cpp`, zaktualizowane `PINOUT.md`, `README.md`,
  `README_S3.md`, `INSTRUKCJA.md` i schemat na stronie WWW.
- `tools/make_zip.py` pomija lokalny `mock_openmeteo.py` (nie trafia do paczki).
- **PSK MAP — tryb „połączenia" (TX→RX)**: przełącznik „Lacza: TAK/NIE" w menu
  ekranu PSK + NVS `psk_show_links` (domyślnie NIE). Włączony rysuje linie
  nadawca→odbiorca z raportów PSKReporter (MQTT: `rc`/`rl` lub segmenty tematu;
  HTTP: `receiverCallsign`/`receiverLocator`) i pary `TX>RX` na liście.

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

Buildy: `esp32-s3-n16r8`, `-notouch` — SUCCESS. Artefakty w `dist/`, `download/`
i `/workspace/share/` (merged-flash*.bin, ZIP).

## Uwagi techniczne

- `git commit` domyslnie wymaga podpisu GPG (`commit.gpgsign=true`). Klucz
  prywatny jest w `.gpg-release-key` (poza VCS). Bez klucza: `--no-gpg-sign`.
