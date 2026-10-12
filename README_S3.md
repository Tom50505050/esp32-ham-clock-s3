# ESP32-HAM-CLOCK na ESP32-S3 N16R8 + ILI9488 3.5" IPS

Instrukcja uruchomienia firmware na **ESP32-S3-WROOM-1 N16R8** (16 MB flash, 8 MB PSRAM)
z zewnętrznym wyświetlaczem **ILI9488 3.5" 480x320 IPS** po SPI i dotykiem **XPT2046**.

## 1. Tabela połączeń

| Funkcja | GPIO (ESP32-S3) | Pin modułu TFT |
|---|---|---|
| SPI SCK | 12 | SCK / SCL |
| SPI MOSI (SDI) | 11 | SDI / MOSI |
| SPI MISO (SDO) | **14** | SDO / MISO — wspólne z T_DO (tylko dotyk) |
| TFT CS | 10 | CS |
| TFT DC (RS) | 9 | DC / RS |
| TFT RST | -1 (niepodłączony) | RESET → do 3.3V przez 10k, albo zostaw |
| Podświetlenie BL | 21 | LED (PWM) |
| Dotyk CS | 15 | T_CS |
| Dotyk IRQ | 16 | T_IRQ (opcjonalnie) |
| Dotyk DIN | 11 | T_DIN (wspólne z MOSI) |
| Dotyk DO | **14** | T_DO (wspólne z MISO) |
| Dotyk CLK | 12 | T_CLK (wspólne ze SCK) |
| RGB LED R/G/B | 4 / 5 / 6 | statusowy LED (opcjonalnie) |
| Bateria ADC | 1 | dzielnik 100k/100k |

**Zasilanie:** VCC modułu → 3.3V (lub 5V, jeśli moduł ma własny regulator), GND wspólne.

### Ważne uwagi sprzętowe (ESP32-S3 N16R8)
- **Nie używaj GPIO 26–37** — są zajęte przez flash (QIO) i PSRAM (OPI). Piny SPI/dotyku
  powyżej są dobrane tak, aby tego uniknąć.
- **GPIO 19/20** to USB D-/D+ (natywne USB). Piny TFT/dotyku nie kolidują z USB.
- Piny powyżej to **domyślne piny FSPI dla ESP32-S3** (SCK 12, MOSI 11, MISO 13, SS 10),
  dzięki czemu obiekt `SPI` działa bez remapu.
- ILI9488 ma pułapkę: **SDO (MISO) nie przechodzi w tristate przy CS=high**. Jeśli na tej
  samej magistrali podłączasz coś jeszcze (np. slot SD), spodziewaj się konfliktów —
  najbezpieczniej nie podłączać MISO, jeśli nie jest potrzebne.

## 2. PlatformIO (zalecane)

Wszystko jest już przygotowane: `platformio.ini`, `partitions.csv`, `tft_setup.h`.

```bash
# build
pio run -e esp32-s3-n16r8

# wgranie firmware
pio run -e esp32-s3-n16r8 -t upload

# wgranie plików LittleFS (splash.bmp, mapa.bmp, index.html) - jeśli używasz
pio run -e esp32-s3-n16r8 -t uploadfs

# podgląd logów
pio device monitor
```

> Projekt trzyma `main.cpp` i `tft_setup.h` w katalogu głównym — dlatego w `platformio.ini`
> ustawiono `src_dir = .`.

### Jak TFT_eSPI wybiera `tft_setup.h` (ważne!)
TFT_eSPI 2.5.x **nie zna** `-DTFT_ESPI_USER_SETUP_PATH`. Zamiast tego `TFT_eSPI.h` robi
`__has_include(<tft_setup.h>)` i używa pliku z katalogu szkicu — ale **tylko gdy katalog
projektu jest na ścieżce include biblioteki**. Dlatego `platformio.ini` ma `-I${PROJECT_DIR}`.

Bez tego wpisu biblioteka cicho spada na domyślny `User_Setup.h` (sterownik **ILI9341**,
piny ESP8266 `PIN_D6`) i firmware kończy się crashem `StoreProhibited` w `tft.begin()`.
Objaw: `TFT begin()...` → `Guru Meditation Error ... EXCVADDR: 0x00000010`.

Szybka weryfikacja, że działa (powinno pokazać `ILI9488_DRIVER` i `TFT_MOSI 11`):
```bash
pio run -e esp32-s3-n16r8 -t compiledb
grep -c ILI9488_DRIVER compile_commands.json   # >0 gdy nasz setup jest brany
```

### Pamięć / PSRAM
- `board_build.arduino.memory_type = qio_opi` = quad flash + octal PSRAM (N16R8).
- `-DBOARD_HAS_PSRAM` włącza PSRAM. Jeśli chcesz przenieść duże sprite'y TFT_eSPI do PSRAM,
  w `main.cpp` przy tworzeniu sprite'a użyj `sprite.createSprite(...)` z wcześniejszym
  `heap_caps_malloc`/`ps_malloc` albo wariantu `createSprite` z PSRAM (zależnie od wersji
  TFT_eSPI). Przy 8 MB PSRAM masz dużo zapasu.

## 3. Arduino IDE

Arduino IDE wspiera ten sam mechanizm co PlatformIO: TFT_eSPI szuka `tft_setup.h`
**w katalogu szkicu**. Jeśli więc obok `.ino` leży nasz `tft_setup.h`, biblioteka użyje
go automatycznie (przykład „Sketch_with_tft_setup"). Trzy warunki:

1. Plik `main.cpp`/`.ino` i `tft_setup.h` leżą w tym samym katalogu szkicu.
2. Biblioteka TFT_eSPI jest w wersji 2.5.x (ma `__has_include`).
3. W `tft_setup.h` **nie** może być `#include <User_Setup.h>` (nasz plik go nie ma).

Jeśli mimo to chcesz zmienić konfigurację biblioteki na stałe, edytuj `User_Setup.h`:

1. Znajdź plik `User_Setup.h` w bibliotece TFT_eSPI
   (`~/Arduino/libraries/TFT_eSPI/User_Setup.h` lub w `Documents/Arduino/libraries/...`).
2. Ustaw w nim:

```cpp
#define ILI9488_DRIVER

#define TFT_MISO 13
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS   10
#define TFT_DC    9
#define TFT_RST  -1
#define TFT_BL   21

#define TFT_INVERSION_ON
#define TFT_RGB_ORDER TFT_BGR

#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY 2500000

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define SMOOTH_FONT
```

3. W `User_Setup_Select.h` zostaw aktywną tylko linię `#include <User_Setup.h>`.

### Ustawienia płytki w Arduino IDE
- **Board:** `ESP32S3 Dev Module`
- **USB CDC On Boot:** `Enabled`
- **Flash Size:** `16MB (128Mb)`
- **Partition Scheme:** `16M Flash (3MB APP/9.9MB FATFS)` albo własny wariant;
  dla OTA/LittleFS najlepiej wgrać `partitions.csv` (jak w PlatformIO).
- **PSRAM:** `OPI PSRAM`
- **Flash Mode:** `DIO 80MHz`  ← nie QIO (część modułów S3 nie startuje w QIO)
- **Upload Speed:** `921600`

> Biblioteki do doinstalowania (Library Manager): **TFT_eSPI**, **XPT2046_Touchscreen**,
> **ArduinoJson (v6)**, **PubSubClient**. **Sgp4** zainstaluj ręcznie z
> https://github.com/Hopperpop/Sgp4-Library (Arduino: Sketch → Include Library → Add .ZIP).

## 4. Kalibracja dotyku

Wartości startowe są w `main.cpp`:

```cpp
#define TOUCH_X_MIN 200
#define TOUCH_X_MAX 3800
#define TOUCH_Y_MIN 200
#define TOUCH_Y_MAX 3800
```

## 4a. Dotyk — włączanie/wyłączanie

Dopóki XPT2046 **nie jest podłączony**, dotyk musi być wyłączony. Inaczej wiszący
pin MISO (GPIO 13) daje losowe odczyty `z >= Z_THRESHOLD`, firmware uznaje to za
"przytrzymanie" i po ~3 s otwiera menu ustawień TFT (a po 5 s kalibrację).

Przełącznik w `tft_setup.h`:

```cpp
#define TOUCH_ENABLED 0   // 0 = brak dotyku (domyślnie), 1 = XPT2046 podłączony
```

albo bez edycji pliku, w `platformio.ini` → `build_flags`:

```ini
-DTOUCH_ENABLED=1
```

Po podłączeniu XPT2046 ustaw `1` i (jeśli trzeba) skalibruj jak w punkcie 4.

Jeśli dotyk jest przesunięty lub odwrócony:
- **odwrócona oś X/Y:** zmień `TOUCH_INVERT_X` / `TOUCH_INVERT_Y` na `true`,
- **zamienione osie:** `TOUCH_SWAP_XY` na `true`,
- **skalowanie:** dostrój `TOUCH_X_MIN/MAX`, `TOUCH_Y_MIN/MAX`.

Kalibrację można też wykonać z poziomu menu na ekranie urządzenia (funkcja kalibracji)
— wartości zapisują się w NVS.

## 5. Rozwiązywanie problemów

| Objaw | Przyczyna / rozwiązanie |
|---|---|
| Biały ekran | Sprawdź zasilanie i podświetlenie (BL na GPIO 21). Sprawdź SCK/MOSI/CS/DC. |
| Odwrócone kolory | Panel wymaga inwersji — upewnij się, że `TFT_INVERSION_ON` jest ustawione. Można przełączyć w panelu WWW. |
| Czerwony/niebieski zamienione | Zmień `TFT_RGB_ORDER` z `TFT_BGR` na `TFT_RGB`. |
| Śnieg / artefakty | Zmniejsz `SPI_FREQUENCY` do 27 MHz lub 20 MHz. Skróć przewody. |
| Dotyk nie działa | Sprawdź T_CS (15), T_CLK (12), T_DIN (11), T_DO (13). Sprawdź kalibrację. |
| Dotyk: stale `x=0 y=0 z=4095` | MISO (T_DO, GPIO 13) nie dochodzi do kontrolera — zwykle **brak wspólnej masy (GND)** między XPT2046 a ESP32 albo zły styk T_DO. Zmierz napięcie na VCC XPT2046 (ma być 3,3 V). |
| Menu ustawień TFT otwiera się samo | Dotyk jest włączony, ale XPT2046 niepodłączony (wiszące MISO daje fałszywe dotknięcia). Ustaw `TOUCH_ENABLED 0` w `tft_setup.h`. |
| Brak LittleFS | Wgraj system plików (`pio run -t uploadfs`) i sprawdź `partitions.csv`. |
| Reset przy WiFi/TLS | Zasilanie/USB za słabe; doładuj kondensator; sprawdź PSRAM (OPI). |
| Bootloop `SHA-256 comparison failed` | Obraz aplikacji we flashu ≠ plik. Wgraj od nowa, po `erase_flash`, całym scalonym obrazem: `write_flash --flash_size 16MB 0x0 merged-flash.bin`. Nie wgrywaj samego `firmware.bin` pod zły offset. |

## 5a. Dotyk XPT2046 — podłączenie i warianty
**T_DO (MISO) jest podłączone do GPIO 14** (nie 13). Ustalone testem sprzętowym
(`tools/touch_test`, v4): GPIO 14 zwracał płynne wartości X/Y (467–4095) przy
ruchu palca, a GPIO 13 tylko szum 0/8191. GPIO 13 nie jest używane.
```
T_CLK -> GPIO 12    T_DIN -> GPIO 11    T_DO -> GPIO 14
T_CS  -> GPIO 15    T_IRQ -> GPIO 16    VCC 3,3 V, GND wspólna z ESP32
```
Domyślny build (`esp32-s3-n16r8`) ma `TFT_MISO=14` i `TOUCH_ENABLED=1` — dotyk
działa od razu.
Wariant bez dotyku (gdy XPT2046 odłączony — wiszące MISO daje fałszywe dotknięcia):
```bash
pio run -e esp32-s3-n16r8-notouch -t upload --upload-port COM9
```
albo gotowy obraz `merged-flash-notouch.bin` (offset `0x0`, 16 MB).
### Odczyt spoczynkowy (referencja)
Bez dotknięcia: `x=4095 y=0 z~9`. Z dotknięciem: `x/y` 500–3900, `z` 400–2100.
Filtr w `readRawTouchPoint()` odrzuca `x==0&&y==0` oraz `z>=4000`.

## 6. Wgrywanie plików LittleFS (mapy, splash, pelny interfejs WWW)

Pliki takie jak `splash.bmp`, `mapa.bmp`, `Mapa swiata.bmp`, `index.html` umieść w
katalogu `data/`, a następnie:

```bash
pio run -e esp32-s3-n16r8 -t uploadfs
```

W repo jest `data/splash.bmp` (placeholder 480x320, 24-bit), `data/fonts/*.vlw`
(fonty Inter/DejaVu/Roboto dla polskich znaków), **pełny interfejs WWW**
(`data/index.html` 148 KB, `data/indexEN.html` 145 KB) wraz z arkuszami stylów
(`data/style.base.css`, `data/style.css`, `data/style.desktop.css`,
`data/style.mobile.css`) oraz `data/manual.txt` i `data/instrukcja.txt`.

Firmware serwuje pod `/` i `/config` plik `/index.html` z LittleFS. Wbudowany HTML
(`getMainHTML()`) to tylko skrócony zapas, używany gdy pliku brak w LittleFS.
**Uwaga:** jeśli wgrasz niekompletny `index.html` (bez `style.*.css`), strona
będzie „ucięta"/bez wyglądu — firmware nie wykryje tego automatycznie, więc
wgraj cały zestaw z `data/`.

Fonty VLW: firmware szuka ich pod `/fonts/Inter-RegularNN.vlw`. Pliki `data/fonts/`
są wgrywane razem z LittleFS przez `-t uploadfs` — albo od razu przez `dist/merged-flash.bin`.

Adres startowy partycji LittleFS w tej tablicy to `0x810000` (patrz `partitions.csv`) —
przy ręcznym wgrywaniu `.bin` używaj tego offsetu, nie `0x290000` z instrukcji dla 4 MB.

## 7. Gotowe obrazy (katalog `dist/`)

Po `pio run` obrazy są w `dist/` (kopie z `.pio/build/esp32-s3-n16r8/`):

| Plik | Offset |
|---|---|
| `bootloader.bin` | `0x0` |
| `partitions.bin` | `0x8000` |
| `boot_app0.bin` | `0xe000` |
| `firmware.bin` | `0x10000` |
| `littlefs.bin` | `0x810000` |

`boot_app0.bin` pochodzi z frameworka (`~/.platformio/packages/framework-arduinoespressif32/tools/partitions/`)
i inicjalizuje obszar OTA. Skrypt `dist/flash.sh` wgrywa wszystko jedną komendą:

```bash
cd dist
PORT=/dev/ttyACM0 ./flash.sh      # Linux/Mac (na Windows: PORT=COM7)
```

Ręcznie tym samym esptool:

```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write_flash -z \
  0x0 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin \
  0x10000 firmware.bin 0x810000 littlefs.bin
```

> Firmware zajmuje ~1.42 MB, więc mieści się w 4 MB partycji `app0`.

### Jeden plik: `merged-flash.bin`

`dist/merged-flash.bin` to wszystkie obrazy scalone w jeden (16 MB, flash QIO/80 MHz).
Wgrywa się go **pod adres `0x0`** — najprościej, zwłaszcza przez przeglądarkę:

```bash
esptool.py --chip esp32s3 --port COM9 --baud 921600 write_flash 0x0 merged-flash.bin
```

WebSerial (https://jason2866.github.io/WebSerial_ESPTool/): wybierz plik
`merged-flash.bin`, adres `0x0`, Offset `0x0`. Obraz zawiera bootloader, partycje,
boot_app0, firmware i LittleFS (z `data/`).

> **Tryb flash = DIO.** Wszystkie obrazy są zbudowane w trybie `dio`
> (`board_build.flash_mode = dio`). Nie zmieniaj na `qio` przy scalaniu ani
> wgrywaniu — część modułów S3 nie startuje w QIO i wtedy widać w logu
> `mode:QIO` → `ets_loader.c 78` → restart (`rst:0x7 TG0WDT_SYS_RST`).
