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


## Autor / naglowki WWW (v1.4.3, 12.10.2026)
- Naglowek na WSZYSTKICH stronach WWW = "ESP32-HAM-CLOCK 1v"
  (title, app-title, h3 na stronie LINKI, h2 na stronie Konfiguracja) - PL i EN.
- Stopka (PL): "Autor oprogramowania Tomasz SP9TNV na podstawie projektu Konrad
  Wisniewski SP3KON." + "Oprogramowanie zostalo zmodyfikowane przez SP9TNV na
  podstawie projektu Konrad Wisniewski SP3KON (z uzyciem AI)." + "Obecna wersja
  oprogramowania ESP32-HAM-CLOCK 1v z dnia 12.10.2026." + "Kontakt: SP9TNV@gmail.com".
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
