#!/usr/bin/env bash
# Wgrywanie firmware ESP32-HAM-CLOCK na ESP32-S3 N16R8 (16MB).
# Użycie:  PORT=/dev/ttyACM0 ./flash.sh
#          PORT=COM7 ./flash.sh
set -u
PORT="${PORT:-/dev/ttyACM0}"
BAUD="${BAUD:-921600}"
CHIP="esp32s3"

# bootloader dla S3 jest pod 0x0 (NIE 0x1000 jak na ESP32 klasycznym)
esptool.py --chip "$CHIP" --port "$PORT" --baud "$BAUD" --before default_reset --after hard_reset \
  write_flash -z \
  0x0      bootloader.bin \
  0x8000   partitions.bin \
  0xe000   boot_app0.bin \
  0x10000  firmware.bin \
  0x810000 littlefs.bin

echo
echo "Gotowe. Naciśnij RST, jeśli płytka nie zrestartowała się sama."
echo "Podgląd: pio device monitor -b 115200   (albo: python -m serial.tools.miniterm $PORT 115200)"
