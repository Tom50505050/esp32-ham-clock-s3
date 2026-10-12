@echo off
REM Wgrywanie TESTU DOTYKU (XPT2046) na ESP32-S3 N16R8.
REM Obraz scalony: bootloader + partycje + aplikacja testowa pod 0x0.
REM Uzycie:  flash_touch_test.bat          (domyslnie COM9)
REM          flash_touch_test.bat COM7
setlocal
set PORT=%1
if "%PORT%"=="" set PORT=COM9
set BAUD=921600

echo.
echo === Wgrywanie testu dotyku na %PORT% ===
echo.

py -m esptool --chip esp32s3 --port %PORT% --baud %BAUD% write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB -z 0x0 touch_test_merged.bin
if errorlevel 1 (
  echo.
  echo [BLAD] Nie udalo sie wgrac. Sprawdz port %PORT% i sterowniki USB.
  goto :eof
)

echo.
echo Gotowe. Podglad: py -m serial.tools.miniterm %PORT% 115200
echo Oczekiwane: linie "[   Ns] brak dotyku (z=..) - zyje", a przy dotknieciu "DOTYK x=.. y=.. z=..".
endlocal
