# ESP32-HAM-CLOCK v1 — ESP32-S3 N16R8 + ILI9488 na 3.5" i 4" 480×320

[![PlatformIO](https://img.shields.io/badge/PlatformIO-esp32--s3--n16r8-orange?logo=platformio&logoColor=white)](platformio.ini)
[![ESP32-S3](https://img.shields.io/badge/MCU-ESP32--S3%20N16R8-red?logo=espressif&logoColor=white)](platformio.ini)
[![Display](https://img.shields.io/badge/Display-ILI9488%203.5%22%20%2B%20XPT2046-blue)](README_S3.md)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

Zaawansowany zegar i terminal krótkofalarski na **ESP32-S3-WROOM-1 N16R8**
(16 MB flash, 8 MB PSRAM) z zewnętrznym wyświetlaczem **ILI9488 na 3.5" i 4" 480×320**
po SPI i panelem dotykowym **XPT2046**.

Projekt bazuje na **ESP32-HAM-CLOCK** Konrada Wiśniewskiego SP3KON i został
zaadaptowany oraz rozbudowany przez **Tomasza SP9TNV** (z użyciem AI).

> Port dla ESP32-S3 N16R8 z ILI9488/XPT2046. Obok tego README znajduje się
> `README_S3.md` — instrukcja montażu, połączeń i wgrywania oraz `INSTRUKCJA.md`.

---

## Najważniejsze cechy

- **16 ekranów TFT** przełączanych automatycznie lub ręcznie, z konfigurowalną
  kolejnością i czasem przełączania (HAM CLOCK, DX CLUSTER, APRS-IS, APRS RADAR,
  BAND INFO, SUN SPOTS, WEATHER, WEATHER FORECAST, POTA, HAMALERT, MATRIX,
  LOKALIZACJA, a także **śledzenie przelotu ISS**).
- **Śledzenie ISS** — pozycja liczona lokalnie (SGP4 + TLE), trajektoria naziemna,
  zasięg 5000 km, radar azymut/elewacja, najbliższy przelot, **flaga kraju** pod
  aktualną pozycją.
- **DX Cluster** (telnet), **APRS-IS** + nadawanie beaconu, **POTA**, **HamAlert**,
  **PSK Reporter / mapa**.
- **Pogoda** (OpenWeatherMap) + ostrzeżenia (burze, silny wiatr).
- **WWW GUI w dwóch językach (PL/EN)** — kafelki DXSPOT / POTA / HAMALERT / APRS /
  Satelity / Propagacja / ALERT / Ekran TFT / Ustawienia / Moduły / Instrukcja / Linki,
  z ikonami SVG i responsywnym układem.
- **Dekodery obrazów**: BMP (24/16-bit), **PNG (pngle)**, **JPG (TJpg_Decoder)** —
  pełnoekranowy splash 480×320 oraz przesłanianie plakatu przez WWW.
- **Moduły zewnętrzne**: GPS, APRS TNC2 / TNC-KISS, ESP32-BRIDGE (Bluetooth SPP).
- Konfiguracja **dotyku** (rotacja, zamiana osi, inwersje) i **TFT** (rotacja,
  jasność, nocne ściemnianie) online, bez rekompilacji.
- Backlight PWM, opcjonalna dioda RGB, pomiar napięcia baterii.

---

## Sprzęt i połączenia (skrót)

| Funkcja | GPIO (ESP32-S3) | Pin modułu TFT |
|---|---|---|
| SPI SCK | 12 | SCK |
| SPI MOSI (SDI) | 11 | SDI / MOSI |
| SPI MISO (SDO) | 14 | SDO / MISO — wspólne z T_DO |
| TFT CS | 10 | CS |
| TFT DC (RS) | 9 | DC / RS |
| TFT RST | -1 | RESET → 3.3 V przez 10k |
| Podświetlenie BL | 21 | LED (PWM) |
| Dotyk CS | 15 | T_CS |
| Dotyk IRQ | 16 | T_IRQ (opcjonalnie) |
| Dotyk DIN / DO / CLK | 11 / 14 / 12 | wspólne z SPI |
| RGB LED R/G/B | 4 / 5 / 6 | statusowy LED (opcjonalnie) |
| Bateria ADC | 1 | dzielnik 100k/100k |

- **Nie używaj GPIO 26–37** — zajęte przez flash (QIO) i PSRAM (OPI).
- **GPIO 19/20** to natywne USB D-/D+ (nie kolidują z TFT).
- Pełny opis, ostrzeżenia i tabela pinów: **`README_S3.md`**.

---

## Budowanie i wgrywanie

### PlatformIO (zalecane)

```bash
pio run -e esp32-s3-n16r8                 # build
pio run -e esp32-s3-n16r8 -t upload       # firmware
pio run -e esp32-s3-n16r8 -t uploadfs     # dane (LittleFS: index.html, splash.bmp, mapa.bmp)
pio device monitor                        # logi
```

Środowisko **`esp32-s3-n16r8-notouch`** buduje wariant bez obsługi dotyku.

### Arduino IDE
Patrz sekcja 3 w **`README_S3.md`** (płytka „ESP32S3 Dev Module", PSRAM OPI, itd.).

### Gotowe obrazy
W `dist/` oraz `download/` znajdują się zbudowane artefakty:
- `merged-flash.bin` / `merged-flash-notouch.bin` — cały obraz 16 MB (offset `0x0`),
- `esp32-ham-clock-s3.zip` — paczka projektu (źródła + `data/` + pliki `.bin`).

Wgrywanie przez przeglądarkę (WebSerial):
**https://jason2866.github.io/WebSerial_ESPTool/** — wybierz `merged-flash.bin`
(płytka z dotykiem) albo `merged-flash-notouch.bin` (bez dotyku), offset `0x0`.

```bash
esptool.py -p COM9 write_flash 0x0 merged-flash.bin
```

> W wydaniach dostępne są **tylko** `merged-flash.bin` i `merged-flash-notouch.bin`
> (osobne `firmware.bin`, `littlefs.bin`, `bootloader.bin`, `boot_app0.bin`
> i `partitions.bin` zostały usunięte).

---

## Releases

Gotowe, zbudowane obrazy i paczki są dostępne na stronie
**[Releases](https://github.com/Tom50505050/esp32-ham-clock-s3/releases)**.

| Wersja | Data | Najważniejsze zmiany |
|---|---|---|
| **v1.2c** | 18.10.2026 | **Naprawa PSK MQTT**: zmiana pasma/trybu nie ściągała spotów (filtr subskrypcji 6-segmentowy → `.../<pasmo>/<tryb>/#`) · mapowanie pasm **160/30/17/12 m** (HTTP i MQTT) · poprawka indeksu odbiorcy (5→6) · czyszczenie spotów po zapisie filtra · **ikona baterii 12 px w lewo** na wszystkich ekranach · synchronizacja `tools/render_header_preview.py` |
| **v1.2b** | 17.10.2026 | **Nagłówek**: ikona **GPS po lewej** stronie (za godziną) · ikona **baterii na wszystkich ekranach** · wspólny `drawHeaderTime()` · Nowe ekrany: **STACJA** (15.) i **ESP32 HAM LOGGER** (16., dziennik ADIF: formularz QSO, klawiatura, tabela ostatnich łączności, SAVE QSO/CLEAR/EXPORT ADIF/SETTINGS, log w **PSRAM** + zapis **plik tymczasowy + rename**, autouzupełnianie NAME/QTH z **QRZ.com → HamDB → callook**, eksport **`/qso.adi`**, endpoint **`/api/qrz_upload`**) · STACJA: wyszukiwanie znaku w **QRZ.com → HamDB → callook.info**, dwa transceivery, dystans + kierunek geograficzny · podglądy 1:1 `tools/render_station_preview.py`, `render_logger_preview.py` |
| **v1.2a** | 16.10.2026 | Poprawki ekranu **PSK MAP**: **kumulacja spotów** (dedup znak+pasmo+tryb, limit 200) · **auto-RESET** po 200 spotach · **panel informacyjny po prawej** (licznik + lista najnowszych, zwężona ramka) · **odświeżanie tylko spotów** (bez przerysowywania nagłówka, paska, ikony baterii i napisów) · **MQTT domyślnie włączone** (start bez HTTP) · eksport rozmów do Markdown |
| **v1.2** | 15.10.2026 | Nowy ekran **APRS RADAR** i **Informacje systemowe** (zamiast Unlis Hunter) · **ISS Pass Tracking** przebudowany (n2yo visualpasses, mapa+radar+metryki, flagi kraju) · **GPS** (NMEA UART2 + automatyczny lokator Maidenhead) · **kolory w TFT**: kolor znaku (menu HSV + picker WWW) i **kolor zegara** · **globalny przełącznik stylu** KLASYCZNY/nowoczesny · **wspólny styl „ISS TRACKER"** na wszystkich ekranach · nowy **ekran startowy** (karta QSL → emblemat ESP32-HAM-CLOCK + VERSION 1.2) i **ekran powitalny** (godło PZK, czcionka VLW) · **WWW**: upload grafiki JPG/PNG/BMP, 14 pozycji, ikony SVG, naprawa „Instrukcja" · **WiFi**: przypinanie do BSSID AP, drabinka mocy TX (AUTH_EXPIRE) |
| **v1** | 12.10.2026 | Wersja dla ESP32-S3 N16R8 + ILI9488 · ikony SVG w UI WWW (nawigacja, status, metryki ISS) · naprawa zakładki „Instrukcja” · flaga kraju pod ISS wyrównana na TFT · wyrazista ikona satelity |


---

## Zmiany między v1.2b a v1.2c

Wydanie **v1.2c** (18.10.2026) to wydanie poprawkowe: **naprawa PSK MQTT**
(brak spotów po zmianie pasma/trybu) oraz **przesunięcie ikony baterii** w lewo
na wszystkich ekranach. Pozostałe ekrany i zależności bez zmian.

### Naprawa: PSK MQTT — zmiana pasma nie ściągała spotów
- Realny temat brokera PSK Reporter ma **11 segmentów**, a subskrypcja budowała
  filtr **6-segmentowy** (`.../<pasmo>/<tryb>/+/+/+`), który **nie pasował do
  żadnego tematu** — po wybraniu pasma mapa zostawała pusta. Subskrypcja zmieniona
  na **`.../<pasmo>/<tryb>/#`**.
- Mapowanie pasm **160 / 30 / 17 / 12 m** w obu ścieżkach (HTTP i MQTT).
- Poprawka filtra odbiorcy: indeks **5** (nadawca) → **6** (odbiorca).
- Po zapisie filtra **czyszczone są spoty** i wymuszane przerysowanie.
- Resubskrypcja **odsubskrybowuje stary temat** — koniec spotów z dwóch pasm.

### Nagłówek — ikona baterii przesunięta w lewo
- Ikona baterii **12 px w lewo**: zegar `drawBatteryIcon(screenW-70, 8)` →
  **`screenW-82, 8`**, wspólny nagłówek `uiHeader` `420, 1` → **`408, 1`**.
- `rightLimit` nagłówka **415 → 403**.
- `drawBatteryQuickUpdate()` — pozycje lustrzane w tej samej geometrii.

### Narzędzia (`tools/`)
- `render_header_preview.py` zsynchronizowany z firmware (**398 / 408 /
  `screenW-82`**); usunięty nieistniejący tekst „%”.

### Wersja
- `FIRMWARE_VERSION`: `1.2b` → **`1.2c`**. Zaktualizowane też `data/index.html`,
  `data/indexEN.html` (LittleFS) i `tools/make_qsl_splash.py`.

---

## Zmiany między v1.2a a v1.2b

Wydanie **v1.2b** (17.10.2026) łączy dwa zakresy: poprawki **nagłówka**
(ikona GPS po lewej, ikona baterii na wszystkich ekranach) oraz **nowe ekrany
STACJA (15.) i ESP32 HAM LOGGER (16.)**. Pozostałe ekrany i zależności bez zmian.

### Nagłówek — ikona GPS i ikona baterii
- **Ikona GPS po lewej stronie nagłówka** — `GPS_ICON_X` przeniesione z `372` na
  **`88`** (obszar dotyku `gpsIconHitX` z `364` na `82`). Ikona leży zaraz za
  godziną HH:MM, przed logo; dotknięcie nadal otwiera ekran satelitów.
- **Wspólny `drawHeaderTime(fg, bg)`** dla obu stylów nagłówka zegara.
- **Ikona baterii na wszystkich ekranach** — wspólny `uiHeader()` rysuje teraz
  ikonę baterii; `drawBatteryQuickUpdate()` rysuje ją w tej samej geometrii co
  `drawBatteryIcon` i z właściwym tłem nagłówka. `loop()` woła
  `drawBatteryQuickUpdate(true)`, więc poziom odświeża się też poza zegarem.

### Nowy ekran STACJA (15. ekran)
- **Wyszukiwanie znaku** — dane pobierane kolejno ze źródeł
  **QRZ.com → HamDB → callook.info** (pierwsze trafienie wygrywa; nazwa źródła
  pokazywana jest na ekranie).
- **Dwa transceivery** — lewy **czerwony** = Twój znak (operator), prawy
  **zielony** = szukany znak. Napis znaku jest **czarny na tle w kolorze radia**,
  a korpusy radia zachowują swoje kolory.
- **Karta wyniku** — pola **Imię / QTH / Grid / Kraj** oraz linia
  **dystans + kierunek geograficzny** (np. `42 km  NE`), a pod nimi **źródło
  danych i godzina pobrania** (HH:MM, czas lokalny).
- **Kierunek geograficzny skrótami** — `N, NE, E, SE, S, SW, W, NW` (8 azymutów,
  po 45°), liczony z lokatora/QTH względem Twojej pozycji.
- **Linie fal** między antenami, kolorowane wg odległości (`propPolarityColor()`).
- **Stany ekranu**: start, klawiatura ekranowa, pobieranie, wynik oraz
  „Nie znaleziono".
- **Styl klasyczny i nowoczesny** — respektuje `uiStyleClassic()`.

### Nowy ekran ESP32 HAM LOGGER (16. ekran)
- **Formularz bieżącego QSO** — pola: **CALLSIGN** (w wyróżnionej żółtej ramce),
  **RST (S) / RST (R)**, **BAND / FREQ / MODE**, **NAME / QTH** oraz
  **DATE/TIME** (UTC z NTP, `GMT_OFFSET_SEC = 0`).
- **Klawiatura ekranowa** — wspólna dla wszystkich pól; po zamknięciu pola
  CALLSIGN następuje **autouzupełnianie NAME/QTH** z tego samego łańcucha źródeł
  co ekran STACJA (**QRZ.com → HamDB → callook.info**).
- **BAND / MODE** — dotknięcie cyklicznie przełącza wartość; zmiana pasma
  podstawia domyślną częstotliwość (np. `40m` → `7.145`).
- **Tabela „Ostatnie łączności"** — 3 najnowsze wpisy (CALL / FREQ / MODE / UTC).
- **Dolny panel przycisków dotykowych**: **SAVE QSO**, **CLEAR**,
  **EXPORT ADIF**, **SETTINGS** (SETTINGS otwiera wspólne menu jasności/tematu).
- **Log w PSRAM + bezpieczny zapis** — rekordy w **PSRAM** (8 MB), zapis dopiero
  na SAVE QSO przez **plik tymczasowy + rename** (`/qso.adi.tmp` → `/qso.adi`),
  więc **utrata zasilania nie uszkadza bazy**; **ADIF** budowany bajtowo poprawnie
  (długości pól liczone w **bajtach UTF-8**, np. `Knurów` = 8 bajtów).

### Integracja z UI WWW
- Nowe pozycje **STACJA** (`station`) i **LOGGER** (`logger`) na liście ekranów
  (`data/index.html`, `data/indexEN.html`) — lista stron z 14 do **16**.
- **Eksport ADIF**: endpoint **`GET /qso.adi`** (pobranie pliku).
- **Wysyłka do QRZ Logbook**: endpoint **`POST /api/qrz_upload`** oraz pole
  **QRZ.com API Key (Logbook)** w ustawieniach WWW (gotowe na przyszłość).

### Narzędzia (`tools/`)
- `tools/render_station_preview.py`, `tools/render_logger_preview.py`,
  `tools/render_pskmap_preview.py`, `tools/render_header_preview.py` — podglądy 1:1.

### Wersja
- `FIRMWARE_VERSION`: `1.2a` → **`1.2b`**.

### Kompatybilność
- Bez zmian zależności (TFT_eSPI, ArduinoJson, PubSubClient, …) i układu partycji.
- Zmienione pliki firmware i ZIP (nowy build).

---

## Zmiany między v1.2a a v1.2b

Wydanie **v1.2b** (16.10.2026) dodaje **nowy ekran STACJA** (15. ekran) oraz
narzędzia podglądu. Pozostałe ekrany i zależności bez zmian.

### Nowy ekran STACJA (15. ekran)
- **Wyszukiwanie znaku** — po wpisaniu znaku (klawiatura ekranowa) dane pobierane
  są kolejno ze źródeł **QRZ.com → HamDB → callook.info** (pierwsze trafienie
  wygrywa; nazwa źródła pokazywana jest na ekranie).
- **Dwa transceivery** — lewy **czerwony** = Twój znak (operator), prawy
  **zielony** = szukany znak. Napis znaku jest **czarny na tle w kolorze radia**,
  a korpusy radia zachowują swoje kolory.
- **Karta wyniku** — pola **Imię / QTH / Grid / Kraj** oraz linia
  **dystans + kierunek geograficzny** (np. `42 km  NE`), a pod nimi **źródło
  danych i godzina pobrania** (HH:MM, czas lokalny).
- **Kierunek geograficzny skrótami** — `N, NE, E, SE, S, SW, W, NW` (8 azymutów,
  po 45°), liczony z lokatora/QTH względem Twojej pozycji.
- **Linie fal** między antenami, kolorowane wg odległości (funkcja
  `propPolarityColor()`), czyli wizualna „propagacja" między stacjami.
- **Stany ekranu**: start („Podaj znak stacji"), klawiatura ekranowa, pobieranie
  („Pobieranie danych…", kolejność źródeł), wynik oraz „Nie znaleziono".
- **Styl klasyczny i nowoczesny** — ekran respektuje globalny przełącznik stylu
  (`uiStyleClassic()`); układ i dane są wspólne dla obu stylów.

### Integracja z UI WWW
- Nowa pozycja **STACJA** na liście ekranów (`data/index.html`,
  `data/indexEN.html`) — kod `station`, lista stron rozszerzona z 14 do **15**.

### Narzędzia (`tools/`)
- `tools/render_station_preview.py` — podgląd ekranu STACJA 1:1 (Pillow) dla
  stanów: start, klawiatura, pobieranie, wynik, wariant klasyczny, brak wyniku.
- `tools/render_pskmap_preview.py` — podgląd ekranu PSK MAP.

### Wersja
- `FIRMWARE_VERSION`: `1.2a` → **`1.2b`**.

### Kompatybilność
- Bez zmian zależności (TFT_eSPI, ArduinoJson, PubSubClient, …) i układu partycji.
- Zmienione pliki firmware i ZIP (nowy build).

---

## Zmiany między v1.2 a v1.2a

Wydanie poprawkowe **v1.2a** (16.10.2026) dotyczy wyłącznie ekranu **PSK MAP**
i narzędzi projektu. Nie zmieniono innych ekranów ani zależności.

### Mapa spotów (PSK MAP)
- **Kumulacja spotów** — spoty już się nie nadpisują. Nowy helper `pskAddSpot()`
  dodaje spoty z **deduplikacją** (klucz: znak + pasmo + tryb). Limit podniesiono
  z `PSK_MAX_SPOTS = 50` do **200**.
- **Auto-RESET po 200 spotach** — po osiągnięciu limitu mapa sama się czyści
  i startuje nowy cykl (w logu: `[PSK] AUTO-RESET: osiagnieto 200 spotow`).
  Wcześniej `break` w pętli HTTP blokował to wyzwolenie.
- **Panel informacyjny po prawej** (`drawPskSpotListPanel()`) — licznik
  `SPOTY: N` + lista najnowszych (znak, pasmo, tryb) z kwadracikiem koloru pasma.
  Ramkę **zwężono dwukrotnie**: 310/166 → 340/136 → **360/116** (prawa krawędź
  bez zmian), żeby zasłaniała mniej mapy. Pole znaku skrócono z 9 do 8 znaków.
- **Odświeżanie tylko spotów** (`drawPskMapSpotsOnly()`) — przy nowych spotach
  rysowane są **wyłącznie spoty** + panel. Nagłówek „PSKReporter MAP", pasek,
  przycisk menu, ikona baterii i napisy **pozostają nietknięte** (koniec migania).
  Zmienna `lastPskSpotCount` śledzi, co już narysowano (rysowanie przyrostowe).
- **MQTT domyślnie włączone** — `pskMqttEnabled = true` oraz NVS
  `psk_mqtt_en = true`; start od razu po MQTT, bez pierwszego zapytania HTTP.
- **Przycisk RESET** — `resetPskMapSpots()` zeruje `pskSpotCount`,
  `lastPskSpotCount` i bufor MQTT, po czym wymusza pełne przerysowanie mapy.
- `fetchPskReporterData()` i `copyPskMqttToDisplay()` **dopisują** spoty
  (usunięto zerowanie licznika i przerywanie pętli na limicie).

### Narzędzia i dokumentacja
- **`tools/export_conversation.py`** — eksport dziennika zdarzeń rozmowy do
  Markdown; tryb pełny i `--dialog` (tylko wypowiedzi), **redakcja sekretów**
  (`gAAAAA*`, `ghp_*`, `github_pat_*`, `sk-*`, `api_key=…`, `Bearer …`).
- **`conversations/`** — skrócony zapis rozmowy (`*-dialog.md`); pełna wersja
  w `.gitignore`.
- **`STAN_PRAC.md`** — notatka o stanie prac i punktach do weryfikacji.
- **`AGENTS.md`** — opis MQTT domyślnego, kumulacji spotów, panelu bocznego
  i procedury zapisu rozmów.

### Wersja
- `FIRMWARE_VERSION` zmieniona z `1.2.1` na **`1.2a`**.

### Uwaga o kompatybilności
- Brak zmian w zależnościach (TFT_eSPI, ArduinoJson, PubSubClient, …).
- Pliki firmware i ZIP zmienione (nowy build); układ partycji bez zmian.

---

## Zmiany między v1 a v1.2

Poniżej pełne zestawienie zmian wprowadzonych od wydania **v1** do **v1.2**
(100 commitów).

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
- **Kolory w TFT**:
  - **Kolor znaku** — menu HSV (przytrzymanie palca na znaku) + color-picker WWW,
    skalowanie HSV, zapis w NVS, API.
  - **Kolor zegara** (cyfry zegara) — pozycja w menu „ODCIEŃ"/„HUE", picker WWW.
- **Globalny przełącznik stylu** — KLASYCZNY / nowoczesny dla wszystkich ekranów.
- **Ekran powitalny** — czarne tło, biały wyśrodkowany tekst, godło PZK, większa
  czcionka VLW z polskimi znakami, wydłużony do 20 s.
- **Ekran startowy** — grafika krótkofalarska → karta **QSL** → emblemat
  **ESP32-HAM-CLOCK** + **VERSION 1.2**, poprawiona flaga PL.

### Wygląd (TFT)
- **Wspólny styl „ISS TRACKER"** na wszystkich ekranach: paleta `UI_COL_*` oraz
  funkcje `uiHeader()`, `uiFooter()`, `uiHamburger()` — ciemna belka nagłówka
  z teal linią, hamburger i ikony WiFi/baterii, wspólna stopka. Usunięto
  pomarańczowe belki i duplikaty ikon.
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

### Wgrywanie z wydania

Przez przeglądarkę (WebSerial): **https://jason2866.github.io/WebSerial_ESPTool/** —
wybierz `merged-flash.bin` (płytka z dotykiem) albo `merged-flash-notouch.bin`
(bez dotyku), offset `0x0`.

```bash
# cały obraz (firmware + LittleFS) - offset 0x0
esptool.py -p COM9 write_flash 0x0 merged-flash.bin
```

W plikach `flash.bat` / `flash.sh` znajdują się gotowe skrypty wgrywania.

### Podpisane wydanie (GPG)

Tag **v1.2** jest podpisany kluczem GPG wydania. Klucz publiczny znajduje się
w repozytorium: `esp32-ham-clock-release-key.asc`.

Weryfikacja:

```bash
gpg --import esp32-ham-clock-release-key.asc
git tag -v v1.2        # Good signature from "Tomasz SP9TNV ..."
```

---

## Struktura repozytorium

```
main.cpp                 # firmware (TFT, ekrany, WS, logika)
tft_setup.h              # konfiguracja TFT_eSPI dla ILI9488
platformio.ini           # środowiska: esp32-s3-n16r8 (+ -notouch)
partitions.csv           # 16 MB flash: app + LittleFS
data/                    # pliki serwowane z LittleFS (UI WWW, czcionki .vlw, bitmapy)
tools/                   # skrypty pomocnicze (konwersja flag, splash, ZIP)
dist/, download/         # zbudowane artefakty (.bin, .zip)
README_S3.md             # instrukcja S3/ILI9488
INSTRUKCJA.md            # instrukcja obsługi
```

---

## Podziękowania / licencja

- Projekt bazowy: **Konrad Wiśniewski SP3KON** — ESP32-HAM-CLOCK.
- Adaptacja i rozwój: **Tomasz SP9TNV**.
- Kontakt: SP9TNV@gmail.com

Projekt udostępniany na licencji **MIT** — szczegóły w pliku [`LICENSE`](LICENSE).

Korzystasz na własną odpowiedzialność — to konstrukcja hobbystyczna.
