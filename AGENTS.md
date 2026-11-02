# AGENTS.md — ESP32-HAM-CLOCK

Firmware zegara/DX-toola dla krótkofalowców (Arduino/PlatformIO).

## Wątki i TFT: zawsze trzymaj `tftMutex` (13.10.2026)
TFT_eSPI **nie jest bezpieczny wątkowo** (wspólna flaga `locked` + jeden mutex
SPI). Po TFT rysują dwa zadania: `UI_Task` (rdzeń 1, prio 2) i `loopTask`
(rdzeń 1, prio 1, czyli `loop()` + handlery HTTP). Bez serializacji jedno
zadanie zwalnia mutex SPI drugiego → `assert failed: xQueueGenericSend` →
panic i restart. Objawia się najbardziej na **świeżo wgranym** firmware (brak
WiFi w NVS → tryb AP → `loopTask` rysuje `drawApInfoBanner()` co 3 s, gdy
`UI_Task` odświeża zegar co 1 s).

Rozwiązanie: jeden **rekurencyjny** mutex `tftMutex`
(`xSemaphoreCreateRecursiveMutex`, tworzony w `setup()`) + `tftLock()/tftUnlock()`.
- `uiTaskLoop` trzyma mutex przez całą iterację rysowania.
- `loopTask` (i handlery HTTP) używają **try-lock** `tftLock(0)` — nie blokują
  serwera WWW, gdy `UI_Task` długo rysuje.
- Rekurencyjny, bo funkcje rysujące wołają się nawzajem (`drawScreen` →
  `drawHamClock` → ...).
- Nowe miejsce rysujące po TFT z `loop()`/handlera **musi** objąć wywołanie
  `if (tftLock(0)) { ...; tftUnlock(); }`.


## Wgrywanie firmware przez stronę WWW (ESP Web Tools) (01.11.2026)
Strona flashera: **`flash/index.html`** — wgrywa `merged-flash.bin` z przeglądarki
(bez instalowania esptool). Używa **ESP Web Tools** (`<esp-web-install-button>`,
`https://unpkg.com/esp-web-tools@10.1.1/dist/web/install-button.js?module`).
- **Wymaga HTTPS** (WebSerial) i Chrome/Edge/Opera. `http://` → „not-allowed".
- Manifesty generowane przez `tools/make_flash_page.py`:
  `flash/manifest-touch.json` (merged-flash.bin) i `flash/manifest-notouch.json`;
  każdy: `chipFamily: ESP32-S3`, jeden part `{path, offset:0}`,
  `new_install_prompt_erase: true` (czysty flash przy pierwszym wgraniu).
- Generator kopiuje `dist/*.bin` do `flash/` i publikuje do
  `/workspace/share/flash/` (HTTPS) oraz `download/flash/`.
- **Ścieżka do binarki w manifeście zależy od lokalizacji** (binarki NIE są
  duplikowane w `flash/` — wyklucza je `flash/.gitignore`):
  - `flash/manifest-*.json` (repo, serwowane na Pages jako `/flash/`) →
    `../download/merged-flash*.bin` (binarki są w `/download/`),
  - `download/flash/` i `share/flash/` → `../merged-flash*.bin` (binarka
    w katalogu nadrzędnym). Generator przepisuje też linki „pobierz" w kopii
    `index.html` (`rewrite_bin_links`).
  - `download/manifest-*.json` (strona projektu) → `merged-flash*.bin` (obok).
- Uruchamiać **po** `tools/make_release.sh`. W WWW urządzenia link do flashera
  (sekcja „Wgraj firmware przez przeglądarkę") wskazuje na własną stronę
  projektu: `https://tom50505050.github.io/esp32-ham-clock-s3/flash/`.
- **Przycisk wbudowany na stronie projektu (`download/index.html`):** w sekcji
  LINKI (`github-flasher-box`) są DWA `<esp-web-install-button>` (touch /
  notouch) — wgrywanie bez wychodzenia ze strony. Manifesty
  (`manifest-touch.json`, `manifest-notouch.json`) leżą **obok** strony
  (korzeń publikacji), a ścieżka do binarki w manifeście jest względna
  (`merged-flash.bin` w tym samym katalogu) — dzięki temu działa niezależnie od
  tego, czy Pages serwuje repo root czy `/docs`. `tools/make_flash_page.py`
  zapisuje te manifesty do `/workspace/share/` i `download/`. Binarki w obu
  korzeniach trzeba odświeżać z `dist/` (inaczej przycisk wgrywa starą wersję).
- Publikacja: dowolny hosting HTTPS — GitHub Pages (repo → Settings → Pages,
  branch `main`, root) da `https://<user>.github.io/esp32-ham-clock-s3/flash/`.
- **Nie mylić z OTA:** to nadal wgrywanie kablem USB przez WebSerial (tylko UI
  w przeglądarce). Firmware nie ma OTA — dlatego slot app1 oddany na LittleFS.

## Publikacja na GitHub Pages i push (02.11.2026)
- Repo: `Tom50505050/esp32-ham-clock-s3`, branch `main`, Pages: source = branch
  `main`, root `/`, build_type `legacy`. Live: `.../flash/` (flasher) i
  `.../download/index.html` (strona projektu z przyciskami).
- **Push:** remote to czysty HTTPS. Uwierzytelnianie przez `gh` CLI:
  `gh auth login --web` (device flow — kod wpisywany na github.com/login/device,
  nic nie trafia do czatu), potem `gh auth setup-git`. Sekret `$GITHUB_TOKEN`
  w środowisku agenta bywa **pusty** — wtedy używać `gh`/remote, nie API z tokenem.
- **Commity są podpisywane GPG** (`commit.gpgsign=true`, klucz
  `E488FAAFECAC49D96D7152124BCD02BCAF6D4670`). Po resecie sesji brak klucza w
  keyringu → commit pada z „No secret key". Naprawa:
  `export GNUPGHOME=/home/openhands/.gnupg && gpg --batch --import .gpg-release-key`.
- **Push zmienia publiczne repo i Pages** — wymaga wyraźnej zgody użytkownika.
- Po pushu Pages przebudowuje się ~30-60 s; build bywa `errored` przejściowo →
  ponowić `gh api -X POST repos/.../pages/builds`.

## APRS styl nowoczesny: pełny zestaw ikon w firmware (01.11.2026)
Ekran nowoczesny (`drawAprsAlertModern`) ma teraz komplet ikon symboli APRS —
24 symbole jako tablice RGB565 64×64 w **`aprs_modern_icons.h`** (wbudowane
w firmware, **nie** w LittleFS — nie zabierają miejsca na uploady z WWW):
- źródła: **Fluent Emoji 3D** (MIT) dla pojazdów/obiektów oraz wycinki z
  `yagi.jpg` dla **REPEATER (#2)** i **YAGI (#11)** (arkusz 4×4 z repo beta),
- tło ikon = kolor karty `0x0884`, więc wtapiają się w kartę bez sentinela
  (decoder BMP nie obsługuje przezroczystości),
- **HOUSE** i **WX** nadal rysuje `drawAprsAlertHouseBitmap` (bitmapa domu
  z kodu); **WX** dodatkowo dostaje plakietkę „WX" (`drawAprsModernWxBadge`),
- **DIGI** = ikona `antenna_bars` z tablicy, reszta bez wpisu → wektorowy fallback.
- Wybór: `aprsModernIconFor(key)` + `drawAprsModernBitmap(bmp,x,y,size)`.
- Generator: `.mock/gen_modern_icons.py` (z `.mock/fluent3d/*.png` i `.mock/cell_*.png`).
- **Ekran klasyczny** (`drawAprsAlertClassic`) bez zmian — nadal ikony 48×48 z
  LittleFS (`/aprs_icons/bmp/*.bmp`, `drawAprsAlertFsIcon`).
- Budżet: firmware 1.85 MB / slot 3.94 MB (44.9%, zapas ~2.1 MB);
  LittleFS **11.94 MB**, `data/` 5.28 MB → **~4.9 MB wolne na uploady WWW**
  (po oddaniu slotu OTA na LittleFS — patrz „Powiększenie LittleFS" niżej).


## Alert APRS: styl nowoczesny + klasyczny, ikona domu 3D (31.10.2026)
Ekran alertu APRS (`ALERT_Screen`) ma teraz dwa warianty wybierane **globalnym**
ustawieniem wyglądu ekranów (`screen1ClockStyle`, menu ZEGAR → „WYGLAD EKRANOW"):
- **nowoczesny** (`drawAprsAlertModern`, `screen1ClockStyle == 0`, domyślny) — ciemna
  karta `#0b1220`, akcent teal `0x3697`, chipy FREQ/ODLEGLOSC, karta STACJA z ikoną
  domu 3D (bitmapa), karta NAMIAR z kompasem, pasek statusu. Bez pulsującej ramki.
- **klasyczny** (`drawAprsAlertClassic`, `screen1ClockStyle == 1`) — dotychczasowy
  wygląd (pomarańczowy pasek, ramka, pulsowanie).
- Ikona domu: `APRS_HOUSE_ICON[4096]` (64×64 RGB565) wygenerowana z referencji
  (oczyszczonej z tła i znaków wodnych); tło = sentinel `0xF81F` pomijane przy
  rysowaniu (`drawAprsAlertHouseBitmap`). Generator bitmapy: `/tmp/wx/gen_bitmap2.py`.
- Kompas `drawAprsAlertCompass` dostał parametr `modern` (kolory).
- Nagłówek pokazuje ostatnią aktualizację w czasie **UTC** (`AKT HH:MMZ`), a dolny
  pasek (stacja nie-WX) ma **kropkę statusu** (zielona = symbol+pozycja OK, żółta =
  dane częściowe) oraz wiersz `AKTUALIZACJA: HH:MM:SSZ`.
- Wspólny helper `aprsDecodeStationWx(station, wx)` = symbol WX + `decodeAprsWxFromComment`.

## Diagnostyka restartów ESP32 (31.10.2026)
Objaw „ESP co jakiś czas się resetuje" diagnozujemy teraz z licznika w pamięci RTC:
- `RTC_NOINIT_ATTR` `diagResetCount` / `diagLastResetReason` / `diagLastMinHeap`
  przeżywają restart (magic `0x48414D43`).
- `GET /api/status` zwraca dodatkowo: `reset_count`, `reset_reason`, `min_heap`
  (minimalny wolny heap od startu).
- Serial przy starcie: `[DIAG] Restart #N, powod: ...`; log heapu co 30 s podaje też minimum.
- Znaczenie `reset_reason`: `BROWNOUT` = za słabe zasilanie (najczęstsze; detektor
  brownoutu jest wyłączony w setup, ale głębokie spadki nadal resetują), `TASK_WDT` =
  zablokowana pętla/zadanie, `PANIC/CRASH` = wyjątek (np. brak stosu, `StoreProhibited`),
  `SOFTWARE` = `ESP.restart()`.
- Typowe przyczyny w tym projekcie: (1) zasilanie/kabel USB przy skokach prądu WiFi+podświetlenie,
  (2) przepełnienie stosu zadań HTTPS (`ISS_Task` już 24 KB), (3) fragmentacja heapu przy
  TLS/`String`. Sprawdzaj `min_heap` i serial.

### Poprawka: zapis z WWW nie restartuje już ESP bez potrzeby (31.10.2026)
Wcześniej `/api/save` **zawsze** wołał `requestRestart()` → `ESP.restart()`, więc każde
„Zapisz ustawienia" z WWW resetowało urządzenie (`reset_reason = SOFTWARE`). Teraz restart
jest tylko gdy zmiana naprawdę wymaga reinicjalizacji sprzętu:
- nowy SSID/hasło WiFi (`wifiChanged`) albo
- geometria ekranu: `tft_rotation` lub `tft_invert`.
Pozostałe ustawienia (APRS, pogoda, kolory, czasy, hosty) działają **na żywo**; zmiana
hostów klaster/POTA/APRS robi `*Client.stop()` i pętla łączy ponownie. Odpowiedź `/api/save`
zwraca teraz `action:"restart"` albo `action:"saved"`.
- Dla stacji pogodowej (symbol WX, dekoder `decodeAprsWxFromComment`) dolny pasek
  pokazuje 4 chipy: TEMP, WIATR, WILG, CISN (`aprsModernWxChip`); dla pozostalych
  symbol + status pozycji.
- Buildy `esp32-s3-n16r8` i `-notouch` SUCCESS; `merge_bin` → `dist/`, ZIP gotowy.


## Dźwięk alertu APRS na wspólnym piezo (GPIO 2) + poprawka opisu pinu LED (31.10.2026)
Alert APRS dostał **krótki sygnał dźwiękowy** („pip-pip-pip") na tym samym piezo
co alarm — `BUZZER_PIN` = **GPIO 2** (LED alertu APRS pozostaje na **GPIO 6**,
kanał niebieski RGB).
- Stałe: `APRS_BEEP_FREQ 2000` Hz, `APRS_BEEP_COUNT 3`, `APRS_BEEP_ON_MS 120`,
  `APRS_BEEP_GAP_MS 120`, `APRS_BEEP_DUTY 600` (stała głośność, **niezależna** od
  suwaka alarmu — stąd `buzzerRawTone(f, duty)`; `alarmTone()` go opakowuje).
- Automat stanu bez blokowania: `aprsBeepPlay()` / `aprsBeepUpdate()` (pętla główna) /
  `aprsBeepAbort()` (natychmiastowa cisza).
- **Alarm ma priorytet** (wspólny pin): `aprsBeepPlay()` nie startuje, gdy
  `alarmRinging`, a `alarmStartRinging()` woła `aprsBeepAbort()`; po alarmie/potwierdzeniu
  obowiązuje okno ciszy `aprsBeepMutedUntilMs` (1500 ms).
- Wyzwalane w `triggerAprsRgbLedAlert()` (obok migania LED); `aprsBeepUpdate()` w `loop()`.
- **Poprawka dokumentacji:** etykiety WWW (`data/index*.html`, `download/index.html`)
  i `tools/lang_pages.json` mówiły „Blue LED - GPIO17" — firmware używa **GPIO 6**.
  Zaktualizowano opis we wszystkich 6 językach (LED GPIO 6 + dźwięk GPIO 2).


## Skala procentowa baterii: 3.8 V = 100%, 3.0 V = 0% (SONY GB/T-18287) (31.10.2026)
Zgłoszenie: akumulator SONY GB/T-18287 3.8 V. Ikona ma pokazywać 100% przy 3.8 V,
a 0% dopiero przy 3.0 V.

**Zmiana:** skala procentowa to 3.80 V = 100% ... 3.00 V = 0%.
- `main.cpp`: `kBatteryCurve[]` = 3.00-3.80 V (3.80→100, 3.70→88, 3.60→74,
  3.50→58, 3.40→42, 3.30→26, 3.20→13, 3.10→6, 3.00→0). Kształt oddaje plateau
  ogniwa (długo trzyma ~3.5-3.7 V, poniżej 3.3 V spada szybko).
- Makra zakresu: `BATTERY_VOLTAGE_MAX` **3.8**, `MIN`/`CUTOFF` **3.0**,
  `LOW` **3.3** (pomarańczowy), `NOM` 3.7. Kolory ikony (czerwony ≤3.0 V,
  pomarańczowy ≤3.3 V) i ostrzeżenie <10% idą za nową skalą.

**Historia:** poprzednio skala była 3.30-3.80 V (0% = 3.3 V), a jeszcze wcześniej
klasyczna 18650 3.40-4.20 V. Obecna wersja rozciąga dół do 3.0 V zgodnie z
życzeniem użytkownika. Kalibracja napięcia (mnożnik/offset z WWW) działa bez
zmian — procent liczony jest z już skalibrowanego napięcia.

**Ikona pioruna (ładowanie):** powyżej **4.08 V** na ikonie baterii pojawia się
czerwony piorun (`drawBatteryBolt()`, kolor `TFT_RED`), sygnalizujący ładowanie.
Próg kasowania 3.98 V (histereza 0.1 V) chroni przed miganiem. Wcześniej piorun
był „wycinany” kolorem tła i włączał się dopiero >4.25 V; teraz jest czerwony i
widoczny też na jasnym (zielonym) wypełnieniu. Historia progu: 4.25 → 4.0 → 4.3 → **4.08 V**.


## ALARM na ekranie Matrix: przytrzymanie 5 s + buzzer (GPIO 2) (31.10.2026)
Zgłoszenie: na ekranie Matrix (SCREEN_MATRIX_CLOCK) przytrzymanie palca przez
**5 s** ma otwierać menu alarmu z ustawialną godziną i dniami tygodnia, a o
wyznaczonej porze ma grać melodia na buzzerze.

**Wejście:** `handleTouchNavigation()` ustawia `matrixGameHoldCandidate` dla
CAŁEGO ekranu Matrix (wcześniej tylko róg x>=320,y<=80 dla gry w węża). Po 5 s
(5000 ms) otwiera `openMatrixAlarmMenu()` — zastąpiło to dawny skrót 2 s do
SYSTEM_INFO na tym ekranie. Menu jasności (przytrzymanie 3 s) jest teraz
wyłączone na ekranie Matrix (`currentScreen != SCREEN_MATRIX_CLOCK` w jego
warunku), żeby oba gesty się nie kłóciły.

**Menu (480x320):** `drawAlarmMenu()` — nagłówek `uiMenuHeader()`, duży podgląd
`GG:MM`, przyciski `-`/`+` dla godzin i minut, 7 kafelków dni
(`ALARM_DAY_SHORT` = ND PN WT SR CZ PT SB), **12 kafelków melodii** w siatce 2x6
(dotknięcie gra próbkę: `alarmPreviewMode = true` → raz, bez pętli), **suwak
głośności melodii** (`drawAlarmVolumeSlider()`), przyciski
`ALARM: WL/WYL`, `ZAPISZ`, `ZAMKNIJ`. Układ trzymają wspólne stałe `ALM_*`
(używane i przy rysowaniu, i w hit-testach). Obsługa: `handleAlarmMenuTouch()`
+ `alarmMelodyTileAt()` dla siatki melodii.

**Suwak głośności (GLOSNOŚĆ):** `ALM_VOL_*` (tor 88..392 @ y=252), wartość =
wypełnienie LEDC `ALM_VOL_MIN..ALM_VOL_MAX` (40..1023). `setAlarmVolumeFromTouch()`
działa **także przy przeciąganiu** (poza guardem `isNewTap`) i po każdej zmianie
gra krótką próbkę melodii nową głośnością. Ustawia `alarmMenuVolume` i od razu
`alarmVolume`; `alarmTone()` bierze wypelnienie z `alarmVolume` (fallback
`BUZZER_DUTY`). Zapis do NVS (`alarm_vol`) dopiero przy `ZAPISZ`/`ZAMKNIJ` —
brak zapisu na każdy piksel przeciągania. Strefa dotyku y 240..268 (między
siatką melodii kończącą się na 237 a przyciskami od 270).

**Dotyk w menu alarmu:** kafelki (dni, melodia) i przyciski dolne zmieniają się
**tylko przy `isNewTap`** — bez tego palec przeciągany po ekranie przelatywał po
kafelkach melodii i restartował podgląd dźwięku (dotyk „źle działał"). Przyciski
+/- mają natomiast **powtarzanie przy przytrzymaniu** (co 150 ms), ale **bez**
500 ms zwłoki (pierwsze naciśnięcie działa natychmiast). Przyciski +/- godzin i
minut są rozsunięte (H-104/H+192, M-242/M+330), żeby się nie nakładały.
Zapis do NVS: klucze `alarm_en/alarm_hour/alarm_min/alarm_days/alarm_mel/alarm_vol`
(`savePreferences()`/`loadPreferences()`), domyślnie alarm WYŁĄCZONY, dni
0x7F (wszystkie), głośność `ALM_VOL_DEFAULT` (512).

**Melodie (12, `ALARM_MELODIES[]`):** BUDZIK, DZWONEK, POBUDKA, PIKANIE,
MORS CQ, MORS SOS, SYRENA, NOKIA (Gran Vals, PD), TETRIS (Korobeiniki, ludowa),
KUKULKA, PIPS, KURANT. Nuty jako `{freq, units}` (`units` × 150 ms, 0 = pauza).

**Buzzer:** pasywny piezo na **GPIO 2** przez LEDC (`alarmInitBuzzer()` w
`setup()` po `loadPreferences()`). Sterowanie w `loop()` (co ~1 s
`checkAlarmTrigger()`, plus `alarmUpdateBuzzer()` w każdej iteracji — automat
stanów nut, nieblokujący). Wzorzec LEDC 2.x/3.x jak przy podświetleniu.

**UWAGA (pułapka LEDC):** gdy pin jest przypięty do kanału LEDC, `digitalWrite()`
**nie wycisza** go — pin dalej generuje PWM ostatniej nuty (efekt: po melodii
zostaje ciągły pisk, którego nic nie wyłącza). Ciszę robi się przez
`alarmTone(0)` → `ledcWriteTone(kanał, 0)` + `ledcWrite(kanał, 0)` (2.x) albo
`ledcWrite(pin, 0)` (3.x). `alarmTone(0)` wołane też na końcu `alarmInitBuzzer()`.

**Głośność piezo:** pasywny piezo ma rezonans ~2–4 kHz, więc nuty 370–880 Hz są
ciche. `BUZZER_OCTAVE_UP 2` podbija strój o 2 oktawy (`freq << 2`) w `alarmTone()`;
ta sama stała `OCTAVE_UP` w `tools/make_alarm_audio.py` (podgląd = to, co gra).
Poziom głośności (wypełnienie LEDC) ustawia suwak w menu alarmu (`alarmVolume`);
`BUZZER_DUTY 512` to tylko wartość domyślna/fallback. Zabezpieczenie w `loop()`:
`alarmRinging` i minął `alarmRingScreenUntilMs` → `alarmStopMelody()` (nigdy nie
piszczy w nieskończoność). **Ważne:** `alarmRingScreenUntilMs` ustawia już
`checkAlarmTrigger()` (przy wyzwoleniu), a `drawAlarmRingScreen()` tylko odświeża.
Gdyby deadline pozostał 0/stary, auto-stop w `loop()` wyciszyłby melodię
natychmiast, zanim UI_Task narysuje ekran (objaw: „ekran alarmu jest, ale brak
melodii"). Stąd też warunek `alarmRingScreenUntilMs != 0` w auto-stopie.

**Dzwonienie:** `checkAlarmTrigger()` liczy minutę absolutną
`tm_yday*1440 + godz*60 + min` i porównuje z celem — alarm odpala **raz
dziennie** (samo `godz*60+min` odpaliłoby tylko raz w życiu modułu). Sprawdza
też `alarmDaysMask` (bit `tm_wday`). Po trafieniu `alarmStartRinging()`:
`alarmRinging = true`, `alarmRingPending = true`, melodia w pętli. UI_Task rysuje
modalny ekran `drawAlarmRingScreen()` (czerwona ramka, „ALARM!", godzina,
melodia) — widoczny na dowolnym ekranie; ustawia `inMenu = true` (blokuje
periodyczne odświeżanie pod spodem) i blokuje wygaszacz.

**Powtarzanie (5 min):** jeśli nikt nie dotknie ekranu, alarm **wznawia się co
5 minut** (`ALARM_REPEAT_MS`), aż do potwierdzenia dotknięciem. `alarmStartRinging()`
planuje `alarmNextRingMs = millis() + ALARM_REPEAT_MS` **od razu przy starcie
dzwonienia** — cykl: dzwoni `ALARM_RING_DISPLAY_MS` (60 s) → pętla główna woła
`alarmStopBuzzer()` (cisza, ale `alarmRinging` **zostaje true**) → po
`ALARM_REPEAT_MS` od startu kolejne `alarmStartRinging()`.

> **Błąd „alarm nie włącza się ponownie po 5 min" (naprawiony):** pierwotnie
> powtórkę planował dopiero blok auto-stopu w pętli głównej, po 60 s dzwonienia.
> Jeśli ten blok nie zadziałał (np. `alarmRingScreenUntilMs` wyzerowane przez UI,
> albo stan `alarmRinging` skasowany), powtórka **nigdy** nie była planowana.
> Teraz `alarmNextRingMs` jest ustawiane w `alarmStartRinging()` — planowanie nie
> zależy już od bloku auto-stopu. Dodatkowo log `[ALARM] Start dzwonienia,
> powtorka za 300 s` pozwala to potwierdzić po UART.

**Potwierdzenie:** dotknięcie ekranu → `alarmStopMelody()` + `alarmNextRingMs = 0`
(pętla główna) oraz `dismissAlarmRingScreen()` (UI_Task, powrót do widoku pod
spodem). `alarmStopMelody()` = pełny stop (`alarmRinging=false`);
`alarmStopBuzzer()` = tylko cisza (stan alarmu zostaje). Uwaga: to rozdzielenie
jest konieczne — inaczej auto-stop kasowałby `alarmRinging` i powtórki nie
działałyby.

**Flagi współdzielone loop/UI_Task** (`alarmRinging`, `alarmRingScreenActive`,
`alarmPreviewMode`, indeksy nut) są `volatile`.

**Podglądy (do wglądu, poza firmware):** `tools/render_alarm_preview.py`
(rasteryzuje menu 1:1 prawdziwymi bitmapami czcionek → `alarm-nowoczesny.png`,
`alarm-klasyczny.png`, `alarm-dzwoni.png`) oraz `tools/make_alarm_audio.py`
(generuje WAV falą prostokątną, dokładnie nuty z firmware). Strona zbiorcza:
`/workspace/share/alarm.html`.


## „Blad wgrywania (format/siec)" przy ekranie powitalnym — brak miejsca w LittleFS (31.10.2026)
Zgłoszenie: wgranie własnej grafiki na **ekran powitalny** (`/upload_welcome`,
zakładka TFT → „Grafika na ekran") kończyło się w WWW komunikatem
„Blad wgrywania (format/siec)". Komunikat jest mylący — to nie błąd sieci.

**Przyczyna (odtworzona):** wolne miejsce w LittleFS kurczy się z każdym uploadem
grafiki, a firmware wymaga `rozmiar_pliku + 64 kB` wolnego przed zapisem
(`handleUpload()`, ścieżka RAW i multipart). Wysłany obraz miał wolne 876544 B
(po dodaniu `.gz` i konwersji ikon do 16-bit). Sekwencja użytkownika:
1. własna ikona burzy (`/wxicon_storm`, ~307 kB) → OK, wolne spada do ~569 kB,
2. własna ikona wiatru (`/wxicon_wind`, ~307 kB) → OK, wolne spada do ~262 kB,
3. ekran powitalny (konwersja 480×320, ~307 kB) → `307266 + 65536 > 262012`
   → kontrola miejsca zwraca **HTTP 507**, a JS pokazuje generyczne
   „Blad wgrywania (format/siec)".

Ścieżka RAW działa poprawnie — 507 jest zwracane, ale WWW nie odróżniało go od
błędu sieci. Wcześniejszy „TypeError: Failed to fetch" (ikony) to była inna,
już naprawiona ścieżka (panic bez kontroli miejsca).

**Naprawa:**
- **Więcej miejsca:** usunięto 6 martwych plików czcionek z `data/fonts/`
  (`dejavu-sans.book{10,12,20}.vlw`, `roboto.regular{10,12,20}.vlw` — 0 odwołań
  w kodzie i WWW, razem ~330 kB). Wolne w LittleFS: 876544 → **3007242 B** (~2,9 MB).
- **Czytelny komunikat:** we wszystkich 6 językach `data/index*.html` dodano
  `uploadErrorMessage()` — 507 pokazuje „Brak miejsca w pamięci urządzenia
  (LittleFS)…", inny kod HTTP pokazuje kod, brak odpowiedzi pokazuje błąd sieci.
  Podmieniono nim generyczne `catch` w ścieżkach: splash, welcome (direct) i konwerter.

**Ważne dla przyszłych zmian:** LittleFS jest ciasny; każda nowa grafika/`.gz`
zwiększa ryzyko 507 przy uploadach. Przed dodawaniem zasobów sprawdź wolne
(`pio run -e esp32-s3-n16r8 -t buildfs` i odczytaj rozmiar obrazu). Po deployu
podmień **oba** katalogi: `share/` i `share/flash/` (oraz `download/`).


## Kalibracja baterii „nie działa" — korekta nadpisywana przez bat_div_cal (31.10.2026)
Zgłoszenie: w WWW (Ustawienia → „Kalibracja baterii") odczyt pokazuje np.
`3.90 V | 61%` mimo w pełni naładowanego akumulatora; wpisanie rzeczywistego
napięcia i „Zapisz" nic nie zmienia.

**Przyczyna (kolejność w `/api/save`):** handler najpierw liczył jednopunktową
korektę z `bat_cal_v` (`batteryDividerCal *= vref/raw`), a **potem** ustawiał
`batteryDividerCal = bat_div_cal`. Formularz WWW **zawsze** wysyła `bat_div_cal`
(bieżącą wartość z urządzenia, `collectConfigPayload`), więc świeżo policzona
korekta była natychmiast nadpisywana starą wartością — kalibracja nigdy się nie
utrwalała.

**Naprawa:** odwrócona kolejność — najpierw `bat_div_cal` i `bat_off`, na końcu
jednopunktowa korekta `bat_cal_v` (teraz autorytatywna i idempotentna: po korekcie
`raw ≈ vref`, więc `|vref-raw| ≤ 50 mV` i kolejny „Zapisz" nic nie zmienia).
Dodano log `[BATTERY] Kalibracja: raw=... vref=... -> div ... => ...`.

**Spójność odczytu:** `/api/config` zwracał `sys_vcc = readBatteryVoltage()`
(świeży odczyt), a `sys_batt_pct = batteryPercentage` (ostatnia wartość z pętli) —
panel pokazywał niespójną parę (np. 3.90 V | 61%, gdy 3.90 V → 62%). Teraz oba
pola pochodzą z tego samego pomiaru (`batteryVoltage` + `batteryPercentage`,
aktualizowane razem w `updateBatteryStatus`).

Uwaga: sam pomiar (dzielnik 100k/100k + kalibracja eFuse) jest poprawny —
`3.90 V → 62%` wg krzywej Li-ion. Jeśli akumulator jest w pełni naładowany, a
firmware mierzy 3.90 V, to albo ogniwo jest rozładowane, albo dzielnik/mnożnik
wymaga kalibracji — teraz kalibracja z WWW faktycznie zapisuje korektę.

## „Błąd wysyłania: TypeError: Failed to fetch" — nieaktualne binarki na stronie wgrywania (31.10.2026)
Zgłoszenie (po naprawie 507): wgranie ikony burzy w WWW kończyło się
`TypeError: Failed to fetch` (a dla splash: „Blad wgrywania (format/siec)").
To NIE jest odpowiedź 507 — `Failed to fetch` to błąd sieciowy: przeglądarka nie
dostała żadnej odpowiedzi, bo urządzenie zresetowało się w trakcie zapisu
(`IntegerDivideByZero` w littlefs — patrz sekcja „Grafiki alertu pogodowego").
Handler zwracał 507 tylko przy `Content-Length` większym niż wolne miejsce; przy
pewnych ścieżkach zapis do pełnego FS i tak trafiał w panikę.

**Główna przyczyna: rozjechane binarki na serwerze.** Strona wgrywania
(`/flash/`, Web Serial) oraz `/merged-flash.bin` serwowały **stare** obrazy
(2026-10-29 i 2026-10-15) — sprzed naprawy 507. `share/flash/merged-flash.bin`
był z 15.10: bez sprawdzania miejsca, bez `wxicon_*`, wolne ~3,6 MB, splash
24-bit. `share/merged-flash.bin` z 29.10 miał kontrolę miejsca, ale wolne
610 kB. Dopiero `dist/merged-flash.bin` (31.10) ma wolne 876 kB i splash 16-bit.
Użytkownik wgrywał więc firmware, w którym ikona 307 kB nie mieściła się z
zapasem → panic → `Failed to fetch`.

**Naprawa:** skopiowano aktualne `dist/merged-flash*.bin` oraz
`esp32-ham-clock-s3.zip` do `share/` **oraz** `share/flash/` (strona wgrywania
`/flash/` linkuje pliki po nazwach względnych, więc wystarczy podmienić binarki
pod tymi samymi nazwami — bez zmian w `index.html`). Wcześniejszy deploy pomijał
`share/flash/` — po każdym wydaniu trzeba podmienić **oba** katalogi.

**Utwardzenie firmware (main.cpp, ścieżka RAW):**
- `rawUploadAbortNoSpace()` — wspólne przerwanie uploadu przy braku miejsca
  (zamyka i usuwa częściowy plik, ustawia flagę → `sendUploadResult()` zwraca 507).
- kontrola pojedynczego zapisu: `rawUploadFile.write()` zwracający mniej bajtów
  niż `raw.currentSize` natychmiast przerywa upload (zamiast pchać dalej zapis do
  pełnego FS, co jest ścieżką paniki).
- `RAW_ABORTED` (klient zerwał połączenie w połowie) usuwa częściowy plik.

Efekt: brak miejsca daje czytelne HTTP 507 („brak miejsca w LittleFS") zamiast
restartu i `Failed to fetch`.

## Naprawa „Błąd wysyłania: brak miejsca w LittleFS" — ikona burzy (31.10.2026)
Zgłoszenie: wgranie grafiki burzy z panelu WWW kończyło się odpowiedzią 507
(`uploadNoSpace`) — brak miejsca w LittleFS.

Przyczyna: dodanie prekompresowanych `.gz` (`tools/make_gz.py`, ~300 kB:
`index*.html.gz` + `style*.css.gz` + `satellite.js.gz`) zjadło zapas miejsca,
który wcześniej wystarczał na wgranie ikony 480x320 (~307 kB). Wolne spadło do
~573 kB, a firmware wymaga `rozmiar_pliku + 64 kB` wolnego przed zapisem.

Naprawa (bez utraty funkcji — `.gz` zostają):
- `data/splash.bmp` i `data/Mapa swiata.bmp` były 24-bit (461 kB) — przekonwertowane
  na 16-bit RGB565 (`tools/bmp24to16.py`, 307 kB każda). Oba dekodery firmware
  (`drawBmpFromFS`/`drawBmp16FromFS`, `pskMapBgLoad`) obsługują 16-bit.
- `data/icon50/burza.bmp` i `wiatr.bmp` były już 16-bit, ale **odwrócone do góry
  nogami** — patrz niżej.
- Efekt: wolne w LittleFS wzrosło z ~573 kB do ~876 kB (214 bloków), zapis ikony
  (~307 kB) mieści się z zapasem.

Błąd `tools/bmp24to16.py`: pisał wiersz wyjściowy `y` ze źródłowego `h-1-y`, czyli
odwracał obraz. Firmware czyta BMP od dołu (`screen_row = h-1-file_row`), a plik
źródłowy jest już zapisany bottom-up — każde odwrócenie wierszy dawało obraz do
góry nogami. Poprawione na mapowanie tożsamościowe. Skutek uboczny: domyślne
`burza.bmp`/`wiatr.bmp` z commita `7052cef` były wyświetlane do góry nogami;
zostały przegenerowane poprawnie.

Generatory zaktualizowane, żeby 24-bit nie wracał:
- `tools/make_splash.py` i `tools/make_qsl_splash.py` zapisują teraz 16-bit RGB565;
- `tools/gen_worldmask.py` czyta 16- i 24-bit (`iss_worldmask.h` przegenerowany).

Uwaga: przeglądarki poprawnie renderują 16-bit BMP (BI_BITFIELDS) — sprawdzone
dla `/mapa_swiat.bmp` i podglądu splash.

## Przycisk „esp32-ham-clock-s3.zip" → repozytorium GitHub (31.10.2026)
- Wszystkie przyciski/linki „⬇ esp32-ham-clock-s3.zip" (pasek pobierania oraz
  box „Najnowsza wersja") prowadzą teraz do
  `https://github.com/Tom50505050/esp32-ham-clock-s3` (`target="_blank"
  rel="noopener"`), zamiast do lokalnego `/esp32-ham-clock-s3.zip`.
- Zmienione w `data/index.html` (PL) i `data/indexEN.html` (EN); DE/FR/ES/RU
  wygenerowane przez `tools/gen_pages.py`. Po edycji HTML trzeba przegenerować
  `.gz` (`tools/make_gz.py`) i przebudować LittleFS.

## Szybsze łączenie z WiFi i szybsze ładowanie WWW (13.11.2026)
- **Szybki reconnect WiFi (bez pełnego skanu).** Po udanym połączeniu STA
  firmware zapamiętuje kanał i BSSID AP w NVS (`wifi_ch`, `wifi_bssid`,
  `persistWifiChannel()`). Przy następnym starcie `connectToWiFi()` łączy się
  wprost na tym kanale (`WiFi.begin(ssid,pass,channel,bssid)`) — bez skanu
  wszystkich kanałów (oszczędność kilku sekund). Gdy zapamiętany kanał nie
  zadziała (restart routera), robi jeden pełny skan i ponawia próbę. Zmiana
  SSID w WWW (`/api/save`) unieważnia zapamiętany kanał.
- **Szybsza drabinka mocy TX.** Najpierw jedna szybka próba przy pełnej mocy
  (6 s) na znanym kanale; pełna drabinka 19,5/15/11 dBm (3×9 s) uruchamia się
  dopiero gdy szybka próba padnie. `wifiAttemptAtPower()` przyjmuje teraz BSSID
  jako argument (wcześniej globalne `g_targetBssid`).
- **gzip dla statycznych zasobów WWW.** `tools/make_gz.py` tworzy w `data/`
  wersje `*.gz` (index*.html, style*.css, satellite.js) — kompresja ~5×.
  Firmware (`streamStaticAsset()` w main.cpp) serwuje `.gz`, gdy klient wyśle
  `Accept-Encoding: gzip`; WebServer sam dodaje `Content-Encoding: gzip`
  (rozpoznaje rozszerzenie). Do tego `Cache-Control` (HTML 10 min, CSS/JS 1 h).
  Nagłówek `Accept-Encoding` jest zbierany przez `collectHeaders()`.
  **Uwaga:** po każdej zmianie `data/*.html|css|js` uruchom `tools/make_gz.py`
  przed `pio run -t buildfs` — inaczej LittleFS poda nieaktualny `.gz`.
  `tools/make_release.sh` robi to automatycznie.
- Efekt: pierwsze załadowanie strony ~1,6 MB → ~300 kB transferu, a powrót do
  strony (bez zmian zasobów) nie pobiera CSS/JS ponownie.

## Moduły ładowania (pełna lista) + kolor linków „Przydatne zasoby" (13.11.2026)
- **Lista modułów ładowania rozszerzona** z 4 na 14 układów. `chargerModuleName()`
  (main.cpp) zwraca teraz: 0=TP4056, 1=TP4100, 2=IP2312, 3=CN3065, 4=TP4054,
  5=TP4057, 6=TP5000, 7=MCP73831, 8=MCP73833, 9=BQ24074, 10=BQ24075, 11=CN3791,
  12=IP5306, 13=IP5328, 14=„Inny". Dodano `CHARGER_MODULE_MAX = 14`; walidacja
  w `loadPreferences` i POST `/api/save` używa tej stałej (wcześniej sztywne 4).
- **WWW – zakładka „Moduły", sekcja „🔋 Akumulator i moduł ładowania"**: select
  `charger_module` ma wszystkie 15 pozycji (PL `data/index.html` + EN
  `data/indexEN.html`, regenerowane DE/FR/ES/RU przez `tools/gen_pages.py`) oraz
  w awaryjnym HTML w `main.cpp`. Etykiety układów to nazwy własne (nie tłumaczone).
- **Kolor linków**: w zakładce „Linki" linki w tabeli „Przydatne zasoby"
  (`.links-mobile-shell .prop-metrics-table td a`) mają teraz ten sam kolor co
  linki w tabeli „Polecane" (aprst.pl): `var(--accent-2, #4ea8ff)` = #4ea8ff
  (stan normalny, `:visited`, `:hover`). Zmiana w `data/style.desktop.css` i
  `data/style.mobile.css`.

## Monitor ESP32-S3: GPS/ładowanie/pojemność + WWW (13.11.2026)
- **Ekran „Informacje systemowe" (ESP32-S3 MONITOR)**: karta HARDWARE pokazuje
  teraz dodatkowo **moduł GPS** (z profilu `gps_module_profile`) i **moduł
  ładowania**; karta MEMORY & POWER pokazuje **napięcie + % + pojemność
  akumulatora + typ akumulatora**. Funkcje pomocnicze `gpsModuleName()`,
  `chargerModuleName()`; karty poszerzone do 7/8 wierszy, wykres i logi
  przesunięte niżej (y=200..310).
- **Nowe zmienne konfiguracyjne (NVS)**: `chargerModuleType` (int, klucz
  `chg_type`, patrz pełna lista w sekcji „Moduły ładowania" wyżej), `batteryCapacityMah`
  (int, klucz `bat_mah`, domyślnie 3000) oraz `batteryType` (String, klucz
  `bat_type`, dowolny opis typu/chemii ogniwa, maks. 24 znaki, domyślnie
  „18650 Li-ion"). Ładowane w `loadPreferences`, zapisywane w `savePreferences`,
  wystawione w API JSON jako `charger_module`, `battery_mah` i `battery_type`
  (GET `/api/config` + POST `/api/save`).
- **WWW – zakładka „Moduły"**: sekcja „🔋 Akumulator i moduł ładowania"
  (pojemność mAh + typ akumulatora + wybór modułu ładowania) znajduje się w
  zakładce **Moduły** (`ensureModulesTab*()`), a nie w formularzu głównym.
  Przycisk „Zapisz Konfigurację" pozostał na samym dole formularza głównego;
  zakładka Moduły ma własny „Zapisz ustawienia Moduły". Pola są czytane
  null-safe w `collectConfigPayload` (elementy mogą nie istnieć, gdy
  zakładka Moduły nie została jeszcze zbudowana).
- **Zakładka „Linki"**: usunięto kafelek „🖼 Wgraj ekran startowy"; wiersz
  „Postaw kawę ☕" nie zawiera już linku (puste pole). Zmiany we wszystkich
  stronach: `data/index.html` (PL) oraz `indexEN/DE/FR/ES/RU.html`
  (regenerowane z `tools/gen_pages.py` + `tools/lang_pages.json`) i w awaryjnym
  HTML w `main.cpp` (tam także nowa zakładka „Moduly" w `showTab`).

## Ustawienia TFT: usunięty KOLOR ZEGARA + kafelka KALIBRACJA (30.10.2026)
- **Usunięto suwak KOLOR ZEGARA** z ekranu ustawień TFT (`drawBrightnessMenu`,
  otwierany 3 s przytrzymaniem ekranu). Zostaje tylko JASNOŚĆ + KOLOR MOTYWU
  (`menuThemeHue`, zapisywany w NVS jako `menu_hue`). Kolor cyfr zegara
  (`clockDigitsColor`) pozostaje w NVS (`clock_color`) i nadal jest ustawiany
  z WWW („Kolor zegara", `clock_color_picker`) oraz z menu na ekranie.
- **Menu koloru cyfr zegara (TFT) — PRZYWRÓCONE**: długie przytrzymanie
  (>=1,2 s) NA ZEGARZE na ekranie HAM CLOCK otwiera `openClockColorMenu()`
  (suwaki HUE/SAT/VALUE + podglad + ZAPISZ/DOMYSLNE/ZAMKNIJ). Funkcje:
  `drawClockColorMenu`, `handleClockColorMenuTouch`, `openClockColorMenu`,
  `drawClockColorPreview`, `redrawClockColorControls` i zmienne
  `clockColorMenu*`/`clockColorMenuActive`. Zapisywane do NVS `clock_color`.
  Uwaga: ten suwak istnieje TYLKO w menu przytrzymania — w ekranie ustawień
  TFT go nie ma (tam zostaje KOLOR MOTYWU).
- **Dodano kafelkę KALIBRACJA** na ekranie ustawień TFT (obok ZAPISZ/DOMYŚLNE/
  UŚPIENIE/ZAMKNIJ; przyciski zwężone do 86 px, 5 w jednym rzędzie, ZAMKNIJ
  na końcu). Wejście w nią uruchamia `startTouchCalibration()` (4 punkty).
- **Stara kalibracja usunięta**: długie przytrzymanie (>=5 s) na ekranie
  ustawień już NIE startuje kalibracji.
- **Wynik kalibracji jest teraz zapisywany trwale**: po 4. punkcie firmware
  przypisuje `touchXMin/Max`, `touchYMin/Max` i woła `savePreferences()`
  (NVS `touch_xmin/xmax/ymin/ymax`), a ekran podsumowania pokazuje
  „Saved. Touch to close". Wcześniej kalibracja nic nie zapisywała
  (był tylko podgląd surowych wartości + podpowiedź „Use WWW panel").

## v1.2d — czcionki zegara, alert pogodowy, migotanie (23.10.2026)
Wydanie zbiorcze (wcześniej planowane jako v1.2d + v1.2e, scalone w jedno **v1.2d**):
- **Wybór czcionki znaku i zegara** — 14 krojów VLW + wbudowana, menu TIME MODE
  (siatka 5×3), zapis w NVS `clock_font`.
- **Własne grafiki alertu pogodowego (burza/wiatr)** — pliki w LittleFS, ścieżki w
  NVS `wxal_ic_s/wxal_ic_w`, trasy `/upload_wxicon_*`, `/wxicon_preview`,
  `/wxicon_reset`; podgląd 480×320 w WWW.
- **Naprawa migotania cyfr zegara (wszystkie 15 czcionek)**: dirty-cells w
  `clockDrawMonospace()` (rysuje tylko zmienione komórki, ~67% mniej pikseli na
  SPI), `clockTimeInkOffset()` (stały pionowy offset `"0123456789:"`, koniec
  drgania 1 px), guard przerysowania w `updateScreen1ClockClassic()`.
  Podgląd QA: `tools/render_clock_flicker_preview.py`.
- **`FIRMWARE_VERSION` 1.2c → 1.2d**; `data/index*.html` (6 języków),
  `tools/lang_pages.json`, `platformio.ini`, README/RELEASE_NOTES zaktualizowane.

## Grafiki alertu pogodowego: restart ESP32 przy wgrywaniu ikony przez WWW (30.10.2026)
- **OBJAW:** przy wgrywaniu przez WWW grafiki ikony alertu (wiatr/burza) ESP32
  restartowal sie. Log uzytkownika pokazal `IntegerDivideByZero` (EXCCAUSE 6) w
  littlefs, nie WDT.
- **PRZYCZYNA (potwierdzona backtrace):** brak miejsca w LittleFS (ENOSPC).
  Log: `Free: 299008` B, a wgrywana ikona WWW (16-bit 480x320) to **307266** B.
  Zapis 307 kB do FS z ~299 kB wolnego wyczerpuje bloki; littlefs wchodzi w
  sciezke braku miejsca i dzieli przez zero (cache/metadane) => panic =>
  restart. Backtrace z `firmware.elf` (mini addr2line w Pythonie, bez
  toolchainu) wskazal `lfs_file_flushedwrite` w
  `components/esp_littlefs/src/littlefs/lfs.c`. To NIE jest blad skalera ani WDT.
- **KLUCZOWY FAKT:** `drawWeatherAlertIconFile()` dla wgranej ikony NIGDY nie
  wchodzi w petle skalowania - WWW zawsze wysyla obraz dokladnie **480x320**
  (`WX_ICON_W/H`), wiec dziala szybka sciezka `drawBmp16FromFS(path,0,0)`.
- **ZAUWAZONE PRZY OKAZJI (naprawione):** skalowana sciezka czytala naglowek BMP
  z pominietym polem zarezerwowanym (`readBmp16; readBmp32` zamiast
  `readBmp16; readBmp32; readBmp32`), przez co `planes` = wysokosc obrazu i
  warunek `planes==1` byl zawsze falszywy. Poprawka: pelny odczyt naglowka
  (dodany offset danych). Kod byl jednak martwy dla ikon 480x320 - nie to
  powodowalo restart.
- **POPRAWKA GLOWNA:** domyslne ikony `/icon50/wiatr.bmp` i `/icon50/burza.bmp`
  byly 24-bit (460854 B kazda). Przekonwertowane do 16-bit RGB565 BI_BITFIELDS
  (307266 B, `tools/bmp24to16.py`) - jak pliki z WWW. Wolne miejsce w FS wzroslo
  z ~610 kB do ~913 kB, wiec upload 307 kB sie miesci.
- **POPRAWKA OCHRONNA (przed ponownym panicem):** przed zapisem sprawdzane jest
  wolne miejsce (`totalBytes - usedBytes`, margines 64 kB) wobec `Content-Length`
  (`server->clientContentLength()` - NIE `header("Content-Length")`, bo WebServer
  nie ma tego naglowka w `header()`; poprzednia kontrola byla martwa). Dodatkowo
  kontrola w trakcie `RAW_WRITE` chroni upload chunked bez Content-Length. Przy
  braku miejsca upload jest odrzucany, plik czyszczony, a trasa zwraca **HTTP 507**
  (`sendUploadResult`) - WWW pokazuje blad zamiast "OK".
- **WOLNE WGRYWANIE IKON (30.10.2026):** pierwsza wersja kontroli w trakcie zapisu
  wolala `LittleFS.usedBytes()` co 8 blokow. `usedBytes()` -> `lfs_fs_size()` ->
  `lfs_fs_rawtraverse()` przechodzi **caly** system plikow (~2016 blokow po 4 kB),
  czyli ~27 pelnych przejsc po 7.9 MB na jedna ikone. Dodatkowo kazde przejscie
  czyta flash synchronicznie w zadaniu `loop()`, blokujac odbior gniazda (klient
  musi czekac, dopoki bufor RX sie nie oprozni) - upload trwal dlugo. Poprawka:
  wolne miejsce liczone **raz** w `RAW_START` (`rawSpaceLeft`), a w petli tylko
  pomniejszane (kontrola O(1)). `usedBytes()` w `/api/sysinfo` pozostaje (wolane
  na zadanie), ale nie w sciezce uploadu.
- **QA:** backtrace zdekodowany z `firmware.elf` (pyelftools + DWARF); wolne
  miejsce FS zmierzone w `littlefs.bin` (littlefs-python: 1867 -> 1793 bloki
  uzyte). Buildy `esp32-s3-n16r8` i `esp32-s3-n16r8-notouch` SUCCESS.
- **WWW: GPS Lat/Lon z 8 miejscami po przecinku.** Panel "GPS status na zywo"
  (`updateGpsLiveStatus`) formatowal `Number(d.lat).toFixed(5)` - zmienione na
  `toFixed(8)` we wszystkich 6 `data/index*.html` (12 miejsc). `gpsLat`/`gpsLon`
  w firmware to `double`, wiec 8. miejsce jest znaczace. Uwaga: ArduinoJson
  6.21.6 dla `double` serializuje maks. 9 cyfr znaczacych i obcina zera
  koncowe (np. `52.12345678` przechodzi, `52.10000000` da `52.1`).
- **NIE MOZNA BYLO ZAPISAC 8 MIEJSC W "TWOJA STACJA" (30.10.2026).** Dwie
  przyczyny:
  1. Pola `user_lat`/`user_lon` mialy `step="0.000001"` (6 miejsc) -> HTML5
     constraint validation odrzucal 8-cyfrowa wartosc (np. `50.22769165`),
     przegladarka blokowala `submit` i pokazywala "Podaj prawidlowa wartosc",
     wiec `saveConfig()` w ogole nie bylo wolane. Poprawka: `step="any"` w
     wszystkich 6 `data/index*.html` + w zapasowej stronie w `main.cpp`
     (`step="0.0001"` -> `step="any"`).
  2. Firmware zapisywal pozycje jako **float32** (`putFloat`/`getFloat`) ->
     tylko ~7 cyfr znaczacych, 8. miejsce gubione juz przy zapisie do NVS.
     Poprawka: nowe klucze NVS `user_lat_d`/`user_lon_d` z pelnym `double`
     (8 B przez `putBytes`), funkcje `prefPutUserPos`/`prefGetUserPos`
     (odczyt: double, fallback na stary float - zgodnosc wstecz). Zapis jest
     podwojny (double + float), wiec starszy firmware nadal odczyta pozycje.
     Pola `double userLat/userLon` byly juz typu double - zmiana tylko w NVS.

## Grafiki alertu pogodowego (burza/wiatr) — pełnoekranowy layout 408×320 (22.10.2026)
- **Ekran alertu: grafika 408×320 + czerwony napis.** Lewy panel (408×320 px)
  wypelnia grafika burzy/wiatru w trybie **fit=contain** (bez skalowania w gore i
  bez przycinania, tlo czarne). Napisy pozostawione bez zmian merytorycznych
  (nazwa zjawiska, poryw `m/s`, `w ciagu 6h`), ale wyśrodkowane na ekranie i w
  **kolorze czerwonym** na czarnym prostokacie z ramka (akcent: czerwony dla
  burzy, pomaranczowy dla wiatru). Przycisk X w prawym gornym rogu bez zmian.
- **Firmware `drawWeatherAlertIconFile(path)`.** Bez parametrow cx/cy/maxSide -
  skaluje obraz do okna 408×320 (fit=contain) i wypelnia caly lewy panel.
  Bufor docelowy 408×320×2 B (~261 kB) trzymany w **PSRAM** (`heap_caps_malloc`
  z `MALLOC_CAP_SPIRAM`, alokowany raz). Obsluga BMP 16/24-bit z odczytem
  wierszowym i resamplingiem nearest liczonym po pikselach docelowych
  (bezpieczne takze dla pomniejszania). Szybka sciezka: obraz 1:1 z panelem
  rysowany wprost (`drawBmpFromFS`/`drawBmp16FromFS`). PNG/JPG: nadal nie sa
  skalowane przez firmware (naglowek uzywany tylko do wymiarow), wiec wgrywaj BMP.
- **WWW (6 jezykow).** Podglad ekranu na `<canvas>` 480×320 rysuje grafike
  408×320 (fit=contain) + czerwony wysrodkowany napis + X. Upload konwertuje
  obraz w przegladarce do **408×320 BMP 16-bit RGB565** (`buildBmp565`).
  Opis sekcji w `lang_pages.json` zaktualizowany (170×170 → 408×320) i strony
  DE/FR/ES/RU wygenerowane przez `gen_pages.py`.
- **QA:** `tools/render_weather_alert_preview.py` → `weather_alert_preview.png`
  (dwa ekrany 480×320: burza + wiatr).
- **Artefakty:** env `esp32-s3-n16r8` i `-notouch` `pio run` + `buildfs` SUCCESS,
  `merge_bin` → `dist/`, `python3 tools/make_zip.py`.

## Grafiki alertu pogodowego (burza/wiatr) — własne ikony z WWW (22.10.2026)
- **Zakładka ALERT POGODOWY (WWW, 6 języków).** Nowa sekcja „Grafiki alertu
  (burza / wiatr)": dwa pola wyboru pliku (JPG/PNG/BMP/GIF/WEBP), przyciski
  **Wgraj** i **Domyślna**, podgląd miniatury oraz **podgląd całego ekranu
  alertu** na `<canvas>` 480×320 (1:1 z firmware). Nowe stringi UI dodane do
  `tools/lang_pages.json` dla DE/FR/ES/RU i wygenerowane przez `gen_pages.py`.
- **Konwersja w przeglądarce.** Wybrany obraz jest skalowany do **408×320**
  (fit=contain, bez skalowania w górę i bez przycinania, czarne tło) i
  konwertowany do **BMP 16-bit RGB565**
  (`buildBmp565(rgba, w, h)` — dodane parametry wymiarów) i wysyłany jako RAW
  POST na `/upload_wxicon_storm` / `/upload_wxicon_wind`.
- **Firmware.** Nowe globalne `weatherIconStormPath` (domyślnie
  `/icon50/burza.bmp`) i `weatherIconWindPath` (`/icon50/wiatr.bmp`), NVS
  `wxal_ic_s` / `wxal_ic_w`, pola `weather_icon_storm` / `weather_icon_wind`
  w `GET/POST /api/config`. Upload zapisuje `/wxicon_storm.<ext>` (lub
  `/wxicon_wind.<ext>`) i aktualizuje ścieżkę w NVS. Trasy `/wxicon_preview`
  (podgląd) i `/wxicon_reset` (powrót do domyślnej). `drawWeatherAlertScreen()`
  rysuje ikonę z pliku przez nowy `drawWeatherAlertIconFile()` (odczyt wymiarów
  z nagłówka BMP/PNG/JPEG, skalowanie nearest-neighbour), a gdy plik brak —
  wbudowaną ikonę wektorową.
- **Domyślne ikony:** `data/icon50/burza.bmp` i `data/icon50/wiatr.bmp`
  (24-bit BMP 50×50, czarne tło) generowane przez `tools/make_weather_icons.py`.
  Podgląd QA: `tools/render_weather_alert_preview.py` → `weather_alert_preview.png`,
  porównanie przed/po: `tools/render_weather_alert_before_after.py`.
- **Układ ekranu (23.10.2026).** Grafika burzy/wiatru zajmuje cały lewy panel
  **408×320** (`WX_ICON_TARGET_W/H`, fit=contain, bez skalowania w górę i bez
  przycinania); prawy pasek 408..479 zostaje czarny (tam przycisk X w prawym
  górnym rogu). Napisy **czerwone** (`TFT_RED`), wyśrodkowane na środku ekranu
  (x=240), na **przezroczystym tle** — rysujemy tylko ramkę akcentu
  (`drawRect`; czerwona dla burzy, pomarańczowa dla wiatru) i sam tekst
  (`setTextColor(TFT_RED)` bez koloru tła). Wcześniej pod napisami był czarny
  prostokąt (`fillRect`) — usunięty na prośbę użytkownika („sama ramka i napisy
  bez czarnego tła”). Wiersze: nazwa zjawiska (rozm. 4), poryw „NN m/s”
  (rozm. 3), `w ciagu 6h` (rozm. 2). Podgląd WWW na `<canvas>` rysuje to samo.
- **Artefakty:** oba env `pio run` + `buildfs` SUCCESS, `merge_bin` → `dist/`,
  `python3 tools/make_zip.py` (630 wpisów).

## TCPIP TNC-KISS (APRS po LoRa/Digi) + preset DX Cluster (21.10.2026)
- **Nowy moduł UART2 `aprs_tnc_kiss`.** W zakładce Modules (WWW, 6 języków)
  można wybrać `APRS_TNC-KISS (TCPIP TNC / LoRa / Digi)`; zapisywany w NVS jako
  `uart2_connected_module`. Beacon APRS budowany jest jako ramka **AX.25 (UI)** i
  wysyłany w **KISS przez TCP** do TNC (LoRa APRS iGate/Digi, Direwolf).
- **Pola KISS w WWW** (wszystkie języki): `aprs_kiss_enabled`, `aprs_kiss_host`,
  `aprs_kiss_port` (domyślnie 8001), `aprs_kiss_path` (domyślnie `WIDE1-1,WIDE2-1`).
  Firmware: NVS `aprs_kiss_en/h/p/pth`, JSON `GET/POST /api/config`
  (walidacja portu 1-65535, trim hosta/ścieżki). Beacon do APRS-IS jest wtedy
  wyłączony (jedno źródło TX); `kissSendBeacon()`/`connectToKiss()`/
  `handleKissData()`/`kissHandleStartupSequence()` wołane w `loop()` niezależnie
  od APRS-IS (sekwencja startowa: KISS ON po 30 s, RESTART po 1 s).
- **Pola APRS dodane do WWW:** `aprs_tocall` (NVS `aprs_tocall`, znacznik w polu
  po `>`), `aprs_tnc2_monitor` (NVS `aprs_tnc2_mon`, 2-liniowy monitor Yaesu),
  `aprs_alert_block` + `aprs_alert_block_enabled` (NVS `aprs_alrt_blk` /
  `aprs_alrt_blk_en`).
- **Preset DX Cluster** (`cluster_preset` + `applyClusterPreset()`): uzupełnia
  host/port (dxspots.com:7300, dxfun.com:8000, cluster.dxzone.com:7300,
  dxc.ve7cc.net:23, rk3dwa.ru:8000). Obecny w PL/EN i w tłumaczeniach.
- **Tłumaczenia generowane z EN.** `data/indexDE|ES|FR|RU.html` powstają przez
  `python3 tools/gen_pages.py` z `data/indexEN.html` + `tools/lang_pages.json`.
  Nowe stringi (m.in. `GPS module profile:`, profil GPS w zakładce Modules,
  etykiety KISS/TOCALL/monitora/blokady, preset DX Cluster) dodane do słownika
  dla wszystkich 4 języków — **nie edytować plików DE/ES/FR/RU ręcznie**, bo
  regeneracja je nadpisze. Markup wewnątrz literałów JS (zakładka Modules)
  dopasowywany jest kluczami z wiodącym `>` (konwencja gen_pages.py).
- **Manual w 6 językach:** nowa sekcja **5.9b TCPIP TNC-KISS** w
  `data/instrukcja.txt`, `data/manual.txt`, `manual{DE,ES,FR,RU}.txt`.
- **Artefakty:** `pio run` dla 4 env SUCCESS, `buildfs` OK; JS walidowany
  `node --check` dla wszystkich 6 stron WWW.

## Instrukcja w 6 językach + ikona satelity + opis ekranów (21.10.2026)
- **Ikona satelity GPS zielona po fixie.** `gpsIconStatusColor()` rozdzielone na
  `gpsIconState()` (0=brak modułu, 1=szuka, 2=fix) i kolor: szary `0x6BF2`,
  bursztynowy `0xFE4A`, **zielony `0x07E0`** gdy `gpsFix`. Nowy
  `refreshGpsIconIfChanged()` (main.cpp ~3980) wołany w `loop()` obok
  `drawBatteryQuickUpdate` odświeża samą ikonę po zmianie stanu (tylko ekran
  HAM CLOCK, z pominięciem `inMenu`/menu jasności/wygaszacza). `gpsIconLastState`
  trzyma ostatnio narysowany stan, żeby nie odświeżać bez potrzeby.
- **Manual w 6 językach.** Nowe pliki `data/manualDE.txt`, `manualFR.txt`,
  `manualES.txt`, `manualRU.txt` (pełne tłumaczenia EN, sekcje 1-6 + opis
  wszystkich ekranów + lista ikon). Trasy `/manualDE.txt` … `/manualRU.txt`
  w main.cpp (~20991, pętla po `manualFiles[]`) streamują plik z LittleFS,
  z fallbackiem na `/manual.txt`. `tools/gen_pages.py` podmienia
  `manualUrl` na `/manual<LANG>.txt` w generowanych stronach DE/FR/ES/RU
  (PL zostaje `/instrukcja.txt`, EN `/manual.txt`).
- **Opis funkcji wszystkich ekranów** w sekcji 4 każdego manuala (14 ekranów:
  HAM CLOCK, DX CLUSTER, APRS-IS, APRS RADAR, BAND INFO, SUN SPOTS, WEATHER,
  WEATHER FORECAST, POTA, HAMALERT, MATRIX, PSK MAP, SYSTEM INFO, ISS, STATION,
  LOGGER) + liczba slotów zaktualizowana z 11 na **14**.
- **Artefakty:** env `esp32-s3-n16r8` i `-notouch` SUCCESS, `buildfs` OK;
  `littlefs.bin` zawiera 4 nowe manuale; `merged-flash*.bin` + ZIP (**594
  wpisy**, w tym `data/manual{DE,FR,ES,RU}.txt`).

## Auto czas letni/zimowy + T_IRQ opcjonalny (21.10.2026)
- **Automatyczna zmiana czasu (DST).** Reguła europejska: ostatnia niedziela
  marca 01:00 UTC → +1 h, ostatnia niedziela października 01:00 UTC → powrót do
  czasu zimowego. `isEuropeanDST(struct tm*)` (UTC) już istniała i jest zgodna
  z bazą IANA (sprawdzone dla 2018-2030 bez rozbieżności). Dodano:
  - nowy przełącznik `bool autoDst` (domyślnie `true`), NVS **`dst_auto`**,
    pole `dst_auto` w eksporcie/imporcie JSON (`GET/POST /api/config`);
  - **`currentTimezoneOffsetHours()`** (main.cpp ~1688) — offset `timezoneHours`
    + ewentualne DST, liczony **na bieżąco z UTC** (usunięto stary 60-sekundowy
    cache `getCachedTimezoneOffset`, który po zmianie roku trzymał zły offset);
  - wszystkie 4 ścieżki DST sprawdzają teraz `autoDst`: `currentTimezoneOffsetHours`,
    `getTimeWithTimezone`, `formatAprsTime` (APRS) i obliczenia dobowe pogody;
  - **WWW**: checkbox „Automatyczna zmiana czasu letni/zimowy (Europa)” obok pola
    strefy czasowej (`data/index.html` + `indexEN.html`), z JS-owym
    `isEuropeanDST()` w `formatAprsTimeLocal` (spójny podgląd czasu lokalnego);
  - **TFT**: w menu TIME MODE (długie przytrzymanie zegara) nowy wiersz
    `AUTO DST` z kaflami **ON / OFF** (y=162), zapis NVS od razu.
- **T_IRQ opcjonalny.** `XPT2046_Touchscreen` obsługuje `tirq=255` = brak pinu:
  wtedy `begin()` **nie** podłącza przerwania, a `update()` nie ustawia
  `isrWake=false`, więc `touched()` (pomiar Z ≥ 300) działa w trybie
  **odpytywania** — dotyk działa bez T_IRQ. Zmiany:
  - `tft_setup.h` + `main.cpp`: nowe makro **`TOUCH_USE_IRQ`** (domyślnie `0`);
    obiekt `touch` tworzony jako `(TOUCH_CS, TOUCH_IRQ)` tylko gdy
    `TOUCH_USE_IRQ=1`, inaczej `(TOUCH_CS, 255)`;
  - nowy env `esp32-s3-n16r8-touchirq` (`-DTOUCH_USE_IRQ=1`) dla płyt z T_IRQ;
  - PINOUT.md + wbudowany manual (`/manual`) opisują T_IRQ jako opcjonalny.
- **Artefakty:** 3 env SUCCESS (`esp32-s3-n16r8`, `-notouch`, `-touchirq`);
  `littlefs.bin` z nowymi stronami; `merged-flash*.bin`, ZIP 590 wpisów.

## POTA: kraj + odległość z API zamiast QRZ (21.10.2026) — dzięki EA7JTP
- **Objaw:** na ekranie POTA kolumna COUNTRY pokazywała `-` (dane pojawiały
  się dopiero po wolnym zapytaniu QRZ, a dla wielu znaków wcale).
- **Przyczyna:** API `api.pota.app` **nie ma pola `country`** — kod czytał
  `spot["country"]`, które nie istnieje, więc `s.country` zostawało puste.
  Kraj jest w polu **`locationDesc`** (prefiks ISO, np. `US-TX`, `ES-M`,
  `DE-BW`, `GB-ENG`), a pozycja aktywatora w `latitude`/`longitude` oraz
  kwadratach `grid4`/`grid6`.
- **Fix (`fetchPotaApi`):** filtr JSON wzbogacony o `locationDesc`,
  `latitude`, `longitude`, `grid4`, `grid6`, `reference` (pula 512 → **768 B**);
  `potaCountryFromLocationDesc()` mapuje prefiks ISO na nazwę kraju (kolumna
  COUNTRY działa od razu, bez QRZ); lat/lon (a gdy brak — `grid6`/`grid4` przez
  `locatorToLatLon`) trafiają do `DXSpot.lat/lon/hasLatLon`, więc odległość
  liczy się lokalnie. **QRZ dopytujemy tylko o brakujący kraj.**
- Weryfikacja na żywym API: 91/91 spotów ma rozwiązany kraj i pozycję.

## 6 języków UI + alerty pogodowe/burzowe (21.10.2026)
- **Języki TFT: PL/EN/DE/FR/ES/RU** (`enum TftLang`, `uint8_t tftLanguage`).
  Napisy przez `tr(TrKey)` (tablice `TR_PL..TR_RU`, 35 kluczy) oraz
  `trStr(pl,en,de,fr,es,ru)` dla etykiet spoza tabeli. Kody dla API/WWW:
  `tftLangToCode` / `tftLangFromCode`.
- **Czcionki VLW nie mają cyrylicy**, a część ekranów używa wbudowanego Font 2
  (tylko ASCII) — dlatego **wszystkie napisy UI są ASCII-only** (RU =
  transliteracja łacińska). Opisy pogody (OpenWeatherMap zwraca je w języku
  interfejsu) przechodzą przez `foldToAscii()` (PL + Latin-Ext + transliteracja
  cyrylicy) — patrz ekran pogody.
- **WWW: 6 stron** `data/index.html` (PL), `indexEN/DE/FR/ES/RU.html`, przełącznik
  języków na każdej stronie + trasy `server->on("/indexXX.html")` w firmware.
  Strony DE/FR/ES/RU generowane z EN przez `python3 tools/gen_pages.py`
  (słownik `tools/lang_pages.json`; klucze = dokładne węzły tekstowe oraz łańcuchy
  w `<script>` — komunikaty statusu/alertów JS — z `data/indexEN.html`); teksty są
  UTF-8 (przeglądarka renderuje poprawnie, także cyrylicę). Apostrofy w tłumaczeniach
  FR są automatycznie escapowane w literałach JS. Po zmianie `indexEN.html` uruchom
  generator ponownie.
- **Uzupełnienie ES/tłumaczeń (21.10.2026)**: dodano brakujące nazwy sekcji z
  emoji (np. `🌐 TFT Language`, `📡 Your Station`, `📁 Settings Backup`,
  `🎯 TFT Calibration`) oraz komunikaty JS (`Showing … spots POTA`,
  `Error loading …`, `Reset …`, statusy GPS/BTSPP) do **DE/FR/ES/RU**
  (po ~503 klucze na język). **Uwaga w generatorze:** globalna podmiana kluczy
  zaczynających się od znaku nie-alfanumerycznego dotyczy tylko etykiet z emoji;
  klucze zaczynające się **od spacji** (fragmenty zdań jak `" POTA spots"`) są
  podmieniane wyłącznie przez dopasowanie całego literału JS — inaczej
  `str.replace` korumpowałby całe zdania (np. `fetch спотов POTA`). Literały
  szablonowe w backtickach (`\`BT Module: ${x}\``) są podmieniane **tylko w
  całości** (klucz zawiera placeholdery `${...}`), żeby nie uszkodzić interpolacji.
- **Alert pogodowy/burzowy**: firmware pobiera `open-meteo.com` (bez klucza API,
  `fetchOpenMeteoAlerts()` co 15 min) i wykrywa burzę (WMO 95-99 lub CAPE ≥ 2000)
  oraz silny wiatr (próg m/s) w horyzoncie 6 h. Ekran `drawWeatherAlertScreen()`
  (reuse infrastruktury ekranu APRS ALERT: `weatherAlertScreenActive`,
  `isAprsAlertCloseButtonHit`). Konfiguracja: NVS `wxal_storm/wxal_wind/
  wxal_windms/wxal_url`, pola WWW `weather_alert_*` + `openmeteo_url`.

## Precyzyjny pomiar baterii: kalibracja ADC + krzywa Li-ion (27.10.2026)
- **Kalibracja eFuse zamiast surowego `analogRead()`**: `readBatteryVoltage()` używa
  `analogReadMilliVolts()` (rdzeń używa `esp_adc_cal` z eFuse Two Point / Vref
  wbudowanego w każdy ESP32-S3), więc znika dominujący błąd offsetu i nieliniowości
  surowych kodów ADC. `analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db)` w `setup()`
  (pełny zakres 0-3.3 V); `analogSetAttenuation()` globalnie zostaje jako fallback.
- **Średnia obcięta**: `batteryTrimmedMeanMv(12, 2)` pobiera 12 próbek, sortuje i
  odrzuca po 2 skrajne z każdej strony (średnia z 8 środkowych). Pojedynczy błędny kod
  ADC (0/4095) nie rozciąga już wyniku.
- **Korekcja tolerancji dzielnika**: `batteryDividerCal` (mnożnik, NVS `bat_div_cal`)
  i `batteryVoltOffset` (offset V, NVS `bat_off`). Wzór:
  `Vbat = (Vpin_mV/1000) * 2.0 * bat_div_cal + bat_off`.
- **Krzywa rozładowania Li-ion** zamiast liniowej mapy: tablica `kBatteryCurve[]`
  z interpolacją liniową w `calculateBatteryPercentage()`.
  Liniowa mapa 3.4-4.2 V zawyżała „50%" (ogniwo trzyma ~3.7-3.8 V przez większość czasu).
  Np. 3.80 V → 47% (było 50%), 3.70 V → 33% (było 37.5%), 3.60 V → 18% (było 25%).
  **UWAGA (31.10.2026):** tabela została później przeskalowana na 3.30-3.80 V
  (3.80 V = 100%) — patrz sekcja „Skala procentowa baterii: 3.8 V = 100%" wyżej.
- **Histereza ładowania**: `batteryCharging` ustawiane przy >4.08 V, kasowane przy
  <3.98 V (od 31.10.2026; wcześniej progi 4.25/4.10 V, potem 4.0/3.9 V, 4.3/4.2 V). Piorun czerwony.
- **Kalibracja jednopunktowa w WWW** (zakładka System → „Kalibracja baterii"):
  pole `bat_cal_v` = rzeczywiste napięcie z multimetru; firmware mnoży
  `bat_div_cal *= vref/raw` tylko gdy `|vref-raw| > 50 mV` (bez tego każdy „Zapisz"
  kumulowałby korektę). Pola `bat_div_cal` i `bat_off` edytowalne wprost.
- **API**: `/api/config` eksportuje `bat_div_cal`, `bat_off`, `sys_vcc`, `sys_batt_pct`,
  `sys_batt_chg`; `/api/save` przyjmuje `bat_cal_v`, `bat_div_cal`, `bat_off`.
  Naprawiona literówka `#ifdef ENABLE_BATTERY_MONITORING` → `BATTERY_MONITORING_ENABLED`
  (wcześniej `sys_vcc` zawsze zwracał 0).
- **Ikona - ujednolicona (27.10.2026)**: w kodzie były **dwie** funkcje
  `drawBatteryIcon` - martwa `drawBatteryIcon(x,y,w,h)` (20×10, własna logika
  kolorów, bez błyskawicy) oraz `drawBatteryIcon(x,y,percentage)` (20×10 w
  `(x, y+2)`) faktycznie rysująca nagłówki na WSZYSTKICH ekranach (zegar
  klasyczny/nowoczesny, `uiHeader`, splash). Nagłówki używały tej drugiej, więc
  wcześniejsze prace nad ikoną ich nie obejmowały. Teraz jest **jedna** funkcja
  `drawBatteryIcon(x, y, percentage = -1, bg = TFT_BLACK)`:
  wspólne `drawBatteryFill()` + `drawBatteryBolt()`, kolor z `getBatteryColor()`
  (histereza ładowania), wnętrze czyszczone kolorem tła nagłówka (`bg`), więc
  poziom może maleć bez starego wypełnienia, a przy ładowaniu rysowana jest
  błyskawica. Układ współrzędnych zgodny z `drawBatteryQuickUpdate` (ramka na
  `y+2`, wnętrze od `y+4`) - brak skoku o 2 px przy szybkiej aktualizacji.
  Miejsca wywołań przekazują `bg`: zegar klasyczny `TFT_RADIO_ORANGE`, zegar
  nowoczesny `HC_BG`, `uiHeader` `bg` (klasyczny/modernowany). Usunięto martwe
  `drawBatteryIcon(x,y,w,h)` i `drawBatteryStatus()` (nigdzie niewołane).
  `tools/render_header_preview.py` zsynchronizowany.
- **Uwaga WWW**: strona `/config` jest serwowana z LittleFS (`data/index*.html`),
  a `getMainHTML()` w `main.cpp` to tylko fallback. Panel kalibracji dodano w
  **obu miejscach**: `main.cpp` (fallback) oraz 6 plikach `data/index*.html`
  (PL/EN/DE/FR/ES/RU) - HTML `#battery-calibration`, JS `applyConfigToUI()`
  (odczyt `bat_div_cal`/`bat_off`/`sys_vcc`) i `collectConfigPayload()`
  (`bat_div_cal`/`bat_off`/`bat_cal_v`).

## Bateria na GPIO7 + ciągłe odświeżanie ekranu GPS (21.10.2026)
- **Pin dzielnika napięcia baterii: GPIO7 (ADC1_CH6)** — `BATTERY_ADC_PIN 7`
  (`main.cpp`). Wcześniej GPIO1 (ADC1_CH0); zmienione 27.10.2026 na GPIO7.
  Schemat na stronie WWW (`getMainHTML`) oraz `PINOUT.md` (tabela
  pinów + listy piny zarezerwowane/wolne) zgodne z GPIO7.
  Przykład „fałszywego odczytu 0.21 V" dotyczy GPIO7 (wolny pin = dzielnik
  niepodłączony, ADC czyta śmieci).
- **Ekran informacji GPS odświeża się ciągle (bez migania)**: `updateGpsScreen()`
  (wołany w `uiTaskLoop` co ~10 ms) rysuje pełny ekran co **300 ms** (było
  **1500 ms**). Cały ekran trafia do sprite w PSRAM (`gpsScreenSprite`, 480×320,
  16 bpp) i jest wypychany **jednym** `pushSprite()` — inaczej kolejne etapy
  rysowania (`fillScreen`, karty, tekst) było widać jako miganie. Ten sam
  wzorzec co `aprsRadarSprite`. Gdy brak PSRAM, rysowanie idzie wprost na `tft`.

## v1.2c — poprawki PSK MQTT i nagłówka (18.10.2026)
- **PSK MQTT — brak spotów po zmianie pasma/trybu**: realny temat brokera ma
  **11 segmentów**, a subskrypcja budowała filtr **6-segmentowy**
  (`.../<pasmo>/<tryb>/+/+/+`), który nie pasował do żadnego tematu. Zmiana na
  **`.../<pasmo>/<tryb>/#`**. Dodano mapowanie pasm **160/30/17/12 m** (HTTP i
  MQTT), poprawiono indeks odbiorcy (5→6), po zapisie filtra czyszczone są spoty,
  a resubskrypcja odsubskrybowuje stary temat.
- **Ikona baterii 12 px w lewo** na wszystkich ekranach: zegar
  `drawBatteryIcon(screenW-70,8)` → `screenW-82,8`, `uiHeader`
  `drawBatteryIcon(420,1)` → `408,1`, `rightLimit` 415→403; mirror w
  `drawBatteryQuickUpdate()`.
- **`tools/render_header_preview.py`** zsynchronizowany z firmware (398/408/
  `screenW-82`).
- **`FIRMWARE_VERSION` 1.2b → 1.2c**; zaktualizowane `data/index.html`,
  `data/indexEN.html`, `tools/make_qsl_splash.py`.

## GPS w nagłówku + ekran satelitów, nowe logi online (17.10.2026, v1.2b)
- **Ikona GPS** w nagłówku ekranu zegara (`drawGpsIconInHeader(bool classic)`,
  rysowana tylko gdy `gpsEnabled`) - oba style nagłówka (klasyczny i nowoczesny).
  Ikona jest po **lewej** stronie nagłówka, za godziną HH:MM. Stałe:
  `GPS_ICON_X=88, GPS_ICON_Y=9, GPS_ICON_W=26, GPS_ICON_H=18`; obszar dotyku
  `gpsIconHitX=82, gpsIconHitY=2, gpsIconHitW=42`. Dotknięcie otwiera
  ekran satelitów (`openGpsScreen()`).
- **Ikona baterii na wszystkich ekranach**: `uiHeader()` (wspólny nagłówek
  pozostałych ekranów) rysuje `drawWifiSignalBars(445,1)` + `drawBatteryIcon(420,1)`.
  `drawBatteryQuickUpdate(bool skip)` dorysowuje tylko wypelnienie w tej samej
  obudowie (zegar: `x=screenW-70,y=8`; inne ekrany: `x=420,y=1`) - obudowa i
  terminal zostaja z naglowka. W `loop()` wolane z `skipScreenCheck=true`, wiec
  poziom odswieza sie takze poza ekranem zegara.
- **Ekran satelitów** (`gpsScreenActive`, `updateGpsScreen()` w `loop()`): lista
  satelitów, wykres nieba (sky plot) i paski C/N0 (SNR). Geom. okna: sky plot
  `plotR=70`, paski SNR max 12 wierszy (krok 6 px) - mieści się w oknie.
  Guardy: pending redraw / `updateScreen1` / auto-switch pomijane gdy
  `gpsScreenActive`.
- Dane satelitów: `gpsSatList` (GSV), `gpsUsedInFix`/`gpsViewSats` (GSA/GGA).
  Wystawione w WWW (`/api/status`, `/api/uart_status`) jako `gps_sats_used` /
  `gps_sats_view`; UI pokazuje `SAT: used/view`.
- **Nowe usługi wysyłki ADIF**: HRDLog.net, HamQTH.com, HAMLOG.online + fix
  eQSL. Dystrybutor `loggerUploadAdif()` (`qrz|eqsl|clublog|hrdlog|hamqth|hamlog`).
  Nowe dane w NVS: `hrdlog_callsign`, `hrdlog_code`, `hamqth_user`, `hamqth_pass`,
  `hamqth_callsign`, `hamlog_api` (+ wcześniej `eqsl_qth`, `clublog_api`).
  eQSL: `loggerUploadToEqsl()` używa **multipart/form-data** (pole `Filename`
  + nagłówek ADIF `EQSL_USER`/`EQSL_PSWD`); urlencoded jest odrzucane.
  `jsonEscape()` - forward-declared przed użyciem w loggerze.
- UI WWW (`data/index.html`, `data/indexEN.html`): nowe sekcje HRDLog/HamQTH/
  HAMLOG, opcje w `log_upload_service`, przycisk **Test połączenia**
  (`logbookTest()` -> `POST /api/log_test`), pola w load/save configu.
- **Paczka ZIP**: `tools/make_zip.py` pakuje TYLKO scalone obrazy
  `merged-flash.bin` i `merged-flash-notouch.bin` (16 MB, offset `0x0`)
  oraz źródła, `data/**`, `tools/*.py` i `tools/*.json` (słownik
  `lang_pages.json` potrzebny do regeneracji stron).
  Scalanie: `python -m esptool --chip esp32s3 merge-bin -o dist/merged-flash.bin
  --flash-mode dio --flash-freq 80m --flash-size 16MB 0x0 bootloader.bin
  0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin
  0x700000 littlefs.bin` (analogicznie `merged-flash-notouch.bin` z
  `firmware-notouch.bin`).
- **Dystrybucja tylko scalona**: w wydaniach GitHub i w dokumentacji
  udostepniamy WYLACZNIE `merged-flash.bin` i `merged-flash-notouch.bin`.
  Wgrywanie przez przegladarke: https://tom50505050.github.io/esp32-ham-clock-s3/flash/
  (plik + offset `0x0`). Osobne `firmware.bin`, `littlefs.bin`, `bootloader.bin`,
  `boot_app0.bin`, `partitions.bin` usuniete ze wszystkich wydan i z dokumentow.
- **Opisy na GitHub**: repo + wszystkie release'y - fraza wyświetlacza zmieniona
  na `ILI9488 na 3.5" i 4" 480×320`. UWAGA: token integracji nie ma uprawnień
  `administration=write`, więc **opis repo** trzeba zmienić ręcznie (release'y
  aktualizują się przez API). Ta sama fraza zaktualizowana w dokumentach
  (README.md, README_S3.md, INSTRUKCJA.md, PINOUT.md, RELEASE_NOTES.md).

## Ekran STACJA (15. ekran) — wyszukiwarka znaku (16.10.2026, v1.2b)
- Nowy ekran `SCREEN_STATION = 15` (`station`): wpisujesz znak, firmware pobiera
  dane kolejno ze zrodel **QRZ.com -> HamDB -> callook.info** (pierwsze trafienie
  wygrywa; nazwa zrodla pokazywana na ekranie). Funkcje: `stationLookupStart()`,
  `fetchHamDbCallsignInfo()`, `drawStationScreen()`, `stationDrawResults()`,
  `drawStationKeyboard()`, `handleStationTouch()`, `stationDrawLoading()`.
- Dwa transceivery (`stationDrawRadio()`): lewy **czerwony** = Twoj znak, prawy
  **zielony** = szukany. Napis znaku jest **czarny na tle w kolorze radia**
  (wyswietlacz radia = kolor radia, tekst czarny) - korpusy radia zachowuja kolory.
- Karta wyniku: **Imie / QTH / Grid / Kraj**, linia **dystans + kierunek**,
  pod nimi **zrodlo + godzina pobrania** (HH:MM, czas lokalny).
- Kierunek geograficzny **skrotami**: `N, NE, E, SE, S, SW, W, NW` (8 azymutow po
  45 stopni), liczony `calculateBearing()` z lokatora/QTH wzgledem pozycji usera.
  Wiersz np. `42 km  NE`. Linie fal miedzy antenami kolorowane wg odleglosci
  (`propPolarityColor()`).
- Ekran respektuje globalny przelacznik stylu (`uiStyleClassic()`) - uklad i dane
  sa WSPOLNE dla stylu klasycznego i nowoczesnego (rozni sie tylko paleta).
- UI WWW: nowa pozycja `station` w `data/index.html` i `data/indexEN.html`
  (lista stron 14 -> 15).
- Podglad 1:1: `tools/render_station_preview.py` (Pillow) -> pliki
  `stacja-1-start.png` ... `stacja-7-nieznaleziony.png` + `stacja-podglad.png`.

## Ekran ESP32 HAM LOGGER (16. ekran) — dziennik ADIF (17.10.2026, v1.2b)
- Nowy ekran `SCREEN_HAM_LOGGER = 16` (`logger`): formularz QSO + tabela
  ostatnich lacznosci + 4 przyciski dotykowe. Funkcje: `drawLoggerScreen()`,
  `drawLoggerForm()`, `drawLoggerKeyboard()`, `handleLoggerTouch()`,
  `loggerHandleKeyboardKey()`, `loggerLookupCallsign()`, `loggerSaveCurrentQso()`,
  `loggerSaveAll()`, `loggerLoadFromFs()`.
- Pola: **CALLSIGN** (zolta ramka), **RST (S)/(R)**, **BAND/FREQ/MODE**,
  **NAME/QTH**, **DATE/TIME** (UTC z NTP; `GMT_OFFSET_SEC = 0`). BAND i MODE
  przelaczaja sie cyklicznie dotknieciem; zmiana pasma podstawia czestotliwosc
  z `loggerBandFreq[]`.
- **Log w PSRAM**: `LogQso` (`logbook`, `LOGGER_LOG_MAX=256`) alokowany przez
  `ps_malloc` (fallback `malloc`) w `loggerEnsureBuffer()`. NIE zapisujemy do
  flasha przy kazdym dotknieciu - tylko na **SAVE QSO**.
- **Bezpieczny zapis**: `loggerSaveAll()` pisze do `/qso.adi.tmp`, potem
  `LittleFS.rename()` na `/qso.adi` - utrata zasilania nie uszkadza bazy.
- **ADIF bajtowo poprawny**: `loggerAdifField()` liczy dlugosc pola w BAJTACH
  UTF-8 (`String::length()`), nie w znakach (np. `Knurów` = 8 bajtow). Naglowek:
  `ADIF_VER` / `PROGRAMID` / `PROGRAMVERSION`.
- Autouzupelnianie NAME/QTH po zamknieciu pola CALLSIGN - ten sam lancuch co
  STACJA: QRZ.com -> HamDB -> callook.info (`loggerLookupCallsign()`).
- UI WWW: pozycja `logger` w `data/index.html` / `data/indexEN.html`
  (lista stron 15 -> 16, nowy `screen_slot_16`). Endpointy:
  `GET /qso.adi` (pobranie pliku ADIF) oraz `POST /api/qrz_upload`
  (wysylka do QRZ Logbook - **gotowe na przyszlosc**, wymaga `qrzApiKey`,
  pole "QRZ.com API Key (Logbook)" w ustawieniach; zapis w NVS `qrz_api_key`).
  Klucz API QRZ sluzy TYLKO do logbooka - wyszukiwanie znakow idzie przez
  login+haslo XML API (`qrz_user`/`qrz_pass`).
- Podglad 1:1: `tools/render_logger_preview.py` (Pillow) -> `logger-1-nowy.png`,
  `logger-2-wpisane.png`, `logger-3-klawiatura.png`, `logger-4-klasyczny.png`,
  `logger-podglad.png`.
- Nowy ekran wymaga dopisania w KILKU miejscach: enum `ScreenType`, obie tablice
  `DEFAULT_SCREEN_ORDER`/`screenOrder` (+ `SCREEN_PAGE_COUNT`/`SCREEN_ORDER_COUNT`
  15->16), `normalizeScreenType()`, `screenTypeToCodeStr()`/`screenCodeToType()`,
  `drawScreen()` (naglowek + case), dispatch dotyku i `uiFooter`-guard.

## ISS: "brak danych" najblizszy/nastepny przelot — PRZYCZYNA = TLE z celestrak (15.10.2026)
- OBJAW: na ekranie ISS TRACKER panele "NAJBLIZSZY PRZELOT"/"NASTEPNE
  PRZELOTY" pokazywaly "brak danych", mimo ze pozycja ISS i kraj dzialaly.
- PRZYCZYNA (potwierdzona logiem szeregowym z urzadzenia): `fetchIssTLE()`
  pobieral TLE TYLKO z `https://celestrak.org/...gp.php?CATNR=25544`, a to
  zrodlo z sieci uzytkownika zwracalo **HTTP kod -1** (brak polaczenia) przy
  KAZDEJ probie. `wheretheiss.at` (pozycja) dzialal, wiec to nie byl brak
  sieci. Bez TLE `issTleValid=false`, wiec `updateIssNextPassEstimate()`
  wychodzilo wczesnie ("TLE jeszcze niedostepne") i UI pokazywalo "brak
  danych". Matematyka SGP4 byla OK - zweryfikowane na hoscie na prawdziwym
  TLE (initpredpoint+nextpass zwracalo przeloty).
- POPRAWKA: `fetchIssTLE()` probuje teraz KOLEJNO kilku niezaleznych zrodel
  (pierwsze udane wygrywa):
  1. celestrak gp.php (jak dotad),
  2. AMSAT `https://www.amsat.org/tle/current/nasabare.txt` (lista wielu
     satelitow - nowy `extractIssTleFromText()` wyciaga blok "1 25544"/"2 25544"),
  3. SatNOGS `https://db.satnogs.org/api/tle/?norad_cat_id=25544` (JSON: tle0/1/2).
  Kazde zrodlo loguje `[ISS TLE] <nazwa>: HTTP kod N` / `Zrodlo OK: <nazwa>`.
- Dodatkowo: wejscie na ekran ISS (`issForceNextPassRefresh`) probuje teraz
  pobrac TLE, jesli `!issTleValid`, zamiast czekac na kolejna probe w issTask.
- Uwaga diagnostyczna: `HTTP kod: -1` = nie udalo sie POLACZYC (nie 4xx/5xx).
  Gdy pozycja dziala a TLE nie, to problem jest specyficzny dla danego hosta
  (blokada IP / geo / TLS), dlatego rozwiazaniem jest lancuch zrodel zapasowych.

## PSK MAP: tryb "polaczenia" (TX>RX) zamiast linii od usera (27.10.2026)
- Nowa opcja w menu ekranu PSK (przycisk "Lacza: TAK/NIE" w prawym gornym rogu
  menu) oraz pole NVS `psk_show_links`. Domyslnie WYLACZONE = dotychczasowe
  zachowanie (kropka stacji + linia od pozycji uzytkownika, kolor wg dystansu DX).
- Wlaczone: dla kazdego spotu rysowana jest linia **nadawca -> odbiorca** z
  dwoch koncow raportu PSKReporter (kropka TX wieksza, RX mniejsza), bez linii
  od usera. Lista spotow pokazuje pare `TX>RX` zamiast `CALL band mode`.
- Zrodlo odbiorcy:
  - MQTT (v2raw): JSON `rc`/`rl`, a gdy brak - segment 6 (znak) i 8 (lokator)
    tematu `pskr/filter/v2raw_1pc/<pasmo>/<tryb>/<nad>/<odb>/<lokN>/<lokO>/...`.
  - HTTP (`pskreporter.info/query`): pola `receiverCallsign`/`receiverLocator`
    (dodane do filtra deserializacji; wczesniej nie byly pobierane).
- `PskSpot` i `PskSpotMqtt` rozszerzone o `rxCall`/`rxLat`/`rxLon`; dedup
  pozostaje znak+pasmo+tryb. `/api/psk` zwraca dodatkowo `rx_call`/`rx_lat`/`rx_lon`.
- Wspolny rysunek jednego spotu: `drawPskSpotGlyph()` (uzywany przez pelne i
  przyrostowe odswiezanie).

## PSK MAP: ramka spotow przeniesiona na lewa strone (16.10.2026)
- Ramka `drawPskSpotListPanel()` byla po prawej (x=360..476, y=40..290) i
  zaslaniala Azje oraz Australie (ok. 31% pikseli tego obszaru to lad).
- Nowa pozycja: x=4..132, y=126..286 - pas poludniowego Atlantyku/Oceanu
  Poludniowego, w ktorym mapa ma <1% ladu (sprawdzone na `data/Mapa swiata.bmp`
  przez zliczanie pikseli roznych od koloru oceanu). Nic nie zaslania.
- Wiersze listy: 22 -> 14 (mniejsza wysokosc ramki).
- Podglad 1:1 przed/po: `tools/render_pskmap_preview.py` ->
  `/workspace/share/pskmap-podglad.png` (wymaga Pillow).

## PSK Reporter: "nie dziala" = limit IP po stronie PSKReporter (15.10.2026)
- OBJAW: ekran PSK MAP rysuje mape + czerwona kropke usera, ale zero spotow
  i zero linii DX.
- PRZYCZYNA (potwierdzona logiem z urzadzenia ORAZ reprodukcja z hosta):
  PSKReporter dlawi IP, ktore zadaje zapytania zbyt czesto. Serwer odpowiada
  wtedy **HTTP 200 z samym `{"message":"Your IP has made too many queries too
  often..."}`** (bez `receptionReport`) albo **HTTP 429**. Kod rozpoznawal to
  jako "Brak danych: brak pola receptionReport". W logach urzadzenia widac
  dokladnie ten komunikat i serie `[PSK] HTTP error: 429`.
  Uwaga: `receiverCallsign` + `flowStartSeconds` dzialaja - ten sam URL z hosta
  zwraca 57 raportow (wszystkie z senderLocator i sNR). Czyli parser i URL sa
  OK; blokada jest po stronie API/IP, nie kodu.
- CO POGARSZALO SPRAWE: kazde zdarzenie (tap na mape, ZAPISZ, wejscie) moglo
  wystrzelic OSOBNE zapytanie. Kilka zapytan w sekunde = natychmiastowy limit,
  a kolejne proby tylko odswiezaja blokade (limit nigdy nie mija).
- ZASADY PSKReporter (pskreporter.info/pskdev.html): nie pobierac czesciej niz
  raz na 5 min, NIE powtarzac tego samego zapytania, dodawac `appcontact=<email>`.
- POPRAWKA (hardening na kodzie z 14.10):
  1. Wspolny straznik `pskFetchAllowed(now)` dla WSZYSTKICH miejsc pobrania:
     blokuje, gdy trwa backoff po limicie ORAZ gdy od ostatniego pobrania
     minelo mniej niz `PSK_MIN_FETCH_INTERVAL_MS` (5 min). Tap/ZAPISZ/auto-refresh
     nie moga juz powtorzyc tego samego zapytania.
  2. `PSK_RATE_LIMIT_BACKOFF_MS` 5 min -> 10 min (limit IP trzyma dluzej niz 5 min).
  3. Naglowek mapy przy braku spotow pokazuje "Odswiez. za N min" zamiast pustki
     (tap w trakcie straznika wygladal na zepsuty).
  4. Log diagnostyczny: przy kazdym pominiętym pobraniu `[PSK] Fetch wstrzymany:
     ...` z powodem i licznikiem sekund; przy limicie `[PSK] LIMIT (429|200+message)`.
  5. Eskalacja backoffu: kolejne limity z rzedu 10 -> 20 -> 30 min (max
     `PSK_MAX_BACKOFF_MS`), zeby IP mialo czas "odpoczac" - dobijanie co 10 min
     tylko przedluzalo blokade.
  6. 429 ustawia tez `pskRateLimited=true` (wczesniej flaga zostawala stara,
     wiec naglowek nie pokazywal limitu).
  7. Banner wersji przy starcie (`===== ESP32-HAM-CLOCK v1.2b =====`) - pozwala
     potwierdzic, ze wgrany jest nowy firmware (FIRMWARE_VERSION 1.2a -> 1.2b).
- DIAGNOSTYKA: "Spoty: 0" + brak "LIMIT PSKReporter" = zapytanie poszlo, ale
  zero wynikow (zly `receiverCallsign` / pusty lokator / okno czasu). "LIMIT
  PSKReporter - poczekaj" = IP zablokowane, odczekac. Po restarcie domowego IP
  (albo po prostu po odczekaniu) i odpytywaniu z umiarem mapa zaskoczy.

## PSK Reporter tryb MQTT: zly temat = 500+ msg/s (16.10.2026)
- OBJAW: tryb MQTT (`psk_mqtt_en=1`) tez nie pokazywal spotow.
- PRZYCZYNA: firmware subskrybowal `pskr/filter/v2/#` = CALY swiat, ~565 msg/s.
  ESP32 nie nadaza parsowac JSON-a z takiego strumienia (callback + deserializeJson
  na kazdej wiadomosci), kolejka sie zapycha i nic nie dochodzi do bufora.
- FORMAT TEMATOW (ustalone empirycznie, broker mqtt.pskreporter.info:1883):
  - `pskr/filter/v2_field/<pasmo>/<tryb>/<poleNadawcy>/<poleOdbiorcy>`  (~10 msg/s dla JO)
  - `pskr/filter/v2/<pasmo>/<tryb>/<nad>/<odb>/<lokN>/<lokO>/<az>/<az>`  (~565 msg/s - za ciezki)
  - `pskr/filter/v2_field/#` = wszystkie pola swiata (~250 msg/s)
  - `pskr/rxinfo/<call>` = metadane odbiornikow (bez spotow)
  - Segmenty v2_field: [0]pskr [1]filter [2]v2_field [3]pasmo [4]tryb [5]nad [6]odb
  - Segmenty v2:        [0]pskr [1]filter [2]v2       [3]pasmo [4]tryb [5]nad [6]odb [7]lokN [8]lokO
- POPRAWKA:
  1. `getPskMqttTopicForLocator()` buduje `pskr/filter/v2_field/<pasmo>/<tryb>/+/<pole>`
     z ustawien (pasmo/tryb z NVS, pole = 2 pierwsze znaki lokatora). Gdy brak
     pasma/trybu -> `+`; gdy brak lokatora -> fallback `pskr/filter/v2/#`.
  2. Callback czyta lokator odbiorcy z idx 6 (v2_field) lub idx 8 (v2) i porownuje
     tyle znakow, ile ma segment (2 dla pola, 4 dla kwadratu) - wczesniej na sztywno
     idx 8 + 4 znaki, co dla v2_field nie pasowalo do niczego.
  3. Resubskrypcja w `loopPskMqtt()` gdy zmieni sie temat (lokator/pasmo/tryb) -
     bez tego zmiana ustawien nie dzialala az do rozlaczenia.
- Uwaga: JSON spotu uzywa krotkich kluczy `sc/sl/rc/rl/md/f/rp/b` - kod juz je czyta.

## PSK MQTT: crash + lawina odswiezen + gubiony dotyk (16.10.2026)
- OBJAWY: po wlaczeniu MQTT ekran PSK odswiezal sie w kolko (pelne przerysowania
  z "Screen cleared" kilka razy na sekunde), mapa migala, a po chwili
  `Guru Meditation / LoadProhibited` (PC w taskSelectHighestPriorityTaskSMP =
  uszkodzony stos). Dodatkowo przycisk menu trzeba bylo dotykac kilka razy.
- PRZYCZYNY (trzy, nakladajace sie):
  1. REENTRANCJA: `drawPskMap()` wolalo `loopPskMqtt()` -> `processPskMqttBuffer()`
     -> `drawPskMap()` (dopoki bufor niepusty). Przy ~10 msg/s = lawina pelnych
     przerysowan ("ekran sie co chwile odswieza").
  2. WYSCIG MIEDZY ZADANIAMI: `loopPskMqtt()` wolano z DWOCH zadan - `loop()`
     (petla glowna, rdzen 1) i `UI_Task` (rdzen 1, prio 2). PubSubClient i bufor
     spotow nie sa bezpieczne watkowo -> korupcja sterty -> crash w schedulerze.
  3. WYSCIG NA DANYCH: `pskSpots[]` pisane z `loop()` (kopiowanie bufora) i
     czytane w `UI_Task` (`drawPskMap`) - wyscig na String-ach = korupcja sterty.
- DODATKOWO: `reconnectPskMqtt()` blokuje do 5 s (`delay(100)` w petli), a
  wczesniej wolane bylo z `UI_Task` - blokowalo odczyt dotyku (XPT2046 dzieli
  SPI z TFT), przez co tapniecia przepadaly (menu trzeba bylo dotykac kilka razy).
- POPRAWKA:
  1. MQTT obsluguje WYLACZNIE `loop()` (jedno zadanie). Z `drawPskMap()` i z
     `UI_Task` usunieto wolania `loopPskMqtt()`.
  2. `processPskMqttBuffer()` (loop) tylko ustawia `pskMqttDirty`.
     Kopiowanie bufora -> `pskSpots` robi `copyPskMqttToDisplay()` WYLACZNIE w
     `UI_Task`, tuz przed `drawPskMap()`.
  3. Bufor MQTT pod mutexem `pskMqttBufferMutex` (callback zapisuje, UI_Task kopiuje).
  4. Przerysowanie z MQTT ograniczone do `PSK_MQTT_DRAW_INTERVAL_MS` (1.5 s) -
     inaczej miganie i blokada dotyku.
  5. Przycisk menu powiekszony 30x26 -> 44x36, obszar dotyku 0..50 x 0..44
     (rezystancyjny panel trafial niedokladnie).
  6. Dodano przycisk RESET w menu PSK (czysci pskSpots + bufor MQTT, nie rusza
     ustawien) - `resetPskMapSpots()` wolane z UI_Task. Przyciski menu:
     ZAPISZ (70,210,70,30), RESET (150,210,70,30), ANULUJ (230,210,70,30).
  7. Log `drawPskMap START` zawiera teraz `spotow=N` - pozwala od razu
     stwierdzic, czy mapa dostaje spoty z bufora MQTT.
  8. Tlo mapy PSK buforowane w PSRAM (raz) - `pskMapBgLoad()`/`drawPskMapBackground()`.
     Wczesniej KAZDE odswiezenie robilo `fillScreen` + dekodowanie BMP 460 kB z
     LittleFS (~1 s) => ciagle "Screen cleared" i miganie. Teraz jeden
     `pushImage(swapBytes=true)` z bufora PSRAM (~60 ms). Cache unieważnia
     `pskMapBgInvalidate()` przy uploadzie mapy (/upload/bmp, /upload/mapa565).
     UWAGA: TFT_eSprite::pushSprite() wymusza swapBytes(false); dla surowego
     RGB565 z BMP trzeba uzyc pushImage z swapBytes(true) (jak drawBmp16FromFS).
  9. MQTT jest teraz DOMYSLNIE WLACZONE (`pskMqttEnabled = true`,
     `pskTempMqttEnabled = true`, getBool("psk_mqtt_en", true)) - start od razu
     bez HTTP. Ustawienie zapisane w NVS nadal ma priorytet.
 10. Spoty KUMULUJA sie na mapie (dedup: znak+pasmo+tryb, `pskAddSpot()`),
     czyszczone przyciskiem RESET albo AUTO-RESET po `PSK_MAX_SPOTS = 200`.
     - Auto-RESET jest w `pskAddSpot()`: gdy licznik dojdzie do 200, zeruje
       `pskSpotCount` i pierwszy spot z kolejnej porcji startuje nowy cykl
       (log: "[PSK] AUTO-RESET: osiagnieto 200 spotow - czyszcze mape").
     - `fetchPskReporterData()` NIE zeruje juz `pskSpotCount` (bylo na 20675)
       i NIE przerywa petli na limicie (break usuniety - inaczej auto-RESET
       nigdy by sie nie wyzwolil).
     - `copyPskMqttToDisplay()` dopisuje zamiast nadpisywac (bylo `pskSpotCount=0`).
     - Bez tego liczba spotow skakala (30 -> 2 -> 1 -> 11), bo kazde okno MQTT
       nadpisywalo tablice.
 11. Panel informacyjny po prawej stronie mapy (`drawPskSpotListPanel()`,
     obszar 310,40 166x250): licznik "SPOTY: N" + lista najnowszych (znak,
     pasmo, tryb). Nakladka rysowana PO spotach, PRZED stopka/menu.
 12. Odswiezanie przyrostowe (`drawPskMapSpotsOnly()`): przy nowych spotach z
     MQTT rysujemy TYLKO nowe spoty + panel + kropke uzytkownika + przycisk
     menu. Naglowek, pasek, ikona baterii, napisy, legenda i stopka zostaja
     nietkniete (bez migania). Klucz: `lastPskSpotCount` (ile juz narysowano).
     - `drawPskMap()` = pelne przerysowanie, ustawia `lastPskSpotCount = 0`
       na starcie i `= pskSpotCount` na koncu.
     - RESET i auto-RESET ustawiaja `lastPskSpotCount = 0` (wymuszaja pelne
       przerysowanie, zeby wyczyscic stare spoty).
     - `drawPskMapSpotsOnly()` robi `drawPskMap()` gdy `pskSpotCount <
       lastPskSpotCount` (po RESET/auto-RESET) oraz gdy `pskMapMenuOpen`.
     - Zalety: brak migania chromu + mniej pracy SPI niz pelny redraw.
- UWAGA: "SSB" NIE wystepuje na PSK Reporter (to glownie FT8/FT4/WSPR/CW).
  Filtr trybu SSB => zero spotow i pusty bufor MQTT (brak logow "Bufor: N").
  Jesli mapa pusta - najpierw sprawdz, czy Tryb = (wszystkie) albo FT8.
- DIAGNOSTYKA: crash z PC w `taskSelectHighestPriorityTaskSMP`/`_frxt_dispatch`
  i `EXCVADDR` ~0x48 to niemal zawsze korupcja sterty/wyscig, nie blad logiki.
  Adresy dekoduj: `xtensa-esp32s3-elf-addr2line -f -C -e .pio/build/<env>/firmware.elf <PC>`.





## KOLOR ZEGARA (cyfry zegara, styl nowoczesny) — 15.10.2026
- Nowa zmienna globalna `clockDigitsColor` (RGB565, domyslnie `0xFFE0` = zolty jak
  `HC_YELLOW`). Ustawia kolor **duzego zegara cyfrowego** HH:MM:SS na ekranie
  HAM CLOCK w stylu **nowoczesnym** (`hamClockModernTime()`,
  `updateScreen1ClockModern()`). Styl klasyczny (`drawHamClockClassic()`) nadal
  rysuje zegar na zolto - niezmienione.
- Ustawienie w USTAWIENIACH TFT (`drawBrightnessMenu()`): w stylu nowoczesnym
  suwak motywu (`drawThemeSlider()`) zmienia teraz **KOLOR ZEGARA**
  (etykieta `TR_CLOCK_COLOR` = "KOLOR ZEGARA", zamiast "KOLOR MOTYWU:").
  W klasycznym stylu ten sam suwak nadal ustawia kolor motywu (`menuThemeHue`).
  `setThemeFromTouch()` rozgalezia sie na `uiStyleClassic()`.
  `handleBrightnessMenuTouch()` - przycisk DOMYSLNE przywraca zolty,
  ZAMKNIJ przywraca `brightnessMenuPrevClockColor`.
- Menu pelnego koloru (HSV: odcien/nasycenie/jasnosc) otwiera sie
  **przytrzymaniem palca na zegarze** (>1200 ms, obszar `clockHitX/Y/W/H`
  aktualizowany w `hamClockModernTime()`). Funkcje: `openClockColorMenu()`,
  `drawClockColorMenu()`, `handleClockColorMenuTouch()` (analogicznie do
  KOLOR ZNAKU). Wspolny suwak: `drawHsvSlider(y, mode, hue, sat, val)`.
- Trwalosc: NVS `clock_color` (`loadPreferences`/`savePreferences`), 0 = czarny
  -> wymuszany zolty. API: `/api/config` GET/POST pole `clock_color`.
- WWW (`data/index.html`, zakladka "Twoja Stacja"): kolor-picker
  `#clock_color_picker` (domyslnie `#ffff00`), konwersja hex<->RGB565 jak dla znaku.

## EKRAN STARTOWY (splash) — 15.10.2026
- `data/splash.bmp` (480x320, 24-bit RGB, bottom-up) w stylu **karty QSL**,
  **bez znaku wywolawczego**:
  - glowny emblemat: **`ESP32-HAM-CLOCK`** (serif bold 36 pt, zloty cien +
    bialy napis + poswiata) z podpisem **`VERSION 1.2`** (cyjan) pod spodem
    (wczesniej byl duzy „QSL" — zastapiony),
  - ramka airmail (skosne paski czerwono-niebieskie), wewnetrzna zlota ramka,
    delikatna mapa swiata w tle, godlo PZK, stopka „ESP32-HAM-CLOCK ... 73!",
  - prawy gorny rog: **flaga PL** (bialy pas U GORY, czerwony U DOLU) + „POLAND"
    — poprzednio flaga byla podzielona pionowo (blad), poprawiona,
  - pola danych: `QTH=Poland`, `VERSION=1.2` (pole „LOCATOR/JO90AA" usuniete),
    `ORGANIZATION=PZK`, `BAND=HF / VHF / UHF`.
  - Rysowany przez `drawSplashScreen()` (po ekranie powitalnym; dotyk pomija).
- Generator: `tools/make_qsl_splash.py` (Pillow + fonty DejaVu; wymaga
  `data/Mapa swiata.bmp` i `data/pzk-logo.bmp`). Regeneracja:
  `python3 tools/make_qsl_splash.py` -> `data/splash.bmp`
  + podglad `/workspace/share/podglad-qsl-splash.png`. Wyjscie jest
  deterministyczne (identyczny md5 przy powtornej generacji).
- **Wgrywanie wlasnej grafiki bez zmian**: WWW (Linki -> "Wgraj ekran startowy")
  lub pliki `/splash.png|.jpg|.jpeg|.bmp` w LittleFS maja pierwszenstwo nad
  `splash.bmp` (patrz `splashCandidates` w `drawSplashScreen()`). Ten sam mechanizm
  dziala dla ekranu powitalnego (`/welcome.*`).

## Cel sprzętowy
- **MCU:** ESP32-S3-WROOM-1 **N16R8** (16 MB flash QIO + 8 MB PSRAM OPI)
- **Wyświetlacz:** zewnętrzny **ILI9488 na 3.5" i 4" 480×320** po SPI (TFT_eSPI)
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
- Endpointy API: `/api/config`, `/api/save`, `/api/spots`, `/api/aprs`, `/api/pota`, `/api/hamalert`, `/api/propagation`, `/api/uart_status`, `/api/gps`, `/api/bridge_bt_*`, `/api/reset_touch`, `/api/reset_tft_rotation`.
- Uploady: `/upload_splash` (pole `splash`, PNG/JPG/BMP → `/splash.<ext>`), `/upload/bmp` (pole `file`, BMP → `/Mapa swiata.bmp`), `/upload/mapa565` (pole `mapa`, BMP 16-bit RGB565 → `/Mapa swiata.bmp`), `/api/upload` (dowolny plik do LittleFS).
- `/api/uart_status` zwraca realny status modułu GPS (UART2). `/api/gps` to lekki endpoint odpytywany co 5 s przez zakładkę „Moduły" (fix, satelity, HDOP, lokator, lat/lon, piny UART).
- `/api/bridge_bt_*` to stuby (ten build nie ma modułu ESP32-BRIDGE) — zwracają stan „nieaktywny", żeby pełne UI działało bez błędów JS.

## GPS + automatyczny lokator (Maidenhead)
- **Wejście:** zakładka **„Moduły"** w WWW (sekcja „📍 Moduł GPS (NMEA 0183 / UART2)"). Pola: włącz GPS, baud, piny RX/TX, „używaj pozycji GPS jako Twojej pozycji", podgląd statusu na żywo + przycisk „Skopiuj lokator GPS do Stacji".
- **Backend:** `gpsEnabled`, `gpsBaud` (dom. 9600), `gpsRxPin` (dom. **18**), `gpsTxPin` (dom. **17**), `gpsUseForPosition` (NVS: `gps_en`, `gps_baud`, `gps_rx`, `gps_tx`, `gps_use`). UART2 = `HardwareSerial gpsSerial(2)`.
- **Parser NMEA** (bez biblioteki): GGA (pozycja, jakość, satelity, HDOP, wysokość) + RMC (status, data, prędkość). Akceptuje `$GPGGA`/`$GNGGA`/`$GLGGA`/`$GNRMC` itd.
- **Lokator:** `latLonToLocator(lat, lon, 6)` liczy 6-znakowy Maidenhead (np. Poznań 52.4064,16.9252 → **JO82LJ**). `locatorToLatLon()` pozostaje dla kierunku odwrotnego.
- **Nadpisanie pozycji:** gdy `gpsEnabled && gpsUseForPosition && gpsFix`, `userLat/userLon/userLocator` są brane z GPS dla wszystkich usług (APRS/PSK/radar). Po utracie FIX wracają wartości ręczne (`userLatManual` itd.). Zapis NVS zawsze utrwala wartości ręczne, nie GPS-owe.
- **Piny GPS:** wolne GPIO (dom. RX=18, TX=17). Nie używać 26–37 (flash/PSRAM), 19/20 (USB), 0/3/45/46 (strapping), 43/44 (UART0).
- Klucz zapisu `use_gps_if_active` (starsze UI, zakładka „Stacja") jest mapowany na `gpsUseForPosition` — oba checkboxy są zsynchronizowane.

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
- Instrukcja wbudowana w WWW (sekcja montażu + `getManualPL()`) zaktualizowana pod S3 (piny, adresy flash, bootloader 0x0, LittleFS 0x700000).

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

## Artefakty (v1.2b)
- `pio run -e esp32-s3-n16r8` i `-e esp32-s3-n16r8-notouch` — OK.
- `pio run -e esp32-s3-n16r8 -t buildfs` → `littlefs.bin`.
- Scalanie (esptool 5.x używa `merge-bin`): `python -m esptool --chip esp32s3
  merge-bin -o merged-flash.bin --flash-mode dio --flash-freq 80m --flash-size
  16MB 0x0 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000
  firmware.bin 0x700000 littlefs.bin` (wariant `-notouch` z `firmware-notouch.bin`).
- ZIP: `python3 tools/make_zip.py` → `esp32-ham-clock-s3.zip` (oba scalone obrazy).
- Wydania GitHub udostępniają TYLKO `merged-flash.bin` i
  `merged-flash-notouch.bin` + ZIP; wgrywanie przez WebSerial
  (https://tom50505050.github.io/esp32-ham-clock-s3/flash/, offset `0x0`).

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
2. **Fałszywy odczyt baterii `0.21V (0%)`** = GPIO7 (ADC1_CH6) niepodłączony
   (wisi) -> dzielnik nie zamontowany albo akumulator niepodłączony. To NIE usterka
   ESP. Dodano sanity-check: odczyt < 1.0 V -> brak pomiaru (bez fałszywego 0%
   i ostrzeżenia "podłącz ładowarkę"), w `updateBatteryStatus()` i przy starcie.
3. Pierwszy odczyt `4.05V (81%)` to wynik wstępny (przed ustabilizowaniem ADC) —
   teraz też filtrowany progiem 1.0 V.

### Uwaga o odczycie baterii
`BATTERY_ADC_PIN 7` = GPIO7 (ADC1_CH6) z dzielnikiem 100k/100k. Jeśli dzielnik
nie jest fizycznie podłączony, GPIO7 "wisi" i ADC zwraca śmieci (0.2-0.4 V).
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

## APRS RADAR pusty — ROZWIAZANE (14.10.2026)
Objaw: ekran APRS RADAR nie pokazywal zadnych stacji, panel APRS-IS "BLAD".
Diagnoza (testy live na serwerach aprs2.net, skrypt `/tmp/aprstest`):
1. **Serwer odrzucal login.** Domyslny `DEFAULT_APRS_PASSCODE` to `00000`, a
   `rotate.aprs2.net` (i cala pula aprs2.net) odpowiada na `pass 0`/`pass -1`
   komunikatem `# Login by user not allowed` i **nie wysyla ZADNYCH ramek** —
   mimo poprawnego polaczenia TCP (`aprsConnected=true`). Z poprawnym
   passcode (`pass 23472`) serwer odpowiada `# logresp ... verified` i sypie
   ramkami. To jedyna przyczyna pustego radaru.
2. Parsery firmware sa OK: port `parseAprsUncompressed/Compressed/Advanced`
   do Pythona zdekodowal 88/106 zywych ramek (zawody to `>`, `T#`, `:`,
   niepoprawny Mic-E — bez znaczenia dla pozycji).
Poprawka w `main.cpp`:
- `computeAprsPasscode(call)` — standardowy algorytm APRS-IS (xor 0x73E2),
  liczony z `getAprsBaseCallsign()` (bez SSID).
- `sendAPRSLogin()`: gdy `aprsPasscode <= 0` (domyslne 00000), passcode jest
  wyliczany z wpisanego znaku; recznie ustawiony (>0) ma pierwszenstwo.
Uwaga: `-1` jest zarezerwowane dla bram i aprs2.net je odrzuca.

## APRS RADAR - panel w ramce (14.10.2026)
- `aprsRadarRowValue()` skraca wartosci dluzsze niz szerokosc ramki i rysuje je
  proporcjonalnym fontem 2 (przy tym samym lewym marginesie); dlugie
  wspolrzedne SZER/DLUG i znak z SSID nie wychodza juz poza panel.
- `ARP_COL_DIM = 0xBDF7` (jasny szary) — etykiety czytelne na czarnym tle.

## PSK MAP pusty w trybie MQTT — ROZWIAZANE (14.10.2026)
Objaw: ekran PSK MAP (tryb MQTT) nie pokazywal zadnych spotow.
Przyczyna (weryfikacja na zywym brokerze `mqtt.pskreporter.info:1883`):
1. **Zly temat subskrypcji.** Kod subskrybowal `pskr/filter/v2/+/+/+/LOC4/#`,
   ale broker NIE grupuje tematow po lokatorze — realny uklad to
   `pskr/filter/v2/<pasmo>/<tryb>/<nadawca>/<odbiorca>/<lokN>/<lokO>/<az>/<az>`.
   Taki temat nie pasowal do niczego → 0 wiadomosci → pusty ekran.
   Poprawnie: subskrybuj `pskr/filter/v2/#` i filtruj w callbacku.
2. **Zle klucze JSON.** Kod czytal `sender`/`senderLocator`/`mode`/`frequency`,
   a broker wysyla krotkie: `sc`/`sl`/`rc`/`rl`/`md`/`f` (Hz)/`rp` (SNR).
3. **Zbyt wolny filtr.** Broker sypie ~240 spotami/s (payload do 176 B). JSON
   dla kazdego to za duzo dla ESP32 — filtrujemy najpierw po lokatorze
   odbiorcy wyczytanym wprost z tematu (`pskMqttTopicSegment(topic, 8)`),
   zostaje garstka spotow odebranych w naszym kwadracie (~17/5400 w 20 s).
Poprawka w `main.cpp`: `getPskMqttTopicForLocator()` → `pskr/filter/v2/#`;
`pskMqttTopicSegment()` (prefiltr po temacie); `pskMqttCallback()` czyta
`sc`/`sl`/`md`/`f`, zostawia spoty gdzie lokator odbiorcy == nasz kwadrat
(rysujemy NADAWCE/DX, jak tryb HTTP).
Uwaga: tryb HTTP (domyslny, `psk_mqtt_en=false`) byl sprawny —
`retrieve.pskreporter.info` z `rptlimit`/`flowStartSeconds` zwraca JSON z
dlugimi kluczami (`senderCallsign`...), ktore firmware czyta poprawnie.

## PSK MAP - gdzie ustawic lokator (14.10.2026)
`userLocator` (NVS `user_locator`) ustawia sie w trzech miejscach:
1. WWW → zakladka **Stacja** → "Locator (np. KN09)".
2. WWW → zakladka **PSK Reporter** → "Twoj Locator" (pole `psk_user_locator`,
   zsynchronizowane z polem w Stacji przez JS input->input; ten sam klucz).
3. Ekran dotykowy: menu PSK MAP → wiersz **"Lok:"** (pole 100,175,90,20,
   klawiatura `pskKeyboardTarget=5`). ZAPISZ kopiuje `pskTempLocator` do
   `userLocator`, zapisuje NVS i - gdy brak wprost LAT/LON - przelicza pozycje
   z lokatora (`updateUserLatLonFromLocator`).
Uwaga: pole "MQTT Znak" w menu dotykowym przeniesione na prawa strone wiersza
(310,175,110,20), zeby zrobic miejsce na "Lok:".

## Piny (skrot) - pelna tabela w PINOUT.md
TFT: SCLK=12, MOSI=11, MISO=14, CS=10, DC=9, RST=-1, BL=21.
Dotyk XPT2046 (wspolne SPI): T_CS=15, T_IRQ=16, T_DIN=11, T_DO=14, T_CLK=12.
Bateria ADC=1. RGB LED=4/5/6. Zarezerwowane: 26-37 (flash/PSRAM), 19/20 (USB).
Zmiana pinow: `tft_setup.h` lub `platformio.ini` build_flags (env `esp32-s3-n16r8-altpins`).

## Serwer plikow (localhost:12000)
Workery: work-1 (12000) i work-2 (12001). Serwer pada przy resecie srodowiska.
Restart: `bash tools/serve.sh` (http.server 12000 w /workspace/share).
**Wazne:** skrypt musi uzywac `setsid ... < /dev/null &` - zwykle `nohup &`
+ `disown` jest ubijane po zakonczeniu komendy w tym srodowisku (proces nie
odczepia sie od sesji i pada razem z terminalem). Sprawdzaj `ps aux | grep
http.server` po restarcie.


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
- Wgrywanie: merged-flash.bin (0x0) albo osobno littlefs.bin (0x700000).


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

## Grafika na ekran: JPG/PNG/BMP -> RGB565 (14.10.2026)
- **Wejscie:** zakładka „Ekran TFT" → sekcja „🖼️ Grafika na ekran (JPG / PNG / BMP)".
- Konwersja odbywa się **w przeglądarce** (canvas + `getImageData`), bez obciążania ESP32:
  - skalowanie do natywnych **480×320** (tryby: `contain` zachowaj proporcje / `cover` wypełnij+przytnij / `stretch` rozciągnij),
  - pakowanie kolorów do **RGB565** (natywny format ILI9488): `((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3)`,
  - zapis jako **BMP 16-bit, BI_BITFIELDS (compression=3), bottom-up, maski R=0xF800 G=0x07E0 B=0x001F**, wiersze wyrównane do 4 B (480 px × 2 B = 960 B → bez paddingu). Plik = 66 + 307200 = 307266 B.
- Ten format czyta `drawBmp16FromFS()` bez żadnej dalszej konwersji (sterownik dostaje gotowe RGB565).
- **Cele uploadu:** „Ekran startowy (splash)" → `/upload_splash` (pole `splash`); „Tło mapy (PSK/ISS)" → `/upload/mapa565` (pole `mapa` → `/Mapa swiata.bmp`). `drawPskMap()` próbuje teraz 24-bit (`drawBmpFromFS`) ORAZ 16-bit RGB565 (`drawBmp16FromFS`).
- **Skrypt offline:** `python3 tools/img_to_bmp565.py obraz.jpg [wyjscie.bmp] [--fit contain|stretch|cover]` generuje identyczny plik RGB565 480×320 (byte-for-byte zgodny z konwerterem JS — zweryfikowane testem).
- UWAGA: UI nie wymaga już Pillow; firmware NIE dekoduje PNG/JPG samodzielnie w tym miejscu — konwersję robi przeglądarka/skrypt. (Istniejący `/upload_splash` przyjmuje PNG/JPG dzięki dekoderom pngle/TJpgDec w `drawImageFromFS`, ale konwerter i tak wysyła gotowy BMP.)
- Wbudowany `getMainHTML()` NIE zawiera tej sekcji (to tylko skrócony zapas).

## Upload binarny: RAW vs multipart — ROZWIAZANE (14.10.2026)
- **Objaw:** „Blad wysylania (siec/format)" przy wysylaniu grafiki z WWW (plik BMP ~300 KB).
- **PRZYCZYNA:** parser `multipart/form-data` w `WebServer` (core Arduino-ESP32 2.x, tu 2.0.17) czyta korpus bajt-po-bajcie (`_uploadReadByte`) i dla wiekszych plikow potrafi zresetowac polaczenie / zgubic koncowa granice (znany bug espressif/arduino-esp32 #9163). Dotyczylo to WSZYSTKICH endpointow uploadu (`/upload_splash`, `/upload/bmp`, `/upload/mapa565`).
- **FIX:** WWW wysyla plik jako **surowe cialo** (RAW) z naglowkami `X-Filename` i `X-Upload-Target`, `Content-Type: application/octet-stream`. Firmware czyta je sciezka `HTTPRaw` (wg `Content-Length`, bloki po `HTTP_RAW_BUFLEN`=1436 B) - szybko i niezawodnie dla binariow.
- **Implementacja:** `handleUpload(targetForced)` w `main.cpp` rozpoznaje tryb po `Content-Type` (multipart -> stara sciezka `upload()`, inaczej -> `raw()`). Rejestracja: `server->collectHeaders({"Content-Type","X-Filename","X-Upload-Target","Content-Length"},4)` **przed** `begin()`. Plik trzymany otwarty przez caly upload (`rawUploadFile`).
- Multipart nadal dziala (np. `curl -F`), ale WWW korzysta z RAW. Endpointy bez zmian: `/upload_splash` (splash), `/upload/bmp` i `/upload/mapa565` (mapa).
- Test reczny (curl RAW): `curl -X POST --data-binary @splash.bmp -H "Content-Type: application/octet-stream" -H "X-Filename: splash.bmp" -H "X-Upload-Target: splash" http://esp-ip/upload_splash`.

### REGRESJA: "nie wgrywa obrazu startowego przez strone www" — ROZWIAZANE (14.10.2026)
- **Objaw:** po commicie `002afdb` ("Wyslij na ESP32" w zakladce "Ekran TFT")
  zwracalo "Blad wysylania (siec/format)" i nie zapisywalo splash.
- **PRZYCZYNA:** literowka w sciezce. JS (`uploadConvertedImage`) wysylal na
  `/upload/splash`, a firmware rejestruje `/upload_splash` (podkreslnik). Trasa
  nie istniala -> 404 -> `Promise.reject(404)`. Drugi przycisk ("Wgraj ekran
  startowy" w zakladce "Linki") mial poprawny `/upload_splash`, ale wysylal
  **multipart** (`FormData`) — czyli ta sama, znana z powyzszego buga sciezka,
  ktora resetuje polaczenie dla wiekszych plikow (PNG/JPG/BMP > ~kilka KB).
- **FIX (WWW, `data/index.html` + `data/indexEN.html`):**
  1. `uploadConvertedImage`: `/upload/splash` -> `/upload_splash`.
  2. `initSplashUpload` (Linki): multipart `FormData` -> RAW body (`File` jako
     `body`, `Content-Type: application/octet-stream`, `X-Filename`,
     `X-Upload-Target: splash`), zgodnie z reszta uploadow.
- Endpointy firmware bez zmian: `/upload_splash`, `/upload/bmp`, `/upload/mapa565`.
- Po zmianie `data/` trzeba przebudowac LittleFS: `pio run -e esp32-s3-n16r8 -t buildfs`.

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

## WWW: lista "Ekrany TFT (kolejnosc)" - 15 pozycji
Pelne UI jest serwowane z LittleFS (`data/index.html` / `data/indexEN.html`),
NIE z wbudowanego stringa w main.cpp. Lista kolejnosci ekranow ma 15 pozycji
(`screen_slot_1..15`, `screenOptionsDefault` dlugosci 15, petle JS `i < 15`).
Opcje: 15 ekranow + OFF (w tym `pskmap`, `systeminfo`, `station`, `iss`). Kody
musza byc zgodne z `screenCodeToType()` w firmware (`bandinfo`, `sunspots`,
`systeminfo`, `station`).
UWAGA: stary kod w main.cpp (`SCREENS`, `screenOrderRow`) jest martwy - nie
edytuj go, bo nie jest serwowany. Po zmianie `data/*.html` trzeba przebudowac
LittleFS (`pio run -t buildfs`) i wgrac image FS, nie tylko firmware.

## Ekran TFT "STACJA" (szukaj znaku QRZ) - 20.10.2026
Nowy `ScreenType` `SCREEN_STATION = 15` (`screenTypeToCodeStr` -> "station").
Rysowanie: `drawStationScreen()` (wywolywane z `drawScreen`); naglowek z
`uiHeader("STACJA"/"STATION INFO", false)`. Dotyk: `handleStationTouch()` na
koncu `handleTouchNavigation()` (po PSK MAP). Uklad w obu stylach
(`uiStyleClassic()`): klasyczny = czarne tlo + kolorowe obramowania, nowoczesny
= `UI_COL_*`. Klawiatura QRO rysowana od x=84 (startX=84), zeby klawisze nie
wchodzily w lewa strefe nawigacji ekranu (cornerX=80); przycisk SZUKAJ na
x=90..240, y=258..288 - tez poza strefa. Stan: `stationKeyboardActive` (jak
`pskKeyboardActive`) blokuje nawigacje w rogach. Szukanie w `stationLookupStart()`:
cache (`getQrzCacheFresh`) -> `fetchQrzCallsignInfo` -> `fetchHamDbCallsignInfo`
-> `fetchCallookCallsignInfo`; po zakonczeniu `stationResultPending` powoduje
przerysowanie w `uiTaskLoop`. Wejscie na ekran (zmiana `currentScreen` w
`drawScreen`) czysci `stationKeyboardActive`/`stationLoading`. Dane logowania QRZ
(`qrzUsername`/`qrzPassword`) sa zapisywane w NVS i ustawiane z WWW
(Ustawienia -> QRZ.com, pola `qrz_user`/`qrz_pass`); QRZ.com nie ma klucza API do
wyszukiwania znakow - klucz API sluzy tylko do logbooka (upload). Bez loginu/hasla
QRZ zostaje HamDB (globalne, darmowe) i callook (tylko USA).
HamDB: `http://api.hamdb.org/v1/<CALL>/json/<APPNAME>` (http, bo Cloudflare robi
302; pola `NOT_FOUND` traktowane jak brak).
UWAGA rysowanie: `tft.drawString(s,x,y)` BEZ parametru fontu uzywa czcionki
GLCD (font 1) skalowanej przez `setTextSize()` - nie Font 2. Dlatego pole
wynikow uzywa `setTextSize(2)` + GLCD (~12x16 px na znak); `stepY = 30`, zeby
5 pol (Imie/QTH/Grid/Kraj/Dyst) zmiescilo sie nad przyciskiem SZUKAJ (y=258).
Podglad ukladu (1:1, prawdziwe bitmapy czcionek): `tools/render_station_preview.py`
-> `/workspace/share/stacja-*.png` (wymaga Pillow).

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

### REGRESJA (naprawione 15.10.2026): zegar przeslanial menu koloru znaku
Objaw: po otwarciu menu "KOLOR ZNAKU" (przytrzymanie palca na znaku) na wierzchu
menu rysowal sie zegar ekranu 1 i przeszkadzal w ustawieniach.
Przyczyna: `openCallsignColorMenu()` zapisywalo stan `inMenu` do
`callsignColorMenuPrevInMenu`, ale NIE ustawialo `inMenu = true`. Petla glowna
odswieza ekran 1 warunkiem `!inMenu`, wiec zegar dalej sie przerysowywal.
Poprawka: `inMenu = true` w `openCallsignColorMenu()` (tak jak w menu jasnosci);
przy ZAPISZ/ZAMKNIJ `inMenu` jest przywracane z `callsignColorMenuPrevInMenu`.
UWAGA na przyszlosc: KAZDE wlasne menu pelnoekranowe musi ustawiac `inMenu = true`
i przywracac poprzednia wartosc przy zamknieciu.


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


## CALLOOK 404-storm + brak auto-odswiezania PSK (14.10.2026, z logu urzadzenia)
Log pokazal dwa realne problemy (PSK byl cicho, CALLOOK sypal 404):

1) CALLOOK/callook.info obsluguje TYLKO znaki USA. Polskie znaki (SP9TNV,
   SP8AJK...) zawsze zwracaly 404, a kolejka QRZ ponawiala je bez konca
   (fetchCallookCallsignInfo w petli co QRZ_LOOKUP_INTERVAL). Stad strumien
   "[CALLOOK] HTTP error: 404" co ~6 s.
   Naprawa: wpis NEGATYWNY w qrzCache (pole notFound). 404 oraz status INVALID
   zapisuja wpis na QRZ_NEGATIVE_TTL_MS (1 h); enqueueQrzLookup pomija znaki
   znane jako brakujace (isQrzCacheKnownMissing). Udany zapis czysci notFound.
   Symulacja: 10 min x 30 znakow -> 3600 zapytan przed, 30 po.

2) Ekran PSK nie odswiezal sie sam. drawPskMap() bylo wolane TYLKO przy
   zdarzeniach (wejscie na ekran, dotyk, zapis) - brak periodycznego wywolania,
   wiec licznik pskAutoRefreshMinutes nigdy nie odpalal. Dodano periodyczne
   wywolanie w UI_Task (sekcja per-screen): gdy interwal minal i nie ma backoffu
   po 429. To naprawia tez "pierwszy fetch po wgraniu nic nie pobral" (przy
   poprzednim FW nie bylo zadnego auto-fetchu - fetch dzialal tylko przy wejsciu
   na ekran z lastPskFetchMs==0, co przy starcie na innym ekranie nie zachodzi).


## Znak bialy + uklad QTH na radarze APRS (14.10.2026)
1) Znak (callsign) domyslnie bialy, ale przy starcie bywal niewidoczny:
   przyczyna - NVS "callsign_color" == 0 (czarny) traktowane jako poprawna
   wartosc i rysowane czarnym na czarnym tle. Naprawa: w loadPreferences() oraz
   w /api/save gdy odczyt == 0 -> wymus 65535 (biel). Wartosc domyslna 65535 juz
   byla, wiec swiezy NVS dziala; problem dotyczyl zapisanego 0.
2) Ekran APRS RADAR, znacznik QTH w centrum: znak byl tuz nad kropka i nachodzil
   na lokator. Teraz: ZNAK wyzej (TC_DATUM na CY-28), pod znacznikiem kolejno
   LOK: <lokator>, SZER: <lat>, DLUG: <lon> (kazdy z etykieta i danymi).
   Szerokosc tekstu (~78 px) miesci sie w okregu radaru (promien 122 px).


## Wspolny styl "ISS TRACKER" na wszystkich ekranach (14.10.2026)
Cel: wszystkie ekrany wygladaja jak ekran ISS TRACKER, przy zachowaniu funkcji.
- Paleta `UI_COL_*` (obok `HamClockTFT tft;`): BG/CARD/BORDER/ACCENT/ACCENT2/
  WARN/TEXT/DIM/FAINT/RED/GREEN. Ciemna karta + akcent teal/niebieski.
- Pomocnicze funkcje: `uiHeader(title, showMenu)` (ciemna belka 30 px + teal
  linia, tytul wysrodkowany, hamburger w stylu ISS, ikony WiFi/baterii),
  `uiFooter()` (strzalki < > + "SWITCH SCREEN"), `uiHamburger(x,y)`.
- `drawScreen()` rysuje naglowek i stopke dla WSZYSTKICH ekranow poza
  `issFullBleed` = {MATRIX_CLOCK, PSK_MAP, APRS_RADAR, SYSTEM_INFO,
  ISS_PASS_TRACKING}. Dzieki temu pojedyncze `drawXxx()` nie rysuja juz wlasnych
  pomaranczowych belek/hamburgera/ikon (usunieto je; zostawiono komentarze).
- `updateScreen1Header()` = `uiHeader("ESP32-HAM-CLOCK", false)`;
  `updateScreen1HeaderClock()` = puste (naglowek ISS bez osobnego zegara).
- `drawSwitchScreenFooter()` deleguje do `uiFooter()`.
- APRS RADAR: naglowek ISS rysowany wewnatrz sprite'a (`aprsRadarRenderFrame`)
  nad obszarem radaru (x < ARP_PANEL_X-1), zegar UTC po prawej w naglowku.
- PSK MAP: `drawPskMapHeader()` uzywa `uiHeader` (nakladka na mape); status
  AUTO/Spoty przesuniety pod belke (y ~34/35).
- Tabele (DX/POTA/HamAlert/APRS-IS): naglowki kolumn w `UI_COL_FAINT`,
  "WAITING FOR ..." w `UI_COL_WARN`.
- Full-bleed zachowane: MATRIX (wlasny motyw), SYSTEM_INFO (wlasna paleta
  SYS_COL_*), ISS TRACKER (wzorzec), PSK (mapa na pelnym ekranie).
- Build: `pio run -e esp32-s3-n16r8 -e esp32-s3-n16r8-notouch` -> SUCCESS.


## "Bialy migajacy ekran" po zapisie/importcie konfiguracji — ROZWIAZANE (14.10.2026)
Objaw: po zapisie/importcie ustawien z WWW (config -> restart ESP32) ekran na
stronie startowej jest bialy i migajacy (wyglada jak petla restartu).

Przyczyna: `initTFT()` po `tft.begin()` robil bezwarunkowy test kolorow
`fillScreen(RED) -> delay(2000) -> GREEN -> delay(2000) -> BLUE -> delay(2000)`,
a po `applyTftInversion()` konczyl na `tft.fillScreen(TFT_WHITE)`. Kazdy zapis z
WWW (`/api/save`) restartuje ESP (`requestRestart(1500)`), wiec sekwencja
odpalala sie przy kazdym zapisie: ~6 s R/G/B + bialy blysk na koncu. Powtarzane
przy kazdym restarcie = wizualnie "bialy migajacy ekran" (a nie prawdziwa petla
restartu — nie bylo WDT/panic; sprawdzic `[SETUP] Powod restartu:` w logu).

Fix (firmware `main.cpp`, `initTFT()`):
1. Test kolorow przeniesiony za `#ifdef TFT_BOOT_SELFTEST` — domyslnie OFF.
   Wlacz `-DTFT_BOOT_SELFTEST` w `platformio.ini` tylko przy diagnozie panelu.
2. Ostatnie wypelnienie init zmienione z `TFT_WHITE` na `TFT_BLACK` — init nie
   konczy sie jasnym blyskiem, a boot/splash i tak startuje od czarnego tla.

Dodatkowo (WWW, `data/index.html` + `data/indexEN.html`): `<select tft_invert>`
mial domyslnie zaznaczone "0" (Normalne), a firmware startuje z inwersja ON
(`TFT_INVERSION_ON`). Wyslanie formularza zanim `loadConfig()` ustawil wartosc
wysylalo `tft_invert:false` i gasilo inwersje -> ciemny splash (mean_lum 40)
renderowal sie jako bialy. Teraz opcja "Odwrocone"/"Inverted" ma `selected`.

Uwaga: `tft_style` ("light" = biale tlo) istnieje w WWW, ale firmware go NIE
obsluguje (brak klucza w `main.cpp`) — opcja jest ignorowana, nie powoduje bieli.

Build: `pio run -e esp32-s3-n16r8 -e esp32-s3-n16r8-notouch` -> SUCCESS.

## Ekran startowy (welcome): grafika krotkofalarska (14.10.2026)
- `drawWelcomeScreenYellow()` (main.cpp) - ekran pokazywany przy starcie, gdy brak pliku splash.
- Dodano grafike zwiazana z krotkofalarstwem: maszt antenowy (kratownica, swiatlo
  ostrzegawcze RED na szczycie), 3 wspolsrodkowe fale radiowe (teal/niebieski,
  naprzemiennie), dwa dipole po bokach i podstawa/ziemia.
- Uklad 480x320: tytul "ESP32-HAM-CLOCK" (size 3, zolty) + linia akcentu, grafika
  na srodku, podpis "HAM RADIO - AMATEUR RADIO STATION", stopka (version/autor/base/
  license/email) w 5 liniach (size 2). Kolory z palety UI_COL_* (spojne z reszta UI).
- Podglad ukladu: `/workspace/share/welcome-preview.png` (render symulujacy ekran).

## EKRAN 1 (HAM CLOCK) - nowy uklad wg opisu obudowy (14.10.2026)
- `drawHamClock()` przepisane. Layout 480x320 (naglowek 0-25 + stopka od 296):
  - **Ramka obudowy**: `drawRoundRect` (srebrny/ciemny metal) 5,29 470x241 + 4 sruby
    (`hamClockScrew`) w rogach (14,38)(466,38)(14,261)(466,261).
  - **Lewy panel GLOBE / SAT** (15,40 120x176): globus (siatka poludnikow/rownoleznikow),
    zolty marker domowego lokatora (`userLatLonValid`/`userLocator`) + czerwony marker ISS
    (`issLat`/`issLng`), pod spodem ISS: lat/lon i LOC.
  - **Srodkowa tarcza analogowa** (`HC_CLOCK_CX/CY/R`=235/114/72): ciemne granatowe tlo,
    cyfry 1..12, kreski minutowe, wskazowki H/M/S (`hamClockDrawHands`), medalion, data
    `RRRR-MM-DD` i etykieta UTC/LOCAL pod tarcza.
  - **Prawy panel 20m SPECTRUM** (320,40 146x176): analizator widma (paski, piki przy
    14.060 CW / 14.074 FT8 / 14.090 FT4 / 14.200 SSB) + wodospad (`pushImage` wierszami,
    bufor `hamClockWf[72][146]` = 21 kB), podpisy 14.000/14.35.
  - **Dolna ramka** (15,222 450x46): Typ / Wersja / Autor / Baza / Licencja / Kontakt.
- Aktualizacje: `updateScreen1Clock()` przerysowuje wskazowki + paski + nowy wiersz
  wodospadu; `updateScreen1Date()` tylko date (bez czyszczenia). `updateScreen1HeaderClock()`
  to teraz no-op (brak zegara w naglowku - jest analogowy).
- Dotyk: przytrzymanie na tarczy zegara (`dx^2+dy^2 <= R^2`) otwiera menu czasu/opcji
  (wczesniej: prawy gorny rog). Znak wywolawczy nie jest juz rysowany na tym ekranie,
  wiec obszar przytrzymania znaku jest zerowany (`callsignHitW/H = 0`).
- Podglad ukladu: `/workspace/share/hamclock-preview.png`. Build oba env: SUCCESS.

### Styl nowoczesny (14.10.2026)
- Tlo: pionowy gradient granatu (`hamClockBg`, ponizej naglowka, zeby nie zamalowac go).
- Obudowa: szczotkowane aluminium (`HC_PLATE` 0x8C71) z gradientem + podwojna obwodka
  (fazowanie `HC_BEZEL_LT/DK`) + sruby z polyskiem (`hamClockScrew`).
- Panele (globus / zegar / widmo / info): ciemne tlo `HC_PANEL_BG`, fazowana ramka
  (`hamClockBevel`), pasek tytulu w `HC_FACE_BG`, akcent cyjanowy `HC_CYAN` (0x07FF).
- Zegar: wlasny panel (150,40 172x176), tarcza R=62 z metaliczna obwodka i cyjanowym
  pierscieniem; cyfry 12/3/6/9 na bialo. Data/etykieta na tle panelu.
- Tekst na plycie: `HC_INK`/`HC_CYAN` na `HC_PANEL_BG` (kontrola tla przy kazdym print,
  bo tekst nie jest juz rysowany na czerni).

### Przebudowa wg wzorca uzytkownika (14.10.2026) - OBOWIAZUJACA
Uzytkownik podal wzorcowy kod TFT_eSPI (320x240, rotacja 1). Przeniesione 1:1 na 480x320:
- **Naglowek**: cyjanowy `"ESP32 - HAM - CLOCK"` (font 4) wysrodkowany (`TC_DATUM`) +
  `drawFastHLine(40,36,400,CYAN)`.
- **Paleta wg wzorca**: `HC_BG` 0x0000, `HC_PANEL` 0x10A2, `HC_CYAN` 0x07FF,
  `HC_YELLOW` 0xFFE0, `HC_WHITE` 0xFFFF, `HC_TEXT` 0xE73C. (Bez aluminium/srub.)
- **Trzy panele** 42..206: GLOBE/SAT (15,120), zegar (150,172), 20m SPECTRUM (332,134).
  Tytuly: cyjanowy znacznik + tekst + linia (`hamClockPanelTitle`), ramka fazowana.
- **Zegar**: tarcza R=58, cyjanowa obwodka, wskazowki H=biala/M=cyjan/S=zolta,
  cyfry 12/3/6/9 biale, data pod tarcza (`TC_DATUM`).
- **Widmo**: paski + wodospad `hamClockWf[70][134]` (~18 kB), podpisy 14.000/14.35.
- **Dolna ramka** (10,210 460x84): cyjanowa obwodka `drawRoundRect`, tresc wysrodkowana
  (`MC_DATUM`): "HAM RADIO  -  AMATEUR RADIO STATION" (cyjan, font 1), potem zolte
  "version v1.2" / "Autor: Tomasz SP9TNV" / "Base: Konrad SP3KON" / "License: MIT"
  (font 2) i "SP9TNV@gmail.com" (font 1).
- Uwaga: przy `MC_DATUM` + `drawString` nie uzywac `setTextPadding` (kalibruje pozycje).
- Podglad: `/workspace/share/hamclock-preview.png`. Build oba env: SUCCESS.

### Upload ekranu POWITALNEGO (JPG/PNG/BMP) przez WWW (14.10.2026)
- Cel: obraz zastepuje EKRAN POWITALNY z wersja i autorami
  (`drawWelcomeScreenYellow`, wyswietlany na starcie PRZED splashem), a NIE splash.
- Trasy: `/upload_splash` (cel "splash" - zakladka Linki) ORAZ `/upload_welcome`
  (cel "welcome" - zakladka TFT). WAZNE: w `handleUpload()` naglowek
  `X-Upload-Target` ma PIERWSZENSTWO nad celem wymuszonym przez trase - inaczej
  upload na `/upload_splash` z naglowkiem "splash" trafial do "welcome" (bug).
- Zakladka TFT -> "Grafika na ekran": sekcja "Ekran powitalny - wgraj bezposrednio"
  (id `welcome-direct-file` / `welcome-direct-btn`), RAW na `/upload_welcome`.
  Podglad: `<img src="/welcome_preview">`. Konwerter RGB565: pole "Cel" ma opcje
  "welcome" -> URL `/upload_welcome`.
- Firmware: `resolveUploadTarget()` dla "welcome" zapisuje `/welcome.<ext>` +
  `removeOldWelcomeVariants()`; dla "splash" `/splash.<ext>` + `removeOldSplashVariants()`.
- `drawWelcomeScreenYellow()` na poczatku probuje `/welcome.{png,jpg,jpeg,bmp}`;
  jesli plik jest - rysuje go zamiast grafiki masztu + tekstow wersja/autor.
- Splash: domyslny `data/splash.bmp` (stary splash) - bez zmian.
- Nowy ekran powitalny widac po restarcie (rysuje sie tylko przy starcie).
- Publish: `/workspace/share/flash/littlefs.bin` + `esp32-ham-clock-s3.zip`.

### Ekran powitalny z KONWERSJA + starszy styl zegara (14.10.2026)
- Upload ekranu powitalnego ma teraz **mozliwosc konwersji** (domyslnie WLACZONA):
  checkbox `welcome-direct-convert` + select `welcome-direct-fit`
  (contain/cover/stretch). Gdy wlaczona, plik jest skalowany w przegladarce do
  480x320 i pakowany do BMP RGB565 (`convertImageToBmp565` -> `buildBmp565`),
  potem RAW na `/upload_welcome`. Odznaczona => plik leci 1:1 (bez zmian rozmiaru).
- `convertImageToBmp565(file, mode, onDone, onError)` - wspolny helper (PL+EN),
  korzysta z `drawFittedToCanvas` i `buildBmp565`.
- **Dwa style ekranu zegara** (v1.4.3+): przelacznik `screen1ClockStyle`
  (0 = NOWOCZESNY, **domyslny**; 1 = KLASYCZNY). Persystencja NVS: `screen1_style`
  (domyslna wartosc 0).
  - `drawHamClock()` na poczatku: `if (screen1ClockStyle == 1) { drawHamClockClassic(); return; }`
  - `drawHamClockClassic()`: pomaranczowy naglowek (`updateScreen1Header`: logo ESP32
    w czarnej obwodce + zegar HH:MM lewy gorny rog + paski WiFi + bateria), ramka
    `drawRoundRect(30,45,420,220)` ciemnoszara, callsign (font 5, kolor `callsignColor`),
    "OPERATOR STATION", duzy zegar cyfrowy (font 7), etykieta UTC/Czas Lokalny, data
    (font VLW Inter 20 albo font 3 ASCII), imieniny (`getPolishNameDay`, tylko PL),
    adres IP na dolnym pasku, strzalki nawigacyjne. Bez `uiHeader`/`uiFooter`.
  - `updateScreen1ClockClassic()` odswieza duzy zegar co sekunde (bez migotania,
    `setTextPadding`). Petla glowna wybiera klasyczne update gdy `screen1ClockStyle==1`.
  - Dotyk: w klasycznym stylu menu czasu otwiera klikniecie w prawy gorny rog
    (`x>285 && y<35`); w nowym - przytrzymanie na tarczy. Obszar znaku
    (`callsignHitX/Y/W/H`) ustawiany w `drawHamClockClassic` (przytrzymanie = kolor znaku).
  - `drawScreen()`: dla klasycznego zegara nie rysuje `uiHeader`/`uiFooter`/`drawHamClockIp`.
  - Menu "TIME MODE" (klik w rogu) ma dodatkowe kafelki "WYGLAD ZEGARA":
    NOWOCZESNY / KLASYCZNY (EN: MODERN / CLASSIC).
- Podglad starszego ukladu: `/workspace/share/hamclock-classic-preview.png`.
  Build oba env: SUCCESS.

### Uwaga: ekran zegara - styl nowoczesny
- Ekran zegara w stylu nowoczesnym (styl 0) = `drawHamClockModern()`: **wyglad jak
  w wersji domyslnej (klasycznej)** - naglowek z logo + zegar HH:MM + WiFi + bateria,
  znak (callsign), "OPERATOR STATION", duzy zegar cyfrowy, etykieta czasu, data,
  imieniny, adres IP i strzalki - ale w stylu nowoczesnym: CZARNY naglowek z
  BIALYM napisem "ESP32-HAM-CLOCK", cyjanowa linia pod naglowkiem, czarne tlo,
  cyjanowa ramka (30,45,420,220), cyjanowy zegar HH:MM w naglowku,
  "OPERATOR STATION"/imieniny/strzalki na cyjano i zolty zegar (`HC_YELLOW 0xFFE0`).
- Naglowek MUSI byc pelny (jak domyslny): inaczej znikaja WiFi, bateria i logo.
- `drawHamClockModern()` ustawia `screen1HeaderNeedsRedraw = false` PO narysowaniu
  naglowka; `updateScreen1HeaderModern()` przerysowuje naglowek tylko gdy
  `screen1HeaderNeedsRedraw == true` (oszczedza migotanie).
- `drawScreen()`: dla SCREEN_HAM_CLOCK NIE rysuje uiHeader/uiFooter/drawHamClockIp
  (oba style maja wlasny naglowek i stopke).
- Menu czasu (WYGLAD ZEGARA: NOWOCZESNY/KLASYCZNY) w obu stylach otwiera sie
  kliknieciem w prawy gorny rog (x > 285 && y < 35).
- **Domyslny styl = NOWOCZESNY** (`screen1ClockStyle = 0`); klasyczny jest opcja.
- Styl zegara (`screen1ClockStyle`) jest teraz stylem GLOBALNYM - patrz sekcja
  "STYL GLOBALNY + MENU NOWOCZESNE" nizej. Przycisk KLASYCZNY w menu czasu
  przelacza WSZYSTKIE ekrany (akcent naglowka, kolor menu, kolor znaku).

## EKRAN 10 (MATRIX) - wysrodkowanie zegara i daty (15.10.2026)
- `drawMatrixFrame()` liczy szerokosc tekstu z realnej metryki `tft.textWidth()`,
  a NIE jako `8 px * rozmiar * liczba znakow`. Wbudowany font TFT_eSPI (GLCD) ma
  **6 px szerokosci na znak**, wiec stare `timeWidth = 8*8*size` dawalo 384 zamiast
  192 i zegar/data byly przesuniete w lewo (nie na srodku).
- Data: czyszczona (`fillRect`) przed rysowaniem - wczesniej zostawaly resztki
  przy zmianie tekstu (rysowanie tylko gdy `timeLocal != lastClockText`).
- Podglad: `/workspace/share/matrix-zegar-srodek.png`.

## STYL GLOBALNY + MENU NOWOCZESNE (15.10.2026)
- `screen1ClockStyle` (0 = nowoczesny, 1 = klasyczny) jest teraz **stylem
  globalnym**, nie tylko zegara. Przycisk `KLASYCZNY`/`NOWOCZESNY` w menu
  "TIME MODE" (ekran zegara, prawy gorny rog) przelacza od razu **wszystkie**
  ekrany: akcent naglowka, kolor menu (`menuThemeColor`) i kolor znaku.
  Wybor jest zapisywany (`screen1_style`) w `savePreferences()`.
- **Wyglad KLASYCZNY = jak ekran zegara klasycznego:** naglowek i naglowki menu
  to pomaranczowa karta (`TFT_RADIO_ORANGE`) z **czarnym** tytulem i linia
  `0x8410`; hamburger czarny na pomaranczowym; tlo czarne; akcent menu/strzalek
  pomaranczowy; kafelki nieaktywne = ciemnoszare. `applyMenuThemeFromHue()` w
  trybie klasycznym uzywa `colorWheel(menuThemeHue)` (domyslnie pomaranczowy),
  wiec kolor z suwaka motywu dziala tylko tutaj; wartosc jest zapamietywana.
- `screen1ClockStyle` jest wczytywany **przed** `applyMenuThemeFromHue()` w
  `loadPreferences()` - inaczej kolor menu bylby zly po restarcie.
- **Menu nowoczesne:** wspolny helper `uiMenuHeader(title)` (ciemna karta
  `UI_COL_CARD` + linia akcentu, tytul po lewej). Uzywaja go: TIME MODE,
  FILTERS (DX), POTA FILTERS, HAMALERT FILTR, APRS, WEATHER MENU, USTAWIENIA TFT,
  KOLOR ZNAKU, WYGASZACZ EKRANU, USPIENIE EKRANU.
- `drawFilterTile()`: nowoczesny = ciemna karta + teal (aktywne wypelnienie,
  nieaktywne obramowanie `UI_COL_BORDER`); klasyczny = stare wypelnienie
  `TFT_DARKGREY`/motyw. Szerokosc etykiety liczona z `tft.textWidth()`
  (wczesniej `strlen*6` - zle dla szerokich znakow).
- `uiHeader()` automatycznie dopasowuje tytul: Font 2 (16 px) gdy sie miesci
  miedzy hamburgerem (x~34) a WiFi (x~415), inaczej Font 1, w ostatecznosci
  przycina. Naprawia to m.in. "POTA.app" i dluzsze tytuly ("SOLAR PROPAGATION
  INDEX", "Prognoza pogody").
- **UWAGA (bug): `drawString(s, x, y, font)` skaluje sie GLOBALNYM `textsize`.**
  Ekran zegara zostawia `textsize=7` (`updateScreen1ClockModern()`), wiec tytul
  naglowka rysowany przy wejsciu na ekran byl 7x za duzy ("POTA.app" - widoczna
  tylko czesc). Po przejsciu na inny ekran (ktory ustawia `textsize=1`) i powrocie
  bylo juz OK. `uiHeader()` ustawia teraz `tft.setTextSize(1)` przed rysowaniem.
  Ten sam wzorzec stosuje `uiHeaderClock()` (rysuje z Font 1).
- **Nadal nie przerabiac** samego ekranu zegara na wspolny styl ISS - ma wlasne
  naglowki (nowoczesny/klasyczny). Zmiana dotyczy tylko podmenu i akcentu.
- Podglad ukladu: `/workspace/share/podglad-menu-nowoczesny.png`
  (renderer: `tools/render_menu_preview.py`, uzywa realnych bitmap fontow
  TFT_eSPI Font16.c + glcdfont.c).
- Artefakty po zmianie: `firmware.bin` / `firmware-notouch.bin`,
  `merged-flash*.bin`, `esp32-ham-clock-s3.zip` (580 wpisow) - w `dist/`
  i `/workspace/share/` (+ `/workspace/share/flash/`). Build oba env: SUCCESS.

## Godlo PZK na ekranie powitalnym (15.10.2026)
- Nad tekstem ekranu powitalnego rysowany jest znak PZK (Polski Zwiazek
  Krotkofalowcow) - niebieski romb 46x96 px, `data/pzk-logo.bmp` (BMP 24-bit,
  tlo czarne, zrodlo: Wikimedia Commons "Logo PZK 1930").
- `drawWelcomeScreenYellow()`: gdy `/pzk-logo.bmp` jest w LittleFS, godlo leci
  na srodku (`(480-46)/2`) a 5 linii tekstu tuz pod nim (+10 px). Bez godla tekst
  jest centrowany jak wczesniej. Stale: `PZK_LOGO_FILE/W/H` (main.cpp ~810).
- Fallback (ROM Font 2) tez rysuje godlo, jesli plik istnieje.
- Artefakty: oba env SUCCESS, buildfs SUCCESS, zip 584 wpisy.

## Ekran powitalny: wieksza czcionka + polskie znaki (15.10.2026)
- `drawWelcomeScreenYellow()` laduje teraz font VLW `data/fonts/Welcome-Regular21.vlw`
  (DejaVu Sans 21 pt, 319 glifow z polskimi ogonkami) i rysuje nim 5 linii bialego
  tekstu - WIEKSZE niz poprzedni ROM Font 2 (16 px). Linia bazowa 20 px, wiersz 25 px,
  najdluzsza linia 465 px (miesci sie w 480). Blok przesuniety o +18 px w dol
  (litera bazowa lezy w dolnej czesci wiersza) - content 100..219 px, wysrodkowany.
- Tekst z polskimi znakami: "Projekt bazowy: Konrad Wiśniewski SP3KON",
  "Adaptacja i rozwój: Tomasz SP9TNV.", "Projekt udostępniany na licencji MIT.".
  Wymaga UTF-8 w zrodle (PlatformIO/GCC domyslnie UTF-8) + glifow w foncie.
- Fallback (brak pliku fontu / malo pamieci): ROM Font 2, tekst bez diakrytykow.
- Generator fontow: `tools/make_vlw_font.py <ttf> <pt> <out.vlw> [xscale]`
  (format VLW big-endian, zakres ASCII+Latin-1+Latin Extended-A).
- Podglad: `tools/render_welcome_preview.py` -> `/workspace/share/welcome-preview.png`.
- Artefakty: oba env SUCCESS, merged-flash*.bin, zip 583 wpisy - w `dist/` i `/workspace/share/`.

## Ekran powitalny: 20 s + czarny/bialy + usuniecie "Postaw kawe" (15.10.2026)
- `drawWelcomeScreenYellow()` (main.cpp ~2369) - gdy brak wlasnej grafiki
  `/welcome.<ext>` - renderuje teraz CZARNE tlo (`TFT_BLACK`) i 5 linii BIALEGO
  tekstu wysrodkowanego (`uiWelcomeText()`, helper `static` zadeklarowany na
  poczatku pliku). Tresc: "ESP32-HAM-CLOCK v1.2" (z `FIRMWARE_VERSION`),
  "Projekt bazowy: Konrad Wisniewski SP3KON", "Adaptacja i rozwoj: Tomasz
  SP9TNV.", "Kontakt: SP9TNV@gmail.com", "Projekt udostepniany na licencji MIT.".
- Czcionka: ROM Font 2 (16 px) - NAJWIEKSZA, ktora nadal miesci najdluzsza linie
  w 480 px; ROM font nie ma polskich ogonkow, wiec tekst bez diakrytykow.
  (Fonty VLW z `data/fonts/` maja polskie znaki, ale sa za male na pelna szerokosc:
  Inter24 - najdluzsza linia ~500 px.)
- Czas: petla `while ((millis() - welcomeStart) < 20000)` z przerwaniem dotykiem
  (wczesniej `delay(2000)`). Ekran splash jest PO ekranie powitalnym.
- WWW: usunieto przycisk "Postaw kawe" (`class="coffee-link"`) z `data/index.html`
  i `data/indexEN.html` oraz jego CSS + `@keyframes coffeePulseWeight` z
  `style.base.css` / `style.modern.css`. Wiersz tabeli "Postaw kawe" w zakladce
  "Linki" pozostawiony.
- Po zmianie `data/` przebudowano LittleFS (`pio run -e esp32-s3-n16r8 -t buildfs`).
- Artefakty: oba env SUCCESS, `merged-flash*.bin`, `esp32-ham-clock-s3.zip`
  (580 wpisow) - w `dist/` i `/workspace/share/`.

- UWAGA: `git commit` domyslnie wymaga podpisu GPG (`commit.gpgsign=true`); klucz
  prywatny jest w `.gpg-release-key` (poza VCS). Bez klucza uzyj `--no-gpg-sign`.

## Wysylka ADIF do logow online (17.10.2026)
- Dwa nowe serwisy obok QRZ Logbook: **eQSL.cc** i **Club Log**.
- `loggerUploadAdif(service)` - dyspozytor: `qrz` (domyslnie) | `eqsl` | `clublog`.
- QRZ: `POST https://logbook.qrz.com/api` `KEY=&ACTION=INSERT&ADIF=`; sukces = `RESULT=OK`.
- eQSL: `POST https://www.eqsl.cc/qslcard/ImportADIF.cfm` pola `Filename` (ADIF) +
  `EQSL_USER`/`EQSL_PSWD`; sukces = "x out of y records added". Dane logowania
  mozna tez w naglowku ADIF (`EQSL_USER`/`EQSL_PSWD`) - uzywamy pól formularza.
- Club Log: `POST https://clublog.org/realtime.php` pola `email` (NIE znak) +
  `password` (Application Password) + `callsign` + `adif` + `api`; **jeden rekord
  ADIF na request**. HTTP 200 = OK/Duplicate/Modified, 400 = odrzucone, **403 =
  zle dane -> PRZERWIJ** (inaczej Club Log blokuje IP). NIE uzywac do batchy.
- Endpoint WWW: `POST /api/qrz_upload` z JSON `{"service":"qrz|eqsl|clublog"}`
  (albo query `?service=`). UI: sekcje eQSL.cc i Club Log + selektor logu +
  podglad listy QSO (`/api/logbook`, `/api/logbook_add`, `/api/logbook_clear`).
- Preferencje (NVS): `eqsl_user/eqsl_pass/eqsl_qth`,
  `clublog_email/clublog_pass/clublog_call/clublog_api`.
- Podglad UI bez urzadzenia: `logger-www-preview.html` (mock API) w `/workspace/share/`.

## Naprawa: zapis modulu (GPS) + czas GPS na zegarze (17.10.2026)
Objawy zgloszone przez uzytkownika: w WWW "Moduly" wybor modulu (GPS) nie
zapisywal sie, a czas z GPS nie byl odczytywany/pokazywany na ekranie zegara.
Trzy niezalezne przyczyny:
1. **Zakladka "Moduly" budowana po `loadConfig()`.** `ensureModulesTabPL()`
   jest wolane na `DOMContentLoaded`, a `loadConfig()` startuje od razu przy
   parsowaniu skryptu - pola `connectedModule`/`gps_enabled` jeszcze nie istnialy,
   wiec odczyt konfiguracji je pomijal, a zapis wysylal domyslne (brak/off).
   Fix: `applyConfigToUI()` na poczatku wola `ensureModulesTabPL/EN()`, a
   `showPage('modules')` doladowuje konfiguracje. Wybor "GPS" synchronizuje
   checkbox `gps_enabled` (zdarzenie `change` selecta + przy odczycie configu).
2. **Brak zmiennej/klucza NVS dla wyboru modulu.** `uart2_connected_module`
   nie byl nigdzie przechowywany. Fix: `String uart2ConnectedModule` (NVS
   `uart2_mod`), odczyt w `loadPreferences()`, zapis w `savePreferences()`,
   obsluga w `POST /api/save` (moduł "gps" wlacza `gpsEnabled`), zwracane w
   `GET /api/config` (`uart2_connected_module`, `uart2_mode`) i `/api/uart_status`.
3. **Czas z NMEA nie trafial do zegara systemowego.** `gpsUtcTime`/`gpsDateStr`
   byly tylko parsowane; ekran zegara uzywa `getLocalTime()`/`getTimeWithTimezone()`
   (RTC), ktore bez NTP/internetu zostaje na 1970 -> "--:--"/"...". Fix:
   `gpsApplyTime()` (wolane z `gpsUpdate()`) robi `settimeofday()` z GGA/RMC -
   pelna data raz (gdy zegar nieustawiony), pozniej tylko korekta godziny co
   najwyzej 30 s. Wymaga `#include <sys/time.h>`.
- UI: panel statusu GPS (`updateGpsLiveStatusPL/EN`) pokazuje teraz "Czas UTC
  (NMEA)" + date z `/api/gps` (`utc_time`, `date`).
- Pliki: `main.cpp`, `data/index.html`, `data/indexEN.html`. Build 3 env
  SUCCESS; zip 582 wpisy.






## Naprawa: PSK Reporter + POTA "brak danych" (17.10.2026)
Objawy zgloszone przez uzytkownika: ekran PSK MAP pusty ("nie dziala psk
reporter"), ekran POTA Cluster tez bez danych. Dwie NIEZALEZNE przyczyny:

1. **PSK: domyslny host HTTP martwy + tryb MQTT bez feedu.**
   - `retrieve.pskreporter.info/query` (dawny domyslny URL) zwraca Cloudflare
     **522/523** (origin down) i **0 bajtow** -> mapa zostawala pusta.
     Dziala `pskreporter.info/query` (ten sam JSON, ten sam Cloudflare IP);
     zweryfikowane: HTTP 200, dane na zywo (`currentSeconds` = teraz).
     Fix: domyslny URL -> `https://pskreporter.info/query?`.
   - Broker `mqtt.pskreporter.info` **nie publikuje juz** feedu PSK:
     `pskr/filter/v2` i `pskr/filter/v2_field` zniknely (zostal tylko WSPR).
     Jedyny feed `pskr/filter/v2raw_1pc` tez byl chwilowo martwy (bridge
     zrestartowany 16.10.2026 23:20, `last_spot_received_at` zamrozone,
     `pskr_last_disconnected` = teraz). Fix: domyslny tryb -> **HTTP**
     (`pskMqttEnabled=false`), plus migracja NVS (`psk_mqtt_off_migr`)
     wylaczajaca MQTT RAZ dla niezmienionego serwera (wlasny broker zostaje).
     Gdyby MQTT wrocil: temat `pskr/filter/v2raw_1pc/<pasmo>/<tryb>/<nad>/<odb>`
     (segment odbiorcy = idx 5, brak segmentow lokatora - filtrujemy po znaku).
   - Bufor parsowania PSK: 24 KB -> **512 KB w PSRAM** (`BasicJsonDocument<
     PskPsramAllocator>`), bo `rptlimit` bywa ignorowany i filtrowany dokument
     rosnie do ~340 KB (wewnetrzny RAM dawal "NoMemory").

2. **POTA: za male bufory JSON (regresja rozmiaru danych, nie kodu).**
   API POTA zwraca teraz CALA liste (~239 spotow = ~150 KB surowego JSON-a,
   ~73 KB po filtrze), a firmware mial 24 KB (filtr) / 64 KB (bez filtra) w
   wewnetrznym RAM -> `deserializeJson` konczyl sie **"NoMemory"** i spoty
   ginely (przy 24 KB dekodowalo sie tylko ~80/239). Fix: bufory w PSRAM
   (`PskPsramAllocator`): **96 KB** (filtr) / **256 KB** (fallback bez filtra).
   - Nowy `struct PskPsramAllocator` (heap_caps_malloc MALLOC_CAP_SPIRAM) na
     poczatku sekcji POTA; wymaga `#include <esp_heap_caps.h>` (juz jest).
   - Weryfikacja hostowa (ArduinoJson 6.21.5, `g++`): 96 KB -> `Ok`, 239 spotow.
- Pliki: `main.cpp`. Build 2 env SUCCESS.

## Naprawa PSK cz.2: "Transfer-Encoding: chunked" psul parsowanie (17.10.2026)
Po wgraniu v1.2b uzytkownik zglosil, ze PSK Reporter nadal nie pobiera danych
i nie rysuje linii (mimo poprawnego HTTP 200). Trzecia, ukryta przyczyna:

- **`pskreporter.info` odpowiada z `Transfer-Encoding: chunked`** (naglowek
  potwierdzony: brak `Content-Length`, jest `Transfer-Encoding: chunked`).
- Kod czytal cialo przez **`http.getStreamPtr()`** i podawal go wprost do
  `deserializeJson`. `getStreamPtr()` zwraca **SUROWY** socket, wiec w strumieniu
  sa naglowki chunkow: `456c\r\n{ "currentSeconds":...`. ArduinoJson konczyl
  z `err=Ok`, ale **bez pola `receptionReport`** (0 spotow -> pusta mapa, brak
  linii). Udowodnione hostowo (`g++`, ArduinoJson 6.21.5):
  - raw chunked -> `err=Ok`, `reports=-1` (brak tablicy);
  - po zdjeciu chunkow -> `err=Ok`, `reports=60`.
- Fix: odczyt przez **`HTTPClient::writeToStream()`** (poprawnie sklada chunki)
  do wlasnego sinku **`PskHttpBodySink : Stream`**, ktory odklada gotowe cialo
  w **PSRAM** (`PSK_HTTP_BODY_CAP = 768 KB`), potem
  `deserializeJson(doc, (const char*)body.data(), body.len, ...)`. `getString()`
  tez sklada chunki, ale trzyma do ~500 KB w wewnetrznym RAM - dlatego sink PSRAM.
- POTA (`api.pota.app`) wysyla `Content-Length` (identity), wiec chunked dotyczy
  tylko PSK.
- Wniosek na przyszlosc: dla kazdego endpointu sprawdzaj `Transfer-Encoding`.
  Gdy `chunked`, NIE parsuj `getStreamPtr()` bezposrednio - uzyj
  `writeToStream()`/`getString()` albo zdjac chunki samodzielnie.

## Naprawa PSK cz.3: mapa zamarzala (kumulacja spotow) (17.10.2026)
Objaw: po wyrysowaniu kresek mapa sie nie odswiezala ("kreski ciagle takie
same"), a po RESET zostawala pusta ("brak odczytu").

- **Przyczyna: spoty tylko sie KUMULOWALY.** `pskAddSpot()` deduplikuje po
  `znak+pasmo+tryb`, a PSKReporter zwraca ZAWSZE te same ~50 najswiezszych
  rekordow (sprawdzone: okno 300 s / 3600 s / 86400 s daje identyczna liste
  top-50). Kolejne pobrania nie dodawaly wiec nic nowego - `pskSpotCount` stal
  w miejscu, `drawPskMapSpotsOnly()` widzial `pskSpotCount == lastPskSpotCount`
  i nic nie rysowal. Mapa zamarzala na pierwszym zestawie.
- **Fix:** kazde UDANE pobranie to swieza migawka - `pskSpotCount = 0;
  lastPskSpotCount = 0;` przed petla spotow (po bramce `receptionReport`).
  Stare spoty znikaja, nowe sie rysuja. Przy `drawPskMapSpotsOnly()` nowy
  (mniejszy) `pskSpotCount` wymusza pelne przerysowanie.
- **RESET:** `resetPskMapSpots()` ustawia `lastPskFetchMs = 0`, wiec kolejne
  `drawPskMap()` pobiera dane od razu - wczesniej po RESET mapa zostawala pusta
  az do uplywu interwalu auto-refresh.
- Uwaga: tryb MQTT dalej KUMULUJE (`copyPskMqttToDisplay` -> `pskAddSpot`) -
  to zamierzone dla strumienia na zywo; dotyczy tylko sciezki HTTP.

## PSK: MQTT znowu domyslny (17.10.2026)
Uzytkownik potwierdzil, ze po przelaczeniu na MQTT mapa dziala i odswieza sie
na zywo ("ruszylo na mqtt"), i poprosil, by zapisac ten tryb na stale.

- `pskMqttEnabled` domyslnie **true** (bylo false) - deklaracja (linia ~252)
  i `preferences->getBool("psk_mqtt_en", true)` w `loadPreferences()`.
- Migracja odwrocona: zamiast wylaczac MQTT (`psk_mqtt_off_migr`), nowa
  `psk_mqtt_default_migr` **wlacza i zapisuje** `psk_mqtt_en=true` raz, gdy
  broker to `mqtt.pskreporter.info`. Dzieki temu tryb jest aktywny od razu po
  starcie (bez potrzeby recznego przelaczania). Wlasny broker uzytkownika
  (inny niz pskreporter) nie jest nadpisywany.
- Tryb HTTP zostaje jako opcja w menu PSK MAP i w panelu WWW
  (`psk_mqtt_enabled`).
- Feed: `pskr/filter/v2raw_1pc/<pasmo>/<tryb>/<nad>/<odb>/<lokNad>/<lokOdb>/<sa>/<ra>`
  (11 segmentow) - dziala (wrocil).

## PSK MQTT: zmiana pasma nie sciagala danych (17.10.2026)
Objaw: po przelaczeniu na inne pasmo (np. 40m) mapa "zawieszala sie" - zero
nowych spotow. Zdiagnozowane na ZYWYM brokerze (`mqtt.pskreporter.info:1883`,
paho-mqtt): realny temat ma **11 segmentow**, a nie 6:
`pskr/filter/v2raw_1pc/<pasmo>/<tryb>/<nad>/<odb>/<lokNad>/<lokOdb>/<sa>/<ra>`
(probka payloadu: `{"sq":..,"f":7074689,"md":"FT8","rp":-16,"sc":"KA1MXL",
"sl":"FN41hs00","rc":"KY4WI","rl":"EL89WA","sa":291,"ra":291,"b":"40m"}`).

Zmierzony czas 30 s na filtr:
| filtr | spoty |
|---|---|
| `pskr/filter/v2raw_1pc/40m/+/+/+` (dawny kod) | **0** |
| `pskr/filter/v2raw_1pc/40m/+/#` | 25 |
| `pskr/filter/v2raw_1pc/40m/#` | 29 |
| `pskr/filter/v2raw_1pc/+/FT8/#` | 129 |
| `pskr/filter/v2raw_1pc/40m/FT8/#` | 21 |

Przyczyny i poprawki:
1. **Filtr subskrypcji 6-seg. nie pasowal do niczego.** `getPskMqttTopicForLocator`
   budowal `.../<pasmo>/<tryb>/+/+/+` (za malo segmentow) -> 0 spotow. Teraz
   `.../<pasmo>/<tryb>/#` (albo `#` gdy brak filtra).
2. **Zly indeks segmentu odbiorcy.** W callbacku `rcIdx` byl 5 = NADawca
   (odbiorca to idx 6). Gdy ustawiono `pskReceiverCallsign`, porownanie
   odrzucalo prawie wszystko. Poprawione na 6.
3. **Stare spoty zostawaly na mapie.** Po zapisie zmiany filtra mapa nie byla
   czyszczona, wiec wygladala jak zamrozona (nowy filtr nic nie dorysowywal).
   W handlerze ZAPISZ: `pskFilterChanged` -> `pskSpotCount=0`,
   `lastPskSpotCount=0` + (HTTP) `lastPskFetchMs=0` + `drawPskMap()`.
4. **Resubskrypcja bez odsubskrybowania.** `loopPskMqtt` przy zmianie tematu
   tylko subskrybowal nowy, wiec lecialy spoty z OBU. Dodano `unsubscribe`
   starego tematu.
5. **Brak mapowania pasm 160/30/17/12 m.** `pskFilterBand.toInt()` dawal np.
   160, ale blok `freq_khz` mapowal tylko 80/40/20/15/10/6 -> `band=0` -> filtr
   odrzucal wszystko. Dodano 160/30/17/12 m w OBU sciezkach (HTTP ~linia 23210,
   MQTT ~linia 25667) oraz kolory w `getPskBandColor`.

## Ikona baterii przesunieta w lewo (17.10.2026)
Na prosbe uzytkownika ikona baterii na WSZYSTKICH ekranach przesunieta o 12 px
w lewo, zeby nie nachodzila na prawa krawedz:
- ekran zegara (klasyczny + nowoczesny): `drawBatteryIcon(screenW - 70, 8)` ->
  `screenW - 82, 8` (~linie 3744 i 4061),
- pozostale ekrany (`uiHeader`): `drawBatteryIcon(420, 1)` -> `408, 1` (~linia 6283),
- `drawBatteryQuickUpdate` (mirror pozycji): `screenW-70`/`420` -> `screenW-82`/`408`
  (~linia 3626),
- `uiHeader`: `rightLimit` 415 -> 403, by tytul nie wchodzil na przesunieta baterie.
Wifi (445/405) i GPS (88) bez zmian.


## Wybor czcionki znaku i zegara (22.10.2026)
Na prosbe uzytkownika ("lepsza czcionka znaku i godziny" + wybor czcionki w menu
zegara) dodano 14 krojow VLW + wbudowana czcionke TFT_eSPI:
- **Generacja**: `python3 tools/make_clock_fonts.py` pobiera kroje (Google Fonts,
  OFL) do `tools/fonts_src/` i tworzy w `data/fonts/` po dwa pliki na opcje:
  `clockNN-call.vlw` (znak) i `clockNN-time.vlw` (duzy zegar). Rozmiar dobierany
  automatycznie (`pick_size`), by `00:00:00` <= 400x60 px i znak <= 400x38 px.
  Zestaw znakow: `0123456789:./-+ A..Z` (42 glify -> male pliki, ~0,85 MB razem).
- **Firmware**: `#define CLOCK_FONT_COUNT 14`, tablica `CLOCK_FONTS[]` (nazwa +
  sciezki VLW), `uint8_t clockFontIndex` (0 = wbudowana). Rysowanie przez
  `clockDrawCentered()`, wspolne dla stylu klasycznego i nowoczesnego.
- **Pozycjonowanie (wazne!)**: uzywamy `drawString()` z `TC_DATUM`, NIE `print()`.
  `print()` ignoruje `setTextDatum` - tekst startowal od kursora (przesuniety w
  prawo o pol szerokosci) i od gory pola czcionki (zjazd w dol). `drawString()`
  honoruje `TC_DATUM` i `setTextPadding(360)` (brak duchow przy sekundowej
  aktualizacji). Pion liczony z `clockInkOffset()` = rzeczywisty atrament glifow
  (`gFont.ascent` + `(maxDesc - maxAsc)/2`), nie z `fontHeight()` (to yAdvance).
  Pas znaku: y=50..94 (srodek 72), pas zegara: y=126..178 (srodek 152).
- **Stala szerokosc zegara (23.10.2026, wazne!)**: kroje VLW maja rozna szerokosc
  cyfr (np. `clock05-time`: „1" = 16 px, „0" = 43 px), wiec zegar zmienial
  szerokosc i „uciekal" w bok przy zmianie cyfry, a napis nie byl rowno
  wysrodkowany. Dodatkowo `TFT_eSPI::textWidth()` liczy sume `gWidth`, a
  `drawString()` przesuwa kursor o `gxAdvance` - te wartosci sie roznia, wiec
  srodek wypadal zle. Rozwiazanie: `clockDrawMonospace()` rysuje znak po znaku w
  **komorkach o stalej szerokosci** (= `gxAdvance` najszerszej cyfry/dwukropka),
  wysrodkowanych wokol x=240; uzywane dla duzego zegara (`forTime`) i w pasku
  podgladu (`clockMonospaceWidth()` do pomiaru). Znak (callsign) zostaje
  proporcjonalny. Podglad QA: `tools/render_clock_center_preview.py`.
- **Brak migotania przy zmianie sekund (23.10.2026, wazne!)**: objaw = migotanie
  cyfr przy kazdej zmianie sekundy. Dwie przyczyny, obie naprawione:
  1. **Przerysowanie calego zegara co sekunde.** `clockDrawMonospace()` czyscil i
     rysowal wszystkie 8 komorek przy kazdym odswiezeniu. Teraz pamieta poprzednia
     godzine (`prevText`) i dotyka TYLKO komorki, ktore sie zmienily (czyszczenie +
     rysunek jednej cyfry) - zwykle 1 komorka zamiast 8 (~67% mniej pikseli na SPI).
     `useCache=false` (podglad w menu) wymusza pelne rysowanie. Pamiec czyszczona
     przez `clockMonoGeneration`/`clockMonoInvalidate()` przy `fillScreen`
     (nowy ekran/kroj) oraz gdy chwilowo zabraknie czcionki VLW.
  2. **Pionowe drganie 1 px.** Korekta pionowa liczona z BIEZACEJ godziny
     (`clockInkOffset(text)`) - rozne cyfry maja rozna wysokosc atramentu
     (BUNGEE `clock10`, MONO BOLD `clock14`: „1" i „8" roznia sie o 1 px). Teraz
     `clockTimeInkOffset()` liczy offset ze stalego zestawu `"0123456789:"`
     (`clockInkOffsetRef()`), niezaleznie od wyswietlanej godziny - dla wszystkich
     krojow.
  Dodatkowo `updateScreen1ClockClassic()` rysuje tylko gdy tekst godziny sie
  zmienil (wczesniej przerysowywal w kazdej iteracji `loop()`). Podglad QA:
  `tools/render_clock_flicker_preview.py` (podswietla przerysowane komorki).
- **Menu**: `drawHamClockTimeMenu()` + `handleScreen1MenuTouch()` - siatka 5x3
  (DOMYSLNA + 14 krojow, `fStartY=214`, `ftH=20`) ponizej przelacznikow UTC/LOCAL,
  stylu i DST. Pasek podgladu `clockFontPreviewBand(150,146,326,62,...)` ma 62 px
  (najwyzsze kroje ~60 px). Wybor zapisywany od razu (`savePreferences()` -> NVS
  `clock_font`). CLOSE na y=286.
- **WWW**: pole `clock_font` w `/api/config` (GET/POST) i w formularzu
  (3 miejsca w `main.cpp`: HTML, `val()`, payload `gint`).
- **Tlumaczenia**: nowy `TrKey::TR_CLOCK_FONT` we wszystkich 6 tablicach
  (PL/EN/DE/FR/ES/RU).
- **Podglad QA**: `python3 tools/render_clock_fonts_preview.py` -> plansza PNG
  wszystkich czcionek (wlasny parser VLW, bez sprzetu).
- **Uwaga na RAM**: kazda czcionka VLW trzyma metryki w RAM (`loadMetrics`), ale
  sa zwalniane w `unloadFont()` po kazdym rysowaniu; warunek `clockFontHasFile()`
  wymaga >90 KB wolnego heapu, inaczej fallback na wbudowana czcionke.
- **Artefakty**: `pio run -e esp32-s3-n16r8` (+ `-notouch`), `-t buildfs`,
  `merge_bin` -> `dist/`, `python3 tools/make_zip.py`. LittleFS: data ~4,6 MB z
  7,87 MB partycji (28 plikow clock*.vlw = ~0,85 MB).


## Paski WiFi w naglowku — wskazania zanizone (2026-10-13) — NAPRAWIONE
Objaw: router ~2 m od urzadzenia, a ikona WiFi pokazywala 1 kreske (albo 0 /
"brak zasiegu" tuz po polaczeniu), mimo dobrego sygnalu (skan logowal RSSI=-60).

Przyczyny (trzy, wszystkie naprawione):
1. **Progi zbyt surowe.** -50/-60/-70/-80 dBm dawaly 1 kreske dopiero przy
   RSSI <= -70. Dla 2 m / -60 dBm wychodzily 2 kreski, a chwilowy spadek do -80
   dawal 0 ("brak zasiegu", czerwone wypelnienie). Nowe progi (`rssiToBars`):
   >-55=4, >-67=3, >-78=2, >-88=1, inaczej 0. -60 dBm -> 3 kreski.
2. **RSSI==0 po polaczeniu.** Tuz po GOT_IP `WiFi.RSSI()` zwraca 0 (radio nie
   zdazylo zmierzyc), co wpadalo w "else -> 0 belek" = czerwony "brak zasiegu".
   Teraz `rssi==0` pokazuje 1 kreske zamiast falszywego braku zasiegu.
3. **Naglowek rysowany raz.** `screen1HeaderNeedsRedraw` ustawia sie przy wejsciu
   na ekran; jesli wtedy RSSI bylo jeszcze 0, ikona zostawala zla na stale.
   Dodano `drawWifiBarsQuickUpdate()` — szybkie przerysowanie SAMYCH paskow co 5 s
   z loop() (bez pelnego rysowania naglowka, wiec bez migotania).

Dodatkowo: `drawWifiSignalBars(x, y, strength, bg)` czysci obszar paskow kolorem
tla naglowka — pusta belka (drawRect) nie zostawia juz sladu po mocniejszym
sygnalе. Ekran monitora systemu pokazuje "NN dBm N/4" z tego samego progu
(`rssiToBars`), zeby dalo sie skorelowac odczyt z ikona.

Uwaga: sam odczyt RSSI to sygnal z routera do ESP. Jesli mimo to jest slaby —
sprawdzic antene (community: "antena byla luzna"), odleglosc od metalu/zasilacza
i czy router nie jest w trybie oszczedzania. Log `[WIFI SCAN] ... RSSI=..` w
konsoli pokazuje pomiar z chwili laczenia.

## Pola dotyku na wszystkich ekranach — audyt i naprawa (2026-10-13)
Zgloszenie: "sprawdz w wszystkich ekranach pola dotyku i je napraw".

Znaleziony blad (funkcjonalny): **wspolna nawigacja w rogach zjadala dotkniecia
przyciskow** na trzech ekranach z wlasnymi przyciskami przy dolnej krawedzi.
Globalny dispatcher (`handleTouchNavigation`) sprawdzal strefy nawigacji
`y >= 60 && (x < 80 || x >= 400)` PRZED handlerami ekranow, a dla PSK/STACJA/
LOGGER te strefy nachodza na:
- **LOGGER**: przyciski `SAVE QSO` (x 8..119), `CLEAR` (125..236),
  `EXPORT ADIF` (242..353), `SETTINGS` (359..470) na `y 248..286` - kazde
  dotkniecie lewych/prawych przelaczalo ekran zamiast wykonac akcje;
- **STACJA**: prawy transceiver (`x 346..466`, `y 200..294`) - dotkniecie
  otwieralo zly ekran zamiast klawiatury;
- **PSK MAP**: wlasna nawigacja `cornerY=280` byla martwa (przechwytywana
  wczesniej przez `y>=60`), a lewa/prawa strefa mapy (80 px) nie dzialala.

Naprawa:
- nowy wspolny helper `handleScreenSwitchTouch(x, y, cornerY, cornerX)`;
- PSK/STACJA/LOGGER **wylaczone** ze wspolnej nawigacji w dispatcherze
  (`screenHandlesOwnNav`) i obsluguja nawigacje same, tylko w **pasie stopki**
  (`y >= 296`, `x < 80` / `x >= 400`) - przyciski sa nietkniete;
- PSK odzyskal wlasna nawigacje `y >= 280` (jak zamierzone w komentarzu).

Dodatkowo: etykieta jezyka w menu jasnosci (`langLabelY`) byla na `208` i
chowala sie pod przyciskami (`190..216`) - przeniesiona na `220` (kafelki
jezykow sa na `228`).

Audyt: przejrzano wszystkie `handle*Touch` + hit-testy (`isPointInRect`,
`isAprsAlertCloseButtonHit`, `isGpsScreenCloseButtonHit`,
`isScreen6RadarRangeTileHit`, `callsignHit*`, `clockHit*`, `gpsIconHit*`).
Pozostale ekrany (DX/POTA/HamAlert/APRS lista/radar, pogoda, menu filtrow,
menu kolorow, jasnosc, uspienie, wygaszacz, QRZ popup, GPS) - geometria
hit-testow zgodna z rysowaniem; nawigacja w rogach jest tam zamierzona.
`isScreen6ViewToggleHit()`/`drawScreen6ViewToggleButton3D()` pozostaja
nieuzywane (przelaczanie listy/radaru robi wybor ekranu 6/9 w WWW).

## APRS RADAR - nawigacja ekranow "slabo dziala" (2026-10-13)
Zgloszenie: "w ekranie aprs radar przyciski przelaczania na inny ekran slabo dzialaja".

Przyczyny:
1. Ekran radaru jest pelnoekranowy (`issFullBleed`), wiec wspolny `uiFooter()`
   NIE rysowal strzalek "SWITCH SCREEN" - nie bylo widac, gdzie dotknac.
2. Animacja smugi (`aprsRadarTick()`: `fillScreen` + `pushSprite` 480x320,
   ~50 ms na klatke) byla wolana z `UI_Task` PRZED `handleTouchNavigation()`,
   wiec w oknie rysowania XPT2046 nie byl odpytywany - tapniecie trafialo
   w klatke i przepadalo (ten sam problem, ktory dotyczyl PSK).

Naprawa:
- `aprsRadarRenderFrame()` rysuje do sprite'a dwie strzalki nawigacji w pasie
  stopki (x=14 i x=466, y=309, kolor `ARP_COL_SWEEP`); napisu "SWITCH SCREEN"
  nie ma, bo srodek dolu zajmuja kafelki zakresu x0.2..x4 (`ARP_TILE_Y=300`).
- w bloku `SCREEN_APRS_RADAR` dispatcher-a dodano
  `handleScreenSwitchTouch(x, y, 296, 80)` - nawigacja tylko w pasie stopki
  (y>=296), z dala od kafelkow zakresu i animacji.
- animacje przeniesiono PO `handleTouchNavigation()` (zeby klatka nie
  opozniala odczytu dotyku), w UI_Task jest teraz: (1) rysunki zadan, (2) odczyt
  dotyku, (3) animacja radaru.


## Ikony APRS na ekranie klasycznym (aprs.fi)

- Ikony APRS 48x48 pochodza z zestawu aprs.fi (hessu/aprs-symbols, CC BY-SA 4.0).
  Zrodla sprite'ow i skrypty generujace sa w `.mock/` (nie commitowane do firmware).
- Gotowe pliki: `data/aprs_icons/` - `*.png` (na czarnym tle, domyslne),
  `bmp/*.bmp` (BMP 24-bit, format drawBmpFromFS()), `transparent/*.png`.
- Firmware: drawAprsAlertFsIcon() mapuje getAPRSSymbolShort() na nazwe pliku i
  rysuje /aprs_icons/bmp/<nazwa>.bmp; brak pliku -> fallback do ikon wektorowych.
  Uzywane TYLKO w stylu klasycznym (drawAprsAlertClassic); nowoczesny bez zmian.
- Wazne: drawPngFromFS() rysuje pelne wiersze ekranu (wspolrzedne ignorowane,
  origin 0,0) - do ikon w punkcie uzywac drawBmpFromFS() (rysuje prostokat ikony).
- Merged-flash: tools/make_release.sh scala bootloader+partitions+boot_app0+
  firmware+littlefs; LittleFS z ikonami wymaga buildfs PRZED scaleniem.

## Srodowisko (resety)

- Po resecie PlatformIO bywa usuwany: pip install platformio (~/.local/bin,
  export PATH="$HOME/.local/bin:$PATH").
- Podglad WWW: bash tools/serve.sh (serwuje /workspace/share/ na porcie 12000).


## Powiększenie LittleFS (partitions.csv)

- **01.11.2026:** slot OTA `app1` (3.5 MB) oddany na LittleFS — OTA **nie jest
  używane** (brak `Update.h`/`esp_ota`; firmware wgrywany przez WebSerial/merged-flash).
  Nowa tablica: app0 0x10000/**0x3F0000** (3.94 MB), littlefs **0x400000/0xBF0000**
  (**11.94 MB**), coredump 0xFF0000/0x10000. Bufor WWW: ~1.9 MB → **~4.9 MB**.
  Offsets 0x700000 → **0x400000** w: `tools/make_release.sh`, `main.cpp`
  (manual + tabela WWW), `README_S3.md`, `INSTRUKCJA.md`, `download/README_S3.md`.
- Wcześniej (31.10): app0 0x10000/0x380000, app1 0x390000/0x370000,
  littlefs 0x700000/0x8F0000 (~8.9 MB); jeszcze wcześniej app0/app1 po 4 MB,
  littlefs 0x810000/0x7E0000 (~7.9 MB).
- UWAGA: zmiana tablicy partycji wymaga wgrania całego merged-flash
  (albo partitions.bin + littlefs.bin) - sam uploadfs nie wystarczy.
- LittleFS montuje się z offsetu z tablicy partycji, więc po zmianie rozmiaru
  stary littlefs.bin trzeba wgrać od nowa (obszar 0x400000..0xFF0000).
- **Bufor WWW = LittleFS wolne po odjęciu `data/`.** Uwaga na zaokrąglanie do
  bloków 4 kB: `data/` to 675 plików, więc 5.28 MB zajmuje realnie ~7.0 MB.
  Wolne = 11.94 − 7.04 ≈ **4.9 MB** (LittleFS `usedBytes()` też liczy blokami).
