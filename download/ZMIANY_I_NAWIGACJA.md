# ESP32-HAM-CLOCK S3 — zmiany między wersjami i nawigacja po ekranach

Dokument dotyczy firmware **v1.2e-beta** na platformę **ESP32-S3 N16R8**
(16 MB flash, 8 MB PSRAM) z wyświetlaczem **ILI9488 480×320 (3.5"/4")** po SPI
i panelem dotykowym **XPT2046**. Dystrybucja: ZIP lokalnie + serwer pobierania
(port 12000) + strona WebSerial (flash/).

---

# 1. Zmiany między wersjami

## 1.1 Od v1.2e (wydanie zbiorcze 06.11.2026) do v1.2e-beta (stan bieżący)

Firmware zachowuje wszystkie funkcje z v1.2e oraz dodaje USZLACHETNIENIA
z kolejnych wersji beta (v1.2e BETA). Ponadto w tej gałęzi wprowadzono:

### GREY LINE (SZARY PAS) — ekran propagacji na szarym paśmie
- Nowy ekran **GREY LINE** z mapą świata (terminator dzień/noc, gradient
  szarego pasa 6°, punkt subsolarny, QTH z callsignem, siatka 30°/15°,
  legenda DZIEN/SZARY PAS/NOC, UTC, odświeżanie co 60 s).
- **A — dotyk mapy**: dotknięcie dowolnego punktu → w stopce współrzędne
  + stan DZIEN/NOC/SZARY PAS; marker zielonym kółkiem wokół punktu.
- **C — dotknięcie QTH**: podświetlenie QTH, szczegóły: pora dnia w QTH,
  a gdy QTH leży w szarym paśmie — **sugestia kierunku DX** („DX bedzie z E/W").
- **B — wschód/zachód Słońca dla QTH**: stopka pokazuje `SWIT HH:MM UTC
  ZACH HH:MM UTC <DZIEN/NOC/SZARY PAS>`; obsługa dnia/nocy polarnej.
- **E — dotknięcie Słońca**: szczegóły sub-solar point — dystans + azymut
  z QTH oraz równanie czasu (EoT).
- **D — etykieta Słońca** pokazuje EoT: `S +lon/+lat +Nm`.
- **F — siatka**: dotknięcie pola `GRID:30` w stopce przełącza siatkę
  30°/15°.
- **Strzałki nawigacyjne na czarnym tle** w dolnych rogach stopki
  (zmiana ekranu; strzałki są rysowane ponownie razem ze stopką, więc
  nie znikają po dotknięciu mapy).

### PROPAGACJA — region domyślny mapy
- Domyślny region mapy PROPAGACJA ustawiany automatycznie na podstawie
  **lokatora** (mapowanie lokator→kontynent EU/NA/AF/SA/AS/OC) oraz
  **z GPS** (gdy pozycja pochodzi z odbiornika GPS).
- Ręczny wybór regionu dotykiem ma priorytet do czasu zmiany QTH.

### APRS — naprawa „zawieszania się" ESP przy alercie
- Przyczyna: synchroniczne żądania HTTPS w `loop()` (mapa PROPAGACJA,
  Callook/QRZ, pogoda, alerty Open-Meteo) blokowały pętlę KISS/APRS-IS;
  pakiety się buforowały i alert „wracał naraz".
- Poprawki: throttling rysowania „ŁADOWANIE..." (300 ms) oraz przeniesienie
  ciężkich pobrań poza bezpośrednią ścieżkę rysowania.

### WWW / panele (zachowane z poprzednich wersji beta)
- Motyw jasny/ciemny w panelu WWW (przełącznik 🌙/🌞, `localStorage`).
- Próg ikony „pioruna" (ładowania) obniżony do **>3.9 V** (zachowany).
- ADIF/OTA przez WWW, AQI, motywy WWW (6 języków), kalibracja baterii,
  włącznik TĘCZA/MIX KOLOR, suwak głośności APRS, REPLY RX→TX.

### APRS — „Buzzer NOC" (cichy sygnał 1 pik)
- Na ekranie **APRS** w menu ustawień dodano checkbox **„Buzzer NOC"** obok
  „Buzzer". Zaznaczenie „Buzzer NOC" **odznacza „Buzzer"** (normalny buzzer
  jest wtedy wyciszony), a **każdy alert APRS gra jeden krótki, cichy pik**
  zamiast melodii zależnej od typu stacji.
- Ustawienie zapamiętywane w NVS (`aprs_bzr_noc`), dostępne też w panelu WWW
  („Buzzer NOC (cichy, 1 pik dla alertów APRS)") i w `/api/config`.

### PROPAGACJA — naprawa ładowania mapy i migania
- Przyczyna: przy wejściu na ekran PROPAGACJA ustawiano tylko flagę
  `propMapFetching`, ale **nie zgłaszano zadania taskowi tła**
  (`propMapFetchReq`), więc mapa wisiała na „ŁADOWANIE..." i nie pobierała się
  nigdy — a ekran przerysowywał stan co ~300 ms, stąd migotanie.
- Poprawka: `drawSunSpots()` ustawia teraz `propMapFetchReq`, a task tła
  (`propMapFetchBgTask`) pobiera/dekoduje mapę bez blokowania dotyku. Po
  zmianie pasma/regionu mapa ładuje się od razu; godzinę UTC lub region
  (z lokatora/GPS) mapa odświeża się automatycznie w tle.

### Pamięć — większa partycja aplikacji (BLE + dekodery obrazu)
- Firmware z BLE i dekoderami PNG/JPG przekroczył limit partycji `app0`
  (0x240000 = 2.25 MB). Partycję aplikacji powiększono do **0x2A0000
  (2.625 MB)**, a LittleFS przesunięto z `0x250000` na **`0x2B0000`**
  (rozmiar 0xD40000 ≈ 13.3 MB — wciąż dużo ponad zawartość `data/`).
- Zaktualizowano `partitions.csv`, `tools/make_release.sh` oraz odnośniki
  w `INSTRUKCJA.md` i `README_S3.md`. Wgrywający przez WebSerial/merged-flash
  nic nie zmieniają (obraz scalony jak dotąd).

## 1.2 Od v1.1 (i wcześniejszych 1.2x) do v1.2e — skrót

### Nowe ekrany i funkcje
- **BAND CONDITION** (ekran pasm z UKE/regionami), **DX CLUSTER** (telnet),
  **POTA / HAMALERT**, **APRS-IS + APRS RADAR**, **WEATHER** (pogoda DSP
  + prognoza), **PSK MAP**, **MATRIX CLOCK**, **SYSTEM INFO**, **STATION
  (transceivery)**, **HAM LOGGER**, **ISS PASS TRACKING**, **RADIOSONDE**,
  **GREY LINE**.
- Zegar z **15 czcionkami** cyfr (dirty-cells, stały offset atramentu —
  koniec migotania), wybór czcionki znaku i zegara.
- **Alert pogodowy** (Open-Meteo, piorun >3.9 V), **ADIF/OTA przez WWW**,
  **AQI**, motywy WWW (jasny/ciemny + 6 języków), **TĘCZA** cyfr zegara.
- APRS: **wysyłanie i odpowiadanie** na wiadomości, suwak głośności,
  historia RX/TX, RE-RX→TX (REPLY).

### Kluczowe naprawy (istotne dla użytkownika)
- **Migotanie cyfr zegara** — rysowanie tylko zmienionych komórek, stały
  pionowy offset.
- **Ściemnianie/białe litery** (hotfixy v1.2e): dimming tylko przy
  `tftNightDimmingPercent>0`, jaśniejsza czcionka, widoczność AP
  (ESP32-HAM-CLOCK) po flashowaniu.
- APRS zwieszenia (opis wyżej) — poprawki w v1.2e-beta.

### Pamięć / instalacja
- Obrazy łączone: **merged-flash.bin** (dotyk) i **merged-flash-notouch.bin**
  (bez dotyku) — jeden plik do wgrania, typowo:
  `esptool.py --chip esp32s3 --baud 460800 merge_bin write_flash 0 merged-flash.bin`
- `new_install_prompt_erase` w manifestach WebSerial; ZIP 737 wpisów
  (main.cpp, binarki, narzędzia, docs, motywy WWW).

## 1.3 Historia wydań (w skrócie)
| Wersja | Data | Co wnosi |
|---|---|---|
| v1.1 | — | Bazowe wydanie z zegarem i podstawowymi ekranami |
| v1.2 | 12.10.2026 | Nowe ekrany (DX, POTA, HAMALERT, APRS, WEATHER...) |
| v1.2a/b/c | 18–21.10.2026 | Poprawki PSK MQTT, nagłówka, drobne ulepszenia |
| v1.2e | 23.10–06.11.2026 | Wydanie zbiorcze: czcionki zegara (15), alert pogodowy, ADIF/OTA WWW, AQI, motywy WWW, naprawa migotania; hotfixy ściemniania |
| v1.2e BETA | 08–14.11.2026 | Motyw jasny/ciemny WWW, próg pioruna >3.9 V, kalibracja baterii, TĘCZA, REPLY APRS, historia APRS, GREY LINE |
| **v1.2e-beta** | b.d. (bieżąca) | GREY LINE (dotyk A/B/C/D/E/F, strzałki nawigacji), region PROPAGACJI z lokatora/GPS, naprawa zawieszeń APRS, wersja spójna wszędzie |

---

# 2. Nawigacja po ekranach

## 2.1 Sposoby przełączania

1. **Przyciski fizyczne** (jeśli skonfigurowane): zmiana ekranu obsługiwana
   w `loop()` przez `getNextScreenId()`/`getPrevScreenId()`.
2. **Ekran dotykowy** (XPT2046): **dolne lewy/prawy róg** ekranu zmieniają
   ekran — po kolei zgodnie z kolejnością ekranów (`screenOrder[]`, patrz 2.2).
   Dwustrefowa obsługa: lewy dół = **następny** (w prawo), prawy dół =
   **poprzedni** (w lewo), zgodnie z funkcją `handleScreenSwitchTouch()`.
3. **Auto-przełączanie**: opcjonalny tryb przeglądania ekranów co
   `TFT_SWITCH_TIME` (domyślnie 30 s), włączany/wyłączany z ustawień.
4. **Ekrany z własnym dotykiem** (GREYLINE, PROPAGACJA, PSK Mapa, STACJA,
   LOGGER, detale sondy): najpierw ich własna obsługa dotyku, a strzałki
   (rogi stopki) nadal zmieniają ekran.

## 2.2 Kolejność ekranów (screenOrder[])

1. **HAM CLOCK** — zegar główny
2. **DX CLUSTER** — spoty DX (telnet)
3. **APRS-IS** — wiadomości APRS-IS
4. **APRS RADAR** — mapa pozycji APRS
5. **BAND INFO** — pasma/UKF
6. **SUN SPOTS (PROPAGACJA)** — kondycja pasm, mapa propagacji
7. **WEATHER DSP** — aktualna pogoda
8. **WEATHER FORECAST** — prognoza
9. **POTA CLUSTER** — spoty POTA
10. **HAMALERT CLUSTER** — alerty HAMALERT
11. **PSK MAP** — mapa PSK reporter (własny dotyk)
12. **MATRIX CLOCK** — zegar matrycowy
13. **SYSTEM INFO** — o systemie (IP, wersja, NVS)
14. **STATION** — transceivery (własny dotyk)
15. **ISS PASS TRACKING** — przeloty ISS
16. **HAM LOGGER** — dziennik QSO (własny dotyk)
17. **GREY LINE** — szary pas propagacji (własny dotyk, strzałki nawigacji)
18. **RADIOSONDE** — sonda meteorologiczna (dostępny z menu ekranu APRS,
    nie w głównej kolejności)

`SCREEN_OFF` jest pomijane przez `getNextScreenId()`/`getPrevScreenId()`.

## 2.3 Szczegóły dotyku

- **Strzałki w dolnych rogach** każdego ekranu: lewy róg `x<~80, y>~296`
  → następny ekran; prawy róg `x>~400, y>~296` → poprzedni ekran.
- **Ekran GREY LINE**: dodatkowo dotyk mapy = wybór punktu (A), Słońca (E),
  QTH (C), pola GRID (F); strzałki na czarnym tle rysowane razem ze stopką.
- **Ekran PROPAGACJA**: dotyk górnych przycisków pasm/regionów wybiera pasmo
  i region; rogi stopki — zmiana ekranu.
- **Menu wewnętrzne** (np. jasność, bateria): wyłącza globalną nawigację,
  dopóki menu aktywne.

## 2.4 Dla programisty
- Enum ekranów: `enum ScreenType` (pliku `main.cpp`, ok. linii 698).
- Kolejność: `ScreenType screenOrder[SCREEN_PAGE_COUNT]` (ok. linii 1128).
- Funkcje: `getNextScreenId()`, `getPrevScreenId()`, `findScreenOrderIndex()`,
  `handleScreenSwitchTouch(x, y, cornerY, cornerX)`.
- Ekrany z własnym dotykiem oznaczane flagą `screenHandlesOwnNav` / własnymi
  handlerami (`handleGreylineTouch`, `handlePropagationTouch`, ...).

---

# 3. Dystrybucja / wgrywanie
- **WebSerial**: `https://tom50505050.github.io/esp32-ham-clock-s3/flash/`
  („Wgraj firmware (dotyk)" / „bez dotyku").
- **Pliki**: `merged-flash.bin` (touch), `merged-flash-notouch.bin`,
  `esp32-ham-clock-s3.zip`.
- **Serwer lokalny**: port 12000 (`http://localhost:12000/`).
- **Uwaga**: przy pierwszym wgraniu manifesty ustawiają `new_install_prompt_erase`
  — po zmianie tablicy partycji wymagane pełne kasowanie flash.