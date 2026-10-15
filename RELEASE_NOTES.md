# ESP32-HAM-CLOCK v1.2 — ESP32-S3 N16R8 + ILI9488 3.5"

Wydanie **v1.2** dla platformy **ESP32-S3 N16R8** (16 MB flash, 8 MB PSRAM)
z wyświetlaczem **ILI9488 3.5" IPS 480×320** po SPI i panelem dotykowym XPT2046.

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

## Pliki

| Plik | Opis | Offset |
|---|---|---|
| `merged-flash.bin` | cały obraz 16 MB (bootloader + partycje + firmware + LittleFS) | `0x0` |
| `merged-flash-notouch.bin` | to samo, wariant bez obsługi dotyku | `0x0` |
| `firmware.bin` | firmware | `0x10000` |
| `littlefs.bin` | dane / UI WWW | `0x810000` |
| `esp32-ham-clock-s3.zip` | paczka projektu (źródła + `data/` + pliki `.bin`) | — |

## Wgrywanie

```bash
# całość naraz
esptool.py -p COM9 write_flash 0x0 merged-flash.bin

# albo osobno
esptool.py -p COM9 write_flash 0x10000 firmware.bin
esptool.py -p COM9 write_flash 0x810000 littlefs.bin
```

Autor: **Tomasz SP9TNV**. Projekt bazowy: **Konrad SP3KON**. Licencja: **MIT**.
