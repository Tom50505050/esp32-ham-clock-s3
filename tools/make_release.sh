#!/usr/bin/env bash
# Scala obrazy dla obu env (esp32-s3-n16r8, esp32-s3-n16r8-notouch) do dist/.
set -u
cd "$(dirname "$0")/.."
# Statyczne zasoby WWW skompresowane (index*.html.gz, style*.css.gz, satellite.js.gz)
# - firmware serwuje je gdy klient wysle Accept-Encoding: gzip (streamStaticAsset).
python3 tools/make_gz.py
# esptool z bibliotekami platformio (systemowy python3 nie ma modulu esptool).
ESPTOOL_ROOT=/workspace/pioenv
BOOT_APP0=~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin

merge() {
  local env="$1" out="$2"
  local b=".pio/build/$env"
  "$ESPTOOL_ROOT/bin/python3" "$ESPTOOL_ROOT/bin/esptool.py" --chip esp32s3 merge_bin -o "$out" \
    --flash_mode dio --flash_freq 80m --flash_size 16MB \
    0x0 "$b/bootloader.bin" \
    0x8000 "$b/partitions.bin" \
    0xe000 "$BOOT_APP0" \
    0x10000 "$b/firmware.bin" \
    0x250000 "$b/littlefs.bin"
}

merge esp32-s3-n16r8        dist/merged-flash.bin
merge esp32-s3-n16r8-notouch dist/merged-flash-notouch.bin
ls -la dist/
