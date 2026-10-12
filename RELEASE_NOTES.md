# ESP32-HAM-CLOCK v1 — ESP32-S3 N16R8 + ILI9488 3.5"

## Zmiany w tym wydaniu

- **WWW:** ikony SVG na kafelkach — nawigacja, pasek statusu i metryki ISS (PL i EN).
- **WWW:** naprawa zakładki „Instrukcja" — `/instrukcja.txt` zwraca zwykły tekst
  (wcześniej 301 bez `Location`, przez co `fetch()` zgłaszał błąd).
- **WWW:** wyrazistsza ikona satelity (kadłub + antena, powiększona).
- **TFT:** flaga kraju pod ISS wyrównana do wiersza nazwy kraju; odczyty
  LAT/LON/ALT/SPEED wyśrodkowane (przywrócone `MC_DATUM`).

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
