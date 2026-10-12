# ESP32-HAM-CLOCK — wersja dla ESP32-S3 N16R8 + ILI9488 3.5"

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

Korzystasz na własną odpowiedzialność — to konstrukcja hobbystyczna.
