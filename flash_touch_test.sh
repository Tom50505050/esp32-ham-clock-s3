#!/usr/bin/env bash
# Wgrywanie TESTU DOTYKU (XPT2046) na ESP32-S3 N16R8.
# Użycie:  PORT=/dev/ttyACM0 ./flash_touch_test.sh
set -u
PORT="${PORT:-/dev/ttyACM0}"
BAUD="${BAUD:-921600}"

esptool.py --chip esp32s3 --port "$PORT" --baud "$BAUD" --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB -z \
  0x0 touch_test_merged.bin

echo
echo "Gotowe. Podgląd: python -m serial.tools.miniterm $PORT 115200"
echo "Oczekiwane: '[ Ns] brak dotyku (z=..) - zyje', a przy dotknięciu 'DOTYK x=.. y=.. z=..'."
