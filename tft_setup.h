#pragma once

// Wspólna konfiguracja TFT_eSPI dla ESP32-S3 (N16R8) + zewnętrzny ILI9488 3.5" IPS 480x320.
//
// UWAGA: ten plik nadpisuje User_Setup.h biblioteki TFT_eSPI WYŁĄCZNIE gdy
// kompilujesz z -DTFT_ESPI_USER_SETUP_PATH="tft_setup.h" (patrz platformio.ini).
// W Arduino IDE takiego mechanizmu nie ma - wtedy wpisz te same ustawienia
// bezpośrednio w User_Setup.h biblioteki (patrz README_S3.md).

#ifndef TFT_PROFILE_ILI9488
#define TFT_PROFILE_ILI9488 1
#endif

#ifndef TFT_PROFILE_ILI9341
#define TFT_PROFILE_ILI9341 0
#endif

#ifndef TFT_DRIVER_PROFILE
#define TFT_DRIVER_PROFILE TFT_PROFILE_ILI9488
#endif

#ifndef USER_SETUP_INFO
#define USER_SETUP_INFO "ESP32S3_HAM_CLOCK_ILI9488_SETUP"
#endif

#ifndef ILI9488_DRIVER
  #define ILI9488_DRIVER
#endif

// Natywna rozdzielczość sterownika (portrait). UI w main.cpp pracuje w 480x320
// (landscape) przez tft.setRotation(1).
#ifndef TFT_WIDTH
  #define TFT_WIDTH 320
#endif
#ifndef TFT_HEIGHT
  #define TFT_HEIGHT 480
#endif

// ========== PINY SPI (ESP32-S3, FSPI / SPI2) ==========
// Na ESP32-S3 domyślne piny FSPI to dokładnie: SCK=12, MOSI=11, MISO=13, SS=10,
// dlatego obiekt 'SPI' (używany przez TFT_eSPI i XPT2046) działa bez remapu.

// KRYTYCZNE dla ESP32-S3: wymusza SPI_PORT = 2 (sprzetowy host SPI2/GPSPI2).
// TFT_eSPI 2.5.43 domyslnie robi SPI_PORT = FSPI, a Arduino core definiuje
// FSPI = 0 dla S3. Wtedy REG_SPI_BASE(0) = 0, SPI_USER_REG(0) = 0x10 i biblioteka
// pisze do adresu 0x10 -> "StoreProhibited, EXCVADDR: 0x00000010" w tft.begin().
// USE_FSPI_PORT ustawia SPI_PORT = 2, czyli poprawny rejestr SPI2.
#ifndef USE_FSPI_PORT
  #define USE_FSPI_PORT
#endif
// T_DO kontrolera XPT2046 jest podłączone do GPIO 14 (potwierdzone testem v4:
// GPIO 14 zwracał płynne wartości X/Y, GPIO 13 tylko szum 0/8191).
// TFT (ILI9488) nie używa MISO, więc ten pin obsługuje wyłącznie dotyk.
#ifndef TFT_MISO
  #define TFT_MISO 14
#endif
#ifndef TFT_MOSI
  #define TFT_MOSI 11
#endif
#ifndef TFT_SCLK
  #define TFT_SCLK 12
#endif
#ifndef TFT_CS
  #define TFT_CS 10
#endif
#ifndef TFT_DC
  #define TFT_DC 9
#endif
// TFT_RST = -1: reset sprzętowy nieużywany (moduł resetuje się z RC / z 3.3V).
#ifndef TFT_RST
  #define TFT_RST -1
#endif
// Podświetlenie (LED/BL) - sterowane PWM z main.cpp (setBacklightPercent).
#ifndef TFT_BL
  #define TFT_BL 21
#endif

// ========== DOTYK XPT2046 (ta sama magistrala SPI co TFT, osobny CS) ==========
// Definiujemy tutaj, aby main.cpp i tft_setup.h nie rozjechały się w przyszłości.
#ifndef TOUCH_CS
  #define TOUCH_CS 15
#endif
#ifndef TOUCH_IRQ
  #define TOUCH_IRQ 16
#endif

// Przełącznik dotyku. Domyślnie 1 (włączony) - XPT2046 podłączony, T_DO na
// GPIO 14. Wyłącz (0 / -DTOUCH_ENABLED=0), jeśli kontroler odłączony: wiszące
// MISO daje fałszywe dotknięcia. main.cpp dodatkowo filtruje odczyty
// x==0&&y==0 oraz z>=4000.
#ifndef TOUCH_ENABLED
  #define TOUCH_ENABLED 1
#endif

#ifndef TOUCH_MOSI
  #define TOUCH_MOSI TFT_MOSI
#endif
#ifndef TOUCH_MISO
  #define TOUCH_MISO TFT_MISO
#endif
#ifndef TOUCH_CLK
  #define TOUCH_CLK TFT_SCLK
#endif

// ========== KOLORY / INWERSJA ==========
// Panel IPS ILI9488 z reguły wymaga inwersji kolorów. main.cpp wykrywa
// TFT_INVERSION_ON i ustawia tftInvertColors = true (można przełączyć w WWW).
#ifndef TFT_INVERSION_ON
  #define TFT_INVERSION_ON
#endif

#ifndef TFT_BL_INVERTED
#define TFT_BL_INVERTED 0
#endif

// Kolejność kolorów. Jeśli na ekranie czerwony i niebieski są zamienione,
// zmień poniżej TFT_BGR na TFT_RGB.
#ifndef TFT_RGB_ORDER
#define TFT_RGB_ORDER TFT_BGR
#endif

// 40 MHz to bezpieczna wartość dla ILI9488. Przy artefaktach/śniegu zmniejsz do
// 27 MHz lub 20 MHz. Przy krótkich, porządnych przewodach działa też 55-80 MHz.
#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY 40000000
#endif

#ifndef SPI_READ_FREQUENCY
#define SPI_READ_FREQUENCY 20000000
#endif

#ifndef SPI_TOUCH_FREQUENCY
#define SPI_TOUCH_FREQUENCY 2500000
#endif

#ifndef LOAD_GLCD
#define LOAD_GLCD
#endif

#ifndef LOAD_FONT2
#define LOAD_FONT2
#endif

#ifndef LOAD_FONT4
#define LOAD_FONT4
#endif

#ifndef SMOOTH_FONT
#define SMOOTH_FONT
#endif
