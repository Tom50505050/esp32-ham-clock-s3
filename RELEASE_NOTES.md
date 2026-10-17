# ESP32-HAM-CLOCK — informacje o wydaniach

Dokument obejmuje wydania **v1.2**, **v1.2a** i **v1.2b** dla platformy
**ESP32-S3 N16R8** (16 MB flash, 8 MB PSRAM) z wyświetlaczem
**ILI9488 na 3.5" i 4" 480×320** po SPI i panelem dotykowym XPT2046.

> **Wgrywanie przez przeglądarkę (WebSerial):**
> https://jason2866.github.io/WebSerial_ESPTool/ — wybierz **`merged-flash.bin`**
> (płytka z dotykiem) albo **`merged-flash-notouch.bin`** (bez dotyku), offset `0x0`.
> W wydaniach dostępne są **tylko te dwa pliki** — osobne obrazy `firmware.bin`,
> `littlefs.bin`, `bootloader.bin`, `boot_app0.bin` i `partitions.bin` zostały
> usunięte ze wszystkich wydań.

---

# ESP32-HAM-CLOCK v1.2b — ekrany STACJA i ESP32 HAM LOGGER

Wydanie **v1.2b** (17.10.2026) dla platformy **ESP32-S3 N16R8** + **ILI9488 na 3.5" i 4" 480×320**
+ XPT2046. Zakres: **nowy ekran STACJA (15. ekran)**, **nowy ekran ESP32 HAM LOGGER
(16. ekran)**, poprawki nagłówka (ikona GPS po lewej, ikona baterii na wszystkich
ekranach), **naprawa PSK Reporter i POTA Cluster** (brak danych), integracja
z UI WWW oraz narzędzia podglądu. Pozostałe ekrany i zależności bez zmian.

## Zmiany od v1.2a

### Nagłówek — ikona GPS i ikona baterii
- **Ikona GPS po lewej stronie nagłówka** — `GPS_ICON_X` przeniesione z `372` na
  **`88`** (obszar dotyku `gpsIconHitX` z `364` na `82`). Ikona leży zaraz za
  godziną HH:MM, przed logo; dotknięcie nadal otwiera ekran satelitów.
- **Wspólny `drawHeaderTime(fg, bg)`** dla obu stylów nagłówka zegara
  (klasyczny i nowoczesny) — mniej duplikacji kodu.
- **Ikona baterii na wszystkich ekranach** — wspólny nagłówek `uiHeader()`
  (używany przez wszystkie ekrany poza zegarem) rysuje teraz ikonę baterii.
  `drawBatteryQuickUpdate()` przepisany: rysuje ikonę w tej samej geometrii co
  `drawBatteryIcon` i z właściwym tłem nagłówka (klasyczny / nowoczesny / splash),
  więc obudowa i poziom naładowania są spójne na każdym ekranie. `loop()` woła
  `drawBatteryQuickUpdate(true)`, więc poziom odświeża się także poza ekranem zegara.

### Nowy ekran STACJA (15. ekran)
- **Wyszukiwanie znaku** — dane pobierane kolejno ze źródeł
  **QRZ.com → HamDB → callook.info** (pierwsze trafienie wygrywa; nazwa źródła
  pokazywana na ekranie). Nowe funkcje: `stationLookupStart()`,
  `fetchHamDbCallsignInfo()`, obsługa callook.
- **Dwa transceivery** (`stationDrawRadio()`) — lewy **czerwony** = Twój znak,
  prawy **zielony** = szukany znak. Napis znaku jest **czarny na tle w kolorze
  radia** (czytelny na jasnym wyświetlaczu), a korpusy radia zachowują kolory.
- **Karta wyniku** — pola **Imię / QTH / Grid / Kraj**, linia
  **dystans + kierunek geograficzny** (np. `42 km  NE`) oraz **źródło danych
  i godzina pobrania** (HH:MM, czas lokalny).
- **Kierunek skrótami** — `N, NE, E, SE, S, SW, W, NW` (8 azymutów po 45°),
  liczony funkcją `calculateBearing()` z lokatora/QTH względem Twojej pozycji.
- **Linie fal** między antenami kolorowane wg odległości
  (`propPolarityColor()`).
- **Stany ekranu** — start, klawiatura ekranowa (`drawStationKeyboard()`,
  `handleStationTouch()`), pobieranie (`stationDrawLoading()`), wynik oraz
  „Nie znaleziono".
- **Styl klasyczny i nowoczesny** — ekran respektuje `uiStyleClassic()`; układ
  i dane wspólne dla obu stylów.

### Nowy ekran ESP32 HAM LOGGER (16. ekran)
- **Formularz QSO** — `drawLoggerForm()`: **CALLSIGN** (żółta ramka),
  **RST (S) / RST (R)**, **BAND / FREQ / MODE**, **NAME / QTH**,
  **DATE/TIME** (UTC z NTP).
- **Klawiatura ekranowa** — `drawLoggerKeyboard()`, `loggerHandleKeyboardKey()`;
  wspólna dla wszystkich pól; po zamknięciu CALLSIGN **autouzupełnia NAME/QTH**
  (`loggerLookupCallsign()` — QRZ.com → HamDB → callook.info).
- **BAND / MODE** — dotknięcie cyklicznie zmienia wartość; zmiana pasma podstawia
  domyślną częstotliwość (`loggerBandFreq[]`).
- **Tabela „Ostatnie łączności"** — 3 najnowsze wpisy (CALL/FREQ/MODE/UTC).
- **4 przyciski dotykowe** — SAVE QSO / CLEAR / EXPORT ADIF / SETTINGS
  (`handleLoggerTouch()`).
- **Styl klasyczny i nowoczesny** — respektuje `uiStyleClassic()`.
- **Log w PSRAM + bezpieczny zapis** — rekordy w **PSRAM** (`LogQso`,
  `loggerEnsureBuffer()`), zapis dopiero na SAVE; **plik tymczasowy + rename**
  (`/qso.adi.tmp` → `/qso.adi`) dla odporności na utratę zasilania
  (`loggerSaveAll()`); **ADIF bajtowo poprawny** (`loggerAdifField()` liczy
  długość w bajtach UTF-8).

### UI WWW
- Nowa pozycja **STACJA** (`station`) oraz **LOGGER** (`logger`) na liście
  ekranów w `data/index.html` i `data/indexEN.html`; liczba stron zwiększona
  z 14 do **16**.
- **`GET /qso.adi`** — pobranie pliku ADIF.
- **`POST /api/qrz_upload`** — wysyłka do QRZ Logbook (gotowe na przyszłość,
  wymaga klucza API) + pole **QRZ.com API Key (Logbook)** w ustawieniach.

### Narzędzia (`tools/`)
- `tools/render_station_preview.py` — podgląd ekranu STACJA 1:1 (Pillow):
  start, klawiatura, pobieranie, wynik, wariant klasyczny, brak wyniku.
- `tools/render_logger_preview.py` — podgląd ekranu LOGGER 1:1 (Pillow).
- `tools/render_pskmap_preview.py` — podgląd ekranu PSK MAP.
- `tools/render_header_preview.py` — podgląd nagłówków (klasyczny, nowoczesny,
  wspólny) 1:1.

### Wersja
- `FIRMWARE_VERSION`: `1.2a` → **`1.2b`**.

### Naprawa: PSK Reporter i POTA Cluster (brak danych)
- **PSK Reporter (pusta mapa).** Dwie nakładające się przyczyny po stronie usługi:
  domyślny host HTTP `retrieve.pskreporter.info/query` zwracał Cloudflare
  **522/523** i 0 bajtów (origin down), a broker `mqtt.pskreporter.info`
  **przestał publikować feed PSK** (został tylko WSPR). Poprawki:
  - domyślny URL HTTP → **`https://pskreporter.info/query?`** (działa, dane na żywo);
  - domyślny tryb → **HTTP** (`pskMqttEnabled=false`) + jednorazowa **migracja NVS**
    wyłączająca MQTT na istniejących urządzeniach (własny broker użytkownika
    zostaje); gdyby MQTT wrócił, temat to `pskr/filter/v2raw_1pc/...`;
  - bufor parsowania JSON 24 KB → **512 KB w PSRAM** (odpowiedź bywa ~500 KB,
    gdy serwer zignoruje `rptlimit`).
- **POTA Cluster (brak danych).** API zwraca teraz całą listę aktywnych spotów
  (~239 spotów = ~150 KB JSON, ~73 KB po filtrze), a bufory 24 KB / 64 KB
  w wewnętrznym RAM kończyły się błędem **`NoMemory`** i spoty ginęły. Poprawki:
  bufory w **PSRAM** — **96 KB** (filtr) / **256 KB** (fallback), przez nowy
  `struct PskPsramAllocator` + `BasicJsonDocument`.

### Kompatybilność
- Bez zmian zależności i układu partycji. Zmienione pliki firmware i ZIP.

## Pliki

| Plik | Opis | Offset |
|---|---|---|
| `merged-flash.bin` | cały obraz 16 MB (bootloader + partycje + firmware + LittleFS) | `0x0` |
| `merged-flash-notouch.bin` | to samo, wariant bez obsługi dotyku | `0x0` |
| `esp32-ham-clock-s3.zip` | paczka projektu (źródła + `data/` + pliki `.bin`) | — |

## Wgrywanie

Przez przeglądarkę: **https://jason2866.github.io/WebSerial_ESPTool/** — wybierz
`merged-flash.bin` (albo `merged-flash-notouch.bin`) i offset `0x0`.

```bash
esptool.py -p COM9 write_flash 0x0 merged-flash.bin
```

Tag **v1.2b** jest podpisany kluczem GPG wydania.

Autor: **Tomasz SP9TNV**. Projekt bazowy: **Konrad SP3KON**. Licencja: **MIT**.

---

# ESP32-HAM-CLOCK v1.2 — ESP32-S3 N16R8 + ILI9488 na 3.5" i 4" 480×320

Wydanie **v1.2** dla platformy **ESP32-S3 N16R8** (16 MB flash, 8 MB PSRAM)
z wyświetlaczem **ILI9488 na 3.5" i 4" 480×320** po SPI i panelem dotykowym XPT2046.

## Zmiany w tym wydaniu

### Nowe ekrany
- **APRS RADAR (ekran 9)** — ekran taktyczny: sprite w PSRAM, animowana smuga,
  kafelki zakresu, QTH „Moja Stacja", tarcza azymut/elewacja, naprawa fałszywej
  pozycji (0,0).
- **Informacje systemowe (ekran 11)** — zastąpił **Unlis Hunter**.
- **PSK MAP** — tryb **MQTT (PSK Reporter)**, pole lokatora, poprawiony parser API.
- **ISS Pass Tracking** — przebudowany w stylu strony WWW (mapa + radar + metryki),
  plan przelotów, przejście na **n2yo visualpasses** (widoczność, czas lokalny,
  flagi kraju), niezawodny odczyt przelotów.

### Nowe funkcje
- **GPS** — obsługa modułu NMEA po UART2 + **automatyczny lokator Maidenhead**.
- **Kolory w TFT** — **kolor znaku** (menu HSV po przytrzymaniu palca + picker WWW,
  zapis w NVS) oraz **kolor zegara** (cyfry zegara, pozycja „ODCIEŃ"/„HUE" w menu).
- **Globalny przełącznik stylu** — KLASYCZNY / nowoczesny dla wszystkich ekranów.
- **Ekran powitalny** — czarne tło, biały wyśrodkowany tekst, godło PZK, większa
  czcionka VLW z polskimi znakami, wydłużony do 20 s.
- **Ekran startowy** — grafika krótkofalarska → karta **QSL** → emblemat
  **ESP32-HAM-CLOCK** + **VERSION 1.2**, poprawiona flaga PL.

### Wygląd (TFT)
- **Wspólny styl „ISS TRACKER"** na wszystkich ekranach: paleta `UI_COL_*` oraz
  funkcje `uiHeader()`, `uiFooter()`, `uiHamburger()` — ciemna belka nagłówka
  z teal linią, hamburger i ikony WiFi/baterii, wspólna stopka. Usunięto
  pomarańczowe belki nagłówków i duplikaty ikon.
- Zegar: nowy nagłówek (czarny pasek, biały napis `ESP32-HAM-CLOCK`), UTC w lewym
  górnym rogu, zegar analogowy + globus + widmo.
- Naprawy: migający biały ekran, wyśrodkowanie Matrix, nagłówek POTA,
  wyśrodkowanie odczytów LAT/LON/ALT/SPEED.

### WWW
- **Upload grafiki (JPG/PNG/BMP)** — bezpośredni upload ekranu startowego
  i powitalnego, auto-konwersja do 480×320 RGB565, naprawa uploadu (surowe ciało
  RAW zamiast multipart).
- **14 pozycji** w UI, kafelki SVG, pasek statusu i metryki ISS (PL i EN).
- Naprawa zakładki „Instrukcja" (`/instrukcja.txt` jako zwykły tekst), usunięty
  przycisk „Postaw kawę".

### WiFi (stabilność)
- Przypinanie do **BSSID AP** (band steering / Smart Connect).
- **Drabinka mocy TX** jako obejście `AUTH_EXPIRE`, diagnostyka łączenia.
- Nie kasuje konfiguracji przy imporcie ustawień z pliku TXT.

### Narzędzia (`tools/`)
- Nowe skrypty: `gen_worldmask.py`, `img_to_bmp565.py`, `make_vlw_font.py`,
  `make_qsl_splash.py`, `render_clock_color_preview.py`, `render_menu_preview.py`,
  `render_welcome_preview.py`; `serve.sh` (setsid — serwer nie pada).

### Nowe pliki danych
- `data/pzk-logo.bmp`, `data/fonts/Welcome-Regular21.vlw`, `iss_worldmask.h`.

---

# ESP32-HAM-CLOCK v1.2a — wydanie poprawkowe (PSK MAP)

Wydanie **v1.2a** (16.10.2026) dla tej samej platformy
(**ESP32-S3 N16R8** + **ILI9488 na 3.5" i 4" 480×320** + XPT2046). Zakres: ekran **PSK MAP**
oraz narzędzia projektu. Pozostałe ekrany i zależności bez zmian.

## Zmiany od v1.2

### Mapa spotów (PSK MAP)
- **Kumulacja spotów** — spoty narastają na mapie zamiast się nadpisywać.
  Nowy `pskAddSpot()` z deduplikacją (znak + pasmo + tryb);
  `PSK_MAX_SPOTS` **50 → 200**.
- **Auto-RESET** po 200 spotach — mapa czyści się sama i startuje nowy cykl.
- **Panel po prawej** (`drawPskSpotListPanel()`) — licznik `SPOTY: N` + lista
  najnowszych; ramka zwężona **310/166 → 340/136 → 360/116** (prawa krawędź
  bez zmian), pole znaku 9 → 8 znaków.
- **Odświeżanie tylko spotów** (`drawPskMapSpotsOnly()`) — nagłówek „PSKReporter
  MAP", pasek, przycisk menu, ikona baterii i napisy nie są przerysowywane
  (koniec migania). Rysowanie przyrostowe przez `lastPskSpotCount`.
- **MQTT domyślnie włączone** (`pskMqttEnabled = true`, NVS `psk_mqtt_en = true`).
- **Przycisk RESET** — `resetPskMapSpots()` zeruje spoty i bufor MQTT.

### Narzędzia i dokumentacja
- `tools/export_conversation.py` — eksport rozmowy do Markdown z redakcją sekretów.
- `conversations/`, `STAN_PRAC.md`, aktualizacje `AGENTS.md`.

### Wersja
- `FIRMWARE_VERSION`: `1.2.1` → **`1.2a`**.

### Kompatybilność
- Bez zmian zależności i układu partycji. Zmienione pliki firmware i ZIP.

## Pliki

| Plik | Opis | Offset |
|---|---|---|
| `merged-flash.bin` | cały obraz 16 MB (bootloader + partycje + firmware + LittleFS) | `0x0` |
| `merged-flash-notouch.bin` | to samo, wariant bez obsługi dotyku | `0x0` |
| `esp32-ham-clock-s3.zip` | paczka projektu (źródła + `data/` + pliki `.bin`) | — |

## Wgrywanie

Przez przeglądarkę: **https://jason2866.github.io/WebSerial_ESPTool/** — wybierz
`merged-flash.bin` (albo `merged-flash-notouch.bin`) i offset `0x0`.

```bash
esptool.py -p COM9 write_flash 0x0 merged-flash.bin
```

Autor: **Tomasz SP9TNV**. Projekt bazowy: **Konrad SP3KON**. Licencja: **MIT**.
