APRS symbol icons (48x48) - for the CLASSIC APRS alert screen.

WARIANTY:
  *.png          - ikony NA CZARNYM TLE (PNG spłaszczone, alpha=255)  <- DOMYSLNY
  bmp/*.bmp      - ikony NA CZARNYM TLE (BMP 24-bit, czytany przez drawBmpFromFS())
  transparent/   - ikony PRZEZROCZYSTE (PNG alpha=0; na czarnym ekranie wygladaja tak samo)
Source: aprs.fi high-resolution symbol set by Hessu, OH7LZB
        https://github.com/hessu/aprs-symbols
License: CC BY-SA 4.0 (https://creativecommons.org/licenses/by-sa/4.0/)
Attribution required: "Ikony APRS: aprs.fi / hessu, CC BY-SA 4.0".

Nazwy plikow odpowiadaja kodom z getAPRSSymbolShort() w main.cpp:
  house /-   car />    digi /#   repeater /r   human /[   laptop /L
  wx /_      ship /s   ambul /a  bike /b       icp /c     fire /d
  firetrk /f hosp /h   yagi /y   glider /g     truck /k   van /v
  rover /p   eoc /o    trkstop /t micerpt /m    node /n    grid /q
  lora_a \a  lora_hash \#
