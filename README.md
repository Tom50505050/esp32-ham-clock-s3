# ESP32-HAM-CLOCK v1.2 — ESP32-S3 N16R8 + ILI9488 3.5"

[![PlatformIO](https://img.shields.io/badge/PlatformIO-esp32--s3--n16r8-orange?logo=platformio&logoColor=white)](platformio.ini)
[![ESP32-S3](https://img.shields.io/badge/MCU-ESP32--S3%20N16R8-red?logo=espressif&logoColor=white)](platformio.ini)
[![Display](https://img.shields.io/badge/Display-ILI9488%203.5%22%20%2B%20XPT2046-blue)](README_S3.md)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

Zaawansowany zegar i terminal krótkofalarski na **ESP32-S3-WROOM-1 N16R8**
(16 MB flash, 8 MB PSRAM) z zewnętrznym wyświetlaczem **ILI9488 3.5" IPS 480×320**
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
- `firmware.bin` / `firmware-notouch.bin` — firmware (offset `0x10000`),
- `littlefs.bin` — dane (offset `0x810000`),
- `merged-flash.bin` / `merged-flash-notouch.bin` — cały obraz 16 MB (offset `0x0`),
- `esp32-ham-clock-s3.zip` — paczka projektu (źródła + `data/` + pliki `.bin`).

Wgrywanie całości:
```bash
esptool.py -p COM9 write_flash 0x0 merged-flash.bin
```

---

## Releases

Gotowe, zbudowane obrazy i paczki są dostępne na stronie
**[Releases](https://github.com/Tom50505050/esp32-ham-clock-s3/releases)**.

| Wersja | Data | Najważniejsze zmiany |
|---|---|---|
| **v1.2** | 15.10.2026 | Nowy ekran **APRS RADAR** i **Informacje systemowe** (zamiast Unlis Hunter) · **ISS Pass Tracking** przebudowany (n2yo visualpasses, mapa+radar+metryki, flagi kraju) · **GPS** (NMEA UART2 + automatyczny lokator Maidenhead) · **kolory w TFT**: kolor znaku (menu HSV + picker WWW) i **kolor zegara** · **globalny przełącznik stylu** KLASYCZNY/nowoczesny · **wspólny styl „ISS TRACKER"** na wszystkich ekranach · nowy **ekran startowy** (karta QSL → emblemat ESP32-HAM-CLOCK + VERSION 1.2) i **ekran powitalny** (godło PZK, czcionka VLW) · **WWW**: upload grafiki JPG/PNG/BMP, 14 pozycji, ikony SVG, naprawa „Instrukcja" · **WiFi**: przypinanie do BSSID AP, drabinka mocy TX (AUTH_EXPIRE) |
| **v1.1** | 12.10.2026 | Wersja dla ESP32-S3 N16R8 + ILI9488 · ikony SVG w UI WWW (nawigacja, status, metryki ISS) · naprawa zakładki „Instrukcja” · flaga kraju pod ISS wyrównana na TFT · wyrazista ikona satelity |

---

## Zmiany między v1.1 a v1.2

Poniżej pełne zestawienie zmian wprowadzonych od wydania **v1.1** do **v1.2**
(99 commitów).

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

```bash
# cały obraz (firmware + LittleFS) - offset 0x0
esptool.py -p COM9 write_flash 0x0 merged-flash.bin

# albo osobno
esptool.py -p COM9 write_flash 0x10000 firmware.bin   # firmware
esptool.py -p COM9 write_flash 0x810000 littlefs.bin  # dane WWW
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
