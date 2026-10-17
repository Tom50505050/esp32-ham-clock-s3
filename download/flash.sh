#!/usr/bin/env bash
# Wgrywanie firmware ESP32-HAM-CLOCK na ESP32-S3 N16R8 (16MB).
# Użycie:  PORT=/dev/ttyACM0 ./flash.sh
#          PORT=COM7 ./flash.sh
#
# Wgrywamy TYLKO scalony obraz merged-flash.bin (offset 0x0) - zawiera
# bootloader, partycje, boot_app0, firmware i LittleFS. Wariant bez dotyku:
# merged-flash-notouch.bin.
set -u
PORT="${PORT:-/dev/ttyACM0}"
BAUD="${BAUD:-921600}"
CHIP="esp32s3"
IMAGE="${IMAGE:-merged-flash.bin}"

esptool.py --chip "$CHIP" --port "$PORT" --baud "$BAUD" --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB -z \
  0x0 "$IMAGE"

echo
echo "Gotowe. Naciśnij RST, jeśli płytka nie zrestartowała się sama."
echo "Podgląd: pio device monitor -b 115200   (albo: python -m serial.tools.miniterm $PORT 115200)"
echo "Przez przeglądarkę: https://jason2866.github.io/WebSerial_ESPTool/ ($IMAGE, offset 0x0)"
