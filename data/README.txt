Zawartość katalogu data/ wgrywana do LittleFS:
    pio run -e esp32-s3-n16r8 -t uploadfs

Pliki (nazwy muszą się zgadzać - kod szuka dokładnie tych ścieżek):

  splash.bmp        - ekran startowy. 24-bit BMP, max 480 px szerokości,
                      wysokość <= 1000. Tu: 480x320. (dostarczony placeholder)
  mapa.bmp          - mapa świata do ekranu HamClock (140x80, 16-bit RGB565).
  Mapa swiata.bmp   - mapa do ekranu PSK (480x320, 24-bit) ORAZ tło mapy
                      satelitów w WWW (serwowana jako /mapa_swiat.bmp).
  mapa_16bit.bmp    - alternatywna mapa 16-bit 140x80 (opcjonalna).
  index.html        - pelny interfejs WWW (148 KB) ladowany z LittleFS. To on
                      jest serwowany pod "/" i "/config". Wbudowany HTML w
                      firmware (getMainHTML()) to tylko skrocony zapas na wypadek
                      braku pliku w LittleFS. UWAGA: niekompletny plik zastapi UI.
  indexEN.html      - angielska wersja interfejsu (145 KB).
  style.base.css    - styl bazowy (wymagany; bez niego strona jest "ucieta"/bez wygladu).
  style.css         - import pozostalych arkuszy (@import).
  style.desktop.css - styl dla ekranow >= 901 px.
  style.mobile.css  - styl dla ekranow <= 900 px.
  manual.txt        - instrukcja EN serwowana pod /manual.txt.
  instrukcja.txt    - instrukcja PL serwowana pod /instrukcja.txt.
  satellite.js      - moduł śledzenia satelitów (ISS) dla WWW.
  icon50/*.bmp      - ikony pogodowe 50x50 (ZALACZONE: 200,300,500,502,511,520,
                      600,611,700,800,800n,801,801n,803,wind). Brak pliku ->
                      rysowana ikona zastepcza.

  fonts/*.vlw       - fonty wektorowe TFT_eSPI (LittleFS: /fonts/*.vlw).
                      Firmware ładuje je warunkowo (LittleFS + wolna RAM):
                        /fonts/Inter-Regular10.vlw
                        /fonts/Inter-Regular12.vlw
                        /fonts/Inter-Regular20.vlw
                        /fonts/Inter-Regular24.vlw
                      Pozostałe (.vlw) są zapasowe/kolekcyjne; możesz je
                      podmienić, zachowując nazwy plików Inter-RegularNN.vlw.
                      Jeśli któregoś brakuje, firmware używa wbudowanego fontu
                      bez polskich znaków.

Wszystkie BMP muszą być 24-bit, bez kompresji (BI_RGB), wiersze bottom-up.
