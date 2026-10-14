# AGENTS.md — ESP32-HAM-CLOCK

Firmware zegara/DX-toola dla krótkofalowców (Arduino/PlatformIO).

## Cel sprzętowy
- **MCU:** ESP32-S3-WROOM-1 **N16R8** (16 MB flash QIO + 8 MB PSRAM OPI)
- **Wyświetlacz:** zewnętrzny **ILI9488 3.5" 480x320 IPS** po SPI (TFT_eSPI)
- **Dotyk:** **XPT2046** po SPI (wspólna magistrala, osobny CS)

## Struktura
- `main.cpp` — cały firmware (jeden duży plik, ~21 tys. linii)
- `tft_setup.h` — konfiguracja TFT_eSPI (nadpisuje `User_Setup.h` przez `TFT_ESPI_USER_SETUP_PATH`)
- `platformio.ini` — env `esp32-s3-n16r8`; `src_dir = .`
- `partitions.csv` — 16 MB: app0/app1 po 4 MB (OTA) + ~7.9 MB LittleFS
- `data/` — zawartość LittleFS (`splash.bmp` placeholder 480x320, **pełny UI WWW** `index.html`/`indexEN.html` + `style.*.css`, `manual.txt`, `instrukcja.txt`, `fonts/*.vlw` + README.txt)
- `README_S3.md` — pinout, kompilacja (PlatformIO + Arduino IDE), troubleshooting

## Interfejs WWW
- Pełny UI (148 KB) jest plikiem `data/index.html` serwowanym z LittleFS pod `/` i `/config`; wymaga `data/style.*.css` (inaczej strona jest „ucięta"/bez wyglądu).
- Wbudowany `getMainHTML()` w `main.cpp` to tylko skrócony zapas (gdy brak pliku w LittleFS).
- Endpointy API: `/api/config`, `/api/save`, `/api/spots`, `/api/aprs`, `/api/pota`, `/api/hamalert`, `/api/propagation`, `/api/uart_status`, `/api/bridge_bt_*`, `/api/reset_touch`, `/api/reset_tft_rotation`.
- `/api/uart_status` i `/api/bridge_bt_*` to stuby (ten build nie ma UART2 ani modułu ESP32-BRIDGE) — zwracają stan „nieaktywny", żeby pełne UI działało bez błędów JS.

## Piny (ESP32-S3)
TFT: SCK 12, MOSI 11, MISO **14**, CS 10, DC 9, RST -1, BL 21.
Dotyk: CS 15, IRQ 16 (DIN/DO/CLK współdzielone z TFT; T_DO = GPIO 14).
RGB LED: 4/5/6. Bateria ADC: 1.
**Nigdy nie używać GPIO 26–37** (flash/PSRAM). GPIO 19/20 = USB.

## Build
```bash
pio run -e esp32-s3-n16r8            # kompilacja
pio run -e esp32-s3-n16r8 -t upload  # wgranie
pio run -e esp32-s3-n16r8 -t uploadfs # LittleFS (data/)
```

## Konwencje / pułapki
- `main.cpp` ma **BOM UTF-8** i polskie komentarze — zachować BOM przy edycji.
- Plik używa ArduinoJson **v6** (`DynamicJsonDocument`) — nie podnosić do v7 bez refaktoryzacji.
- TFT_eSPI musi dostać `-I${PROJECT_DIR}` — inaczej `tft_setup.h` jest ignorowany (biblioteka robi `__has_include(<tft_setup.h>)`, a flaga `TFT_ESPI_USER_SETUP_PATH` w 2.5.43 nie działa).
- ESP32-S3: TFT_eSPI 2.5.43 ustawia `SPI_PORT = FSPI = 0` (core 2.x), co daje `REG_SPI_BASE(0)=0` i crash `tft.begin()`. Wymagane `USE_FSPI_PORT` w `tft_setup.h` (wymusza `SPI_PORT=2`).
- **Dotyk:** `TOUCH_ENABLED` w `tft_setup.h` (0 = wyłączony). Przy niepodłączonym XPT2046 wiszące MISO daje fałszywe dotknięcia i samo otwiera menu ustawień TFT.
- Arduino IDE nie ma mechanizmu `-I`/`tft_setup.h` — ustawienia trzeba wpisać w `User_Setup.h`.
- Inwersja kolorów ILI9488: `TFT_INVERSION_ON` (kod wykrywa makro).
- Instrukcja wbudowana w WWW (sekcja montażu + `getManualPL()`) zaktualizowana pod S3 (piny, adresy flash, bootloader 0x0, LittleFS 0x810000).

## Reset po połączeniu WiFi — ROZWIĄZANE (2026-10-11)
Objaw: ESP restartuje się przy wejściu na stronę WWW po WiFi. Log: `***ERROR*** A stack overflow in task loopTask` / `Stack canary watchpoint triggered (loopTask)`.
Dwie przyczyny:
1. **Przepełnienie stosu `loopTask`.** Handlery `WebServer` wykonują się w zadaniu `loop()` (domyślnie **8192 B** stosu). Handlery `/api/spots`, `/api/pota`, `/api/hamalert` alokowały na stosie `StaticJsonDocument<12000>` (12 KB) → panic. **Fix:** wszystkie duże dokumenty JSON → `DynamicJsonDocument doc(N)` (sterta), plus zapasowo `-DARDUINO_LOOP_STACK_SIZE=16384`.
2. **LittleFS się nie montował.** `LittleFS.begin()` w tym rdzeniu domyślnie szuka partycji o etykiecie **`spiffs`**, a nasza partycja nazywa się `littlefs` → `partition "spiffs" could not be found`, `LittleFS ready: NO`. Skutek: pełny UI z LittleFS nigdy nie był serwowany (stąd „ucięta" strona). **Fix:** `LittleFS.begin(false, "/littlefs", 10, "littlefs")` (jawne `partitionLabel`).

## Wygląd WWW + śledzenie satelitów — 2026-10-11 (v1.4)
- **Nowoczesny motyw:** `data/style.modern.css` — nakładka ładowana PO style.base/desktop/mobile (nadpisuje wygląd bez zmiany HTML/JS). Zmienne CSS, ciemny motyw z gradientami, glassmorphism, karty, pigułki nawigacji (sticky), tabele sticky-header + hover, responsywność.
- **Pasek statusu** w nagłówku (online/offline, IP, wolny RAM) — nowy lekki endpoint `/api/status` (StaticJsonDocument<256>), odpytywany co 10 s.
- **Strona „Satelity"** (`#satellite-page`, przycisk 🛰 w nav, PL+EN):
  - `data/satellite.js` — mapa świata na canvas (kontury kontynentów WBUDOWANE w JS, brak CDN → działa offline), ground-track z SGP4 (`/api/iss_track`), okrąg zasięgu 5000 km, znacznik QTH, radar azymut/elewacja, metryki (lat/lon/wys./prędkość/az/el/dystans), najbliższy przelot, auto-odświeżanie co 5 s.
  - Nowy endpoint `/api/iss_track` (DynamicJsonDocument 4096): pozycja + `track[45]` (issTrajectoryLat/Lng z SGP4) + observer + next_pass + in_range.
  - Trasy LittleFS: `/style.modern.css`, `/satellite.js`.
- Wejście na stronę wysyła zdarzenie `satellite:enter` (z `showPage`), co uruchamia/re-rozmiarowuje canvas.

## Błąd zapisu konfiguracji: empty_ssid — ROZWIĄZANE (2026-10-11)
Objaw: WWW pokazuje `Błąd zapisu konfiguracji: empty_ssid` przy każdym zapisie (TFT, APRS, pogoda, moduły).
Przyczyny:
1. **Walidacja `empty_ssid` blokowała cały zapis.** Handler `/api/save` odrzucał żądanie, gdy `wifiSSID` i `wifiSSID2` były puste. W trybie AP (bez skonfigurowanego WiFi) formularz ma puste SSID, więc **żadne** inne ustawienie nie dało się zapisać. **Fix:** puste SSID dozwolone — urządzenie zostaje w AP; zapis innych ustawień działa.
2. **Klucze NVS >15 znaków.** `aprs_alert_near_en` i `cluster_usefilters` (18 znaków) przekraczały limit NVS → `nvs_set_u8 fail: KEY_TOO_LONG` (widoczne w logu). Te ustawienia nigdy się nie zapisywały. **Fix:** skrócone do `aprs_alert_near` / `cluster_usefilt`.
3. **Restart po każdym zapisie.** Wcześniej `/api/save` zawsze zwracał `action:"restart"`. **Fix:** restart tylko gdy zmieniono SSID i trzeba przełączyć AP→STA (`needRestart`), inaczej `action:"saved"`.

## Diagnoza dotyku (XPT2046) — stan na 2026-10-11
Ustalenia z testów sprzętowych (`tools/touch_test`):
- Skan I2C: 0 urządzeń -> panel to XPT2046 (SPI), nie FT6236/GT911.
- **ROZWIAZANE: T_DO (MISO) jest na GPIO 14, nie 13.** Test v4 (zmiennosc danych przy ruchu palca): GPIO 14 dawalo 19-36 roznych wartosci w zakresie 467-4095 (prawdziwe X/Y), GPIO 13 tylko 0/8191 (szum wiszacej linii). Domyslne ustawienie to `TFT_MISO 14` + `TOUCH_ENABLED 1` w tft_setup.h.
- Poprzednia (bledna) hipoteza: GPIO 13 "zwarte do 0" - wynikalo z tego, ze kontroler nie sterowal GPIO 13 (T_DO tam nie bylo).
- Bit-bang (bez biblioteki) na wszystkich sensownych GPIO: brak danych (same 0 albo 0x1FFF).
- T_IRQ (GPIO 16) reaguje na dotyk -> kontroler prawdopodobnie działa; problem jest tylko na linii T_DO.
- Wartości `B1 C1 91 D1 91` na GPIO 9/10 to przesłuch MOSI (piny TFT_DC/TFT_CS), NIE dane dotyku.
- Uwaga: `SPIClass::begin()` w rdzeniu ESP32 robi `if(_spi) return;` — przy działającej magistrali ignoruje nowe piny. Skan MISO wymaga `SPI.end()` przed każdą próbą.
- Firmware zabezpieczony: `readRawTouchPoint()` odrzuca `x==0&&y==0` oraz `z>=4000`.
Do sprawdzenia po stronie sprzętu: VCC XPT2046 = 3,3 V, przejście T_DO<->GPIO13, opór GPIO13<->GND, ewentualna zamiana T_DIN/T_DO.

## Testy sprzętowe
- `tools/touch_test/` — osobny projekt PlatformIO (env `touch_test`) diagnozujący XPT2046:
  wypisuje surowe `x/y/z` na Serial. Build: `pio run -d tools/touch_test -e touch_test`.
  Główny env wyklucza `tools/` przez `build_src_filter`.

## Restarty / "ekran zgasl i WiFi sie rozlacza" — ROZWIĄZANE (2026-10-11, v1.4.1)
Objawy zgłoszone przez użytkownika:
1. "ESP nie nawiązuje połączenia WiFi" / "cały czas prosi o hasło WiFi".
2. "Ekran na początku działał i nagle przestał" (świeci tylko podświetlenie).
3. Ten sam objaw powtórzony przy kolejnym uruchomieniu.

Przyczyny i poprawki w `main.cpp`:
1. **Brownout wyłączony tylko dla ESP32 klasycznego.** `WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0)`
   było pod `#if CONFIG_IDF_TARGET_ESP32`, więc na **S3 detektor brownoutu zostawał włączony**.
   Każdy chwilowy spadek napięcia (skok poboru prądu przez radio WiFi / podświetlenie TFT)
   resetował układ → ekran gasł, AP "gubił" klienta i ponownie pytał o hasło.
   **Fix:** `#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S3` (+ `#include <esp_system.h>`).
2. **Diagnostyka powodu restartu.** W `setup()` wypisywane jest teraz `esp_reset_reason()`
   (`BROWNOUT`, `TASK_WDT`, `PANIC/CRASH`...). Pierwsza linia logu mówi, co restartuje układ.
3. **`enterScreenSleep()` gasił podświetlenie błędnym API.** `ledcWrite(TFT_BL_PIN, 0)`
   w rdzeniu 2.x przyjmuje **kanał** (0..15), a nie pin. Pin BL=21 ≥ LEDC_CHANNELS →
   wywołanie po cichu ignorowane → podświetlenie **zostawało włączone**, a ekran był czyszczony
   na czarno. To dawało dokładnie "świeci, ale nic nie widać".
   **Fix:** helper `writeBacklightDuty()` wybiera kanał (rdzeń 2.x) albo pin (rdzeń 3.x);
   użyty w `setBacklightPercent()` i `enterScreenSleep()`.
4. **Uśpienie bez działającego dotyku = nieodwracalny czarny ekran.** Jeśli XPT2046 nie działa
   (np. wariant `notouch`), nie ma jak obudzić ekranu. **Fix:** `enterScreenSleep()` nie usypia,
   gdy `!touchReady`.
5. **Ograniczenie mocy WiFi.** `WiFi.setTxPower(WIFI_POWER_11dBm)` w `startAPMode()` —
   mniejszy chwilowy pobór prądu, mniejsze ryzyko brownoutu przy słabym zasilaniu (USB/ładowarka).
   Reconnect STA w tle obsługuje rdzeń (`_autoReconnect = true` domyślnie).

Wniosek dla użytkownika: jeśli po tych poprawkach w logu nadal pojawia się
`Powód restartu: BROWNOUT`, problem jest sprzętowy — zasilanie (kabel USB, ładowarka,
kondensator przy 3V3). Firmware nie da się wtedy naprawić samym kodem.

## Artefakty (v1.4.3)
- `pio run -e esp32-s3-n16r8` i `-e esp32-s3-n16r8-notouch` — OK.
- `pio run -e esp32-s3-n16r8 -t buildfs` → `littlefs.bin`.
- Scalanie: `esptool.py --chip esp32s3 merge_bin -o merged-flash.bin --flash_mode dio
  --flash_freq 80m --flash_size 16MB 0x0 bootloader.bin 0x8000 partitions.bin
  0xe000 boot_app0.bin 0x10000 firmware.bin 0x810000 littlefs.bin`.
- ZIP: `python3 tools/make_zip.py` → `esp32-ham-clock-s3.zip` (576 wpisów, ~7.4 MB).

## Analiza logu COM9 (2026-10-11 23:25) — ESP32 jest SPRAWNY
Log użytkownika (v1.4.1) potwierdza, że sprzęt działa, brak restartów:
- `[SETUP] Powód restartu: POWERON (1)` — **NIE brownout/panic/WDT**. Zasilanie OK.
- TFT begin OK, `setRotation(1)`, `invertDisplay(true)` — panel odpowiada.
- LittleFS zamontowany (8257536 B, 1458176 użyte), pliki obecne.
- Brak konfiguracji WiFi → AP `ESP32-HAM-CLOCK` / hasło `1234567890`, IP 192.168.4.1.
- `Serwer WWW uruchomiony`, ISS task na Core 0 (czeka 90 s).
- `Free Heap: 199312 bytes` **stabilne co 30 s** → brak restartu, brak wycieku.
- `addApbChangeCallback(): duplicate func` — **nieszkodliwe** ostrzeżenie rdzenia
  (podwójna rejestracja callbacku APB przy zmianie freq); nie powoduje awarii.

Wnioski:
1. „Czarny ekran + podświetlenie" NIE jest restartem. Najprawdopodobniej splash
   (`drawSplashScreen` -> `/splash.bmp`) albo główny ekran rysuje się, ale bez
   WiFi/spotów jest wizualnie ubogi. Dodano log `[UI] alive screen=... menu=... sleep=...`
   co 15 s w `uiTaskLoop`, żeby potwierdzić, że zadanie UI żyje i jaki ekran rysuje.
2. **Fałszywy odczyt baterii `0.21V (0%)`** = GPIO1 (ADC1_CH0) niepodłączony
   (wisi) -> dzielnik nie zamontowany albo akumulator niepodłączony. To NIE usterka
   ESP. Dodano sanity-check: odczyt < 1.0 V -> brak pomiaru (bez fałszywego 0%
   i ostrzeżenia "podłącz ładowarkę"), w `updateBatteryStatus()` i przy starcie.
3. Pierwszy odczyt `4.05V (81%)` to wynik wstępny (przed ustabilizowaniem ADC) —
   teraz też filtrowany progiem 1.0 V.

### Uwaga o odczycie baterii
`BATTERY_ADC_PIN 1` = GPIO1 (ADC1_CH0) z dzielnikiem 100k/100k. Jeśli dzielnik
nie jest fizycznie podłączony, GPIO1 "wisi" i ADC zwraca śmieci (0.2-0.4 V).
Nie da się tego naprawić w firmware — trzeba podłączyć dzielnik lub ustawić
pomiar nieaktywny.

## v1.4.2 - zapis WWW bez restartu ESP (12.10.2026)
Zgloszony objaw: "po zapisaniu zmian w WWW restart ESP32". Przyczyna: handler
`/api/save` wywolywal `requestRestart()` gdy zmieniono SSID, co robilo pelny
`ESP.restart()` (reset TFT i wszystkich uslug).
Poprawka:
- nowe flagi `wifiReconfigRequested` / `wifiReconfigAtMs`,
- nowa funkcja `wifiReconfigure()` (obok `connectToWiFi`): laczy STA w locie,
  po sukcesie wylacza AP (`softAPdisconnect`), wznawia NTP/cluster/POTA/APRS;
  brak SSID -> powrot do AP. Bez `ESP.restart()`.
- handler `/api/save`: przy zmianie SSID ustawia `wifiReconfigRequested` i odpowiada
  `action:saved` (juz nie `restart`). Zmiany TFT/APRS/pogody nadal bez restartu.
- forward decl: `connectToCluster`, `connectToPotaCluster`, `updateNTPTime`.

## Piny (skrot) - pelna tabela w PINOUT.md
TFT: SCLK=12, MOSI=11, MISO=14, CS=10, DC=9, RST=-1, BL=21.
Dotyk XPT2046 (wspolne SPI): T_CS=15, T_IRQ=16, T_DIN=11, T_DO=14, T_CLK=12.
Bateria ADC=1. RGB LED=4/5/6. Zarezerwowane: 26-37 (flash/PSRAM), 19/20 (USB).
Zmiana pinow: `tft_setup.h` lub `platformio.ini` build_flags (env `esp32-s3-n16r8-altpins`).

## Serwer plikow (localhost:12000)
Workery: work-1 (12000) i work-2 (12001). Serwer pada przy resecie srodowiska.
Restart: `bash tools/serve.sh` (http.server 12000 w /workspace/share).


## Linki WWW (12.10.2026)
- Repo: https://github.com/Tom50505050/esp32-ham-clock (w kafelku "LINKI",
  link, API releases/latest, oraz w main.cpp APRS_POSITION_COMMENT i instrukcji).
- Usunieto kafelek sp3yor.net z tabeli "Polecane" (index.html + indexEN.html).
- Etykieta wersji kafelka zaktualizowana v1.3.0 -> v1.4.2.
- Podglad strony urzadzenia: /workspace/share/index-device.html (dodany do :12000).


## Autor / naglowki WWW (v1.2, 12.10.2026)
- Naglowek na WSZYSTKICH stronach WWW = "ESP32-HAM-CLOCK v1.2"
  (title, app-title, h3 na stronie LINKI, h2 na stronie Konfiguracja) - PL i EN.
- Stopka (PL): "Autor oprogramowania Tomasz SP9TNV na podstawie projektu Konrad
  Wisniewski SP3KON." + "Oprogramowanie zostalo zmodyfikowane przez SP9TNV na
  podstawie projektu Konrad Wisniewski SP3KON (z uzyciem AI)." + "Obecna wersja
  oprogramowania ESP32-HAM-CLOCK v1.2 z dnia 12.10.2026." + "Kontakt: SP9TNV@gmail.com".
- Analogicznie EN.
- Firmware (main.cpp): ekran "o programie" na TFT, instrukcja LittleFS, stopka
  HTML pomocy i strona konfiguracji -> autor Tomasz SP9TNV, kontakt SP9TNV@gmail.com.
- Komunikat po zapisie WWW: "przelaczy sie na nowa siec WiFi (bez restartu)".


## RESTART PO ZAPISIE WWW - przywrocony (12.10.2026)
Zmiana decyzji: uzytkownik chce, aby zapis na stronie WWW RESTARTOWAL ESP.
Poprzednia proba (wifiReconfigure bez restartu) cofnieta.
Stan handlera /api/save:
- bool needRestart = wifiChanged && (wifiConfigured || !wifiConnected);
- jesli needRestart: action:"restart" + requestRestart(1500) -> ESP.restart()
- inaczej: action:"saved" (bez restartu - tylko TFT/APRS/pogoda).
JS: alert "...zrestartuje sie i polaczy z nowa siecia WiFi."
Pozostawione (nieuzywane) elementy wifiReconfigure/wifiReconfigRequested -
kandydaci do usuniecia w przyszlosci.


## KAZDY zapis WWW restartuje ESP (12.10.2026)
Decyzja: przyciski "Zapisz ustawienia TFT", "Zapisz konfiguracje", APRS i pogoda
(wszystkie -> /api/save) maja restartowac ESP.
Stan handlera /api/save: bezwarunkowo action:"restart" + requestRestart(1500)
-> ESP.restart(). Zmienne wifiChanged/wifiConfigured pozostawione jako (void).
JS: alert "Ustawienia zapisane. Urzadzenie zrestartuje sie."
Osobne endpointy /api/reset_touch i /api/reset_tft_rotation dzialaja jak wczesniej
(reset_tft_rotation tez restartuje).


## Ikony pogody icon50/ (12.10.2026)
Brakowalo plikow ikon - firmware juz szuka /icon50/NNN.bmp (weatherIconPathForId).
Pobrano 15 ikon (50x50, 24-bit BI_RGB) z repo Tom50505050/esp32-ham-clock:
200,300,500,502,511,520,600,611,700,800,800n,801,801n,803,wind -> data/icon50/.
- drawWeatherIcon (ekran WEATHER) + drawWeatherDetailIconCell (ekran WEATHER
  FORECAST) automatycznie korzystaja z tych plikow (fallback: rysowana ikona).
- Rozszerzenia -w (biala ramka) NIE sa uzywane przez firmware - nie dodano.
- littlefs.bin zawiera ikony; merged-flash.bin je osadza.
- Wgrywanie: merged-flash.bin (0x0) albo osobno littlefs.bin (0x810000).


## Pelny folder data/ + prawdziwa mapa satelitow (12.10.2026)
- data/ zawiera teraz komplet: index.html, indexEN.html, style.*, satellite.js,
  fonts/*.vlw, icon50/*.bmp, splash.bmp, mapa.bmp, mapa_16bit.bmp,
  "Mapa swiata.bmp" (prawdziwa mapa 480x320, 24-bit).
- Prawdziwe mapy pobrane z repo Tom50505050/esp32-ham-clock (data/).
- Satelity WWW: data/satellite.js rysuje prawdziwa mape jako tlo
  (Image src=/mapa_swiat.bmp -> drawImage). Fallback: wbudowane kontury
  Natural Earth (var LAND) + gradient oceanu.
- Firmware: nowe endpointy w setupWebServer():
    GET /mapa_swiat.bmp  -> streamLittleFsFile("/Mapa swiata.bmp", image/bmp)
    onNotFound -> /icon50/<plik>.bmp z LittleFS (image/bmp)
- Narzedzia: tools/gen_sat_land.py + tools/apply_sat_land.py (regeneracja
  konturow z Natural Earth), tools/patch_sat_map.py.
- UWAGA: brak uprawnien push do GitHub (konto sp9tnv-ctrl ma tylko odczyt
  repo Tom50505050 i SP3KON; nie moze forsowac ani tworzyc repo).

## Pro tracker satelitow WWW (12.10.2026)
data/satellite.js: cien nocy (terminator z subsolarPoint, getImageData/putImageData),
okno widocznosci (footprint z wysokosci), poswiata orbity z grotami, siatka z
etykietami lon, celownik QTH. Legenda PL/EN (noc, okno widocznosci).
Podglad: /workspace/share/_sat-preview.html + _preview.html (mock API).


## PSK map: linie + dni 1..30 (12.10.2026)
- drawPskMap(): pozycja usera z userLatLonValid LUB z lokatora (fallback locatorToLatLon). Bez fallbacku userOnMap=false -> brak linii DX i czerwonej kropki.
- Menu PSK pole "Dni": wybor 1..30 (bylo cykl 1/2/3). Clamp psk_report_days = 30. Commit ba29ea3.

## ISS: flagi TFT + okno przelotu 24 h (12.10.2026)
- FLAGA TFT: drawBmpFromFS() akceptuje WYLACZNIE 24-bit BMP (bpp==24). Wczesniejsze flagi 16-bit RGB565 NIE byly rysowane - dlatego brak flagi przy kraju. tools/convert_flags_tft.py generuje 24-bit BMP (32x21, bottom-up, BGR) do data/tft_flags/<CC>.BMP. drawBmpFromFS(path,x,y) - y to DOLNY wiersz flagi. Nazwa kraju centrowana obok flagi.
- NASTEPNY PRZELOT: przyczyna "brak danych" = zbyt krotkie okno skanu 6 h. ISS wraca nad dana szerokosc po kilku orbitach - realnie 8-12 h. Okno zwiekszone do 24 h (updateIssNextPassEstimate). Dodane AOS/LOS/maxEl, obsluga gdy ISS widoczny teraz ("do HH:MM"). /api/iss_track zwraca next_pass_aos/los/max_el/iss_visible. n2yo czyta endUTC/maxEl.
- WAZNE: petle skanu SGP4 (do 1440 iteracji) MUSZA miec vTaskDelay(1) co 15 iteracji, inaczej Task Watchdog resetuje ESP.
- INSTRUKCJA.md = pelny przewodnik projektu (build PIO/Arduino, COM9, flagi, ISS, PSK).

## Splash (ekran startowy) — 12.10.2026
- drawSplashScreen() sprawdza nazwy: /icon50/splash.bmp, /splash.bmp, /boot.bmp, /logo.bmp, /start.bmp, /splash_16bit.bmp, /splash16.bmp. Probuje 24-bit BMP (drawBmpFromFS) ORAZ 16-bit RGB565 (drawBmp16FromFS). Wczesniej tylko 24-bit -> 16-bit splash.bmp dawal czarny ekran.
- Aby zmienic splash: wrzuc plik do data/ (dowolna z tych nazw), potem -t uploadfs. PNG/JPG -> tools/make_splash.py obraz.png data/splash.bmp (480x320, 24-bit, bottom-up).
- UWAGA: firmware NIE dekoduje PNG/JPG (brak pngle/TJpgDec) - tylko BMP.

## ISS: predyktor nextpass() + plan kilku przelotow (13.10.2026)
- PRZYCZYNA "brak odczytu nastepny przelot" na urzadzeniu: reczny skan orbity co 60 s
  w oknie 24 h (do ~1440 propagacji SGP4) byl (a) bardzo drogi na Core 0 i (b) zawodny
  w pierwszym cyklu po starcie (TLE/NTP jeszcze sie nie ustabilizowaly). Matematycznie
  byl poprawny - sprawdzone natywnie na prawdziwym TLE (144/144 zapytan OK).
- FIX: updateIssNextPassEstimate() uzywa teraz wbudowanego predyktora biblioteki Sgp4:
  initpredpoint(epoch, 0.0) + nextpass(&info, 60, false, 0.0) w petli -> lista AOS/LOS/maxEl
  dla kilku najbliszszych przelotow. Kilka-kilkanascie iteracji Brenta zamiast 1440.
  Gdy ISS widoczny teraz - krotki (max ~100 min) skan do biezacego LOS, potem predyktor
  startuje od LOS (zeby nie zwrocic biezacego przelotu jako "nastepnego").
- Nowe globalne: ISS_MAX_PASSES (4), issPassCount, issPassAos[]/issPassLos[]/issPassMaxEl[].
- /api/iss_track zwraca dodatkowo tablice "passes": [[aos,los,maxEl],...].
- TFT (SCREEN_ISS_PASS_TRACKING): panel "NASTEPNE PRZELOTY" (348,38,128x70) - do 3 wierszy
  HH:MM + maxEl; pole "AOS IN" pokazuje odliczanie ("za 2h05m" / "za 12m" / "w toku").
- WWW: karta "Nastepne przeloty" (#sat-pass-list) w satellite.js renderuje liste z d.passes.
- UWAGA: nextpass() modyfikuje wewnetrzny stan Sgp4 (jdC) - wywolywac pod issTleMutex,
  tak jak reszta uzyc issSat.
- Artefakty: pio run -e esp32-s3-n16r8 (+ -notouch), -t buildfs, merge_bin -> dist/,
  python3 tools/make_zip.py. esptool.py jest w ~/.platformio/packages/tool-esptoolpy/.

## ISS: ekran TFT w stylu strony WWW "Satelity" (13.10.2026)
- CEL: ekran ISS na TFT ma wygladac jak zakladka "Satelity" z WWW (data/index.html
  + data/satellite.js + data/style.modern.css). Zamiast rozrzuconych prostokatow z
  recznym wymazywaniem (migotanie, resztki po trajektorii/ikonie) caly ekran jest
  teraz renderowany do bufora TFT_eSprite 480x320 (16bpp, PSRAM) i wypychany JEDNYM
  pushSprite(0,0). Brak migotania i brak recznego kasowania - kazda klatka jest pelna.
- Uklad (480x320, wspolrzedne w main.cpp, issRenderFrame):
  * naglowek 0..30: "ISS TRACKER" + kropka statusu NA ZYWO/CZEKAJ,
  * lewa kolumna x=4 w=118: 6 kart metryk (KRAJ POD ISS, SZEROKOSC, DLUGOSC,
    WYSOKOSC, PREDKOSC, W ZASIEGU) - odpowiednik .sat-metric (akcentowy pasek 3 px),
  * prawa kolumna x=358 w=118: karta NAJBLIZSZY PRZELOT (y=34..90), karta
    NASTEPNE PRZELOTY (y=94..190, do 4 wierszy "dd.mm HH:MM" + maxEl) oraz 3 karty
    AZYMUT/ELEWACJA/DYSTANS (y=194..320) - odpowiednik .sat-pass/.sat-pass-list,
  * srodek gora: mapa swiata 228x114 @ (126,34) z trajektoria naziemna, siatka 30 st.,
    znacznikiem QTH (zloty) i ISS (teal) - odpowiednik #sat-map,
  * srodek dol: radar azymut/elewacja (240,232) r=68 - odpowiednik #sat-radar.
- MAPA: kontynenty rysowane z WBUDOWANEJ bitmapy 1-bit iss_worldmask.h (228x114),
  wygenerowanej offline z data/Mapa swiata.bmp przez tools/gen_worldmask.py
  (rozklad kolorow: woda = niebieski dominujacy, reszta = lad; downsample wiekszoscia).
  Bez zaleznosci od LittleFS i bez dekodowania obrazu na urzadzeniu. Po zmianie mapy
  uruchom ponownie tools/gen_worldmask.py.
- Paleta ISS_COL_* w main.cpp = dokladne odpowiedniki zmiennych CSS (--accent #35d0ba
  itd.) w RGB565. Zmiana motywu WWW -> przelicz RGB565.
- Alokacja sprite w PSRAM: TFT_eSprite domyslnie uzywa ps_calloc gdy psramFound()
  (TFT_eSPI 2.5.43, Extensions/Sprite.cpp). 480x320x2 = 307 200 B.
- drawIssStaticInterface()/updateIssDynamicDisplay() sa teraz aliasami
  issDrawFrameFromState() (pelny redraw). drawIssRadarBackground() usuniete.
- Podglad ukladu (mock w HTML): /workspace/share/tftmock2.html (nie w repo).

## ISS: n2yo visualpasses (widocznosc), czas lokalny, flagi kraju (13.10.2026)
- WIDOCZNOSC: oprocz radiopasses (elewacja) pobieramy z n2yo takze visualpasses
  (ISS oswietlony Sloncem + obserwator w ciemnosci). Nowa funkcja
  fetchIssVisualPassesFromN2yo() dopasowuje visual do radio po AOS (tolerancja
  3 min - rozne progi elewacji) i wypelnia issPassVisStart/VisEnd/Duration/Mag.
  Wywolywana po udanym radiopasses w fetchIssNextPassFromN2yo().
- LIMITY n2yo: visualpasses i radiopasses po 100 zapytan/godz. na klucz. Oba sa
  wolane razem co 5 min (lastIssPassEstimate) = 12+12/h - bezpiecznie w limicie.
  NIE zmniejszac interwalu ponizej ~3 min (24+24/h) - zapas musi zostac na retry.
- SGP4 (bez klucza n2yo) NIE liczy widocznosci - issPassVis* zostaja zerowe,
  UI pokazuje "widoczny: --".
- CZAS LOKALNY: AOS/LOS i okno widocznosci sa konwertowane przez
  getCachedTimezoneOffset() (timezoneHours + DST europejski) w issRenderFrame.
- TFT: karta NAJBLIZSZY PRZELOT zwiekszona do y=34..106 (3 linie: HH:MM,
  odliczanie, "wid. HH:MM-HH:MM" na WARN). Karta NASTEPNE PRZELOTY y=110..194;
  kazdy wiersz ma kropke widocznosci (WARN = widoczny optycznie, FAINT = radio).
  Karty AZYMUT/ELEWACJA/DYSTANS przesuniete na y=202.
- FLAGI: karta "KRAJ POD ISS" (lewa kolumna) rysuje flage z LittleFS
  /tft_flags/<CC>.bmp (32x21, 24-bit) przez issDrawFlagToSprite() - wlasny
  odpowiednik drawBmpFromFS(), bo tamten pisze do globalnego tft, a nie do
  sprite'a. Uklad jak w drawBmpFromFS: wiersze BMP od dolu, piksele BGR,
  kolor przez tft.color565() (konwersja kanalow jak przy flagach WWW).
  Nazwa kraju skracana do 7 znakow gdy flaga jest rysowana (inaczej 12).
  Brak pliku flagi = sam tekst (bez bledu).
- /api/iss_track: kazdy wpis "passes" to teraz
  [aos, los, maxEl, visStart, visEnd, duration_s, mag] (zera gdy brak n2yo).
  WWW (data/satellite.js) pokazuje okno widocznosci w tabeli i w "nastepny przelot".

## WiFi nie laczy sie po wczytaniu konfiguracji z pliku TXT — ROZWIAZANE (13.10.2026)
Objaw: po uzyciu "Wczytaj ustawienia z pliku TXT" w WWW ESP przestaje laczyc sie
z WiFi (zostaje w trybie AP) po restarcie.

Przyczyna (dwie warstwy, obie naprawione):
1. **Import zapisywal surowy podzbior, nie formularz.** `importSettingsFromFile()`
   robil `applyConfigToUI(importedConfig)` i potem `saveConfig(importedConfig)` —
   wysylal do `/api/save` dokladnie to, co bylo w pliku. Gdy TXT nie zawieral
   `wifi_ssid`/`wifi_pass` (np. backup z innej wersji albo czesciowy), payload ich
   nie mial.
   **Fix (WWW, `data/index.html` + `indexEN.html`):** import zapisuje pelny
   formularz: `saveConfig()` (bez argumentu → `collectConfigPayload()`), po tym jak
   `applyConfigToUI()` juz scalilo importowane wartosci.
2. **Firmware zerowal WiFi przy braku klucza.** `/api/save` robil bezwarunkowo
   `wifiSSID = doc["wifi_ssid"].as<String>()`; brak klucza w JSON = pusty String =
   skasowane SSID. To samo dla `wifi_pass`/`wifi_ssid2`/`wifi_pass2`.
   **Fix (firmware, `main.cpp`):** `if (doc.containsKey("wifi_ssid"))` przed
   przypisaniem — kazde z 4 pol nadpisywane tylko gdy klucz JEST w payloadzie.
   Dodatkowo `applyConfigToUI()` w WWW nadpisuje pola WiFi tylko dla kluczy
   obecnych w danych (`hasOwnProperty`), zeby czesciowy import nie czyscil
   formularza przed zapisem.

Bonus: warunek startu WiFi w `setup()` zmieniony z `wifiSSID.length() > 0` na
`wifiSSID.length() > 0 || wifiSSID2.length() > 0` — konfiguracja z samym SSID2
tez sie teraz laczy (wczesniej ignorowana).

Test regresji (jsdom, symulacja UI): `importSettingsFromFile()` z plikiem bez
kluczy WiFi → `/api/save` i tak zawiera zapisane wczesniej `wifi_ssid`/`wifi_pass`.


## WiFi: brak polaczenia ze stacja (status 6, skan -2) — DIAGNOZA + POPRAWKI (13.10.2026)
Objaw z logu (firmware z poprawka importu, index.html=162654):
`WiFi.status(): 6` (WL_DISCONNECTED) po 20 s, `Znaleziono sieci: -2`
(= WIFI_SCAN_FAILED), potem AP. Radio dziala (AP mode OK), wiec to nie sprzet.

Zmiany w `connectToWiFi()` (main.cpp):
1. **Handler zdarzen WiFi** (`WiFi.onEvent`, rejestrowany raz) loguje
   `[WIFI EVT] DISCONNECTED reason=<n> (<nazwa>)` — np. `NO_AP_FOUND` (201),
   `AUTH_EXPIRE`, `4WAY_HANDSHAKE_TIMEOUT`, `HANDSHAKE_TIMEOUT`. To rozstrzyga,
   czy problem to zasieg (201), zle haslo (15/204), czy autoryzacja.
2. **`WiFi.setMinSecurity(WIFI_AUTH_OPEN)`** — domyslne `WIFI_AUTH_WPA2_PSK`
   w arduino-esp32 2.0.x odrzuca sieci z autoryzacja mieszana (WPA/WPA2),
   WPA/WPA3 albo otwarte. `WiFi.printDiag` tego nie pokazuje.
3. **`WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN)` + `setSortMethod(...BY_SIGNAL)`**
   — pewniejsze wykrycie sieci niz domyslny fast-scan.
4. **Skan przed polaczeniem** (`findNetwork`): loguje RSSI/kanał/typ szyfrowania
   znalezionej sieci; jesli znany kanal → `WiFi.begin(ssid, pass, channel)`
   (pomija wewnetrzny skan begin()). Skan zwracajacy -2 nie przerywa proby.
5. Log `[WIFI SCAN] <ssid> NIEWIDOCZNA` / `widoczna: RSSI=.. ch=.. enc=..`.

Interpretacja kolejnego logu: jesli teraz pojawi sie
`[WIFI EVT] DISCONNECTED reason=201 (NO_AP_FOUND)` → siec poza zasiegiem/5 GHz
albo zly SSID. `reason=15` → zle haslo. `reason=205` (CONNECTION_FAIL) → router.

UWAGA: blok "TYMCZASOWA NAPRAWA KONFIGURACJI" w `setup()` (main.cpp ~21519)
ma na sztywno `Multiplay_C984` jako "poprawna" siec — do usuniecia docelowo.

### Wynik z realnego urzadzenia (log 18:20, firmware z diagnostyka)
```
[SKANOWANIE WIFI] Multiplay_C984 widoczna: RSSI=-60 ch=4 enc=3  (WPA2, silny sygnal)
[WIFI EVT] ROZŁĄCZONO powód=2 (AUTH_EXPIRE)  x5
[WIFI EVT] ROZŁĄCZONO powód=201 (NO_AP_FOUND)
WiFi.status(): 1
```
Czyli: SSID widoczny, silny sygnal, standardowe WPA2 — ale autoryzacja wygasa.
To NIE zasieg i NIE 5 GHz i NIE zle haslo (enc=3, haslo z NVS 15 znakow).
`AUTH_EXPIRE` (2) powtarzane przy pelnej mocy to klasyczny objaw problemu
zasilania/TX-power (skok pradu przy nadawaniu) albo routera (Smart Connect,
PMF/WPA3 transition, deauth). Ten board juz ogranicza prad w AP
(`setTxPower(WIFI_POWER_11dBm)` w `startAPMode`, komentarz o brownoucie).

### Poprawka: drabinka mocy TX (`wifiAttemptAtPower`)
`attemptConnect()` probuje kolejno `WIFI_POWER_19_5dBm` → `15dBm` → `11dBm`,
kazda po 9 s; przy `reason=201` dodatkowa powtorka na tym samym poziomie.
Gdy ktoras moc zadziala, laczy sie i konczy. Loguje `[WIFI] Proba, moc=idx` i
`[WIFI] Nieudane przy mocy idx=N, reason=R`.

Jesli po tej poprawce nadal nie laczy — problem jest po stronie routera/zasilania:
- w routerze: wylaczyc "Smart Connect"/band steering, ustawic WPA2 (nie WPA2/WPA3 mixed),
  sprawdzić liste MAC (filtr), kanal 1-11, ewentualnie osobne SSID dla 2.4 GHz,
- na urzadzeniu: sprawdzic zasilanie (USB 5 V/2 A, kondensator, slabosc 3V3),
  poluzowana antena (community: "antena byla luzna").




### Log 18:45 - drabinka mocy NIE pomogla => problem poza firmware
```
[WIFI] Proba, moc=78 (idx 0) -> AUTH_EXPIRE (2)
[WIFI] Proba, moc=60 (idx 1) -> AUTH_EXPIRE (2)
[WIFI] Proba, moc=44 (idx 2) -> AUTH_EXPIRE (2), potem AUTH_FAIL (202)
```
AUTH_EXPIRE przy WSZYSTKICH mocach (19,5/15/11 dBm) wyklucza brownout/zasilanie.
Ostatni skan zwrocil -2 (radio zajete), stad falszywe "Nie widze Twojego SSID".
`AUTH_EXPIRE -> AUTH_FAIL` przy widocznej sieci (RSSI=-53, ch=4, enc=3) to wg
spolecznosci ESP klasyczny objaw ODRZUCANIA stacji przez AP:
- band steering / "Smart Connect" (router wypycha 2,4 GHz klienta na 5 GHz),
- tryb WPA2/WPA3-mixed + PMF (transition mode),
- filtr MAC / izolacja klienta,
- albo po prostu zle haslo (router nie rozroznia "zle haslo" od "auth timeout").

Nie ma reguly kodowej, ktora to naprawi - to konfiguracja routera/hasla.
Dodano tylko przypinanie do BSSID (`begin(ssid,pass,ch,bssid)`) jako obejscie
band-steeringu. TEST ROZSTRZYGAJACY: polaczyc sie z `Multiplay_C984` telefonem
z haslem `ZTEEQFWL7H01091` (z logu). Telefon tez nie laczy => haslo/polityka AP.
Telefon laczy => interop ESP-router: wylaczyc Smart Connect, osobne SSID 2,4 GHz,
WPA2-PSK(AES) bez WPA3/PMF, sprawdzic antene (RX dziala = skan, TX moze nie).


### ROZSTRZYGNIECIE (potwierdzone przez uzytkownika): hotspot telefonu DZIALA
ESP laczy sie bez problemu z siecia komorkowa (hotspot telefonu). To definitywnie
zamyka watek: **firmware i sprzet sa sprawne**, problem jest tylko z AP
`Multiplay_C984`. Potwierdza to wczesniejsza diagnoze: AUTH_EXPIRE -> AUTH_FAIL
przy widocznej silnej sieci = router odrzuca stacje (band steering / Smart Connect,
WPA2/WPA3-mixed + PMF, filtr MAC, izolacja klienta) albo nieaktualne haslo.

Wniosek: NIE ma juz czego naprawiac w kodzie. Naprawa jest w routerze:
1. Wylaczyc "Smart Connect" / band steering (kluczowe).
2. Ustawic WPA2-PSK (AES) - nie "WPA2/WPA3 mixed"; PMF off.
3. Sprawdzic filtr MAC / izolacje klienta; dopisac MAC ESP.
4. Kanal 1-11 recznie; najlepiej osobne SSID dla 2,4 GHz.
5. Upewnic sie, ze haslo w routerze = to w panelu ESP.

Kod diagnostyczny (handler zdarzen, findNetwork, przypinanie BSSID,
setMinSecurity(OPEN)) zostaje jako wartosciowa diagnostyka/odpornosc.
Drabinka mocy TX okazala sie nieskuteczna dla tego przypadku (AUTH_EXPIRE przy
kazdej mocy) - kandydat do uproszczenia/usuniecia, bo dodaje ~27 s opoznienia,
gdy AP jest nieosiagalny.


## EKRAN APRS RADAR (SCREEN_APRS_RADAR = 9) - nowy render taktyczny
Zastapiono stary ekran APRS RADAR renderem opartym o TFT_eSprite 480x320
w PSRAM (wzorzec jak ekran ISS):
- `aprsRadarSprite` + `ensureAprsRadarSprite()` - alokacja w PSRAM, jeden
  `pushSprite(0,0)` na klatke => brak migotania i resztek.
- `aprsRadarRenderFrame(angleDeg, advanceBlips)` - rysuje pelna klatke.
  advanceBlips=false przy pelnym odswiezeniu (drawAprsRadarBody), true w animacji.
- `aprsRadarTick()` wolane z `UI_Task` (NIE z `loopTask`!) co ~100 ms gdy ekran
  aktywny; obraca smuge (`aprsRadarSweepAngle += 4`) i zapala blipy
  (`aprsRadarBlip[]`). Wolanie z `loop()` powodowalo reset: `pushSprite` z dwoch
  zadan na tym samym SPI zawieszalo magistrale. `aprsRadarRenderFrame` dodatkowo
  bierze mutex `aprsRadarSpriteMux` (pomija klatke zamiast czekac).
- Geometria: ARP_CX=127, ARP_CY=158, ARP_RADIUS=122, panel ARP_PANEL_X=262.
- Kolory taktyczne `aprsRadarStationColor()`: zolty=mobilne, cyjan=WX,
  magenta=digi/repeater/node/LoRa, bialy=stale (wg `getAPRSSymbolShort`).
- Zakresy: `APRS_RADAR_RANGE_MULT[] = {0.2,0.5,1,2,4}` - mnozniki promienia
  filtra APRS-IS (`aprsFilterRadius`, max 50 km; serwer wysyla tylko stacje w
  tym promieniu). Srodek radaru = `userLat/userLon` z fallbackiem 52.40/16.92
  (ten sam co `station.distance`).
- Kafelki zakresu (x0.2..x4) wysrodkowane na dole (`ARP_TILE_*`,
  `aprsRadarTileStartX()`, `isScreen6RadarRangeTileHit()`); dotkniecie
  konkretnego kafelka ustawia zakres.
- Panel boczny: QTH, STATUS (WiFi/APRS-IS), VFO 144.800, ZAKRES + lista stacji
  z odlegloscia, zegar UTC.

UWAGA (nawigacja/dotyk): XPT2046 dzieli magistrale SPI z TFT, wiec ciagly
`pushSprite` zaklocal odczyt dotyku i "zajadal" tapniecia (nie dalo sie przejsc
na inny ekran / otworzyc menu). Dlatego animacja jest wstrzymywana na ~500 ms od
ostatniej aktywnosci dotyku (`screenSleepLastActivityMs`). Przycisk menu
(hamburger) rysowany jest do sprite'a PRZED `pushSprite`
(`drawHamburgerMenuButton3DSprite`) - rysowanie go na `tft` po pushSprite
powodowalo miganie, bo "niebrudzacy" pushSprite zmazywal go co klatke.

UWAGA (pozycja/QTH): `userLatLonValid` + `userLat/userLon` steruja srodkiem
radaru, filtrem APRS-IS i odleglosciami. Formularz WWW wysyla puste pola jako
`parseFloat(...)||0` => `(0,0)`. Dlatego `(0,0)` jest traktowane jako BRAK
pozycji (applyConfigJson, loadPreferences i srodek radaru), a wtedy pozycja
liczona jest z `userLocator`. Bez tego puste pola dawaly srodek na oceanie i
radar byl pusty. Panel QTH pokazuje CALL + LOKATOR + SZER/DLUG.

UWAGA: jesli PSRAM/sprite sie nie zaalokuje, ekran nie rysuje sie wcale
(`ensureAprsRadarSprite` zwraca false) - to celowe, zeby nie mieszac sprite
z bezposrednim rysowaniem do tft. Sprawdz `[APRS RADAR] Brak sprite...` w logu.

## EKRAN 11: INFORMACJE SYSTEMOWE (SCREEN_SYSTEM_INFO = 11)
Zastapil ekran "Unlis Hunter" (gra usunieta). Dashboard telemetrii w stylu
"modern dark": `drawSystemInfo()` rysuje szkielet (naglowek + 4 karty),
`updateSystemInfo()` odswieza metryki i wykres co 1 s z `UI_Task`.
- Karty: HARDWARE & PROCESSOR (MCU/arch/clock/CPU temp/flash+PSRAM),
  MEMORY & NETWORK (free heap, PSRAM free, WiFi status/RSSI/IP),
  CPU TEMP HISTORY (wykres 45 probek, os 40..60 C), SYSTEM LOGS.
- Temperatura: `temperatureRead()` (Arduino ESP32 core); jesli NaN/<0 ->
  wartosc zastepcza. Kolor progu: zielony <55, zolty 55-65, czerwony >=65.
- Ekran tylko do odczytu (brak menu). Nawigacja w rogach jak na innych
  ekranach; stary kod "unlishunter" nadal mapowany na ten ekran (kompatybilnosc).
- Wykres i metryki rysowane bezposrednio na tft (bez sprite) - aktualizacje
  co 1 s, wiec nie ma ciaglego ruchu po SPI jak przy radarze.

## WWW: lista "Ekrany TFT (kolejnosc)" - 14 pozycji
Pelne UI jest serwowane z LittleFS (`data/index.html` / `data/indexEN.html`),
NIE z wbudowanego stringa w main.cpp. Lista kolejnosci ekranow ma 14 pozycji
(`screen_slot_1..14`, `screenOptionsDefault` dlugosci 14, petle JS `i < 14`).
Opcje: 14 ekranow + OFF (w tym `pskmap`, `systeminfo`, `iss`). Kody musza byc
zgodne z `screenCodeToType()` w firmware (`bandinfo`, `sunspots`, `systeminfo`).
UWAGA: stary kod w main.cpp (`SCREENS`, `screenOrderRow`) jest martwy - nie
edytuj go, bo nie jest serwowany. Po zmianie `data/*.html` trzeba przebudowac
LittleFS (`pio run -t buildfs`) i wgrac image FS, nie tylko firmware.

## Nawigacja ekranow: NIE dodawaj wczesnego `return` per-ekran
`handleTouchNavigation()` obsluguje menu, popupy, potem nawigacje w rogach
(lewo/prawo). Wczesny `return` dla danego ekranu (jak bylo dla SYSTEM_INFO)
zjada dotkniecia ZANIM dojdzie do nawigacji - ekran staje sie "uwieziony".
Ekran tylko-do-odczytu nie potrzebuje zadnego bloku w handleTouchNavigation.

## ISS: radar AZ/EL, wysrodkowany naglowek, wymuszone odswiezanie przelotow (13.10.2026)
- RADAR AZYMUT/ELEWACJA nie pokazywal danych, gdy ISS byl POD HORYZONTEM:
  stary warunek `if (el >= 0 && az >= 0)` wpadal w galaz "brak danych" (el < 0
  niemal caly czas, bo ISS widoczny tylko kilka min/przelot). Teraz: `hasData`
  -> nad horyzontem znacznik wg azymutu/promienia elewacji, pod horyzontem
  znacznik w srodku (nadir) + etykieta "AZ xx  EL yy". "brak danych" tylko
  gdy naprawde brak pozycji.
- Naglowek: "ESP32-HAM-CLOCK" i "ISS TRACKER" wysrodkowane (TC_DATUM @ x=240).
- Panel "NAJBLIZSZY/NATEPNE PRZELOTY" byl pusty po wejsciu na ekran, bo plan
  liczony jest w issTask co 5 min / po TLE (TLE startuje dopiero po 90 s).
  Dodane `volatile bool issForceNextPassRefresh`: ustawiane przy wejsciu na
  ekran (drawScreen) i po swiezym TLE; issTask obsluguje je natychmiast,
  nie czekajac na 5-minutowy interwal.
- JASNOSC: pole "Nocne obnizenie jasnosci" w WWW (tft_night_dimming) bylo
  ignorowane przez firmware. Teraz zapisywane do pref `tft_night_dim`,
  odtwarzane, i stosowane przez `applyEffectiveBacklight()` (osobne
  `setBacklightPercentRaw()` NIE nadpisuje bazowego `backlightPercent`).
  Noc = ikona OWM "n" albo brak ikony i godzina lokalna poza 6-18; loop
  przelicza PWM przy zmianie pory dnia. Max jasnosc = 100% (0xFFFF duty);
  slider WWW ogranicza do min 10%.

## KOLOR ZNAKU: WWW + przytrzymanie palca na znaku (13.10.2026)
Kolor znaku wywolawczego (`callsignColor`, pref `callsign_color`, RGB565,
domyslnie 0xFFFF bialy) mozna teraz ustawic na dwa sposoby:

1. WWW (pelne UI z LittleFS): `data/index.html` / `data/indexEN.html`, sekcja
   "Twoja Stacja" - `<input type="color" id="callsign_color_picker">` +
   podglad HEX i przycisk "Domyslny (bialy)". JS: `rgb565ToHex`/`hexToRgb565`
   (konwersja 5-6-5 <-> #RRGGBB), `resetCallsignColor()`. Pole trafia do
   payloadu `/api/save` jako `callsign_color` (int 0..65535).
   UWAGA: serwowane jest `data/index.html` z LittleFS; wbudowany string
   `getMainHTML()` w main.cpp ma tylko pole `callsign_color` (int) - to
   fallback, gdy brak pliku w FS.
2. TFT: przytrzymanie palca NA ZNAKU na ekranie HAM CLOCK (>=1200 ms) otwiera
   menu koloru (`openCallsignColorMenu`). Trzy suwaki HSV: hue (0..255),
   nasycenie (0..100%), jasnosc (0..100%) + podglad znaku i przyciski
   ZAPISZ/DOMYSLNE/ZAMKNIJ. `hsv565()` (HSV->RGB565), `rgb565ToHSV()` (start
   suwakow od aktualnego koloru). Suwaki rysowane bez `fillScreen`
   (`redrawCallsignColorControls`), zeby nie migalo przy przeciaganiu.
- Obszar znaku: `callsignHitX/Y/W/H` zapisywane w `drawHamClock()`.
- Wykrywanie: w `handleTouchNavigation()` PRZED galezia `brightnessMenuActive`,
  po 1200 ms trzymania (dluzsze 3000 ms nadal otwiera menu jasnosci).
  `longPressHandled` blokuje ponowne otwarcie. Auto-przelaczanie ekranow
  wstrzymane, gdy `callsignColorMenuActive`.
- Klawisze tlumaczen: `TR_CALLSIGN_COLOR`, `TR_SATURATION`, `TR_VALUE`
  (dodane na koncu enum i tablic TR_PL/TR_EN - kolejnosc musi sie zgadzac).
- UWAGA (naprawione): `hsv565()` liczy kanaly w skali 0..255 przed `rgb565()`
  (nie 0..100!) - inaczej 100% jasnosci dawalo ~39% ("polowa jasnosci").
  Domyslne suwaki: nasycenie 100%, jasnosc 100%.


## PSK Reporter: naprawa parsera API - mapa nie dzialala (14.10.2026)
Objaw: ekran PSK MAP rysowal mape i tylko czerwona kropke usera, zero spotow
i linii DX. Przyczyna: `fetchPskReporterData()` czytal pola, ktorych API juz
nie zwraca (stary schemat). Realna odpowiedz `retrieve.pskreporter.info/query`
(json=1) to:
- tablica `receptionReport` (NIE `receptions` ani `rx`),
- pola `senderCallsign`, `senderLocator`, `frequency`, `mode`, `sNR`
  (NIE `callsign`/`locator`/`snr`).
Naprawa:
- `doc["receptionReport"]` zamiast `receptions`/`rx`; filtr strumieniowy
  wskazuje te same realne nazwy pol.
- Pole snr czytane z `sNR`.
- Filtr nadawcy: `senderCallsign=` (bylo `transmitterCallsign=` - ignorowane).
- Limit: `rptlimit=` (bylo `maxrows=` - ignorowane w trybie JSON); dodane
  `rronly=1&noactive=1`. Bez tego API zwraca CALA tabele (~1,4 MB) i sygnalizuje
  obciecie w `receptionReportLimited`. Z rptlimit=50 odpowiedz ~16 KB.
- `DynamicJsonDocument doc` zwiekszony 8192 -> 24576 B (100 rekordow x ~250 B).
- Rate-limit: API przy zbyt czestych zapytaniach zwraca
  `{"message":"Your IP has made too many queries too often..."}` bez tablicy.
  Wykrywane przez `!doc.containsKey("receptionReport")` -> `pskRateLimited=true`,
  a naglowek mapy pokazuje "LIMIT PSKReporter - poczekaj" (czerwony) lub
  "Spoty: N" (zielony).
- `drawPskMap()` pobiera dane PRZED rysowaniem naglowka, zeby status byl aktualny.
- API jest wrazliwe na czestotliwosc zapytan - przy testach z jednego IP latwo
  o "too many queries"; odpytywac z umiarem (auto-refresh >= 5 min).

### Burza zapytan / utrwalony limit 429 (14.10.2026, z logu urzadzenia)
Objaw z logu: jedno tapniecie ZAPISZ wywolywalo 3-4 zapytania PSK, a kolejne
redrawy ciagle dostawaly HTTP 429. Przyczyna: bramka w `drawPskMap()` brzmiala
`pskSpotCount == 0 || po interwale` - po nieudanym fetchu spoty zostaja 0, wiec
KAZDY redraw ponawial zapytanie (ZAPISZ sam robil fetch + draw + delay + draw).
To zamienialo jeden blad w burze i nie pozwalalo limitowi 429 minac.
Naprawa:
- `pskBackoffUntilMs` + `PSK_RATE_LIMIT_BACKOFF_MS` (5 min). HTTP 429 ORAZ
  odpowiedz 200 z samym `message` (bez receptionReport) ustawiaja backoff.
- Bramka: fetch tylko gdy `lastPskFetchMs == 0` (pierwszy raz) albo po interwale,
  i tylko gdy `millis() >= pskBackoffUntilMs`. Bez tego "0 spotow" nie ponawia.
- ZAPISZ: zamiast `fetchPskReporterData()` na sile, `lastPskFetchMs = 0` -
  drawPskMap pobierze, ale z poszanowaniem backoffu.
- Okno czasu: `flowStartSeconds` clampowane do 86400 s (24 h). API i tak po cichu
  obcina dluzsze okna (`receptionReportLimited.oldestRequested`), a pskReportDays
  domyslnie 3 -> wczesniej wysylalismy -259200.
- Symulacja bramki (12 redrawow przy padajacym API): stare 12 fetchy -> nowe 2.
- REGRESJA (z kolejnego logu): ZAPISZ przestal pobierac dane, bo po zmianie
  polegal na bramce drawPskMap, ktora siedzi w `if (pskAutoRefreshMinutes > 0)`.
  Przy auto-refresh=0 nic sie nie pobieralo. Naprawa: ZAPISZ robi jawny
  `fetchPskReporterData()` (z poszanowaniem backoffu), a pierwszy fetch w
  drawPskMap jest niezalezny od auto-refresh (pierwsze wejscie na ekran zawsze
  pobiera; kolejne wg interwalu, gdy auto-refresh > 0).



## Strona WWW urzadzenia: dodawanie linkow (14.10.2026)
Sekcja "Polecane" (PL) / "Recommended" (EN) jest w tabeli links-desc-table:
- PL: data/index.html (linia ~1095)
- EN: data/indexEN.html (linia ~1194)
- download/index.html to STARA kopia opublikowanej strony (przed v1.2, brak
  pskmap) - ma tez sekcje "Polecane"; dodajac link, zaktualizuj tez ta kopie.
Dodano: ekalk.eu -> "Bezpłatne kalkulatory online dla elektroników i radioamatorów"
(EN: "Free online calculators for electronics engineers and radio amateurs").
Po zmianie data/ trzeba przebudowac LittleFS (pio run -e esp32-s3-n16r8 -t buildfs),
bo WWW jest wgrywane z LittleFS - inaczej zmiana nie trafi na urzadzenie.
