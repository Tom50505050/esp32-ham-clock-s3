# ESP32-HAM-CLOCK — informacje o wydaniach

Dokument obejmuje wydania **v1.2**, **v1.2a**, **v1.2b**, **v1.2c** i **v1.2d** dla platformy
**ESP32-S3 N16R8** (16 MB flash, 8 MB PSRAM) z wyświetlaczem
**ILI9488 na 3.5" i 4" 480×320** po SPI i panelem dotykowym XPT2046.

> **Wgrywanie przez przeglądarkę (WebSerial):**
> https://jason2866.github.io/WebSerial_ESPTool/ — wybierz **`merged-flash.bin`**
> (płytka z dotykiem) albo **`merged-flash-notouch.bin`** (bez dotyku), offset `0x0`.
> W wydaniach dostępne są **tylko te dwa pliki** — osobne obrazy `firmware.bin`,
> `littlefs.bin`, `bootloader.bin`, `boot_app0.bin` i `partitions.bin` zostały
> usunięte ze wszystkich wydań.

---

# ESP32-HAM-CLOCK v1.2d — czcionki zegara, alert pogodowy i migotanie

Wydanie **v1.2d** (23.10.2026) dla platformy **ESP32-S3 N16R8** (16 MB flash, 8 MB
PSRAM) + **ILI9488 na 3.5" i 4" 480×320** + XPT2046. Zakres wydania:
- **wybór czcionki** znaku (callsign) i dużego zegara — 14 krojów + wbudowana,
- **własne grafiki alertu pogodowego** (burza / wiatr) wypełniające **cały ekran**
  480×320 (fit=cover, bez czarnych pasów) z podglądem 1:1 w WWW,
- **naprawa migotania cyfr zegara** dla wszystkich czcionek.

## Wybór czcionki znaku i zegara
### Menu czasu — wybór czcionki (15 opcji)
- W menu **TIME MODE** (ekran HAM CLOCK) doszedł wybór czcionki w siatce 5×3:
  **DOMYSLNA** (wbudowana TFT_eSPI) oraz 14 krojów: Sans Bold, Orbitron,
  Oxanium, Michroma, Audiowide, Russo One, Tech Mono, Quantico, Electrolize,
  Bungee, Nova Mono, Iceland, Serif Bold, Mono Bold.
- Nad siatką jest **podglad na żywo**: znak (`SP5ABC`) i godzina (`12:00`)
  rysowane aktualnie wybraną czcionką, więc efekt widać przed zamknięciem menu.
- Wybór działa **natychmiast** i jest zapisywany w NVS (`clock_font`).
  Ten sam numer można ustawić z panelu WWW (pole **Czcionka zegara (0-14)**).
- Nowe czcionki to pliki **VLW** (TFT_eSPI Smooth Font) w `data/fonts/`
  (`clockNN-call.vlw` dla znaku, `clockNN-time.vlw` dla zegara) — łącznie ~0,85 MB,
  mieści się w partycji LittleFS.

### Generator czcionek
- Nowe narzędzie `tools/make_clock_fonts.py` pobiera kroje (Google Fonts, licencja
  OFL) i generuje oba pliki VLW dla każdej opcji, dobierając rozmiar tak, aby
  `00:00:00` oraz długi znak zmieściły się w 480 px.
- Nowe narzędzie `tools/render_clock_fonts_preview.py` renderuje planszę poglądową
  wszystkich czcionek (kontrola QA bez sprzętu) — wynik: `clock_fonts_preview.png`.

### Rysowanie znaku i zegara
- Znak i godzina rysowane są wybraną czcionką, wyśrodkowane poziomo i pionowo w
  swoim pasie; duży zegar używa `setTextPadding`, więc aktualizacja co sekundę
  nie zostawia „duchów" po zmianie cyfr.
- **Poprawka pozycjonowania (22.10.2026)**: zamiast `print()` używane jest
  `drawString()` z `TC_DATUM`. `print()` **ignoruje** `setTextDatum`, więc znak i
  godzina były przesunięte w prawo o połowę szerokości (nie wyśrodkowane), a
  kursor startował od góry pola czcionki (napis zjeżdżał w dół pasa).
  `drawString()` honoruje `TC_DATUM` (poziome wyśrodkowanie) i `setTextPadding`.
- Pion liczony jest od **rzeczywistego atramentu** glifów (`clockInkOffset`), a nie
  od całego pola czcionki — dzięki temu każdy krój jest wyśrodkowany tak samo, bo
  `ascent`/`descent` różnią się między czcionkami. Zweryfikowano dla wszystkich
  15 opcji: znak w pasie 50–94 px, zegar w pasie 126–178 px, bez przycięć.
- Pasek podglądu w menu czasu ma teraz pełną wysokość 62 px (najwyższe kroje mają
  ~60 px) i własny, poprawiony układ — wcześniej 20 px ucinało podgląd.

---

## Alert pogodowy — własne grafiki (burza / wiatr)
### Panel WWW — zakładka „ALERT POGODOWY” (6 języków)
- Nowa sekcja **Grafiki alertu (burza / wiatr)** z dwoma polami wyboru pliku
  (JPG / PNG / BMP / GIF / WEBP), przyciskami **Wgraj** i **Domyślna** oraz
  miniaturami aktualnych ikon.
- **Podgląd całego ekranu alertu** na kanwie 480×320 — dokładnie tak, jak pokaże
  to TFT. Grafika **wypełnia cały ekran** (fit=cover, brak czarnych pasów), a
  napisy alertu i przycisk zamknięcia rysowane są na jej wierzchu.
- Dowolny obraz jest skalowany do **480×320** (dopasowanie z zachowaniem
  proporcji — pokrycie całego ekranu, nadmiar przycinany symetrycznie) i
  konwertowany w przeglądarce do **BMP 16-bit RGB565** przed wysłaniem na
  urządzenie — bez potrzeby ręcznej konwersji plików.

### Firmware
- Ikony alertu pochodzą teraz z plików w LittleFS. Ścieżki trzymane są w NVS
  (`wxal_ic_s`, `wxal_ic_w`) i eksponowane w `GET/POST /api/config` jako
  `weather_icon_storm` / `weather_icon_wind`.
- **Grafika wypełnia cały ekran 480×320** (fit=cover — pokrycie bez czarnych
  pasów, nadmiar przycinany symetrycznie, małe obrazy powiększane). Napisy
  alertu i przycisk zamknięcia rysowane są na wierzchu grafiki.
- **Domyślnie:** `/icon50/burza.bmp` (burza) i `/icon50/wiatr.bmp` (wiatr)
  w rozmiarze **480×320** (pełny ekran).
  Wgranie własnej grafiki zapisuje `/wxicon_storm.<ext>` / `/wxicon_wind.<ext>`;
  przycisk „Domyślna” przywraca ikony z `data/icon50`.
- Nowe trasy: `/upload_wxicon_storm`, `/upload_wxicon_wind` (RAW POST),
  `/wxicon_preview` (podgląd) i `/wxicon_reset` (powrót do domyślnej).
- Gdy pliku ikony brak, ekran alertu rysuje wbudowaną ikonę wektorową (jak dotąd).

### Nowe narzędzia
- `tools/make_weather_icons.py` — generuje domyślne `data/icon50/burza.bmp`
  i `data/icon50/wiatr.bmp` (24-bit BMP **480×320**, pełny ekran).
- `tools/render_weather_alert_preview.py` — renderuje podgląd QA ekranu alertu
  (burza + wiatr) do pliku PNG.

---

## Naprawa migotania cyfr zegara (wszystkie czcionki)
Objaw: przy każdej zmianie sekundy migały niektóre cyfry zegara. Dwie przyczyny:
- **Przerysowanie całego zegara co sekundę.** `clockDrawMonospace()` czyścił i
  rysował wszystkie 8 komórek przy każdym odświeżeniu. Teraz pamięta poprzednią
  godzinę (`prevText`) i dotyka **tylko komórek, które się zmieniły** (zwykle 1
  zamiast 8, ~67% mniej pikseli na SPI). `useCache=false` (podgląd w menu) wymusza
  pełne rysowanie; pamięć unieważniana przy `fillScreen` (`clockMonoGeneration`)
  i gdy chwilowo brak czcionki VLW (`clockMonoInvalidate()`).
- **Pionowe drganie 1 px.** Korekta pionowa liczona z bieżącej godziny
  (`clockInkOffset(text)`) — różne cyfry mają różną wysokość atramentu (BUNGEE
  `clock10`, MONO BOLD `clock14`: „1" i „8" różnią się o 1 px). Teraz
  `clockTimeInkOffset()` liczy offset ze stałego zestawu `"0123456789:"`.
- `updateScreen1ClockClassic()` rysuje tylko gdy tekst godziny się zmienił
  (wcześniej przerysowywał w każdej iteracji `loop()`).
- Podgląd QA: `tools/render_clock_flicker_preview.py`.

## Poprawki ekranu DX SPOTS i pin dzielnika baterii
- **Nazwy krajów na ekranie DX SPOTS (`dxspots.com`).** Cluster telnet nie
  przesyła pola kraju, a dotychczas kolumna „Country" opierała się wyłącznie na
  (wolnym, opcjonalnym) zapytaniu QRZ — bez skonfigurowanego QRZ pozostawała
  pusta (`-`). Dodano `countryFromCallsignPrefix()` w `main.cpp`: kraj jest
  wyprowadzany lokalnie i natychmiast z prefiksu znaku (dopasowanie
  **najdłuższego** prefiksu, m.in. `SP→POLAND`, `DL→GERMANY`, `K/W/N→USA`,
  `JA→JAPAN`), analogicznie jak ekran POTA korzysta z `locationDesc`. QRZ nadal
  może nadpisać/uzupełnić kraj, gdy jest dostępny.
- **Pin dzielnika napięcia baterii: GPIO 1 → GPIO 7** (ADC1_CH0 → ADC1_CH6).
  `BATTERY_ADC_PIN` zmieniony na **7** w `main.cpp`; zaktualizowany schemat
  dzielnika na stronie WWW (`getMainHTML`), `PINOUT.md`, `README.md`,
  `README_S3.md` i `INSTRUKCJA.md`.
- `tools/make_zip.py` jawnie pomija lokalny serwer-atrapę `mock_openmeteo.py`
  (nie trafia do paczki wydania).

## Wersja
- `FIRMWARE_VERSION`: `1.2c` → **`1.2d`**. Zaktualizowane też `data/index*.html`
  (PL/EN/DE/FR/ES/RU), `tools/lang_pages.json` i `platformio.ini`.

---

# ESP32-HAM-CLOCK v1.2c — poprawki PSK MQTT i nagłówka

Wydanie **v1.2c** (18.10.2026) dla platformy **ESP32-S3 N16R8** + **ILI9488 na 3.5" i 4" 480×320**
+ XPT2046. Zakres: **naprawa PSK MQTT (brak spotów po zmianie pasma/trybu)**,
**przesunięcie ikony baterii w lewo na wszystkich ekranach** oraz synchronizacja
narzędzia podglądu nagłówka. Pozostałe ekrany, zależności i układ partycji bez zmian.

## Zmiany od v1.2b

### Naprawa: PSK MQTT — zmiana pasma nie ściągała spotów
- Realny temat brokera PSK Reporter ma **11 segmentów**, a subskrypcja budowała
  filtr **6-segmentowy** (`.../<pasmo>/<tryb>/+/+/+`), który **nie pasował do
  żadnego tematu** — po wybraniu pasma mapa zostawała pusta (wyglądała na
  zawieszoną). Subskrypcja zmieniona na **`.../<pasmo>/<tryb>/#`**.
- Dodano mapowanie pasm **160 / 30 / 17 / 12 m** w obu ścieżkach (HTTP i MQTT).
- Poprawka filtra odbiorcy: czytany był indeks **5** (nadawca) zamiast **6**
  (odbiorca).
- Po zapisie filtra **czyszczone są spoty** i wymuszane przerysowanie mapy.
- Przy resubskrypcji **odsubskrybowywany jest stary temat** — koniec spotów
  napływających jednocześnie z dwóch pasm.

### Nagłówek — ikona baterii przesunięta w lewo
- Ikona baterii przesunięta **12 px w lewo** na wszystkich ekranach:
  zegar (klasyczny i nowoczesny) `drawBatteryIcon(screenW-70, 8)` →
  **`screenW-82, 8`**, wspólny nagłówek `uiHeader` `drawBatteryIcon(420, 1)` →
  **`408, 1`**.
- `rightLimit` nagłówka z **415** na **403** (napis nie nachodzi na ikonę).
- `drawBatteryQuickUpdate()` — pozycje lustrzane zaktualizowane do tej samej
  geometrii (bez migania przy odświeżaniu MQTT).

### Narzędzia (`tools/`)
- `render_header_preview.py` — usunięte zaszyte stare współrzędne baterii
  (410/420, `screenW-120`), zsynchronizowane z firmware (**398 / 408 /
  `screenW-82`**); usunięty nieistniejący tekst „%” w podglądzie quick-update.

### Instrukcja w 6 językach + ikona satelity + opis ekranów
- **Ikona satelity GPS** przy zmianie stanu zmienia kolor: **zielona**
  (`0x07E0`) po uzyskaniu fixu (ESP32 połączony z satelitami), bursztynowa gdy
  szuka, szara bez modułu. Odświeżana na bieżąco (`refreshGpsIconIfChanged`).
- **Manual w językach PL / EN / DE / FR / ES / RU** — dodano
  `data/manualDE.txt`, `manualFR.txt`, `manualES.txt`, `manualRU.txt`
  (pełne tłumaczenia) oraz trasy `/manualDE.txt` … `/manualRU.txt`.
  Zakładka „Instrukcja” wczytuje manual w języku wybranym w WWW.
- **Opis funkcji wszystkich ekranów** (14 ekranów TFT) + lista ikon nagłówka
  w sekcji 4 każdego manuala; liczba slotów 11 → **14**.
- **Pin dzielnika napięcia baterii: GPIO 1 → GPIO 7.** `BATTERY_ADC_PIN`
  zmieniony z **1** (ADC1_CH0) na **7** (ADC1_CH6) w `main.cpp`; zaktualizowany
  schemat dzielnika na stronie WWW (`getMainHTML`), `PINOUT.md` (tabela + listy
  pinów) i `README_S3.md`.

### Wersja
- `FIRMWARE_VERSION`: pozostaje **`1.2c`** (ZIP 594 wpisy, w tym 4 nowe manuale;
  `merged-flash*.bin` przebudowane z nowym LittleFS).

### Kompatybilność
- Bez zmian zależności i układu partycji. Zmienione pliki firmware i ZIP.

## Pliki

| Plik | Opis | Offset |
|---|---|---|
| `merged-flash.bin` | cały obraz 16 MB (bootloader + partycje + firmware + LittleFS) | `0x0` |
| `merged-flash-notouch.bin` | to samo, wariant bez obsługi dotyku | `0x0` |
| `esp32-ham-clock-s3.zip` | paczka projektu (źródła + `data/` + pliki `.bin`) | — |

## Wgrywanie

Przez przeglądarkę: **https://jason2866.github.io/WebSerial_ESPTool/** — wybierz
`merged-flash.bin` (albo `merged-flash-notouch.bin`) i offset `0x0`.

```bash
esptool.py -p COM9 write_flash 0x0 merged-flash.bin
```

Autor: **Tomasz SP9TNV**. Projekt bazowy: **Konrad SP3KON**. Licencja: **MIT**.

---

# ESP32-HAM-CLOCK v1.2b — ekrany STACJA i ESP32 HAM LOGGER

Wydanie **v1.2b** (17.10.2026) dla platformy **ESP32-S3 N16R8** + **ILI9488 na 3.5" i 4" 480×320**
+ XPT2046. Zakres: **nowy ekran STACJA (15. ekran)**, **nowy ekran ESP32 HAM LOGGER
(16. ekran)**, poprawki nagłówka (ikona GPS po lewej, ikona baterii na wszystkich
ekranach), **naprawa PSK Reporter i POTA Cluster** (brak danych), integracja
z UI WWW oraz narzędzia podglądu. Pozostałe ekrany i zależności bez zmian.

## Zmiany od v1.2a

### Nagłówek — ikona GPS i ikona baterii
- **Ikona GPS po lewej stronie nagłówka** — `GPS_ICON_X` przeniesione z `372` na
  **`88`** (obszar dotyku `gpsIconHitX` z `364` na `82`). Ikona leży zaraz za
  godziną HH:MM, przed logo; dotknięcie nadal otwiera ekran satelitów.
- **Wspólny `drawHeaderTime(fg, bg)`** dla obu stylów nagłówka zegara
  (klasyczny i nowoczesny) — mniej duplikacji kodu.
- **Ikona baterii na wszystkich ekranach** — wspólny nagłówek `uiHeader()`
  (używany przez wszystkie ekrany poza zegarem) rysuje teraz ikonę baterii.
  `drawBatteryQuickUpdate()` przepisany: rysuje ikonę w tej samej geometrii co
  `drawBatteryIcon` i z właściwym tłem nagłówka (klasyczny / nowoczesny / splash),
  więc obudowa i poziom naładowania są spójne na każdym ekranie. `loop()` woła
  `drawBatteryQuickUpdate(true)`, więc poziom odświeża się także poza ekranem zegara.

### Nowy ekran STACJA (15. ekran)
- **Wyszukiwanie znaku** — dane pobierane kolejno ze źródeł
  **QRZ.com → HamDB → callook.info** (pierwsze trafienie wygrywa; nazwa źródła
  pokazywana na ekranie). Nowe funkcje: `stationLookupStart()`,
  `fetchHamDbCallsignInfo()`, obsługa callook.
- **Dwa transceivery** (`stationDrawRadio()`) — lewy **czerwony** = Twój znak,
  prawy **zielony** = szukany znak. Napis znaku jest **czarny na tle w kolorze
  radia** (czytelny na jasnym wyświetlaczu), a korpusy radia zachowują kolory.
- **Karta wyniku** — pola **Imię / QTH / Grid / Kraj**, linia
  **dystans + kierunek geograficzny** (np. `42 km  NE`) oraz **źródło danych
  i godzina pobrania** (HH:MM, czas lokalny).
- **Kierunek skrótami** — `N, NE, E, SE, S, SW, W, NW` (8 azymutów po 45°),
  liczony funkcją `calculateBearing()` z lokatora/QTH względem Twojej pozycji.
- **Linie fal** między antenami kolorowane wg odległości
  (`propPolarityColor()`).
- **Stany ekranu** — start, klawiatura ekranowa (`drawStationKeyboard()`,
  `handleStationTouch()`), pobieranie (`stationDrawLoading()`), wynik oraz
  „Nie znaleziono".
- **Styl klasyczny i nowoczesny** — ekran respektuje `uiStyleClassic()`; układ
  i dane wspólne dla obu stylów.

### Nowy ekran ESP32 HAM LOGGER (16. ekran)
- **Formularz QSO** — `drawLoggerForm()`: **CALLSIGN** (żółta ramka),
  **RST (S) / RST (R)**, **BAND / FREQ / MODE**, **NAME / QTH**,
  **DATE/TIME** (UTC z NTP).
- **Klawiatura ekranowa** — `drawLoggerKeyboard()`, `loggerHandleKeyboardKey()`;
  wspólna dla wszystkich pól; po zamknięciu CALLSIGN **autouzupełnia NAME/QTH**
  (`loggerLookupCallsign()` — QRZ.com → HamDB → callook.info).
- **BAND / MODE** — dotknięcie cyklicznie zmienia wartość; zmiana pasma podstawia
  domyślną częstotliwość (`loggerBandFreq[]`).
- **Tabela „Ostatnie łączności"** — 3 najnowsze wpisy (CALL/FREQ/MODE/UTC).
- **4 przyciski dotykowe** — SAVE QSO / CLEAR / EXPORT ADIF / SETTINGS
  (`handleLoggerTouch()`).
- **Styl klasyczny i nowoczesny** — respektuje `uiStyleClassic()`.
- **Log w PSRAM + bezpieczny zapis** — rekordy w **PSRAM** (`LogQso`,
  `loggerEnsureBuffer()`), zapis dopiero na SAVE; **plik tymczasowy + rename**
  (`/qso.adi.tmp` → `/qso.adi`) dla odporności na utratę zasilania
  (`loggerSaveAll()`); **ADIF bajtowo poprawny** (`loggerAdifField()` liczy
  długość w bajtach UTF-8).

### UI WWW
- Nowa pozycja **STACJA** (`station`) oraz **LOGGER** (`logger`) na liście
  ekranów w `data/index.html` i `data/indexEN.html`; liczba stron zwiększona
  z 14 do **16**.
- **`GET /qso.adi`** — pobranie pliku ADIF.
- **`POST /api/qrz_upload`** — wysyłka do QRZ Logbook (gotowe na przyszłość,
  wymaga klucza API) + pole **QRZ.com API Key (Logbook)** w ustawieniach.

### Narzędzia (`tools/`)
- `tools/render_station_preview.py` — podgląd ekranu STACJA 1:1 (Pillow):
  start, klawiatura, pobieranie, wynik, wariant klasyczny, brak wyniku.
- `tools/render_logger_preview.py` — podgląd ekranu LOGGER 1:1 (Pillow).
- `tools/render_pskmap_preview.py` — podgląd ekranu PSK MAP.
- `tools/render_header_preview.py` — podgląd nagłówków (klasyczny, nowoczesny,
  wspólny) 1:1.

### Wersja
- `FIRMWARE_VERSION`: `1.2a` → **`1.2b`**.

### Tryb PSK: MQTT domyślnie
- PSK Reporter działa teraz domyślnie w trybie **MQTT** (`pskMqttEnabled=true`),
  bo feed PSK wrócił na `pskr/filter/v2raw_1pc` i daje spoty na żywo, a mapa
  odświeża się sama (bez limitu zapytań HTTP i bez „zamarzania" na kilku
  spotach). Migracja NVS (`psk_mqtt_default_migr`) **włącza i zapisuje** ten tryb
  raz, więc jest aktywny **od razu po starcie**; własny broker użytkownika nie
  jest nadpisywany. Tryb HTTP (pskreporter.info) zostaje do wyboru w menu
  ekranu PSK MAP i w panelu WWW.

### Naprawa: PSK MQTT — zmiana pasma nie ściągała spotów
- Po przełączeniu na inne pasmo (np. **40 m**) mapa wyglądała na zawieszoną.
  Przyczyna: realny temat brokera ma **11 segmentów**, a kod subskrybował filtr
  **6-segmentowy** (`.../40m/FT8/+/+/+`) — taki filtr nie pasuje do **żadnego**
  tematu (0 spotów w 30 s; `.../40m/FT8/#` = 21 spotów). Poprawione na
  `.../<pasmo>/<tryb>/#`.
- Filtr po znaku odbiorcy czytał zły segment (idx 5 = nadawca zamiast 6 =
  odbiorca) — gdy ustawiono znak odbiornika, odrzucał prawie wszystko.
- Po zapisie zmiany filtra mapa nie była czyszczona (stare spoty zostawały) —
  teraz czyścimy spoty i wymuszamy przerysowanie; resubskrypcja odsubskrybowuje
  stary temat (koniec spotów z dwóch pasm naraz).
- Dodano mapowanie pasm **160/30/17/12 m** (menu oferowało je, ale kod nie
  rozpoznawał częstotliwości → 0 spotów) oraz ich kolory.

### Naprawa: PSK Reporter i POTA Cluster (brak danych)
- **PSK Reporter (pusta mapa).** Trzy nakładające się przyczyny:
  1. domyślny host HTTP `retrieve.pskreporter.info/query` zwracał Cloudflare
     **522/523** i 0 bajtów (origin down);
  2. broker `mqtt.pskreporter.info` **przestał publikować feed PSK** (został
     tylko WSPR);
  3. odpowiedź `pskreporter.info` przychodzi z **`Transfer-Encoding: chunked`**,
     a kod parsował **surowy socket** (`getStreamPtr()`), w którym w środku JSON
     są nagłówki chunków (np. `456c\r\n{...`). ArduinoJson kończył wtedy bez
     pola `receptionReport` (0 spotów, brak linii), mimo statusu HTTP 200.
  Poprawki:
  - domyślny URL HTTP → **`https://pskreporter.info/query?`** (działa, dane na żywo);
  - domyślny tryb → **HTTP** (`pskMqttEnabled=false`) + jednorazowa **migracja NVS**
    wyłączająca MQTT na istniejących urządzeniach (własny broker użytkownika
    zostaje); gdyby MQTT wrócił, temat to `pskr/filter/v2raw_1pc/...`;
  - odczyt ciała przez **`HTTPClient::writeToStream()`** (poprawnie składa chunki)
    do bufora w **PSRAM** (`PskHttpBodySink`), a nie z surowego socketu;
  - bufor parsowania JSON 24 KB → **512 KB w PSRAM** (odpowiedź bywa ~500 KB,
    gdy serwer zignoruje `rptlimit`);
  - **odświeżanie = świeża migawka.** Spoty tylko się kumulowały (dedup
    znak+pasmo+tryb), a PSKReporter zwraca zawsze najświeższe ~50 rekordów —
    więc kolejne pobrania nie dodawały nic nowego i mapa **zamarzała** („kreski
    ciągle takie same"). Teraz każde udane pobranie podmienia całą listę spotów
    (stare znikają), a **RESET** wymusza natychmiastowy odczyt (nie zostawia
    pustej mapy).
- **POTA Cluster (brak danych).** API zwraca teraz całą listę aktywnych spotów
  (~239 spotów = ~150 KB JSON, ~73 KB po filtrze), a bufory 24 KB / 64 KB
  w wewnętrznym RAM kończyły się błędem **`NoMemory`** i spoty ginęły. Poprawki:
  bufory w **PSRAM** — **96 KB** (filtr) / **256 KB** (fallback), przez nowy
  `struct PskPsramAllocator` + `BasicJsonDocument`.
- **POTA Cluster (kraj = `-`, brak odległości).** API `api.pota.app` **nie
  zwraca pola `country`** — kod czytał nieistniejący klucz, więc kolumna
  COUNTRY pokazywała `-`, a dane pojawiały się dopiero po wolnym zapytaniu QRZ.
  Kraj jest w **`locationDesc`** (prefiks ISO, np. `US-TX`, `ES-M`, `GB-ENG`),
  a pozycja aktywatora w `latitude`/`longitude` oraz `grid4`/`grid6`. Poprawki:
  filtr JSON rozszerzony o te pola (pula 512 → 768 B), nowa funkcja
  `potaCountryFromLocationDesc()` mapuje prefiks ISO na nazwę kraju (COUNTRY
  działa od razu), a lat/lon (lub kwadrat) trafiają do `DXSpot`, więc odległość
  liczy się lokalnie. QRZ dopytujemy już tylko o brakujący kraj.

### Automatyczna zmiana czasu letni/zimowy
- Nowy przełącznik **`autoDst`** (domyślnie włączony) dodaje +1 h w czasie
  letnim według reguły europejskiej: od **ostatniej niedzieli marca 01:00 UTC**
  do **ostatniej niedzieli października 01:00 UTC**.
- Offset lokalny liczony jest **na bieżąco z UTC** przez
  `currentTimezoneOffsetHours()` — usunięto stary 60-sekundowy cache, który po
  zmianie roku trzymał nieaktualny offset. Zmiana czasu działa od razu, bez restartu.
- Wszystkie ścieżki czasu lokalnego (zegar, APRS, dobowe obliczenia pogody,
  przejścia ISS) respektują przełącznik.
- **WWW:** checkbox „Automatyczna zmiana czasu letni/zimowy (Europa)” obok pola
  strefy czasowej; podgląd czasu lokalnego spotów APRS uwzględnia DST w JS.
- **TFT:** w menu TIME MODE (przytrzymanie zegara) nowy wiersz **`AUTO DST`**
  z kaflami **ON / OFF** — zapis do NVS natychmiast.

### T_IRQ opcjonalny (dotyk bez przerwania)
- Dotyk XPT2046 działa teraz **bez podłączonego T_IRQ**: biblioteka pracuje
  w trybie odpytywania (`tirq=255`), a dotyk wykrywany jest pomiarem Z.
- Nowe makro **`TOUCH_USE_IRQ`** (domyślnie `0`). Ustaw `-DTOUCH_USE_IRQ=1`
  (env `esp32-s3-n16r8-touchirq`) tylko, gdy T_IRQ jest fizycznie podłączony.
- Zaktualizowano `PINOUT.md` oraz wbudowany manual WWW (T_IRQ opisany jako opcjonalny).

### Kompatybilność
- Bez zmian zależności i układu partycji. Zmienione pliki firmware i ZIP.

## Pliki

| Plik | Opis | Offset |
|---|---|---|
| `merged-flash.bin` | cały obraz 16 MB (bootloader + partycje + firmware + LittleFS) | `0x0` |
| `merged-flash-notouch.bin` | to samo, wariant bez obsługi dotyku | `0x0` |
| `esp32-ham-clock-s3.zip` | paczka projektu (źródła + `data/` + pliki `.bin`) | — |

## Wgrywanie

Przez przeglądarkę: **https://jason2866.github.io/WebSerial_ESPTool/** — wybierz
`merged-flash.bin` (albo `merged-flash-notouch.bin`) i offset `0x0`.

```bash
esptool.py -p COM9 write_flash 0x0 merged-flash.bin
```

Tag **v1.2b** jest podpisany kluczem GPG wydania.

Autor: **Tomasz SP9TNV**. Projekt bazowy: **Konrad SP3KON**. Licencja: **MIT**.

---

# ESP32-HAM-CLOCK v1.2 — ESP32-S3 N16R8 + ILI9488 na 3.5" i 4" 480×320

Wydanie **v1.2** dla platformy **ESP32-S3 N16R8** (16 MB flash, 8 MB PSRAM)
z wyświetlaczem **ILI9488 na 3.5" i 4" 480×320** po SPI i panelem dotykowym XPT2046.

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

---

# ESP32-HAM-CLOCK v1.2a — wydanie poprawkowe (PSK MAP)

Wydanie **v1.2a** (16.10.2026) dla tej samej platformy
(**ESP32-S3 N16R8** + **ILI9488 na 3.5" i 4" 480×320** + XPT2046). Zakres: ekran **PSK MAP**
oraz narzędzia projektu. Pozostałe ekrany i zależności bez zmian.

## Zmiany od v1.2

### Mapa spotów (PSK MAP)
- **Kumulacja spotów** — spoty narastają na mapie zamiast się nadpisywać.
  Nowy `pskAddSpot()` z deduplikacją (znak + pasmo + tryb);
  `PSK_MAX_SPOTS` **50 → 200**.
- **Auto-RESET** po 200 spotach — mapa czyści się sama i startuje nowy cykl.
- **Panel po prawej** (`drawPskSpotListPanel()`) — licznik `SPOTY: N` + lista
  najnowszych; ramka zwężona **310/166 → 340/136 → 360/116** (prawa krawędź
  bez zmian), pole znaku 9 → 8 znaków.
- **Odświeżanie tylko spotów** (`drawPskMapSpotsOnly()`) — nagłówek „PSKReporter
  MAP", pasek, przycisk menu, ikona baterii i napisy nie są przerysowywane
  (koniec migania). Rysowanie przyrostowe przez `lastPskSpotCount`.
- **MQTT domyślnie włączone** (`pskMqttEnabled = true`, NVS `psk_mqtt_en = true`).
- **Przycisk RESET** — `resetPskMapSpots()` zeruje spoty i bufor MQTT.

### Narzędzia i dokumentacja
- `STAN_PRAC.md`, aktualizacje `AGENTS.md`.

### Wersja
- `FIRMWARE_VERSION`: `1.2.1` → **`1.2a`**.

### Kompatybilność
- Bez zmian zależności i układu partycji. Zmienione pliki firmware i ZIP.

## Pliki

| Plik | Opis | Offset |
|---|---|---|
| `merged-flash.bin` | cały obraz 16 MB (bootloader + partycje + firmware + LittleFS) | `0x0` |
| `merged-flash-notouch.bin` | to samo, wariant bez obsługi dotyku | `0x0` |
| `esp32-ham-clock-s3.zip` | paczka projektu (źródła + `data/` + pliki `.bin`) | — |

## Wgrywanie

Przez przeglądarkę: **https://jason2866.github.io/WebSerial_ESPTool/** — wybierz
`merged-flash.bin` (albo `merged-flash-notouch.bin`) i offset `0x0`.

```bash
esptool.py -p COM9 write_flash 0x0 merged-flash.bin
```

Autor: **Tomasz SP9TNV**. Projekt bazowy: **Konrad SP3KON**. Licencja: **MIT**.
