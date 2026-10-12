# ESP32-HAM-CLOCK na ESP32-S3 N16R8 — instrukcja projektu

Kompletny przewodnik po projekcie: od sprzętu, przez build i wgranie na `COM9`, aż po
rozbudowę i publikację artefaktów. Dokument uzupełnia (nie zastępuje) `README_S3.md`
(szczegóły TFT_eSPI/Arduino) i `PINOUT.md` (tabela pinów).

**Sprzęt docelowy:** ESP32-S3-WROOM-1 **N16R8** (16 MB flash QIO + 8 MB PSRAM OPI)
+ zewnętrzny **ILI9488 3.5" IPS 480x320** (SPI) + dotyk **XPT2046** (SPI, wspólna magistrala).

---

## 1. Struktura projektu

```
.
├── main.cpp                 # cały firmware (jeden szkic, ~19 tys. linii)
├── tft_setup.h              # konfiguracja TFT_eSPI: sterownik ILI9488 + piny + dotyk
├── platformio.ini           # 3 środowiska: n16r8, n16r8-notouch, n16r8-altpins
├── partitions.csv           # 2x OTA po 4 MB + ~7,9 MB LittleFS + coredump
├── data/                    # zawartość LittleFS (gra do ESP32)
│   ├── index.html           # panel WWW (PL)
│   ├── indexEN.html         # panel WWW (EN)
│   ├── satellite.js         # logika strony "Śledzenie satelitów — ISS"
│   ├── style.*.css          # arkusze stylów panelu
│   ├── instrukcja.txt       # instrukcja widoczna w zakładce "Instrukcja"
│   ├── flags/*.png          # flagi krajów dla WWW (254 pliki, ~1,1 MB)
│   ├── tft_flags/*.BMP      # flagi 24-bit BMP dla ekranu TFT (254 pliki)
│   ├── fonts/*.vlw          # fonty VLW (polskie znaki)
│   ├── icon50/*.bmp         # ikony pogody
│   └── *.bmp                # splash.bmp, mapa.bmp, "Mapa swiata.bmp"
├── tools/                   # skrypty pomocnicze (konwersje, test dotyku)
├── dist/                    # gotowe obrazy do wgrania
├── download/                # kopie artefaktów do publikacji
└── README_S3.md, PINOUT.md, AGENTS.md
```

W `platformio.ini` ustawiono `src_dir = .` — dlatego `main.cpp` leży w katalogu głównym,
a nie w `src/`.

---

## 2. Sprzęt i połączenia

Pełna tabela i warianty pinów: **`PINOUT.md`**. Skrót (domyślne, działające):

| Funkcja | GPIO | Uwaga |
|---|---|---|
| SPI SCK / CLK | 12 | wspólne z T_CLK |
| SPI MOSI / SDI | 11 | wspólne z T_DIN |
| SPI MISO / SDO | 14 | **wspólne z T_DO** dotyku |
| TFT CS / DC / RST | 10 / 9 / -1 | RST niepodłączony |
| Podświetlenie BL | 21 | PWM |
| Dotyk CS / IRQ | 15 / 16 | |
| RGB LED R/G/B | 4 / 5 / 6 | statusowy (opcjonalny) |
| Bateria ADC | 1 | dzielnik 100k/100k |

Zasady dla N16R8:
- **Nie używać GPIO 26–37** (flash QIO + PSRAM OPI) ani **19/20** (USB), **43/44** (UART0/logi).
- GPIO 0, 3, 45, 46 to piny strapping — unikać lub używać świadomie.

### Moduły opcjonalne
Panel obsługuje dołączane moduły (zakładka **Moduły** w WWW): **GPS** (UART2),
**APRS TNC2 / TNC-KISS**, **ESP32-BRIDGE** (Bluetooth SPP). Domyślnie UART2: RX=IO27,
TX=IO22 — **uwaga:** te piny są zarezerwowane na N16R8 (PSRAM), więc przy N16R8
przenieś UART2 na wolne GPIO (`2, 7, 8, 13, 17, 18, 38–42, 47, 48`).

---

## 3. BUILD — PlatformIO (zalecane)

```bash
# 1) build firmware
pio run -e esp32-s3-n16r8

# 2) build systemu plików (LittleFS z katalogu data/)
pio run -e esp32-s3-n16r8 -t buildfs

# 3) wgranie firmware + plików (COM9)
pio run -e esp32-s3-n16r8 -t upload --upload-port COM9
pio run -e esp32-s3-n16r8 -t uploadfs --upload-port COM9

# 4) podgląd logów (115200)
pio device monitor --port COM9
```

Warianty:
```bash
pio run -e esp32-s3-n16r8-notouch   # dotyk wyłączony (XPT2046 odłączony)
pio run -e esp32-s3-n16r8-altpins   # przykład zmiany pinów bez edycji tft_setup.h
```

> Piny zmieniaj w `tft_setup.h` (bloki `#ifndef`) albo nadpisz w `platformio.ini`
> przez `-DTFT_MISO=... -DTOUCH_CS=...` (nadpisuje plik). Szczegóły: `PINOUT.md`.
> Na hostach bez `pio` w PATH: `PYTHONPATH=/workspace/pylibs PLATFORMIO_CORE_DIR=/workspace/pio_core python3 -m platformio ...`.

### Krytyczna pułapka: wybór `tft_setup.h`
TFT_eSPI 2.5.x robi `__has_include(<tft_setup.h>)` i używa pliku z katalogu szkicu
**tylko gdy katalog projektu jest na ścieżce include biblioteki**. Dlatego
`platformio.ini` zawiera `-I${PROJECT_DIR}`. Bez tego TFT_eSPI cicho bierze domyślny
`User_Setup.h` (ILI9341, piny ESP8266) → crash `StoreProhibited` w `tft.begin()`.

Weryfikacja, że nasz setup jest brany:
```bash
pio run -e esp32-s3-n16r8 -t compiledb
grep -c ILI9488_DRIVER compile_commands.json   # >0 = OK
```

---

## 4. BUILD — Arduino IDE

Warunki, aby zadziałało identycznie jak PlatformIO:
1. `main.cpp`/`.ino` i `tft_setup.h` leżą w **tym samym katalogu szkicu**.
2. Biblioteka **TFT_eSPI 2.5.x** (ma `__has_include`).
3. W `tft_setup.h` **nie** ma `#include <User_Setup.h>` (nasz plik go nie zawiera).

Ustawienia płytki (**Tools**):
- Board: **ESP32S3 Dev Module**
- USB CDC On Boot: **Enabled**
- Flash Size: **16MB (128Mb)**
- PSRAM: **OPI PSRAM**
- Flash Mode: **DIO 80MHz**  ← nie QIO
- Upload Speed: **921600**
- Partition Scheme: własny (`partitions.csv`) lub 16M z OTA

Biblioteki (Library Manager / ręcznie):
**TFT_eSPI**, **XPT2046_Touchscreen**, **ArduinoJson (v6)**, **PubSubClient**,
**Sgp4** — z https://github.com/Hopperpop/Sgp4-Library (Sketch → Include Library → Add .ZIP).

> Wgranie LittleFS w Arduino IDE: użyj `arduino-littlefs-upload` albo gotowego
> `dist/merged-flash.bin` (patrz §6).

---

## 5. Dotyk XPT2046

- Domyślny build `esp32-s3-n16r8` ma `TFT_MISO=14` i `TOUCH_ENABLED=1` — działa od razu.
- Bez podłączonego XPT2046 użyj wariantu `-notouch` (wiszące MISO daje fałszywe dotknięcia,
  które po ~3 s same otwierają menu).
- **T_DO → GPIO 14** (nie 13 — GPIO 13 dawał szum; ustalone przez `tools/touch_test`).
- Odczyt spoczynkowy: `x=4095 y=0 z~9`. Dotknięcie: `x/y` 500–3900, `z` 400–2100.
- Filtr `readRawTouchPoint()` odrzuca `x==0&&y==0` oraz `z>=4000`.

Kalibracja (w `tft_setup.h`/`main.cpp` lub przez `-D`):
```cpp
#define TOUCH_X_MIN 200
#define TOUCH_X_MAX 3800
#define TOUCH_Y_MIN 200
#define TOUCH_Y_MAX 3800
#define TOUCH_SWAP_XY  false
#define TOUCH_INVERT_X false
#define TOUCH_INVERT_Y false
```
Objawy i naprawa: patrz §9 (Rozwiązywanie problemów).

---

## 6. Wgrywanie na COM9 (obrazy z `dist/`)

`dist/` zawiera gotowe obrazy (kopie z `.pio/build/esp32-s3-n16r8/`):

| Plik | Offset |
|---|---|
| `bootloader.bin` | `0x0` |
| `partitions.bin` | `0x8000` |
| `boot_app0.bin` | `0xe000` |
| `firmware.bin` | `0x10000` |
| `littlefs.bin` | `0x810000` |
| `merged-flash.bin` | `0x0` (wszystko scalone, 0xFF0000 B) |
| `merged-flash-notouch.bin` | `0x0` (wariant bez dotyku) |

**Najprościej — jeden plik:**
```bash
esptool.py --chip esp32s3 --port COM9 --baud 921600 write_flash 0x0 merged-flash.bin
```
WebSerial (https://jason2866.github.io/WebSerial_ESPTool/): wybierz `merged-flash.bin`,
offset `0x0`. Obraz zawiera bootloader, partycje, boot_app0, firmware i LittleFS.

Ręcznie (osobne obrazy, Flash Download Tool / esptool):
```bash
esptool.py --chip esp32s3 --port COM9 --baud 921600 write_flash -z \
  0x0 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin \
  0x10000 firmware.bin 0x810000 littlefs.bin
```

> **Tryb flash = DIO.** Wszystkie obrazy są scalane w trybie `dio`. Nie zmieniaj na QIO
> (część modułów S3 nie startuje w QIO → `rst:0x7 TG0WDT_SYS_RST`).

### Scalanie `merged-flash.bin` (po własnym buildzie)
```bash
cd dist
ESPTOOL=/workspace/pio_core/packages/tool-esptoolpy/esptool.py
python3 $ESPTOOL --chip esp32s3 merge_bin -o merged-flash.bin \
  --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin \
  0x10000 firmware.bin 0x810000 littlefs.bin
```

### Publikacja artefaktów (ZIP)
```bash
python3 tools/make_zip.py          # tworzy esp32-ham-clock-s3.zip (~572 wpisy)
```

---

## 7. Pierwsze uruchomienie i panel WWW

1. Bez zapisanej sieci WiFi urządzenie startuje w **trybie AP**:
   SSID `ESP32-HAM-CLOCK`, hasło `1234567890`, adres `http://192.168.4.1`.
2. W zakładce **Ustawienia** wpisz WiFi, znak, lokator, współrzędne (Lat/Lon), klucze API
   (OpenWeather, QRZ, n2yo — opcjonalne), dane APRS-IS/DX Cluster.
3. Zapisz — urządzenie restartuje się i łączy z Twoją siecią. Adres IP zobaczysz na TFT;
   wpisz go w przeglądarce.

Panel WWW (zakładki): DXSPOT, POTA, HAMALERT, APRS, **Satelity (ISS)**, Propagacja, ALERT,
Ekran TFT, Ustawienia, Moduły, Instrukcja, Linki.

---

## 8. Funkcje specyficzne dla tej wersji

### 8.1 Śledzenie ISS (`/api/iss_track`, `satellite.js`)
- Pozycja z `wheretheiss.at`, ground-track z **SGP4** (TLE z Celestrak, odświeżane),
  mapa noc/dzień, radar azymut/elewacja, zasięg 5000 km.
- **Flaga kraju pod ISS:** endpoint zwraca `country` i `country_code`. WWW pokazuje flagę
  `GET /flags/<cc>.png` (254 kraje, lazy-load, `onerror` ukrywa). TFT rysuje flagę
  `GET /flags/<CC>.BMP` obok nazwy kraju.
  - **Flagi TFT muszą być 24-bit BMP** — `drawBmpFromFS()` akceptuje tylko 24 bpp
    (sprawdza nagłówek). Generacja 16-bit RGB565 **nie jest wyświetlana**.
  - Generator: `python3 tools/convert_flags_tft.py` → `data/tft_flags/<CC>.BMP`
    (32x21 px, bottom-up, BGR). Czytaj flagi przez `drawBmpFromFS(path, x, y)` gdzie
    `y` = **dolny** wiersz flagi (funkcja rysuje od dołu do góry).
- **Następny przelot:** liczony dwiema drogami:
  - z **n2yo.com** (jeśli ustawiono `n2yoApiKey`) — gotowe `startUTC/endUTC/maxEl`;
  - lokalnie z **SGP4** (`updateIssNextPassEstimate()`), gdy brak klucza — skan orbity
    w oknie **24 h** z krokiem 60 s, przejścia AOS/LOS wyznaczane bisekcją.
  - Pole/endpoint zwraca `next_pass_epoch`, `next_pass_valid`, `next_pass_aos`,
    `next_pass_los`, `next_pass_max_el`, `iss_visible`.
  - Gdy ISS jest **aktualnie widoczny**, TFT i WWW pokazują koniec bieżącego przelotu
    („do HH:MM"), inaczej najbliższy AOS.
  - **Uwaga:** przy dawnym oknie 6 h najbliższy przelot nad Polską bywa po ~8–12 h,
    więc ekran regularnie pokazywał „brak danych". Okno 24 h to naprawia.
  - Długie pętle SGP4 (1440 iteracji) mają `vTaskDelay(1)` co 15 iteracji — inaczej
    Task Watchdog Timer resetuje urządzenie.

### 8.2 PSK Reporter — mapa (`drawPskMap()`)
- Pozycja użytkownika liczona z `userLatLonValid` **lub z lokatora** (fallback);
  bez tego fallbacku linie DX nie są rysowane (brak czerwonej kropki QTH).
- Menu PSK: pole **Dni** pozwala wybrać zakres **1–30** dni (`psk_report_days`).
- Kolory linii zależą od odległości DX; spoty wg pasma.

### 8.3 Ekran startowy (splash)
Firmware wczytuje grafikę startową z LittleFS w `drawSplashScreen()`, korzystając
z uniwersalnego `drawImageFromFS()`. Obsługiwane formaty:

| Format | Dekoder | Uwagi |
|--------|---------|-------|
| BMP (24-bit RGB888, 16-bit RGB565) | wbudowany `drawBmpFromFS` / `drawBmp16FromFS` | najszybszy, bez alokacji |
| PNG | `pngle` | obsługa kanału alfa (przezroczyste = czarne tło) |
| JPG / JPEG | `TJpg_Decoder` | skala 1/2, 1/4, 1/8 |

Obraz większy niż 480×320 jest automatycznie skalowany (PNG: 2/4/8×, JPG: skala
sprzętowa dekodera), a mniejszy rysowany 1:1. Firmware szuka kolejno plików:
`/splash.png`, `/splash.jpg`, `/splash.jpeg`, `/icon50/splash.bmp`, `/splash.bmp`,
`/splash_16bit.bmp`, `/splash16.bmp`, `/boot.bmp`, `/boot.png`, `/logo.bmp`,
`/logo.png`, `/start.bmp`, `/start.png`.

**Najprościej — przez WWW (bez rekompilacji):**
1. Otwórz stronę urządzenia i zakładkę **LINKI**.
2. W sekcji „Wgraj ekran startowy" wybierz plik PNG / JPG / BMP.
3. Kliknij **Wgraj ekran startowy**. Plik trafia do LittleFS jako `/splash.<ext>`,
   a nowy obraz od razu pokazuje się na TFT (podgląd widoczny też w WWW).

**Przez `data/` + LittleFS (przy buildzie projektu):**
```bash
# masz gotowy BMP 480x320:
cp twoj_splash.bmp data/splash.bmp
pio run -e esp32-s3-n16r8 -t uploadfs   # LittleFS (bez zmiany firmware!)

# masz PNG/JPG - możesz wgrać bezpośrednio (firmware ma dekodery):
cp obraz.png data/splash.png
pio run -e esp32-s3-n16r8 -t uploadfs

# lub przekonwertuj PNG/JPG do 24-bit BMP 480x320:
python3 tools/make_splash.py obraz.png data/splash.bmp
pio run -e esp32-s3-n16r8 -t uploadfs
```

Ekran startowy pokazuje się na kilka sekund i można go pominąć dotknięciem ekranu.

### 8.4 Flagi ISS — ponowna generacja
```bash
python3 tools/convert_flags_tft.py     # data/tft_flags/*.BMP (24-bit)
pio run -e esp32-s3-n16r8 -t buildfs   # przepakuj LittleFS
```

---

## 9. Rozwiązywanie problemów

| Objaw | Przyczyna / rozwiązanie |
|---|---|
| Biały ekran | Zasilanie/BL (GPIO 21); sprawdź SCK/MOSI/CS/DC. |
| Crash `StoreProhibited` w `tft.begin()` | TFT_eSPI bierze zły `User_Setup.h` — dodaj `-I${PROJECT_DIR}` i `tft_setup.h`. |
| `Następny przelot: brak danych` | Najbliższy przelot poza oknem skanu (zwiększ okno) lub brak TLE / brak czasu NTP. Sprawdź log `[ISS PASS]`. |
| Brak flagi pod krajem ISS | Flaga TFT musi być **24-bit BMP** (`/flags/CC.BMP`); WWW używa `/flags/cc.png`. Sprawdź obecność w LittleFS. |
| Menu TFT otwiera się samo | Dotyk włączony bez podłączonego XPT2046 — użyj `-notouch`. |
| Dotyk `x=0 y=0 z=4095` | T_DO/MISO (GPIO 14) nie dochodzi — brak wspólnej GND lub zły styk. |
| Reset przy WiFi/TLS | Za słabe zasilanie; sprawdź PSRAM (OPI). |
| Bootloop `SHA-256 comparison failed` | Wgraj od nowa po `erase_flash`, całym `merged-flash.bin` pod `0x0`. |
| Brak LittleFS | `pio run -t uploadfs`; sprawdź offset partycji `0x810000`. |

---

## 10. Pamięć i wydajność (N16R8)

- `board_build.arduino.memory_type = qio_opi` = quad flash + octal PSRAM.
- `-DBOARD_HAS_PSRAM` włącza 8 MB PSRAM (dużo zapasu na sprite'y TFT).
- Firmware zajmuje ~1,42 MB z 4 MB partycji `app0`; RAM ~26%.
- LittleFS ~7,9 MB — mapy BMP + 254 flagi PNG + 254 flagi BMP + fonty VLW (~4,1 MB danych).

---

## 11. Licencja i autorzy

Autor: **Tomasz SP9TNV** na podstawie projektu **Konrada SP3KON**
(z użyciem AI). Kontakt: SP9TNV@gmail.com. Zobacz też `data/instrukcja.txt`.
