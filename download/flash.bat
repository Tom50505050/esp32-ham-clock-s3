@echo off
REM Wgrywanie firmware + fontow ESP32-HAM-CLOCK na ESP32-S3 N16R8 (16MB).
REM Uzycie:  flash.bat            (domyslnie COM9)
REM          flash.bat COM7
setlocal
set PORT=%1
if "%PORT%"=="" set PORT=COM9
set BAUD=921600

echo.
echo === Wgrywanie scalonego obrazu (firmware + fonty + LittleFS) na %PORT% ===
echo.
echo Plik: merged-flash.bin (offset 0x0). Wariant bez dotyku: merged-flash-notouch.bin
echo.

py -m esptool --chip esp32s3 --port %PORT% --baud %BAUD% write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB -z 0x0 merged-flash.bin
if errorlevel 1 (
  echo.
  echo [BLAD] Nie udalo sie wgrac. Sprawdz port %PORT% i sterowniki USB.
  goto :eof
)

echo.
echo Gotowe. Nacisnij RST, jesli plytka nie zrestartowala sie sama.
echo Podglad: py -m serial.tools.miniterm %PORT% 115200
echo Przez przegladarke: https://jason2866.github.io/WebSerial_ESPTool/ (merged-flash.bin, offset 0x0)
endlocal
