# ESP32-HAM-CLOCK — tabela pinów i zmiana pinów

Sprzęt: **ESP32-S3-WROOM-1 N16R8** (16 MB flash QIO + 8 MB PSRAM OPI)
+ **ILI9488 na 3.5" i 4" 480×320** (SPI) + dotyk **XPT2046** (SPI, wspólna magistrala).

Wszystkie piny zmienia się w **jednym pliku**: `tft_setup.h` (albo z `platformio.ini` /
Arduino IDE przez `-D...`, bez edycji pliku).

---

## 1. Piny ustawione teraz (domyślne, działające)

| Funkcja | GPIO | Pin na module TFT | Uwaga |
|---|---|---|---|
| SPI SCK / CLK | **12** | SCK / SCL | wspólne z T_CLK |
| SPI MOSI / SDI | **11** | SDI / MOSI | wspólne z T_DIN |
| SPI MISO / SDO | **14** | SDO / MISO | **wspólne z T_DO** (tylko dotyk) |
| TFT CS | **10** | CS | |
| TFT DC (RS) | **9** | DC / RS | |
| TFT RST | **-1** | RESET | niepodłączony (moduł resetuje się z RC) |
| Podświetlenie BL | **21** | LED / BL | PWM (jasność + wygaszanie) |
| Dotyk CS | **15** | T_CS | |
| Dotyk IRQ | **16** | T_IRQ | opcjonalny |
| Dotyk DO (MISO) | **14** | T_DO | = TFT_MISO (`TFT_MISO 14`) |
| Dotyk DIN (MOSI) | **11** | T_DIN | = TFT_MOSI |
| Dotyk CLK | **12** | T_CLK | = TFT_SCLK |
| RGB LED R / G / B | **4 / 5 / 6** | — | statusowy LED, opcjonalny |
| Bateria (ADC) | **1** | — | dzielnik 100k/100k (ADC1_CH0) |

- `TFT_MISO 14`: T_DO XPT2046 jest na GPIO 14 (ustalone testem sprzętowym; GPIO 13 dawał szum).
- Dotyk korzysta z **tej samej** magistrali SPI co TFT — nie ma osobnych pinów CLK/MOSI/MISO.

### Piny zarezerwowane na N16R8 — NIE używać
- **26–37** — flash (QIO) i PSRAM (OPI).
- **19, 20** — natywne USB (D-, D+).
- **0, 3, 45, 46** — piny strapping (tryb boot / napięcie).
- **43, 44** — UART0 (logi na COM9).
- **1** — używane na pomiar baterii (można zwolnić, jeśli nie używasz dzielnika).

### Wolne, bezpieczne GPIO (gdyby trzeba było przenieść piny)
`2, 7, 8, 13, 17, 18, 38, 39, 40, 41, 42, 47, 48`
(z 13 uwaga: u Ciebie to nie jest T_DO, ale nadaje się na CS/DC/BL).
Uwaga: GPIO 45/46 bywają zajęte przez PSRAM/strapping — unikać.

---

## 2. Jak zmienić piny

### Sposób A — edycja `tft_setup.h` (zalecany, na stałe)
Otwórz `tft_setup.h` i zmień wartości w blokach `#ifndef`:

```cpp
#define TFT_MISO 14   // T_DO dotyku
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS   10
#define TFT_DC    9
#define TFT_RST  -1
#define TFT_BL   21
#define TOUCH_CS 15
#define TOUCH_IRQ 16
```

Zmiany samego dotyku bez ruszania TFT — wystarczy `TFT_MISO` (T_DO) oraz `TOUCH_CS`.

### Sposób B — `platformio.ini` (bez edycji plików, np. nowy wariant)
Dopisz do `build_flags` (nadpisuje `tft_setup.h`, bo main.cpp/tft_setup.h mają `#ifndef`):

```ini
build_flags =
  ${env:esp32-s3-n16r8.build_flags}
  -DTFT_MISO=13
  -DTOUCH_CS=15
  -DTFT_BL=21
```

Przykład kompletnego, osobnego środowiska znajdziesz w `platformio.ini`
(sekcja `[env:esp32-s3-n16r8-altpins]`).

### Sposób C — Arduino IDE
W `tft_setup.h` zmieniaj jak w sposobie A. Dodatkowe `-D` wpisz w
**File → Preferences → Additional compiler flags** lub w komentarzach nie zadziała —
najprościej edytuj `tft_setup.h`.

### Zmiana samej kalibracji / kolejności osi dotyku (bez zmiany pinów)
W `main.cpp` (lub przez `-D`):

```cpp
#define TOUCH_X_MIN 200
#define TOUCH_X_MAX 3800
#define TOUCH_Y_MIN 200
#define TOUCH_Y_MAX 3800
#define TOUCH_SWAP_XY  false
#define TOUCH_INVERT_X false
#define TOUCH_INVERT_Y false
```

---

## 3. Zmiana jasności / wygaszania podświetlenia (GPIO 21)
- Jasność: menu ekranu lub WWW (`tft_backlight`).
- Gdy podświetlenie jest odwrócone (gaśnie przy 100%), ustaw `#define TFT_BL_INVERTED 1`.
