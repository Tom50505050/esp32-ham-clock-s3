#include <Arduino.h>

/*
 * ESP32-HAM-CLOCK - Odbiornik DX Cluster i stacji APRS-IS z interfejsem WWW i opcjonalnym wyĹ›wietlaczem TFT
 * 
 * - WiFi Manager (AP mode jeĹ›li brak zapisanych danych)
 * - PoĹ‚Ä…czenie z Telnet DX Cluster (TYLKO ODBIERANIE - nie wysyĹ‚a spotĂłw)
 * - Parsowanie i przechowywanie spotĂłw DX
 * - Obliczanie odlegĹ‚oĹ›ci (Haversine)
 * - Interfejs WWW z polling (odĹ›wieĹĽanie co 2 sekundy)
 * - WyĹ›wietlacz TFT (ESP32-S3 N16R8) + zewnętrzny ILI9488 3.5" IPS 480x320 SPI 
 * 
 * UWAGA: UrzÄ…dzenie dziaĹ‚a TYLKO w trybie odbioru.
 * Tak samo z APRS.fi  - tylko odczyt.
*/

// Wczesne forward-deklaracje, aby auto-prototypowanie Arduino znaĹ‚o typy uĹĽywane w sygnaturach
enum ScreenType : uint8_t;    // peĹ‚na definicja niĹĽej
enum Screen6ViewMode : uint8_t;
enum TrKey : uint8_t;
struct DXSpot;                // peĹ‚na definicja niĹĽej
struct APRSStation;           // peĹ‚na definicja niĹĽej
struct AprsWxDecoded;         // peĹ‚na definicja niĹĽej
struct PropagationData;       // peĹ‚na definicja niĹĽej
struct WeatherData;           // peĹ‚na definicja niĹĽej
namespace fs { class File; }

static const uint8_t WEATHER_DETAIL_COLS = 5;

void normalizePolish(String &text);

// ========== KONFIGURACJA WYĹšWIETLACZA TFT ==========
// WĹ‚Ä…cz TFT w platformio.ini (build_flags) lub Arduino IDE; guard to avoid redefinition warnings
#ifndef ENABLE_TFT_DISPLAY
#define ENABLE_TFT_DISPLAY
#endif

// Wersja firmware
#define FIRMWARE_VERSION "1.2"

#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <esp_rom_sys.h>
#include <esp_system.h>
#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S3
  #include <soc/rtc_cntl_reg.h>
#endif
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <FS.h>
#include <LittleFS.h>
#include <math.h>
#include <ctype.h>
#include <WiFiClient.h>  

// Prawdziwa propagacja orbity ISS (TLE + SGP4) do trajektorii na radarze i
// przewidywania kolejnego przelotu. Biblioteka "Sgp4" (Hopperpop/Mitch Barry)
// - trzeba jÄ… dodaÄ‡ w platformio.ini:
//   lib_deps = https://github.com/Hopperpop/Sgp4-Library.git
// JeĹ›li nazwy metod/pĂłl poniĹĽej nie bÄ™dÄ… siÄ™ zgadzaÄ‡ z wersjÄ… biblioteki,
// ktĂłrÄ… PlatformIO faktycznie pobierze, sprawdĹş przykĹ‚adowy szkic w
// .pio/libdeps/esp32dev/Sgp4-Library/examples/ - i daj znaÄ‡ bĹ‚Ä…d kompilacji,
// dostosujÄ™ wywoĹ‚ania do dokĹ‚adnych nazw.
#include <Sgp4.h>

// Dekodery obrazu (ekran startowy): JPEG (TJpg_Decoder) + PNG (pngle).
// Wymagane w platformio.ini (lib_deps). BMP obsluguje drawBmpFromFS().
#include <TJpg_Decoder.h>
extern "C" {
  #include "pngle.h"
}
#include <esp_heap_caps.h>

static const char* tr(TrKey key);
static void buildWeatherDetailHeaders(String headers[WEATHER_DETAIL_COLS]);
static uint16_t readBmp16(fs::File &f);
static uint32_t readBmp32(fs::File &f);
String getAPRSSymbolShort(const APRSStation &station);
void drawBootScreen();
void tftBootPrintLine(const String &line);
String formatDistanceOrCountry(const DXSpot &spot, size_t maxLen);
void drawWelcomeScreenYellow();
static void uiWelcomeText(int y, const char *txt, uint8_t font, uint16_t color);
void drawSplashScreen();
void drawHamClock();
void drawHamClockClassic();
void drawHamClockIp();
void drawDxCluster();
void drawSunSpots();
void drawBandInfo();
void drawWeather();
void drawWeatherForecast();
void drawAprsIs();
void drawAprsRadar();
void drawPotaCluster();
void drawHamalertCluster();
void drawMatrixClock();
void drawQrzInfoScreen();
void handleQrzInfoTouch(uint16_t x, uint16_t y);

void drawHamburgerMenuButton3D(int x, int y);
void handleTouchCalibrationTouch(int16_t rawX, int16_t rawY, uint16_t x, uint16_t y, bool isNewTap);
static bool isAprsAlertCloseButtonHit(uint16_t x, uint16_t y);
static void dismissAprsAlertScreen();
void drawBrightnessMenu();
void handleBrightnessMenuTouch(uint16_t x, uint16_t y, bool isNewTap);
void drawCallsignColorMenu();
void handleCallsignColorMenuTouch(uint16_t x, uint16_t y, bool isNewTap);
void openCallsignColorMenu();
void drawClockColorMenu();
void handleClockColorMenuTouch(uint16_t x, uint16_t y, bool isNewTap);
void openClockColorMenu();
uint16_t hsv565(int hue, int sat, int val);
static void rgb565ToHsv(uint16_t c, int &h, int &sat, int &val);
bool isPointInRect(uint16_t x, uint16_t y, int rx, int ry, int rw, int rh);
void handleScreen1MenuTouch(uint16_t x, uint16_t y);
void handleScreen2MenuTouch(uint16_t x, uint16_t y);
void handleScreen6MenuTouch(uint16_t x, uint16_t y);
void handleScreen5MenuTouch(uint16_t x, uint16_t y);
void handleScreen7MenuTouch(uint16_t x, uint16_t y);
void handleScreen8MenuTouch(uint16_t x, uint16_t y);
void handlePskMapTouch(uint16_t x, uint16_t y);
void drawDxClusterFilterMenu();
void drawPotaFilterMenu();
void drawHamalertFilterMenu();
void drawAprsSortMenu();
void drawWeatherMenu();
void drawHamClockTimeMenu();
static bool isScreen6RadarRangeTileHit(uint16_t x, uint16_t y, int &index);
static void uiHeader(const char *title, bool showMenu);
static void uiHeaderClock();
static void uiFooter();
static void triggerScreen6RadarHint();
float calculateBearing(double lat1, double lon1, double lat2, double lon2);
float calculateDistance(double lat1, double lon1, double lat2, double lon2);

// ========== DANE PSK REPORTER ==========
struct PskSpot {
  String callsign;
  float lat;
  float lon;
  int band;       // w metrach (40, 20, 80, etc.)
  String mode;
  unsigned long timestamp;
  int snr;        // opcjonalnie
};

const int PSK_MAX_SPOTS = 50;
PskSpot pskSpots[PSK_MAX_SPOTS];
int pskSpotCount = 0;
unsigned long lastPskFetchMs = 0;
bool pskRateLimited = false;      // PSKReporter odrzucil zapytanie (limit zapytan)
unsigned long pskBackoffUntilMs = 0; // do kiedy nie pytamy API po 429/limicie
const unsigned long PSK_FETCH_INTERVAL_MS = 900000; // co 15 minut
const unsigned long PSK_RATE_LIMIT_BACKOFF_MS = 300000; // 5 min przerwy po HTTP 429

// Konfiguracja PSKReporter (zapisana w NVS)
String pskReceiverCallsign = "";  // znak odbiornika do filtrowania
int pskMaxSpots = 50;             // maksymalna liczba spotĂłw
int pskHoursWindow = 1;           // okno czasowe w godzinach
String pskFilterBand = "";        // filtr pasmo (pusty = wszystkie)
String pskFilterMode = "";        // filtr tryb (pusty = wszystkie)
String pskCustomUrl = "";         // wĹ‚asny URL API (pusty = domyĹ›lny)

// HTTP Monitoring - monitorowanie znaku
String pskMonitorCallsign = "";
int pskReportDays = 0;
int pskAutoRefreshMinutes = 5;

// MQTT PSK Reporter - alternatywny tryb
bool pskMqttEnabled = false;              // Czy uĹĽywaÄ‡ MQTT zamiast HTTP
String pskMqttServer = "mqtt.pskreporter.info";  // Serwer MQTT
int pskMqttPort = 1883;                   // Port MQTT (1883 dla plain, 8883 dla TLS)
String pskMqttCallsign = "";             // Znak do monitorowania (jeĹ›li pusty - monitoruje receiver)

WiFiClient pskMqttWifiClient;
PubSubClient pskMqttClient(pskMqttWifiClient);

// Deklaracje funkcji MQTT
void setupPskMqtt();
void loopPskMqtt();
void pskMqttCallback(char* topic, byte* payload, unsigned int length);
void reconnectPskMqtt();
void processPskMqttBuffer();

// Menu PSK Map (tymczasowe ustawienia podczas edycji na ekranie)
bool pskMapMenuOpen = false;
String pskTempReceiver = "";
String pskTempBand = "";
String pskTempMode = "";
int pskTempMaxSpots = 50;
int pskTempReportDays = 3;
bool pskTempMqttEnabled = false;
String pskTempMqttServer = "";
String pskTempMqttCallsign = "";
String pskTempLocator = "";
String pskKeyboardBuffer = "";  // Bufor dla klawiatury ekranowej
int pskKeyboardTarget = 0;        // 1=znak, 2=max, 3=mqtt_server, 4=mqtt_call, 5=lokator
enum PskMenuField { PSK_FIELD_NONE, PSK_FIELD_CALL, PSK_FIELD_BAND, PSK_FIELD_MODE, PSK_FIELD_MAXSPOTS,
                    PSK_FIELD_MQTT_MODE, PSK_FIELD_MQTT_SERVER, PSK_FIELD_MQTT_CALL, PSK_FIELD_DAYS,
                    PSK_FIELD_LOCATOR, PSK_FIELD_KEYBOARD, PSK_FIELD_BAND_SELECT, PSK_FIELD_MODE_SELECT };
PskMenuField pskActiveField = PSK_FIELD_NONE;
bool pskKeyboardActive = false;  // Czy klawiatura PSK jest widoczna (blokuje nawigacjÄ™)

// ĹšcieĹĽka do pliku BMP z mapÄ…
const char* PSK_MAP_BMP_PATH = "/Mapa swiata.bmp";

// Zakres wspĂłĹ‚rzÄ™dnych dla mapy (Plate CarrĂ©e - peĹ‚ny zakres)
const float MAP_LAT_MIN = -90.0f;  // min szerokoĹ›Ä‡ geograficzna (Antarktyda)
const float MAP_LAT_MAX = 90.0f;   // max szerokoĹ›Ä‡ geograficzna (Arktyka)
const float MAP_LON_MIN = -180.0f; // min dĹ‚ugoĹ›Ä‡ geograficzna
const float MAP_LON_MAX = 180.0f;  // max dĹ‚ugoĹ›Ä‡ geograficzna
const int MAP_DISPLAY_X = 0;       // pozycja X mapy na ekranie
const int MAP_DISPLAY_Y = 0;       // pozycja Y (0 = peĹ‚ny ekran)
const int MAP_DISPLAY_W = 480;     // szerokoĹ›Ä‡ mapy
const int MAP_DISPLAY_H = 320;     // wysokoĹ›Ä‡ mapy (480x320)

// Forward declarations for Screen Saver functions
void resetScreenSaverActivity();
void checkScreenSaverTimeout();
void drawScreenSaverMenu();
void handleScreenSaverMenuTouch(uint16_t x, uint16_t y);

// Forward declarations for Screen Sleep functions
void drawScreenSleepMenu();
void handleScreenSleepMenuTouch(uint16_t x, uint16_t y);
void enterScreenSleep();
void wakeUpFromSleep();
void applyEffectiveBacklight();
void setBacklightPercentRaw(int percent);

void savePreferences();
void locatorToLatLon(String locator, double &lat, double &lon);
String latLonToLocator(double lat, double lon, int precision = 6);
void gpsApplyConfig();
void gpsUpdate();
void gpsApplyPosition();
void gpsSnapshotManualPosition();
bool gpsModuleDetected();
bool fetchAirPollutionData(double lat, double lon);
static bool drawBmpFromFS(const String &filename, int16_t x, int16_t y);
static bool drawBmp16FromFS(const String &filename, int16_t x, int16_t y);
// Rysuje obraz z LittleFS: BMP (24/16-bit), PNG, JPG. Skaluje do 480x320.
static bool drawImageFromFS(const String &path);
static bool isSupportedImagePath(const String &path);
void sendAPRSLogin();
static String getAprsTxCallsignWithSsid();
static String getAprsBaseCallsign();
void sendAPRSFilter();
void fetchIssData();
void getIssCountryFromCoords(double lat, double lng);
String lookupIssCountryFromCoords(double lat, double lng);
bool fetchIssTLE();
void calculateIssTrajectorySGP4();
void updateIssNextPassEstimate();
bool fetchIssNextPassFromN2yo();


// ========== TYPY EKRANĂ“W (uĹĽywane w prototypach) ==========
enum ScreenType : uint8_t {
  SCREEN_OFF = 0,
  SCREEN_HAM_CLOCK = 1,
  SCREEN_DX_CLUSTER = 2,
  SCREEN_SUN_SPOTS = 3,
  SCREEN_BAND_INFO = 4,
  SCREEN_WEATHER_DSP = 5,
  SCREEN_APRS_IS = 6,
  SCREEN_POTA_CLUSTER = 7,
  SCREEN_HAMALERT_CLUSTER = 8,
  SCREEN_APRS_RADAR = 9,
  SCREEN_MATRIX_CLOCK = 10,
  SCREEN_SYSTEM_INFO = 11,
  SCREEN_WEATHER_FORECAST = 12,
  SCREEN_PSK_MAP = 13,
  SCREEN_ISS_PASS_TRACKING = 14
};

#define LOG_VERBOSE false
#define LOGV_PRINT(x) do { if (LOG_VERBOSE) Serial.print(x); } while (0)
#define LOGV_PRINTLN(x) do { if (LOG_VERBOSE) Serial.println(x); } while (0)
#define LOGV_PRINTF(...) do {} while (0)

// ========== WYĹšWIETLACZ TFT (ESP32 DevKit + zewnÄ™trzny SPI TFT) ==========
#ifdef ENABLE_TFT_DISPLAY
// WspĂłlna konfiguracja dla szkicu i kompilowanej osobno biblioteki TFT_eSPI.
// DziÄ™ki temu biblioteka nie wraca do wĹ‚asnego User_Setup.h z innym driverem/pinami.
#include "tft_setup.h"

// 1-bit landmask (bit=1 land) dla mapy swiata na ekranie ISS. Generowany
// offline z data/Mapa swiata.bmp przez tools/gen_worldmask.py - patrz komentarz
// przy issDrawWorldMap(). Wbudowany w firmware, wiec nie zalezy od LittleFS.
#include "iss_worldmask.h"

#define TFT_UI_WIDTH 480
#define TFT_UI_HEIGHT 320

#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

class HamClockTFT : public TFT_eSPI {
public:
  HamClockTFT() : TFT_eSPI() {}

  using TFT_eSPI::drawCentreString;
  using TFT_eSPI::drawString;
  using TFT_eSPI::fontHeight;
  using TFT_eSPI::pushImage;
  using TFT_eSPI::setCursor;
  using TFT_eSPI::textWidth;

  bool usesScaledUi() {
    return false;
  }

  int16_t width(void) {
    return usesScaledUi() ? logicalWidth() : TFT_eSPI::width();
  }

  int16_t height(void) {
    return usesScaledUi() ? logicalHeight() : TFT_eSPI::height();
  }

  void setCursor(int16_t x, int16_t y) {
    TFT_eSPI::setCursor(scaleX(x), scaleY(y));
  }

  void setTextSize(uint8_t size) {
    TFT_eSPI::setTextSize(scaleTextSize(size));
  }

  int16_t drawString(const String &string, int32_t x, int32_t y) {
    return unscaleX(TFT_eSPI::drawString(string, scaleX(x), scaleY(y)));
  }

  int16_t drawString(const char *string, int32_t x, int32_t y) {
    return drawString(String(string), x, y);
  }

  int16_t drawString(const String &string, int32_t x, int32_t y, uint8_t font) {
    return unscaleX(TFT_eSPI::drawString(string, scaleX(x), scaleY(y), font));
  }

  int16_t drawString(const char *string, int32_t x, int32_t y, uint8_t font) {
    return drawString(String(string), x, y, font);
  }

  int16_t drawCentreString(const String &string, int32_t x, int32_t y, uint8_t font) {
    return unscaleX(TFT_eSPI::drawCentreString(string, scaleX(x), scaleY(y), font));
  }

  int16_t drawCentreString(const char *string, int32_t x, int32_t y, uint8_t font) {
    return drawCentreString(String(string), x, y, font);
  }

  int16_t textWidth(const String &string) {
    return unscaleX(TFT_eSPI::textWidth(string));
  }

  int16_t textWidth(const char *string) {
    return textWidth(String(string));
  }

  // Szerokosc tekstu dla konkretnego fontu (1..8), niezalezna od setTextSize().
  // Uzywane przez uiHeader(), ktore wybiera font zamiast skalowac rozmiar.
  int16_t textWidthFont(const char *string, uint8_t font) {
    return unscaleX(TFT_eSPI::textWidth(string, font));
  }

  int16_t fontHeight() {
    return unscaleY(TFT_eSPI::fontHeight());
  }

  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    TFT_eSPI::fillRect(scaleX(x), scaleY(y), scaleLenX(w), scaleLenY(h), color);
  }

  void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    TFT_eSPI::drawRect(scaleX(x), scaleY(y), scaleLenX(w), scaleLenY(h), color);
  }

  void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color) {
    TFT_eSPI::drawRoundRect(scaleX(x), scaleY(y), scaleLenX(w), scaleLenY(h), scaleRadius(r), color);
  }

  void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color) {
    TFT_eSPI::fillRoundRect(scaleX(x), scaleY(y), scaleLenX(w), scaleLenY(h), scaleRadius(r), color);
  }

  void drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color) {
    TFT_eSPI::drawFastHLine(scaleX(x), scaleY(y), scaleLenX(w), color);
  }

  void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color) {
    TFT_eSPI::drawFastVLine(scaleX(x), scaleY(y), scaleLenY(h), color);
  }

  void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
    TFT_eSPI::drawLine(scaleX(x0), scaleY(y0), scaleX(x1), scaleY(y1), color);
  }

  void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color) {
    TFT_eSPI::fillTriangle(scaleX(x0), scaleY(y0), scaleX(x1), scaleY(y1), scaleX(x2), scaleY(y2), color);
  }

  void drawCircle(int32_t x, int32_t y, int32_t r, uint32_t color) {
    TFT_eSPI::drawCircle(scaleX(x), scaleY(y), scaleRadius(r), color);
  }

  void fillCircle(int32_t x, int32_t y, int32_t r, uint32_t color) {
    TFT_eSPI::fillCircle(scaleX(x), scaleY(y), scaleRadius(r), color);
  }

  void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t *data) {
    pushImageScaled(x, y, w, h, data);
  }

  void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *data) {
    pushImageScaled(x, y, w, h, data);
  }

private:
  int16_t logicalWidth() {
    return (TFT_eSPI::width() >= TFT_eSPI::height()) ? TFT_UI_WIDTH : TFT_UI_HEIGHT;
  }

  int16_t logicalHeight() {
    return (TFT_eSPI::width() >= TFT_eSPI::height()) ? TFT_UI_HEIGHT : TFT_UI_WIDTH;
  }

  int32_t scaleX(int32_t value) {
    return usesScaledUi() ? scaleValue(value, TFT_eSPI::width(), logicalWidth()) : value;
  }

  int32_t scaleY(int32_t value) {
    return usesScaledUi() ? scaleValue(value, TFT_eSPI::height(), logicalHeight()) : value;
  }

  int32_t scaleLenX(int32_t value) {
    return usesScaledUi() ? scaleSize(value, TFT_eSPI::width(), logicalWidth()) : value;
  }

  int32_t scaleLenY(int32_t value) {
    return usesScaledUi() ? scaleSize(value, TFT_eSPI::height(), logicalHeight()) : value;
  }

  int32_t scaleRadius(int32_t value) {
    if (!usesScaledUi()) {
      return value;
    }
    return min(scaleLenX(value), scaleLenY(value));
  }

  int16_t unscaleX(int16_t value) {
    return usesScaledUi() ? (int16_t)unscaleValue(value, logicalWidth(), TFT_eSPI::width()) : value;
  }

  int16_t unscaleY(int16_t value) {
    return usesScaledUi() ? (int16_t)unscaleValue(value, logicalHeight(), TFT_eSPI::height()) : value;
  }

  uint8_t scaleTextSize(uint8_t value) {
    if (!usesScaledUi() || value <= 1) {
      return value;
    }
    float scaleXf = (float)TFT_eSPI::width() / (float)logicalWidth();
    float scaleYf = (float)TFT_eSPI::height() / (float)logicalHeight();
    uint8_t scaled = (uint8_t)lroundf((float)value * min(scaleXf, scaleYf));
    return (scaled < 1) ? 1 : scaled;
  }

  static int32_t scaleValue(int32_t value, int32_t physical, int32_t logical) {
    return (int32_t)(((int64_t)value * (int64_t)physical + (logical / 2)) / (int64_t)logical);
  }

  static int32_t scaleSize(int32_t value, int32_t physical, int32_t logical) {
    if (value <= 0) {
      return 0;
    }
    int32_t scaled = scaleValue(value, physical, logical);
    return (scaled < 1) ? 1 : scaled;
  }

  static int32_t unscaleValue(int32_t value, int32_t logical, int32_t physical) {
    return (int32_t)(((int64_t)value * (int64_t)logical + (physical / 2)) / (int64_t)physical);
  }

  template <typename PixelPtr>
  void pushImageScaled(int32_t x, int32_t y, int32_t w, int32_t h, PixelPtr data) {
    const uint16_t *pixelData = data;
    if (!usesScaledUi()) {
      TFT_eSPI::pushImage(x, y, w, h, const_cast<uint16_t *>(pixelData));
      return;
    }

    int32_t scaledX = scaleX(x);
    int32_t scaledY = scaleY(y);

    // BMP-y ikon pogodowych sÄ… przesyĹ‚ane po jednym wierszu, wiÄ™c moĹĽna je
    // bezpiecznie przeskalowaÄ‡ w poziomie bez ruszania reszty kodu.
    if (h == 1 && w > 0) {
      int32_t scaledW = scaleLenX(w);
      uint16_t *scaledLine = (uint16_t *)malloc((size_t)scaledW * sizeof(uint16_t));
      if (scaledLine != nullptr) {
        for (int32_t i = 0; i < scaledW; i++) {
          int32_t srcX = (int32_t)(((int64_t)i * (int64_t)w) / (int64_t)scaledW);
          if (srcX >= w) srcX = w - 1;
          scaledLine[i] = pixelData[srcX];
        }
        TFT_eSPI::pushImage(scaledX, scaledY, scaledW, 1, scaledLine);
        free(scaledLine);
        return;
      }
    }

    TFT_eSPI::pushImage(scaledX, scaledY, w, h, const_cast<uint16_t *>(pixelData));
  }
};

HamClockTFT tft;

// ========== WSPOLNY STYL "ISS TRACKER" ==========
// Ciemne karty + akcenty teal/niebieski. Uzywane przez wszystkie ekrany, zeby
// wyglad byl spojny z ekranem ISS TRACKER (SCREEN_ISS_PASS_TRACKING).
static const uint16_t UI_COL_BG      = 0x0000; // #000000 tlo
static const uint16_t UI_COL_CARD    = 0x0884; // #0b1220 karta
static const uint16_t UI_COL_BORDER  = 0x298A; // #2a3357 obramowanie
static const uint16_t UI_COL_ACCENT  = 0x3697; // #35d0ba akcent (teal)
static const uint16_t UI_COL_ACCENT2 = 0x4D5F; // #4ea8ff akcent (niebieski)
static const uint16_t UI_COL_WARN    = 0xFE4A; // #ffc857 zloty
static const uint16_t UI_COL_TEXT    = 0xEF7E; // #e8eef7 tekst
static const uint16_t UI_COL_DIM     = 0x9D98; // #9fb0c6 przygaszony
static const uint16_t UI_COL_FAINT   = 0x6BF2; // #6c7d94 slabу
static const uint16_t UI_COL_RED     = 0xF800; // czerwony (blad)
static const uint16_t UI_COL_GREEN   = 0x07E0; // zielony (OK)
static const int UI_HEADER_H = 26;
static const int UI_FOOTER_Y = 300;

#define TFT_BL_PIN TFT_BL

#define BACKLIGHT_PWM_CHANNEL 0
#define BACKLIGHT_PWM_FREQ 5000
#define BACKLIGHT_PWM_RES_BITS 8
#define MIN_BACKLIGHT_PERCENT 10
#define TFT_BACKLIGHT 100
int backlightPercent = TFT_BACKLIGHT;
int tftNightDimmingPercent = 0;   // nocne obnizenie jasnosci (0-100%), z WWW

// ========== DOTYK (XPT2046) ==========
// Piny docelowe dla ESP32-S3 N16R8 (zewnętrzny ILI9488 + XPT2046 na wspólnym SPI).
// Można je nadpisać w tft_setup.h lub w build_flags (-DTOUCH_CS=...).
#ifndef TOUCH_CS
#define TOUCH_CS 15
#endif
#ifndef TOUCH_IRQ
#define TOUCH_IRQ 16
#endif
#ifndef TOUCH_MOSI
#define TOUCH_MOSI TFT_MOSI
#endif
#ifndef TOUCH_MISO
#define TOUCH_MISO TFT_MISO
#endif
#ifndef TOUCH_CLK
#define TOUCH_CLK TFT_SCLK
#endif

// Kalibracja dotyku (dopasuj jeĹ›li pozycje sÄ… przesuniÄ™te)
#define TOUCH_X_MIN 200
#define TOUCH_X_MAX 3800
#define TOUCH_Y_MIN 200
#define TOUCH_Y_MAX 3800
#define TOUCH_SWAP_XY false
#define TOUCH_INVERT_X false
#define TOUCH_INVERT_Y false

int touchXMin = TOUCH_X_MIN;
int touchXMax = TOUCH_X_MAX;
int touchYMin = TOUCH_Y_MIN;
int touchYMax = TOUCH_Y_MAX;
bool touchSwapXY = TOUCH_SWAP_XY;
bool touchInvertX = TOUCH_INVERT_X;
bool touchInvertY = TOUCH_INVERT_Y;
uint8_t touchRotation = 1;
uint8_t tftRotation = 1;
#ifdef TFT_INVERSION_ON
bool tftInvertColors = true;
#else
bool tftInvertColors = false;
#endif

static bool sanitizeTftInvertSetting(bool requested) {
  return requested;
}

bool tftInitialized = false;
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);
bool touchReady = false;  // true tylko po udanym touch.begin(); patrz TOUCH_ENABLED

int bootLogY = 2;
const int BOOT_LOG_LINE_HEIGHT = 10;
bool bootSequenceActive = true;
bool littleFsReady = false;
bool splashPathLoaded = false;

// ========== SYSTEM MENU I NAWIGACJI ==========
const int SCREEN_PAGE_COUNT = 14;
const ScreenType DEFAULT_SCREEN_ORDER[SCREEN_PAGE_COUNT] = {
  SCREEN_HAM_CLOCK,
  SCREEN_DX_CLUSTER,
  SCREEN_APRS_IS,
  SCREEN_APRS_RADAR,
  SCREEN_BAND_INFO,
  SCREEN_SUN_SPOTS,
  SCREEN_WEATHER_DSP,
  SCREEN_WEATHER_FORECAST,
  SCREEN_POTA_CLUSTER,
  SCREEN_HAMALERT_CLUSTER,
  SCREEN_PSK_MAP,
  SCREEN_MATRIX_CLOCK,
  SCREEN_SYSTEM_INFO,
  SCREEN_ISS_PASS_TRACKING
};

ScreenType screenOrder[SCREEN_PAGE_COUNT] = {
  SCREEN_HAM_CLOCK,
  SCREEN_DX_CLUSTER,
  SCREEN_APRS_IS,
  SCREEN_APRS_RADAR,
  SCREEN_BAND_INFO,
  SCREEN_SUN_SPOTS,
  SCREEN_WEATHER_DSP,
  SCREEN_WEATHER_FORECAST,
  SCREEN_POTA_CLUSTER,
  SCREEN_HAMALERT_CLUSTER,
  SCREEN_PSK_MAP,
  SCREEN_MATRIX_CLOCK,
  SCREEN_SYSTEM_INFO,
  SCREEN_ISS_PASS_TRACKING
};
ScreenType currentScreen = SCREEN_OFF;  // Ustawiany po wczytaniu preferencji
bool inMenu = false;    // Czy jesteĹ›my w menu wewnÄ™trznym strony
int menuOption = 0;     // Aktualna opcja w menu (jeĹ›li inMenu == true)
const int DEFAULT_TFT_SWITCH_TIME_SEC = 30;
bool tftAutoSwitchEnabled = false;
int tftAutoSwitchTimeSec = DEFAULT_TFT_SWITCH_TIME_SEC;
unsigned long tftAutoSwitchLastMs = 0;
ScreenType tftAutoSwitchLastScreen = SCREEN_OFF;
unsigned long lastScreenUpdate = 0;
const unsigned long SCREEN_UPDATE_INTERVAL = 100; // Aktualizuj ekran co 100ms
const unsigned long DX_SCREEN_MIN_REDRAW_MS = 5000; // Ogranicz zapis tabeli DX do max 1 raz/5s

// ========== WYGASZACZ EKRANU (Matrix) ==========
const int DEFAULT_SCREEN_SAVER_TIMEOUT_MIN = 5;  // DomyĹ›lny czas w minutach
bool screenSaverEnabled = false;                  // Czy wygaszacz wĹ‚Ä…czony
int screenSaverTimeoutMin = DEFAULT_SCREEN_SAVER_TIMEOUT_MIN;  // Czas w minutach
unsigned long screenSaverLastActivityMs = 0;      // Ostatnia aktywnoĹ›Ä‡
bool screenSaverActive = false;                   // Czy wygaszacz aktualnie dziaĹ‚a
ScreenType screenSaverPrevScreen = SCREEN_OFF;    // Poprzedni ekran przed wygaszaczem
bool screenSaverMenuActive = false;               // Czy jesteĹ›my w menu wygaszacza
unsigned long lastScreen1UpdateMs = 0;

// Typy wygaszacza ekranu
enum ScreenSaverType {
  SAVER_ANALOG_CLOCK = 0,   // Analogowy zegar
  SAVER_DIGITAL_CLOCK = 1,  // Cyfrowy zegar (stary bouncing)
  SAVER_MATRIX = 2          // Efekt Matrix
};
const char* SAVER_TYPE_NAMES[] = {"ANALOG", "DIGITAL", "MATRIX"};
ScreenSaverType screenSaverType = SAVER_ANALOG_CLOCK;  // DomyĹ›lnie analogowy

// ========== UĹšPIENIE EKRANU ==========
const int DEFAULT_SCREEN_SLEEP_TIMEOUT_MIN = 5;  // DomyĹ›lny czas w minutach
bool screenSleepEnabled = false;                  // Czy uĹ›pienie wĹ‚Ä…czone
int screenSleepTimeoutMin = DEFAULT_SCREEN_SLEEP_TIMEOUT_MIN;  // Czas w minutach
unsigned long screenSleepLastActivityMs = 0;      // Ostatnia aktywnoĹ›Ä‡
bool screenSleepActive = false;                   // Czy uĹ›pienie aktualnie dziaĹ‚a
bool screenSleepMenuActive = false;               // Czy jesteĹ›my w menu uĹ›pienia
ScreenType screenSleepPrevScreen = SCREEN_OFF;    // Poprzedni ekran przed uĹ›pieniem
int screenSleepMenuTimeoutMin = DEFAULT_SCREEN_SLEEP_TIMEOUT_MIN; // WartoĹ›Ä‡ w menu

// ========== QRZ POPUP ==========
bool qrzPopupActive = false;
String qrzPopupCallsign = "";
String qrzPopupGrid = "";
String qrzPopupCountry = "";
String qrzPopupName = "";
String qrzPopupQth = "";
String qrzPopupEmail = "";
float qrzPopupDistance = 0.0;
double qrzPopupLat = 0.0;
double qrzPopupLon = 0.0;
bool qrzPopupHasLatLon = false;
unsigned long qrzPopupFetchedAt = 0;
unsigned long lastScreen3UpdateMs = 0;
unsigned long lastScreen4UpdateMs = 0;
unsigned long lastScreen5UpdateMs = 0;
unsigned long lastScreen7UpdateMs = 0;
unsigned long lastScreen8UpdateMs = 0;
bool screen1HeaderNeedsRedraw = true;

const int TFT_TABLE_TOP = 32;
const int TFT_TABLE_BOTTOM = 320;
const int TFT_TABLE_WIDTH = 460;
const int TFT_TABLE_HEIGHT = TFT_TABLE_BOTTOM - TFT_TABLE_TOP;
// (ekran UnlisHunter usuniety - zastapiony ekranem "Informacje systemowe")
const unsigned long TABLE_NAV_FOOTER_VISIBLE_MS = 5000UL;
unsigned long tableNavFooterVisibleUntilMs = 0;
#ifndef TFT_TABLE_SPRITE_COLOR_DEPTH
#define TFT_TABLE_SPRITE_COLOR_DEPTH 16
#endif
const uint16_t TFT_TABLE_ALT_ROW_COLOR = 0x0841;
TFT_eSprite sharedTableSprite = TFT_eSprite(&tft);
bool sharedTableSpriteReady = false;
bool sharedTableSpriteInitTried = false;

static bool ensureSharedTableSprite() {
  if (tft.usesScaledUi()) {
    return false;
  }
  if (sharedTableSpriteReady) {
    return true;
  }
  if (sharedTableSpriteInitTried) {
    return false;
  }

  sharedTableSpriteInitTried = true;
  sharedTableSprite.setColorDepth(TFT_TABLE_SPRITE_COLOR_DEPTH);
  sharedTableSpriteReady = (sharedTableSprite.createSprite(TFT_TABLE_WIDTH, TFT_TABLE_HEIGHT) != nullptr);
  if (!sharedTableSpriteReady) {
    LOGV_PRINTLN("[TFT] Shared table sprite alloc failed, fallback to direct draw");
    return false;
  }
  return true;
}

static bool isTableFooterScreen(ScreenType screenNum) {
  return (screenNum == SCREEN_DX_CLUSTER ||
          screenNum == SCREEN_POTA_CLUSTER ||
          screenNum == SCREEN_HAMALERT_CLUSTER ||
          screenNum == SCREEN_APRS_IS ||
          screenNum == SCREEN_WEATHER_DSP ||
          screenNum == SCREEN_WEATHER_FORECAST);
}

static bool isTableNavFooterVisible(ScreenType screenNum) {
  return isTableFooterScreen(screenNum) && millis() < tableNavFooterVisibleUntilMs;
}

static int getTableMaxRowsForScreen(ScreenType screenNum) {
  return isTableNavFooterVisible(screenNum) ? 10 : 11;
}

static int getTableBottomForScreen(ScreenType screenNum) {
  return isTableNavFooterVisible(screenNum) ? TFT_TABLE_BOTTOM : 320;
}

extern uint16_t menuThemeColor;

// Deklaracje zmiennych menu (definiowane pĂłĹşniej w kodzie)
extern bool brightnessMenuActive;
extern bool screenSaverMenuActive;
extern bool screenSleepMenuActive;
extern bool touchCalActive;
extern bool pskKeyboardActive;  // Klawiatura PSK aktywna

static void drawSwitchScreenFooter() {
  uiFooter();
}

// Ustawienie trybu czasu dla ekranu 1
enum Screen1TimeMode {
  SCREEN1_TIME_UTC = 0,
  SCREEN1_TIME_LOCAL = 1
};
uint8_t screen1TimeMode = SCREEN1_TIME_UTC;
// Styl ekranu 1 (zegara): 0 = nowoczesny (domyslny), 1 = klasyczny.
// UWAGA: to jest teraz styl GLOBALNY - przycisk KLASYCZNY w menu czasu
// przelacza wszystkie ekrany (kolor znaku/tlo menu) na klasyczny.
uint8_t screen1ClockStyle = 0;
static bool uiStyleClassic() { return screen1ClockStyle == 1; }

enum TftLang : uint8_t {
  TFT_LANG_PL = 0,
  TFT_LANG_EN = 1
};
uint8_t tftLanguage = TFT_LANG_PL;

enum DxTableSizeMode : uint8_t {
  DX_TABLE_SIZE_NORMAL = 0,
  DX_TABLE_SIZE_ENLARGED = 1
};
uint8_t dxTableSizeMode = DX_TABLE_SIZE_NORMAL;

// Font z polskimi znakami dla daty/dnia tygodnia
#define ROBOTO_FONT10_NAME "/fonts/Inter-Regular10"
#define ROBOTO_FONT10_FILE "/fonts/Inter-Regular10.vlw"
#define ROBOTO_FONT12_NAME "/fonts/Inter-Regular12"
#define ROBOTO_FONT12_FILE "/fonts/Inter-Regular12.vlw"
#define ROBOTO_FONT20_NAME "/fonts/Inter-Regular20"
#define ROBOTO_FONT20_FILE "/fonts/Inter-Regular20.vlw"
#define ROBOTO_FONT24_NAME "/fonts/Inter-Regular24"
#define ROBOTO_FONT24_FILE "/fonts/Inter-Regular24.vlw"

// Wiekszy font z polskimi znakami dla ekranu powitalnego (VLW, DejaVu Sans 21 pt).
#define WELCOME_FONT_NAME "/fonts/Welcome-Regular21"
#define WELCOME_FONT_FILE "/fonts/Welcome-Regular21.vlw"

// Godlo PZK (Polski Zwiazek Krotkofalowcow) na ekranie powitalnym - romb 46x96.
#define PZK_LOGO_FILE "/pzk-logo.bmp"
#define PZK_LOGO_W 46
#define PZK_LOGO_H 96

enum TrKey : uint8_t {
  TR_TIME_SHORT = 0,
  TR_CALL_SHORT,
  TR_COUNTRY,
  TR_WAITING_SPOTS,
  TR_WEATHER,
  TR_TEMPERATURE,
  TR_HUMIDITY,
  TR_PRESSURE,
  TR_WIND,
  TR_FORECAST_3H,
  TR_FORECAST_TOMORROW,
  TR_NO_DATA,
  TR_ERROR_PREFIX,
  TR_PAGE,
  TR_TFT_CALIBRATION_HINT,
  TR_TFT_CALIBRATE_BTN,
  TR_ROT_90_RIGHT,
  TR_ROT_90_LEFT,
  TR_DISPLAY_SETTINGS,
  TR_BRIGHTNESS,
  TR_THEME_COLOR,
  TR_HOLD_CAL_HINT,
  TR_SAVE,
  TR_DEFAULT,
  TR_CLOSE,
  TR_LANGUAGE,
  TR_CALLSIGN_COLOR,
  TR_SATURATION,
  TR_VALUE,
  TR_CLOCK_COLOR,
  TR_HUE,
  TR_KEY_COUNT
};

static const char* TR_PL[TR_KEY_COUNT] = {
  "Czas",
  "Znak",
  "KRAJ",
  "Oczekiwanie na spoty...",
  "POGODA",
  "TEMPERATURA",
  "WILGOTNOĹšÄ†",
  "CIĹšNIENIE",
  "WIATR",
  "Prognoza na 3 godziny:",
  "Prognoza na jutro:",
  "Brak danych",
  "BĹÄ„D: ",
  "Strona",
  "Przytrzymaj 5 sek = Kalibracja",
  "TFT Kalibracja",
  "ObrĂłt 90deg w prawo (rot90cw)",
  "ObrĂłt 90deg w lewo (rot90ccw)",
  "USTAWIENIA TFT",
  "JASNOĹšÄ†:",
  "KOLOR MOTYWU:",
  "Przytrzymaj 3 sek = Kalibracja",
  "ZAPISZ",
  "DOMYĹšLNE",
  "ZAMKNIJ",
  "JÄZYK",
  "KOLOR ZNAKU",
  "NASYCENIE",
  "JASNOĹšÄ†",
  "KOLOR ZEGARA",
  "ODCIEĹ"
};

static const char* TR_EN[TR_KEY_COUNT] = {
  "Time",
  "Call",
  "COUNTRY",
  "Waiting for spots...",
  "WEATHER",
  "TEMPERATURE",
  "HUMIDITY",
  "PRESSURE",
  "WIND",
  "Forecast (3 hours):",
  "Forecast for tomorrow:",
  "No data",
  "ERROR: ",
  "Page",
  "Hold 5 sec anywhere = Calibration:",
  "TFT Calibrate",
  "Rotate 90 deg right (rot90cw)",
  "Rotate 90 deg left (rot90ccw)",
  "DISPLAY SETTINGS",
  "BRIGHTNESS:",
  "THEME COLOR:",
  "Hold 3 sec anywhere = Calibration",
  "SAVE",
  "DEFAULT",
  "CLOSE",
  "LANGUAGE",
  "CALLSIGN COLOR",
  "SATURATION",
  "VALUE",
  "CLOCK COLOR",
  "HUE"
};

static const char* tr(TrKey key) {
  uint8_t idx = static_cast<uint8_t>(key);
  if (idx >= TR_KEY_COUNT) {
    return "";
  }
  return (tftLanguage == TFT_LANG_EN) ? TR_EN[idx] : TR_PL[idx];
}

static const char* tftLangToCode(uint8_t lang) {
  return (lang == TFT_LANG_EN) ? "en" : "pl";
}

static uint8_t tftLangFromCode(const String &code) {
  String up = code;
  up.toLowerCase();
  if (up == "en") {
    return TFT_LANG_EN;
  }
  return TFT_LANG_PL;
}

static const char* dxTableSizeToCode(uint8_t mode) {
  return (mode == DX_TABLE_SIZE_ENLARGED) ? "enlarged" : "normal";
}

static uint8_t dxTableSizeFromCode(const String &code) {
  String up = code;
  up.toLowerCase();
  if (up == "enlarged" || up == "large" || up == "big") {
    return DX_TABLE_SIZE_ENLARGED;
  }
  return DX_TABLE_SIZE_NORMAL;
}

static bool isDxTableEnlarged() {
  return dxTableSizeMode == DX_TABLE_SIZE_ENLARGED;
}

static int getDxTableMaxRows() {
  if (!isDxTableEnlarged()) {
    return getTableMaxRowsForScreen(SCREEN_DX_CLUSTER);
  }
  return isTableNavFooterVisible(SCREEN_DX_CLUSTER) ? 6 : 7;
}

static int getPotaTableMaxRows() {
  if (!isDxTableEnlarged()) {
    return getTableMaxRowsForScreen(SCREEN_POTA_CLUSTER);
  }
  return isTableNavFooterVisible(SCREEN_POTA_CLUSTER) ? 6 : 7;
}

static int getHamalertTableMaxRows() {
  if (!isDxTableEnlarged()) {
    return getTableMaxRowsForScreen(SCREEN_HAMALERT_CLUSTER);
  }
  return isTableNavFooterVisible(SCREEN_HAMALERT_CLUSTER) ? 6 : 7;
}

static int getAprsTableMaxRows() {
  if (!isDxTableEnlarged()) {
    return getTableMaxRowsForScreen(SCREEN_APRS_IS);
  }
  return isTableNavFooterVisible(SCREEN_APRS_IS) ? 6 : 7;
}

static uint16_t getAprsCallsignColorForEnlarged(const APRSStation &station) {
  String symbolShort = getAPRSSymbolShort(station);
  if (symbolShort == "HOUSE") return TFT_YELLOW;
  if (symbolShort == "HUMAN") return TFT_BLUE;
  if (symbolShort == "CAR") return TFT_RED;
  return TFT_WHITE;
}
#endif

// Funkcja zamieniajÄ…ca polskie znaki na ASCII (dla wyĹ›wietlacza TFT)
String toAsciiPolish(const String& input) {
  String output = input;
  // MaĹ‚e litery
  output.replace("Ä…", "a");
  output.replace("Ä‡", "c");
  output.replace("Ä™", "e");
  output.replace("Ĺ‚", "l");
  output.replace("Ĺ„", "n");
  output.replace("Ăł", "o");
  output.replace("Ĺ›", "s");
  output.replace("Ĺş", "z");
  output.replace("ĹĽ", "z");
  // DuĹĽe litery
  output.replace("Ä„", "A");
  output.replace("Ä†", "C");
  output.replace("Ä", "E");
  output.replace("Ĺ", "L");
  output.replace("Ĺ", "N");
  output.replace("Ă“", "O");
  output.replace("Ĺš", "S");
  output.replace("Ĺą", "Z");
  output.replace("Ĺ»", "Z");
  return output;
}

// Kolorystyka motywu menu
#define DEFAULT_MENU_THEME_COLOR 0xFD20
#define DEFAULT_MENU_THEME_HUE 20
uint8_t menuThemeHue = DEFAULT_MENU_THEME_HUE;
uint16_t menuThemeColor = DEFAULT_MENU_THEME_COLOR;

// Filtry ekranu 2 (TFT)
enum Screen2FilterMode {
  FILTER_MODE_ALL = 0,
  FILTER_MODE_CW = 1,
  FILTER_MODE_SSB = 2,
  FILTER_MODE_DIGI = 3
};
Screen2FilterMode screen2FilterMode = FILTER_MODE_ALL;
int screen2FilterBandIndex = 0; // 0 = ALL
const char *SCREEN2_FILTER_BANDS[] = {"ALL", "240m", "80m", "40m", "20m", "17m", "15m", "12m", "10m"};
const int SCREEN2_FILTER_BANDS_COUNT = sizeof(SCREEN2_FILTER_BANDS) / sizeof(SCREEN2_FILTER_BANDS[0]);
const size_t COUNTRY_COL_MAX_LEN = 10;

// Filtry ekranu 7 (POTA)
Screen2FilterMode screen7FilterMode = FILTER_MODE_ALL;
int screen7FilterBandIndex = 0; // 0 = ALL

// Filtry ekranu 8 (HAMALERT)
Screen2FilterMode screen8FilterMode = FILTER_MODE_ALL;
int screen8FilterBandIndex = 0; // 0 = ALL
const char *SCREEN8_FILTER_BANDS[] = {"ALL", "240m", "80m", "40m", "20m", "17m", "15m", "12m", "10m", "VHF", "UHF", "SHF"};
const int SCREEN8_FILTER_BANDS_COUNT = sizeof(SCREEN8_FILTER_BANDS) / sizeof(SCREEN8_FILTER_BANDS[0]);

// Sortowanie ekranu 6 (APRS)
enum Screen6SortMode {
  APRS_SORT_TIME = 0,
  APRS_SORT_CALLSIGN = 1,
  APRS_SORT_DISTANCE = 2
};

enum Screen6ViewMode : uint8_t {
  APRS_VIEW_LIST = 0,
  APRS_VIEW_RADAR = 1
};

Screen6SortMode screen6SortMode = APRS_SORT_TIME;
Screen6ViewMode screen6ViewMode = APRS_VIEW_LIST;
bool screen6MenuBeaconingTemp = true;
bool screen6MenuAprsAlertTemp = true;
bool screen6MenuRangeAlertTemp = true;
bool screen6MenuLedAlertTemp = true;

const int SCREEN6_VIEW_BTN_ICON_X = 294;
const int SCREEN6_VIEW_BTN_ICON_Y = 7;
const int SCREEN6_VIEW_BTN_HIT_X = 276;
const int SCREEN6_VIEW_BTN_HIT_Y = 0;
const int SCREEN6_VIEW_BTN_HIT_W = 44;
const int SCREEN6_VIEW_BTN_HIT_H = 50;
const unsigned long SCREEN6_RADAR_ZOOM_TAP_COOLDOWN_MS = 220UL;
unsigned long screen6RadarLastZoomTapMs = 0;
const unsigned long SCREEN6_RADAR_HINT_DURATION_MS = 3000UL;
unsigned long screen6RadarHintUntilMs = 0;

// Zakresy radaru APRS jako MNOZNIKI promienia filtra APRS-IS (aprsFilterRadius).
// Serwer APRS-IS wysyla tylko stacje w promieniu filtra (max 50 km), wiec
// odleglosci stacji sa wzgledem niego; mnoznik tylko skaluje obraz.
const float APRS_RADAR_RANGE_MULT[] = {0.2f, 0.5f, 1.0f, 2.0f, 4.0f};
const int APRS_RADAR_RANGE_COUNT = 5;
const int APRS_RADAR_RANGE_DEFAULT = 2;  // 1.0x = pelny promien filtra
int aprsRadarRangeIndex = APRS_RADAR_RANGE_DEFAULT;

// Menu jasnosci (wywolywane dlugim przytrzymaniem)
bool brightnessMenuActive = false;
int brightnessMenuValue = 100;
ScreenType brightnessMenuPrevScreen = SCREEN_HAM_CLOCK;
bool brightnessMenuPrevInMenu = false;
uint8_t brightnessMenuPrevThemeHue = DEFAULT_MENU_THEME_HUE;
int brightnessMenuPrevBacklight = TFT_BACKLIGHT;
uint16_t brightnessMenuPrevClockColor = 0xFFE0;
unsigned long brightnessMenuOpenedMs = 0;
unsigned long brightnessMenuTouchStartMs = 0;
bool brightnessMenuLongPressHandled = false;

bool touchCalActive = false;
uint8_t touchCalStep = 0;
int16_t touchCalRawX1 = 0;
int16_t touchCalRawY1 = 0;
int16_t touchCalRawX2 = 0;
int16_t touchCalRawY2 = 0;
int16_t touchCalRawX3 = 0;
int16_t touchCalRawY3 = 0;
int16_t touchCalRawX4 = 0;
int16_t touchCalRawY4 = 0;
int touchCalNewXMin = TOUCH_X_MIN;
int touchCalNewXMax = TOUCH_X_MAX;
int touchCalNewYMin = TOUCH_Y_MIN;
int touchCalNewYMax = TOUCH_Y_MAX;

// ========== KONFIGURACJA ==========
#define AP_SSID "ESP32-HAM-CLOCK"
#define AP_PASSWORD "1234567890"
#define DEFAULT_CLUSTER_HOST "dxspots.com"
#define DEFAULT_CLUSTER_PORT 7300
#define DEFAULT_POTA_CLUSTER_HOST ""
#define DEFAULT_POTA_CLUSTER_PORT 7300
#define DEFAULT_POTA_FILTER_COMMAND "accept/spot comment POTA"
#define DEFAULT_POTA_API_URL "https://api.pota.app/v1/spots"
#define DEFAULT_HAMALERT_HOST "hamalert.org"
#define DEFAULT_HAMALERT_PORT 7300
#define NTP_SERVER "pool.ntp.org"
#define MAX_SPOTS 50  // Bufor 50 ostatnich spotĂłw
#define MAX_POTA_SPOTS 30  // Bufor 30 ostatnich spotĂłw (TFT pokaĹĽe max 10)
#define GMT_OFFSET_SEC 0  // UTC
#define DEFAULT_TIMEZONE_HOURS 1  // UTC+1 dla Polski (zimowy), DST doda +1 latem
#define DEFAULT_CALLSIGN "SWL"
#define DEFAULT_OPENWEBRX_URL "http://okno.ddns.net:8078"
#define PROPAGATION_URL "https://www.hamqsl.com/solarxml.php"
const unsigned long PROPAGATION_FETCH_INTERVAL_MS = 60UL * 60UL * 1000UL; // 60 min
const unsigned long PROPAGATION_FETCH_RETRY_MS = 5UL * 60UL * 1000UL;      // 5 min retry on error
const unsigned long WEATHER_FETCH_INTERVAL_MS = 10UL * 60UL * 1000UL;      // 10 min
const unsigned long WEATHER_FETCH_RETRY_MS = 2UL * 60UL * 1000UL;          // 2 min retry on error
const unsigned long QRZ_LOOKUP_INTERVAL_DEFAULT_MS = 6000;                 // byĹ‚o 3s - zbyt agresywne w poĹ‚Ä…czeniu z resztÄ… ruchu sieciowego na urzÄ…dzeniu
const unsigned long QRZ_LOOKUP_INTERVAL_DX_MS = 4000;                      // byĹ‚o 2s
const unsigned long QRZ_LOOKUP_INTERVAL_POTA_MS = 4000;                    // byĹ‚o 2s
const unsigned long QRZ_RETRY_DELAY_MS = 5000;                             // 5s retry
const unsigned long QRZ_CACHE_TTL_MS = 15UL * 60UL * 1000UL;               // 15 min cache wpisu
const uint8_t QRZ_RETRY_LIMIT = 2;
// Jak dlugo pamietamy "nie znaleziono" - callook.info obsluguje tylko znaki USA,
// wiec wiekszosc swiata zawsze daje 404. Bez tego ten sam znak byl pytany w kolko.
const unsigned long QRZ_NEGATIVE_TTL_MS = 60UL * 60UL * 1000UL;
const int QRZ_QUEUE_SIZE = 20;

// ========== STRUKTURA DANYCH ==========
struct DXSpot {
  String time;        // Czas UTC
  String spotter;     // Stacja zgĹ‚aszajÄ…ca
  String callsign;    // Znak wywoĹ‚awczy
  float frequency;    // CzÄ™stotliwoĹ›Ä‡ (kHz - jak w DX Cluster)
  String comment;     // Komentarz
  float distance;     // OdlegĹ‚oĹ›Ä‡ (km)
  String country;     // Kraj (z QRZ, jeĹ›li dostÄ™pny)
  String name;        // ImiÄ™ i nazwisko (z QRZ, jeĹ›li dostÄ™pne)
  String locator;     // Maidenhead Locator (jeĹ›li dostÄ™pny)
  float lat;          // Szerokosc geo (jesli znana)
  float lon;          // Dlugosc geo (jesli znana)
  bool hasLatLon;     // Czy lat/lon jest znane
  String band;        // Pasmo (240m, 80m, 40m, etc.)
  String mode;        // Modulacja (CW, SSB, FT8/FT4)
};

// Struktura dla stacji APRS
struct APRSStation {
  String time;        // Czas UTC (timestamp)
  String callsign;    // Znak wywoĹ‚awczy nadawcy
  String symbol;      // Symbol APRS (raw)
  String symbolTable; // Table symbol (znak przed /)
  float lat;          // SzerokoĹ›Ä‡ geograficzna
  float lon;          // DĹ‚ugoĹ›Ä‡ geograficzna
  String comment;     // Komentarz
  float freqMHz;      // CzÄ™stotliwoĹ›Ä‡ z komentarza (MHz)
  float distance;     // OdlegĹ‚oĹ›Ä‡ w km (Haversine)
  bool hasLatLon;     // Czy pozycja jest znana
};

struct AprsWxDecoded {
  bool hasAny = false;
  bool hasWindDir = false;
  int windDirDeg = 0;
  bool hasWindSpeed = false;
  float windSpeedKmh = 0.0f;
  bool hasTempC = false;
  float tempC = 0.0f;
  bool hasHumidity = false;
  int humidityPct = 0;
  bool hasPressure = false;
  float pressureHpa = 0.0f;

  // Starszy ekran APRS weather uĹĽywa tych nazw pĂłl bezpoĹ›rednio.
  int windDir = 0;
  float windSpeed = 0.0f;
  float temp = 0.0f;
  int humidity = 0;
  float pressure = 0.0f;
};

// ========== ZMIENNE GLOBALNE ==========
WebServer* server = nullptr;
Preferences* preferences = nullptr;
WiFiClient telnetClient;
WiFiClient potaTelnetClient;
WiFiClient aprsClient;  // Klient dla APRS-IS

struct PropagationData {
  String sfi;
  String ssn;
  String kindex;
  String aindex;
  String xray;
  String muf;
  String updated;
  String hfBandLabel[4];
  String hfBandFreq[4];
  String hfBandDay[4];
  String hfBandNight[4];
  bool valid = false;
  String lastError = "";
  unsigned long fetchedAtMs = 0;
};

PropagationData propagationData;
unsigned long lastPropagationFetchMs = 0;
bool lastPropagationFetchOk = true;

struct WeatherData {
  static const uint8_t DETAIL_COLS = WEATHER_DETAIL_COLS;
  String cityName;
  String description;
  String iconCode;  // OWM icon code (e.g., 01d/01n) to detect day/night
  int weatherId = 800; // OpenWeatherMap condition ID (default clear sky)
  float tempC = 0.0f;
  int humidity = 0;
  int pressure = 0;
  float windMs = 0.0f;
  float pm25 = 0.0f;  // PM2.5 w Ă‚Âµg/mĂ‚Ĺ‚
  float pm10 = 0.0f;  // PM10 w Ă‚Âµg/mĂ‚Ĺ‚
  // Prognozy
  float forecast3hTempC = 0.0f;
  float forecast3hWindMs = 0.0f;
  String forecast3hDesc;
  bool forecast3hValid = false;
  float forecastNextDayTempC = 0.0f;
  float forecastNextDayWindMs = 0.0f;
  String forecastNextDayDesc;
  bool forecastNextDayValid = false;
  float detailTempC[DETAIL_COLS] = {0, 0, 0, 0, 0};
  int detailHumidity[DETAIL_COLS] = {0, 0, 0, 0, 0};
  float detailWindMs[DETAIL_COLS] = {0, 0, 0, 0, 0};
  int detailWeatherId[DETAIL_COLS] = {800, 800, 800, 800, 800};
  String detailIconCode[DETAIL_COLS];
  bool detailValid[DETAIL_COLS] = {false, false, false, false, false};
  float nightTempC[2] = {0.0f, 0.0f};
  bool nightTempValid[2] = {false, false};
  String updated;
  bool valid = false;
  String lastError = "";
  unsigned long fetchedAtMs = 0;
};

WeatherData weatherData;
unsigned long lastWeatherFetchMs = 0;
bool lastWeatherFetchOk = true;

struct PendingQrzLookup {
  String callsign;
  unsigned long nextTryMs = 0;
  uint8_t attempts = 0;
};

PendingQrzLookup qrzQueue[QRZ_QUEUE_SIZE];
int qrzQueueLen = 0;
unsigned long lastQrzLookupMs = 0;

// Zwraca interwaĹ‚ dla kolejki QRZ zaleĹĽnie od aktywnego ekranu
unsigned long getQrzLookupIntervalMs() {
#ifdef ENABLE_TFT_DISPLAY
  if (tftInitialized && !inMenu) {
    if (currentScreen == SCREEN_DX_CLUSTER) {
      return QRZ_LOOKUP_INTERVAL_DX_MS;
    }
    if (currentScreen == SCREEN_POTA_CLUSTER) {
      return QRZ_LOOKUP_INTERVAL_POTA_MS;
    }
  }
#endif
  return QRZ_LOOKUP_INTERVAL_DEFAULT_MS;
}

DXSpot spots[MAX_SPOTS];
int spotCount = 0;
DXSpot potaSpots[MAX_POTA_SPOTS];
int potaSpotCount = 0;
DXSpot hamalertSpots[MAX_POTA_SPOTS];
int hamalertSpotCount = 0;
SemaphoreHandle_t dxSpotsMutex = nullptr;

const size_t DX_TIME_MAX_LEN = 24;
const size_t DX_CALLSIGN_MAX_LEN = 16;
const size_t DX_SPOTTER_MAX_LEN = 16;
const size_t DX_COMMENT_MAX_LEN = 96;
const size_t DX_COUNTRY_MAX_LEN = 28;
const size_t DX_NAME_MAX_LEN = 32;
const size_t DX_LOCATOR_MAX_LEN = 8;
const size_t DX_BAND_MAX_LEN = 12;
const size_t DX_MODE_MAX_LEN = 8;

const size_t APRS_TIME_MAX_LEN = 24;
const size_t APRS_CALLSIGN_MAX_LEN = 16;
const size_t APRS_SYMBOL_MAX_LEN = 12;
const size_t APRS_COMMENT_MAX_LEN = 96;

static inline void clampStringLength(String &value, size_t maxLen) {
  if (value.length() > maxLen) {
    value.remove(maxLen);
  }
}

static void compactDxSpotStrings(DXSpot &spot) {
  clampStringLength(spot.time, DX_TIME_MAX_LEN);
  clampStringLength(spot.callsign, DX_CALLSIGN_MAX_LEN);
  clampStringLength(spot.spotter, DX_SPOTTER_MAX_LEN);
  clampStringLength(spot.comment, DX_COMMENT_MAX_LEN);
  clampStringLength(spot.country, DX_COUNTRY_MAX_LEN);
  clampStringLength(spot.name, DX_NAME_MAX_LEN);
  clampStringLength(spot.locator, DX_LOCATOR_MAX_LEN);
  clampStringLength(spot.band, DX_BAND_MAX_LEN);
  clampStringLength(spot.mode, DX_MODE_MAX_LEN);
}

static void compactAprsStationStrings(APRSStation &station) {
  clampStringLength(station.time, APRS_TIME_MAX_LEN);
  clampStringLength(station.callsign, APRS_CALLSIGN_MAX_LEN);
  clampStringLength(station.symbol, APRS_SYMBOL_MAX_LEN);
  clampStringLength(station.symbolTable, APRS_SYMBOL_MAX_LEN);
  clampStringLength(station.comment, APRS_COMMENT_MAX_LEN);
}

static inline void lockDxSpots() {
  if (dxSpotsMutex != nullptr) {
    xSemaphoreTake(dxSpotsMutex, portMAX_DELAY);
  }
}

static inline void unlockDxSpots() {
  if (dxSpotsMutex != nullptr) {
    xSemaphoreGive(dxSpotsMutex);
  }
}

// Forward declarations for timezone conversion
extern int timezoneHours;
bool isEuropeanDST(struct tm *timeinfo);

// Global cache for timezone calculation to avoid repeated time() calls
static int cachedTimezoneOffset = -999;  // -999 means not initialized
static unsigned long lastTimezoneCacheMs = 0;
static const unsigned long TIMEZONE_CACHE_MS = 60000; // Cache for 1 minute

static int getCachedTimezoneOffset() {
  unsigned long nowMs = millis();
  if (cachedTimezoneOffset == -999 || (nowMs - lastTimezoneCacheMs) > TIMEZONE_CACHE_MS) {
    int dstOffset = 0;
    time_t now = time(nullptr);
    if (now > 100000) {
      struct tm utcTm;
      gmtime_r(&now, &utcTm);
      dstOffset = isEuropeanDST(&utcTm) ? 1 : 0;
    }
    cachedTimezoneOffset = timezoneHours + dstOffset;
    lastTimezoneCacheMs = nowMs;
  }
  return cachedTimezoneOffset;
}

String formatSpotUtc(String raw) {
  raw.trim();

  // ObsĹ‚uga ISO 8601 (np. 2024-12-12T12:34Z)
  int tPos = raw.indexOf('T');
  if (tPos >= 0 && (tPos + 5) <= (int)raw.length()) {
    raw = raw.substring(tPos + 1);
  }

  // UsuĹ„ ewentualne koĹ„cowe "Z"
  if (raw.endsWith("Z") || raw.endsWith("z")) {
    raw.remove(raw.length() - 1);
  }

  int hour = 0, minute = 0;
  bool parsed = false;
  
  // JeĹ›li juĹĽ jest dwukropek, przytnij do HH:MM
  if (raw.length() >= 5 && raw.charAt(2) == ':') {
    hour = raw.substring(0, 2).toInt();
    minute = raw.substring(3, 5).toInt();
    parsed = true;
  } else if (raw.length() >= 4) {
    // Brak dwukropka: sprĂłbuj wstawiÄ‡ miÄ™dzy HH a MM (np. "1234" -> "12:34")
    hour = raw.substring(0, 2).toInt();
    minute = raw.substring(2, 4).toInt();
    parsed = true;
  } else if (raw.length() >= 2) {
    // Awaryjnie: tylko godzina
    hour = raw.substring(0, 2).toInt();
    minute = 0;
    parsed = true;
  }
  
  if (!parsed) {
    return raw; // Nie udaĹ‚o siÄ™ sparsowaÄ‡
  }

  // Konwersja na czas lokalny (uĹĽyj cache'owanego offsetu)
  int offset = getCachedTimezoneOffset();
  int localHour = hour + offset;
  
  // ObsĹ‚uga przejĹ›cia przez pĂłĹ‚noc
  if (localHour < 0) localHour += 24;
  else if (localHour >= 24) localHour -= 24;
  
  // Formatuj wynik uĹĽywajÄ…c statycznego bufora (bez alokacji String)
  char result[6];
  snprintf(result, sizeof(result), "%02d:%02d", localHour, minute);
  return String(result);
}

// POTA API (HTTP)
const unsigned long POTA_API_FETCH_INTERVAL_MS = 180UL * 1000UL; // 180s
unsigned long lastPotaApiFetchMs = 0;

// HAMALERT Telnet
const unsigned long HAMALERT_FETCH_INTERVAL_MS = 60UL * 1000UL; // 60s
unsigned long lastHamalertFetchMs = 0;

// APRS-IS konfiguracja (domyĹ›lne wartoĹ›ci)
#define DEFAULT_APRS_IS_HOST "rotate.aprs2.net"
#define DEFAULT_APRS_IS_PORT 14580
#define DEFAULT_APRS_CALLSIGN "nocall"
#define DEFAULT_APRS_PASSCODE 00000
#define DEFAULT_APRS_SSID 0
#define DEFAULT_APRS_FILTER_RADIUS 50  // PromieĹ„ w km (domyĹ›lnie 50, zakres 1-50)
#define MAX_APRS_STATIONS 20  // Maksymalna liczba stacji do wyĹ›wietlenia (bufor dla WWW)
#define MAX_APRS_DISPLAY_LCD 11  // Maksymalna liczba stacji na ekranie LCD

// Zmienne konfiguracyjne APRS-IS
String aprsIsHost = DEFAULT_APRS_IS_HOST;
int aprsIsPort = DEFAULT_APRS_IS_PORT;
String aprsCallsign = DEFAULT_APRS_CALLSIGN;
int aprsPasscode = DEFAULT_APRS_PASSCODE;
int aprsSsid = DEFAULT_APRS_SSID;
int aprsFilterRadius = DEFAULT_APRS_FILTER_RADIUS;  // PromieĂ„Ä…Ă˘â‚¬Ĺľ w km (0-30)
// Uwaga: APRS uĂ„Ä…Ă„Ëťywa wspĂ„â€šĹ‚Ă„Ä…Ă˘â‚¬ĹˇrzÄ‚â€žĂ˘â€žËdnych z sekcji "Moja Stacja" (userLat, userLon)


AprsWxDecoded currentWX;
String lastWxStation = "N/A";
const unsigned long APRS_POSITION_FIRST_DELAY_MS = 60UL * 1000UL; // pierwszy beacon po 1 minucie
const char* aprsServer = "rotate.aprs2.net";
const int aprsPort = 14580;
const char* aprsPass = "12345"; // TWĂ“J PASSCODE
const char* aprsFilter = "t/m/t/w"; // Filtruj tylko wiadomoĹ›ci i stacje pogodowe (WX)
const int DEFAULT_APRS_INTERVAL_MIN = 29; // kolejne co 29 minut (domyĹ›lnie)
const char *NVS_KEY_APRS_INTERVAL_MIN = "aprs_int_min";
const int DEFAULT_APRS_ALERT_MIN_SEC = 300;
const char *NVS_KEY_APRS_ALERT_MIN_SEC = "aprs_alrt_sec";
const int DEFAULT_APRS_ALERT_SCREEN_SEC = 5;
const char *NVS_KEY_APRS_ALERT_SCREEN_SEC = "alrt_scr_s";
const float DEFAULT_APRS_ALERT_DISTANCE_KM = 1.0f;
const char *NVS_KEY_APRS_ALERT_DISTANCE_KM = "alrt_dst_km";
const char *NVS_KEY_APRS_ALERT_WX_ENABLED = "alrt_wx_en";
const bool DEFAULT_ENABLE_LED_ALERT = true;
const int DEFAULT_LED_ALERT_DURATION_MS = 5000;
const int DEFAULT_LED_ALERT_BLINK_MS = 500;
const char *NVS_KEY_ENABLE_LED_ALERT = "led_al_en";
const char *NVS_KEY_LED_ALERT_DURATION_MS = "led_al_dur";
const char *NVS_KEY_LED_ALERT_BLINK_MS = "led_al_blk";
unsigned long aprsPositionIntervalMs = (unsigned long)DEFAULT_APRS_INTERVAL_MIN * 60UL * 1000UL;
unsigned long lastAPRSPositionTxMs = 0;
unsigned long nextAPRSPositionDueMs = 0;
int aprsIntervalMinutes = DEFAULT_APRS_INTERVAL_MIN;
const char DEFAULT_APRS_SYMBOL_TABLE = '/';
const char DEFAULT_APRS_SYMBOL_CODE = '-';
String aprsSymbolTwoChar = "/-";
char aprsSymbolTable = DEFAULT_APRS_SYMBOL_TABLE;
char aprsSymbolCode = DEFAULT_APRS_SYMBOL_CODE;
String aprsUserComment = "";
String aprsAlertCsv = "";
bool aprsAlertEnabled = true;
int aprsAlertMinSeconds = DEFAULT_APRS_ALERT_MIN_SEC;
int aprsAlertScreenSeconds = DEFAULT_APRS_ALERT_SCREEN_SEC;
bool aprsAlertNearbyEnabled = true;
bool aprsAlertWxEnabled = false;
float aprsAlertDistanceKm = DEFAULT_APRS_ALERT_DISTANCE_KM;
bool enableLedAlert = DEFAULT_ENABLE_LED_ALERT;
int ledAlertDurationMs = DEFAULT_LED_ALERT_DURATION_MS;
int ledAlertBlinkMs = DEFAULT_LED_ALERT_BLINK_MS;
int aprsBeaconTxCount = 0;
const char *APRS_POSITION_COMMENT = "https://github.com/Tom50505050/esp32-ham-clock";
bool aprsBeaconEnabled = true;
const unsigned long APRS_ALERT_FRAME_PULSE_MS = 500UL;
const int APRS_ALERT_CLOSE_BTN_X = 449;
const int APRS_ALERT_CLOSE_BTN_Y = 7;
const int APRS_ALERT_CLOSE_BTN_W = 26;
const int APRS_ALERT_CLOSE_BTN_H = 26;
const int APRS_ALERT_CLOSE_HIT_W = 50;
const int APRS_ALERT_CLOSE_HIT_H = 50;
bool aprsAlertScreenActive = false;
unsigned long aprsAlertScreenUntilMs = 0;
unsigned long aprsAlertFrameLastToggleMs = 0;
bool aprsAlertFramePulseOn = true;
APRSStation aprsAlertScreenStation;
volatile bool aprsAlertDrawPending = false;
APRSStation aprsAlertPendingStation;
portMUX_TYPE aprsAlertPendingMux = portMUX_INITIALIZER_UNLOCKED;

struct AprsAlertCooldownEntry {
  String callsign;
  unsigned long lastAlertMs = 0;
};

AprsAlertCooldownEntry aprsAlertCooldown[MAX_APRS_STATIONS];
int aprsAlertCooldownReplaceIdx = 0;

APRSStation aprsStations[MAX_APRS_STATIONS];
int aprsStationCount = 0;
bool aprsConnected = false;
bool aprsLoginSent = false;
unsigned long lastAPRSAttempt = 0;
unsigned long lastAPRSRxMs = 0;
String aprsBuffer = "";
const unsigned long APRS_INACTIVITY_RECONNECT_MS = 5UL * 60UL * 1000UL; // 5 minut

void parseAPRSWeather(String line) {
  // Szukamy poczÄ…tku danych pogodowych (znak '_')
  int wxIdx = line.indexOf('_');
  if (wxIdx == -1) return;

  // Pobranie znaku stacji (na poczÄ…tku linii przed '>')
  int callEnd = line.indexOf('>');
  if (callEnd != -1) {
    lastWxStation = line.substring(0, callEnd);
  }

  // Wycinanie danych (format APRS jest staĹ‚y po znaku '_')
  // PrzykĹ‚ad: _MMDDHHmmcCCsSSgGGtTTThHHbBBBBB
  String data = line.substring(wxIdx + 1);

  // Kierunek i prÄ™dkoĹ›Ä‡ wiatru (c...s...)
  if (data.indexOf('c') != -1 && data.indexOf('s') != -1) {
    currentWX.windDir = data.substring(data.indexOf('c')+1, data.indexOf('c')+4).toInt();
    int miph = data.substring(data.indexOf('s')+1, data.indexOf('s')+4).toInt();
    currentWX.windSpeed = miph * 0.44704; // mph -> m/s
  }

  // Temperatura (t...) - w Fahrenheitach
  int tIdx = data.indexOf('t');
  if (tIdx != -1) {
    int tempF = data.substring(tIdx + 1, tIdx + 4).toInt();
    currentWX.temp = (tempF - 32) * 5.0 / 9.0; // F -> C
  }

  // WilgotnoĹ›Ä‡ (h...)
  int hIdx = data.indexOf('h');
  if (hIdx != -1) {
    currentWX.humidity = data.substring(hIdx + 1, hIdx + 3).toInt();
  }

  // CiĹ›nienie (b...) - w dziesiÄ…tych hPa
  int bIdx = data.indexOf('b');
  if (bIdx != -1) {
    float baro = data.substring(bIdx + 1, bIdx + 6).toFloat();
    currentWX.pressure = baro / 10.0;
  }
  
  Serial.printf("WX od %s: %.1fC, %d%%, %.1f hPa\n", lastWxStation.c_str(), currentWX.temp, currentWX.humidity, currentWX.pressure);
}

void updateAPRS() {
  if (!aprsClient.connected()) {
    if (aprsClient.connect(aprsServer, aprsPort)) {
      String loginCall = aprsCallsign.length() ? aprsCallsign : String(DEFAULT_APRS_CALLSIGN);
      String login = "user " + loginCall + " pass " + String(aprsPasscode) + " vers ESP32HamClock 1.0 filter " + aprsFilter;
      aprsClient.println(login);
    }
    return;
  }

  while (aprsClient.available()) {
    String line = aprsClient.readStringUntil('\n');
    if (line.indexOf("_") != -1 && line.indexOf("t") != -1) {
      parseAPRSWeather(line);
      if (currentScreen == SCREEN_WEATHER_DSP) lastScreenUpdate = 0;
    }
  }
}
void drawWeatherScreen() {
  tft.fillRect(0, 0, 480, 40, 0x03E0); // Ciemnozielony nagĹ‚Ăłwek
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.drawCentreString("APRS WEATHER STATION", 240, 10, 1);

  // 1. Temperatura (DuĹĽy odczyt)
  tft.setTextSize(4);
  tft.setTextColor(currentWX.temp > 0 ? TFT_ORANGE : TFT_CYAN);
  String tStr = String(currentWX.temp, 1) + " C";
  tft.drawCentreString(tStr, 240, 70, 1);

  // 2. PozostaĹ‚e parametry (Siatka 2x2)
  tft.setTextSize(2);
  tft.setTextColor(TFT_YELLOW);
  
  // WilgotnoĹ›Ä‡
  tft.drawString("HUMIDITY:", 50, 240);
  tft.setTextColor(TFT_WHITE);
  tft.drawString(String(currentWX.humidity) + " %", 200, 240);

  // CiĹ›nienie
  tft.setTextColor(TFT_YELLOW);
  tft.drawString("BARO:", 280, 240);
  tft.setTextColor(TFT_WHITE);
  tft.drawString(String(currentWX.pressure, 1) + " hPa", 380, 240);

  // Wiatr
  tft.setTextColor(TFT_YELLOW);
  tft.drawString("WIND:", 50, 210);
  tft.setTextColor(TFT_WHITE);
  String wStr = String(currentWX.windSpeed, 1) + " m/s (" + String(currentWX.windDir) + "*)";
  tft.drawString(wStr, 200, 210);

  // 3. Info o stacji
  tft.drawFastHLine(20, 260, 440, TFT_DARKGREY);
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY);
  tft.drawCentreString("Last station: " + lastWxStation, 240, 280, 1);
}

// Konfiguracja WiFi i Cluster
String wifiSSID = "";
String wifiPassword = "";
String wifiSSID2 = "";
String wifiPassword2 = "";
String clusterHost = DEFAULT_CLUSTER_HOST;
int clusterPort = DEFAULT_CLUSTER_PORT;
String potaClusterHost = DEFAULT_POTA_CLUSTER_HOST;
int potaClusterPort = DEFAULT_POTA_CLUSTER_PORT;
String potaFilterCommand = DEFAULT_POTA_FILTER_COMMAND;
String potaApiUrl = DEFAULT_POTA_API_URL;
String hamalertHost = DEFAULT_HAMALERT_HOST;
int hamalertPort = DEFAULT_HAMALERT_PORT;
String hamalertLogin = "";
String hamalertPassword = "";
String userCallsign = "";
String userLocator = "";
double userLat = 0.0;
double userLon = 0.0;
bool userLatLonValid = false;

// ========== GPS / GNSS (NMEA 0183 po UART2) ==========
// Modul GPS podlacza sie do wolnych GPIO (domyslnie RX=18 <- TX modulu,
// TX=17 -> RX modulu). Lokator Maidenhead liczymy automatycznie z pozycji.
bool gpsEnabled = false;          // modul GPS wlaczony (NVS gps_en)
int  gpsRxPin = 18;               // ESP32 RX  <- TX modulu GPS
int  gpsTxPin = 17;               // ESP32 TX  -> RX modulu GPS
int  gpsBaud = 9600;              // predkosc NMEA
bool gpsUseForPosition = false;   // uzywaj pozycji GPS zamiast recznej (NVS gps_use)
bool gpsFix = false;              // GGA quality > 0
bool gpsHasPosition = false;      // odebrano poprawna pozycje
double gpsLat = 0.0, gpsLon = 0.0;
float gpsAltM = 0.0f, gpsSpeedKmh = 0.0f, gpsHdop = 99.9f;
int gpsSats = 0;
String gpsUtcTime = "";           // "hhmmss.sss"
String gpsDateStr = "";           // "ddmmyy"
String gpsLocator = "";
unsigned long gpsLastFixMs = 0;       // ostatnia poprawna pozycja
unsigned long gpsLastSentenceMs = 0;  // ostatnia dowolna ramka NMEA (detekcja modulu)
uint32_t gpsSentenceCount = 0;
HardwareSerial gpsSerial(2);
bool gpsSerialBegun = false;
int gpsLastBaudApplied = -1, gpsLastRxApplied = -1, gpsLastTxApplied = -1;
// Reczne (z WWW) wartosci pozycji. Gdy "uzywaj GPS" jest wlaczone i jest FIX,
// userLat/userLon/userLocator sa nadpisywane pozycja GPS dla wszystkich
// odbiorcow (APRS, PSK, radar). Po wylaczeniu GPS wracamy do tych wartosci.
double userLatManual = 0.0, userLonManual = 0.0;
String userLocatorManual = "";
bool userLatLonValidManual = false;
bool gpsPositionOverrideActive = false;

int timezoneHours = DEFAULT_TIMEZONE_HOURS;
String qrzUsername = "";
String qrzPassword = "";
String qrzStatus = "Callook.info: ready";
String weatherApiKey = "";
// Klucz API n2yo.com (opcjonalny) - jeĹ›li ustawiony, "nastÄ™pny przelot ISS"
// jest pobierany z ich gotowego API do przewidywania przelotĂłw zamiast
// liczony lokalnie przez SGP4 (duĹĽo prostsze i bardziej niezawodne niĹĽ
// wĹ‚asne skanowanie orbity na ESP32). Rejestracja/klucz: https://www.n2yo.com/api/
String n2yoApiKey = "";
String openWebRxUrl = DEFAULT_OPENWEBRX_URL;
uint16_t callsignColor = 65535; // Default white (0xFFFF)
// Kolor cyfr zegara (duzy zegar cyfrowy na ekranie HAM CLOCK, styl nowoczesny).
// Domyslnie zolty (HC_YELLOW) - jak wczesniej. Zmiana: menu jasnosci (suwak
// "KOLOR ZEGARA"), WWW (Twoja Stacja) albo dlugie przytrzymanie zegara.
uint16_t clockDigitsColor = 0xFFE0; // Default yellow (0xFFE0)
bool clockColorMenuActive = false;
int clockColorMenuHue = 0;    // 0..255
int clockColorMenuSat = 100;  // 0..100 (%)
int clockColorMenuVal = 100;  // 0..100 (%)
uint16_t clockColorMenuPrev = 0xFFE0;
ScreenType clockColorMenuPrevScreen = SCREEN_HAM_CLOCK;
bool clockColorMenuPrevInMenu = false;
unsigned long clockColorMenuOpenedMs = 0;
// Menu koloru znaku (przytrzymanie palca na znaku na ekranie HAM CLOCK).
bool callsignColorMenuActive = false;
int callsignColorMenuHue = 0;    // 0..255
int callsignColorMenuSat = 100;  // 0..100 (%) - domyslnie pelne nasycenie
int callsignColorMenuVal = 100;  // 0..100 (%)
uint16_t callsignColorMenuPrev = 65535;
ScreenType callsignColorMenuPrevScreen = SCREEN_HAM_CLOCK;
bool callsignColorMenuPrevInMenu = false;
unsigned long callsignColorMenuOpenedMs = 0;
// Obszar znaku na ekranie HAM CLOCK (do wykrycia przytrzymania palca).
int callsignHitX = 40, callsignHitY = 45, callsignHitW = 400, callsignHitH = 55;
// Obszar duzego zegara na ekranie HAM CLOCK (do wykrycia przytrzymania palca).
int clockHitX = 40, clockHitY = 125, clockHitW = 400, clockHitH = 70;

// ========== ISS TRACKING VARIABLES ==========
#define ISS_TRAJECTORY_POINTS 45
double issLat = 0.0;
double issLng = 0.0;
double issAltitude = 0.0;
double issSpeed = 0.0;
String issCountry = "CZEKAJ...";
String issCountryCode = "";
double issDistance = 0.0;
double issAzimuth = 0.0;
double issElevation = 0.0;
int issTrajectoryX[ISS_TRAJECTORY_POINTS] = {0};
int issTrajectoryY[ISS_TRAJECTORY_POINTS] = {0};
int issTrajectoryIndex = 0;
int lastIssSatX = -1;
int lastIssSatY = -1;
unsigned long lastIssDataFetch = 0;
unsigned long lastIssCountryCheck = 0;
const int issCx = 240;  // Center X for ISS radar
const int issCy = 160;  // Center Y for ISS radar
double issTrajectoryLat[ISS_TRAJECTORY_POINTS] = {0};
double issTrajectoryLng[ISS_TRAJECTORY_POINTS] = {0};
const int issRMax = 120; // Max radius for ISS radar (5000 km scale)

// Kierunek lotu (namiar) wyliczony z dwĂłch kolejnych PRAWDZIWYCH odczytĂłw
// pozycji ISS - uĹĽywany do sensownej ekstrapolacji trajektorii w
// calculateIssTrajectory(), zamiast zgadywanej, staĹ‚ej prÄ™dkoĹ›ci kÄ…towej.
double issGroundBearingDeg = 0.0;
bool issGroundBearingValid = false;
double issTrajectoryEpoch = 0.0;      // czas (epoch) pierwszego punktu trajektorii
double issTrajectoryStepSec = 60.0;   // odstep czasu miedzy punktami [s]

// Szacowany czas nastÄ™pnego przelotu (epoch, sekundy) - patrz updateIssPassEstimate().
// To jest PRZYBLIĹ»ENIE oparte na okresie orbitalnym ISS (~92.68 min), nie
// peĹ‚ne wyznaczenie orbity (TLE/SGP4), wiÄ™c moĹĽe siÄ™ rĂłĹĽniÄ‡ od rzeczywistego
// czasu o kilka-kilkanaĹ›cie minut.
time_t issNextPassEstimateEpoch = 0;
bool issNextPassEstimateValid = false;
// Rozszerzone dane o przelocie (AOS = wejscie, LOS = zejscie, max elewacja).
// Gdy ISS jest AKTUALNIE widoczny pokazujemy biezacy przelot: AOS jest w
// przeszlosci, wiec UI wyswietla "WIDOCZNY DO <LOS>". Gdy nie widoczny -
// najblizszy przyszly AOS.
time_t issNextPassAosEpoch = 0;
time_t issNextPassLosEpoch = 0;
double issNextPassMaxEl = 0.0;
bool issNextPassWasVisible = false;
bool issWasInRangeLastCheck = false;

// Lista kilku najbliszych przyszlych przelotow (AOS/LOS/maxEl), wypelniana
// razem z pojedynczym "nastepnym przelotem" powyzej. Pozwala UI pokazac
// plan przelotow, a nie tylko jeden wpis - i daje czytelny dowod, ze odczyt
// dziala (widac kolejne godziny), nawet jesli pierwszy AOS jest odlegly.
#define ISS_MAX_PASSES 4
int issPassCount = 0;
// Wymuszone odswiezenie planu przelotow (wejscie na ekran ISS / swieze TLE).
volatile bool issForceNextPassRefresh = false;
time_t issPassAos[ISS_MAX_PASSES] = {0};
time_t issPassLos[ISS_MAX_PASSES] = {0};
double issPassMaxEl[ISS_MAX_PASSES] = {0.0};

// Widocznosc przelotu (oswietlenie + obserwator w ciemnosci) z n2yo
// visualpasses. Dla kazdego przelotu: start/koniec widocznosci (epoch UTC),
// czas trwania [s] i maks. jasnosc. 0 = brak danych (np. brak klucza n2yo).
time_t issPassVisStart[ISS_MAX_PASSES] = {0};
time_t issPassVisEnd[ISS_MAX_PASSES] = {0};
int    issPassDuration[ISS_MAX_PASSES] = {0};
double issPassMag[ISS_MAX_PASSES] = {0.0};

// FreeRTOS task for ISS updates on Core 0
TaskHandle_t issTaskHandle = NULL;
SemaphoreHandle_t issDataMutex = NULL;

// ===== TLE / SGP4 (prawdziwa mechanika orbitalna) =====
// UĹĽywane do: (1) trajektorii ISS rysowanej na radarze, (2) przewidywania
// czasu nastÄ™pnego przelotu. To ZASTÄPUJE poprzednie przybliĹĽenia
// (namiar+prÄ™dkoĹ›Ä‡, model sinusoidalny) prawdziwÄ… propagacjÄ… orbity.
Sgp4 issSat;
bool issTleValid = false;
unsigned long lastIssTleFetch = 0;
const unsigned long ISS_TLE_REFRESH_MS = 6UL * 3600UL * 1000UL; // co 6h
char issTleLine1[130] = {0};
char issTleLine2[130] = {0};
char issTleName[26] = "ISS (ZARYA)";
SemaphoreHandle_t issTleMutex = NULL; // chroni issSat/issTleValid (Sgp4 nie jest thread-safe)

// ISS task function - runs on Core 0
void issTask(void *pvParameters) {
  Serial.println("[ISS TASK] Started on Core 0");

  // Poczekaj, zanim ISS zacznie cokolwiek pobieraÄ‡ z sieci - inne usĹ‚ugi
  // uruchamiane przy starcie (DX Cluster, HamAlert, APRS-IS, POTA, pogoda)
  // i tak potrzebujÄ… WiFi/gniazd/TLS w pierwszych sekundach po starcie;
  // ISS dokĹ‚adajÄ…c siÄ™ do tego akurat w tym najbardziej zatĹ‚oczonym
  // momencie zwiÄ™kszaĹ‚ ryzyko problemĂłw z pamiÄ™ciÄ…/gniazdami. ISS nie jest
  // pilny (przelot i tak trwa kilka minut), wiÄ™c niech pobiera dane jako
  // ostatni, gdy reszta juĹĽ siÄ™ ustatkuje.
  Serial.println("[ISS TASK] Czekam 90s zanim zaczne pobierac dane (niech inne uslugi wystartuja pierwsze)...");
  vTaskDelay(pdMS_TO_TICKS(90000));
  Serial.println("[ISS TASK] Start pobierania danych ISS");

  while (true) {
    unsigned long now = millis();
    
    // Fetch ISS data every 30 seconds - HTTPS (n2yo / wheretheiss) allocuje
    // ~20-30 KB TLS kontekst za kazdym razem; 5s interwal wyczerpywal
    // pamiec ESP32 i powodowal crash (StoreProhibited 0x0).
    // ISS leci ~7.66 km/s, 30s interwal to ~230 km - wystarczajaco
    // czesto dla wyswietlania na mapie.
    if (now - lastIssDataFetch > 30000 || lastIssDataFetch == 0) {
      // Take mutex before updating ISS data
      if (issDataMutex != NULL && xSemaphoreTake(issDataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        fetchIssData();
        lastIssDataFetch = millis();
        xSemaphoreGive(issDataMutex);
      }
    }
    
    // Fetch country every ~30 seconds
    // Sprawdzaj kraj co 30s - Nominatim to ciezki HTTPS (reverse geocode),
    // przy 12s generowal zbyt duze obciazenie sieciowe i przyczynial sie
    // do wyczerpania pamieci/pamieci TLS ESP32.
    if (now - lastIssCountryCheck > 30000 || lastIssCountryCheck == 0) {
      double snapLat = 0.0, snapLng = 0.0;
      bool haveSnapshot = false;
      if (issDataMutex != NULL && xSemaphoreTake(issDataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        snapLat = issLat;
        snapLng = issLng;
        haveSnapshot = true;
        xSemaphoreGive(issDataMutex);
      }

      if (haveSnapshot) {
        // The lookup itself (HTTPS request, up to ~3s) intentionally runs
        // OUTSIDE the mutex and doesn't touch any shared state directly -
        // previously the whole slow network call happened while holding
        // issDataMutex, which also happened to coincide with the ESP32's
        // single shared pool of WiFi sockets/TLS memory getting starved,
        // causing OTHER unrelated HTTPS requests elsewhere in the sketch
        // (weather, callook) to intermittently fail while this was busy.
        String country = lookupIssCountryFromCoords(snapLat, snapLng);
        // Pusty wynik = przejĹ›ciowe niepowodzenie (patrz komentarz w
        // lookupIssCountryFromCoords) - NIE nadpisujemy w tym wypadku,
        // ĹĽeby dobrze znana nazwa kraju nie znikaĹ‚a przy kaĹĽdej drobnej
        // usterce sieciowej (to wĹ‚aĹ›nie powodowaĹ‚o "miganie").
        if (country.length() > 0) {
          if (issDataMutex != NULL && xSemaphoreTake(issDataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            issCountry = country;
            xSemaphoreGive(issDataMutex);
          }
        }
      }
      lastIssCountryCheck = millis();
    }

    // OdĹ›wieĹĽ TLE co ISS_TLE_REFRESH_MS (domyĹ›lnie co 6h), plus jednorazowo
    // OdĹ›wieĹĽ TLE co ISS_TLE_REFRESH_MS (domyĹ›lnie co 6h) gdy juĹĽ je mamy,
    // ale przy NIEUDANEJ prĂłbie ponawiaj duĹĽo szybciej (co 2 min) zamiast
    // czekaÄ‡ kolejne 6h - wczeĹ›niej pojedyncze przejĹ›ciowe niepowodzenie
    // (np. "[ISS TLE] HTTP kod: -1" widoczne w logu) blokowaĹ‚o TLE na 6h,
    // co skutkowaĹ‚o brakiem danych o kolejnym przelocie przez caĹ‚y ten czas.
    const unsigned long tleRetryMs = 2UL * 60UL * 1000UL;
    unsigned long tleInterval = issTleValid ? ISS_TLE_REFRESH_MS : tleRetryMs;
    if (lastIssTleFetch == 0 || now - lastIssTleFetch > tleInterval) {
      lastIssTleFetch = millis(); // ustaw PRZED prĂłbÄ…, ĹĽeby nieudane pobranie
                                   // (np. brak WiFi na starcie) nie zapÄ™tliĹ‚o
                                   // ciÄ…gĹ‚ych prĂłb co 100ms
      bool tleOk = fetchIssTLE();
      if (!tleOk) {
        Serial.println("[ISS TLE] Niepowodzenie - ponowna proba za 2 minuty");
      }
    }

    // Przewidywanie nastÄ™pnego przelotu. JeĹ›li skonfigurowano klucz n2yo.com
    // (w ustawieniach WWW), uĹĽywamy ich API - prostsze, bardziej niezawodne,
    // nie obciÄ…ĹĽa ESP32 wĹ‚asnym skanowaniem SGP4. Bez klucza - zapasowo
    // liczymy to sami lokalnie (skanowanie do przodu co 30s w oknie 6h, co
    // jest wyraĹşnie droĹĽsze niĹĽ pojedyncza propagacja, wiÄ™c robimy to rzadko).
    static unsigned long lastIssPassEstimate = 0;
    // Wymuszone odswiezenie (wejscie na ekran ISS albo swieze TLE) - nie
    // czekaj na 5-minutowy interwal, zeby "NAJBLIZSZY/NATEPNE PRZELOTY"
    // nie byly puste zaraz po wejsciu na ekran.
    if (issForceNextPassRefresh) {
      issForceNextPassRefresh = false;
      if (n2yoApiKey.length() > 0) {
        if (!fetchIssNextPassFromN2yo() && issTleValid) updateIssNextPassEstimate();
      } else if (issTleValid) {
        updateIssNextPassEstimate();
      }
      lastIssPassEstimate = millis();
    }
    if (lastIssPassEstimate == 0 || now - lastIssPassEstimate > 5UL * 60UL * 1000UL) {
      lastIssPassEstimate = millis();
      if (n2yoApiKey.length() > 0) {
        if (!fetchIssNextPassFromN2yo()) {
          Serial.println("[ISS PASS] n2yo nieudane, probuje lokalnego SGP4 jako zapasowego");
          if (issTleValid) updateIssNextPassEstimate();
        }
      } else if (issTleValid) {
        updateIssNextPassEstimate();
      }
    }

    // Yield to other tasks
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// Konfiguracja filtrĂ„â€šĹ‚w CC-Cluster (dxspots.com)
bool clusterNoAnnouncements = true;      // set/noann - wyĹ‚Ä…cz ogĹ‚oszenia
bool clusterNoWWV = true;                // set/nowwv - wyĹ‚Ä…cz WWV
bool clusterNoWCY = true;                // set/nowcy - wyĹ‚Ä…cz WCY
bool clusterUseFilters = false;           // Czy uĹĽywaÄ‡ filtrĂłw (set/filter)
String clusterFilterCommands = "";        // Dodatkowe komendy filtrĂ„â€šĹ‚w (np. "set/filter k,ve/pass")

// Status poĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czenia
bool wifiConnected = false;
bool telnetConnected = false;
unsigned long lastTelnetAttempt = 0;
unsigned long lastNTPUpdate = 0;
unsigned long lastWiFiReconnectAttempt = 0;
unsigned long lastPotaAttempt = 0;

const uint8_t RGB_LED_RED_PIN = 4;
const uint8_t RGB_LED_GREEN_PIN = 5;
const uint8_t RGB_LED_BLUE_PIN = 6;
const unsigned long RGB_RED_DISCONNECTED_BLINK_MS = 1000UL;
bool rgbLedPrevWifiConnected = false;
unsigned long rgbRedBlinkLastToggleMs = 0;
bool rgbRedBlinkStateOn = false;
bool rgbBlueAprsAlertActive = false;
unsigned long rgbBlueAprsAlertUntilMs = 0;
unsigned long rgbBlueAprsLastToggleMs = 0;
bool rgbBlueAprsStateOn = false;

// ========== KONFIGURACJA PĹYTKI TP4056 I AKUMULATORA 18650 ==========
// PĹ‚ytka Ĺ‚adowania 5V 1A z moduĹ‚em TP4056 i ochronÄ… baterii 18650
// - WejĹ›cie: 5V (micro USB/USB-C)
// - WyjĹ›cie: 4.2V (bateria naĹ‚adowana) lub 5V (zasilanie zewnÄ™trzne)
// - Pomiar napiÄ™cia przez dzielnik 100k/100k na pin ADC

#define BATTERY_MONITORING_ENABLED  // WĹ‚Ä…cz pomiar napiÄ™cia baterii
#define BATTERY_ADC_PIN 1           // Pin ADC1_CH0 (GPIO1) - pomiar napiÄ™cia baterii (ESP32-S3)
#define BATTERY_ADC_RESOLUTION 12   // RozdzielczoĹ›Ä‡ ADC (12 bit = 0-4095)
#define BATTERY_VOLTAGE_DIVIDER 2.0f // Dzielnik napiÄ™cia 100k/100k (1:2)
#define BATTERY_ADC_VREF 3.3f       // NapiÄ™cie referencyjne ADC (3.3V)

// Zakresy napiÄ™cia akumulatora 18650 (dla pĹ‚ytki z ochronÄ… DW01A)
#define BATTERY_VOLTAGE_MAX 4.2f      // 100% naĹ‚adowana
#define BATTERY_VOLTAGE_NOM 3.8f    // 50% nominalna
#define BATTERY_VOLTAGE_LOW 3.6f    // 25% niski poziom (pomaraĹ„czowy)
#define BATTERY_VOLTAGE_MIN 3.4f    // 0% krytyczny (czerwony)
#define BATTERY_VOLTAGE_CUTOFF 3.4f // 0% ochrona wyĹ‚Ä…cza

// InterwaĹ‚y pomiaru i ostrzegania
#define BATTERY_CHECK_INTERVAL_MS 1000UL   // Sprawdzaj co 1 sekundÄ™
#define BATTERY_WARN_INTERVAL_MS 60000UL   // Ostrzegaj co 60 sekund przy niskim poziomie

unsigned long lastBatteryCheckMs = 0;
float batteryVoltage = 0.0f;
int batteryPercentage = 0;
bool batteryLowWarningShown = false;
bool batteryCharging = false;  // Rozpoznawanie Ĺ‚adowania (napiÄ™cie > 4.25V)

// Funkcja odczytu napiÄ™cia baterii z ADC
float readBatteryVoltage() {
  // DokĹ‚adniejszy odczyt - Ĺ›rednia z 8 prĂłbek
  long adcSum = 0;
  const int samples = 8;
  for (int i = 0; i < samples; i++) {
    adcSum += analogRead(BATTERY_ADC_PIN);
    delayMicroseconds(100);
  }
  float adcAvg = adcSum / (float)samples;
  float voltageAtPin = (adcAvg / 4095.0f) * BATTERY_ADC_VREF;
  return voltageAtPin * BATTERY_VOLTAGE_DIVIDER;
}

// Funkcja obliczania procentu naĹ‚adowania (liniowe mapowanie: 4.2V=100%, 3.8V=50%, 3.4V=0%)
int calculateBatteryPercentage(float voltage) {
  if (voltage >= BATTERY_VOLTAGE_MAX) return 100;
  if (voltage <= BATTERY_VOLTAGE_MIN) return 0;
  // Liniowe mapowanie miÄ™dzy 3.4V (0%) a 4.2V (100%)
  // 3.8V = 50%
  float percentage = ((voltage - BATTERY_VOLTAGE_MIN) / (BATTERY_VOLTAGE_MAX - BATTERY_VOLTAGE_MIN)) * 100.0f;
  return (int)percentage;
}

// SprawdĹş stan baterii i zwrĂłÄ‡ true jeĹ›li byĹ‚a aktualizacja
bool updateBatteryStatus() {
  unsigned long now = millis();
  if (now - lastBatteryCheckMs < BATTERY_CHECK_INTERVAL_MS) {
    return false;
  }
  lastBatteryCheckMs = now;
  
  float v = readBatteryVoltage();
  // Sanity: odczyt < 1.0 V = dzielnik/akumulator niepodlaczony (wiszace GPIO1)
  // albo ADC zwraca smieci. Wtedy nie raportujemy "0%" ani falszywego
  // ostrzezenia "podlacz ladowarke" - po prostu brak pomiaru.
  if (v < 1.0f) {
    batteryVoltage = 0.0f;
    batteryPercentage = 0;
    batteryCharging = false;
    return false;
  }
  
  batteryVoltage = v;
  batteryPercentage = calculateBatteryPercentage(batteryVoltage);
  
  // Wykrywanie Ĺ‚adowania (napiÄ™cie > 4.25V wskazuje na podĹ‚Ä…czone 5V)
  batteryCharging = (batteryVoltage > 4.25f);
  
  return true;
}

// Zwraca kolor dla poziomu baterii (do wyĹ›wietlania na TFT)
uint16_t getBatteryColor() {
  if (batteryCharging) return 0x07E0;  // Zielony podczas Ĺ‚adowania
  if (batteryVoltage <= BATTERY_VOLTAGE_MIN) return TFT_RED;
  if (batteryVoltage <= BATTERY_VOLTAGE_LOW) return 0xFDA0;  // PomaraĹ„czowy
  return 0x07E0;  // Zielony
}

// Funkcja rysujÄ…ca ikonÄ™ baterii na TFT
void drawBatteryIcon(int x, int y, int w, int h) {
  if (!tftInitialized) return;
  
  uint16_t color = getBatteryColor();
  int level = batteryPercentage;
  
  // Obrys baterii
  tft.drawRect(x, y, w, h, TFT_WHITE);
  // Plusik (kontakt) baterii
  tft.fillRect(x + w, y + h/4, 3, h/2, TFT_WHITE);
  
  // WypeĹ‚nienie poziomem naĹ‚adowania
  int fillW = (w - 4) * level / 100;
  if (fillW < 0) fillW = 0;
  if (fillW > w - 4) fillW = w - 4;
  
  tft.fillRect(x + 2, y + 2, fillW, h - 4, color);
  // WyczyĹ›Ä‡ pozostaĹ‚Ä… czÄ™Ĺ›Ä‡ (gdy poziom spada)
  tft.fillRect(x + 2 + fillW, y + 2, (w - 4) - fillW, h - 4, TFT_BLACK);
}

// Funkcja wyĹ›wietlajÄ…ca status baterii na ekranie (w nagĹ‚Ăłwku)
void drawBatteryStatus(int x, int y) {
  if (!tftInitialized) return;
  
  // Aktualizuj stan baterii
  updateBatteryStatus();
  
  // Rysuj ikonÄ™ baterii (30x16 pikseli)
  drawBatteryIcon(x, y, 30, 14);
  
  // Tekst z procentami i napiÄ™ciem
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  char buf[16];
  snprintf(buf, sizeof(buf), "%d%% %.1fV", batteryPercentage, batteryVoltage);
  tft.setCursor(x + 36, y + 4);
  tft.print(buf);
  
  // Indykator Ĺ‚adowania
  if (batteryCharging) {
    tft.setTextColor(0x07E0);  // Zielony
    tft.setCursor(x + 36, y - 8);
    tft.print("[Ĺ‚adowanie]");
  }
}

// Szybka aktualizacja tylko ikony baterii (bez odĹ›wieĹĽania caĹ‚ego nagĹ‚Ăłwka)
// UĹĽywana w loop() aby uniknÄ…Ä‡ watchdog resetu
void drawBatteryQuickUpdate(bool skipScreenCheck = false) {
  if (!tftInitialized) return;
  if (!skipScreenCheck && (currentScreen != SCREEN_HAM_CLOCK || inMenu)) return;
  if (brightnessMenuActive || screenSaverMenuActive) return;  // Nie rysuj podczas menu
  
  // SprawdĹş czy dzielnik jest podĹ‚Ä…czony
  if (batteryVoltage < 1.5f || batteryVoltage > 5.0f) return;
  
  int screenW = tft.width();
  int x = screenW - 120, y = 10, w = 26, h = 12;  // Prawa strona ekranu (bardziej w lewo)
  
  // WyczyĹ›Ä‡ obszar ikony baterii przed narysowaniem (unika paskĂłw/Ĺ›mieci)
  tft.fillRect(x - 2, y - 2, w + 6, h + 4, TFT_BLACK);
  
  // Rysuj ikonÄ™ baterii
  uint16_t color = getBatteryColor();
  int level = batteryPercentage;
  
  tft.drawRect(x, y, w, h, TFT_WHITE);
  tft.fillRect(x + w, y + h/4, 3, h/2, TFT_WHITE);
  
  int fillW = (w - 4) * level / 100;
  if (fillW < 0) fillW = 0;
  if (fillW > w - 4) fillW = w - 4;
  
  tft.fillRect(x + 2, y + 2, fillW, h - 4, color);
  tft.fillRect(x + 2 + fillW, y + 2, (w - 4) - fillW, h - 4, TFT_BLACK);
  
  // Tekst z procentem baterii - wyczyĹ›Ä‡ tĹ‚o przed napisaniem
  tft.fillRect(x + 32, y + 3, 30, 8, TFT_BLACK);  // WyczyĹ›Ä‡ obszar tekstu
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  char batBuf[16];
  snprintf(batBuf, sizeof(batBuf), "%d%%", batteryPercentage);
  tft.setCursor(x + 32, y + 3);
  tft.print(batBuf);
}

// ========== Restart (np. po zapisaniu konfiguracji)
bool restartRequested = false;
unsigned long restartAtMs = 0;
// Rekonfiguracja WiFi bez restartu (zmiana SSID w WWW).
bool wifiReconfigRequested = false;
unsigned long wifiReconfigAtMs = 0;

// Stan sesji DX Cluster (Telnet)
bool clusterLoginSent = false;
bool clusterLoginScheduled = false;
unsigned long clusterSendLoginAtMs = 0;
unsigned long lastClusterKeepAliveMs = 0;
unsigned long lastTelnetRxMs = 0;
const unsigned long TELNET_INACTIVITY_RECONNECT_MS = 5UL * 60UL * 1000UL; // 5 minut

// Stan sesji POTA Cluster (Telnet)
bool potaTelnetConnected = false;
bool potaLoginSent = false;
bool potaLoginScheduled = false;
unsigned long potaSendLoginAtMs = 0;
unsigned long lastPotaKeepAliveMs = 0;
unsigned long lastPotaRxMs = 0;
const unsigned long POTA_TELNET_INACTIVITY_RECONNECT_MS = 5UL * 60UL * 1000UL; // 5 minut

// Bufor dla danych Telnet
String telnetBuffer = "";
String pendingTelnetLine = "";
unsigned long pendingTelnetDropped = 0;

// Bufor dla danych POTA Telnet
String potaTelnetBuffer = "";
String pendingPotaLine = "";
unsigned long pendingPotaDropped = 0;

// QRZ cache (ĹĽeby nie pytaÄ‡ w kĂłĹ‚ko o te same znaki)
struct QrzCacheEntry {
  String callsign;
  String grid;
  String country;
  String name;
  String email;
  String qth;
  float lat;
  float lon;
  bool hasLatLon;
  bool notFound;      // wpis negatywny: API nie zna tego znaku (np. callook = tylko USA)
  unsigned long fetchedAtMs;
};
const int QRZ_CACHE_SIZE = 20;
QrzCacheEntry qrzCache[QRZ_CACHE_SIZE];
bool isQrzCacheKnownMissing(const String &callsign, unsigned long ttlMs);
void writeQrzNegativeCache(const String &callsign);

// Forward declarations
void drawSystemInfo();
void drawPskMap();
void drawIssPassTracking();

// ========== INICJALIZACJA TFT (ESP32-S3 N16R8 + ILI9488) ==========
#ifdef ENABLE_TFT_DISPLAY
void setupBacklightPwm() {
#if (TFT_BL_PIN >= 0)
  pinMode(TFT_BL_PIN, OUTPUT);
  digitalWrite(TFT_BL_PIN, TFT_BL_INVERTED ? LOW : HIGH);
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  // Arduino ESP32 core 3.x LEDC API
  ledcAttach(TFT_BL_PIN, BACKLIGHT_PWM_FREQ, BACKLIGHT_PWM_RES_BITS);
#else
  // Arduino ESP32 core 2.x LEDC API
  ledcSetup(BACKLIGHT_PWM_CHANNEL, BACKLIGHT_PWM_FREQ, BACKLIGHT_PWM_RES_BITS);
  ledcAttachPin(TFT_BL_PIN, BACKLIGHT_PWM_CHANNEL);
#endif
#endif
}

// Zapis surowego wypelnienia PWM podswietlenia. Na rdzeniu 2.x ledcWrite()
// przyjmuje KANAL (0..15), a nie numer GPIO - wywolanie ledcWrite(TFT_BL_PIN,...)
// z pinem 21 jest po cichu ignorowane (chan >= LEDC_CHANNELS), wiec podswietlenie
// NIE gaslo. W rdzeniu 3.x ledcWrite() przyjmuje pin. Ta funkcja izoluje obie
// roznice, zeby zarowno jasnosc jak i usypianie ekranu dzialaly poprawnie.
static void writeBacklightDuty(uint32_t duty) {
#if (TFT_BL_PIN >= 0)
  uint32_t dutyMax = (1U << BACKLIGHT_PWM_RES_BITS) - 1U;
  if (duty > dutyMax) duty = dutyMax;
  if (TFT_BL_INVERTED) {
    duty = dutyMax - duty;
  }
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  ledcWrite(TFT_BL_PIN, duty);
#else
  ledcWrite(BACKLIGHT_PWM_CHANNEL, duty);
#endif
#endif
}

void setBacklightPercent(int percent) {
  if (percent < MIN_BACKLIGHT_PERCENT) percent = MIN_BACKLIGHT_PERCENT;
  if (percent > 100) percent = 100;
  backlightPercent = percent;
  uint32_t dutyMax = (1U << BACKLIGHT_PWM_RES_BITS) - 1U;
  writeBacklightDuty((percent * dutyMax) / 100U);
}

// Zapis PWM bez ruszania wartosci bazowej backlightPercent. Uzywane przez
// nocne obnizenie jasnosci, zeby nie nadpisac jasnosci ustawionej przez
// uzytkownika (setBacklightPercent() robi backlightPercent = percent).
void setBacklightPercentRaw(int percent) {
  if (percent < MIN_BACKLIGHT_PERCENT) percent = MIN_BACKLIGHT_PERCENT;
  if (percent > 100) percent = 100;
  uint32_t dutyMax = (1U << BACKLIGHT_PWM_RES_BITS) - 1U;
  writeBacklightDuty((percent * dutyMax) / 100U);
}

// Noc = ikona OWM "n" albo brak ikony i godzina lokalna poza 6-18.
bool tftIsNightTime() {
  if (weatherData.iconCode.endsWith("n")) return true;
  if (weatherData.iconCode.endsWith("d")) return false;
  struct tm ti;
  if (getLocalTime(&ti, 1)) return (ti.tm_hour < 6 || ti.tm_hour >= 18);
  return false;
}

int effectiveBacklightPercent() {
  int p = backlightPercent;
  if (tftNightDimmingPercent > 0 && tftIsNightTime()) {
    p = p - (p * tftNightDimmingPercent) / 100;
  }
  if (p < MIN_BACKLIGHT_PERCENT) p = MIN_BACKLIGHT_PERCENT;
  if (p > 100) p = 100;
  return p;
}

// Wypelnienie PWM z uwzglednieniem nocnego obnizenia (bez zmiany bazowej wartosci).
void applyEffectiveBacklight() { setBacklightPercentRaw(effectiveBacklightPercent()); }

void applyTouchRotation() {
#if TOUCH_ENABLED
  Serial.print("*** Applying touch.setRotation(");
  Serial.print(touchRotation);
  Serial.println(") ***");
  touch.setRotation(touchRotation);
#endif
}

void applyTftRotation() {
  Serial.print("*** Applying tft.setRotation(");
  Serial.print(tftRotation);
  Serial.println(") ***");
  tft.setRotation(tftRotation);
}

void applyTftInversion() {
  tftInvertColors = sanitizeTftInvertSetting(tftInvertColors);
  Serial.print("*** Applying tft.invertDisplay(");
  Serial.print(tftInvertColors ? "true" : "false");
  Serial.println(") ***");
  tft.invertDisplay(tftInvertColors);
  
  // Fallback direct register write for ILI9488/ST7796
  // 0x21 is Display Inversion ON, 0x20 is OFF
  Serial.print("*** Applying direct register write: 0x");
  Serial.print(tftInvertColors ? "21" : "20");
  Serial.println(" ***");
  tft.writecommand(tftInvertColors ? 0x21 : 0x20);
}

void initTFT() {
  Serial.println("=== Inicjalizacja TFT ===");
  Serial.printf("TFT setup info=%s\n", USER_SETUP_INFO);
  Serial.printf("TFT profile=%s SCLK=%d MOSI=%d MISO=%d CS=%d DC=%d RST=%d BL=%d\n",
                "ILI9488",
                TFT_SCLK, TFT_MOSI, TFT_MISO, TFT_CS, TFT_DC, TFT_RST, TFT_BL_PIN);
  
  // Podświetlenie i reset muszą być na prawdziwych wyjściach GPIO.
  setupBacklightPwm();
  applyEffectiveBacklight();
#if (TFT_RST >= 0)
  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, HIGH);
  delay(10);
  digitalWrite(TFT_RST, LOW);
  delay(20);
  digitalWrite(TFT_RST, HIGH);
  delay(150);
#endif
  Serial.println("TFT begin()...");
  tft.begin();
  Serial.println("TFT begin() OK");
  // Test kolorow (RED->GREEN->BLUE->WHITE) tylko na zadanie: domyslnie OFF.
  // Sekwencja konczy sie bialym wypelnieniem i powtarza sie przy KAZDYM
  // restarcie (zapis/import konfiguracji restartuje ESP), wiec wygladala jak
  // "bialy migajacy ekran" / petla restartu. Wlacz przez -DTFT_BOOT_SELFTEST
  // w platformio.ini przy diagnozie panelu.
#ifdef TFT_BOOT_SELFTEST
  tft.fillScreen(TFT_RED);
  delay(2000);
  tft.fillScreen(TFT_GREEN);
  delay(2000);
  tft.fillScreen(TFT_BLUE);
  delay(2000);
  tft.fillScreen(TFT_WHITE);
#endif

  applyTftRotation();
  applyTftInversion();
  // Czarny (nie bialy) - init nie moze konczyc sie jasnym blyskiem, a ekran
  // startowy i tak zaczyna od czarnego tla (drawBootScreen/drawSplashScreen).
  tft.fillScreen(TFT_BLACK);

  // Dotyk XPT2046 na tej samej magistrali SPI co TFT (osobny CS).
#if TOUCH_ENABLED
  // Magistrala FSPI jest juz zainicjalizowana przez TFT_eSPI z MISO=TFT_MISO.
  // Jawne piny gwarantuja, ze dotyk czyta z tego samego MISO (np. 14 w wariancie awaryjnym).
  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
  touch.begin(SPI);
  touchReady = true;
  applyTouchRotation();
#else
  Serial.println("[DOTYK] Wylaczony (TOUCH_ENABLED=0) - XPT2046 nie podlaczony");
#endif
  
  tftInitialized = true;
  inMenu = false;
  menuOption = 0;
  
  Serial.println("=== TFT zainicjalizowany OK ===");
  
  // WyĹ›wietl ekran startowy (boot log)
  drawBootScreen();
  tftBootPrintLine("=== Inicjalizacja TFT ===");
  tftBootPrintLine("TFT begin()...");
  tftBootPrintLine("TFT begin() OK");
  tftBootPrintLine("=== TFT zainicjalizowany OK ===");
}

void drawBootScreen() {
  if (!tftInitialized) {
    return;
  }
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN);
  tft.setTextSize(1);
  bootLogY = 2;
  tft.setCursor(2, bootLogY);
}

void tftBootPrintLine(const String &line) {
  if (!tftInitialized) {
    return;
  }
  tft.setTextColor(TFT_GREEN);
  tft.setTextSize(1);
  tft.setCursor(2, bootLogY);
  tft.print(line);
  bootLogY += BOOT_LOG_LINE_HEIGHT;
  if (bootLogY > 230) {
    tft.fillScreen(TFT_BLACK);
    bootLogY = 2;
  }
}

void drawWelcomeScreenYellow() {
  if (!tftInitialized) {
    return;
  }

  // Ekran powitalny moze byc zastapiony wlasna grafika (/welcome.<ext>),
  // wgrana przez WWW (zakladka TFT -> "Grafika na ekran", cel "Ekran powitalny").
  if (littleFsReady) {
    for (const char *cand : {"/welcome.png", "/welcome.jpg", "/welcome.jpeg", "/welcome.bmp"}) {
      if (!LittleFS.exists(cand)) continue;
      if (drawImageFromFS(cand)) {
        Serial.printf("[Welcome] Zaladowano ekran powitalny: %s\n", cand);
        tft.setTextSize(1);
        tft.setTextColor(TFT_LIGHTGREY);
        tft.setCursor(240 - 80, 310);
        tft.print("Kliknij ekran aby kontynuowac...");
        return;
      }
      Serial.printf("[Welcome] Plik %s istnieje, ale dekodowanie nie udalo sie\n", cand);
    }
  }

  tft.fillScreen(TFT_BLACK);

  // Jedna linia tytulu z wersja firmware (FIRMWARE_VERSION = "1.2").
  // Polskie znaki - wymaga UTF-8 w zrodle (--utf8-source).
  String lines[5];
  lines[0] = "ESP32-HAM-CLOCK v" FIRMWARE_VERSION;
  lines[1] = "Projekt bazowy: Konrad Wiśniewski SP3KON";
  lines[2] = "Adaptacja i rozwój: Tomasz SP9TNV.";
  lines[3] = "Kontakt: SP9TNV@gmail.com";
  lines[4] = "Projekt udostępniany na licencji MIT.";

  // Wiekszy font VLW (DejaVu Sans 21 pt, polskie znaki). Gdy brak pliku/pamieci,
  // spadamy na ROM Font 2 (16 px, bez diakrytykow) - i tak sie zmiesci.
  bool smooth = littleFsReady && LittleFS.exists(WELCOME_FONT_FILE) && ESP.getFreeHeap() > 80000;
  if (smooth) {
    tft.loadFont(WELCOME_FONT_NAME, LittleFS);

    int lineH = tft.fontHeight() + 2;
    int textH = lineH * 5;

    // Godlo PZK (romb) nad tekstem, jesli plik jest w LittleFS.
    int logoH = 0, logoY = 0;
    if (LittleFS.exists(PZK_LOGO_FILE)) {
      logoH = PZK_LOGO_H;
      logoY = (320 - (logoH + 10 + textH)) / 2;
      if (drawBmpFromFS(PZK_LOGO_FILE, (480 - PZK_LOGO_W) / 2, logoY)) {
        logoY += logoH + 10;
      } else {
        logoH = 0;
      }
    }
    // Gdy jest godlo - blok tekstu tuz pod nim; inaczej optycznie w dol (+18 px,
    // bo litera bazowa lezy w dolnej czesci wiersza i ogonki schodza ponizej).
    int y = (logoH > 0) ? logoY : (320 - textH) / 2 + 18;

    for (int i = 0; i < 5; i++) {
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setCursor((480 - tft.textWidth(lines[i])) / 2, y);
      tft.print(lines[i]);
      y += lineH;
    }
    tft.unloadFont();
    return;
  }

  // Fallback: ROM Font 2 (16 px), tekst bez polskich ogonkow.
  const char *ascii[5] = {
    "ESP32-HAM-CLOCK v" FIRMWARE_VERSION,
    "Projekt bazowy: Konrad Wisniewski SP3KON",
    "Adaptacja i rozwoj: Tomasz SP9TNV.",
    "Kontakt: SP9TNV@gmail.com",
    "Projekt udostepniany na licencji MIT."
  };
  const int lineH = 24;
  const int textH = lineH * 5;
  int y;
  if (LittleFS.exists(PZK_LOGO_FILE) &&
      drawBmpFromFS(PZK_LOGO_FILE, (480 - PZK_LOGO_W) / 2, (320 - (PZK_LOGO_H + 10 + textH)) / 2)) {
    y = (320 - (PZK_LOGO_H + 10 + textH)) / 2 + PZK_LOGO_H + 10;
  } else {
    y = (320 - textH) / 2 + 4;
  }
  for (int i = 0; i < 5; i++) {
    uiWelcomeText(y, ascii[i], 2, TFT_WHITE);
    y += lineH;
  }
}

// Rysuje wysrodkowana linie ekranu powitalnego (czarny ekran, bialy tekst).
// Font 2 = 16 px, wiec linia jest czytelna; dluzsze napisy mierzone i centrowane.
static void uiWelcomeText(int y, const char *txt, uint8_t font, uint16_t color) {
  tft.setTextSize(1);
  tft.setTextColor(color, TFT_BLACK);
  int w = tft.textWidth(txt, font);
  int x = (480 - w) / 2;
  if (x < 4) x = 4;
  tft.setCursor(x, y);
  tft.setTextDatum(TL_DATUM);
  tft.print(txt);
}

// Ekran startowy z grafika (splash). Wczytuje PLIK, ktory wrzucisz do data/
// lub wgrasz przez WWW (zakladka Linki -> "Wgraj ekran startowy").
// Obsluguje: BMP 24/16-bit, PNG, JPG (przez drawImageFromFS -> dekodery).
void drawSplashScreen() {
  if (!tftInitialized) {
    return;
  }

  tft.fillScreen(TFT_BLACK);
  const int centerX = 240;

  // Kolejnosc = priorytet. PNG/JPG dodane, bo firmware ma teraz dekodery.
  static const char *splashCandidates[] = {
    "/splash.png",
    "/splash.jpg",
    "/splash.jpeg",
    "/icon50/splash.bmp",
    "/splash.bmp",
    "/splash_16bit.bmp",
    "/splash16.bmp",
    "/boot.bmp",
    "/boot.png",
    "/logo.bmp",
    "/logo.png",
    "/start.bmp",
    "/start.png",
  };

  bool imgLoaded = false;
  if (littleFsReady) {
    for (const char *cand : splashCandidates) {
      if (!LittleFS.exists(cand)) continue;
      if (drawImageFromFS(cand)) {
        imgLoaded = true;
        Serial.printf("[Splash] Zaladowano ekran startowy: %s\n", cand);
        break;
      }
      Serial.printf("[Splash] Plik %s istnieje, ale dekodowanie nie udalo sie\n", cand);
    }
  }

  splashPathLoaded = imgLoaded;

  if (!imgLoaded) {
    Serial.println("[Splash] Nie znaleziono obrazu startowego - czarny ekran");
    tft.fillScreen(TFT_BLACK);
  }

  // Tekst "Kliknij aby kontynuowac"
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY);
  tft.setCursor(centerX - 80, 310);
  tft.print("Kliknij ekran aby kontynuowac...");
}

// Aktualizuj wyĂ„Ä…Ă˘â‚¬Ĺźwietlacz z aktualnym adresem IP
void updateTFT_IP() {
  if (!tftInitialized) {
    return;
  }
  
  IPAddress ip;
  String modeStr = "";
  
  // SprawdĂ„Ä…ÄąĹş czy jesteĂ„Ä…Ă˘â‚¬Ĺźmy w trybie STA czy AP
  if (wifiConnected && WiFi.status() == WL_CONNECTED) {
    ip = WiFi.localIP();
    modeStr = "WiFi";
  } else if (WiFi.getMode() & WIFI_AP) {
    ip = WiFi.softAPIP();
    modeStr = "AP Mode";
  } else {
    // Brak IP - wyĹ›wietl komunikat
    tft.fillRect(10, 40, 300, 20, TFT_WHITE);
    tft.setTextColor(TFT_RED, TFT_WHITE);
    tft.setTextSize(1);
    tft.setCursor(10, 45);
    tft.print("No connection");
    return;
  }
  
  // WyczyĂ„Ä…Ă˘â‚¬ĹźÄ‚â€žĂ˘â‚¬Ë‡ obszar IP (dla rotacji 1: szerokoĂ„Ä…Ă˘â‚¬ĹźÄ‚â€žĂ˘â‚¬Ë‡ 480)
  tft.fillRect(10, 40, 300, 20, TFT_BLACK);
  
  // WyĂ„Ä…Ă˘â‚¬Ĺźwietl tryb i IP w jednej linii (oszczÄ™dnoĹ›Ä‡ miejsca)
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setTextSize(1);
  tft.setCursor(10, 45);
  tft.print(modeStr);
  tft.print(": ");
  
  // WyĹ›wietl IP
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.print(ip.toString());
}

// Aktualizuj wyĂ„Ä…Ă˘â‚¬Ĺźwietlacz z tabelÄ… spotĂłw (podobnie jak na stronie WWW)
void updateTFT_Spots() {
  static unsigned long lastTFTPrint = 0;
  static int tftCallCount = 0;
  tftCallCount++;
  
  if (!tftInitialized) {
    return;
  }
  
  // Print co 10 wywoĹ‚aĹ„ (dla debugowania)
  unsigned long now = millis();
  if (now - lastTFTPrint > 10000) { // Co 10 sekund
    Serial.print("[TFT] updateTFT_Spots wywoĂ„Ä…Ă˘â‚¬Ĺˇane ");
    Serial.print(tftCallCount);
    Serial.print(" razy, spotCount=");
    Serial.println(spotCount);
    lastTFTPrint = now;
    tftCallCount = 0;
  }
  
  // WyczyĂ„Ä…Ă˘â‚¬ĹźÄ‚â€žĂ˘â‚¬Ë‡ obszar tabeli (zostaw nagĂ„Ä…Ă˘â‚¬ĹˇĂ„â€šĹ‚wek i IP)
  // Dla rotacji 1 (krajobraz): szerokoĂ„Ä…Ă˘â‚¬ĹźÄ‚â€žĂ˘â‚¬Ë‡ 480, wysokoĂ„Ä…Ă˘â‚¬ĹźÄ‚â€žĂ˘â‚¬Ë‡ 320
  // Zostaw miejsce na nagĂ„Ä…Ă˘â‚¬ĹˇĂ„â€šĹ‚wek (0-45) i tabelÄ‚â€žĂ˘â€žË (65-320)
  tft.fillRect(0, 65, 480, 175, TFT_WHITE);
  
  // WyĂ„Ä…Ă˘â‚¬Ĺźwietl nagĂ„Ä…Ă˘â‚¬ĹˇĂ„â€šĹ‚wek tabeli
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setTextSize(1);
  
  // NagĹ‚Ăłwki kolumn (dla szerokoĂ„Ä…Ă˘â‚¬Ĺźci 480px)
  int yPos = 65;
  tft.setCursor(5, yPos);
  tft.print(tr(TR_TIME_SHORT));
  tft.setCursor(55, yPos);
  tft.print(tr(TR_CALL_SHORT));
  tft.setCursor(130, yPos);
  tft.print("MHz");
  tft.setCursor(200, yPos);
  tft.print("Mode");
  tft.setCursor(260, yPos);
  tft.print("km");
  
  yPos += 15;
  
  // WyĹ›wietl maksymalnie 10 spotĂ„â€šĹ‚w na wyĂ„Ä…Ă˘â‚¬Ĺźwietlaczu TFT (Ă„Ä…Ă„Ëťeby zmieĹ›ciÄ‡ siÄ™ na ekranie)
  int maxDisplaySpots = min(spotCount, 10);
  
  for (int i = 0; i < maxDisplaySpots; i++) {
    if (yPos >= 230) break; // Nie wchodĹş poza ekran
    
    // Znak wywoĹ‚awczy - rozmiar 2 (wiÄ™kszy)
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(5, yPos);
    String callStr = spots[i].callsign;
    callStr.toUpperCase();
    if (callStr.length() > 8) callStr = callStr.substring(0, 8);
    tft.print(callStr);
    
    // CzÄ™stotliwoĹ›Ä‡ (MHz) - rozmiar 2
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(140, yPos);
    float freqMHz = spots[i].frequency / 1000.0;
    tft.print(freqMHz, 3);
    
    // Mode - rozmiar 2
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(260, yPos);
    String modeStr = spots[i].mode;
    if (modeStr.length() > 4) modeStr = modeStr.substring(0, 4);
    tft.print(modeStr);
    
    // Czas i kraj - rozmiar 1
    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(350, yPos + 5);
    String timeStr = spots[i].time;
    if (timeStr.length() > 5) timeStr = timeStr.substring(0, 5);
    tft.print(timeStr);
    
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.setCursor(400, yPos + 5);
    String countryText = formatDistanceOrCountry(spots[i], 6);
    tft.print(countryText);
    
    yPos += 20;
  }
  
  // JeĹ›li brak spotĂłw, wyĹ›wietl komunikat
  if (spotCount == 0) {
    tft.setTextColor(TFT_RED, TFT_WHITE);
    tft.setTextSize(1);
    tft.setCursor(10, 90);
    tft.println(tr(TR_WAITING_SPOTS));
  }
}

// ========== SYSTEM MENU I EKRANĂ„â€šĂ˘â‚¬Ĺ›W ==========

extern TaskHandle_t uiTaskHandle;
static void requestUiScreenRedraw(uint8_t pendingScreenId);

// GĂ„Ä…Ă˘â‚¬ĹˇĂ„â€šĹ‚wna funkcja rysujÄ‚â€žĂ˘â‚¬Â¦ca ekrany
void drawScreen(ScreenType screenNum) {
  if (!tftInitialized) {
    return;
  }

  if (uiTaskHandle != nullptr && xTaskGetCurrentTaskHandle() != uiTaskHandle) {
    requestUiScreenRedraw((uint8_t)screenNum);
    return;
  }

  static ScreenType lastDrawnScreen = SCREEN_OFF;
  if (screenNum != lastDrawnScreen) {
    if (isTableFooterScreen(screenNum)) {
      tableNavFooterVisibleUntilMs = millis() + TABLE_NAV_FOOTER_VISIBLE_MS;
    }
    if (screenNum == SCREEN_APRS_RADAR) {
      // On first enter to radar screen, show zoom/info hints for a short time.
      screen6RadarHintUntilMs = millis() + SCREEN6_RADAR_HINT_DURATION_MS;
    }
    lastDrawnScreen = screenNum;
  }

  if (aprsAlertScreenActive) {
    return;
  }
  
  tft.fillScreen(TFT_BLACK);

  // Wspolny styl ISS: naglowek i stopka dla wszystkich ekranow poza pelnoekranowymi.
  const bool issFullBleed = (screenNum == SCREEN_MATRIX_CLOCK ||
                             screenNum == SCREEN_PSK_MAP ||
                             screenNum == SCREEN_APRS_RADAR ||
                             screenNum == SCREEN_SYSTEM_INFO ||
                             screenNum == SCREEN_ISS_PASS_TRACKING);
  if (!issFullBleed) {
    switch (screenNum) {
      case SCREEN_HAM_CLOCK:
        // Zegar rysuje wlasny naglowek (klasyczny albo wg kodu wzorcowego).
        screen1HeaderNeedsRedraw = (screen1ClockStyle == 1);
        break;
      case SCREEN_DX_CLUSTER:       uiHeader("DXSPOTS.com", true); break;
      case SCREEN_POTA_CLUSTER:     uiHeader("POTA.app", true); break;
      case SCREEN_HAMALERT_CLUSTER: uiHeader("HAMALERT.org", true); break;
      case SCREEN_APRS_IS:          uiHeader("APRS-IS", true); break;
      case SCREEN_WEATHER_DSP:      uiHeader(tr(TR_WEATHER), true); break;
      case SCREEN_WEATHER_FORECAST: uiHeader((tftLanguage == TFT_LANG_EN) ? "Weather forecast" : "Prognoza pogody", true); break;
      case SCREEN_SUN_SPOTS:        uiHeader((tftLanguage == TFT_LANG_EN) ? "PROPAGATION" : "PROPAGACJA", false); break;
      case SCREEN_BAND_INFO:        uiHeader("SOLAR PROPAGATION INDEX", false); break;
      case SCREEN_SYSTEM_INFO:      uiHeader("SYSTEM INFO", true); break;
      default: break;
    }
  }
  
  switch (screenNum) {
    case SCREEN_HAM_CLOCK:
      drawHamClock();
      break;
    case SCREEN_DX_CLUSTER:
      drawDxCluster();
      break;
    case SCREEN_SUN_SPOTS:
      drawSunSpots();
      break;
    case SCREEN_BAND_INFO:
      drawBandInfo();
      break;
    case SCREEN_WEATHER_DSP:
      drawWeather();
      break;
    case SCREEN_WEATHER_FORECAST:
      drawWeatherForecast();
      break;
    case SCREEN_APRS_IS:
      drawAprsIs();
      break;
    case SCREEN_APRS_RADAR:
      drawAprsRadar();
      break;
    case SCREEN_POTA_CLUSTER:
      drawPotaCluster();
      break;
    case SCREEN_HAMALERT_CLUSTER:
      drawHamalertCluster();
      break;
    case SCREEN_MATRIX_CLOCK:
      drawMatrixClock();
      break;
    case SCREEN_SYSTEM_INFO:
      drawSystemInfo();
      break;
    case SCREEN_PSK_MAP:
      drawPskMap();
      break;
    case SCREEN_ISS_PASS_TRACKING:
      issForceNextPassRefresh = true;  // odswiez plan przelotow przy wejsciu
      drawIssPassTracking();
      break;
    case SCREEN_OFF:
    default:
      drawHamClock();
      break;
  }

  // Zegar (oba style) rysuje wlasna stopke/naglowek, wiec bez uiFooter/IP.
  const bool hamClockScreen = (screenNum == SCREEN_HAM_CLOCK);
  if (!issFullBleed && screenNum != SCREEN_OFF && !hamClockScreen) {
    uiFooter();
  }
}

// Ekran 1: Startowy - Info
uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t colorWheel(uint8_t pos) {
  pos = 255 - pos;
  if (pos < 85) {
    return rgb565(255 - pos * 3, 0, pos * 3);
  }
  if (pos < 170) {
    pos -= 85;
    return rgb565(0, pos * 3, 255 - pos * 3);
  }
  pos -= 170;
  return rgb565(pos * 3, 255 - pos * 3, 0);
}

void applyMenuThemeFromHue() {
  // W trybie nowoczesnym menu ma zawsze teal (wspolny styl ISS), niezaleznie od
  // suwaka motywu; w trybie klasycznym uzywamy koloru z kolowego (domyslnie
  // pomaranczowy). Zapisujemy suwak do menuThemeHue, wiec powrot do klasycznego
  // odtwarza wybrany wczesniej kolor.
  menuThemeColor = uiStyleClassic() ? colorWheel(menuThemeHue) : UI_COL_ACCENT;
}

// Definicja koloru "Radio Orange" - dynamiczny motyw
#define TFT_RADIO_ORANGE menuThemeColor

// Kolory i tĂ„Ä…Ă˘â‚¬Ĺˇo "Matrix" dla ekranu 10
#define MATRIX_DARKGREEN 0x0480
#define MATRIX_BRIGHTGREEN 0x07E0
#define MATRIX_WHITEGREEN 0xAFE5

uint16_t lerpColor565(uint16_t from, uint16_t to, uint8_t t) {
  uint8_t r1 = (from >> 11) & 0x1F;
  uint8_t g1 = (from >> 5) & 0x3F;
  uint8_t b1 = from & 0x1F;
  uint8_t r2 = (to >> 11) & 0x1F;
  uint8_t g2 = (to >> 5) & 0x3F;
  uint8_t b2 = to & 0x1F;
  uint8_t r = r1 + ((r2 - r1) * t) / 255;
  uint8_t g = g1 + ((g2 - g1) * t) / 255;
  uint8_t b = b1 + ((b2 - b1) * t) / 255;
  return (r << 11) | (g << 5) | b;
}

struct MatrixDrop {
  int x;
  int y;
  int speed;
  int len;
  char headChar;
  bool introParticipates;
  int introStartY;
  int introDelayMs;
  float introSpeedPxPerMs;
  bool introActive;
};

const int SCREEN10_WIDTH = 480;
const int SCREEN10_HEIGHT = 320;
const int MATRIX_COL_SPACING = 7;
const int numDrops = SCREEN10_WIDTH / MATRIX_COL_SPACING; // Tyle kolumn, ile siÄ™ mieĹ›ci
const char MATRIX_HEAD_TEXT[] = "ESP32   HAM   CLOCK   by   SP3KON";
const int MATRIX_HEAD_TEXT_LEN = (int)(sizeof(MATRIX_HEAD_TEXT) - 1);
MatrixDrop drops[numDrops];
bool matrixInitialized = false;

static inline char getMatrixHeadCharForColumn(int columnIdx) {
  if (MATRIX_HEAD_TEXT_LEN <= 0) {
    return ' ';
  }
  if (numDrops <= MATRIX_HEAD_TEXT_LEN) {
    return MATRIX_HEAD_TEXT[columnIdx];
  }

  const int freeCols = numDrops - MATRIX_HEAD_TEXT_LEN;
  const int leftPad = freeCols / 2;
  const int rightStart = leftPad + MATRIX_HEAD_TEXT_LEN;

  if (columnIdx < leftPad || columnIdx >= rightStart) {
    return ' ';
  }

  return MATRIX_HEAD_TEXT[columnIdx - leftPad];
}

const int SCREEN10_HEADER_H = 0;
const int SCREEN10_BODY_TOP = 0;  // PeĹ‚ny ekran matrix
const int SCREEN10_BODY_BOTTOM = 320;
const int SCREEN10_BODY_H = SCREEN10_BODY_BOTTOM - SCREEN10_BODY_TOP;
const int CLOCK_TEXT_SIZE = 6;
const int CLOCK_CHAR_W = 6;
const int CLOCK_CHAR_H = 8;
const unsigned long MATRIX_UPDATE_INTERVAL_MS = 120;
const unsigned long MATRIX_INTRO_ALIGN_MS = 3400UL;

unsigned long lastScreen10UpdateMs = 0;
unsigned long lastMatrixUpdateMs = 0;
bool screen10NeedsRedraw = true;
bool matrixIntroActive = false;
unsigned long matrixIntroStartMs = 0;
int clockMaskX = 0;
int clockMaskY = 0;
int clockMaskW = 0;
int clockMaskH = 0;
String lastClockText = "";
bool clockNeedsRedraw = true;

// ========== WYGASZACZ Z PORUSZAJÄ„CYM SIÄ ZEGAREM ==========
float bounceClockX = 240;  // Pozycja X zegara (Ĺ›rodek ekranu)
float bounceClockY = 160;  // Pozycja Y zegara (Ĺ›rodek ekranu)
float bounceClockVX = 2.5; // PrÄ™dkoĹ›Ä‡ X (piksele na klatkÄ™)
float bounceClockVY = 2.0; // PrÄ™dkoĹ›Ä‡ Y
const int BOUNCE_CLOCK_TEXT_SIZE = 4;  // Rozmiar tekstu zegara
const int BOUNCE_CLOCK_CHAR_W = 6 * BOUNCE_CLOCK_TEXT_SIZE;  // SzerokoĹ›Ä‡ znaku
const int BOUNCE_CLOCK_CHAR_H = 8 * BOUNCE_CLOCK_TEXT_SIZE;   // WysokoĹ›Ä‡ znaku
int bounceClockW = 0;  // SzerokoĹ›Ä‡ tekstu zegara (obliczana dynamicznie)
int bounceClockH = BOUNCE_CLOCK_CHAR_H;
uint16_t bounceClockColor = TFT_RADIO_ORANGE;
unsigned long lastBounceClockUpdateMs = 0;
const unsigned long BOUNCE_CLOCK_UPDATE_MS = 16; // ~60 FPS
String lastBounceClockText = "";

// Sprite dla zegara Matrix - eliminuje migotanie
TFT_eSprite clockSprite = TFT_eSprite(&tft);

static void resetMatrixDropRandom(int i) {
  drops[i].y = random(-SCREEN10_BODY_H, 0);
  drops[i].speed = random(5, 15);
  drops[i].len = random(4, 17);
  drops[i].headChar = (char)random(33, 126);
  drops[i].introActive = false;
}

static void prepareMatrixIntro() {
  const int charStep = 8;
  const int alignHeadY = SCREEN10_BODY_TOP + (SCREEN10_BODY_H / 2);

  for (int i = 0; i < numDrops; i++) {
    drops[i].headChar = getMatrixHeadCharForColumn(i);
    drops[i].introParticipates = (drops[i].headChar != ' ');
    drops[i].introStartY = random(-140, -40);
    drops[i].introDelayMs = random(0, 1900);
    drops[i].introActive = drops[i].introParticipates;

    int targetDropY = (alignHeadY - SCREEN10_BODY_TOP) - ((drops[i].len - 1) * charStep);
    int travelMs = (int)MATRIX_INTRO_ALIGN_MS - drops[i].introDelayMs;
    if (travelMs < 250) {
      travelMs = 250;
    }

    drops[i].introSpeedPxPerMs = (float)(targetDropY - drops[i].introStartY) / (float)travelMs;
    if (drops[i].introSpeedPxPerMs < 0.03f) {
      drops[i].introSpeedPxPerMs = 0.03f;
    }
  }

  matrixIntroActive = true;
  matrixIntroStartMs = millis();
}

String getUtcTimeString() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 1)) {
    char timeBuffer[9];
    strftime(timeBuffer, 9, "%H:%M:%S", &timeinfo);
    return String(timeBuffer);
  }
  return "--:--:--";
}

// Sprawdza czy obowiÄ…zuje czas letni w Europie (ostatnia niedziela marca - ostatnia niedziela paĹşdziernika)
bool isEuropeanDST(struct tm *timeinfo) {
  int month = timeinfo->tm_mon + 1;  // 1-12
  int day = timeinfo->tm_mday;       // 1-31
  int wday = timeinfo->tm_wday;      // 0=niedziela
  
  // StyczeĹ„, luty, listopad, grudzieĹ„ - zima
  if (month < 3 || month > 10) return false;
  // KwiecieĹ„-wrzesieĹ„ - lato
  if (month > 3 && month < 10) return true;
  
  // Marzec - sprawdĹş ostatniÄ… niedzielÄ™
  if (month == 3) {
    int lastSunday = 31 - ((31 - day + wday) % 7);
    return day > lastSunday || (day == lastSunday && timeinfo->tm_hour >= 1);
  }
  
  // PaĹşdziernik - sprawdĹş ostatniÄ… niedzielÄ™
  if (month == 10) {
    int lastSunday = 31 - ((31 - day + wday) % 7);
    return day < lastSunday || (day == lastSunday && timeinfo->tm_hour < 1);
  }
  
  return false;
}

bool getTimeWithTimezone(struct tm *outTm) {
  time_t now = time(nullptr);
  if (now < 100000) {
    return false;
  }
  // Pobierz czas UTC aby sprawdziÄ‡ DST
  struct tm utcTm;
  gmtime_r(&now, &utcTm);
  int dstOffset = isEuropeanDST(&utcTm) ? 1 : 0;  // +1h w czasie letnim
  now += (time_t)(timezoneHours + dstOffset) * 3600;
  gmtime_r(&now, outTm);
  return true;
}

String getTimezoneTimeString(const char *fmt, size_t bufSize) {
  struct tm timeinfo;
  if (!getTimeWithTimezone(&timeinfo)) {
    return "--:--";
  }
  char timeBuffer[16];
  strftime(timeBuffer, bufSize, fmt, &timeinfo);
  return String(timeBuffer);
}

String formatAprsTimeWithTimezone(String timeStr) {
  if (timeStr.endsWith("Z")) {
    timeStr.remove(timeStr.length() - 1);
  }
  if (timeStr.length() < 4) {
    return timeStr;
  }
  int hour = timeStr.substring(0, 2).toInt();
  int minute = timeStr.substring(2, 4).toInt();
  int hourLocal = hour + timezoneHours;
  while (hourLocal < 0) hourLocal += 24;
  while (hourLocal >= 24) hourLocal -= 24;
  String hh = (hourLocal < 10 ? "0" : "") + String(hourLocal);
  String mm = (minute < 10 ? "0" : "") + String(minute);
  return hh + ":" + mm;
}

String sanitizePolishToAscii(const String &input) {
  String out;
  out.reserve(input.length());
  const char *s = input.c_str();
  size_t len = input.length();
  for (size_t i = 0; i < len; i++) {
    uint8_t c = (uint8_t)s[i];
    if (c < 0x80) {
      out += (char)c;
      continue;
    }
    if (i + 1 >= len) {
      continue;
    }
    uint8_t c2 = (uint8_t)s[i + 1];
    // UTF-8 Polish letters
    if (c == 0xC4 && c2 == 0x84) { out += 'a'; i++; continue; } // Ä„
    if (c == 0xC4 && c2 == 0x85) { out += 'a'; i++; continue; } // Ä„
    if (c == 0xC4 && c2 == 0x86) { out += 'c'; i++; continue; } // Ä†
    if (c == 0xC4 && c2 == 0x87) { out += 'c'; i++; continue; } // Ä†
    if (c == 0xC4 && c2 == 0x98) { out += 'e'; i++; continue; } // Ä
    if (c == 0xC4 && c2 == 0x99) { out += 'e'; i++; continue; } // Ä
    if (c == 0xC5 && c2 == 0x81) { out += 'l'; i++; continue; } // Ĺ
    if (c == 0xC5 && c2 == 0x82) { out += 'l'; i++; continue; } // Ĺ‚
    if (c == 0xC5 && c2 == 0x83) { out += 'n'; i++; continue; } // Ĺ
    if (c == 0xC5 && c2 == 0x84) { out += 'n'; i++; continue; } // Ĺ„
    if (c == 0xC3 && c2 == 0x93) { out += 'o'; i++; continue; } // Ă“
    if (c == 0xC3 && c2 == 0xB3) { out += 'o'; i++; continue; } // Ăł
    if (c == 0xC5 && c2 == 0x9A) { out += 'S'; i++; continue; } // Ĺš
    if (c == 0xC5 && c2 == 0x9B) { out += 's'; i++; continue; } // Ĺ›
    if (c == 0xC5 && c2 == 0xB9) { out += 'z'; i++; continue; } // Ĺą
    if (c == 0xC5 && c2 == 0xBA) { out += 'z'; i++; continue; } // Ĺş
    if (c == 0xC5 && c2 == 0xBB) { out += 'z'; i++; continue; } // Ĺ»
    if (c == 0xC5 && c2 == 0xBC) { out += 'z'; i++; continue; } // ĹĽ
    // Unknown multibyte - skip
  }
  return out;
}

String getPolishDateStringFull() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 1)) {
    return "Brak daty";
  }

  static const char* weekdaysFull[] = {
    "Niedziela", "Poniedzialek", "Wtorek",
    "Sroda", "Czwartek", "Piatek", "Sobota"
  };

  String dateBuf = String(timeinfo.tm_mday < 10 ? "0" : "") + String(timeinfo.tm_mday) +
                   "." +
                   String((timeinfo.tm_mon + 1) < 10 ? "0" : "") + String(timeinfo.tm_mon + 1) +
                   "." + String(1900 + timeinfo.tm_year);

  String full = String(weekdaysFull[timeinfo.tm_wday]) + " " + dateBuf;
  return full;  // Zwracamy oryginalny tekst z polskimi znakami
}

String getEnglishDateStringFull() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 1)) {
    return "No date";
  }

  static const char* weekdaysFullEn[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday",
    "Thursday", "Friday", "Saturday"
  };

  String dateBuf = String(timeinfo.tm_mday < 10 ? "0" : "") + String(timeinfo.tm_mday) +
                   "." +
                   String((timeinfo.tm_mon + 1) < 10 ? "0" : "") + String(timeinfo.tm_mon + 1) +
                   "." + String(1900 + timeinfo.tm_year);

  String full = String(weekdaysFullEn[timeinfo.tm_wday]) + " " + dateBuf;
  return full;
}

String getPolishDateStringFullWithTimezone() {
  struct tm timeinfo;
  if (!getTimeWithTimezone(&timeinfo)) {
    return "Brak daty";
  }

  static const char* weekdaysFull[] = {
    "Niedziela", "Poniedzialek", "Wtorek",
    "Sroda", "Czwartek", "Piatek", "Sobota"
  };

  String dateBuf = String(timeinfo.tm_mday < 10 ? "0" : "") + String(timeinfo.tm_mday) +
                   "." +
                   String((timeinfo.tm_mon + 1) < 10 ? "0" : "") + String(timeinfo.tm_mon + 1) +
                   "." + String(1900 + timeinfo.tm_year);

  String full = String(weekdaysFull[timeinfo.tm_wday]) + " " + dateBuf;
  return full;  // Zwracamy oryginalny tekst z polskimi znakami
}

String getEnglishDateStringFullWithTimezone() {
  struct tm timeinfo;
  if (!getTimeWithTimezone(&timeinfo)) {
    return "No date";
  }

  static const char* weekdaysFullEn[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday",
    "Thursday", "Friday", "Saturday"
  };

  String dateBuf = String(timeinfo.tm_mday < 10 ? "0" : "") + String(timeinfo.tm_mday) +
                   "." +
                   String((timeinfo.tm_mon + 1) < 10 ? "0" : "") + String(timeinfo.tm_mon + 1) +
                   "." + String(1900 + timeinfo.tm_year);

  String full = String(weekdaysFullEn[timeinfo.tm_wday]) + " " + dateBuf;
  return full;
}

// Funkcja zwracajÄ…ca imieniny dla danego dnia (polskie imieniny)
String getPolishNameDay() {
  struct tm timeinfo;
  if (!getTimeWithTimezone(&timeinfo)) {
    return "";
  }
  
  // Tablica imienin dla kaĹĽdego dnia roku (miesiÄ…c 0-11, dzieĹ„ 1-31)
  // StyczeĹ„
  static const char* nameDays[12][31] = {
    {"Mieszko, MieczysĹ‚aw", "Izydor, Grzegorz", "Danuta, Genowefa", "Angelika, Aniela", "Edward, Szymon",
     "Kacper, Melchior, Baltazar", "Lucjan, Julian", "Seweryn, Marcin", "Marcelina, Julianna", "Danuta, Piotr",
     "Agata, MaĹ‚gorzata", "Arkadiusz, Benedykt", "BogumiĹ‚a, Wojciech", "Feliks, Nina", "PaweĹ‚, Arnold",
     "Marcel, WĹ‚odzimierz", "Antoni, Jan", "MaĹ‚gorzata, Piotr", "Henryk, Marta", "Fabian, Sebastian",
     "Agnieszka, JarosĹ‚aw", "Anastazy, Wincenty", "Ildefons, Rajmund", "Felicja, Tymoteusz", "PaweĹ‚, MichaĹ‚",
     "Paula, Wanda", "Aniela, Jerzy", "Walery, Karol", "Franciszek, JĂłzef", "Maciej, Martyna",
     "Joanna, Marceli"
    },
    // Luty
    {"Brygida, Ignacy", "Maria, MirosĹ‚aw", "BĹ‚aĹĽej, Oskar", "Andrzej, JĂłzef", "Agata, Jakub",
     "Dorota, PaweĹ‚", "Ryszard, Teodor", "Hieronim, Sebastian", "Apolonia, Cyryl", "Elwira, Scholastyka",
     "Lucjan, Olgierd", "Eulalia, RadosĹ‚aw", "Grzegorz, Leszek", "Krystyna, Liliana", "Jowita, Faustyn",
     "Danuta, Juliana", "Aleksy, Zbigniew", "Szymon, Konstancja", "Arnold, Konrad", "Leon, Ludomir",
     "Eleonora, Feliks", "Marta, MaĹ‚gorzata", "Roman, Damian", "Maciej, Marek", "Cezary, Donat",
     "MichaĹ‚, Tytus", "Aleksander, Gabriel", "Antoni, Romuald", "Baltazar, Kochany", "Marta, Geralda"
    },
    // Marzec
    {"Albina, Antonina", "Helena, Halszka", "Tomasz, Maryna", "Lucja, Kazimierz", "Adrian, Adriana",
     "RĂłĹĽa, Eugeniusz", "Tomasz, Felicyta", "Beatrycze, Wincenty", "Franciszka, Brunon", "Cyprian, Aleksander",
     "Ludwik, Konstantyn", "Grzegorz, Alojzy", "BoĹĽena, Patrycja", "Matylda, Lazarz", "Longin, Klemens",
     "Izabela, Oktawia", "Patryk, Zbigniew", "Cyryl, Edward", "JĂłzef, Bogdan", "Klaudia, Eufemia",
     "Lubomir, Benedykt", "Katarzyna, BogusĹ‚aw", "Pelagia, Oktawian", "Gabriel, Marek", "Mariola, WiÄ™nczysĹ‚aw",
     "Teodor, Olga", "Ernest, Lidia", "Aniela, Sykstus", "Wiktoryna, Jan", "Amelia, Dobromierz",
     "Beniamin, Leon"
    },
    // KwiecieĹ„
    {"GraĹĽyna, Teodor", "WĹ‚adysĹ‚aw, Franciszek", "Ryszard, Pankracy", "Izydor, Wojciech", "Katarzyna, Irena",
     "Izabela, Wilhelm", "Rufin, Celestyn", "Juliusz, Dionizy", "Maria, Dymitr", "MichaĹ‚, Makary",
     "Filip, Leon", "Juliusz, LubosĹ‚aw", "PrzemysĹ‚aw, Hermenegild", "Berenika, Tiburcy", "Wiktoryn, Anastazja",
     "Ksenia, Erwin", "Aniceta, Robert", "Alicja, BogusĹ‚aw", "Waleriana, Tytus", "CzesĹ‚aw, Agnieszka",
     "Anselm, JarosĹ‚aw", "Kaja, Leon", "Jerzy, Wojciech", "Grzegorz, PaweĹ‚", "Erwina, Marek",
     "Marzena, Klaudiusz", "Zygmunt, Florian", "Piotr, Waleria", "Rita, Bogna", "Katarzyna, Marian",
     "Lidia, TrzebomysĹ‚"
    },
    // Maj
    {"JĂłzef, Jeremiasz", "Zygmunt, Anatolia", "Maria, Mariola", "Monika, Florian", "WĹ‚odzimierz, Iryda",
     "Judyta, Filip", "Benedykt, Gizela", "StanisĹ‚aw, Lizbona",     "Ĺukasz, BoĹĽena", "Antonin, Izydor",
     "Iga, Maja", "Joanna, Pankracy", "Glora, Roberta", "Maciej, Bonifacy", "Zofia, Izydora",
     "JÄ™drzej, JÄ™drzej", "Brunon, SĹ‚awomir", "Aleksandra, Eryk", "Iwa, Piotr", "Bernard, Aleksandra",
     "Jerzy, Jan", "Wiktoria, Julia", "Emilia, MichaĹ‚", "Joanna, Zuzanna", "Grzegorz, MaĹ‚gorzata",
     "Filip, Paulina", "Augustyn, Julian", "Jaromir, Justyna", "Magdalena, ZdisĹ‚awa", "Ferdynand, Karolina",
     "Aniela, Petronela"
    },
    // Czerwiec
    {"Justyn, Konrad", "Erazm, Marzenna", "Leszek, Tamara", "Katarzyna, Franciszek", "Walter, Bonifacy",
     "Natalia, Saba", "Antoni, Robert", "Maksym, Seweryn", "Feliks, Pelagia", "Bogdan, MaĹ‚gorzata",
     "Barnaba, RadomiĹ‚", "Janina, Jan", "Antoni, Lucylla", "Eliza, Walenty", "Witold, Jolanta",
     "Alina, Benon", "Albert, Ignacy", "Marek, ElĹĽbieta", "Gerwazy, Protazy", "Dina, Bogna",
     "Alicja, Alojzy", "Paulina, Tomasz", "Wanda, Zenon", "Jan, Danuta", "WĹ‚adysĹ‚aw, Dorota",
     "Piotr, PaweĹ‚", "Maryla, WĹ‚adysĹ‚awa", "Leon, Ireneusz", "Piotr, PaweĹ‚", "Emilia, Lucyna",
     "Oktawia, Oktawian"
    },
    // Lipiec
    {"Halina, Marian", "Jagoda, Maria", "Anatol, Tomasz", "ElĹĽbieta, Odon", "Antoni, Maria",
     "Dominika, Gotard", "Cyryl, Metody", "Edgar, ElĹĽbieta", "Ĺucjan, Weronika", "Sylwia, Witalis",
     "Olga, Kalina", "Jan, Brunon", "Henryk, Kinga", "Bonawentura, Kamil", "Dawid, Henryk",
     "Eustachy, Maria", "Aneta, Bogdan", "Emil, Erwin", "Wincenty, WodzisĹ‚aw", "CzesĹ‚aw, Eliasz",
     "Piotr, Wiktor", "Magdalena, WĹ‚adysĹ‚awa", "Ilona, Stefan", "Kinga, Krystyna", "Walentyna, Krzysztof",
     "Anna, MirosĹ‚awa", "Aurelia, Malwina", "Lilia, Innocenty", "Olaf, Marta", "Julita, LudmiĹ‚a",
     "Ignacy, Lubomir"
    },
    // SierpieĹ„
    {"Nadzieja, Piotr", "Karina, Gustaw", "Lidia, Nikodem", "Dominik, Protus", "Emil, Karol",
     "SĹ‚awomir, Jan", "Kajetan, Albert", "Cyprian, Emiliana", "Roman, Ryszard", "Borys, Wawrzyniec",
     "Klara, Zuzanna", "Lech, Euzebiusz", "Hildegarda, Diana", "Alfred, Euzebiusz", "Maria, Napoleon",
     "Roch, Stefan", "Anita, Eliza", "Klara, Helena",     "Jan, BolesĹ‚aw", "Bernard, Samuel",
     "Joanna, Franciszek", "Maria, Cezary", "RĂłĹĽa, Apolinary", "Jerzy, Bartosz", "Luiza, Ludwik",
     "Maria, Zefiryn", "JĂłzef, Klaudiusz", "Patrycja, Wyszomir", "Beata, Jan", "RĂłĹĽa, SzczÄ™sny",
     "Ramona, Rajmund"
    },
    // WrzesieĹ„
    {"Ida, BronisĹ‚aw", "Stefan, Wilhelm", "Grzegorz, Izabela", "Ida, Julian", "Dorota, Wawrzyniec",
     "Beata, Eugeniusz", "Regina, Melchior", "Mariam, Adriana", "Piotr, Sergiusz", "ElĹĽbieta, Gordian",
     "Jacek, Protus", "Gwido, Maria", "Eugenia, Aureliusz", "Bernard, Roksana", "Albin, Nikodem",
     "Edyta, Kornel", "Franciszek, Hildegarda", "Irma, StanisĹ‚aw", "Jan, Konstancja", "Filipina, Eustachy",
     "Jonasz, Mateusz", "Tomasz, Maurycy", "Tekla, BogusĹ‚aw", "Gerard, Herman", "Aurelia, LadysĹ‚aw",
     "WĹ‚adysĹ‚aw, Ewa", "Damian, Kosma", "WacĹ‚aw, Laurencjusz", "MichaĹ‚, RafaĹ‚, Gabriela", "Hieronim, Remigiusz"
    },
    // PaĹşdziernik
    {"Danuta, Remigiusz", "Teofil, Dionizy", "Teresa, Heliodor", "Edwin, Franciszek", "Igor, Placyd",
     "Artur, Brunon", "Maria, Marek", "Pelagia, Brygida", "Arnold, Dionizy", "Paulina, Franciszek",
     "Emil, Aldona", "Maksymilian, Eustachy", "Edward, Gerald", "Ĺukasz, Manfred", "Teresa, Jadwiga",
     "Gall, Florian", "Ignacy, Rudolfa", "Ĺukasz, Julian", "Ziemowit, Jadwiga", "Irena, Jan",
     "Urszula, Hilaria", "Filip, Marek", "Marlena, Seweryn", "RafaĹ‚, Marcin", "Beata, Daria",
     "Ĺucjan, Ewaryst", "Iwona, Sabina", "Szymon, Juda", "Euzebia, Wioletta", "Zenobia, PrzemysĹ‚aw",
     "Urbain, Saturnin"
    },
    // Listopad
    {"Seweryn, Wiktoryna", "Bohdan, BoĹĽydar", "Sylwia, Marcin", "Karol, Wiktor", "ElĹĽbieta, Zachariasz",
     "Feliks, Leonard", "Antoni, Ernest", "Seweryn, Bogdan", "Aleksander, Ludwik", "Lena, Marcin",
     "SzczÄ™sny, Leon", "Marcin, Witold", "MikoĹ‚aj, StanisĹ‚aw", "Emilian, Wawrzyniec", "Albert, Leopold",
     "Gertruda, Edmund", "Grzegorz, Salomea", "Klaudyna, Karolina", "ElĹĽbieta, Seweryn", "Anatol, RafaĹ‚",
     "Albertina, Janusz", "Cecylia, Felicyta", "Adela, Klemens", "Flora, Emma", "Katarzyna, Erazm",
     "Delfina, Sylwester", "Wirgiliusz, Walery", "Jakub, ZdzisĹ‚aw", "Fryderyk, Zygmunt", "Andrzej, Seweryn",
     ""
    },
    // GrudzieĹ„
    {"Eligiusz, Natalia", "Bibiana, Balbina", "Franciszek, Ksawery", "Barbara, Krystian", "Norbert, Wawrzyniec",
     "Jadwiga, Jan", "Marcin, AmbroĹĽy", "Maria, ĹšwiÄ™tosĹ‚awa", "WiesĹ‚aw, Leokadia", "Daniel, Bogdan",
     "Damaszek, Waldemar", "Dagmara, Aleksandra", "Ĺucja, Otylia", "Alfred, Izydor", "Nina, Celina",
     "Albina, Olimpia", "Lazarz, Florian", "Gracjan, BogusĹ‚aw", "MichaĹ‚, Dariusz", "BogumiĹ‚a, Dominik",
     "TomisĹ‚aw, Honorata", "Zenon, Honorata", "Wiktor, Seweryn", "Ewa, Adam", "Anastazja, Eugenia",
     "Dionizy, Szczepan", "Jan, Zenobiusz", "Teofila, GodzisĹ‚aw", "Marta, Tomasz", "Rainer, Eugeniusz",
     "Sylwester, Melania"
    }
  };
  
  int month = timeinfo.tm_mon;  // 0-11
  int day = timeinfo.tm_mday - 1;  // 0-30
  
  if (month >= 0 && month < 12 && day >= 0 && day < 31) {
    String nameDay = nameDays[month][day];
    if (nameDay.length() > 0) {
      return String("Imieniny: ") + nameDay;
    }
  }
  return "";
}

// Rysuje liniÄ™ daty/tygodnia wyĹ›rodkowanÄ… - zaktualizowana dla 480x320
void drawDateLine(const String &dateText) {
  const int screenW = 480;
  const int timeY = 122;  // Taka sama jak w drawHamClock
  const int dateY = timeY + 84;  // Taka sama jak w drawHamClock

  // CzyĹ›Ä‡ obszar daty (szeroki prostokÄ…t na caĹ‚ej szerokoĹ›ci)
  tft.fillRect(30, dateY - 2, 420, 24, TFT_BLACK);
  
  // Rysuj datÄ™ wyĹ›rodkowanÄ… - POWIÄKSZONA (TextSize 3 jak w drawHamClock)
  tft.setTextSize(3);
  tft.setTextColor(TFT_WHITE);
  int dateWidth = dateText.length() * 18; // ~18px na znak przy TextSize 3
  int dateX = (screenW - dateWidth) / 2;
  tft.setCursor(dateX > 10 ? dateX : 10, dateY);
  tft.print(dateText);
}

// Funkcja rysujÄ…ca paski sygnaĹ‚u WiFi (belki jak w telefonie)
// x, y - pozycja lewego gĂłrnego rogu
// strength - siĹ‚a sygnaĹ‚u 0-4 (0=brak, 4=max)
void drawWifiSignalBars(int x, int y, int strength = -1) {
  // Automatyczna detekcja siĹ‚y sygnaĹ‚u jeĹ›li nie podano
  if (strength < 0) {
    if (!wifiConnected || WiFi.status() != WL_CONNECTED) {
      strength = 0;
    } else {
      int rssi = WiFi.RSSI();
      if (rssi > -50) strength = 4;
      else if (rssi > -60) strength = 3;
      else if (rssi > -70) strength = 2;
      else if (rssi > -80) strength = 1;
      else strength = 0;
    }
  }
  
  // Kolory dla belek WiFi - gradient od czerwonego do zielonego
  uint16_t bar1Color = TFT_RED;       // Belka 1 (sĹ‚aby sygnaĹ‚)
  uint16_t bar2Color = TFT_ORANGE;    // Belka 2
  uint16_t bar3Color = TFT_YELLOW;    // Belka 3
  uint16_t bar4Color = TFT_GREEN;     // Belka 4 (mocny sygnaĹ‚)
  uint16_t noSignalColor = TFT_RED;   // Brak zasiÄ™gu
  uint16_t emptyColor = TFT_DARKGREY; // Puste belki
  
  // 4 belki o rosnÄ…cej wysokoĹ›ci w rĂłĹĽnych kolorach (gradient)
  // Belka 1 (najniĹĽsza) - CZERWONA
  if (strength >= 1) {
    tft.fillRect(x, y + 12, 6, 4, bar1Color);
  } else if (strength == 0) {
    tft.fillRect(x, y + 12, 6, 4, noSignalColor);
  } else {
    tft.drawRect(x, y + 12, 6, 4, emptyColor);
  }
  
  // Belka 2 - POMARAĹCZOWA
  if (strength >= 2) {
    tft.fillRect(x + 8, y + 8, 6, 8, bar2Color);
  } else if (strength == 0) {
    tft.fillRect(x + 8, y + 8, 6, 8, noSignalColor);
  } else {
    tft.drawRect(x + 8, y + 8, 6, 8, emptyColor);
  }
  
  // Belka 3 - Ĺ»Ă“ĹTA
  if (strength >= 3) {
    tft.fillRect(x + 16, y + 4, 6, 12, bar3Color);
  } else if (strength == 0) {
    tft.fillRect(x + 16, y + 4, 6, 12, noSignalColor);
  } else {
    tft.drawRect(x + 16, y + 4, 6, 12, emptyColor);
  }
  
  // Belka 4 (najwyĹĽsza) - ZIELONA
  if (strength >= 4) {
    tft.fillRect(x + 24, y, 6, 16, bar4Color);
  } else if (strength == 0) {
    tft.fillRect(x + 24, y, 6, 16, noSignalColor);
  } else {
    tft.drawRect(x + 24, y, 6, 16, emptyColor);
  }
}

// Funkcja rysujÄ…ca ikonÄ™ baterii
// x, y - pozycja lewego gĂłrnego rogu
// percentage - procent naĹ‚adowania 0-100
void drawBatteryIcon(int x, int y, int percentage = -1) {
  // JeĹ›li napiÄ™cie baterii jest 0.00V lub poniĹĽej 0.1V, nie rysuj ikony
  if (batteryVoltage <= 0.1f) {
    return;
  }
  
  // Automatyczna detekcja procentu jeĹ›li nie podano
  if (percentage < 0) {
    percentage = batteryPercentage;
  }
  
  // Ograniczenie zakresu
  if (percentage < 0) percentage = 0;
  if (percentage > 100) percentage = 100;
  
  // Wymiary ikony baterii
  const int batteryW = 20;
  const int batteryH = 10;
  const int terminalW = 3;
  const int terminalH = 6;
  
  // Kolor zaleĹĽny od poziomu naĹ‚adowania
  uint16_t batteryColor;
  if (percentage <= 20) {
    batteryColor = TFT_RED;      // Krytyczny
  } else if (percentage <= 50) {
    batteryColor = TFT_ORANGE;   // Niski
  } else if (percentage <= 75) {
    batteryColor = TFT_YELLOW;   // Ĺšredni
  } else {
    batteryColor = TFT_GREEN;    // Wysoki
  }
  
  // Rysuj obudowÄ™ baterii
  tft.drawRect(x, y + 2, batteryW, batteryH, TFT_WHITE);
  
  // Rysuj terminal baterii (maĹ‚y wystÄ™p po prawej stronie)
  tft.fillRect(x + batteryW, y + 4, terminalW, terminalH, TFT_WHITE);
  
  // Rysuj poziom naĹ‚adowania (wewnÄ…trz obudowy)
  int fillWidth = (batteryW - 4) * percentage / 100;
  if (fillWidth > 0) {
    tft.fillRect(x + 2, y + 4, fillWidth, batteryH - 4, batteryColor);
  }
}

void updateScreen1HeaderClock() {
  // Nowy uklad HAM CLOCK: zegar jest analogowy na tarczy, brak zegara w naglowku.
  return;
}

void updateDxClusterClock() {
  // ZEGAR WYĹÄ„CZONY - usuniÄ™ty z nagĹ‚Ăłwka
  return;
}

void updateWeatherClock() {
  // ZEGAR WYĹÄ„CZONY - usuniÄ™ty z nagĹ‚Ăłwka
  return;
}

void updateWeatherForecastClock() {
  // ZEGAR WYĹÄ„CZONY - usuniÄ™ty z nagĹ‚Ăłwka
  return;
}

void updateBandInfoClock() {
  // ZEGAR WYĹÄ„CZONY - usuniÄ™ty z nagĹ‚Ăłwka
  return;
}

void updateHamalertClock() {
  // ZEGAR WYĹÄ„CZONY - usuniÄ™ty z nagĹ‚Ăłwka
  return;
}

void updatePotaClusterClock() {
  // ZEGAR WYĹÄ„CZONY - usuniÄ™ty z nagĹ‚Ăłwka
  return;
}

void updateScreen1Header() {
  if (!tftInitialized || currentScreen != SCREEN_HAM_CLOCK || inMenu) {
    return;
  }
  if (!screen1HeaderNeedsRedraw) {
    return;
  }
  uiHeader("ESP32-HAM-CLOCK", false);
  uiHeaderClock();
  screen1HeaderNeedsRedraw = false;
}

// Naglowek w stylu starszej wersji (v1.4.3): pomaranczowa belka, logo
// "ESP32" w czarnej obwodce + "-HAM-CLOCK", zegar HH:MM, paski WiFi i bateria.
static void updateScreen1HeaderClassic() {
  if (!tftInitialized || currentScreen != SCREEN_HAM_CLOCK || inMenu) {
    return;
  }
  if (!screen1HeaderNeedsRedraw) {
    return;
  }

  const int headerH = 40;
  const int screenW = 480;
  tft.fillRect(0, 0, screenW, headerH, TFT_RADIO_ORANGE);
  tft.drawFastHLine(0, headerH - 1, screenW, 0x8410);

  String logoPart1 = "ESP32";
  String logoPart2 = "-HAM-CLOCK";
  tft.setTextSize(2);
  int part1W = tft.textWidth(logoPart1);
  int part2W = tft.textWidth(logoPart2);
  int startX = (screenW - (part1W + part2W)) / 2;
  int textY = 12;
  tft.fillRect(startX - 6, textY - 3, part1W + 12, 22, TFT_BLACK);
  tft.setTextColor(TFT_RADIO_ORANGE, TFT_BLACK);
  tft.setCursor(startX, textY);
  tft.print(logoPart1);
  tft.setTextColor(TFT_BLACK, TFT_RADIO_ORANGE);
  tft.setCursor(startX + part1W, textY);
  tft.print(logoPart2);

  drawWifiSignalBars(screenW - 45, 8);
  drawBatteryIcon(screenW - 70, 8);

  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 1)) {
    char timeBuffer[6];
    strftime(timeBuffer, 6, "%H:%M", &timeinfo);
    String timeStr = String(timeBuffer);
    tft.setTextSize(2);
    int timeX = 10, timeY = 12;
    tft.fillRect(timeX - 2, timeY - 2, tft.textWidth(timeStr) + 4, 20, TFT_RADIO_ORANGE);
    tft.setTextColor(TFT_BLACK, TFT_RADIO_ORANGE);
    tft.setCursor(timeX, timeY);
    tft.print(timeStr);
  }

  screen1HeaderNeedsRedraw = false;
}

// ================= EKRAN 1: HAM CLOCK =================
// Styl wg wzorca: czarne tlo, cyjanowy naglowek wysrodkowany z linia,
// trzy panele (globus / zegar analogowy / widmo+wodospad 20 m) oraz dolna
// ramka informacyjna (cyjanowa obwodka, zolte linie wysrodkowane).
static const int HC_HLINE_Y = 36;
static const int HC_PANEL_Y = 42, HC_PANEL_H = 164;
static const int HC_GLOBE_X = 15,  HC_GLOBE_W = 120;
static const int HC_CLOCK_X = 150, HC_CLOCK_W = 172;
static const int HC_SPEC_X = 332,  HC_SPEC_W = 134;
static const int HC_CLOCK_CX = 236, HC_CLOCK_CY = 120, HC_CLOCK_R = 58;
static const int HC_GLOBE_CX = 75,  HC_GLOBE_CY = 118, HC_GLOBE_R = 46;
static const int HC_SPEC_BAR_Y = 62,  HC_SPEC_BAR_H = 48;
static const int HC_SPEC_WF_Y = 114,  HC_SPEC_WF_H = 70;
static const int HC_INFO_X = 10, HC_INFO_Y = 210, HC_INFO_W = 460, HC_INFO_H = 84;

// Uklad wg kodu wzorcowego (drawUI): naglowek cyjanowy, linia, ramka 300x85.
static const int HC_CODE_HDR_Y = 8;
static const int HC_CODE_HLINE_X = 20, HC_CODE_HLINE_Y = 35, HC_CODE_HLINE_W = 280;
static const int HC_CODE_FRAME_X = 10, HC_CODE_FRAME_Y = 150;
static const int HC_CODE_FRAME_W = 300, HC_CODE_FRAME_H = 85;
static const int HC_CODE_FRAME_CX = HC_CODE_FRAME_X + HC_CODE_FRAME_W / 2;  // 160

// Paleta (RGB565) - zgodna ze wzorcem
static const uint16_t HC_BG        = 0x0000;  // czarny
static const uint16_t HC_PANEL     = 0x10A2;  // ciemny grafit
static const uint16_t HC_CYAN      = 0x07FF;  // blekit
static const uint16_t HC_YELLOW    = 0xFFE0;  // zolty
static const uint16_t HC_WHITE     = 0xFFFF;  // bialy
static const uint16_t HC_TEXT      = 0xE73C;  // jasnoszary
static const uint16_t HC_BEZEL_LT  = 0x39E7;
static const uint16_t HC_BEZEL_DK  = 0x18E3;
static const uint16_t HC_GOLD      = 0xFEA0;

static uint16_t hamClockWf[HC_SPEC_WF_H][HC_SPEC_W];
static bool hamClockWfInit = false;

// Fazowana ramka panelu: ciemna dolna/prawa krawedz + jasna gorna/lewa.
static void hamClockBevel(int x, int y, int w, int h) {
  tft.drawRoundRect(x, y, w, h, 6, HC_BEZEL_DK);
  tft.drawFastHLine(x + 4, y + 1, w - 8, HC_BEZEL_LT);
  tft.drawFastVLine(x + 1, y + 4, h - 8, HC_BEZEL_LT);
  tft.drawFastHLine(x + 4, y + h - 2, w - 8, HC_BEZEL_DK);
  tft.drawFastVLine(x + w - 2, y + 4, h - 8, HC_BEZEL_DK);
}

// Tytul panelu: cyjanowy znacznik + tekst + cienka linia.
static void hamClockPanelTitle(int x, int y, int w, const char *title) {
  tft.fillRect(x + 6, y + 4, 3, 9, HC_CYAN);
  tft.setTextSize(1);
  tft.setTextColor(HC_CYAN, HC_PANEL);
  tft.setCursor(x + 12, y + 4);
  tft.print(title);
  tft.drawFastHLine(x + 6, y + 16, w - 12, HC_BEZEL_LT);
}

static uint8_t hamClockNoise(int bin, uint32_t t) {
  uint32_t v = (uint32_t)bin * 2654435761u + (t & 0xFFFFu) * 40503u;
  v ^= v >> 15; v *= 2246822519u; v ^= v >> 13;
  return (uint8_t)((v >> 8) & 0x3F);
}

// Amplituda 0..100 dla bitu widma. Piki przy aktywnych czestotliwosciach
// pasma 20 m (CW 14.060, FT8 14.074, FT4 14.090, SSB 14.200), powolny dryf.
static uint8_t hamClockSpectrumAmp(int bin, uint32_t t) {
  const float peaks[4] = {14.060f, 14.074f, 14.090f, 14.200f};
  float freq = 14.000f + (bin / (float)(HC_SPEC_W - 1)) * 0.350f;
  int amp = hamClockNoise(bin, t) / 2;
  for (int p = 0; p < 4; p++) {
    float drift = 0.004f * sinf((float)t / 40.0f + p * 1.7f);
    float d = freq - (peaks[p] + drift);
    amp += (int)(expf(-(d * d) / 0.0032f) * (68 - p * 9));
  }
  if (amp > 100) amp = 100;
  if (amp < 3) amp = 3;
  return (uint8_t)amp;
}

static uint16_t hamClockSpecColor(uint8_t a) {
  if (a < 50) return rgb565(0, (uint8_t)(120 + a), 60);
  if (a < 80) return rgb565((uint8_t)((a - 50) * 8), 220, 40);
  return rgb565(255, (uint8_t)(220 - (a - 80) * 8), 40);
}

static uint16_t hamClockWfColor(uint8_t a) {
  int r = (a > 60) ? (a - 60) * 4 : 0;
  int g = a * 2;
  int b = (a < 40) ? (120 - a * 2) : 0;
  if (r > 255) r = 255;
  if (g > 255) g = 255;
  if (b < 0) b = 0;
  return rgb565((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

// Rzutuje lat/lon na tarcze globusa (rownoodleglosciowo, przyciete do kola).
static void hamClockGlobeMarker(double lat, double lon, uint16_t col) {
  if (lat < -90 || lat > 90 || lon < -180 || lon > 180) return;
  int x = HC_GLOBE_CX + (int)((lon / 180.0) * HC_GLOBE_R * 0.92);
  int y = HC_GLOBE_CY - (int)((lat / 90.0) * HC_GLOBE_R * 0.92);
  int dx = x - HC_GLOBE_CX, dy = y - HC_GLOBE_CY;
  int lim = HC_GLOBE_R - 3; lim *= lim;
  if (dx * dx + dy * dy > lim) return;
  tft.fillCircle(x, y, 3, col);
}

// Wskazowki zegara + medalion srodka (bez tarczy) - do czestych aktualizacji.
static void hamClockDrawHands(const struct tm &t) {
  const int cx = HC_CLOCK_CX, cy = HC_CLOCK_CY, r = HC_CLOCK_R;
  float hAng = ((t.tm_hour % 12) * 30 + t.tm_min * 0.5 - 90) * PI / 180.0f;
  float mAng = (t.tm_min * 6 - 90) * PI / 180.0f;
  float sAng = (t.tm_sec * 6 - 90) * PI / 180.0f;

  int hx = cx + (int)(r * 0.50 * cosf(hAng));
  int hy = cy + (int)(r * 0.50 * sinf(hAng));
  tft.drawLine(cx, cy, hx, hy, HC_WHITE);
  tft.drawLine(cx + 1, cy, hx + 1, hy, HC_WHITE);
  tft.drawLine(cx, cy + 1, hx, hy + 1, HC_WHITE);

  int mx = cx + (int)(r * 0.74 * cosf(mAng));
  int my = cy + (int)(r * 0.74 * sinf(mAng));
  tft.drawLine(cx, cy, mx, my, HC_CYAN);
  tft.drawLine(cx + 1, cy, mx + 1, my, HC_CYAN);
  tft.drawLine(cx, cy + 1, mx, my + 1, HC_CYAN);

  int sx = cx + (int)(r * 0.88 * cosf(sAng));
  int sy = cy + (int)(r * 0.88 * sinf(sAng));
  tft.drawLine(cx, cy, sx, sy, HC_YELLOW);

  tft.fillCircle(cx, cy, 6, HC_CYAN);
  tft.fillCircle(cx, cy, 4, HC_BG);
  tft.fillCircle(cx, cy, 2, HC_YELLOW);
}

// ================= EKRAN 1: STARSZA WERSJA (v1.4.3) =================
// Klasyczny uklad: pomaranczowy naglowek z logo/klockiem/WiFi/bateria,
// ramka ze znakiem + duzy zegar cyfrowy + data + imieniny, adres IP i strzalki.
void drawHamClockClassic() {
  tft.fillScreen(TFT_BLACK);
  screen1HeaderNeedsRedraw = true;

  const int screenW = 480;
  const int screenH = 320;

  updateScreen1HeaderClassic();

  // Ramka wokol znaku, zegara i daty.
  int clockFrameX = 30;
  int clockFrameW = 420;
  int clockFrameY = 45;
  int clockFrameH = 220;
  tft.drawRoundRect(clockFrameX, clockFrameY, clockFrameW, clockFrameH, 10, TFT_DARKGREY);

  // Znak (callsign) - wysrodkowany w ramce.
  tft.setTextColor(callsignColor);
  tft.setTextSize(5);
  String callsign = (userCallsign.length() > 0) ? userCallsign : String(DEFAULT_CALLSIGN);
  callsign.toUpperCase();
  int callWidth = tft.textWidth(callsign);
  int callX = (screenW - callWidth) / 2;
  int callY = 55;
  tft.setCursor(max(10, callX), callY);
  tft.print(callsign);
  callsignHitX = max(10, callX) - 10;
  callsignHitY = callY - 5;
  callsignHitW = callWidth + 20;
  callsignHitH = 50;

  tft.setTextSize(1);
  tft.setTextColor(TFT_RADIO_ORANGE, TFT_BLACK);
  String opText = "OPERATOR STATION";
  tft.setCursor((screenW - tft.textWidth(opText)) / 2, callY + 40);
  tft.print(opText);

  // Duzy zegar cyfrowy.
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(7);
  String timeStr = (screen1TimeMode == SCREEN1_TIME_LOCAL)
                     ? getTimezoneTimeString("%H:%M:%S", 9)
                     : getUtcTimeString();
  int timeY = 130;
  tft.setCursor(max(10, (screenW - tft.textWidth(timeStr)) / 2), timeY);
  tft.print(timeStr);

  tft.setTextSize(1);
  const char *timeLabel = "UTC";
  if (screen1TimeMode == SCREEN1_TIME_LOCAL) {
    timeLabel = (tftLanguage == TFT_LANG_EN) ? "Local Time" : "Czas Lokalny";
  }
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor((screenW - tft.textWidth(timeLabel)) / 2, timeY + 66);
  tft.print(timeLabel);

  // Data (pelna, z polskimi znakami przez font VLW).
  String dateText;
  struct tm timeinfo;
  bool timeValid = (screen1TimeMode == SCREEN1_TIME_LOCAL)
                     ? getTimeWithTimezone(&timeinfo)
                     : getLocalTime(&timeinfo, 1);
  if (!timeValid || timeinfo.tm_year < 125) {
    dateText = "...";
  } else if (screen1TimeMode == SCREEN1_TIME_LOCAL) {
    dateText = (tftLanguage == TFT_LANG_EN) ? getEnglishDateStringFullWithTimezone()
                                            : getPolishDateStringFullWithTimezone();
  } else {
    dateText = (tftLanguage == TFT_LANG_EN) ? getEnglishDateStringFull()
                                            : getPolishDateStringFull();
  }

  int dateY = timeY + 84;
  if (littleFsReady && LittleFS.exists(ROBOTO_FONT20_FILE) && ESP.getFreeHeap() > 80000) {
    tft.loadFont(ROBOTO_FONT20_NAME, LittleFS);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(max(10, (screenW - tft.textWidth(dateText)) / 2), dateY);
    tft.print(dateText);
    tft.unloadFont();
  } else {
    tft.setTextSize(3);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    String ascii = sanitizePolishToAscii(dateText);
    tft.setCursor(max(10, (screenW - tft.textWidth(ascii)) / 2), dateY);
    tft.print(ascii);
  }

  // Imieniny (tylko PL).
  if (tftLanguage != TFT_LANG_EN) {
    String nameDayText = getPolishNameDay();
    if (nameDayText.length() > 0) {
      int nameDayY = dateY + 30;
      if (littleFsReady && LittleFS.exists(ROBOTO_FONT12_FILE) && ESP.getFreeHeap() > 80000) {
        tft.loadFont(ROBOTO_FONT12_NAME, LittleFS);
        tft.setTextColor(TFT_RADIO_ORANGE, TFT_BLACK);
        tft.setCursor(max(10, (screenW - tft.textWidth(nameDayText)) / 2), nameDayY);
        tft.print(nameDayText);
        tft.unloadFont();
      } else {
        tft.setTextSize(2);
        tft.setTextColor(TFT_RADIO_ORANGE, TFT_BLACK);
        String ascii = sanitizePolishToAscii(nameDayText);
        int x = max(10, (screenW - tft.textWidth(ascii)) / 2);
        tft.setCursor(x, nameDayY);
        tft.print(ascii);
      }
    }
  }

  // Adres IP (dolny pasek).
  IPAddress ip;
  bool connected = false;
  if (wifiConnected && WiFi.status() == WL_CONNECTED) {
    ip = WiFi.localIP();
    connected = true;
  } else if (WiFi.getMode() & WIFI_AP) {
    ip = WiFi.softAPIP();
    connected = true;
  }
  tft.setTextSize(2);
  String ipStr = connected ? ip.toString() : String("NO CONNECTION");
  tft.setTextColor(connected ? TFT_LIGHTGREY : TFT_RED, TFT_BLACK);
  tft.setCursor((screenW - tft.textWidth(ipStr)) / 2, screenH - 20);
  tft.print(ipStr);

  // Strzalki nawigacyjne.
  int arrowY = screenH - 30;
  int arrowSize = 12;
  tft.fillTriangle(15, arrowY, 15 + arrowSize, arrowY - arrowSize, 15 + arrowSize, arrowY + arrowSize, TFT_RADIO_ORANGE);
  tft.fillTriangle(screenW - 15, arrowY, screenW - 15 - arrowSize, arrowY - arrowSize, screenW - 15 - arrowSize, arrowY + arrowSize, TFT_RADIO_ORANGE);

#ifdef BATTERY_MONITORING_ENABLED
  drawBatteryQuickUpdate(true);
#endif
}

// Aktualizacja duzego zegara cyfrowego (styl klasyczny).
static void updateScreen1ClockClassic() {
  if (!tftInitialized || currentScreen != SCREEN_HAM_CLOCK || inMenu) {
    return;
  }
  const int screenW = 480;
  const int timeY = 130;
  String timeStr = (screen1TimeMode == SCREEN1_TIME_LOCAL)
                     ? getTimezoneTimeString("%H:%M:%S", 9)
                     : getUtcTimeString();
  int timeX = (screenW - timeStr.length() * 42) / 2;
  if (timeX < 10) timeX = 10;
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(7);
  tft.setTextPadding(8 * 42 + 10);
  tft.setCursor(timeX, timeY);
  tft.print(timeStr);
  tft.setTextPadding(0);
}

// ================= EKRAN 1: ZEGAR w stylu nowoczesnym =================
// Wyglad jak w wersji DOMYSLNEJ (klasycznej): naglowek z logo, zegarem HH:MM,
// WiFi i bateria; znak, "OPERATOR STATION", duzy zegar cyfrowy, data, imieniny,
// adres IP i strzalki - ale w stylu nowoczesnym: czarny naglowek z bialym
// napisem "ESP32-HAM-CLOCK", czarne tlo, cyjanowa ramka i zolty zegar.
static void hamClockModernHeader() {
  const int headerH = 40;
  const int screenW = 480;
  tft.fillRect(0, 0, screenW, headerH, HC_BG);
  tft.drawFastHLine(0, headerH - 1, screenW, HC_CYAN);

  String logo = "ESP32-HAM-CLOCK";
  tft.setTextSize(2);
  tft.setTextColor(HC_WHITE, HC_BG);
  tft.setCursor((screenW - tft.textWidth(logo)) / 2, 12);
  tft.print(logo);

  drawWifiSignalBars(screenW - 45, 8);
  drawBatteryIcon(screenW - 70, 8);

  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 1)) {
    char timeBuffer[6];
    strftime(timeBuffer, 6, "%H:%M", &timeinfo);
    String timeStr = String(timeBuffer);
    tft.setTextSize(2);
    tft.setTextColor(HC_CYAN, HC_BG);
    tft.setCursor(10, 12);
    tft.print(timeStr);
  }
}

static void hamClockModernCallsign() {
  tft.setTextColor(callsignColor);
  tft.setTextSize(5);
  String callsign = (userCallsign.length() > 0) ? userCallsign : String(DEFAULT_CALLSIGN);
  callsign.toUpperCase();
  int callWidth = tft.textWidth(callsign);
  int callX = (480 - callWidth) / 2;
  int callY = 55;
  tft.setCursor(max(10, callX), callY);
  tft.print(callsign);
  callsignHitX = max(10, callX) - 10;
  callsignHitY = callY - 5;
  callsignHitW = callWidth + 20;
  callsignHitH = 50;

  tft.setTextSize(1);
  tft.setTextColor(HC_CYAN, HC_BG);
  String opText = "OPERATOR STATION";
  tft.setCursor((480 - tft.textWidth(opText)) / 2, callY + 40);
  tft.print(opText);
}

static void hamClockModernTime() {
  tft.setTextColor(clockDigitsColor, HC_BG);
  tft.setTextSize(7);
  String timeStr = (screen1TimeMode == SCREEN1_TIME_LOCAL)
                     ? getTimezoneTimeString("%H:%M:%S", 9)
                     : getUtcTimeString();
  int timeY = 130;
  int timeX = max(10, (480 - tft.textWidth(timeStr)) / 2);
  tft.setCursor(timeX, timeY);
  tft.print(timeStr);
  clockHitX = timeX - 10;
  clockHitY = timeY - 5;
  clockHitW = tft.textWidth(timeStr) + 20;
  clockHitH = 66;

  tft.setTextSize(1);
  const char *timeLabel = "UTC";
  if (screen1TimeMode == SCREEN1_TIME_LOCAL) {
    timeLabel = (tftLanguage == TFT_LANG_EN) ? "Local Time" : "Czas Lokalny";
  }
  tft.setTextColor(HC_TEXT, HC_BG);
  tft.setCursor((480 - tft.textWidth(timeLabel)) / 2, timeY + 66);
  tft.print(timeLabel);
}

static void hamClockModernDate() {
  String dateText;
  struct tm timeinfo;
  bool timeValid = (screen1TimeMode == SCREEN1_TIME_LOCAL)
                     ? getTimeWithTimezone(&timeinfo)
                     : getLocalTime(&timeinfo, 1);
  if (!timeValid || timeinfo.tm_year < 125) {
    dateText = "...";
  } else if (screen1TimeMode == SCREEN1_TIME_LOCAL) {
    dateText = (tftLanguage == TFT_LANG_EN) ? getEnglishDateStringFullWithTimezone()
                                            : getPolishDateStringFullWithTimezone();
  } else {
    dateText = (tftLanguage == TFT_LANG_EN) ? getEnglishDateStringFull()
                                            : getPolishDateStringFull();
  }

  int dateY = 130 + 84;
  if (littleFsReady && LittleFS.exists(ROBOTO_FONT20_FILE) && ESP.getFreeHeap() > 80000) {
    tft.loadFont(ROBOTO_FONT20_NAME, LittleFS);
    tft.setTextColor(HC_WHITE, HC_BG);
    tft.setCursor(max(10, (480 - tft.textWidth(dateText)) / 2), dateY);
    tft.print(dateText);
    tft.unloadFont();
  } else {
    tft.setTextSize(3);
    tft.setTextColor(HC_WHITE, HC_BG);
    String ascii = sanitizePolishToAscii(dateText);
    tft.setCursor(max(10, (480 - tft.textWidth(ascii)) / 2), dateY);
    tft.print(ascii);
  }
}

static void hamClockModernNameDay() {
  if (tftLanguage == TFT_LANG_EN) return;
  String nameDayText = getPolishNameDay();
  if (nameDayText.length() == 0) return;
  int nameDayY = 130 + 84 + 30;
  if (littleFsReady && LittleFS.exists(ROBOTO_FONT12_FILE) && ESP.getFreeHeap() > 80000) {
    tft.loadFont(ROBOTO_FONT12_NAME, LittleFS);
    tft.setTextColor(HC_CYAN, HC_BG);
    tft.setCursor(max(10, (480 - tft.textWidth(nameDayText)) / 2), nameDayY);
    tft.print(nameDayText);
    tft.unloadFont();
  } else {
    tft.setTextSize(2);
    tft.setTextColor(HC_CYAN, HC_BG);
    String ascii = sanitizePolishToAscii(nameDayText);
    tft.setCursor(max(10, (480 - tft.textWidth(ascii)) / 2), nameDayY);
    tft.print(ascii);
  }
}

static void hamClockModernIpAndArrows() {
  IPAddress ip;
  bool connected = false;
  if (wifiConnected && WiFi.status() == WL_CONNECTED) {
    ip = WiFi.localIP();
    connected = true;
  } else if (WiFi.getMode() & WIFI_AP) {
    ip = WiFi.softAPIP();
    connected = true;
  }
  tft.setTextSize(2);
  String ipStr = connected ? ip.toString() : String("NO CONNECTION");
  tft.setTextColor(connected ? HC_TEXT : TFT_RED, HC_BG);
  tft.setCursor((480 - tft.textWidth(ipStr)) / 2, 320 - 20);
  tft.print(ipStr);

  int arrowY = 320 - 30;
  int arrowSize = 12;
  tft.fillTriangle(15, arrowY, 15 + arrowSize, arrowY - arrowSize, 15 + arrowSize, arrowY + arrowSize, HC_CYAN);
  tft.fillTriangle(480 - 15, arrowY, 480 - 15 - arrowSize, arrowY - arrowSize, 480 - 15 - arrowSize, arrowY + arrowSize, HC_CYAN);
}

static void drawHamClockModern() {
  tft.fillScreen(HC_BG);

  hamClockModernHeader();
  screen1HeaderNeedsRedraw = false;

  // Ramka wokol znaku, zegara i daty - cyjanowa (nowoczesny akcent).
  tft.drawRoundRect(30, 45, 420, 220, 10, HC_CYAN);

  hamClockModernCallsign();
  hamClockModernTime();
  hamClockModernDate();
  hamClockModernNameDay();
  hamClockModernIpAndArrows();

#ifdef BATTERY_MONITORING_ENABLED
  drawBatteryQuickUpdate(true);
#endif
}

static void updateScreen1ClockModern() {
  if (!tftInitialized || currentScreen != SCREEN_HAM_CLOCK || inMenu) {
    return;
  }
  tft.setTextColor(clockDigitsColor, HC_BG);
  tft.setTextSize(7);
  String timeStr = (screen1TimeMode == SCREEN1_TIME_LOCAL)
                     ? getTimezoneTimeString("%H:%M:%S", 9)
                     : getUtcTimeString();
  tft.setTextPadding(8 * 42 + 10);
  tft.setCursor(max(10, (480 - tft.textWidth(timeStr)) / 2), 130);
  tft.print(timeStr);
  tft.setTextPadding(0);
}

static void updateScreen1HeaderModern() {
  if (!tftInitialized || currentScreen != SCREEN_HAM_CLOCK || inMenu) {
    return;
  }
  if (!screen1HeaderNeedsRedraw) {
    return;
  }
  hamClockModernHeader();
  screen1HeaderNeedsRedraw = false;
}

void drawHamClock() {
  if (screen1ClockStyle == 1) { drawHamClockClassic(); return; }
  drawHamClockModern();
}


// Adres IP nad stopka. Rysowany PO uiFooter(), zeby napis "SWITCH SCREEN"
// nie zamalowal adresu (stopka czysci caly pas od y=296 w dol).
void drawHamClockIp() {
  const int screenW = 480;
  IPAddress ip;
  bool connected = false;
  if (wifiConnected && WiFi.status() == WL_CONNECTED) {
    ip = WiFi.localIP();
    connected = true;
  } else if (WiFi.getMode() & WIFI_AP) {
    ip = WiFi.softAPIP();
    connected = true;
  }
  tft.setTextSize(2);
  if (connected) {
    String ipStr = ip.toString();
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor((screenW - tft.textWidth(ipStr)) / 2, 280);
    tft.print(ipStr);
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    String noConn = "NO CONNECTION";
    tft.setCursor((screenW - tft.textWidth(noConn)) / 2, 280);
    tft.print(noConn);
  }
}

void updateScreen1Clock() {
  if (!tftInitialized || currentScreen != SCREEN_HAM_CLOCK || inMenu) {
    return;
  }

  struct tm ti;
  bool ok = (screen1TimeMode == SCREEN1_TIME_LOCAL) ? getTimeWithTimezone(&ti) : getLocalTime(&ti, 1);
  if (!ok) return;
  hamClockDrawHands(ti);

  // Analizator widma - odswiez paski i dopisz nowy wiersz wodospadu.
  uint32_t tick = millis() / 250;
  tft.fillRect(HC_SPEC_X, HC_SPEC_BAR_Y, HC_SPEC_W, HC_SPEC_BAR_H, HC_BG);
  for (int gy = 0; gy < HC_SPEC_BAR_H; gy += 12)
    tft.drawFastHLine(HC_SPEC_X, HC_SPEC_BAR_Y + gy, HC_SPEC_W, HC_BEZEL_DK);
  for (int i = 0; i < HC_SPEC_W; i++) {
    uint8_t a = hamClockSpectrumAmp(i, tick);
    int h = (int)((uint32_t)a * HC_SPEC_BAR_H / 100);
    if (h < 1) h = 1;
    tft.drawFastVLine(HC_SPEC_X + i, HC_SPEC_BAR_Y + HC_SPEC_BAR_H - h, h, hamClockSpecColor(a));
  }
  for (int yy = 0; yy < HC_SPEC_WF_H - 1; yy++)
    for (int xx = 0; xx < HC_SPEC_W; xx++)
      hamClockWf[yy][xx] = hamClockWf[yy + 1][xx];
  for (int xx = 0; xx < HC_SPEC_W; xx++)
    hamClockWf[HC_SPEC_WF_H - 1][xx] = hamClockWfColor(hamClockSpectrumAmp(xx, tick));
  for (int yy = 0; yy < HC_SPEC_WF_H; yy++)
    tft.pushImage(HC_SPEC_X, HC_SPEC_WF_Y + yy, HC_SPEC_W, 1, hamClockWf[yy]);
}

void updateScreen1Date() {
  if (!tftInitialized || currentScreen != SCREEN_HAM_CLOCK || inMenu) {
    return;
  }

  struct tm timeinfo;
  bool gotTime = (screen1TimeMode == SCREEN1_TIME_LOCAL)
                   ? getTimeWithTimezone(&timeinfo)
                   : getLocalTime(&timeinfo, 1);
  if (!gotTime || timeinfo.tm_year < 125) {
    return;
  }

  static int lastDay = -1;
  static int lastMonth = -1;
  static int lastYear = -1;
  if (timeinfo.tm_mday == lastDay &&
      timeinfo.tm_mon == lastMonth &&
      timeinfo.tm_year == lastYear) {
    return;
  }
  lastDay = timeinfo.tm_mday;
  lastMonth = timeinfo.tm_mon;
  lastYear = timeinfo.tm_year;

  char dateStr[24];
  snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d",
           timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(HC_TEXT, HC_PANEL);
  tft.drawString(dateStr, HC_CLOCK_CX, HC_CLOCK_CY + HC_CLOCK_R + 10, 2);
  tft.setTextDatum(TL_DATUM);
}

void updateScreen2Clock() {
  // ZEGAR WYĹÄ„CZONY - usuniÄ™ty z nagĹ‚Ăłwka DX Cluster
  return;
}

bool spotMatchesScreen2Filters(const DXSpot &spot) {
  if (screen2FilterMode != FILTER_MODE_ALL) {
    if (screen2FilterMode == FILTER_MODE_CW && spot.mode != "CW") return false;
    if (screen2FilterMode == FILTER_MODE_SSB && spot.mode != "SSB") return false;
    if (screen2FilterMode == FILTER_MODE_DIGI && !(spot.mode == "FT8" || spot.mode == "FT4")) return false;
  }
  if (screen2FilterBandIndex > 0 && screen2FilterBandIndex < SCREEN8_FILTER_BANDS_COUNT) {
    String selectedBand = SCREEN8_FILTER_BANDS[screen2FilterBandIndex];
    float freqMHz = spot.frequency;

    if (selectedBand == "VHF") {
      if (!(freqMHz >= 30.0f && freqMHz < 300.0f)) return false;
    } else if (selectedBand == "UHF") {
      if (!(freqMHz >= 300.0f && freqMHz < 3000.0f)) return false;
    } else if (selectedBand == "SHF") {
      if (!(freqMHz >= 3000.0f && freqMHz < 30000.0f)) return false;
    } else {
      if (spot.band != selectedBand) return false;
    }
  }
  return true;
}

bool spotMatchesScreen7Filters(const DXSpot &spot) {
  if (screen7FilterMode != FILTER_MODE_ALL) {
    if (screen7FilterMode == FILTER_MODE_CW && spot.mode != "CW") return false;
    if (screen7FilterMode == FILTER_MODE_SSB && spot.mode != "SSB") return false;
    if (screen7FilterMode == FILTER_MODE_DIGI && !(spot.mode == "FT8" || spot.mode == "FT4")) return false;
  }
  if (screen7FilterBandIndex > 0 && screen7FilterBandIndex < SCREEN2_FILTER_BANDS_COUNT) {
    if (spot.band != SCREEN2_FILTER_BANDS[screen7FilterBandIndex]) return false;
  }
  return true;
}

bool spotMatchesScreen8Filters(const DXSpot &spot) {
  if (screen8FilterMode != FILTER_MODE_ALL) {
    if (screen8FilterMode == FILTER_MODE_CW && spot.mode != "CW") return false;
    if (screen8FilterMode == FILTER_MODE_SSB && spot.mode != "SSB") return false;
    if (screen8FilterMode == FILTER_MODE_DIGI && !(spot.mode == "FT8" || spot.mode == "FT4")) return false;
  }
  if (screen8FilterBandIndex > 0 && screen8FilterBandIndex < SCREEN8_FILTER_BANDS_COUNT) {
    String selectedBand = SCREEN8_FILTER_BANDS[screen8FilterBandIndex];
    float freqMHz = spot.frequency;

    if (selectedBand == "VHF") {
      if (!(freqMHz >= 30.0f && freqMHz < 300.0f)) return false;
    } else if (selectedBand == "UHF") {
      if (!(freqMHz >= 300.0f && freqMHz < 3000.0f)) return false;
    } else if (selectedBand == "SHF") {
      if (!(freqMHz >= 3000.0f && freqMHz < 30000.0f)) return false;
    } else {
      if (spot.band != selectedBand) return false;
    }
  }
  return true;
}

float getAprsSortDistance(const APRSStation &s) {
  if (s.hasLatLon && s.distance > 0) {
    return s.distance;
  }
  return 1.0e9f;
}

bool aprsSortLess(int a, int b) {
  if (screen6SortMode == APRS_SORT_CALLSIGN) {
    String ca = aprsStations[a].callsign;
    String cb = aprsStations[b].callsign;
    ca.toUpperCase();
    cb.toUpperCase();
    if (ca == cb) {
      return a < b;
    }
    return ca < cb;
  }
  if (screen6SortMode == APRS_SORT_DISTANCE) {
    float da = getAprsSortDistance(aprsStations[a]);
    float db = getAprsSortDistance(aprsStations[b]);
    if (da == db) {
      String ca = aprsStations[a].callsign;
      String cb = aprsStations[b].callsign;
      ca.toUpperCase();
      cb.toUpperCase();
      return ca < cb;
    }
    return da < db;
  }
  // APRS_SORT_TIME = aktualna kolejnoĹ›Ä‡
  return a < b;
}

void buildAprsDisplayOrder(int *order, int &count) {
  count = min(aprsStationCount, MAX_APRS_DISPLAY_LCD);
  for (int i = 0; i < count; i++) {
    order[i] = i;
  }
  if (screen6SortMode == APRS_SORT_TIME) {
    return;
  }
  for (int i = 0; i < count - 1; i++) {
    int best = i;
    for (int j = i + 1; j < count; j++) {
      if (aprsSortLess(order[j], order[best])) {
        best = j;
      }
    }
    if (best != i) {
      int tmp = order[i];
      order[i] = order[best];
      order[best] = tmp;
    }
  }
}

uint32_t computeScreen2Signature() {
  // Prosty hash treĹ›ci tabeli (10 lub 11 wierszy zaleĹĽnie od paska nawigacji)
  const uint32_t fnvPrime = 16777619u;
  uint32_t hash = 2166136261u;

  lockDxSpots();
  hash ^= (uint32_t)spotCount;
  hash *= fnvPrime;
  hash ^= (uint32_t)screen2FilterMode;
  hash *= fnvPrime;
  hash ^= (uint32_t)screen2FilterBandIndex;
  hash *= fnvPrime;
  hash ^= (uint32_t)dxTableSizeMode;
  hash *= fnvPrime;
  hash ^= isTableNavFooterVisible(SCREEN_DX_CLUSTER) ? 1u : 0u;
  hash *= fnvPrime;

  int maxRows = getDxTableMaxRows();
  int displayCount = 0;
  for (int i = 0; i < spotCount && displayCount < maxRows; i++) {
    const DXSpot &s = spots[i];
    if (!spotMatchesScreen2Filters(s)) {
      continue;
    }
    for (size_t j = 0; j < s.time.length(); j++) {
      hash ^= (uint8_t)s.time[j];
      hash *= fnvPrime;
    }
    for (size_t j = 0; j < s.callsign.length(); j++) {
      hash ^= (uint8_t)s.callsign[j];
      hash *= fnvPrime;
    }
    hash ^= (uint32_t)(s.frequency * 100);
    hash *= fnvPrime;
    for (size_t j = 0; j < s.mode.length(); j++) {
      hash ^= (uint8_t)s.mode[j];
      hash *= fnvPrime;
    }
    for (size_t j = 0; j < s.country.length(); j++) {
      hash ^= (uint8_t)s.country[j];
      hash *= fnvPrime;
    }
    hash ^= (uint32_t)s.distance;
    hash *= fnvPrime;
    displayCount++;
  }

  unlockDxSpots();

  return hash;
}

void updateScreen2Data() {
  if (!tftInitialized || currentScreen != SCREEN_DX_CLUSTER || inMenu) {
    return;
  }

  // 1) Zaktualizuj czas w nagĹ‚Ăłwku (bez peĹ‚nego odĹ›wieĹĽania)
  updateScreen2Clock();

  // 2) OdĹ›wieĹĽ tabelÄ™ tylko gdy dane siÄ™ zmieniĹ‚y
  static uint32_t lastSig = 0;
  static unsigned long lastTableRedrawMs = 0;
  uint32_t currentSig = computeScreen2Signature();
  if (currentSig == lastSig) {
    return;
  }

  unsigned long now = millis();
  if (lastTableRedrawMs != 0 && (now - lastTableRedrawMs) < DX_SCREEN_MIN_REDRAW_MS) {
    return;
  }

  lastSig = currentSig;
  lastTableRedrawMs = now;

  // 3) Renderuj tabelÄ™ do bufora i wypchnij jednym ruchem (bez migotania)
  const bool navVisible = isTableNavFooterVisible(SCREEN_DX_CLUSTER);
  const bool enlarged = isDxTableEnlarged();
  const int maxRows = getDxTableMaxRows();
  const int tableTop = TFT_TABLE_TOP;
  const int tableBottom = getTableBottomForScreen(SCREEN_DX_CLUSTER);
  const int tableHeight = tableBottom - tableTop;
  TFT_eSprite *tableSprite = (navVisible && ensureSharedTableSprite()) ? &sharedTableSprite : nullptr;

  if (tableSprite != nullptr) {
    tableSprite->fillSprite(TFT_BLACK);

    int yPos = 8;
    tableSprite->setTextColor(TFT_DARKGREY);
    tableSprite->setTextSize(1);
    if (enlarged) {
      tableSprite->setCursor(5, yPos);   tableSprite->print("UTC");
      tableSprite->setCursor(74, yPos);  tableSprite->print("CALL");
      tableSprite->setCursor(204, yPos); tableSprite->print("MHz");
      tableSprite->setCursor(282, yPos); tableSprite->print("MODE");
    } else {
      tableSprite->setCursor(5, yPos);   tableSprite->print("UTC");
      tableSprite->setCursor(50, yPos);  tableSprite->print("CALLSIGN");
      tableSprite->setCursor(125, yPos); tableSprite->print("MHz");
      tableSprite->setCursor(190, yPos); tableSprite->print("MODE");
      tableSprite->setCursor(260, yPos); tableSprite->print(sanitizePolishToAscii(String(tr(TR_COUNTRY))));
    }
    tableSprite->drawFastHLine(0, yPos + 10, 480, TFT_DARKGREY);
    yPos += enlarged ? 20 : 18;

    int displayCount = 0;
    lockDxSpots();
    for (int i = 0; i < spotCount && displayCount < maxRows; i++) {
      if (!spotMatchesScreen2Filters(spots[i])) {
        continue;
      }
      if (yPos >= (tableHeight - 2)) {
        break;
      }
      if (enlarged) {
        if (displayCount % 2 == 0) {
          tableSprite->fillRect(0, yPos - 5, 480, 24, TFT_TABLE_ALT_ROW_COLOR);
        }

        tableSprite->setTextSize(2);
        tableSprite->setTextColor(TFT_LIGHTGREY);
        tableSprite->setCursor(5, yPos);
        tableSprite->print(formatSpotUtc(spots[i].time));

        tableSprite->setTextColor(TFT_WHITE);
        tableSprite->setCursor(74, yPos);
        String callText = spots[i].callsign;
        if (callText.length() > 8) callText = callText.substring(0, 8);
        tableSprite->print(callText);

        tableSprite->setTextColor(TFT_YELLOW);
        tableSprite->setCursor(204, yPos);
        tableSprite->print(spots[i].frequency / 1000.0, 3);

        tableSprite->setTextColor(TFT_GREEN);
        tableSprite->setCursor(282, yPos);
        String modeText = spots[i].mode;
        if (modeText.length() > 4) modeText = modeText.substring(0, 4);
        tableSprite->print(modeText);

        yPos += 27;
      } else {
        if (displayCount % 2 == 0) {
          tableSprite->fillRect(0, yPos - 2, 480, 16, TFT_TABLE_ALT_ROW_COLOR);
        }

        tableSprite->setTextSize(1);

        tableSprite->setTextColor(TFT_LIGHTGREY);
        tableSprite->setCursor(5, yPos);
        tableSprite->print(formatSpotUtc(spots[i].time));

        tableSprite->setTextColor(TFT_WHITE, TFT_BLACK);
        tableSprite->setTextSize(1);
        tableSprite->setCursor(50, yPos);
        String callText = spots[i].callsign;
        callText.toUpperCase();
        if (callText.length() > 10) callText = callText.substring(0, 10);
        tableSprite->print(callText);

        tableSprite->setTextColor(TFT_YELLOW);
        tableSprite->setCursor(125, yPos);
        tableSprite->print(spots[i].frequency / 1000.0, 3);

        tableSprite->setTextColor(TFT_GREEN);
        tableSprite->setCursor(190, yPos);
        tableSprite->print(spots[i].mode);

        tableSprite->setTextColor(TFT_RADIO_ORANGE);
        tableSprite->setCursor(260, yPos);
        String countryText = formatDistanceOrCountry(spots[i], COUNTRY_COL_MAX_LEN);
        tableSprite->print(countryText);

        yPos += 17;
      }
      displayCount++;
    }
    unlockDxSpots();

    if (displayCount == 0) {
      tableSprite->setTextColor(TFT_RED);
      tableSprite->setTextSize(2);
      tableSprite->setCursor(40, 120 - tableTop);
      tableSprite->print("WAITING FOR SPOTS...");
    }

    tableSprite->pushSprite(0, tableTop);
    return;
  }

  // Fallback bez sprite (np. gdy zabraknie RAM)
  tft.fillRect(0, tableTop, 480, tableHeight, TFT_BLACK);

  int yPos = 40;
  tft.setTextColor(TFT_DARKGREY);
  tft.setTextSize(1);
  if (enlarged) {
    tft.setCursor(5, yPos);   tft.print("UTC");
    tft.setCursor(74, yPos);  tft.print("CALL");
    tft.setCursor(204, yPos); tft.print("MHz");
    tft.setCursor(330, yPos); tft.print("MODE");
  } else {
    tft.setCursor(5, yPos);   tft.print("UTC");
    tft.setCursor(50, yPos);  tft.print("CALLSIGN");
    tft.setCursor(125, yPos); tft.print("MHz");
    tft.setCursor(190, yPos); tft.print("MODE");
    tft.setCursor(260, yPos); tft.print(sanitizePolishToAscii(String(tr(TR_COUNTRY))));
  }
  tft.drawFastHLine(0, yPos + 10, 480, TFT_DARKGREY);
  yPos += enlarged ? 20 : 18;

  int displayCount = 0;
  lockDxSpots();
  for (int i = 0; i < spotCount && displayCount < maxRows; i++) {
    if (!spotMatchesScreen2Filters(spots[i])) {
      continue;
    }
    if (yPos >= (tableBottom - 2)) {
      break;
    }
    if (enlarged) {
      if (displayCount % 2 == 0) {
        tft.fillRect(0, yPos - 5, 480, 24, TFT_TABLE_ALT_ROW_COLOR);
      }

      tft.setTextSize(2);
      tft.setTextColor(TFT_LIGHTGREY);
      tft.setCursor(5, yPos);
      tft.print(formatSpotUtc(spots[i].time));

      tft.setTextColor(TFT_WHITE);
      tft.setCursor(74, yPos);
      String callText = spots[i].callsign;
      if (callText.length() > 8) callText = callText.substring(0, 8);
      tft.print(callText);

      tft.setTextColor(TFT_YELLOW);
      tft.setCursor(204, yPos);
      tft.print(spots[i].frequency / 1000.0, 3);

      tft.setTextColor(TFT_GREEN);
      tft.setCursor(330, yPos);
      String modeText = spots[i].mode;
      if (modeText.length() > 4) modeText = modeText.substring(0, 4);
      tft.print(modeText);

      yPos += 27;
    } else {
      if (displayCount % 2 == 0) {
        tft.fillRect(0, yPos - 2, 480, 16, TFT_TABLE_ALT_ROW_COLOR);
      }

      tft.setTextSize(1);

      tft.setTextColor(TFT_LIGHTGREY);
      tft.setCursor(5, yPos);
      tft.print(formatSpotUtc(spots[i].time));

      tft.setTextColor(TFT_WHITE);
      tft.setCursor(50, yPos);
      String callStr = spots[i].callsign;
      callStr.toUpperCase();
      tft.print(callStr);

      tft.setTextColor(TFT_YELLOW);
      tft.setCursor(125, yPos);
      tft.print(spots[i].frequency / 1000.0, 3);

      tft.setTextColor(TFT_GREEN);
      tft.setCursor(190, yPos);
      String modeStr = spots[i].mode;
      if (modeStr.length() > 4) modeStr = modeStr.substring(0, 4);
      tft.print(modeStr);

      tft.setTextColor(TFT_RADIO_ORANGE);
      tft.setCursor(260, yPos);
      String countryText = formatDistanceOrCountry(spots[i], COUNTRY_COL_MAX_LEN);
      tft.print(countryText);

      yPos += 17;
    }
    displayCount++;
  }
  unlockDxSpots();

  if (displayCount == 0) {
    tft.setTextColor(UI_COL_WARN);
    tft.setTextSize(2);
    tft.setCursor(40, 120);
    tft.print("WAITING FOR SPOTS...");
  }
  
  // StrzaĹ‚ki nawigacyjne - ZAWSZE rysuj po odĹ›wieĹĽeniu tabeli
  drawSwitchScreenFooter();
}

// Ekran 7: POTA Cluster (SSB only, 10 spotĂłw)
void drawPotaCluster() {
  // UĹĽyj tych samych wspĂłĹ‚rzÄ™dnych co w drawHamClock() - ograniczenie do obszaru ramki




  // Ikona WiFi w gĂłrnym prawym rogu
  
  // Ikona baterii obok ikony WiFi

  // ZEGAR w prawym gĂłrnym rogu - USUNIÄTY
  // struct tm timeinfo;
  // if (getLocalTime(&timeinfo, 1)) {
  //   char timeBuffer[10];
  //   strftime(timeBuffer, 10, "%H:%M Z", &timeinfo);
  //   int timeWidth = strlen(timeBuffer) * 12;
  //   tft.setCursor(460 - timeWidth, 8);
  //   tft.print(timeBuffer);
  // }

  int yPos = 40;
  const bool enlarged = isDxTableEnlarged();
  tft.setTextColor(UI_COL_FAINT);
  tft.setTextSize(1);

  if (enlarged) {
    tft.setCursor(5, yPos);   tft.print("UTC");
    tft.setCursor(74, yPos);  tft.print("CALL");
    tft.setCursor(204, yPos); tft.print("MHz");
    tft.setCursor(330, yPos); tft.print("MODE");
  } else {
    tft.setCursor(5, yPos);   tft.print("UTC");
    tft.setCursor(50, yPos);  tft.print("CALLSIGN");
    tft.setCursor(140, yPos); tft.print("MHz");
    tft.setCursor(260, yPos); tft.print("MODE");
    tft.setCursor(360, yPos); tft.print(sanitizePolishToAscii(String(tr(TR_COUNTRY))));
  }
  tft.drawFastHLine(0, yPos + 10, 480, TFT_DARKGREY);
  yPos += enlarged ? 20 : 18;

  int maxRows = getPotaTableMaxRows();
  int displayCount = 0;
  for (int i = 0; i < potaSpotCount && displayCount < maxRows; i++) {
    if (!spotMatchesScreen7Filters(potaSpots[i])) continue;
    if (enlarged) {
      if (displayCount % 2 == 0) tft.fillRect(0, yPos - 5, 480, 24, TFT_TABLE_ALT_ROW_COLOR);

      tft.setTextSize(2);
      tft.setTextColor(TFT_LIGHTGREY);
      tft.setCursor(5, yPos);
      tft.print(formatSpotUtc(potaSpots[i].time));

      tft.setTextColor(TFT_WHITE);
      tft.setCursor(74, yPos);
      String callText = potaSpots[i].callsign;
      if (callText.length() > 8) callText = callText.substring(0, 8);
      tft.print(callText);

      tft.setTextColor(TFT_YELLOW);
      tft.setCursor(204, yPos);
      tft.print(potaSpots[i].frequency, 3);

      tft.setTextColor(TFT_GREEN);
      tft.setCursor(330, yPos);
      String modeText = potaSpots[i].mode;
      if (modeText.length() > 4) modeText = modeText.substring(0, 4);
      tft.print(modeText);

      yPos += 27;
    } else {
      if (displayCount % 2 == 0) tft.fillRect(0, yPos - 2, 480, 16, TFT_TABLE_ALT_ROW_COLOR);

      tft.setTextSize(1);
      tft.setTextColor(TFT_LIGHTGREY);
      tft.setCursor(5, yPos);
      String timeStr = potaSpots[i].time;
      // ObsĹ‚uĹĽ ISO 8601 z datÄ…: wyciÄ…gnij HH:MM
      int tPos = timeStr.indexOf('T');
      if (tPos > 0 && tPos + 5 < (int)timeStr.length()) {
        timeStr = timeStr.substring(tPos + 1, tPos + 6);
      }
      if (timeStr.length() > 5) timeStr = timeStr.substring(0, 5);
      tft.print(timeStr);

      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setTextSize(1);
      tft.setCursor(50, yPos);
      String callStr = potaSpots[i].callsign;
      callStr.toUpperCase();
      tft.print(callStr);

      tft.setTextColor(TFT_YELLOW);
      tft.setCursor(125, yPos);
      tft.print(potaSpots[i].frequency, 3); // frequency juĹĽ w MHz

      tft.setTextColor(TFT_GREEN);
      tft.setCursor(240, yPos);
      tft.print(potaSpots[i].mode);

      tft.setTextColor(TFT_RADIO_ORANGE);
      tft.setCursor(220, yPos);
      String countryText = formatDistanceOrCountry(potaSpots[i], COUNTRY_COL_MAX_LEN);
      tft.print(countryText);

      yPos += 17;
    }
    displayCount++;
  }


  if (displayCount == 0) {
    tft.setTextColor(UI_COL_WARN);
    tft.setTextSize(2);
    tft.setCursor(40, 120);
    tft.print("WAITING FOR SPOTS...");
  }
}

void updateScreen7Clock() {
  if (!tftInitialized || currentScreen != 7 || inMenu) {
    return;
  }

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 1)) {
    return;
  }

  char timeBuffer[10];
  strftime(timeBuffer, 10, "%H:%M Z", &timeinfo);
  int timeWidth = strlen(timeBuffer) * 12;
  int timeX = 315 - timeWidth;

  tft.fillRect(timeX - 2, 4, timeWidth + 6, 24, TFT_RADIO_ORANGE);
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(timeX, 8);
  tft.print(timeBuffer);
}

uint32_t computeScreen7Signature() {
  const uint32_t fnvPrime = 16777619u;
  uint32_t hash = 2166136261u;

  hash ^= (uint32_t)potaSpotCount;
  hash *= fnvPrime;
  hash ^= (uint32_t)screen7FilterMode;
  hash *= fnvPrime;
  hash ^= (uint32_t)screen7FilterBandIndex;
  hash *= fnvPrime;
  hash ^= (uint32_t)dxTableSizeMode;
  hash *= fnvPrime;
  hash ^= isTableNavFooterVisible(SCREEN_POTA_CLUSTER) ? 1u : 0u;
  hash *= fnvPrime;

  int maxDisplaySpots = min(potaSpotCount, getPotaTableMaxRows());
  int displayCount = 0;
  for (int i = 0; i < potaSpotCount && displayCount < maxDisplaySpots; i++) {
    const DXSpot &s = potaSpots[i];
    if (!spotMatchesScreen7Filters(s)) continue;
    for (size_t j = 0; j < s.time.length(); j++) {
      hash ^= (uint8_t)s.time[j];
      hash *= fnvPrime;
    }
    for (size_t j = 0; j < s.callsign.length(); j++) {
      hash ^= (uint8_t)s.callsign[j];
      hash *= fnvPrime;
    }

    hash ^= (uint32_t)(s.frequency * 100);
    hash *= fnvPrime;
    for (size_t j = 0; j < s.mode.length(); j++) {
      hash ^= (uint8_t)s.mode[j];
      hash *= fnvPrime;
    }
    for (size_t j = 0; j < s.country.length(); j++) {
      hash ^= (uint8_t)s.country[j];
      hash *= fnvPrime;
    }
    displayCount++;
  }

  return hash;
}

void updateScreen7Data() {
  if (!tftInitialized || currentScreen != 7 || inMenu) {
    return;
  }

  // WyĹ‚Ä…czono - uĹĽywamy tylko updatePotaClusterClock dla zegara w prawym rogu
  // updateScreen7Clock();

  static uint32_t lastSig = 0;
  uint32_t currentSig = computeScreen7Signature();
  if (currentSig == lastSig) {
    return;
  }
  lastSig = currentSig;

  const bool navVisible = isTableNavFooterVisible(SCREEN_POTA_CLUSTER);
  const bool enlarged = isDxTableEnlarged();
  const int maxRows = getPotaTableMaxRows();
  const int tableTop = TFT_TABLE_TOP;
  const int tableBottom = getTableBottomForScreen(SCREEN_POTA_CLUSTER);
  const int tableHeight = tableBottom - tableTop;
  TFT_eSprite *tableSprite = (navVisible && ensureSharedTableSprite()) ? &sharedTableSprite : nullptr;

  if (tableSprite != nullptr) {
    tableSprite->fillSprite(TFT_BLACK);

    int yPos = 8;
    tableSprite->setTextColor(TFT_DARKGREY);
    tableSprite->setTextSize(1);
    if (enlarged) {
      tableSprite->setCursor(5, yPos);   tableSprite->print("UTC");
      tableSprite->setCursor(74, yPos);  tableSprite->print("CALL");
      tableSprite->setCursor(204, yPos); tableSprite->print("MHz");
      tableSprite->setCursor(282, yPos); tableSprite->print("MODE");
    } else {
      tableSprite->setCursor(5, yPos);   tableSprite->print("UTC");
      tableSprite->setCursor(50, yPos);  tableSprite->print("CALLSIGN");
      tableSprite->setCursor(125, yPos); tableSprite->print("MHz");
      tableSprite->setCursor(190, yPos); tableSprite->print("MODE");
      tableSprite->setCursor(260, yPos); tableSprite->print(sanitizePolishToAscii(String(tr(TR_COUNTRY))));
    }
    tableSprite->drawFastHLine(0, yPos + 10, 480, TFT_DARKGREY);
    yPos += enlarged ? 20 : 18;

    int displayCount = 0;
    for (int i = 0; i < potaSpotCount && displayCount < maxRows; i++) {
      if (!spotMatchesScreen7Filters(potaSpots[i])) continue;
      if (yPos >= (tableHeight - 2)) {
        break;
      }
      if (enlarged) {
        if (displayCount % 2 == 0) {
          tableSprite->fillRect(0, yPos - 5, 480, 24, TFT_TABLE_ALT_ROW_COLOR);
        }

        tableSprite->setTextSize(2);

        tableSprite->setTextColor(TFT_LIGHTGREY);
        tableSprite->setCursor(5, yPos);
        tableSprite->print(formatSpotUtc(potaSpots[i].time));

        tableSprite->setTextColor(TFT_WHITE);
        tableSprite->setCursor(74, yPos);
        String callText = potaSpots[i].callsign;
        if (callText.length() > 8) callText = callText.substring(0, 8);
        tableSprite->print(callText);

        tableSprite->setTextColor(TFT_YELLOW);
        tableSprite->setCursor(204, yPos);
        tableSprite->print(potaSpots[i].frequency, 3);

        tableSprite->setTextColor(TFT_GREEN);
        tableSprite->setCursor(282, yPos);
        String modeText = potaSpots[i].mode;
        if (modeText.length() > 4) modeText = modeText.substring(0, 4);
        tableSprite->print(modeText);

        yPos += 27;
      } else {
        if (displayCount % 2 == 0) {
          tableSprite->fillRect(0, yPos - 2, 480, 16, TFT_TABLE_ALT_ROW_COLOR);
        }

        tableSprite->setTextSize(1);

        tableSprite->setTextColor(TFT_LIGHTGREY);
        tableSprite->setCursor(5, yPos);
        String timeStr = potaSpots[i].time;
        int tPos = timeStr.indexOf('T');
        if (tPos > 0 && tPos + 5 < (int)timeStr.length()) {
          timeStr = timeStr.substring(tPos + 1, tPos + 6);
        }
        if (timeStr.length() > 5) timeStr = timeStr.substring(0, 5);
        tableSprite->print(timeStr);

        tableSprite->setTextColor(TFT_WHITE);
        tableSprite->setCursor(50, yPos);
        tableSprite->print(potaSpots[i].callsign);

        tableSprite->setTextColor(TFT_YELLOW);
        tableSprite->setCursor(125, yPos);
        tableSprite->print(potaSpots[i].frequency, 3);

        tableSprite->setTextColor(TFT_GREEN);
        tableSprite->setCursor(190, yPos);
        tableSprite->print(potaSpots[i].mode);

        tableSprite->setTextColor(TFT_RADIO_ORANGE);
        tableSprite->setCursor(260, yPos);
        String countryText = formatDistanceOrCountry(potaSpots[i], COUNTRY_COL_MAX_LEN);
        tableSprite->print(countryText);

        yPos += 17;
      }
      displayCount++;
    }

    if (displayCount == 0) {
      tableSprite->setTextColor(TFT_RED);
      tableSprite->setTextSize(2);
      tableSprite->setCursor(40, 120 - tableTop);
      tableSprite->print("WAITING FOR SPOTS...");
    }

    tableSprite->pushSprite(0, tableTop);
    return;
  }

  // Fallback bez sprite (np. gdy zabraknie RAM)
  tft.fillRect(0, tableTop, 480, tableHeight, TFT_BLACK);

  int yPos = 40;
  tft.setTextColor(TFT_DARKGREY);
  tft.setTextSize(1);
  if (enlarged) {
    tft.setCursor(5, yPos);   tft.print("UTC");
    tft.setCursor(74, yPos);  tft.print("CALL");
    tft.setCursor(204, yPos); tft.print("MHz");
    tft.setCursor(330, yPos); tft.print("MODE");
  } else {
    tft.setCursor(5, yPos);   tft.print("UTC");
    tft.setCursor(50, yPos);  tft.print("CALLSIGN");
    tft.setCursor(125, yPos); tft.print("MHz");
    tft.setCursor(260, yPos); tft.print("MODE");
    tft.setCursor(360, yPos); tft.print(sanitizePolishToAscii(String(tr(TR_COUNTRY))));
  }
  tft.drawFastHLine(0, yPos + 10, 480, TFT_DARKGREY);
  yPos += enlarged ? 20 : 18;

  int displayCount = 0;
  for (int i = 0; i < potaSpotCount && displayCount < maxRows; i++) {
    if (!spotMatchesScreen7Filters(potaSpots[i])) continue;
    if (yPos >= (tableBottom - 2)) {
      break;
    }
    if (enlarged) {
      if (displayCount % 2 == 0) {
        tft.fillRect(0, yPos - 5, 480, 24, TFT_TABLE_ALT_ROW_COLOR);
      }

      tft.setTextSize(2);

      tft.setTextColor(TFT_LIGHTGREY);
      tft.setCursor(5, yPos);
      tft.print(formatSpotUtc(potaSpots[i].time));

      tft.setTextColor(TFT_WHITE);
      tft.setCursor(74, yPos);
      String callText = potaSpots[i].callsign;
      if (callText.length() > 8) callText = callText.substring(0, 8);
      tft.print(callText);

      tft.setTextColor(TFT_YELLOW);
      tft.setCursor(204, yPos);
      tft.print(potaSpots[i].frequency, 3);

      tft.setTextColor(TFT_GREEN);
      tft.setCursor(330, yPos);
      String modeText = potaSpots[i].mode;
      if (modeText.length() > 4) modeText = modeText.substring(0, 4);
      tft.print(modeText);

      yPos += 27;
    } else {
      if (displayCount % 2 == 0) {
        tft.fillRect(0, yPos - 2, 480, 16, TFT_TABLE_ALT_ROW_COLOR);
      }

      // Znak wywoĹ‚awczy - rozmiar 1 (standardowy)
      tft.setTextSize(1);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setCursor(50, yPos);
      String callStr = potaSpots[i].callsign;
      callStr.toUpperCase();
      if (callStr.length() > 8) callStr = callStr.substring(0, 8);
      tft.print(callStr);
      
      // CzÄ™stotliwoĹ›Ä‡ - rozmiar 1
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.setCursor(140, yPos);
      tft.print(potaSpots[i].frequency, 3);
      
      // Mode - rozmiar 1
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.setCursor(260, yPos);
      String modeStr = potaSpots[i].mode;
      if (modeStr.length() > 4) modeStr = modeStr.substring(0, 4);
      tft.print(modeStr);
      
      // Czas i kraj - rozmiar 1 (dopasowane do nagĹ‚ĂłwkĂłw)
      tft.setTextSize(1);
      tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      tft.setCursor(5, yPos + 5);
      String timeStr = potaSpots[i].time;
      int tPos = timeStr.indexOf('T');
      if (tPos > 0 && tPos + 5 < (int)timeStr.length()) {
        timeStr = timeStr.substring(tPos + 1, tPos + 6);
      }
      if (timeStr.length() > 5) timeStr = timeStr.substring(0, 5);
      tft.print(timeStr);

      tft.setTextColor(TFT_ORANGE, TFT_BLACK);
      tft.setCursor(360, yPos + 5);
      String countryText = formatDistanceOrCountry(potaSpots[i], COUNTRY_COL_MAX_LEN);
      tft.print(countryText);

      yPos += 20;
    }
    displayCount++;
  }

  if (displayCount == 0) {
    tft.setTextColor(UI_COL_WARN);
    tft.setTextSize(2);
    tft.setCursor(40, 120);
    tft.print("WAITING FOR SPOTS...");
  }
  
  // StrzaĹ‚ki nawigacyjne - ZAWSZE rysuj po odĹ›wieĹĽeniu tabeli
  drawSwitchScreenFooter();
}

static String formatHamalertTftTime(const String &rawTime) {
  String t = rawTime;
  t.trim();
  if (t.length() == 0) return "-----";

  int tPos = t.indexOf('T');
  if (tPos > 0 && (tPos + 5) < (int)t.length()) {
    String hhmm = t.substring(tPos + 1, tPos + 6);
    if (hhmm.length() == 5) {
      return hhmm;
    }
  }

  if (t.length() >= 5 && t.charAt(2) == ':') {
    return t.substring(0, 5);
  }

  if (t.length() >= 5 && t.charAt(4) == 'Z' && isDigit((unsigned char)t.charAt(0)) && isDigit((unsigned char)t.charAt(1)) && isDigit((unsigned char)t.charAt(2)) && isDigit((unsigned char)t.charAt(3))) {
    String hhmm = t.substring(0, 4);
    return hhmm.substring(0, 2) + ":" + hhmm.substring(2, 4);
  }

  return t;
}

void drawHamalertCluster() {
  // ekran czysci drawScreen (wspolny styl ISS)




  // Ikona WiFi w gĂłrnym prawym rogu
  
  // Ikona baterii obok ikony WiFi

  // ZEGAR - rysowany osobno przez updateHamalertClock()

  int yPos = 40;
  const bool enlarged = isDxTableEnlarged();
  tft.setTextColor(UI_COL_FAINT);
  tft.setTextSize(1);
  if (enlarged) {
    tft.setCursor(5, yPos);   tft.print("UTC");
    tft.setCursor(74, yPos);  tft.print("CALL");
    tft.setCursor(204, yPos); tft.print("MHz");
    tft.setCursor(330, yPos); tft.print("MODE");
  } else {
    tft.setCursor(5, yPos);   tft.print("UTC");
    tft.setCursor(50, yPos);  tft.print("CALLSIGN");
    tft.setCursor(125, yPos); tft.print("MHz");
    tft.setCursor(240, yPos); tft.print("MODE");
    tft.setCursor(220, yPos); tft.print(sanitizePolishToAscii(String(tr(TR_COUNTRY))));
  }
  tft.drawFastHLine(0, yPos + 10, 480, TFT_DARKGREY);
  yPos += enlarged ? 20 : 18;

  const int maxRows = getHamalertTableMaxRows();
  int displayCount = 0;
  for (int i = 0; i < hamalertSpotCount && displayCount < maxRows; i++) {
    if (!spotMatchesScreen8Filters(hamalertSpots[i])) {
      continue;
    }
    if (enlarged) {
      if (displayCount % 2 == 0) {
        tft.fillRect(0, yPos - 5, 480, 24, TFT_TABLE_ALT_ROW_COLOR);
      }

      tft.setTextSize(2);

      tft.setTextColor(TFT_LIGHTGREY);
      tft.setCursor(5, yPos);
      tft.print(formatHamalertTftTime(hamalertSpots[i].time));

      tft.setTextColor(TFT_WHITE);
      tft.setCursor(74, yPos);
      String callText = hamalertSpots[i].callsign;
      if (callText.length() > 8) callText = callText.substring(0, 8);
      tft.print(callText);

      tft.setTextColor(TFT_YELLOW);
      tft.setCursor(204, yPos);
      tft.print(hamalertSpots[i].frequency, 3);

      tft.setTextColor(TFT_GREEN);
      tft.setCursor(330, yPos);
      String modeText = hamalertSpots[i].mode;
      if (modeText.length() > 4) modeText = modeText.substring(0, 4);
      tft.print(modeText);

      yPos += 27;
    } else {
      if (displayCount % 2 == 0) {
        tft.fillRect(0, yPos - 2, 480, 16, TFT_TABLE_ALT_ROW_COLOR);
      }

      tft.setTextSize(1);

      tft.setTextColor(TFT_LIGHTGREY);
      tft.setCursor(5, yPos);
      tft.print(formatHamalertTftTime(hamalertSpots[i].time));

      tft.setTextColor(TFT_WHITE);
      tft.setCursor(50, yPos);
      tft.print(hamalertSpots[i].callsign);

      tft.setTextColor(TFT_YELLOW);
      tft.setCursor(125, yPos);
      tft.print(hamalertSpots[i].frequency, 3);

      tft.setTextColor(TFT_GREEN);
      tft.setCursor(240, yPos);
      tft.print(hamalertSpots[i].mode);

      tft.setTextColor(TFT_RADIO_ORANGE);
      tft.setCursor(220, yPos);
      String countryText = formatDistanceOrCountry(hamalertSpots[i], COUNTRY_COL_MAX_LEN);
      tft.print(countryText);

      yPos += 17;
    }
    displayCount++;
  }

  // StrzaĹ‚ki nawigacyjne - ZAWSZE widoczne

  if (displayCount == 0) {
    tft.setTextColor(UI_COL_WARN);
    tft.setTextSize(2);
    tft.setCursor(40, 120);
    tft.print("WAITING FOR SPOTS...");
  }
}

void updateScreen8Clock() {
  if (!tftInitialized || currentScreen != SCREEN_HAMALERT_CLUSTER || inMenu) {
    return;
  }

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 1)) {
    return;
  }

  char timeBuffer[10];
  strftime(timeBuffer, 10, "%H:%M Z", &timeinfo);
  int timeWidth = strlen(timeBuffer) * 12;
  int timeX = 475 - timeWidth;

  tft.fillRect(timeX - 2, 4, timeWidth + 6, 24, TFT_RADIO_ORANGE);
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(timeX, 8);
  tft.print(timeBuffer);
}

uint32_t computeScreen8Signature() {
  const uint32_t fnvPrime = 16777619u;
  uint32_t hash = 2166136261u;

  hash ^= (uint32_t)hamalertSpotCount;
  hash *= fnvPrime;
  hash ^= (uint32_t)screen8FilterMode;
  hash *= fnvPrime;
  hash ^= (uint32_t)screen8FilterBandIndex;
  hash *= fnvPrime;
  hash ^= (uint32_t)dxTableSizeMode;
  hash *= fnvPrime;
  hash ^= isTableNavFooterVisible(SCREEN_HAMALERT_CLUSTER) ? 1u : 0u;
  hash *= fnvPrime;

  int maxRows = getHamalertTableMaxRows();
  int displayCount = 0;
  for (int i = 0; i < hamalertSpotCount && displayCount < maxRows; i++) {
    const DXSpot &s = hamalertSpots[i];
    if (!spotMatchesScreen8Filters(s)) {
      continue;
    }
    for (size_t j = 0; j < s.time.length(); j++) {
      hash ^= (uint8_t)s.time[j];
      hash *= fnvPrime;
    }
    for (size_t j = 0; j < s.callsign.length(); j++) {
      hash ^= (uint8_t)s.callsign[j];
      hash *= fnvPrime;
    }
    hash ^= (uint32_t)(s.frequency * 100);
    hash *= fnvPrime;
    for (size_t j = 0; j < s.mode.length(); j++) {
      hash ^= (uint8_t)s.mode[j];
      hash *= fnvPrime;
    }
    for (size_t j = 0; j < s.country.length(); j++) {
      hash ^= (uint8_t)s.country[j];
      hash *= fnvPrime;
    }
    displayCount++;
  }

  return hash;
}

void updateScreen8Data() {
  if (!tftInitialized || currentScreen != SCREEN_HAMALERT_CLUSTER || inMenu) {
    return;
  }

  // WyĹ‚Ä…czono - zegar tylko na gĂłrnym pasku przez updateHamalertClock
  // updateScreen8Clock();

  static uint32_t lastSig = 0;
  uint32_t currentSig = computeScreen8Signature();
  if (currentSig == lastSig) {
    return;
  }
  lastSig = currentSig;

  const bool enlarged = isDxTableEnlarged();
  const int maxRows = getHamalertTableMaxRows();
  const int tableTop = TFT_TABLE_TOP;
  const int tableBottom = getTableBottomForScreen(SCREEN_HAMALERT_CLUSTER);
  const int tableHeight = tableBottom - tableTop;

  tft.fillRect(0, tableTop, 480, tableHeight, TFT_BLACK);

  int yPos = 40;
  tft.setTextColor(TFT_DARKGREY);
  tft.setTextSize(1);
  if (enlarged) {
    tft.setCursor(5, yPos);   tft.print("UTC");
    tft.setCursor(74, yPos);  tft.print("CALL");
    tft.setCursor(204, yPos); tft.print("MHz");
    tft.setCursor(330, yPos); tft.print("MODE");
  } else {
    tft.setCursor(5, yPos);   tft.print("UTC");
    tft.setCursor(50, yPos);  tft.print("CALLSIGN");
    tft.setCursor(125, yPos); tft.print("MHz");
    tft.setCursor(240, yPos); tft.print("MODE");
    tft.setCursor(220, yPos); tft.print(sanitizePolishToAscii(String(tr(TR_COUNTRY))));
  }
  tft.drawFastHLine(0, yPos + 10, 480, TFT_DARKGREY);
  yPos += enlarged ? 20 : 18;

  int displayCount = 0;
  for (int i = 0; i < hamalertSpotCount && displayCount < maxRows; i++) {
    if (!spotMatchesScreen8Filters(hamalertSpots[i])) {
      continue;
    }
    if (yPos >= (tableBottom - 2)) {
      break;
    }
    if (enlarged) {
      if (displayCount % 2 == 0) {
        tft.fillRect(0, yPos - 5, 480, 24, TFT_TABLE_ALT_ROW_COLOR);
      }

      tft.setTextSize(2);

      tft.setTextColor(TFT_LIGHTGREY);
      tft.setCursor(5, yPos);
      tft.print(formatHamalertTftTime(hamalertSpots[i].time));

      tft.setTextColor(TFT_WHITE);
      tft.setCursor(74, yPos);
      String callText = hamalertSpots[i].callsign;
      if (callText.length() > 8) callText = callText.substring(0, 8);
      tft.print(callText);

      tft.setTextColor(TFT_YELLOW);
      tft.setCursor(204, yPos);
      tft.print(hamalertSpots[i].frequency, 3);

      tft.setTextColor(TFT_GREEN);
      tft.setCursor(330, yPos);
      String modeText = hamalertSpots[i].mode;
      if (modeText.length() > 4) modeText = modeText.substring(0, 4);
      tft.print(modeText);

      yPos += 27;
    } else {
      if (displayCount % 2 == 0) {
        tft.fillRect(0, yPos - 2, 480, 16, TFT_TABLE_ALT_ROW_COLOR);
      }

      tft.setTextSize(1);

      tft.setTextColor(TFT_LIGHTGREY);
      tft.setCursor(5, yPos);
      tft.print(formatHamalertTftTime(hamalertSpots[i].time));

      tft.setTextColor(TFT_WHITE);
      tft.setCursor(50, yPos);
      tft.print(hamalertSpots[i].callsign);

      tft.setTextColor(TFT_YELLOW);
      tft.setCursor(125, yPos);
      tft.print(hamalertSpots[i].frequency, 3);

      tft.setTextColor(TFT_GREEN);
      tft.setCursor(240, yPos);
      tft.print(hamalertSpots[i].mode);

      tft.setTextColor(TFT_RADIO_ORANGE);
      tft.setCursor(220, yPos);
      String countryText = formatDistanceOrCountry(hamalertSpots[i], COUNTRY_COL_MAX_LEN);
      tft.print(countryText);

      yPos += 17;
    }
    displayCount++;
  }

  if (displayCount == 0) {
    tft.setTextColor(UI_COL_WARN);
    tft.setTextSize(2);
    tft.setCursor(40, 120);
    tft.print("WAITING FOR SPOTS...");
  }
  
  // StrzaĹ‚ki nawigacyjne - ZAWSZE rysuj po odĹ›wieĹĽeniu tabeli
  drawSwitchScreenFooter();
}

bool readRawTouchPoint(int16_t &rx, int16_t &ry) {
  if (!touchReady) {
    return false;
  }
  if (!touch.touched()) {
    return false;
  }
  TS_Point p = touch.getPoint();
  // Niepodlaczony/wiszacy XPT2046 zwraca stale x=0,y=0,z=4095, co biblioteka
  // uznaje za dotyk. Prawdziwy dotyk nigdy nie daje dokladnie 0,0.
  if ((p.x == 0 && p.y == 0) || p.z >= 4000) {
    return false;
  }
  rx = p.x;
  ry = p.y;
  return true;
}

void mapRawToScreenWithValues(int16_t rx, int16_t ry,
                              int xmin, int xmax, int ymin, int ymax,
                              bool swapXY, bool invertX, bool invertY,
                              uint16_t &x, uint16_t &y) {
  int16_t mx = rx;
  int16_t my = ry;

  if (swapXY) {
    int16_t tmp = mx;
    mx = my;
    my = tmp;
  }
  if (invertX) {
    mx = xmax - (mx - xmin);
  }
  if (invertY) {
    my = ymax - (my - ymin);
  }

  mx = constrain(mx, xmin, xmax);
  my = constrain(my, ymin, ymax);

  int screenW = tft.width();
  int screenH = tft.height();
  x = map(mx, xmin, xmax, 0, screenW - 1);
  y = map(my, ymin, ymax, 0, screenH - 1);
}

void mapRawToScreen(int16_t rx, int16_t ry, uint16_t &x, uint16_t &y) {
  mapRawToScreenWithValues(rx, ry, touchXMin, touchXMax, touchYMin, touchYMax,
                           touchSwapXY, touchInvertX, touchInvertY, x, y);
}

bool readTouchPoint(uint16_t &x, uint16_t &y) {
  int16_t rx = 0;
  int16_t ry = 0;
  if (!readRawTouchPoint(rx, ry)) {
    return false;
  }
  mapRawToScreen(rx, ry, x, y);
  return true;
}

const int SCREEN_ORDER_COUNT = 14;//SCREEN_PAGE_COUNT;

ScreenType normalizeScreenType(uint8_t raw) {
  switch (raw) {
    case SCREEN_HAM_CLOCK:
    case SCREEN_DX_CLUSTER:
    case SCREEN_SUN_SPOTS:
    case SCREEN_BAND_INFO:
    case SCREEN_WEATHER_DSP:
    case SCREEN_WEATHER_FORECAST:
    case SCREEN_APRS_IS:
    case SCREEN_APRS_RADAR:
    case SCREEN_POTA_CLUSTER:
    case SCREEN_HAMALERT_CLUSTER:
    case SCREEN_PSK_MAP:
    case SCREEN_MATRIX_CLOCK:
    case SCREEN_SYSTEM_INFO:
    case SCREEN_ISS_PASS_TRACKING:
      return (ScreenType)raw;
    default:
      return SCREEN_OFF;
  }
}

const char* screenTypeToCodeStr(ScreenType t) {
  switch (t) {
    case SCREEN_HAM_CLOCK: return "hamclock";
    case SCREEN_DX_CLUSTER: return "dxcluster";
    case SCREEN_APRS_IS: return "aprsis";
    case SCREEN_APRS_RADAR: return "aprsradar";
    // Kody musza zgadzac sie z opcjami <select> w data/index.html (bandinfo/sunspots),
    // inaczej WWW nie potrafi ustawic wartosci i pola sa puste.
    case SCREEN_BAND_INFO: return "bandinfo";
    case SCREEN_SUN_SPOTS: return "sunspots";
    case SCREEN_WEATHER_DSP: return "weather";
    case SCREEN_WEATHER_FORECAST: return "weatherforecast";
    case SCREEN_POTA_CLUSTER: return "pota";
    case SCREEN_HAMALERT_CLUSTER: return "hamalert";
    case SCREEN_PSK_MAP: return "pskmap";
    case SCREEN_MATRIX_CLOCK: return "matrix";
    case SCREEN_SYSTEM_INFO: return "systeminfo";
    case SCREEN_ISS_PASS_TRACKING: return "iss";
    case SCREEN_OFF:
    default:
      return "off";
  }
}

ScreenType screenCodeToType(const String &code) {
  String c = code;
  c.toLowerCase();
  c.trim();
  if (c == "hamclock") return SCREEN_HAM_CLOCK;
  if (c == "dxcluster") return SCREEN_DX_CLUSTER;
  if (c == "aprsis") return SCREEN_APRS_IS;
  if (c == "aprsradar") return SCREEN_APRS_RADAR;
  if (c == "bandinfo" || c == "solarindex" || c == "solar indeks") return SCREEN_BAND_INFO;
  if (c == "sunspots" || c == "propagacja") return SCREEN_SUN_SPOTS;
  if (c == "weather") return SCREEN_WEATHER_DSP;
  if (c == "weatherforecast" || c == "forecast") return SCREEN_WEATHER_FORECAST;
  if (c == "pota") return SCREEN_POTA_CLUSTER;
  if (c == "hamalert") return SCREEN_HAMALERT_CLUSTER;
  if (c == "pskmap" || c == "pskreporter") return SCREEN_PSK_MAP;
  if (c == "matrix") return SCREEN_MATRIX_CLOCK;
  if (c == "systeminfo" || c == "unlishunter") return SCREEN_SYSTEM_INFO;
  if (c == "iss") return SCREEN_ISS_PASS_TRACKING;
  return SCREEN_OFF;
}

void loadDefaultScreenOrder() {
  for (int i = 0; i < SCREEN_ORDER_COUNT; i++) {
    screenOrder[i] = DEFAULT_SCREEN_ORDER[i];
  }
}

void ensureScreenOrderValid() {
  bool hasActive = false;
  bool seen[15] = {false}; // indeksy odpowiadajÄ… ScreenType wartoĹ›ciom (0..14)
  for (int i = 0; i < SCREEN_ORDER_COUNT; i++) {
    screenOrder[i] = normalizeScreenType(screenOrder[i]);
    ScreenType t = screenOrder[i];
    if (t != SCREEN_OFF) {
      if (t >= 0 && t <= SCREEN_ISS_PASS_TRACKING) {
        if (seen[t]) {
          // drugi raz ten sam ekran Ă˘â‚¬â€ś usuĹ„ duplikat
          screenOrder[i] = SCREEN_OFF;
          continue;
        }
        seen[t] = true;
      }
      hasActive = true;
    }
  }
  if (!hasActive) {
    loadDefaultScreenOrder();
  }
}

int findScreenOrderIndex(ScreenType screenId) {
  for (int i = 0; i < SCREEN_ORDER_COUNT; i++) {
    if (screenOrder[i] == screenId) {
      return i;
    }
  }
  return 0;
}

ScreenType firstActiveScreen() {
  for (int i = 0; i < SCREEN_ORDER_COUNT; i++) {
    if (screenOrder[i] != SCREEN_OFF) {
      return screenOrder[i];
    }
  }
  return SCREEN_HAM_CLOCK;
}

ScreenType getNextScreenId(ScreenType currentId) {
  int idx = findScreenOrderIndex(currentId);
  for (int step = 1; step <= SCREEN_ORDER_COUNT; step++) {
    ScreenType candidate = screenOrder[(idx + step) % SCREEN_ORDER_COUNT];
    if (candidate != SCREEN_OFF) {
      return candidate;
    }
  }
  return SCREEN_HAM_CLOCK;
}

ScreenType getPrevScreenId(ScreenType currentId) {
  int idx = findScreenOrderIndex(currentId);
  for (int step = 1; step <= SCREEN_ORDER_COUNT; step++) {
    int prev = idx - step;
    if (prev < 0) {
      prev += SCREEN_ORDER_COUNT;
    }
    ScreenType candidate = screenOrder[prev];
    if (candidate != SCREEN_OFF) {
      return candidate;
    }
  }
  return SCREEN_HAM_CLOCK;
}

static int normalizeTftAutoSwitchTimeSec(int seconds) {
  if (seconds < 1) return 1;
  if (seconds > 3600) return 3600;
  return seconds;
}

static void applyTftAutoSwitchTimeSec(int seconds) {
  tftAutoSwitchTimeSec = normalizeTftAutoSwitchTimeSec(seconds);
}

static void resetTftAutoSwitchTimer() {
  tftAutoSwitchLastMs = millis();
  tftAutoSwitchLastScreen = currentScreen;
}

void drawQrzPopup();
void handleQrzPopupTouch(uint16_t x, uint16_t y);
void openQrzPopup(const String &callsign);

void handleTouchNavigation() {
  if (!tftInitialized) {
    return;
  }

  const uint16_t menuHitW = 50;
  const uint16_t menuHitH = 50;

  static bool touchActive = false;
  static unsigned long lastTouchMs = 0;
  static unsigned long touchStartMs = 0;
  static uint16_t touchStartX = 0;
  static uint16_t touchStartY = 0;
  static bool longPressHandled = false;
  static bool matrixGameHoldCandidate = false;
  unsigned long now = millis();

  uint16_t x = 0;
  uint16_t y = 0;
  int16_t rawX = 0;
  int16_t rawY = 0;
  if (readRawTouchPoint(rawX, rawY)) {
    // JeĹ›li ekran jest w trybie uĹ›pienia - wybudĹş go
    if (screenSleepActive) {
      wakeUpFromSleep();
      return;
    }
    mapRawToScreen(rawX, rawY, x, y);
    resetScreenSaverActivity();  // Reset wygaszacza przy aktywnoĹ›ci uĹĽytkownika
    // Reset timera uĹ›pienia przy aktywnoĹ›ci
    screenSleepLastActivityMs = millis();
    bool isNewTap = false;
    if (!touchActive) {
      if ((now - lastTouchMs) <= 150) {
        return;
      }
      touchActive = true;
      lastTouchMs = now;
      touchStartMs = now;
      touchStartX = x;
      touchStartY = y;
      longPressHandled = false;
      matrixGameHoldCandidate = (currentScreen == SCREEN_MATRIX_CLOCK && !inMenu && x >= 320 && y <= 80);
      isNewTap = true;
    }

    if (touchCalActive) {
      handleTouchCalibrationTouch(rawX, rawY, x, y, isNewTap);
      return;
    }

    if (aprsAlertScreenActive) {
      if (isNewTap && isAprsAlertCloseButtonHit(x, y)) {
        dismissAprsAlertScreen();
      }
      return;
    }

    // ObsĹ‚uga wygaszacza ekranu - dotkniÄ™cie wchodzi w menu ustawieĹ„
    if (screenSaverActive) {
      if (isNewTap) {
        resetScreenSaverActivity();
        inMenu = true;
        screenSaverMenuActive = true;
        drawScreenSaverMenu();
      }
      return;
    }

    if (!brightnessMenuActive && !longPressHandled && matrixGameHoldCandidate) {
      if (currentScreen == SCREEN_MATRIX_CLOCK && !inMenu && (now - touchStartMs) >= 2000) {
        currentScreen = SCREEN_SYSTEM_INFO;
        drawScreen(currentScreen);
        resetTftAutoSwitchTimer();
        longPressHandled = true;
        matrixGameHoldCandidate = false;
        return;
      }
    }

    if (!brightnessMenuActive && !longPressHandled) {
      if (now - touchStartMs >= 3000) {
        brightnessMenuPrevScreen = currentScreen;
        brightnessMenuPrevInMenu = inMenu;
        brightnessMenuPrevBacklight = backlightPercent;
        brightnessMenuPrevThemeHue = menuThemeHue;
        brightnessMenuPrevClockColor = clockDigitsColor;
        brightnessMenuActive = true;
        brightnessMenuValue = backlightPercent;
        brightnessMenuOpenedMs = now;
        inMenu = true;
        drawBrightnessMenu();
        longPressHandled = true;
        return;
      }
    }

    // Przytrzymanie palca NA ZNAKU (ekran HAM CLOCK) otwiera menu koloru znaku.
    if (!brightnessMenuActive && !callsignColorMenuActive && !clockColorMenuActive && !longPressHandled &&
        currentScreen == SCREEN_HAM_CLOCK && !inMenu && (now - touchStartMs) >= 1200 &&
        isPointInRect(x, y, callsignHitX, callsignHitY, callsignHitW, callsignHitH)) {
      longPressHandled = true;
      openCallsignColorMenu();
      return;
    }

    // Przytrzymanie palca NA ZEGARZE (ekran HAM CLOCK) otwiera menu koloru zegara.
    if (!brightnessMenuActive && !callsignColorMenuActive && !clockColorMenuActive && !longPressHandled &&
        currentScreen == SCREEN_HAM_CLOCK && !inMenu && (now - touchStartMs) >= 1200 &&
        isPointInRect(x, y, clockHitX, clockHitY, clockHitW, clockHitH)) {
      longPressHandled = true;
      openClockColorMenu();
      return;
    }

    if (brightnessMenuActive) {
      handleBrightnessMenuTouch(x, y, isNewTap);
      return;
    }

    if (callsignColorMenuActive) {
      handleCallsignColorMenuTouch(x, y, isNewTap);
      return;
    }

    if (clockColorMenuActive) {
      handleClockColorMenuTouch(x, y, isNewTap);
      return;
    }

    if (!isNewTap) {
      return;
    }


    if (inMenu) {
      if (screenSaverMenuActive) {
        handleScreenSaverMenuTouch(x, y);
        return;
      }
      if (screenSleepMenuActive) {
        handleScreenSleepMenuTouch(x, y);
        return;
      }
      if (currentScreen == SCREEN_HAM_CLOCK) {
        handleScreen1MenuTouch(x, y);
      }
      if (currentScreen == SCREEN_DX_CLUSTER) {
        handleScreen2MenuTouch(x, y);
      }
      if (currentScreen == SCREEN_POTA_CLUSTER) {
        handleScreen7MenuTouch(x, y);
      }
      if (currentScreen == SCREEN_HAMALERT_CLUSTER) {
        handleScreen8MenuTouch(x, y);
      }
      if (currentScreen == SCREEN_APRS_IS || currentScreen == SCREEN_APRS_RADAR) {
        handleScreen6MenuTouch(x, y);
      }
      if (currentScreen == SCREEN_WEATHER_DSP || currentScreen == SCREEN_WEATHER_FORECAST) {
        handleScreen5MenuTouch(x, y);
      }
      return;
    }

    if (qrzPopupActive) {
      handleQrzPopupTouch(x, y);
      return;
    }

    if (currentScreen == SCREEN_DX_CLUSTER) {
      if (x < menuHitW && y < menuHitH) {
          inMenu = true;
          drawDxClusterFilterMenu();
          return;
        }
      // KlikniÄ™cie w znak (callsign) - sprawdĹş czy dotkniÄ™to wiersza z spotem
      if (y >= 40 && y < 280 && x >= 50 && x <= 200) {
        // Oblicz ktĂłry wiersz zostaĹ‚ dotkniÄ™ty
        int rowHeight = isDxTableEnlarged() ? 27 : 20;
        int headerOffset = 40;
        int clickedRow = (y - headerOffset) / rowHeight;
        if (clickedRow >= 0) {
          // ZnajdĹş spot w tablicy
          lockDxSpots();
          int displayCount = 0;
          for (int i = 0; i < spotCount; i++) {
            if (!spotMatchesScreen2Filters(spots[i])) continue;
            if (displayCount == clickedRow) {
              // KlikniÄ™to w ten spot
              String callsign = spots[i].callsign;
              unlockDxSpots();
              openQrzPopup(callsign);
              return;
            }
            displayCount++;
          }
          unlockDxSpots();
        }
      }
    }
    if (currentScreen == SCREEN_POTA_CLUSTER) {
        if (x < menuHitW && y < menuHitH) {
          inMenu = true;
          drawPotaFilterMenu();
          return;
        }
      // KlikniÄ™cie w znak (callsign) - sprawdĹş czy dotkniÄ™to wiersza z spotem
      if (y >= 40 && y < 280 && x >= 50 && x <= 200) {
        // Oblicz ktĂłry wiersz zostaĹ‚ dotkniÄ™ty
        int rowHeight = isDxTableEnlarged() ? 27 : 20;
        int headerOffset = 40;
        int clickedRow = (y - headerOffset) / rowHeight;
        if (clickedRow >= 0) {
          // ZnajdĹş spot w tablicy POTA
          int displayCount = 0;
          for (int i = 0; i < potaSpotCount; i++) {
            if (!spotMatchesScreen7Filters(potaSpots[i])) continue;
            if (displayCount == clickedRow) {
              // KlikniÄ™to w ten spot
              String callsign = potaSpots[i].callsign;
              openQrzPopup(callsign);
              return;
            }
            displayCount++;
          }
        }
      }
    }
    if (currentScreen == SCREEN_HAMALERT_CLUSTER) {
        if (x < menuHitW && y < menuHitH) {
          inMenu = true;
          drawHamalertFilterMenu();
          return;
        }
      }
      if (currentScreen == SCREEN_APRS_IS) {
      if (x < menuHitW && y < menuHitH) {
        inMenu = true;
        screen6MenuBeaconingTemp = aprsBeaconEnabled;
        screen6MenuAprsAlertTemp = aprsAlertEnabled;
        screen6MenuRangeAlertTemp = aprsAlertNearbyEnabled;
        screen6MenuLedAlertTemp = enableLedAlert;
        drawAprsSortMenu();
        return;
      }
    }
      if (currentScreen == SCREEN_APRS_RADAR) {
      int tileIdx = -1;
      if (isScreen6RadarRangeTileHit(x, y, tileIdx)) {
        if ((now - screen6RadarLastZoomTapMs) < SCREEN6_RADAR_ZOOM_TAP_COOLDOWN_MS) {
          return;
        }
        screen6RadarLastZoomTapMs = now;
        if (tileIdx >= 0 && tileIdx < APRS_RADAR_RANGE_COUNT &&
            tileIdx != aprsRadarRangeIndex) {
          aprsRadarRangeIndex = tileIdx;
          triggerScreen6RadarHint();
          drawScreen(SCREEN_APRS_RADAR);
          resetTftAutoSwitchTimer();
        }
        return;
      }
      if (x < menuHitW && y < menuHitH) {
        inMenu = true;
        screen6MenuBeaconingTemp = aprsBeaconEnabled;
        screen6MenuAprsAlertTemp = aprsAlertEnabled;
        screen6MenuRangeAlertTemp = aprsAlertNearbyEnabled;
        screen6MenuLedAlertTemp = enableLedAlert;
        drawAprsSortMenu();
        return;
      }
    }
    if (currentScreen == SCREEN_HAM_CLOCK) {
      if (screen1ClockStyle == 1) {
        // Styl klasyczny: menu czasu otwiera sie kliknieciem w prawy gorny rog.
        if (x > 285 && y < 35) {
          inMenu = true;
          drawHamClockTimeMenu();
          return;
        }
      } else {
        // Nowy uklad ma ten sam naglowek co domyslny: menu czasu otwiera sie
        // kliknieciem w prawy gorny rog ekranu.
        if (x > 285 && y < 35) {
          inMenu = true;
          drawHamClockTimeMenu();
          return;
        }
      }
    }
    if (currentScreen == SCREEN_WEATHER_DSP || currentScreen == SCREEN_WEATHER_FORECAST) {
      if (x < menuHitW && y < menuHitH) {
        inMenu = true;
        drawWeatherMenu();
        return;
      }
    }

    // Nawigacja: dolne obszary dotyku (ok. 80x180)
    // Zamienione kierunki: lewa strona = w prawo (next), prawa strona = w lewo (prev)
    // TYLKO gdy nie jesteĹ›my w ĹĽadnym menu i nie ma aktywnej klawiatury
    if (!inMenu && !brightnessMenuActive && !screenSaverMenuActive && !screenSleepMenuActive && !touchCalActive && !pskKeyboardActive) {
      const uint16_t cornerY = 60;
      const uint16_t cornerX = 80;
      if (y >= cornerY && x < cornerX) {
        currentScreen = getNextScreenId(currentScreen);
        drawScreen(currentScreen);
        resetTftAutoSwitchTimer();
        return;
      } else if (y >= cornerY && x >= (480 - cornerX)) {
        currentScreen = getPrevScreenId(currentScreen);
        drawScreen(currentScreen);
        resetTftAutoSwitchTimer();
        return;
      }
    }

    // ObsĹ‚uga PSK map - na koĹ„cu, aby nie blokowaÄ‡ nawigacji w rogach
    if (currentScreen == SCREEN_PSK_MAP) {
      handlePskMapTouch(x, y);
      return;
    }
  } else {
    touchActive = false;
    longPressHandled = false;
    matrixGameHoldCandidate = false;
    // Reset licznikĂłw long-press dla menu jasnoĹ›ci po puszczeniu dotyku
    if (brightnessMenuActive) {
      brightnessMenuTouchStartMs = 0;
      brightnessMenuLongPressHandled = false;
    }
  }
}
// Ekran 2: DX Cluster
void drawDxCluster() {
  // ekran czysci drawScreen (wspolny styl ISS)

  // 1. NAGĂ„Ä…Ă‚ÂĂ„â€šĂ˘â‚¬Ĺ›WEK: Belka z menu, nazwÄ‚â€žĂ˘â‚¬Â¦ klastra i czasem UTC

// IKONA MENU (3D)

// Nazwa klastra - przesuniÄ™ta w prawo (x=35 zamiast 5), by zrobiÄ‡ miejsce na menu

  // Ikona WiFi w gĂłrnym prawym rogu
  
  // Ikona baterii obok ikony WiFi

  // ZEGAR - rysowany osobno przez updateDxClusterClock()

  // 2. NAGĹĂ“WKI TABELI
  const bool enlarged = isDxTableEnlarged();
  int yPos = 40;
  tft.setTextColor(UI_COL_FAINT);
  tft.setTextSize(1);

  if (enlarged) {
    tft.setCursor(5, yPos);   tft.print("CZAS");
    tft.setCursor(74, yPos);  tft.print("CALL");
    tft.setCursor(204, yPos); tft.print("MHz");
    tft.setCursor(330, yPos); tft.print("MODE");
  } else {
    tft.setCursor(5, yPos);   tft.print("CZAS");
    tft.setCursor(50, yPos);  tft.print("CALLSIGN");
    tft.setCursor(140, yPos); tft.print("MHz");
    tft.setCursor(260, yPos); tft.print("MODE");
    tft.setCursor(360, yPos); tft.print(sanitizePolishToAscii(String(tr(TR_COUNTRY))));
  }
  tft.drawFastHLine(0, yPos + 10, 480, TFT_DARKGREY);
  yPos += enlarged ? 20 : 18;

  // 3. LISTA SPOTĂ„â€šĂ˘â‚¬Ĺ›W
  int maxRows = getDxTableMaxRows();
  int displayCount = 0;
  lockDxSpots();
  for (int i = 0; i < spotCount && displayCount < maxRows; i++) {
    if (!spotMatchesScreen2Filters(spots[i])) {
      continue;
    }
    if (enlarged) {
      if (displayCount % 2 == 0) tft.fillRect(0, yPos - 5, 480, 24, TFT_TABLE_ALT_ROW_COLOR);
      tft.setTextSize(2);
      tft.setTextColor(TFT_LIGHTGREY);
      tft.setCursor(5, yPos);
      tft.print(formatSpotUtc(spots[i].time));

      tft.setTextColor(TFT_WHITE);
      tft.setCursor(74, yPos);
      String callText = spots[i].callsign;
      if (callText.length() > 8) callText = callText.substring(0, 8);
      tft.print(callText);

      tft.setTextColor(TFT_YELLOW);
      tft.setCursor(204, yPos);
      tft.print(spots[i].frequency / 1000.0, 3);

      tft.setTextColor(TFT_GREEN);
      tft.setCursor(330, yPos);
      String modeText = spots[i].mode;
      if (modeText.length() > 4) modeText = modeText.substring(0, 4);
      tft.print(modeText);
      yPos += 27;
    } else {
      if (displayCount % 2 == 0) tft.fillRect(0, yPos - 2, 480, 16, TFT_TABLE_ALT_ROW_COLOR);

      // Znak wywoĹ‚awczy - rozmiar 1 (standardowy)
      tft.setTextSize(1);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setCursor(50, yPos);
      String callStr = spots[i].callsign;
      callStr.toUpperCase();
      if (callStr.length() > 8) callStr = callStr.substring(0, 8);
      tft.print(callStr);
      
      // CzÄ™stotliwoĹ›Ä‡ - rozmiar 1
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.setCursor(140, yPos);
      tft.print(spots[i].frequency / 1000.0, 3);
      
      // Mode - rozmiar 1
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.setCursor(260, yPos);
      String modeStr = spots[i].mode;
      if (modeStr.length() > 4) modeStr = modeStr.substring(0, 4);
      tft.print(modeStr);
      
      // Czas i kraj - rozmiar 1 (dopasowane do nagĹ‚ĂłwkĂłw)
      tft.setTextSize(1);
      tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      tft.setCursor(5, yPos + 5);
      tft.print(formatSpotUtc(spots[i].time));
      
      tft.setTextColor(TFT_ORANGE, TFT_BLACK);
      tft.setCursor(360, yPos + 5);
      tft.print(formatDistanceOrCountry(spots[i], COUNTRY_COL_MAX_LEN));

      yPos += 20;
    }
    displayCount++;
  }
  unlockDxSpots();

  // 4. Pasek nawigacji - ZAWSZE widoczny (zmienione z 5 sekund)

  if (displayCount == 0) {
    tft.setTextColor(UI_COL_WARN);
    tft.setTextSize(2);
    tft.setCursor(40, 120);
    tft.print("WAITING FOR SPOTS...");
  }
  drawSwitchScreenFooter(); // Dodano rysowanie strzaĹ‚ek nawigacyjnych
}

bool isPointInRect(uint16_t x, uint16_t y, int rx, int ry, int rw, int rh) {
  return (x >= rx && x <= (rx + rw) && y >= ry && y <= (ry + rh));
}

void drawHamburgerMenuButton3D(int x, int y) {
  const int iconW = 20;
  const int iconH = 17;
  const int pad = 3;
  const int btnX = x - pad;
  const int btnY = y - pad;
  const int btnW = iconW + (pad * 2);
  const int btnH = iconH + (pad * 2);
  const uint16_t edgeLight = lerpColor565(TFT_WHITE, TFT_RADIO_ORANGE, 140);
  const uint16_t edgeShadow = lerpColor565(TFT_BLACK, TFT_RADIO_ORANGE, 115);

  tft.fillRoundRect(btnX, btnY, btnW, btnH, 3, TFT_RADIO_ORANGE);
  tft.drawRoundRect(btnX, btnY, btnW, btnH, 3, edgeLight);
  tft.drawLine(btnX + 1, btnY + btnH - 1, btnX + btnW - 2, btnY + btnH - 1, edgeShadow);
  tft.drawLine(btnX + btnW - 1, btnY + 1, btnX + btnW - 1, btnY + btnH - 2, edgeShadow);

  tft.fillRect(x, y, iconW, 3, TFT_BLACK);
  tft.fillRect(x, y + 7, iconW, 3, TFT_BLACK);
  tft.fillRect(x, y + 14, iconW, 3, TFT_BLACK);
}

// Hamburger w stylu ISS (teal na ciemnej karcie) - zgodny z obszarem dotyku menu.
static void uiHamburger(int x, int y) {
  const bool classic = uiStyleClassic();
  const int iconW = 20, iconH = 17, pad = 3;
  const int btnX = x - pad, btnY = y - pad;
  const int btnW = iconW + pad * 2, btnH = iconH + pad * 2;
  // Klasyczny: czarna ikona na pomaranczowym naglowku (jak w zegarze klasycznym).
  const uint16_t btnBg = classic ? TFT_RADIO_ORANGE : UI_COL_CARD;
  const uint16_t ink = classic ? TFT_BLACK : UI_COL_ACCENT2;
  tft.fillRoundRect(btnX, btnY, btnW, btnH, 3, btnBg);
  tft.drawRoundRect(btnX, btnY, btnW, btnH, 3, ink);
  tft.fillRect(x, y, iconW, 3, ink);
  tft.fillRect(x, y + 7, iconW, 3, ink);
  tft.fillRect(x, y + 14, iconW, 3, ink);
}

// Naglowek ekranu: nowoczesny = ciemna karta + teal linia, klasyczny =
// pomaranczowa karta + czarny tytul (jak naglowek zegara klasycznego).
// Tytul jest automatycznie dopasowywany fontem: najpierw Font 2 (16 px), a gdy
// nie miesci sie miedzy ikona menu (x~34) a WiFi (x~420), schodzimy do Font 1.
static void uiHeader(const char *title, bool showMenu) {
  const bool classic = uiStyleClassic();
  const uint16_t bg = classic ? TFT_RADIO_ORANGE : UI_COL_CARD;
  const uint16_t fg = classic ? TFT_BLACK : UI_COL_TEXT;
  const uint16_t line = classic ? 0x8410 : UI_COL_ACCENT;
  tft.fillRect(0, 0, 480, UI_HEADER_H, bg);
  tft.drawFastHLine(0, UI_HEADER_H - 1, 480, line);
  if (showMenu) uiHamburger(5, 4);
  tft.setTextColor(fg, bg);
  // drawString(..., font) skaluje sie globalnym textsize - ekran zegara zostawia
  // textsize=7, co rozdmuchuje tytul naglowka (np. "POTA.app") przy wejsciu.
  tft.setTextSize(1);
  const int leftLimit = 34;
  const int rightLimit = 415;
  const int availW = rightLimit - leftLimit;
  // textWidthFont() jest niezalezne od setTextSize() - mierzymy realna szerokosc
  // napisu w danym foncie (Font 2 = 16 px, Font 1 = 8 px).
  const int w2 = tft.textWidthFont(title, 2);
  const int w1 = tft.textWidthFont(title, 1);
  if (w2 <= availW) {
    tft.setTextDatum(TC_DATUM);
    tft.drawString(title, 240, 5, 2);
  } else if (w1 <= availW) {
    tft.setTextDatum(TC_DATUM);
    tft.drawString(title, 240, 9, 1);
  } else {
    // Ostatecznosc: przytnij Font 2 tak, zeby zmiescil sie w naglowku.
    String t(title);
    while (t.length() > 1 && tft.textWidthFont(t.c_str(), 2) > availW) {
      t.remove(t.length() - 1);
    }
    tft.setTextDatum(TC_DATUM);
    tft.drawString(t, 240, 5, 2);
  }
  tft.setTextDatum(TL_DATUM);
  drawWifiSignalBars(445, 1);
  drawBatteryIcon(420, 1);
}

// Naglowek MENU: nowoczesny = ciemna karta + teal linia, klasyczny =
// pomaranczowa karta + czarny tytul (spojny z naglowkiem zegara klasycznego).
// Uzywany przez wszystkie podmenu (filtry DX/POTA/HamAlert, APRS, pogoda, czas,
// jasnosc, kolor znaku, uspienie).
static void uiMenuHeader(const char *title) {
  const bool classic = uiStyleClassic();
  const uint16_t bg = classic ? TFT_RADIO_ORANGE : UI_COL_CARD;
  const uint16_t fg = classic ? TFT_BLACK : UI_COL_TEXT;
  const uint16_t line = classic ? 0x8410 : TFT_RADIO_ORANGE;
  tft.fillRect(0, 0, 480, 30, bg);
  tft.drawFastHLine(0, 29, 480, line);
  tft.setTextColor(fg, bg);
  tft.setTextSize(1);
  int w1 = tft.textWidth(title);
  if (w1 <= 450) {
    tft.setCursor(10, 11);
    tft.print(title);
  } else {
    tft.setTextSize(2);
    int w2 = tft.textWidth(title);
    if (w2 <= 460) {
      tft.setCursor(10, 7);
      tft.print(title);
    } else {
      String t(title);
      tft.setTextSize(2);
      while (t.length() > 1 && tft.textWidth(t) > 460) t.remove(t.length() - 1);
      tft.setCursor(10, 7);
      tft.print(t);
    }
  }
  tft.setTextColor(fg, bg);
}

// Zegar UTC w lewym gornym rogu naglowka (mniejszy, obok hamburgera przy menu).
static void uiHeaderClock() {
  if (!tftInitialized || inMenu || brightnessMenuActive || screenSaverMenuActive ||
      screenSleepMenuActive || touchCalActive || pskKeyboardActive) {
    return;
  }
  String utc = getUtcTimeString();
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.fillRect(34, 4, 60, 16, UI_COL_CARD);
  tft.setTextColor(UI_COL_ACCENT, UI_COL_CARD);
  tft.drawString(utc.substring(0, 5), 36, 8, 1);
  tft.setTextDatum(TL_DATUM);
}

// Stopka: strzalki < > + podpis "SWITCH SCREEN" (styl ISS).
static void uiFooter() {
  if (inMenu || brightnessMenuActive || screenSaverMenuActive ||
      screenSleepMenuActive || touchCalActive || pskKeyboardActive) {
    return;
  }
  const int arrowY = 309;
  const int arrowSize = 10;
  const uint16_t arrowCol = uiStyleClassic() ? TFT_RADIO_ORANGE : UI_COL_ACCENT;
  tft.fillTriangle(14, arrowY, 14 + arrowSize, arrowY - arrowSize, 14 + arrowSize, arrowY + arrowSize, arrowCol);
  tft.fillTriangle(466, arrowY, 466 - arrowSize, arrowY - arrowSize, 466 - arrowSize, arrowY + arrowSize, arrowCol);
  tft.setTextDatum(TC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(UI_COL_FAINT, UI_COL_BG);
  tft.drawString("SWITCH SCREEN", 240, UI_FOOTER_Y, 1);
  tft.setTextDatum(TL_DATUM);
}

// Hamburger rysowany do sprite'a radaru (ten sam wyglad co wersja na tft).
// Rysowany w sprite przed pushSprite => brak migotania przy kazdej klatce.
static void drawHamburgerMenuButton3DSprite(TFT_eSprite &sp, int x, int y) {
  const int iconW = 20;
  const int iconH = 17;
  const int pad = 3;
  const int btnX = x - pad;
  const int btnY = y - pad;
  const int btnW = iconW + (pad * 2);
  const int btnH = iconH + (pad * 2);
  const uint16_t edgeLight = lerpColor565(TFT_WHITE, TFT_RADIO_ORANGE, 140);
  const uint16_t edgeShadow = lerpColor565(TFT_BLACK, TFT_RADIO_ORANGE, 115);
  sp.fillRoundRect(btnX, btnY, btnW, btnH, 3, TFT_RADIO_ORANGE);
  sp.drawRoundRect(btnX, btnY, btnW, btnH, 3, edgeLight);
  sp.drawLine(btnX + 1, btnY + btnH - 1, btnX + btnW - 2, btnY + btnH - 1, edgeShadow);
  sp.drawLine(btnX + btnW - 1, btnY + 1, btnX + btnW - 1, btnY + btnH - 2, edgeShadow);
  sp.fillRect(x, y, iconW, 3, TFT_BLACK);
  sp.fillRect(x, y + 7, iconW, 3, TFT_BLACK);
  sp.fillRect(x, y + 14, iconW, 3, TFT_BLACK);
}

static void drawScreen6ViewToggleButton3D(int x, int y, Screen6ViewMode mode) {
  const int iconW = 20;
  const int iconH = 17;
  const int pad = 3;
  const int btnX = x - pad;
  const int btnY = y - pad;
  const int btnW = iconW + (pad * 2);
  const int btnH = iconH + (pad * 2);
  const uint16_t edgeLight = lerpColor565(TFT_WHITE, TFT_RADIO_ORANGE, 140);
  const uint16_t edgeShadow = lerpColor565(TFT_BLACK, TFT_RADIO_ORANGE, 115);

  tft.fillRoundRect(btnX, btnY, btnW, btnH, 3, TFT_RADIO_ORANGE);
  tft.drawRoundRect(btnX, btnY, btnW, btnH, 3, edgeLight);
  tft.drawLine(btnX + 1, btnY + btnH - 1, btnX + btnW - 2, btnY + btnH - 1, edgeShadow);
  tft.drawLine(btnX + btnW - 1, btnY + 1, btnX + btnW - 1, btnY + btnH - 2, edgeShadow);

  tft.setTextSize(2);
  tft.setTextColor(TFT_BLACK);
  tft.setCursor(x + 6, y + 1);
  tft.print(mode == APRS_VIEW_RADAR ? "L" : "R");
}

static bool isScreen6ViewToggleHit(uint16_t x, uint16_t y) {
  return (x >= SCREEN6_VIEW_BTN_HIT_X &&
          x <= (SCREEN6_VIEW_BTN_HIT_X + SCREEN6_VIEW_BTN_HIT_W) &&
          y >= SCREEN6_VIEW_BTN_HIT_Y &&
          y <= (SCREEN6_VIEW_BTN_HIT_Y + SCREEN6_VIEW_BTN_HIT_H));
}

// Kafelki zakresu (x0.2..x4) wysrodkowane na dole ekranu - geometria wspolna
// dla rysowania i testu dotyku. Zwraca indeks trafionego kafelka.
static const int ARP_TILE_W = 52;
static const int ARP_TILE_H = 18;
static const int ARP_TILE_GAP = 6;
static const int ARP_TILE_Y = 300;

static int aprsRadarTileStartX() {
  const int total = APRS_RADAR_RANGE_COUNT * ARP_TILE_W +
                    (APRS_RADAR_RANGE_COUNT - 1) * ARP_TILE_GAP;
  return (480 - total) / 2;
}

static bool isScreen6RadarRangeTileHit(uint16_t x, uint16_t y, int &index) {
  if (currentScreen != SCREEN_APRS_RADAR) {
    return false;
  }
  if (y < ARP_TILE_Y || y > (ARP_TILE_Y + ARP_TILE_H)) {
    return false;
  }
  int startX = aprsRadarTileStartX();
  for (int i = 0; i < APRS_RADAR_RANGE_COUNT; i++) {
    int bx = startX + i * (ARP_TILE_W + ARP_TILE_GAP);
    if (x >= bx && x <= (bx + ARP_TILE_W)) {
      index = i;
      return true;
    }
  }
  return false;
}

static void triggerScreen6RadarHint() {
  screen6RadarHintUntilMs = millis() + SCREEN6_RADAR_HINT_DURATION_MS;
}

static bool isScreen6RadarHintVisible() {
  return (currentScreen == SCREEN_APRS_RADAR && millis() < screen6RadarHintUntilMs);
}

// ============================================================================
// EKRAN APRS RADAR (sprite 480x320 w PSRAM) - styl "tactical sweep".
// Cala klatka jest rysowana do bufora i wypychana jednym pushSprite(),
// wiec nie ma migotania ani resztek poprzedniej klatki. Kolory taktyczne
// wg typu stacji, panel boczny z telemetria, siatka + pierscienie + wskaznik
// polnocny, wysrodkowane kafelki zakresu (x0.2..x4) przelaczane dotykiem.
// ============================================================================

// Paleta retro-zielona + kolory taktyczne (RGB565).
static const uint16_t ARP_COL_BG    = 0x0000; // czarne tlo
static const uint16_t ARP_COL_GRID  = 0x0320; // ciemna zielen (siatka)
static const uint16_t ARP_COL_SWEEP = 0x07E0; // jaskrawa zielen (smuga)
static const uint16_t ARP_COL_TEXT  = 0x07E0; // zielony tekst
static const uint16_t ARP_COL_DIM   = 0xBDF7; // jasny szary (etykiety/wartosci)
static const uint16_t ARP_COL_MOBILE= 0xFFE0; // zolty  (pojazdy)
static const uint16_t ARP_COL_WX    = 0x07FF; // cyjan  (pogoda)
static const uint16_t ARP_COL_FIXED = 0xFFFF; // bialy  (stacje stale)
static const uint16_t ARP_COL_DIGI  = 0xF81F; // magenta (przemienniki)
static const uint16_t ARP_COL_GREEN = 0x07E0; // zielony (OK)
static const uint16_t ARP_COL_RED   = 0xF800; // czerwony (blad)
static const uint16_t ARP_COL_PANEL = 0x0841; // tlo panelu

// Geometria radaru i panelu bocznego.
static const int ARP_CX = 127;
static const int ARP_CY = 158;
static const int ARP_RADIUS = 122;
static const int ARP_PANEL_X = 262;
static const int ARP_PANEL_W = 218;

// Sprite calego ekranu w PSRAM (16bpp = 307 200 B).
TFT_eSprite aprsRadarSprite = TFT_eSprite(&tft);
static bool aprsRadarSpriteReady = false;
static bool aprsRadarSpriteInitTried = false;
// Sprite jest wspoldzielony przez UI_Task (rysowanie ekranu) i petle loop()
// (animacja smugi). Bez blokady oba zadania wypychalyby go po SPI naraz,
// co zawieszalo magistrale i powodowalo reset. Mutex serializuje pushSprite.
static SemaphoreHandle_t aprsRadarSpriteMux = nullptr;

static bool ensureAprsRadarSprite() {
  if (aprsRadarSpriteReady) return true;
  if (aprsRadarSpriteInitTried) return false;
  aprsRadarSpriteInitTried = true;
  aprsRadarSprite.setColorDepth(16);
  aprsRadarSpriteReady = (aprsRadarSprite.createSprite(480, 320) != nullptr);
  if (!aprsRadarSpriteReady) {
    Serial.println("[APRS RADAR] Brak sprite 480x320 (PSRAM?) - ekran niedostepny");
  }
  return aprsRadarSpriteReady;
}

// Kat smugi radaru (stopnie) - aktualizowany raz na klatke.
static float aprsRadarSweepAngle = 0.0f;
// Stacje "zapalone" po przejsciu smugi. Smuga je zapala, potem gasna.
static uint8_t aprsRadarBlip[MAX_APRS_STATIONS];

// Kolor taktyczny wg typu symbolu APRS.
static uint16_t aprsRadarStationColor(const APRSStation &station) {
  String sym = getAPRSSymbolShort(station);
  if (sym == "CAR" || sym == "TRUCK" || sym == "VAN" || sym == "BIKE" ||
      sym == "AMBUL" || sym == "FIRETRK" || sym == "ROVER" || sym == "SHIP") {
    return ARP_COL_MOBILE;
  }
  if (sym == "WX") return ARP_COL_WX;
  if (sym == "DIGI" || sym == "REPEATER" || sym == "NODE" || sym == "MICERPT" || sym == "LORA") {
    return ARP_COL_DIGI;
  }
  return ARP_COL_FIXED;
}

// Etykieta sekcji panelu (zielony naglowek + linia).
static void aprsRadarSection(int y, const char *txt) {
  aprsRadarSprite.setTextDatum(TL_DATUM);
  aprsRadarSprite.setTextSize(1);
  aprsRadarSprite.setTextColor(ARP_COL_TEXT, ARP_COL_PANEL);
  aprsRadarSprite.drawString(txt, ARP_PANEL_X + 6, y, 1);
  aprsRadarSprite.drawFastHLine(ARP_PANEL_X, y + 16, ARP_PANEL_W, ARP_COL_GRID);
}

// Wartosc wiersza panelu dopasowana do prawej krawedzi ramki: skracana, a gdy
// nadal za szeroka, rysowana proporcjonalnym fontem 2 (przy tym samym lewym
// marginesie i bazowej linii tekstu). Bez tego dlugie wartosci (np. wspolrzedne
// SZER/DLUG albo znak z SSID) wychodzily poza ramke panelu.
static void aprsRadarRowValue(int y, const char *value, uint16_t valCol) {
  const int x = ARP_PANEL_X + 92;
  const int maxW = (ARP_PANEL_X + ARP_PANEL_W - 6) - x;
  String v(value ? value : "");
  int font = 1;
  int charW = 6;
  if ((int)v.length() * 6 > maxW) {
    font = 2;      // proporcjonalny 16 px - wezsze znaki, wieksza czytelnosc
    charW = 8;
  }
  int fit = maxW / charW;
  if (fit < 1) fit = 1;
  if ((int)v.length() > fit) v = v.substring(0, fit);
  aprsRadarSprite.setTextDatum(TL_DATUM);
  aprsRadarSprite.setTextSize(1);
  aprsRadarSprite.setTextColor(valCol, ARP_COL_PANEL);
  // Font 2 jest o 8 px wyzszy od Font 1 - podniesienie o 4 px trzyma wspolna linie.
  aprsRadarSprite.drawString(v, x, font == 2 ? y - 4 : y, font);
}

// Wiersz "ETYKIETA: wartosc" w panelu.
static void aprsRadarRow(int y, const char *label, const char *value, uint16_t valCol) {
  aprsRadarSprite.setTextDatum(TL_DATUM);
  aprsRadarSprite.setTextSize(1);
  aprsRadarSprite.setTextColor(ARP_COL_DIM, ARP_COL_PANEL);
  aprsRadarSprite.drawString(label, ARP_PANEL_X + 6, y, 1);
  aprsRadarRowValue(y, value, valCol);
}

// Panel zakresow na dole, wysrodkowany; aktywny kafelek podswietlony.
static void aprsRadarRangeTiles() {
  const char *labels[] = {"x0.2", "x0.5", "x1", "x2", "x4"};
  const int startX = aprsRadarTileStartX();
  for (int i = 0; i < APRS_RADAR_RANGE_COUNT; i++) {
    int bx = startX + i * (ARP_TILE_W + ARP_TILE_GAP);
    int by = ARP_TILE_Y;
    bool active = (i == aprsRadarRangeIndex);
    if (active) {
      aprsRadarSprite.fillRect(bx, by, ARP_TILE_W, ARP_TILE_H, ARP_COL_TEXT);
      aprsRadarSprite.setTextColor(ARP_COL_BG, ARP_COL_TEXT);
    } else {
      aprsRadarSprite.drawRect(bx, by, ARP_TILE_W, ARP_TILE_H, ARP_COL_GRID);
      aprsRadarSprite.setTextColor(ARP_COL_TEXT, ARP_COL_BG);
    }
    aprsRadarSprite.setTextDatum(MC_DATUM);
    aprsRadarSprite.setTextSize(1);
    aprsRadarSprite.drawString(labels[i], bx + ARP_TILE_W / 2, by + ARP_TILE_H / 2, 1);
  }
  aprsRadarSprite.setTextDatum(TL_DATUM);
}

// Rysuje jedna klatke radaru. angleDeg - kat smugi; advanceBlips - czy
// aktualizowac stan "zapalenia" stacji (false przy pelnym odswiezeniu).
static void aprsRadarRenderFrame(float angleDeg, bool advanceBlips) {
  if (!ensureAprsRadarSprite()) return;
  if (aprsRadarSpriteMux == nullptr) {
    aprsRadarSpriteMux = xSemaphoreCreateMutex();
  }
  bool locked = false;
  if (aprsRadarSpriteMux != nullptr) {
    if (xSemaphoreTake(aprsRadarSpriteMux, pdMS_TO_TICKS(500)) != pdTRUE) {
      return;  // inne zadanie wlasnie rysuje - pomijamy klatke
    }
    locked = true;
  }

  TFT_eSprite &sp = aprsRadarSprite;
  sp.fillScreen(ARP_COL_BG);

  // Naglowek w stylu ISS (karta + teal linia) nad obszarem radaru (panel ma wlasne tlo).
  sp.fillRect(0, 0, ARP_PANEL_X - 1, UI_HEADER_H, UI_COL_CARD);
  sp.drawFastHLine(0, UI_HEADER_H - 1, ARP_PANEL_X - 1, UI_COL_ACCENT);
  sp.fillRoundRect(2, 1, 24, 22, 3, UI_COL_CARD);
  sp.drawRoundRect(2, 1, 24, 22, 3, UI_COL_ACCENT2);
  sp.fillRect(5, 5, 18, 2, UI_COL_ACCENT2);
  sp.fillRect(5, 11, 18, 2, UI_COL_ACCENT2);
  sp.fillRect(5, 17, 18, 2, UI_COL_ACCENT2);
  sp.setTextDatum(TC_DATUM);
  sp.setTextSize(1);
  sp.setTextColor(UI_COL_TEXT, UI_COL_CARD);
  sp.drawString("APRS RADAR", (ARP_PANEL_X - 1) / 2, 9, 1);
  sp.setTextDatum(TL_DATUM);

  // --- Siatka radaru ---
  sp.fillCircle(ARP_CX, ARP_CY, ARP_RADIUS, 0x0120);
  sp.drawCircle(ARP_CX, ARP_CY, ARP_RADIUS, ARP_COL_GRID);
  sp.drawCircle(ARP_CX, ARP_CY, (ARP_RADIUS * 2) / 3, ARP_COL_GRID);
  sp.drawCircle(ARP_CX, ARP_CY, ARP_RADIUS / 3, ARP_COL_GRID);
  sp.drawLine(ARP_CX - ARP_RADIUS, ARP_CY, ARP_CX + ARP_RADIUS, ARP_CY, ARP_COL_GRID);
  sp.drawLine(ARP_CX, ARP_CY - ARP_RADIUS, ARP_CX, ARP_CY + ARP_RADIUS, ARP_COL_GRID);

  sp.setTextDatum(TL_DATUM);
  sp.setTextSize(1);
  sp.setTextColor(ARP_COL_DIM, ARP_COL_BG);
  sp.drawString("N", ARP_CX - 3, ARP_CY - ARP_RADIUS - 10, 1);
  sp.drawString("S", ARP_CX - 3, ARP_CY + ARP_RADIUS + 2, 1);
  sp.drawString("E", ARP_CX + ARP_RADIUS + 2, ARP_CY - 4, 1);
  sp.drawString("W", ARP_CX - ARP_RADIUS - 8, ARP_CY - 4, 1);

  // --- Smuga radaru (co 30 stopni linia, na koncu jasna krawedz) ---
  for (int k = 0; k < 6; k++) {
    float a = (angleDeg - k * 3.0f) * (float)M_PI / 180.0f;
    uint16_t col = (k == 0) ? ARP_COL_SWEEP : ARP_COL_GRID;
    int ex = ARP_CX + (int)(cos(a) * ARP_RADIUS);
    int ey = ARP_CY + (int)(sin(a) * ARP_RADIUS);
    sp.drawLine(ARP_CX, ARP_CY, ex, ey, col);
  }

  // --- Stacje APRS ---
  // Srodek radaru = pozycja uzytkownika z tym samym fallbackiem co reszta UI,
  // zeby azymut/odleglosc byly liczone od tego samego punktu co station.distance.
  // (0,0) traktujemy jak brak pozycji - inaczej srodek radaru wypada na oceanie.
  const bool centerOk = userLatLonValid && !(userLat == 0.0 && userLon == 0.0);
  const double centerLat = centerOk ? userLat : 52.40;
  const double centerLon = centerOk ? userLon : 16.92;
  const float filterKm = (aprsFilterRadius > 0) ? (float)aprsFilterRadius : 50.0f;
  const float maxRange = filterKm * APRS_RADAR_RANGE_MULT[aprsRadarRangeIndex];
  int order[MAX_APRS_DISPLAY_LCD];
  int displayCount = 0;
  buildAprsDisplayOrder(order, displayCount);
  int shown = 0;

  for (int i = 0; i < displayCount; i++) {
    int idx = order[i];
    if (idx < 0 || idx >= MAX_APRS_STATIONS) continue;
    const APRSStation &st = aprsStations[idx];
    if (!st.hasLatLon || st.callsign.length() == 0) continue;

    float dist = st.distance;
    if (dist <= 0.0f) {
      dist = calculateDistance(centerLat, centerLon, (double)st.lat, (double)st.lon);
    }
    if (dist > maxRange) continue;

    float bearingDeg = calculateBearing(centerLat, centerLon, (double)st.lat, (double)st.lon);
    float rPix = (dist / maxRange) * ARP_RADIUS;
    float a = bearingDeg * (float)M_PI / 180.0f;
    int px = ARP_CX + (int)(sin(a) * rPix);
    int py = ARP_CY - (int)(cos(a) * rPix);

    // Blip: zapalony gdy smuga minie jego azymut, potem gasnie.
    float sweepDelta = angleDeg - bearingDeg;
    while (sweepDelta < 0) sweepDelta += 360.0f;
    while (sweepDelta >= 360.0f) sweepDelta -= 360.0f;
    if (advanceBlips) {
      if (sweepDelta < 6.0f) {
        aprsRadarBlip[idx] = 6;   // swiezo "oswietlona"
      } else if (aprsRadarBlip[idx] > 0) {
        aprsRadarBlip[idx]--;
      }
    }

    uint16_t base = aprsRadarStationColor(st);
    int dotR = (aprsRadarBlip[idx] > 0) ? 3 : 2;
    sp.fillCircle(px, py, dotR, base);

    String call = st.callsign;
    if (call.length() > 8) call = call.substring(0, 8);
    sp.setTextDatum(TL_DATUM);
    sp.setTextSize(1);
    sp.setTextColor(base, ARP_COL_BG);
    int tx = px + 5;
    int ty = py - 4;
    if (tx + (int)call.length() * 6 > ARP_PANEL_X - 2) tx = px - 5 - (int)call.length() * 6;
    if (ty < 2) ty = py + 5;
    if (ty > 300) ty = 300;
    sp.drawString(call, tx, ty, 1);

    shown++;
    if ((shown & 0x03) == 0) yield();
  }

  // Wlasna stacja (QTH) w centrum: ZNAK wyzej, pod nim LOKATOR + SZER/DLUG.
  sp.fillCircle(ARP_CX, ARP_CY, 3, ARP_COL_TEXT);
  {
    String c = userCallsign.length() > 0 ? userCallsign : String("QTH");
    if (c.length() > 10) c = c.substring(0, 10);
    sp.setTextSize(1);
    sp.setTextDatum(TC_DATUM);
    // Znak wyzej nad znacznikiem (bylo BC_DATUM przy CY-6).
    sp.setTextColor(ARP_COL_TEXT, ARP_COL_BG);
    sp.drawString(c, ARP_CX, ARP_CY - 28, 1);
    // Lokator i wspolrzedne nizej, kazdy z etykieta i danymi.
    int my = ARP_CY + 8;
    char lbl[32];
    sp.setTextColor(ARP_COL_DIM, ARP_COL_BG);
    snprintf(lbl, sizeof(lbl), "LOK: %s", userLocator.length() > 0 ? userLocator.c_str() : "--");
    sp.drawString(lbl, ARP_CX, my, 1);
    my += 10;
    if (userLatLonValid) {
      snprintf(lbl, sizeof(lbl), "SZER: %.4f", userLat);
      sp.drawString(lbl, ARP_CX, my, 1);
      my += 10;
      snprintf(lbl, sizeof(lbl), "DLUG: %.4f", userLon);
      sp.drawString(lbl, ARP_CX, my, 1);
    } else {
      sp.drawString("SZER: --", ARP_CX, my, 1);
      my += 10;
      sp.drawString("DLUG: --", ARP_CX, my, 1);
    }
    sp.setTextDatum(TL_DATUM);
  }

  // --- Panel boczny ---
  sp.fillRect(ARP_PANEL_X - 1, 0, ARP_PANEL_W + 1, 320, ARP_COL_PANEL);
  sp.drawFastVLine(ARP_PANEL_X - 1, 0, 320, ARP_COL_GRID);

  // QTH (Moja Stacja): CALL + LOKATOR + wspolrzedne.
  aprsRadarSection(6, "QTH");
  char buf[40];
  {
    String call = userCallsign.length() > 0 ? userCallsign : String("(brak)");
    if (call.length() > 12) call = call.substring(0, 12);
    aprsRadarRow(16, "CALL:", call.c_str(), ARP_COL_TEXT);
    aprsRadarRow(27, "LOK:", userLocator.length() > 0 ? userLocator.c_str() : "--",
                 ARP_COL_TEXT);
    if (userLatLonValid) {
      snprintf(buf, sizeof(buf), "%.4f", userLat);
      aprsRadarRow(38, "SZER:", buf, ARP_COL_TEXT);
      snprintf(buf, sizeof(buf), "%.4f", userLon);
      aprsRadarRow(49, "DLUG:", buf, ARP_COL_TEXT);
    } else {
      aprsRadarRow(38, "SZER:", "--", ARP_COL_DIM);
      aprsRadarRow(49, "DLUG:", "--", ARP_COL_DIM);
    }
  }

  // STATUS
  aprsRadarSection(74, "STATUS");
  bool wifiOk = (WiFi.status() == WL_CONNECTED);
  aprsRadarRow(92, "WIFI:", wifiOk ? "OK" : "BLAD", wifiOk ? ARP_COL_GREEN : ARP_COL_RED);
  aprsRadarRow(104, "APRS-IS:", aprsConnected ? "OK" : "BLAD", aprsConnected ? ARP_COL_GREEN : ARP_COL_RED);

  // VFO
  aprsRadarSection(122, "VFO");
  aprsRadarRow(140, "CZEST:", "144.800 MHz", ARP_COL_FIXED);

  // CEL / ZAKRES
  aprsRadarSection(158, "ZAKRES RADARU");
  snprintf(buf, sizeof(buf), "%.0f km", maxRange);
  aprsRadarRow(176, "PROMIEN:", buf, ARP_COL_TEXT);
  snprintf(buf, sizeof(buf), "%d", shown);
  aprsRadarRow(188, "STACJE:", buf, ARP_COL_TEXT);

  // STACJE (lista, kolor taktyczny)
  aprsRadarSection(206, "STACJE APRS");
  int row = 0;
  for (int i = 0; i < displayCount && row < 6; i++) {
    int idx = order[i];
    if (idx < 0 || idx >= MAX_APRS_STATIONS) continue;
    const APRSStation &st = aprsStations[idx];
    if (!st.hasLatLon || st.callsign.length() == 0) continue;
    float dist = st.distance;
    if (dist <= 0.0f) {
      dist = calculateDistance(centerLat, centerLon, (double)st.lat, (double)st.lon);
    }
    if (dist > maxRange) continue;

    int y = 224 + row * 14;
    String call = st.callsign;
    if (call.length() > 9) call = call.substring(0, 9);
    sp.setTextDatum(TL_DATUM);
    sp.setTextSize(1);
    sp.setTextColor(aprsRadarStationColor(st), ARP_COL_PANEL);
    sp.drawString(call, ARP_PANEL_X + 6, y, 1);
    snprintf(buf, sizeof(buf), "%4.1f", dist);
    sp.setTextDatum(TR_DATUM);
    sp.setTextColor(ARP_COL_TEXT, ARP_COL_PANEL);
    sp.drawString(buf, ARP_PANEL_X + ARP_PANEL_W - 8, y, 1);
    row++;
  }
  if (row == 0) {
    sp.setTextDatum(TL_DATUM);
    sp.setTextColor(ARP_COL_DIM, ARP_COL_PANEL);
    sp.drawString("brak stacji w zasiegu", ARP_PANEL_X + 6, 226, 1);
  }

  // --- Zegar UTC w naglowku (prawa strona) ---
  struct tm timeinfo;
  if (getTimeWithTimezone(&timeinfo)) {
    char tb[8];
    strftime(tb, sizeof(tb), "%H:%M", &timeinfo);
    sp.setTextDatum(TR_DATUM);
    sp.setTextSize(1);
    sp.setTextColor(UI_COL_TEXT, UI_COL_CARD);
    sp.drawString(tb, ARP_PANEL_X - 6, 9, 1);
    sp.setTextDatum(TL_DATUM);
  }

  // --- Panel zakresow na dole ---
  aprsRadarRangeTiles();

  // Krotka podpowiedz po zmianie zakresu (dotkniecie kafelka na dole).
  if (isScreen6RadarHintVisible()) {
    static const char *tileLabels[] = {"x0.2", "x0.5", "x1", "x2", "x4"};
    char hint[24];
    snprintf(hint, sizeof(hint), "ZAKRES %s", tileLabels[aprsRadarRangeIndex]);
    sp.setTextDatum(TC_DATUM);
    sp.setTextSize(1);
    sp.setTextColor(ARP_COL_TEXT, ARP_COL_BG);
    sp.drawString(hint, ARP_CX, 283, 1);
    sp.setTextDatum(TL_DATUM);
  }

  sp.pushSprite(0, 0);

  if (locked) {
    xSemaphoreGive(aprsRadarSpriteMux);
  }
}

static void drawAprsRadarBody() {
  // Pelne odswiezenie: nie zmieniamy stanu blipow, tylko odtwarzamy klatke.
  aprsRadarRenderFrame(aprsRadarSweepAngle, false);
}

// Animacja smugi - wolane cyklicznie z UI_Task gdy ekran radaru jest aktywny.
static void aprsRadarTick() {
  if (currentScreen != SCREEN_APRS_RADAR || inMenu || !tftInitialized) return;
  if (!aprsRadarSpriteReady && !ensureAprsRadarSprite()) return;
  aprsRadarSweepAngle += 4.0f;
  if (aprsRadarSweepAngle >= 360.0f) aprsRadarSweepAngle -= 360.0f;
  aprsRadarRenderFrame(aprsRadarSweepAngle, true);
}

void drawFilterTile(int x, int y, int w, int h, const char *label, bool active) {
  // Styl nowoczesny: ciemna karta, aktywne = teal (wypelnienie), nieaktywne =
  // ciemna karta z obramowaniem. Klasyczny: wypelnienie kolorem motywu.
  const bool classic = uiStyleClassic();
  uint16_t fill = classic ? (active ? TFT_RADIO_ORANGE : TFT_DARKGREY)
                          : (active ? TFT_RADIO_ORANGE : UI_COL_CARD);
  uint16_t border = classic ? TFT_BLACK : (active ? TFT_RADIO_ORANGE : UI_COL_BORDER);
  uint16_t text = classic ? (active ? TFT_BLACK : TFT_WHITE)
                          : (active ? TFT_BLACK : UI_COL_TEXT);
  tft.fillRect(x, y, w, h, fill);
  tft.drawRect(x, y, w, h, border);
  tft.setTextSize(1);
  tft.setTextColor(text);
  int textW = tft.textWidth(label);
  int textX = x + (w - textW) / 2;
  int textY = y + (h - 8) / 2;
  tft.setCursor(textX, textY);
  tft.print(label);
}

static void drawScreen6MenuCheckButton(int x, int y, const char *label, bool checked) {
  const int boxSize = 18;
  tft.drawRect(x, y, boxSize, boxSize, TFT_WHITE);
  if (checked) {
    tft.fillRect(x + 4, y + 4, boxSize - 8, boxSize - 8, TFT_RADIO_ORANGE);
  } else {
    tft.fillRect(x + 1, y + 1, boxSize - 2, boxSize - 2, TFT_BLACK);
  }

  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(x + boxSize + 10, y + 2);
  tft.print(label);
}

void drawDxClusterFilterMenu() {
  tft.fillScreen(TFT_BLACK);
  uiMenuHeader("FILTERS");

  tft.setTextSize(2);
  tft.setTextColor(TFT_LIGHTGREY);
  tft.setCursor(10, 45);
  tft.print("MODE");

  const int modeTileW = 100;
  const int modeTileH = 40;
  const int modeGap = 10;
  const int modeStartX = (480 - (4 * modeTileW + 3 * modeGap)) / 2;
  const int modeY = 70;
  drawFilterTile(modeStartX + 0 * (modeTileW + modeGap), modeY, modeTileW, modeTileH, "ALL", screen2FilterMode == FILTER_MODE_ALL);
  drawFilterTile(modeStartX + 1 * (modeTileW + modeGap), modeY, modeTileW, modeTileH, "CW", screen2FilterMode == FILTER_MODE_CW);
  drawFilterTile(modeStartX + 2 * (modeTileW + modeGap), modeY, modeTileW, modeTileH, "SSB", screen2FilterMode == FILTER_MODE_SSB);
  drawFilterTile(modeStartX + 3 * (modeTileW + modeGap), modeY, modeTileW, modeTileH, "DIGI", screen2FilterMode == FILTER_MODE_DIGI);

  tft.setTextColor(TFT_LIGHTGREY);
  tft.setCursor(10, 125);
  tft.print("BAND");

  const int bandTileW = 100;
  const int bandTileH = 32;
  const int bandGap = 8;
  const int bandStartX = (480 - (4 * bandTileW + 3 * bandGap)) / 2;
  const int bandStartY = 140;
  int bandIdx = 0;
  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 4; col++) {
      if (bandIdx >= SCREEN8_FILTER_BANDS_COUNT) break;
      int x = bandStartX + col * (bandTileW + bandGap);
      int y = bandStartY + row * (bandTileH + bandGap);
      drawFilterTile(x, y, bandTileW, bandTileH, SCREEN8_FILTER_BANDS[bandIdx], screen2FilterBandIndex == bandIdx);
      bandIdx++;
    }
  }

  const int closeW = 140;
  const int closeH = 40;
  const int closeX = (480 - closeW) / 2;
  const int closeY = 275;
  drawFilterTile(closeX, closeY, closeW, closeH, "CLOSE", false);
}

void handleScreen2MenuTouch(uint16_t x, uint16_t y) {
  const int modeTileW = 100;
  const int modeTileH = 40;
  const int modeGap = 10;
  const int modeStartX = (480 - (4 * modeTileW + 3 * modeGap)) / 2;
  const int modeY = 70;

  for (int i = 0; i < 4; i++) {
    int rx = modeStartX + i * (modeTileW + modeGap);
    if (isPointInRect(x, y, rx, modeY, modeTileW, modeTileH)) {
      screen2FilterMode = (Screen2FilterMode)i;
      drawDxClusterFilterMenu();
      return;
    }
  }

  const int bandTileW = 100;
  const int bandTileH = 32;
  const int bandGap = 8;
  const int bandStartX = (480 - (4 * bandTileW + 3 * bandGap)) / 2;
  const int bandStartY = 140;
  int bandIdx = 0;
  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 4; col++) {
      if (bandIdx >= SCREEN8_FILTER_BANDS_COUNT) break;
      int rx = bandStartX + col * (bandTileW + bandGap);
      int ry = bandStartY + row * (bandTileH + bandGap);
      if (isPointInRect(x, y, rx, ry, bandTileW, bandTileH)) {
        screen2FilterBandIndex = bandIdx;
        drawDxClusterFilterMenu();
        return;
      }
      bandIdx++;
    }
  }

  const int closeW = 140;
  const int closeH = 40;
  const int closeX = (480 - closeW) / 2;
  const int closeY = 275;
  if (isPointInRect(x, y, closeX, closeY, closeW, closeH)) {
    inMenu = false;
    drawScreen(SCREEN_DX_CLUSTER);
    return;
  }
}

void drawAprsSortMenu() {
  tft.fillScreen(TFT_BLACK);
  uiMenuHeader("APRS");

  const int tileW = 90;
  const int tileH = 26;
  const int gap = 6;
  const int startX = (480 - (3 * tileW + 2 * gap)) / 2;
  const int tileY = 32;
  drawFilterTile(startX + 0 * (tileW + gap), tileY, tileW, tileH, "TIME", screen6SortMode == APRS_SORT_TIME);
  drawFilterTile(startX + 1 * (tileW + gap), tileY, tileW, tileH, "CALL", screen6SortMode == APRS_SORT_CALLSIGN);
  drawFilterTile(startX + 2 * (tileW + gap), tileY, tileW, tileH, "DIST", screen6SortMode == APRS_SORT_DISTANCE);

  tft.setTextColor(TFT_LIGHTGREY);
  tft.setTextSize(2);
  tft.setCursor(10, 74);
  tft.print("APRS Setting:");

  const int checkX = 34;
  const int checkRowY0 = 94;
  const int checkRowGap = 28;
  drawScreen6MenuCheckButton(checkX, checkRowY0 + 0 * checkRowGap, "Beaconing", screen6MenuBeaconingTemp);
  drawScreen6MenuCheckButton(checkX, checkRowY0 + 1 * checkRowGap, "APRS Alert", screen6MenuAprsAlertTemp);
  drawScreen6MenuCheckButton(checkX, checkRowY0 + 2 * checkRowGap, "Range Alert", screen6MenuRangeAlertTemp);
  drawScreen6MenuCheckButton(checkX, checkRowY0 + 3 * checkRowGap, "LED / Buzzer", screen6MenuLedAlertTemp);

  const int closeW = 100;
  const int saveW = 100;
  const int closeH = 26;
  const int closeX = 54;
  const int saveX = 166;
  const int closeY = 210;
  drawFilterTile(closeX, closeY, closeW, closeH, "CLOSE", false);
  drawFilterTile(saveX, closeY, saveW, closeH, "SAVE", false);
}

void handleScreen6MenuTouch(uint16_t x, uint16_t y) {
  const int tileW = 90;
  const int tileH = 26;
  const int gap = 6;
  const int startX = (480 - (3 * tileW + 2 * gap)) / 2;
  const int tileY = 32;

  if (isPointInRect(x, y, startX + 0 * (tileW + gap), tileY, tileW, tileH)) {
    screen6SortMode = APRS_SORT_TIME;
    drawAprsSortMenu();
    return;
  }
  if (isPointInRect(x, y, startX + 1 * (tileW + gap), tileY, tileW, tileH)) {
    screen6SortMode = APRS_SORT_CALLSIGN;
    drawAprsSortMenu();
    return;
  }
  if (isPointInRect(x, y, startX + 2 * (tileW + gap), tileY, tileW, tileH)) {
    screen6SortMode = APRS_SORT_DISTANCE;
    drawAprsSortMenu();
    return;
  }

  const int checkX = 34;
  const int checkRowY0 = 94;
  const int checkRowGap = 28;
  const int checkHitW = 250;
  const int checkHitH = 22;

  if (isPointInRect(x, y, checkX, checkRowY0 + 0 * checkRowGap, checkHitW, checkHitH)) {
    screen6MenuBeaconingTemp = !screen6MenuBeaconingTemp;
    drawAprsSortMenu();
    return;
  }
  if (isPointInRect(x, y, checkX, checkRowY0 + 1 * checkRowGap, checkHitW, checkHitH)) {
    screen6MenuAprsAlertTemp = !screen6MenuAprsAlertTemp;
    drawAprsSortMenu();
    return;
  }
  if (isPointInRect(x, y, checkX, checkRowY0 + 2 * checkRowGap, checkHitW, checkHitH)) {
    screen6MenuRangeAlertTemp = !screen6MenuRangeAlertTemp;
    drawAprsSortMenu();
    return;
  }
  if (isPointInRect(x, y, checkX, checkRowY0 + 3 * checkRowGap, checkHitW, checkHitH)) {
    screen6MenuLedAlertTemp = !screen6MenuLedAlertTemp;
    drawAprsSortMenu();
    return;
  }

  const int closeW = 100;
  const int saveW = 100;
  const int closeH = 26;
  const int closeX = 54;
  const int saveX = 166;
  const int closeY = 210;
  if (isPointInRect(x, y, closeX, closeY, closeW, closeH)) {
    inMenu = false;
    drawScreen(currentScreen);
    return;
  }

  if (isPointInRect(x, y, saveX, closeY, saveW, closeH)) {
    aprsBeaconEnabled = screen6MenuBeaconingTemp;
    aprsAlertEnabled = screen6MenuAprsAlertTemp;
    aprsAlertNearbyEnabled = screen6MenuRangeAlertTemp;
    enableLedAlert = screen6MenuLedAlertTemp;
    savePreferences();
    inMenu = false;
    drawScreen(currentScreen);
    return;
  }
}

void drawPotaFilterMenu() {
  tft.fillScreen(TFT_BLACK);
  uiMenuHeader("POTA FILTERS");

  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY);
  tft.setCursor(10, 28);
  tft.print("MODE");

  const int modeTileW = 70;
  const int modeTileH = 26;
  const int modeGap = 6;
  const int modeStartX = (480 - (4 * modeTileW + 3 * modeGap)) / 2;
  const int modeY = 40;
  drawFilterTile(modeStartX + 0 * (modeTileW + modeGap), modeY, modeTileW, modeTileH, "ALL", screen7FilterMode == FILTER_MODE_ALL);
  drawFilterTile(modeStartX + 1 * (modeTileW + modeGap), modeY, modeTileW, modeTileH, "CW", screen7FilterMode == FILTER_MODE_CW);
  drawFilterTile(modeStartX + 2 * (modeTileW + modeGap), modeY, modeTileW, modeTileH, "SSB", screen7FilterMode == FILTER_MODE_SSB);
  drawFilterTile(modeStartX + 3 * (modeTileW + modeGap), modeY, modeTileW, modeTileH, "DIGI", screen7FilterMode == FILTER_MODE_DIGI);

  tft.setTextColor(TFT_LIGHTGREY);
  tft.setCursor(10, 82);
  tft.print("BAND");

  const int bandTileW = 90;
  const int bandTileH = 24;
  const int bandGap = 6;
  const int bandStartX = (480 - (3 * bandTileW + 2 * bandGap)) / 2;
  const int bandStartY = 96;
  int bandIdx = 0;
  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 3; col++) {
      if (bandIdx >= SCREEN2_FILTER_BANDS_COUNT) break;
      int x = bandStartX + col * (bandTileW + bandGap);
      int y = bandStartY + row * (bandTileH + bandGap);
      drawFilterTile(x, y, bandTileW, bandTileH, SCREEN2_FILTER_BANDS[bandIdx], screen7FilterBandIndex == bandIdx);
      bandIdx++;
    }
  }

  const int closeW = 100;
  const int closeH = 26;
  const int closeX = (480 - closeW) / 2;
  const int closeY = 210;
  drawFilterTile(closeX, closeY, closeW, closeH, "CLOSE", false);
}

void handleScreen7MenuTouch(uint16_t x, uint16_t y) {
  const int modeTileW = 70;
  const int modeTileH = 26;
  const int modeGap = 6;
  const int modeStartX = (480 - (4 * modeTileW + 3 * modeGap)) / 2;
  const int modeY = 40;

  for (int i = 0; i < 4; i++) {
    int rx = modeStartX + i * (modeTileW + modeGap);
    if (isPointInRect(x, y, rx, modeY, modeTileW, modeTileH)) {
      screen7FilterMode = (Screen2FilterMode)i;
      drawPotaFilterMenu();
      return;
    }
  }

  const int bandTileW = 90;
  const int bandTileH = 24;
  const int bandGap = 6;
  const int bandStartX = (480 - (3 * bandTileW + 2 * bandGap)) / 2;
  const int bandStartY = 96;
  int bandIdx = 0;
  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 3; col++) {
      if (bandIdx >= SCREEN2_FILTER_BANDS_COUNT) break;
      int rx = bandStartX + col * (bandTileW + bandGap);
      int ry = bandStartY + row * (bandTileH + bandGap);
      if (isPointInRect(x, y, rx, ry, bandTileW, bandTileH)) {
        screen7FilterBandIndex = bandIdx;
        drawPotaFilterMenu();
        return;
      }
      bandIdx++;
    }
  }

  const int closeW = 100;
  const int closeH = 26;
  const int closeX = (480 - closeW) / 2;
  const int closeY = 210;
  if (isPointInRect(x, y, closeX, closeY, closeW, closeH)) {
    inMenu = false;
    drawScreen(SCREEN_POTA_CLUSTER);
    return;
  }
}

void drawHamalertFilterMenu() {
  tft.fillScreen(TFT_BLACK);
  uiMenuHeader("HAMALERT FILTR");

  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY);
  tft.setCursor(10, 28);
  tft.print("MODE");

  const int modeTileW = 70;
  const int modeTileH = 26;
  const int modeGap = 6;
  const int modeStartX = (480 - (4 * modeTileW + 3 * modeGap)) / 2;
  const int modeY = 40;
  drawFilterTile(modeStartX + 0 * (modeTileW + modeGap), modeY, modeTileW, modeTileH, "ALL", screen8FilterMode == FILTER_MODE_ALL);
  drawFilterTile(modeStartX + 1 * (modeTileW + modeGap), modeY, modeTileW, modeTileH, "CW", screen8FilterMode == FILTER_MODE_CW);
  drawFilterTile(modeStartX + 2 * (modeTileW + modeGap), modeY, modeTileW, modeTileH, "SSB", screen8FilterMode == FILTER_MODE_SSB);
  drawFilterTile(modeStartX + 3 * (modeTileW + modeGap), modeY, modeTileW, modeTileH, "DIGI", screen8FilterMode == FILTER_MODE_DIGI);

  tft.setTextColor(TFT_LIGHTGREY);
  tft.setCursor(10, 82);
  tft.print("BAND");

  const int bandTileW = 72;
  const int bandTileH = 24;
  const int bandGap = 6;
  const int bandStartX = (480 - (4 * bandTileW + 3 * bandGap)) / 2;
  const int bandStartY = 96;
  int bandIdx = 0;
  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 4; col++) {
      if (bandIdx >= SCREEN8_FILTER_BANDS_COUNT) break;
      int x = bandStartX + col * (bandTileW + bandGap);
      int y = bandStartY + row * (bandTileH + bandGap);
      drawFilterTile(x, y, bandTileW, bandTileH, SCREEN8_FILTER_BANDS[bandIdx], screen8FilterBandIndex == bandIdx);
      bandIdx++;
    }
  }

  const int closeW = 100;
  const int closeH = 26;
  const int closeX = (480 - closeW) / 2;
  const int closeY = 210;
  drawFilterTile(closeX, closeY, closeW, closeH, "CLOSE", false);
}

void handleScreen8MenuTouch(uint16_t x, uint16_t y) {
  const int modeTileW = 70;
  const int modeTileH = 26;
  const int modeGap = 6;
  const int modeStartX = (480 - (4 * modeTileW + 3 * modeGap)) / 2;
  const int modeY = 40;

  for (int i = 0; i < 4; i++) {
    int rx = modeStartX + i * (modeTileW + modeGap);
    if (isPointInRect(x, y, rx, modeY, modeTileW, modeTileH)) {
      screen8FilterMode = (Screen2FilterMode)i;
      drawHamalertFilterMenu();
      return;
    }
  }

  const int bandTileW = 72;
  const int bandTileH = 24;
  const int bandGap = 6;
  const int bandStartX = (480 - (4 * bandTileW + 3 * bandGap)) / 2;
  const int bandStartY = 96;
  int bandIdx = 0;
  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 4; col++) {
      if (bandIdx >= SCREEN8_FILTER_BANDS_COUNT) break;
      int rx = bandStartX + col * (bandTileW + bandGap);
      int ry = bandStartY + row * (bandTileH + bandGap);
      if (isPointInRect(x, y, rx, ry, bandTileW, bandTileH)) {
        screen8FilterBandIndex = bandIdx;
        drawHamalertFilterMenu();
        return;
      }
      bandIdx++;
    }
  }

  const int closeW = 100;
  const int closeH = 26;
  const int closeX = (480 - closeW) / 2;
  const int closeY = 210;
  if (isPointInRect(x, y, closeX, closeY, closeW, closeH)) {
    inMenu = false;
    drawScreen(SCREEN_HAMALERT_CLUSTER);
    return;
  }
}

void drawHamClockTimeMenu() {
  tft.fillScreen(TFT_BLACK);
  uiMenuHeader("TIME MODE");

  const int tileW = 110;
  const int tileH = 28;
  const int gap = 10;
  const int startX = (480 - (2 * tileW + gap)) / 2;
  const int tileY = 60;
  drawFilterTile(startX, tileY, tileW, tileH, "UTC", screen1TimeMode == SCREEN1_TIME_UTC);
  drawFilterTile(startX + tileW + gap, tileY, tileW, tileH, "LOCAL", screen1TimeMode == SCREEN1_TIME_LOCAL);

  // Wybor wygladu WSZYSTKICH ekranow: nowoczesny / klasyczny.
  tft.setTextColor(TFT_LIGHTGREY);
  tft.setTextSize(1);
  tft.setCursor(startX, 100);
  tft.print((tftLanguage == TFT_LANG_EN) ? "SCREEN STYLE (ALL)" : "WYGLAD EKRANOW (WSZYSTKIE)");
  const int styleY = 114;
  drawFilterTile(startX, styleY, tileW, tileH, (tftLanguage == TFT_LANG_EN) ? "MODERN" : "NOWOCZESNY", screen1ClockStyle == 0);
  drawFilterTile(startX + tileW + gap, styleY, tileW, tileH, (tftLanguage == TFT_LANG_EN) ? "CLASSIC" : "KLASYCZNY", screen1ClockStyle == 1);

  const int closeW = 100;
  const int closeH = 26;
  const int closeX = (480 - closeW) / 2;
  const int closeY = 210;
  drawFilterTile(closeX, closeY, closeW, closeH, "CLOSE", false);
}

void drawWeatherMenu() {
  tft.fillScreen(TFT_BLACK);
  uiMenuHeader("WEATHER MENU");

  const int tileW = 140;
  const int tileH = 36;
  const int gap = 12;
  const int startX = (480 - (2 * tileW + gap)) / 2;
  const int tileY = 80;
  
  drawFilterTile(startX, tileY, tileW, tileH, "REFRESH", false);
  drawFilterTile(startX + tileW + gap, tileY, tileW, tileH, "FORECAST", currentScreen == SCREEN_WEATHER_FORECAST);

  const int closeW = 140;
  const int closeH = 36;
  const int closeX = (480 - closeW) / 2;
  const int closeY = 210;
  drawFilterTile(closeX, closeY, closeW, closeH, "CLOSE", false);
}

void handleScreen5MenuTouch(uint16_t x, uint16_t y) {
  const int tileW = 140;
  const int tileH = 36;
  const int gap = 12;
  const int startX = (480 - (2 * tileW + gap)) / 2;
  const int tileY = 80;

  if (isPointInRect(x, y, startX, tileY, tileW, tileH)) {
    // Refresh - force weather data update
    if (weatherApiKey.length() > 0) {
      lastWeatherFetchMs = 0; // Reset to force immediate fetch
    }
    inMenu = false;
    drawScreen(currentScreen);
    return;
  }
  if (isPointInRect(x, y, startX + tileW + gap, tileY, tileW, tileH)) {
    // Toggle between weather and forecast screen
    if (currentScreen == SCREEN_WEATHER_DSP) {
      currentScreen = SCREEN_WEATHER_FORECAST;
    } else {
      currentScreen = SCREEN_WEATHER_DSP;
    }
    inMenu = false;
    drawScreen(currentScreen);
    return;
  }

  const int closeW = 140;
  const int closeH = 36;
  const int closeX = (480 - closeW) / 2;
  const int closeY = 210;
  if (isPointInRect(x, y, closeX, closeY, closeW, closeH)) {
    inMenu = false;
    drawScreen(currentScreen);
    return;
  }
}

void handleScreen1MenuTouch(uint16_t x, uint16_t y) {
  const int tileW = 110;
  const int tileH = 28;
  const int gap = 10;
  const int startX = (480 - (2 * tileW + gap)) / 2;
  const int tileY = 60;

  if (isPointInRect(x, y, startX, tileY, tileW, tileH)) {
    screen1TimeMode = SCREEN1_TIME_UTC;
    savePreferences();
    drawHamClockTimeMenu();
    return;
  }
  if (isPointInRect(x, y, startX + tileW + gap, tileY, tileW, tileH)) {
    screen1TimeMode = SCREEN1_TIME_LOCAL;
    savePreferences();
    drawHamClockTimeMenu();
    return;
  }

  // Wybor wygladu WSZYSTKICH ekranow (nowoczesny / klasyczny). Klikniecie
  // zapisuje od razu - wszystkie ekrany i menu przelaczaja sie natychmiast.
  const int styleY = 114;
  if (isPointInRect(x, y, startX, styleY, tileW, tileH)) {
    if (screen1ClockStyle != 0) {
      screen1ClockStyle = 0;
      applyMenuThemeFromHue();
      savePreferences();
    }
    drawHamClockTimeMenu();
    return;
  }
  if (isPointInRect(x, y, startX + tileW + gap, styleY, tileW, tileH)) {
    if (screen1ClockStyle != 1) {
      screen1ClockStyle = 1;
      applyMenuThemeFromHue();
      savePreferences();
    }
    drawHamClockTimeMenu();
    return;
  }

  const int closeW = 100;
  const int closeH = 26;
  const int closeX = (480 - closeW) / 2;
  const int closeY = 210;
  if (isPointInRect(x, y, closeX, closeY, closeW, closeH)) {
    inMenu = false;
    drawScreen(SCREEN_HAM_CLOCK);
    return;
  }
}

void redrawCurrentMenu() {
  if (currentScreen == SCREEN_HAM_CLOCK) {
    drawHamClockTimeMenu();
    return;
  }
  if (currentScreen == SCREEN_DX_CLUSTER) {
    drawDxClusterFilterMenu();
    return;
  }
  if (currentScreen == SCREEN_APRS_IS || currentScreen == SCREEN_APRS_RADAR) {
    drawAprsSortMenu();
    return;
  }
  if (currentScreen == SCREEN_WEATHER_DSP || currentScreen == SCREEN_WEATHER_FORECAST) {
    drawWeatherMenu();
    return;
  }
}

void restoreAfterBrightnessMenu() {
  inMenu = brightnessMenuPrevInMenu;
  if (brightnessMenuPrevInMenu) {
    redrawCurrentMenu();
    return;
  }
  currentScreen = brightnessMenuPrevScreen;
  drawScreen(currentScreen);
}

void drawBrightnessSlider() {
  const int sliderX = 40;
  const int sliderY = 90;
  const int sliderW = 400;
  const int sliderH = 12;
  const int knobW = 6;

  // WyczyĹ›Ä‡ obszar suwaka (z zapasem na pokrÄ™tĹ‚o), ĹĽeby nie zostawaĹ‚ Ĺ›lad
  tft.fillRect(sliderX - knobW, sliderY - 6, sliderW + knobW * 2, sliderH + 12, TFT_BLACK);

  tft.drawRect(sliderX, sliderY, sliderW, sliderH, TFT_WHITE);
  int fillW = (brightnessMenuValue * (sliderW - 2)) / 100;
  tft.fillRect(sliderX + 1, sliderY + 1, sliderW - 2, sliderH - 2, TFT_BLACK);
  tft.fillRect(sliderX + 1, sliderY + 1, fillW, sliderH - 2, TFT_RADIO_ORANGE);

  int knobX = sliderX + (brightnessMenuValue * (sliderW - knobW)) / 100;
  tft.fillRect(knobX, sliderY - 4, knobW, sliderH + 8, TFT_WHITE);

  tft.setTextColor(TFT_LIGHTGREY);
  tft.setTextSize(1);
  tft.setCursor(10, 70);
  tft.print(sanitizePolishToAscii(String(tr(TR_BRIGHTNESS))));
  tft.fillRect(120, 70, 40, 8, TFT_BLACK);
  tft.setCursor(120, 70);
  tft.print(brightnessMenuValue);
  tft.print("%");
}

void drawThemeSlider() {
  const int sliderX = 40;
  const int sliderY = 130;
  const int sliderW = 400;
  const int sliderH = 12;
  const int knobW = 6;

  // WyczyĹ›Ä‡ obszar suwaka (z zapasem na pokrÄ™tĹ‚o)
  tft.fillRect(sliderX - knobW, sliderY - 6, sliderW + knobW * 2, sliderH + 12, TFT_BLACK);

  tft.setTextColor(TFT_LIGHTGREY);
  tft.setTextSize(1);
  tft.setCursor(10, 110);
  // W stylu nowoczesnym menu ma staly kolor (teal), wiec ten suwak ustawia
  // KOLOR ZEGARA (cyfry duzego zegara). W klasycznym - KOLOR MOTYWU.
  tft.print(sanitizePolishToAscii(String(tr(uiStyleClassic() ? TR_THEME_COLOR : TR_CLOCK_COLOR))));

  if (!uiStyleClassic()) {
    // Kolor zegara: suwak pokazuje teczowy wybor, a kropka-podglad pokazuje
    // aktualny kolor cyfr zegara.
    for (int i = 0; i < sliderW; i++) {
      uint8_t hue = (uint8_t)((i * 255) / (sliderW - 1));
      tft.drawFastVLine(sliderX + i, sliderY + 1, sliderH - 2, colorWheel(hue));
    }
    tft.drawRect(sliderX, sliderY, sliderW, sliderH, TFT_WHITE);
    int clockHue, clockSat, clockVal;
    rgb565ToHsv(clockDigitsColor, clockHue, clockSat, clockVal);
    int knobX = sliderX + (clockHue * (sliderW - knobW)) / 255;
    tft.fillRect(knobX, sliderY - 4, knobW, sliderH + 8, TFT_WHITE);
    tft.fillRect(sliderX + sliderW + 8, sliderY - 6, 26, sliderH + 12, clockDigitsColor);
    tft.drawRect(sliderX + sliderW + 8, sliderY - 6, 26, sliderH + 12, TFT_WHITE);
    return;
  }

  for (int i = 0; i < sliderW; i++) {
    uint8_t hue = (uint8_t)((i * 255) / (sliderW - 1));
    tft.drawFastVLine(sliderX + i, sliderY + 1, sliderH - 2, colorWheel(hue));
  }
  tft.drawRect(sliderX, sliderY, sliderW, sliderH, TFT_WHITE);

  int knobX = sliderX + (menuThemeHue * (sliderW - knobW)) / 255;
  tft.fillRect(knobX, sliderY - 4, knobW, sliderH + 8, TFT_WHITE);
}

void drawBrightnessMenuHeader() {
  uiMenuHeader(sanitizePolishToAscii(String(tr(TR_DISPLAY_SETTINGS))).c_str());
}

void drawBrightnessMenu() {
  tft.fillScreen(TFT_BLACK);
  drawBrightnessMenuHeader();

  // PodpowiedĹş: dĹ‚ugi tap 3s w dowolnym miejscu uruchamia kalibracjÄ™
  tft.setTextColor(TFT_LIGHTGREY);
  tft.setTextSize(1);
  tft.setCursor(10, 40);
  tft.print(sanitizePolishToAscii(String(tr(TR_TFT_CALIBRATION_HINT))));

  drawBrightnessSlider();
  drawThemeSlider();

  const int langLabelY = 150;
  const int langTileW = 70;
  const int langTileH = 22;
  const int langGap = 10;
  const int langStartX = (480 - (2 * langTileW + langGap)) / 2;
  const int langY = 240;

  tft.setTextColor(TFT_LIGHTGREY);
  tft.setTextSize(1);
  tft.setCursor(10, langLabelY);
  tft.print(sanitizePolishToAscii(String(tr(TR_LANGUAGE))));
  drawFilterTile(langStartX, langY, langTileW, langTileH, "PL", tftLanguage == TFT_LANG_PL);
  drawFilterTile(langStartX + langTileW + langGap, langY, langTileW, langTileH, "EN", tftLanguage == TFT_LANG_EN);

  const int btnW = 90;
  const int btnH = 26;
  const int btnY = 190;
  const int btnGap = 10;
  const int saveX = 10;
  const int defaultX = saveX + btnW + btnGap;
  const int saverX = defaultX + btnW + btnGap;
  const int closeX = saverX + btnW + btnGap;
  drawFilterTile(saveX, btnY, btnW, btnH, tr(TR_SAVE), false);
  drawFilterTile(defaultX, btnY, btnW, btnH, tr(TR_DEFAULT), false);
  drawFilterTile(saverX, btnY, btnW, btnH, "USPIENIE", false);
  drawFilterTile(closeX, btnY, btnW, btnH, tr(TR_CLOSE), false);
}

void drawTouchCalTarget(int cx, int cy, uint16_t color) {
  tft.drawLine(cx - 10, cy, cx + 10, cy, color);
  tft.drawLine(cx, cy - 10, cx, cy + 10, color);
  tft.drawRect(cx - 12, cy - 12, 24, 24, color);
}

void drawTouchCalibrationScreen() {
  tft.fillScreen(TFT_BLACK);

  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  int centerY = 120;
  
  if (touchCalStep == 0) {
    tft.setCursor(40, centerY);
    tft.print("Touch target 1/4 (top-left)");
    drawTouchCalTarget(20, 20, TFT_RADIO_ORANGE);
  } else if (touchCalStep == 1) {
    tft.setCursor(35, centerY);
    tft.print("Touch target 2/4 (top-right)");
    drawTouchCalTarget(460, 20, TFT_RADIO_ORANGE);
  } else if (touchCalStep == 2) {
    tft.setCursor(30, centerY);
    tft.print("Touch target 3/4 (bottom-right)");
    drawTouchCalTarget(460, 300, TFT_RADIO_ORANGE);
  } else if (touchCalStep == 3) {
    tft.setCursor(35, centerY);
    tft.print("Touch target 4/4 (bottom-left)");
    drawTouchCalTarget(20, 300, TFT_RADIO_ORANGE);
  } else {
    // touchCalStep >= 4 - Review screen
    tft.setCursor(40, centerY);
    tft.print("Calibration complete - Touch to close");
    
    // Narysuj wszystkie 4 targets jako potwierdzenie
    drawTouchCalTarget(20, 20, TFT_DARKGREY);
    drawTouchCalTarget(460, 20, TFT_DARKGREY);
    drawTouchCalTarget(460, 300, TFT_DARKGREY);
    drawTouchCalTarget(20, 300, TFT_DARKGREY);
    
    tft.setTextColor(TFT_LIGHTGREY);
    tft.setCursor(10, 30);
    tft.print("Raw X: ");
    tft.print(touchCalNewXMin);
    tft.print(" - ");
    tft.print(touchCalNewXMax);
    tft.setCursor(10, 45);
    tft.print("Raw Y: ");
    tft.print(touchCalNewYMin);
    tft.print(" - ");
    tft.print(touchCalNewYMax);
    
    tft.setCursor(10, 65);
    tft.print("Detected: Swap:");
    tft.print(touchSwapXY ? "YES" : "NO");
    tft.print(" InvX:");
    tft.print(touchInvertX ? "YES" : "NO");
    tft.setCursor(10, 80);
    tft.print("          InvY:");
    tft.print(touchInvertY ? "YES" : "NO");
    
    // PodpowiedĹş do ustawienia w WWW
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(10, 145);
    tft.print("Use WWW panel >");
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, 240);
    tft.print(sanitizePolishToAscii(String(tr(TR_TFT_CALIBRATION_HINT))));
    tft.setTextColor(TFT_GREENYELLOW);
    tft.setCursor(10, 180);
    
    // Ustal jaki tryb sugerowaÄ‡
    if (touchSwapXY && touchInvertX && touchInvertY) {
      tft.print("-> SWAP XY + INVERT BOTH");
    } else if (touchSwapXY && touchInvertX) {
      tft.print("-> ");
      tft.print(tr(TR_ROT_90_RIGHT));
    } else if (touchSwapXY && touchInvertY) {
      tft.print("-> ");
      tft.print(tr(TR_ROT_90_LEFT));
    } else if (touchSwapXY) {
      tft.print("-> SWAP XY");
    } else if (touchInvertX && touchInvertY) {
      tft.print("-> INVERT BOTH (180deg)");
    } else if (touchInvertX) {
      tft.print("-> INVERT X");
    } else if (touchInvertY) {
      tft.print("-> INVERT Y");
    } else {
      tft.print("-> NONE (default - OK)");
    }
    
    tft.setTextColor(TFT_LIGHTGREY);
    tft.setCursor(10, 210);
    tft.print("(Touch will NOT save changes)");
  }
}

void startTouchCalibration() {
  touchCalActive = true;
  touchCalStep = 0;
  touchCalRawX1 = 0;
  touchCalRawY1 = 0;
  touchCalRawX2 = 0;
  touchCalRawY2 = 0;
  touchCalRawX3 = 0;
  touchCalRawY3 = 0;
  touchCalRawX4 = 0;
  touchCalRawY4 = 0;
  touchCalNewXMin = touchXMin;
  touchCalNewXMax = touchXMax;
  touchCalNewYMin = touchYMin;
  touchCalNewYMax = touchYMax;
  drawTouchCalibrationScreen();
}

void setBrightnessFromTouch(uint16_t x) {
  const int sliderX = 40;
  const int sliderW = 400;
  int val = (int)((long)(x - sliderX) * 100 / (sliderW - 1));
  if (val < MIN_BACKLIGHT_PERCENT) val = MIN_BACKLIGHT_PERCENT;
  if (val > 100) val = 100;
  if (val != brightnessMenuValue) {
    brightnessMenuValue = val;
    setBacklightPercent(brightnessMenuValue);
    drawBrightnessSlider();
  }
}

void setThemeFromTouch(uint16_t x) {
  const int sliderX = 40;
  const int sliderW = 400;
  int val = (int)((long)(x - sliderX) * 255 / (sliderW - 1));
  if (val < 0) val = 0;
  if (val > 255) val = 255;
  uint8_t hue = (uint8_t)val;
  // W stylu nowoczesnym suwak ustawia KOLOR ZEGARA (cyfry zegara); w klasycznym
  // - KOLOR MOTYWU (kolor kolowy menu).
  if (!uiStyleClassic()) {
    if (hue != clockColorMenuHue || clockDigitsColor != hsv565(hue, 100, 100)) {
      clockColorMenuHue = hue;
      clockDigitsColor = hsv565(hue, 100, 100);
      drawThemeSlider();
    }
    return;
  }
  if (hue != menuThemeHue) {
    menuThemeHue = hue;
    applyMenuThemeFromHue();
    drawBrightnessMenuHeader();
    drawThemeSlider();
    drawBrightnessSlider();
  }
}

void handleTouchCalibrationTouch(int16_t rawX, int16_t rawY, uint16_t x, uint16_t y, bool isNewTap) {
  if (!touchCalActive || !isNewTap) {
    return;
  }

  if (touchCalStep == 0) {
    // Target 1: lewy gĂłrny (20, 20)
    touchCalRawX1 = rawX;
    touchCalRawY1 = rawY;
    touchCalStep = 1;
    drawTouchCalibrationScreen();
    return;
  }

  if (touchCalStep == 1) {
    // Target 2: prawy gĂłrny (300, 20)
    touchCalRawX2 = rawX;
    touchCalRawY2 = rawY;
    touchCalStep = 2;
    drawTouchCalibrationScreen();
    return;
  }

  if (touchCalStep == 2) {
    // Target 3: prawy dolny (300, 220)
    touchCalRawX3 = rawX;
    touchCalRawY3 = rawY;
    touchCalStep = 3;
    drawTouchCalibrationScreen();
    return;
  }

  if (touchCalStep == 3) {
    // Target 4: lewy dolny (20, 220)
    touchCalRawX4 = rawX;
    touchCalRawY4 = rawY;
    
    // Wszystkie 4 punkty zebrane - oblicz parametry
    // Min/Max ze wszystkich 4 punktĂłw surowych
    touchCalNewXMin = min(min(touchCalRawX1, touchCalRawX2), min(touchCalRawX3, touchCalRawX4));
    touchCalNewXMax = max(max(touchCalRawX1, touchCalRawX2), max(touchCalRawX3, touchCalRawX4));
    touchCalNewYMin = min(min(touchCalRawY1, touchCalRawY2), min(touchCalRawY3, touchCalRawY4));
    touchCalNewYMax = max(max(touchCalRawY1, touchCalRawY2), max(touchCalRawY3, touchCalRawY4));
    
    // Wykryj swap: porĂłwnaj rozpiÄ™toĹ›Ä‡ surowych wartoĹ›ci
    // Targets rozciÄ…gajÄ… siÄ™ w X: 20-300 (280px) i Y: 20-220 (200px)
    // JeĹ›li deltaRawY > deltaRawX, osie sÄ… zamienione
    int deltaRawX = touchCalNewXMax - touchCalNewXMin;
    int deltaRawY = touchCalNewYMax - touchCalNewYMin;
    touchSwapXY = (deltaRawY > deltaRawX);
    
    // Wykryj odwrĂłcenie osi
    // PorĂłwnaj punkt 1 (gĂłra-lewo) z punktem 3 (dĂłĹ‚-prawo)
    int16_t x1 = touchCalRawX1;
    int16_t y1 = touchCalRawY1;
    int16_t x3 = touchCalRawX3;
    int16_t y3 = touchCalRawY3;
    
    // JeĹ›li osie sÄ… zamienione, swap wspĂłĹ‚rzÄ™dne przed sprawdzeniem kierunku
    if (touchSwapXY) {
      int16_t tmp = x1; x1 = y1; y1 = tmp;
      tmp = x3; x3 = y3; y3 = tmp;
    }
    
    // Target 1 (20,20) -> Target 3 (300,220): oczekujemy x3 > x1 i y3 > y1
    touchInvertX = (x3 < x1);
    touchInvertY = (y3 < y1);
    
    touchCalStep = 4;
    drawTouchCalibrationScreen();
    return;
  }

  if (touchCalStep >= 4) {
    // Dowolne dotkniÄ™cie zamyka ekran kalibracji bez zapisywania
    // PrzywrĂłÄ‡ poprzednie wartoĹ›ci flag
    touchSwapXY = TOUCH_SWAP_XY;
    touchInvertX = TOUCH_INVERT_X;
    touchInvertY = TOUCH_INVERT_Y;
    touchCalActive = false;
    drawBrightnessMenu();
    return;
  }
}

void handleBrightnessMenuTouch(uint16_t x, uint16_t y, bool isNewTap) {
  unsigned long now = millis();
  
  if (now - brightnessMenuOpenedMs < 1500) {
    return;
  }

  // Zawsze aktualizuj znacznik startu dla bieĹĽÄ…cego dotkniÄ™cia, zanim obsĹ‚uĹĽymy kafelki
  if (isNewTap) {
    brightnessMenuTouchStartMs = now;
    brightnessMenuLongPressHandled = false;
  }

  // DĹ‚ugi tap (>=5s) w dowolnym miejscu ekranu jasnoĹ›ci uruchamia kalibracjÄ™ dotyku
  if (!brightnessMenuLongPressHandled && brightnessMenuTouchStartMs > 0 && (now - brightnessMenuTouchStartMs) >= 5000) {
    brightnessMenuLongPressHandled = true;
    startTouchCalibration();
    return;
  }

  const int sliderX = 40;
  const int sliderY = 90;
  const int sliderW = 400;
  const int sliderH = 12;
  const int themeSliderY = 130;
  const int themeSliderH = 12;
  const int langTileW = 70;
  const int langTileH = 22;
  const int langGap = 10;
  const int langStartX = (480 - (2 * langTileW + langGap)) / 2;
  const int langY = 240;

  // ObsĹ‚uga sliderĂłw
  if (y >= sliderY - 6 && y <= sliderY + sliderH + 6) {
    setBrightnessFromTouch(x);
  }
  if (y >= themeSliderY - 6 && y <= themeSliderY + themeSliderH + 6) {
    setThemeFromTouch(x);
  }

  if (isNewTap) {
    if (isPointInRect(x, y, langStartX, langY, langTileW, langTileH)) {
      tftLanguage = TFT_LANG_PL;
      // WymuĹ› odĹ›wieĹĽenie opisĂłw pogody po zmianie jÄ™zyka
      lastWeatherFetchMs = 0;
      weatherData.valid = false;
      weatherData.forecast3hValid = false;
      weatherData.forecastNextDayValid = false;
      for (uint8_t i = 0; i < WeatherData::DETAIL_COLS; i++) {
        weatherData.detailValid[i] = false;
      }
      savePreferences();
      drawBrightnessMenu();
      return;
    }
    if (isPointInRect(x, y, langStartX + langTileW + langGap, langY, langTileW, langTileH)) {
      tftLanguage = TFT_LANG_EN;
      // WymuĹ› odĹ›wieĹĽenie opisĂłw pogody po zmianie jÄ™zyka
      lastWeatherFetchMs = 0;
      weatherData.valid = false;
      weatherData.forecast3hValid = false;
      weatherData.forecastNextDayValid = false;
      for (uint8_t i = 0; i < WeatherData::DETAIL_COLS; i++) {
        weatherData.detailValid[i] = false;
      }
      savePreferences();
      drawBrightnessMenu();
      return;
    }
  }

  if (!isNewTap) {
    return;
  }

  const int btnW = 90;
  const int btnH = 26;
  const int btnY = 190;
  const int btnGap = 10;
  const int saveX = 10;
  const int defaultX = saveX + btnW + btnGap;
  const int saverX = defaultX + btnW + btnGap;
  const int closeX = saverX + btnW + btnGap;

  if (isPointInRect(x, y, saveX, btnY, btnW, btnH)) {
    backlightPercent = brightnessMenuValue;
    savePreferences();
    applyEffectiveBacklight();
    brightnessMenuActive = false;
    restoreAfterBrightnessMenu();
    return;
  }
  if (isPointInRect(x, y, defaultX, btnY, btnW, btnH)) {
    if (!uiStyleClassic()) {
      // Styl nowoczesny: suwak to kolor zegara - przywroc domyslny zolty.
      clockDigitsColor = 0xFFE0;
      clockColorMenuHue = 42;
      drawThemeSlider();
      return;
    }
    menuThemeHue = DEFAULT_MENU_THEME_HUE;
    applyMenuThemeFromHue();
    drawBrightnessMenuHeader();
    drawThemeSlider();
    drawBrightnessSlider();
    return;
  }
  if (isPointInRect(x, y, closeX, btnY, btnW, btnH)) {
    backlightPercent = brightnessMenuPrevBacklight;
    applyEffectiveBacklight();
    menuThemeHue = brightnessMenuPrevThemeHue;
    applyMenuThemeFromHue();
    clockDigitsColor = brightnessMenuPrevClockColor;
    brightnessMenuActive = false;
    restoreAfterBrightnessMenu();
    return;
  }
  
  // Przycisk UĹšPIENIE - otwĂłrz menu uĹ›pienia
  if (isPointInRect(x, y, saverX, btnY, btnW, btnH)) {
    brightnessMenuActive = false;  // Zamknij menu jasnoĹ›ci
    screenSleepMenuActive = true;
    drawScreenSleepMenu();
    return;
  }
}

// ===== Menu koloru znaku (przytrzymanie palca na znaku, ekran HAM CLOCK) =====

// HSV (hue 0..255, sat/val 0..100) -> RGB565.
uint16_t hsv565(int hue, int sat, int val) {
  hue = ((hue % 256) + 256) % 256;
  sat = constrain(sat, 0, 100);
  val = constrain(val, 0, 100);
  uint8_t region = hue / 43;                 // 6 obszarow koloru
  uint8_t rem = (uint8_t)((hue - region * 43) * 6);  // 0..255
  // Kanaly w skali 0..255 (rgb565() oczekuje 8 bitow, nie procentow).
  uint8_t v = (uint8_t)((val * 255) / 100);
  uint8_t p = (uint8_t)((v * (100 - sat)) / 100);
  uint8_t q = (uint8_t)((v * (100 - (sat * rem) / 255)) / 100);
  uint8_t t = (uint8_t)((v * (100 - (sat * (255 - rem)) / 255)) / 100);
  uint8_t r, g, b;
  switch (region) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
  return rgb565(r, g, b);
}

// RGB565 -> HSV, zeby suwaki startowaly od aktualnego koloru znaku.
static void rgb565ToHsv(uint16_t c, int &h, int &sat, int &val) {
  float r = ((c >> 11) & 0x1F) / 31.0f;
  float g = ((c >> 5) & 0x3F) / 63.0f;
  float b = (c & 0x1F) / 31.0f;
  float mx = max(r, max(g, b));
  float mn = min(r, min(g, b));
  float d = mx - mn;
  val = (int)(mx * 100.0f + 0.5f);
  sat = (mx <= 0.0f) ? 0 : (int)(d / mx * 100.0f + 0.5f);
  float hd = 0.0f;
  if (d > 0.0f) {
    if (mx == r) { hd = (g - b) / d; if (hd < 0.0f) hd += 6.0f; }
    else if (mx == g) hd = (b - r) / d + 2.0f;
    else hd = (r - g) / d + 4.0f;
  }
  h = (int)(hd * 255.0f / 6.0f + 0.5f);
  if (h > 255) h = 255;
}

// Wspolny suwak HSV (0 = hue 0..255, 1 = nasycenie 0..100, 2 = jasnosc 0..100).
static void drawHsvSlider(int y, int mode, int hue, int sat, int val) {
  const int sx = 40, sw = 400, sh = 14, knobW = 6;
  tft.fillRect(sx - knobW, y - 6, sw + knobW * 2, sh + 12, TFT_BLACK);
  for (int i = 0; i < sw; i++) {
    uint16_t c;
    if (mode == 0) {
      c = hsv565((i * 255) / (sw - 1), sat, val);
    } else if (mode == 1) {
      c = hsv565(hue, (i * 100) / (sw - 1), val);
    } else {
      c = hsv565(hue, sat, (i * 100) / (sw - 1));
    }
    tft.drawFastVLine(sx + i, y + 1, sh - 2, c);
  }
  tft.drawRect(sx, y, sw, sh, TFT_WHITE);
  int value = (mode == 0) ? hue : (mode == 1 ? sat : val);
  int maxv = (mode == 0) ? 255 : 100;
  int knobX = sx + (value * (sw - knobW)) / maxv;
  tft.fillRect(knobX, y - 4, knobW, sh + 8, TFT_WHITE);
}

static void drawCallsignColorSlider(int y, int mode) {
  drawHsvSlider(y, mode, callsignColorMenuHue, callsignColorMenuSat, callsignColorMenuVal);
}

static void drawCallsignColorPreview() {
  tft.fillRect(0, 36, 480, 40, TFT_BLACK);
  uint16_t preview = hsv565(callsignColorMenuHue, callsignColorMenuSat, callsignColorMenuVal);
  String cs = (userCallsign.length() > 0) ? userCallsign : DEFAULT_CALLSIGN;
  cs.toUpperCase();
  tft.setTextSize(3);
  tft.setTextColor(preview, TFT_BLACK);
  int w = tft.textWidth(cs);
  tft.setCursor((480 - w) / 2, 44);
  tft.print(cs);
}

// Odswiezenie bez fillScreen - uzywane podczas przeciagania suwakow (bez migotania).
static void redrawCallsignColorControls() {
  drawCallsignColorPreview();
  drawCallsignColorSlider(120, 0);
  drawCallsignColorSlider(160, 1);
  drawCallsignColorSlider(200, 2);
}

void drawCallsignColorMenu() {
  tft.fillScreen(TFT_BLACK);
  uiMenuHeader(sanitizePolishToAscii(String(tr(TR_CALLSIGN_COLOR))).c_str());

  drawCallsignColorPreview();

  const int hueY = 120, satY = 160, valY = 200;
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY);
  tft.setCursor(10, hueY - 14);
  tft.print(sanitizePolishToAscii(String(tr(TR_THEME_COLOR))));
  tft.setCursor(10, satY - 14);
  tft.print(sanitizePolishToAscii(String(tr(TR_SATURATION))));
  tft.setCursor(10, valY - 14);
  tft.print(sanitizePolishToAscii(String(tr(TR_VALUE))));
  drawCallsignColorSlider(hueY, 0);
  drawCallsignColorSlider(satY, 1);
  drawCallsignColorSlider(valY, 2);

  const int btnW = 110, btnH = 28, btnY = 250, gap = 15;
  const int saveX = 15, defX = saveX + btnW + gap, closeX = defX + btnW + gap;
  drawFilterTile(saveX, btnY, btnW, btnH, tr(TR_SAVE), false);
  drawFilterTile(defX, btnY, btnW, btnH, tr(TR_DEFAULT), false);
  drawFilterTile(closeX, btnY, btnW, btnH, tr(TR_CLOSE), false);
}

void openCallsignColorMenu() {
  rgb565ToHsv(callsignColor, callsignColorMenuHue, callsignColorMenuSat, callsignColorMenuVal);
  if (callsignColorMenuSat == 0) {
    // Domyslny bialy znak nie ma nasycenia - suwaki startuja na 100%.
    callsignColorMenuSat = 100;
    callsignColorMenuVal = 100;
  }
  callsignColorMenuPrev = callsignColor;
  callsignColorMenuPrevScreen = currentScreen;
  callsignColorMenuPrevInMenu = inMenu;
  callsignColorMenuOpenedMs = millis();
  // Blokuj petle glowna (m.in. odswiezanie zegara ekranu 1 co sekunde), inaczej
  // zegar rysuje sie na wierzchu menu koloru znaku i przeszkadza w ustawieniach.
  inMenu = true;
  callsignColorMenuActive = true;
  drawCallsignColorMenu();
}

void handleCallsignColorMenuTouch(uint16_t x, uint16_t y, bool isNewTap) {
  unsigned long now = millis();
  if (now - callsignColorMenuOpenedMs < 400) {
    return;
  }

  const int sx = 40, sw = 400, sh = 14;
  const int hueY = 120, satY = 160, valY = 200;

  if (y >= hueY - 6 && y <= hueY + sh + 6) {
    callsignColorMenuHue = constrain((int)((long)(x - sx) * 255 / (sw - 1)), 0, 255);
    redrawCallsignColorControls();
  }
  if (y >= satY - 6 && y <= satY + sh + 6) {
    callsignColorMenuSat = constrain((int)((long)(x - sx) * 100 / (sw - 1)), 0, 100);
    redrawCallsignColorControls();
  }
  if (y >= valY - 6 && y <= valY + sh + 6) {
    callsignColorMenuVal = constrain((int)((long)(x - sx) * 100 / (sw - 1)), 0, 100);
    redrawCallsignColorControls();
  }

  if (!isNewTap) {
    return;
  }

  const int btnW = 110, btnH = 28, btnY = 250, gap = 15;
  const int saveX = 15, defX = saveX + btnW + gap, closeX = defX + btnW + gap;

  if (isPointInRect(x, y, saveX, btnY, btnW, btnH)) {
    callsignColor = hsv565(callsignColorMenuHue, callsignColorMenuSat, callsignColorMenuVal);
    savePreferences();
    callsignColorMenuActive = false;
    currentScreen = callsignColorMenuPrevScreen;
    inMenu = callsignColorMenuPrevInMenu;
    drawScreen(currentScreen);
    return;
  }
  if (isPointInRect(x, y, defX, btnY, btnW, btnH)) {
    callsignColor = 65535;
    callsignColorMenuHue = 0;
    callsignColorMenuSat = 0;
    callsignColorMenuVal = 100;
    redrawCallsignColorControls();
    return;
  }
  if (isPointInRect(x, y, closeX, btnY, btnW, btnH)) {
    callsignColor = callsignColorMenuPrev;
    callsignColorMenuActive = false;
    currentScreen = callsignColorMenuPrevScreen;
    inMenu = callsignColorMenuPrevInMenu;
    drawScreen(currentScreen);
    return;
  }
}

// ===== Menu koloru cyfr zegara (dlugie przytrzymanie zegara, ekran HAM CLOCK) =====

static void drawClockColorPreview() {
  tft.fillRect(0, 36, 480, 50, TFT_BLACK);
  uint16_t preview = hsv565(clockColorMenuHue, clockColorMenuSat, clockColorMenuVal);
  String timeStr = (screen1TimeMode == SCREEN1_TIME_LOCAL)
                     ? getTimezoneTimeString("%H:%M:%S", 9)
                     : getUtcTimeString();
  tft.setTextSize(4);
  tft.setTextColor(preview, TFT_BLACK);
  int w = tft.textWidth(timeStr);
  tft.setCursor((480 - w) / 2, 40);
  tft.print(timeStr);
}

// Odswiezenie bez fillScreen - uzywane podczas przeciagania suwakow (bez migotania).
static void redrawClockColorControls() {
  drawClockColorPreview();
  drawHsvSlider(120, 0, clockColorMenuHue, clockColorMenuSat, clockColorMenuVal);
  drawHsvSlider(160, 1, clockColorMenuHue, clockColorMenuSat, clockColorMenuVal);
  drawHsvSlider(200, 2, clockColorMenuHue, clockColorMenuSat, clockColorMenuVal);
}

void drawClockColorMenu() {
  tft.fillScreen(TFT_BLACK);
  uiMenuHeader(sanitizePolishToAscii(String(tr(TR_CLOCK_COLOR))).c_str());

  drawClockColorPreview();

  const int hueY = 120, satY = 160, valY = 200;
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY);
  tft.setCursor(10, hueY - 14);
  tft.print(sanitizePolishToAscii(String(tr(TR_HUE))));
  tft.setCursor(10, satY - 14);
  tft.print(sanitizePolishToAscii(String(tr(TR_SATURATION))));
  tft.setCursor(10, valY - 14);
  tft.print(sanitizePolishToAscii(String(tr(TR_VALUE))));
  drawHsvSlider(hueY, 0, clockColorMenuHue, clockColorMenuSat, clockColorMenuVal);
  drawHsvSlider(satY, 1, clockColorMenuHue, clockColorMenuSat, clockColorMenuVal);
  drawHsvSlider(valY, 2, clockColorMenuHue, clockColorMenuSat, clockColorMenuVal);

  const int btnW = 110, btnH = 28, btnY = 250, gap = 15;
  const int saveX = 15, defX = saveX + btnW + gap, closeX = defX + btnW + gap;
  drawFilterTile(saveX, btnY, btnW, btnH, tr(TR_SAVE), false);
  drawFilterTile(defX, btnY, btnW, btnH, tr(TR_DEFAULT), false);
  drawFilterTile(closeX, btnY, btnW, btnH, tr(TR_CLOSE), false);
}

void openClockColorMenu() {
  rgb565ToHsv(clockDigitsColor, clockColorMenuHue, clockColorMenuSat, clockColorMenuVal);
  if (clockColorMenuSat == 0) {
    // Domyslny zolty ma nasycenie, ale szary/czarny kolor nie - suwaki na 100%.
    clockColorMenuSat = 100;
    clockColorMenuVal = 100;
  }
  clockColorMenuPrev = clockDigitsColor;
  clockColorMenuPrevScreen = currentScreen;
  clockColorMenuPrevInMenu = inMenu;
  clockColorMenuOpenedMs = millis();
  // Blokuj petle glowna (odswiezanie zegara co sekunde), inaczej zegar rysuje
  // sie na wierzchu menu koloru i przeszkadza w ustawieniach.
  inMenu = true;
  clockColorMenuActive = true;
  drawClockColorMenu();
}

void handleClockColorMenuTouch(uint16_t x, uint16_t y, bool isNewTap) {
  unsigned long now = millis();
  if (now - clockColorMenuOpenedMs < 400) {
    return;
  }

  const int sx = 40, sw = 400, sh = 14;
  const int hueY = 120, satY = 160, valY = 200;

  if (y >= hueY - 6 && y <= hueY + sh + 6) {
    clockColorMenuHue = constrain((int)((long)(x - sx) * 255 / (sw - 1)), 0, 255);
    redrawClockColorControls();
  }
  if (y >= satY - 6 && y <= satY + sh + 6) {
    clockColorMenuSat = constrain((int)((long)(x - sx) * 100 / (sw - 1)), 0, 100);
    redrawClockColorControls();
  }
  if (y >= valY - 6 && y <= valY + sh + 6) {
    clockColorMenuVal = constrain((int)((long)(x - sx) * 100 / (sw - 1)), 0, 100);
    redrawClockColorControls();
  }

  if (!isNewTap) {
    return;
  }

  const int btnW = 110, btnH = 28, btnY = 250, gap = 15;
  const int saveX = 15, defX = saveX + btnW + gap, closeX = defX + btnW + gap;

  if (isPointInRect(x, y, saveX, btnY, btnW, btnH)) {
    clockDigitsColor = hsv565(clockColorMenuHue, clockColorMenuSat, clockColorMenuVal);
    savePreferences();
    clockColorMenuActive = false;
    currentScreen = clockColorMenuPrevScreen;
    inMenu = clockColorMenuPrevInMenu;
    drawScreen(currentScreen);
    return;
  }
  if (isPointInRect(x, y, defX, btnY, btnW, btnH)) {
    clockDigitsColor = 0xFFE0;  // domyslny zolty
    clockColorMenuHue = 42;
    clockColorMenuSat = 100;
    clockColorMenuVal = 100;
    redrawClockColorControls();
    return;
  }
  if (isPointInRect(x, y, closeX, btnY, btnW, btnH)) {
    clockDigitsColor = clockColorMenuPrev;
    clockColorMenuActive = false;
    currentScreen = clockColorMenuPrevScreen;
    inMenu = clockColorMenuPrevInMenu;
    drawScreen(currentScreen);
    return;
  }
}

// Ekran 6: APRS-IS - zegar tylko dla RADAR, nie dla listy
void updateScreen6Clock() {
  if (!tftInitialized || inMenu) {
    return;
  }
  // Zegar tylko dla APRS_RADAR, nie dla APRS_IS (lista)
  if (currentScreen != SCREEN_APRS_RADAR) {
    return;
  }

  static char lastDrawnTime[6] = "";
  static bool lastRadarMode = false;

  struct tm timeinfo;
  if (!getTimeWithTimezone(&timeinfo)) {
    return;
  }

  char timeBuffer[6];
  strftime(timeBuffer, 6, "%H:%M", &timeinfo);
  int timeWidth = strlen(timeBuffer) * 12;
  bool radarMode = true; // Zawsze radar mode tutaj
  uint16_t topClearColor = TFT_BLACK;
  uint16_t timeColor = TFT_WHITE;
  int timeY = 5;

  if (strcmp(lastDrawnTime, timeBuffer) == 0 && lastRadarMode == radarMode) {
    return;
  }

  int timeX = 480 - timeWidth - 4;

  tft.fillRect(timeX - 2, 4, timeWidth + 6, 24, topClearColor);
  tft.setTextColor(timeColor);
  tft.setTextSize(2);
  tft.setCursor(timeX, timeY);
  tft.print(timeBuffer);

  strncpy(lastDrawnTime, timeBuffer, sizeof(lastDrawnTime));
  lastDrawnTime[sizeof(lastDrawnTime) - 1] = '\0';
  lastRadarMode = radarMode;
}

uint32_t computeScreen6Signature() {
  // Prosty hash treĂ„Ä…Ă˘â‚¬Ĺźci tabeli (10 lub 11 stacji APRS zaleĹĽnie od paska nawigacji)
  const uint32_t fnvPrime = 16777619u;
  uint32_t hash = 2166136261u;

  hash ^= (uint32_t)aprsStationCount;
  hash *= fnvPrime;
  hash ^= (uint32_t)screen6SortMode;
  hash *= fnvPrime;
  hash ^= (uint32_t)aprsRadarRangeIndex;
  hash *= fnvPrime;
  hash ^= isScreen6RadarHintVisible() ? 1u : 0u;
  hash *= fnvPrime;
  hash ^= (uint32_t)dxTableSizeMode;
  hash *= fnvPrime;
  hash ^= isTableNavFooterVisible(SCREEN_APRS_IS) ? 1u : 0u;
  hash *= fnvPrime;

  int order[MAX_APRS_DISPLAY_LCD];
  int displayCount = 0;
  buildAprsDisplayOrder(order, displayCount);
  int maxRows = getAprsTableMaxRows();
  int visibleCount = min(displayCount, maxRows);
  for (int i = 0; i < visibleCount; i++) {
    const APRSStation &s = aprsStations[order[i]];
    for (size_t j = 0; j < s.time.length(); j++) {
      hash ^= (uint8_t)s.time[j];
      hash *= fnvPrime;
    }
    for (size_t j = 0; j < s.callsign.length(); j++) {
      hash ^= (uint8_t)s.callsign[j];
      hash *= fnvPrime;
    }
    hash ^= (uint32_t)(s.lat * 1000);
    hash *= fnvPrime;
    hash ^= (uint32_t)(s.lon * 1000);
    hash *= fnvPrime;
    hash ^= (uint32_t)s.distance;
    hash *= fnvPrime;
    hash ^= (uint32_t)(s.freqMHz * 1000);
    hash *= fnvPrime;
  }

  return hash;
}

void updateScreen6Data() {
  if (!tftInitialized || inMenu) {
    return;
  }
  if (currentScreen != SCREEN_APRS_IS && currentScreen != SCREEN_APRS_RADAR) {
    return;
  }

  // 1) Zaktualizuj czas w nagĹ‚Ăłwku (bez peĹ‚nego odĹ›wieĹĽania)
  updateScreen6Clock();

  // 2) OdĹ›wieĹĽ tabelÄ™ tylko gdy dane siÄ™ zmieniĹ‚y
  static uint32_t lastSig = 0;
  uint32_t currentSig = computeScreen6Signature();
  if (currentSig == lastSig) {
    return;
  }
  lastSig = currentSig;

  if (currentScreen == SCREEN_APRS_RADAR) {
    drawAprsRadar();
    return;
  }

  // 3) Renderuj tabelÄ™ do bufora i wypchnij jednym ruchem (bez migotania)
  const bool navVisible = isTableNavFooterVisible(SCREEN_APRS_IS);
  const bool enlarged = isDxTableEnlarged();
  const int maxRows = getAprsTableMaxRows();
  const int tableTop = TFT_TABLE_TOP;
  const int tableBottom = getTableBottomForScreen(SCREEN_APRS_IS);
  const int tableHeight = tableBottom - tableTop;
  TFT_eSprite *tableSprite = (navVisible && ensureSharedTableSprite()) ? &sharedTableSprite : nullptr;

  if (tableSprite != nullptr) {
    tableSprite->fillSprite(TFT_BLACK);

    int yPos = 8;
    tableSprite->setTextColor(TFT_DARKGREY);
    tableSprite->setTextSize(1);
    if (enlarged) {
      tableSprite->setCursor(5, yPos);   tableSprite->print("UTC");
      tableSprite->setCursor(74, yPos);  tableSprite->print("CALL");
      tableSprite->setCursor(198, yPos); tableSprite->print("KM");
      tableSprite->setCursor(224, yPos); tableSprite->print("FREQ");
    } else {
      tableSprite->setCursor(5, yPos);   tableSprite->print("UTC");
      tableSprite->setCursor(50, yPos);  tableSprite->print("CALLSIGN");
      tableSprite->setCursor(125, yPos); tableSprite->print("SYMBOL");
      tableSprite->setCursor(200, yPos); tableSprite->print("KM");
      tableSprite->setCursor(245, yPos); tableSprite->print("FREQ");
    }
    tableSprite->drawFastHLine(0, yPos + 10, 480, TFT_DARKGREY);
    yPos += enlarged ? 20 : 18;

    int order[MAX_APRS_DISPLAY_LCD];
    int displayCount = 0;
    buildAprsDisplayOrder(order, displayCount);
    int visibleCount = min(displayCount, maxRows);
    for (int i = 0; i < visibleCount; i++) {
      if (yPos >= (tableHeight - 2)) {
        break;
      }
      const APRSStation &station = aprsStations[order[i]];
      if (enlarged) {
        if (i % 2 == 0) {
          tableSprite->fillRect(0, yPos - 5, 480, 24, TFT_TABLE_ALT_ROW_COLOR);
        }

        tableSprite->setTextSize(2);

        tableSprite->setTextColor(TFT_LIGHTGREY);
        tableSprite->setCursor(5, yPos);
        String timeStr = formatAprsTimeWithTimezone(station.time);
        tableSprite->print(timeStr);

        tableSprite->setTextColor(getAprsCallsignColorForEnlarged(station));
        tableSprite->setCursor(74, yPos);
        String callText = station.callsign;
        if (callText.length() > 8) callText = callText.substring(0, 8);
        tableSprite->print(callText);

        tableSprite->setTextColor(TFT_RADIO_ORANGE);
        tableSprite->setCursor(198, yPos);
        if (station.hasLatLon && station.distance > 0) {
          tableSprite->print((int)station.distance);
        } else {
          tableSprite->print("-");
        }

        tableSprite->setTextColor(TFT_CYAN);
        tableSprite->setCursor(224, yPos);
        if (station.freqMHz > 0.0f) {
          tableSprite->print(String(station.freqMHz, 3));
        } else {
          tableSprite->print("-");
        }

        yPos += 27;
      } else {
        if (i % 2 == 0) {
          tableSprite->fillRect(0, yPos - 2, 480, 16, TFT_TABLE_ALT_ROW_COLOR);
        }

        tableSprite->setTextSize(1);

        tableSprite->setTextColor(TFT_LIGHTGREY);
        tableSprite->setCursor(5, yPos);
        String timeStr = formatAprsTimeWithTimezone(station.time);
        tableSprite->print(timeStr);

        tableSprite->setTextColor(TFT_WHITE);
        tableSprite->setCursor(50, yPos);
        tableSprite->print(station.callsign);

        String symbolShort = getAPRSSymbolShort(station);
        uint16_t symbolColor = TFT_GREEN;
        if (symbolShort == "HUMAN") {
          symbolColor = TFT_BLUE;
        } else if (symbolShort == "HOUSE") {
          symbolColor = TFT_YELLOW;
        } else if (symbolShort == "CAR") {
          symbolColor = TFT_RED;
        }
        tableSprite->setTextColor(symbolColor);
        tableSprite->setCursor(125, yPos);
        tableSprite->print(symbolShort);

        tableSprite->setTextColor(TFT_RADIO_ORANGE);
        tableSprite->setCursor(200, yPos);
        if (station.hasLatLon && station.distance > 0) {
          tableSprite->print((int)station.distance);
          tableSprite->print(" km");
        } else {
          tableSprite->print("-");
        }

        tableSprite->setTextColor(TFT_CYAN);
        tableSprite->setCursor(245, yPos);
        if (station.freqMHz > 0.0f) {
          tableSprite->print(String(station.freqMHz, 3));
        } else {
          tableSprite->print("-");
        }

        yPos += 17;
      }
    }

    if (visibleCount == 0) {
      tableSprite->setTextColor(TFT_RED);
      tableSprite->setTextSize(2);
      tableSprite->setCursor(40, 120 - tableTop);
      tableSprite->print("WAITING FOR APRS...");
    }

    tableSprite->pushSprite(0, tableTop);
    return;
  }

  // Fallback bez sprite (np. gdy zabraknie RAM)
  tft.fillRect(0, tableTop, 480, tableHeight, TFT_BLACK);

  int yPos = 40;
  tft.setTextColor(TFT_DARKGREY);
  tft.setTextSize(1);
  if (enlarged) {
    tft.setCursor(5, yPos);   tft.print("UTC");
    tft.setCursor(74, yPos);  tft.print("CALL");
    tft.setCursor(198, yPos); tft.print("KM");
    tft.setCursor(224, yPos); tft.print("FREQ");
  } else {
    tft.setCursor(5, yPos);   tft.print("UTC");
    tft.setCursor(50, yPos);  tft.print("CALLSIGN");
    tft.setCursor(125, yPos); tft.print("SYMBOL");
    tft.setCursor(200, yPos); tft.print("KM");
    tft.setCursor(245, yPos); tft.print("FREQ");
  }
  tft.drawFastHLine(0, yPos + 10, 480, TFT_DARKGREY);
  yPos += enlarged ? 20 : 18;

  int order[MAX_APRS_DISPLAY_LCD];
  int displayCount = 0;
  buildAprsDisplayOrder(order, displayCount);
  int visibleCount = min(displayCount, maxRows);
  for (int i = 0; i < visibleCount; i++) {
    if (yPos >= (tableBottom - 2)) {
      break;
    }
    const APRSStation &station = aprsStations[order[i]];
    if (enlarged) {
      if (i % 2 == 0) {
        tft.fillRect(0, yPos - 5, 480, 24, TFT_TABLE_ALT_ROW_COLOR);
      }

      tft.setTextSize(2);

      tft.setTextColor(TFT_LIGHTGREY);
      tft.setCursor(5, yPos);
      String timeStr = formatAprsTimeWithTimezone(station.time);
      tft.print(timeStr);

      tft.setTextColor(getAprsCallsignColorForEnlarged(station));
      tft.setCursor(74, yPos);
      String callText = station.callsign;
      if (callText.length() > 8) callText = callText.substring(0, 8);
      tft.print(callText);

      tft.setTextColor(TFT_RADIO_ORANGE);
      tft.setCursor(198, yPos);
      if (station.hasLatLon && station.distance > 0) {
        tft.print((int)station.distance);
      } else {
        tft.print("-");
      }

      tft.setTextColor(TFT_CYAN);
      tft.setCursor(224, yPos);
      if (station.freqMHz > 0.0f) {
        tft.print(String(station.freqMHz, 3));
      } else {
        tft.print("-");
      }

      yPos += 27;
    } else {
      if (i % 2 == 0) {
        tft.fillRect(0, yPos - 2, 480, 16, TFT_TABLE_ALT_ROW_COLOR);
      }

      tft.setTextSize(1);

      tft.setTextColor(TFT_LIGHTGREY);
      tft.setCursor(5, yPos);
      String timeStr = formatAprsTimeWithTimezone(station.time);
      tft.print(timeStr);

      tft.setTextColor(TFT_WHITE);
      tft.setCursor(50, yPos);
      tft.print(station.callsign);

      String symbolShort = getAPRSSymbolShort(station);
      uint16_t symbolColor = TFT_GREEN;
      if (symbolShort == "HUMAN") {
        symbolColor = TFT_BLUE;
      } else if (symbolShort == "HOUSE") {
        symbolColor = TFT_YELLOW;
      } else if (symbolShort == "CAR") {
        symbolColor = TFT_RED;
      }
      tft.setTextColor(symbolColor);
      tft.setCursor(125, yPos);
      tft.print(symbolShort);

      tft.setTextColor(TFT_RADIO_ORANGE);
      tft.setCursor(200, yPos);
      if (station.hasLatLon && station.distance > 0) {
        tft.print((int)station.distance);
        tft.print(" km");
      } else {
        tft.print("-");
      }

      tft.setTextColor(TFT_CYAN);
      tft.setCursor(245, yPos);
      if (station.freqMHz > 0.0f) {
        tft.print(String(station.freqMHz, 3));
      } else {
        tft.print("-");
      }

      yPos += 17;
    }
  }

  if (visibleCount == 0) {
    tft.setTextColor(UI_COL_WARN);
    tft.setTextSize(2);
    tft.setCursor(40, 120);
    tft.print("WAITING FOR APRS...");
  }
  
  // StrzaĹ‚ki nawigacyjne - ZAWSZE rysuj po odĹ›wieĹĽeniu tabeli
  drawSwitchScreenFooter();
}

void drawAprsIs() {
  screen6ViewMode = APRS_VIEW_LIST;
  // ekran czysci drawScreen (wspolny styl ISS)

  // 1. NAGĂ„Ä…Ă‚ÂĂ„â€šĂ˘â‚¬Ĺ›WEK: Belka z menu, nazwÄ‚â€žĂ˘â‚¬Â¦ serwera i czasem UTC

  // IKONA MENU (3D)

  // Nazwa serwera APRS-IS - przesuniĂ„ta w prawo (x=35 zamiast 5), by zrobiĂ„â€ˇ miejsce na menu

  // Ikona WiFi w gĂÂłrnym prawym rogu
  
  // Ikona baterii obok ikony WiFi

  // ZEGAR USUNIĂ„TY z nagĂ…â€šĂÂłwka APRS-IS

  // 2. NAGĂ…ÂĂâ€śWKI TABELI
  int yPos = 40;
  const bool enlarged = isDxTableEnlarged();
  tft.setTextColor(UI_COL_FAINT);
  tft.setTextSize(1);
  if (enlarged) {
    tft.setCursor(5, yPos);   tft.print("UTC");
    tft.setCursor(74, yPos);  tft.print("CALL");
    tft.setCursor(198, yPos); tft.print("KM");
    tft.setCursor(224, yPos); tft.print("FREQ");
  } else {
    tft.setCursor(5, yPos);   tft.print("UTC");
    tft.setCursor(50, yPos);  tft.print("CALLSIGN");
    tft.setCursor(125, yPos); tft.print("SYMBOL");
    tft.setCursor(200, yPos); tft.print("KM");
  }
  tft.drawFastHLine(0, yPos + 10, 480, TFT_DARKGREY);
  yPos += enlarged ? 20 : 18;

  // 3. LISTA STACJI APRS (10 z paskiem nawigacji, 11 bez)
  int order[MAX_APRS_DISPLAY_LCD];
  int maxRows = getAprsTableMaxRows();
  int displayCount = 0;
  buildAprsDisplayOrder(order, displayCount);
  int visibleCount = min(displayCount, maxRows);
  for (int i = 0; i < visibleCount; i++) {
    const APRSStation &station = aprsStations[order[i]];
    if (enlarged) {
      if (i % 2 == 0) tft.fillRect(0, yPos - 5, 480, 24, TFT_TABLE_ALT_ROW_COLOR);

      tft.setTextSize(2);
      tft.setTextColor(TFT_LIGHTGREY);
      tft.setCursor(5, yPos);
      String timeStr = formatAprsTimeWithTimezone(station.time);
      tft.print(timeStr);

      tft.setTextColor(getAprsCallsignColorForEnlarged(station));
      tft.setCursor(74, yPos);
      String callText = station.callsign;
      if (callText.length() > 8) callText = callText.substring(0, 8);
      tft.print(callText);

      tft.setTextColor(TFT_RADIO_ORANGE);
      tft.setCursor(198, yPos);
      if (station.hasLatLon && station.distance > 0) {
        tft.print((int)station.distance);
      } else {
        tft.print("-");
      }

      tft.setTextColor(TFT_CYAN);
      tft.setCursor(224, yPos);
      if (station.freqMHz > 0.0f) {
        tft.print(String(station.freqMHz, 3));
      } else {
        tft.print("-");
      }

      yPos += 27;
    } else {
      if (i % 2 == 0) tft.fillRect(0, yPos - 2, 480, 16, TFT_TABLE_ALT_ROW_COLOR);

      tft.setTextSize(1);
      tft.setTextColor(TFT_LIGHTGREY);
      tft.setCursor(5, yPos);
      String timeStr = formatAprsTimeWithTimezone(station.time);
      tft.print(timeStr);

      tft.setTextColor(TFT_WHITE);
      tft.setCursor(50, yPos);
      tft.print(station.callsign);

      String symbolShort = getAPRSSymbolShort(station);
      uint16_t symbolColor = TFT_GREEN;
      if (symbolShort == "HUMAN") {
        symbolColor = TFT_BLUE;
      } else if (symbolShort == "HOUSE") {
        symbolColor = TFT_YELLOW;
      } else if (symbolShort == "CAR") {
        symbolColor = TFT_RED;
      }
      tft.setTextColor(symbolColor);
      tft.setCursor(125, yPos);
      tft.print(symbolShort);

      tft.setTextColor(TFT_RADIO_ORANGE);
      tft.setCursor(200, yPos);
      if (station.hasLatLon && station.distance > 0) {
        tft.print((int)station.distance);
        tft.print(" km");
      } else {
        tft.print("-");
      }

      yPos += 17;
    }
  }

  // 4. Pasek nawigacji - ZAWSZE widoczny

  if (visibleCount == 0) {
    tft.setTextColor(UI_COL_WARN);
    tft.setTextSize(2);
    tft.setCursor(40, 120);
    tft.print("WAITING FOR APRS...");
  }
}

void drawAprsRadar() {
  screen6ViewMode = APRS_VIEW_RADAR;
  // Diagnostyka: ile stacji i jaki promien filtra (pomaga gdy radar pusty).
  Serial.printf("[APRS RADAR] count=%d filter=%dkm userValid=%d lat=%.4f lon=%.4f\n",
                aprsStationCount, aprsFilterRadius, userLatLonValid ? 1 : 0,
                userLat, userLon);
  // Caly ekran rysuje sprite (patrz aprsRadarRenderFrame) - jeden pushSprite.
  drawAprsRadarBody();
}

static String extractXmlTagValue(const String &xml, const char* tag) {
  String openTag = "<" + String(tag) + ">";
  String closeTag = "</" + String(tag) + ">";
  int start = xml.indexOf(openTag);
  if (start < 0) return "";
  start += openTag.length();
  int end = xml.indexOf(closeTag, start);
  if (end < 0) return "";
  return xml.substring(start, end);
}

static String extractBandCondition(const String &xml, const char* bandName, const char* timeName) {
  String token = "<band name=\"" + String(bandName) + "\" time=\"" + String(timeName) + "\">";
  int start = xml.indexOf(token);
  if (start < 0) return "";
  start += token.length();
  int end = xml.indexOf("</band>", start);
  if (end < 0) return "";
  return xml.substring(start, end);
}

static void setPropagationBandDefaults(PropagationData &out) {
  out.hfBandLabel[0] = "80m-40m";
  out.hfBandFreq[0] = "3.5-7.3 MHz";
  out.hfBandLabel[1] = "30m-20m";
  out.hfBandFreq[1] = "10.1-14.35 MHz";
  out.hfBandLabel[2] = "17m-15m";
  out.hfBandFreq[2] = "18.068-21.45 MHz";
  out.hfBandLabel[3] = "12m-10m";
  out.hfBandFreq[3] = "24.89-29.7 MHz";

  for (int i = 0; i < 4; i++) {
    out.hfBandDay[i] = "--";
    out.hfBandNight[i] = "--";
  }
}

// Konwertuje czas UTC z hamqsl.com (format: "DD Mon YYYY at HHMM UTC") na czas lokalny
static String convertPropagationTimeToLocal(const String &utcTime) {
  if (utcTime.length() == 0 || utcTime == "--") return utcTime;
  
  // SprawdĹş czy zawiera "UTC" - jeĹ›li nie, zwrĂłÄ‡ oryginaĹ‚
  int utcPos = utcTime.indexOf("UTC");
  if (utcPos < 0) return utcTime;
  
  // SprawdĹş czy zawiera "at" - format: "DD Mon YYYY at HHMM UTC"
  int atPos = utcTime.indexOf(" at ");
  if (atPos < 0) return utcTime;
  
  // WyciÄ…gnij czas HHMM
  int timeStart = atPos + 4; // Po " at "
  String timePart = utcTime.substring(timeStart, utcPos - 1); // -1 aby usunÄ…Ä‡ spacjÄ™ przed UTC
  timePart.trim();
  
  if (timePart.length() < 4) return utcTime;
  
  // Parsuj godzinÄ™ i minutÄ™
  int hour = timePart.substring(0, 2).toInt();
  int minute = timePart.substring(2, 4).toInt();
  
  // Dodaj offset strefy czasowej i DST
  int dstOffset = 0;
  time_t now = time(nullptr);
  if (now > 100000) {
    struct tm utcTm;
    gmtime_r(&now, &utcTm);
    dstOffset = isEuropeanDST(&utcTm) ? 1 : 0;
  }
  int localHour = hour + timezoneHours + dstOffset;
  
  // ObsĹ‚uga przejĹ›cia przez pĂłĹ‚noc
  while (localHour < 0) localHour += 24;
  while (localHour >= 24) localHour -= 24;
  
  // Sformatuj lokalny czas
  String hh = (localHour < 10 ? "0" : "") + String(localHour);
  String mm = (minute < 10 ? "0" : "") + String(minute);
  
  // ZwrĂłÄ‡ sformatowany czas lokalny zachowujÄ…c datÄ™
  String datePart = utcTime.substring(0, atPos);
  return datePart + " " + hh + mm + " LOC";
}

static bool parsePropagationXml(const String &xml, PropagationData &out) {
  setPropagationBandDefaults(out);

  String sfi = extractXmlTagValue(xml, "solarflux");
  String ssn = extractXmlTagValue(xml, "sunspots");
  String kindex = extractXmlTagValue(xml, "kindex");
  String aindex = extractXmlTagValue(xml, "aindex");
  String xray = extractXmlTagValue(xml, "xray");
  String muf = extractXmlTagValue(xml, "muf");
  String updated = extractXmlTagValue(xml, "updated");

  sfi.trim();
  ssn.trim();
  kindex.trim();
  aindex.trim();
  xray.trim();
  muf.trim();
  updated.trim();

  if (sfi.length() == 0 || kindex.length() == 0) {
    return false;
  }

  out.sfi = sfi;
  out.ssn = ssn.length() ? ssn : "--";
  out.kindex = kindex;
  out.aindex = aindex.length() ? aindex : "--";
  out.xray = xray.length() ? xray : "--";
  out.muf = muf.length() ? muf : "--";
  out.updated = updated.length() ? convertPropagationTimeToLocal(updated) : "--";
  out.valid = true;
  out.lastError = "";
  out.fetchedAtMs = millis();

  for (int i = 0; i < 4; i++) {
    String day = extractBandCondition(xml, out.hfBandLabel[i].c_str(), "day");
    String night = extractBandCondition(xml, out.hfBandLabel[i].c_str(), "night");
    day.trim();
    night.trim();
    if (day.length() > 0) out.hfBandDay[i] = day;
    if (night.length() > 0) out.hfBandNight[i] = night;
  }

  return true;
}

bool fetchPropagationData() {
  if (WiFi.status() != WL_CONNECTED) {
    propagationData.lastError = "WiFi offline";
    return false;
  }

  static WiFiClientSecure client;

  client.setInsecure();

  HTTPClient http;
  http.setTimeout(2500);
  if (!http.begin(client, PROPAGATION_URL)) {
    propagationData.lastError = "HTTP begin failed";
    return false;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    propagationData.lastError = "HTTP " + String(httpCode);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  if (!parsePropagationXml(payload, propagationData)) {
    propagationData.lastError = "Parse error";
    propagationData.valid = false;
    return false;
  }

  return true;
}

void updateScreen3Clock() {
  // ZEGAR WYĹÄ„CZONY - usuniÄ™ty z nagĹ‚Ăłwka propagacji (Sun Spots)
  return;
}

uint32_t computeScreen3Signature() {
  const uint32_t fnvPrime = 16777619u;
  uint32_t hash = 2166136261u;

  hash ^= (uint32_t)propagationData.valid;
  hash *= fnvPrime;

  const String* fields[] = {
    &propagationData.sfi,
    &propagationData.kindex,
    &propagationData.aindex,
    &propagationData.updated,
    &propagationData.lastError
  };
  for (const String* field : fields) {
    for (size_t i = 0; i < field->length(); i++) {
      hash ^= (uint8_t)(*field)[i];
      hash *= fnvPrime;
    }
  }

  return hash;
}

static uint16_t conditionColor(String cond) {
  String up = cond;
  up.toUpperCase();
  if (up.indexOf("GOOD") >= 0) return TFT_GREEN;
  if (up.indexOf("FAIR") >= 0) return TFT_YELLOW;
  if (up.indexOf("POOR") >= 0) return TFT_RED;
  return TFT_LIGHTGREY;
}

void drawSunSpotsBody() {
  const int bodyTop = 32;
  const int bodyBottom = 220;
  tft.fillRect(0, bodyTop, 480, bodyBottom - bodyTop, TFT_BLACK);

  String sfiText = propagationData.valid && propagationData.sfi.length() ? propagationData.sfi : "--";
  String kText = propagationData.valid && propagationData.kindex.length() ? propagationData.kindex : "--";
  String aText = propagationData.valid && propagationData.aindex.length() ? propagationData.aindex : "--";
  String updatedText = propagationData.valid && propagationData.updated.length() ? propagationData.updated : "--";

  tft.setTextSize(2);
  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(10, 52);
  tft.print("SFI");

  tft.setTextSize(4);
  tft.setTextColor(TFT_GREEN);
  int sfiWidth = sfiText.length() * 24;
  tft.setCursor(460 - sfiWidth, 44);
  tft.print(sfiText);

  tft.setTextSize(2);
  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(10, 100);
  tft.print("K-INDEX");

  float kVal = kText.length() ? kText.toFloat() : -1.0f;
  uint16_t kColor = TFT_LIGHTGREY;
  if (kVal >= 0.0f) {
    kColor = (kVal < 4.0f) ? TFT_GREEN : (kVal < 6.0f) ? TFT_ORANGE : TFT_RED;
  }
  tft.setTextSize(4);
  tft.setTextColor(kColor);
  int kWidth = kText.length() * 24;
  tft.setCursor(460 - kWidth, 92);
  tft.print(kText);

  tft.setTextSize(2);
  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(10, 148);
  tft.print("A-INDEX");

  tft.setTextSize(4);
  tft.setTextColor(TFT_CYAN);
  int aWidth = aText.length() * 24;
  tft.setCursor(460 - aWidth, 140);
  tft.print(aText);

  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(10, 190);
  tft.print("UPDATED");
  tft.setTextSize(2);
  tft.setTextColor(TFT_LIGHTGREY);
  tft.setCursor(10, 202);
  tft.print(updatedText);

  if (!propagationData.valid) {
    tft.setTextSize(1);
    tft.setTextColor(TFT_RED);
    tft.setCursor(10, 176);
    String err = propagationData.lastError.length() ? propagationData.lastError : tr(TR_NO_DATA);
    tft.print(sanitizePolishToAscii(String(tr(TR_ERROR_PREFIX))));
    tft.print(err);
  }
}

void updateScreen3Data() {
  if (!tftInitialized || currentScreen != 3 || inMenu) {
    return;
  }

  updateScreen3Clock();

  static uint32_t lastSig = 0;
  uint32_t currentSig = computeScreen3Signature();
  if (currentSig == lastSig) {
    return;
  }
  lastSig = currentSig;

  drawSunSpotsBody();
}

void drawSunSpots() {
  // ekran czysci drawScreen (wspolny styl ISS)




  // Ikona WiFi w gĂłrnym prawym rogu
  
  // Ikona baterii obok ikony WiFi

  // WyĹ‚Ä…czono - zegar tylko na gĂłrnym pasku
  /*
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 1)) {
    char timeBuffer[10];
    strftime(timeBuffer, 10, "%H:%M Z", &timeinfo);
    int timeWidth = strlen(timeBuffer) * 12;
    tft.setCursor(460 - timeWidth, 8);
    tft.print(timeBuffer);
  }
  */

  drawSunSpotsBody();

  // StrzaĹ‚ki nawigacyjne - duĹĽe, blisko krawÄ™dzi (takie same na wszystkich ekranach)
  tft.setTextSize(1);
}

void updateScreen4Clock() {
  // ZEGAR WYĹÄ„CZONY - usuniÄ™ty z nagĹ‚Ăłwka propagacji (Sun Spots)
  return;
}

uint32_t computeScreen4Signature() {
  const uint32_t fnvPrime = 16777619u;
  uint32_t hash = 2166136261u;

  hash ^= (uint32_t)propagationData.valid;
  hash *= fnvPrime;

  for (int i = 0; i < 4; i++) {
    const String* fields[] = {
      &propagationData.hfBandLabel[i],
      &propagationData.hfBandFreq[i],
      &propagationData.hfBandDay[i],
      &propagationData.hfBandNight[i]
    };
    for (const String* field : fields) {
      for (size_t j = 0; j < field->length(); j++) {
        hash ^= (uint8_t)(*field)[j];
        hash *= fnvPrime;
      }
    }
  }

  for (size_t j = 0; j < propagationData.updated.length(); j++) {
    hash ^= (uint8_t)propagationData.updated[j];
    hash *= fnvPrime;
  }
  for (size_t j = 0; j < propagationData.lastError.length(); j++) {
    hash ^= (uint8_t)propagationData.lastError[j];
    hash *= fnvPrime;
  }

  return hash;
}

void drawBandInfoBody() {
  const int bodyTop = 45; // Zaczynamy niĹĽej, pod liniÄ…
  const int bodyBottom = 280;
  tft.fillRect(0, bodyTop, 480, bodyBottom - bodyTop, TFT_BLACK);

  // === GĂ“RNA SEKCJA - INDEKSY SĹONECZNE (3 kolumny) ===
  int yPos = bodyTop + 5;
  int lineHeight = 22;

  // Pierwsza linia: SFI | A-idx | MUF (w ramce)
  tft.setTextSize(2);

  // SFI
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(10, yPos);
  tft.print("SFI:");
  tft.setTextColor(TFT_WHITE);
  tft.print(propagationData.sfi.length() ? propagationData.sfi : "--");

  // A-idx (Ĺ›rodek)
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(130, yPos);
  tft.print("A-idx:");
  tft.setTextColor(TFT_WHITE);
  tft.print(propagationData.aindex.length() ? propagationData.aindex : "--");

  // MUF w niebieskiej ramce z ĹĽĂłĹ‚tym tekstem
  tft.fillRect(280, yPos - 2, 145, 22, TFT_BLUE);
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(290, yPos);
  String mufText = propagationData.muf.length() ? propagationData.muf : "--";
  tft.print("MUF:" + mufText + "MHz");

  // Druga linia: SSN | K-idx | X-Ray (w ramce)
  yPos += lineHeight;

  // SSN
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(10, yPos);
  tft.print("SSN:");
  tft.setTextColor(TFT_WHITE);
  tft.print(propagationData.ssn.length() ? propagationData.ssn : "--");

  // K-idx (Ĺ›rodek)
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(130, yPos);
  tft.print("K-idx:");
  tft.setTextColor(TFT_WHITE);
  tft.print(propagationData.kindex.length() ? propagationData.kindex : "--");

  // X-Ray w niebieskiej ramce z ĹĽĂłĹ‚tym tekstem
  tft.fillRect(280, yPos - 2, 100, 22, TFT_BLUE);
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(290, yPos);
  String xrayText = propagationData.xray.length() ? propagationData.xray : "--";
  tft.print("X-Ray:" + xrayText);

  // === ĹšRODKOWA SEKCJA - TABELA PASMA (lewa strona) ===
  yPos += lineHeight + 10;
  int tableTop = yPos;

  // Ramka wokĂłĹ‚ tabeli
  tft.drawRoundRect(10, tableTop - 5, 260, 140, 5, TFT_DARKGREY);

  // NagĹ‚Ăłwki tabeli
  tft.setTextSize(2);
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(15, yPos);
  tft.print("HF BAND");
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(105, yPos);
  tft.print("DAY");
  tft.setCursor(175, yPos);
  tft.print("NIGHT");
  tft.drawFastHLine(12, yPos + 16, 256, TFT_DARKGREY);

  yPos += 22;

  // Wiersze tabeli (4 pasma)
  int rowHeight = 30;
  for (int i = 0; i < 4; i++) {
    // PodĹ›wietlenie co drugi wiersz (w ramce)
    if (i % 2 == 0) {
      tft.fillRect(12, yPos - 2, 256, rowHeight - 2, 0x1082); // Ciemnoszary
    }

    tft.setTextSize(2);

    // BAND label
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(15, yPos);
    tft.print(propagationData.hfBandLabel[i]);

    // Day - kolor w zaleĹĽnoĹ›ci od warunku
    String dayCond = propagationData.hfBandDay[i];
    tft.setTextColor(conditionColor(dayCond));
    tft.setCursor(105, yPos);
    tft.print(dayCond.length() ? dayCond : "--");

    // Night
    String nightCond = propagationData.hfBandNight[i];
    tft.setTextColor(conditionColor(nightCond));
    tft.setCursor(175, yPos);
    tft.print(nightCond.length() ? nightCond : "--");

    yPos += rowHeight;
  }

  // === PRAWA STRONA - S/N, Aurora, Ikona ===
  // Pozycja ikony (bardziej w lewo i niĹĽej)
  int iconX = 340;
  int iconY = tableTop + 90;
  int iconSize = 50; // ZakĹ‚adamy rozmiar ikony 50x50

  // S/N - wyĹ›rodkowane nad ikonÄ…
  tft.setTextSize(2);
  tft.setTextColor(TFT_CYAN);
  String snText = "S/N:";
  String snVal = "S1-S2";
  int snWidth = tft.textWidth(snText) + tft.textWidth(snVal);
  int snX = iconX + (iconSize - snWidth) / 2;
  tft.setCursor(snX, iconY - 55);
  tft.print(snText);
  tft.setTextColor(TFT_WHITE);
  tft.print(snVal);

  // Aurora - wyĹ›rodkowane nad ikonÄ…
  String auroraText = "Aurora:";
  String auroraVal = "2";
  int auroraWidth = tft.textWidth(auroraText) + tft.textWidth(auroraVal);
  int auroraX = iconX + (iconSize - auroraWidth) / 2;
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(auroraX, iconY - 35);
  tft.print(auroraText);
  tft.setTextColor(TFT_WHITE);
  tft.print(auroraVal);

  // === IKONA PROPAGACJI (sĹ‚oĹ„ce/chmura) ===
  int kIndexVal = propagationData.kindex.toInt();
  String propIcon;
  if (kIndexVal < 3) {
    propIcon = "/icon50/800.bmp"; // SĹ‚oĹ„ce - dobre warunki
  } else if (kIndexVal < 5) {
    propIcon = "/icon50/801.bmp"; // CzÄ™Ĺ›ciowe zachmurzenie - Ĺ›rednie
  } else {
    propIcon = "/icon50/200.bmp"; // Burza - sĹ‚abe warunki
  }
  if (littleFsReady && LittleFS.exists(propIcon)) {
    drawBmpFromFS(propIcon, iconX, iconY);
  }

  // === DOLNA SEKCJA - INFO O ZAMKNIÄTYCH PASMACH ===
  yPos = tableTop + 145; // Pod tabelÄ…
  tft.setTextSize(1);
  tft.setTextColor(TFT_RED);
  tft.setCursor(10, yPos);
  tft.print("Very High Band Closed");
  tft.setCursor(155, yPos);
  tft.print("Es Europe Band Closed");

  yPos += 14;
  tft.setCursor(10, yPos);
  tft.print("Es Asia Band Closed");
  tft.setCursor(155, yPos);
  tft.print("Es Afr Band Closed");

  // === STATUS BĹÄDU ===
  if (!propagationData.valid) {
    tft.setTextSize(1);
    tft.setTextColor(TFT_RED);
    tft.setCursor(10, bodyBottom - 20);
    String err = propagationData.lastError.length() ? propagationData.lastError : tr(TR_NO_DATA);
    tft.print(sanitizePolishToAscii(String(tr(TR_ERROR_PREFIX))));
    tft.print(err);
  }
}

void updateScreen4Data() {
  if (!tftInitialized || currentScreen != 4 || inMenu) {
    return;
  }

  // WyĹ‚Ä…czono - zegar tylko na gĂłrnym pasku
  // updateScreen4Clock();

  static uint32_t lastSig = 0;
  uint32_t currentSig = computeScreen4Signature();
  if (currentSig == lastSig) {
    return;
  }
  lastSig = currentSig;

  drawBandInfoBody();
}

void drawBandInfo() {
  // ekran czysci drawScreen (wspolny styl ISS)

  // NagĹ‚Ăłwek w kolorze ĹĽĂłĹ‚tym/zĹ‚otym na czarnym tle (jak na zdjÄ™ciu)
  tft.setTextColor(TFT_GOLD);

  // Ikona WiFi w gĂłrnym prawym rogu
  
  // Ikona baterii obok ikony WiFi

  // Linia pod nagĹ‚Ăłwkiem
  tft.drawLine(10, 35, 470, 35, TFT_DARKGREY);

  // ZEGAR - rysowany osobno przez updateBandInfoClock()

  drawBandInfoBody();

  // StrzaĹ‚ki nawigacyjne - duĹĽe, blisko krawÄ™dzi (takie same na wszystkich ekranach)
  tft.setTextSize(1);
}

bool fetchWeatherForecast(double lat, double lon);

bool fetchWeatherData() {
  if (WiFi.status() != WL_CONNECTED) {
    weatherData.lastError = "WiFi offline";
    weatherData.valid = false;
    return false;
  }
  if (weatherApiKey.length() == 0) {
    weatherData.lastError = "No API key";
    weatherData.valid = false;
    return false;
  }

  double lat = 0.0;
  double lon = 0.0;
  if (userLatLonValid) {
    lat = userLat;
    lon = userLon;
  } else if (userLocator.length() >= 4) {
    locatorToLatLon(userLocator, lat, lon);
  } else {
    weatherData.lastError = "No locator";
    weatherData.valid = false;
    return false;
  }

  // Reset prognoz przed nowym pobraniem
  weatherData.forecast3hValid = false;
  weatherData.forecastNextDayValid = false;
  for (uint8_t i = 0; i < WeatherData::DETAIL_COLS; i++) {
    weatherData.detailValid[i] = false;
  }

  {
    // Zakres { } jest celowy: HTTPClient tego pierwszego (podstawowego)
    // zapytania musi zostaÄ‡ w peĹ‚ni zniszczony ZANIM wywoĹ‚amy
    // fetchWeatherForecast()/fetchAirPollutionData() poniĹĽej.
    // WiFiClientSecure jest static - reuse zamiast nowy za kazdym razem,
    // zeby nie wyczerpywac pamieci ESP32 TLS kontekstami (~20KB kazdy).
    static WiFiClientSecure client;
    client.setInsecure();

    String langParam = (tftLanguage == TFT_LANG_EN) ? "en" : "pl";
    String url = "https://api.openweathermap.org/data/2.5/weather?lat=" +
                 String(lat, 4) + "&lon=" + String(lon, 4) +
                 "&appid=" + weatherApiKey + "&units=metric&lang=" + langParam;

    HTTPClient http;
    http.setTimeout(2500);
    if (!http.begin(client, url)) {
      weatherData.lastError = "HTTP begin failed";
      weatherData.valid = false;
      return false;
    }

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
      weatherData.lastError = "HTTP " + String(httpCode);
      weatherData.valid = false;
      http.end();
      return false;
    }

    WiFiClient *stream = http.getStreamPtr();

    DynamicJsonDocument filter(512);
    filter["cod"] = true;
    filter["message"] = true;
    filter["name"] = true;
    filter["weather"][0]["description"] = true;
    filter["weather"][0]["icon"] = true;
    filter["weather"][0]["id"] = true;
    filter["main"]["temp"] = true;
    filter["main"]["humidity"] = true;
    filter["main"]["pressure"] = true;
    filter["wind"]["speed"] = true;

    DynamicJsonDocument doc(1536);
    DeserializationError err = deserializeJson(doc, *stream, DeserializationOption::Filter(filter));
    http.end();
    if (err) {
      weatherData.lastError = "JSON error";
      weatherData.valid = false;
      return false;
    }

    int cod = doc["cod"] | 0;
    if (cod != 200) {
      String msg = doc["message"] | "";
      weatherData.lastError = msg.length() ? msg : "API error";
      weatherData.valid = false;
      return false;
    }

    String desc = doc["weather"][0]["description"] | "";
    String cityName = doc["name"] | "";
    String icon = doc["weather"][0]["icon"] | ""; // 01d / 01n itp. do wykrycia pory dnia
    int weatherId = doc["weather"][0]["id"] | 800; // pobieranie kodu pogody
    float temp = doc["main"]["temp"] | 0.0f;
    int humidity = doc["main"]["humidity"] | 0;
    int pressure = doc["main"]["pressure"] | 0;
    float wind = doc["wind"]["speed"] | 0.0f;


    if (desc.equalsIgnoreCase("zachmurzenie umiarkowane")) {
      desc = "zachmurzenie";
    }

    weatherData.description = desc.length() ? desc : "--";
    weatherData.cityName = cityName;
    weatherData.iconCode = icon;
    weatherData.weatherId = weatherId; // kod pogody
    weatherData.tempC = temp;
    weatherData.humidity = humidity;
    weatherData.pressure = pressure;
    weatherData.windMs = wind;
  } // <- client/http/doc znikajÄ… tutaj, zwalniajÄ…c pamiÄ™Ä‡ TLS i JSON

  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 1)) {
    char timeBuf[6];
    strftime(timeBuf, sizeof(timeBuf), "%H:%M", &timeinfo);
    weatherData.updated = String(timeBuf);
  } else {
    weatherData.updated = "--:--";
  }

  weatherData.valid = true;
  weatherData.lastError = "";
  weatherData.fetchedAtMs = millis();

  // Prognozy (3h i jutro) - osobne poĹ‚Ä…czenie HTTPS, uruchamiane dopiero
  // teraz, gdy powyĹĽsze poĹ‚Ä…czenie zostaĹ‚o w peĹ‚ni zamkniÄ™te.
  fetchWeatherForecast(lat, lon);

  // Pobierz dane o jakoĹ›ci powietrza (PM2.5 i PM10)
  fetchAirPollutionData(lat, lon);

  return true;
}

bool fetchWeatherForecast(double lat, double lon) {
  if (WiFi.status() != WL_CONNECTED || weatherApiKey.length() == 0) {
    return false;
  }

  static WiFiClientSecure client;
  client.setInsecure();

  String langParam = (tftLanguage == TFT_LANG_EN) ? "en" : "pl";
  String url = "https://api.openweathermap.org/data/2.5/forecast?lat=" +
               String(lat, 4) + "&lon=" + String(lon, 4) +
               "&appid=" + weatherApiKey + "&units=metric&lang=" + langParam + "&cnt=40";

  HTTPClient http;
  http.setTimeout(2500);
  if (!http.begin(client, url)) {
    return false;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();

  DynamicJsonDocument filter(1024);
  filter["list"][0]["dt"] = true;
  filter["list"][0]["main"]["temp"] = true;
  filter["list"][0]["main"]["humidity"] = true;
  filter["list"][0]["wind"]["speed"] = true;
  filter["list"][0]["weather"][0]["description"] = true;
  filter["list"][0]["weather"][0]["id"] = true;
  filter["list"][0]["weather"][0]["icon"] = true;
  filter["city"]["name"] = true;

  DynamicJsonDocument doc(16384);
  DeserializationError err = deserializeJson(doc, *stream, DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    return false;
  }

  JsonArray list = doc["list"].as<JsonArray>();
  if (list.isNull() || list.size() == 0) {
    return false;
  }

  if (weatherData.cityName.length() == 0) {
    String cityFromForecast = doc["city"]["name"] | "";
    if (cityFromForecast.length() > 0) {
      weatherData.cityName = cityFromForecast;
    }
  }

  const long nowUnix = (long)time(nullptr);
  const long targetOffsetsSec[WeatherData::DETAIL_COLS] = {
    3L * 3600L,
    6L * 3600L,
    24L * 3600L,
    48L * 3600L,
    72L * 3600L
  };
  const int fallbackIdx[WeatherData::DETAIL_COLS] = {0, 1, 8, 16, 24};
  
  // UwzglÄ™dnij DST (czas letni) w obliczeniach strefy czasowej
  struct tm utcTm;
  time_t nowTimeT = (time_t)nowUnix;
  gmtime_r(&nowTimeT, &utcTm);
  int dstOffset = isEuropeanDST(&utcTm) ? 1 : 0;
  const long timezoneOffsetSec = (long)(timezoneHours + dstOffset) * 3600L;
  
  const long nowLocalUnix = nowUnix + timezoneOffsetSec;
  const long baseLocalDayStart = (nowLocalUnix / 86400L) * 86400L;

  weatherData.nightTempValid[0] = false;
  weatherData.nightTempValid[1] = false;

  auto assignSlot = [&](uint8_t slot, JsonObject entry) {
    weatherData.detailTempC[slot] = entry["main"]["temp"] | 0.0f;
    weatherData.detailHumidity[slot] = entry["main"]["humidity"] | 0;
    weatherData.detailWindMs[slot] = entry["wind"]["speed"] | 0.0f;
    weatherData.detailWeatherId[slot] = entry["weather"][0]["id"] | 800;
    weatherData.detailIconCode[slot] = String((const char *)(entry["weather"][0]["icon"] | ""));
    weatherData.detailValid[slot] = true;
  };

  for (uint8_t slot = 0; slot < WeatherData::DETAIL_COLS; slot++) {
    int chosenIdx = -1;

    // Dla slotĂłw 0 i 1 (+3h, +6h) - znajdĹş najbliĹĽszy czas do target
    // Dla slotĂłw 2+ (dni) - znajdĹş najlepsze dopasowanie dnia (poĹ‚udnie)
    if (slot >= 2 && nowUnix > 100000L) {
      const long targetDayStartLocal = baseLocalDayStart + (long)(slot - 1) * 86400L;
      const long targetNoonLocal = targetDayStartLocal + 12L * 3600L;
      long bestScore = 0x7FFFFFFF;

      for (uint16_t i = 0; i < list.size(); i++) {
        JsonObject entry = list[i];
        long dt = entry["dt"] | 0L;
        if (dt <= 0L) {
          continue;
        }

        long dtLocal = dt + timezoneOffsetSec;
        long entryDayStartLocal = (dtLocal / 86400L) * 86400L;
        if (entryDayStartLocal != targetDayStartLocal) {
          continue;
        }

        int localHour = (int)((dtLocal % 86400L) / 3600L);
        bool isDayHour = (localHour >= 9 && localHour <= 18);
        String iconCode = String((const char *)(entry["weather"][0]["icon"] | ""));
        bool iconIsDay = iconCode.endsWith("d");
        bool isDayCandidate = isDayHour || iconIsDay;

        long score = labs(dtLocal - targetNoonLocal);
        if (!isDayCandidate) {
          score += 12L * 3600L;
        }

        if (score < bestScore) {
          bestScore = score;
          chosenIdx = (int)i;
        }
      }
    }

    // Dla slotĂłw 0 i 1 (+3h, +6h) lub jeĹ›li nie znaleziono dopasowania dnia
    if (chosenIdx < 0 && nowUnix > 100000L) {
      long target = nowUnix + targetOffsetsSec[slot];
      long bestDiff = 0x7FFFFFFF;

      for (uint16_t i = 0; i < list.size(); i++) {
        JsonObject entry = list[i];
        long dt = entry["dt"] | 0L;
        if (dt <= 0L) {
          continue;
        }
        long diff = labs(dt - target);
        if (diff < bestDiff) {
          bestDiff = diff;
          chosenIdx = (int)i;
        }
      }
    }

    // Fallback gdy czas niezsynchronizowany lub brak dopasowania - uĹĽyj indeksĂłw fallback
    if (chosenIdx < 0) {
      int idx = fallbackIdx[slot];
      if (idx < (int)list.size()) {
        chosenIdx = idx;
      } else if (list.size() > 0) {
        chosenIdx = list.size() - 1;  // Ostatni dostÄ™pny element
      }
    }

    if (chosenIdx >= 0 && chosenIdx < (int)list.size()) {
      assignSlot(slot, list[chosenIdx]);
    }
  }

  for (uint8_t daySlot = 0; daySlot < 2; daySlot++) {
    const long targetDayStartLocal = baseLocalDayStart + (long)(daySlot + 1) * 86400L;
    int bestNightIdx = -1;
    long bestNightScore = 0x7FFFFFFF;
    int bestAnyIdx = -1;
    float bestAnyTemp = 1000.0f;

    for (uint16_t i = 0; i < list.size(); i++) {
      JsonObject entry = list[i];
      long dt = entry["dt"] | 0L;
      if (dt <= 0L) {
        continue;
      }

      long dtLocal = dt + timezoneOffsetSec;
      long entryDayStartLocal = (dtLocal / 86400L) * 86400L;
      if (entryDayStartLocal != targetDayStartLocal) {
        continue;
      }

      float temp = entry["main"]["temp"] | 0.0f;
      if (bestAnyIdx < 0 || temp < bestAnyTemp) {
        bestAnyTemp = temp;
        bestAnyIdx = (int)i;
      }

      int localHour = (int)((dtLocal % 86400L) / 3600L);
      bool isNightHour = (localHour <= 6 || localHour >= 21);
      if (!isNightHour) {
        continue;
      }

      int refHour = (localHour >= 21) ? 24 + localHour : localHour;
      int refTarget = 26; // 02:00 w oknie 21..30
      long score = labs((long)refHour - (long)refTarget);
      if (score < bestNightScore) {
        bestNightScore = score;
        bestNightIdx = (int)i;
      }
    }

    int chosenNightIdx = (bestNightIdx >= 0) ? bestNightIdx : bestAnyIdx;
    if (chosenNightIdx >= 0 && chosenNightIdx < (int)list.size()) {
      JsonObject entry = list[chosenNightIdx];
      weatherData.nightTempC[daySlot] = entry["main"]["temp"] | 0.0f;
      weatherData.nightTempValid[daySlot] = true;
    }
  }

  if (weatherData.detailValid[0]) {
    weatherData.forecast3hTempC = weatherData.detailTempC[0];
    weatherData.forecast3hWindMs = weatherData.detailWindMs[0];
    weatherData.forecast3hDesc = "--";
    weatherData.forecast3hValid = true;
  }

  if (weatherData.detailValid[2]) {
    weatherData.forecastNextDayTempC = weatherData.detailTempC[2];
    weatherData.forecastNextDayWindMs = weatherData.detailWindMs[2];
    weatherData.forecastNextDayDesc = "--";
    weatherData.forecastNextDayValid = true;
  }

  bool anyDetailValid = false;
  for (uint8_t i = 0; i < WeatherData::DETAIL_COLS; i++) {
    if (weatherData.detailValid[i]) {
      anyDetailValid = true;
      break;
    }
  }

  return anyDetailValid;
}

bool fetchAirPollutionData(double lat, double lon) {
  if (WiFi.status() != WL_CONNECTED || weatherApiKey.length() == 0) {
    Serial.println("[AIR] No WiFi or API key");
    return false;
  }

  static WiFiClientSecure client;
  client.setInsecure();

  String url = "https://api.openweathermap.org/data/2.5/air_pollution?lat=" +
               String(lat, 4) + "&lon=" + String(lon, 4) +
               "&appid=" + weatherApiKey;

  HTTPClient http;
  http.setTimeout(2500);
  if (!http.begin(client, url)) {
    return false;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();

  DynamicJsonDocument filter(256);
  filter["list"][0]["components"]["pm2_5"] = true;
  filter["list"][0]["components"]["pm10"] = true;

  DynamicJsonDocument doc(768);
  DeserializationError err = deserializeJson(doc, *stream, DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    return false;
  }

  // Pobierz wartoĂ„Ä…Ă˘â‚¬Ĺźci PM2.5 i PM10 z komponentĂ„â€šĹ‚w
  JsonArray list = doc["list"].as<JsonArray>();
  if (list.isNull() || list.size() == 0) {
    return false;
  }

  JsonObject listItem = list[0];
  JsonObject components = listItem["components"].as<JsonObject>();
  if (components.isNull()) {
    Serial.println("[AIR] No components data");
    return false;
  }

  weatherData.pm25 = components["pm2_5"] | 0.0f;
  weatherData.pm10 = components["pm10"] | 0.0f;
  Serial.print("[AIR] PM2.5: ");
  Serial.print(weatherData.pm25);
  Serial.print(", PM10: ");
  Serial.println(weatherData.pm10);
  return true;
}

// Funkcja okreĂ„Ä…Ă˘â‚¬ĹźlajÄ‚â€žĂ˘â‚¬Â¦ca kolor na podstawie wartoĂ„Ä…Ă˘â‚¬Ĺźci PM2.5
uint16_t getPM25Color(float pm25) {
  if (pm25 <= 12) return TFT_GREEN;      // Dobra
  if (pm25 <= 35) return TFT_YELLOW;     // Umiarkowana
  if (pm25 <= 55) return TFT_ORANGE;     // Niezdrowa dla wraĂ„Ä…Ă„Ëťliwych
  if (pm25 <= 150) return TFT_RED;       // Niezdrowa
  return 0xF800; // Ciemny czerwony - Bardzo niezdrowa
}

// Funkcja okreĂ„Ä…Ă˘â‚¬ĹźlajÄ‚â€žĂ˘â‚¬Â¦ca kolor na podstawie wartoĂ„Ä…Ă˘â‚¬Ĺźci PM10
uint16_t getPM10Color(float pm10) {
  if (pm10 <= 20) return TFT_GREEN;      // Dobra
  if (pm10 <= 50) return TFT_YELLOW;     // Umiarkowana
  if (pm10 <= 100) return TFT_ORANGE;   // Niezdrowa dla wraĂ„Ä…Ă„Ëťliwych
  if (pm10 <= 200) return TFT_RED;      // Niezdrowa
  return 0xF800; // Ciemny czerwony - Bardzo niezdrowa
}

void updateScreen5Clock() {
  if (!tftInitialized ||
      (currentScreen != SCREEN_WEATHER_DSP && currentScreen != SCREEN_WEATHER_FORECAST) ||
      inMenu) {
    return;
  }

  struct tm timeinfo;
  if (!getTimeWithTimezone(&timeinfo)) {
    return;
  }

  char timeBuffer[6];
  strftime(timeBuffer, 6, "%H:%M", &timeinfo);
  int timeWidth = strlen(timeBuffer) * 12;
  int timeX = 475 - timeWidth;

  tft.fillRect(timeX - 2, 4, timeWidth + 6, 24, TFT_RADIO_ORANGE);
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(timeX, 8);
  tft.print(timeBuffer);
}

uint32_t computeScreen5Signature() {
  const uint32_t fnvPrime = 16777619u;
  uint32_t hash = 2166136261u;

  hash ^= (uint32_t)weatherData.valid;
  hash *= fnvPrime;
  bool footerVisible = (currentScreen == SCREEN_WEATHER_FORECAST)
                       ? isTableNavFooterVisible(SCREEN_WEATHER_FORECAST)
                       : isTableNavFooterVisible(SCREEN_WEATHER_DSP);
  hash ^= footerVisible ? 1u : 0u;
  hash *= fnvPrime;

  const String* fields[] = {&weatherData.cityName, &weatherData.description, &weatherData.updated, &weatherData.lastError};
  for (const String* field : fields) {
    for (size_t j = 0; j < field->length(); j++) {
      hash ^= (uint8_t)(*field)[j];
      hash *= fnvPrime;
    }
  }
  hash ^= (uint32_t)(weatherData.tempC * 10);
  hash *= fnvPrime;
  hash ^= (uint32_t)weatherData.humidity;
  hash *= fnvPrime;
  hash ^= (uint32_t)weatherData.pressure;
  hash *= fnvPrime;
  hash ^= (uint32_t)(weatherData.windMs * 10);
  hash *= fnvPrime;
  hash ^= (uint32_t)(weatherData.pm25 * 10);
  hash *= fnvPrime;
  hash ^= (uint32_t)(weatherData.pm10 * 10);
  hash *= fnvPrime;

  // Prognozy
  hash ^= (uint32_t)weatherData.forecast3hValid;
  hash *= fnvPrime;
  hash ^= (uint32_t)(weatherData.forecast3hTempC * 10);
  hash *= fnvPrime;
  hash ^= (uint32_t)(weatherData.forecast3hWindMs * 10);
  hash *= fnvPrime;
  for (size_t j = 0; j < weatherData.forecast3hDesc.length(); j++) {
    hash ^= (uint8_t)weatherData.forecast3hDesc[j];
    hash *= fnvPrime;
  }

  hash ^= (uint32_t)weatherData.forecastNextDayValid;
  hash *= fnvPrime;
  hash ^= (uint32_t)(weatherData.forecastNextDayTempC * 10);
  hash *= fnvPrime;
  hash ^= (uint32_t)(weatherData.forecastNextDayWindMs * 10);
  hash *= fnvPrime;
  for (size_t j = 0; j < weatherData.forecastNextDayDesc.length(); j++) {
    hash ^= (uint8_t)weatherData.forecastNextDayDesc[j];
    hash *= fnvPrime;
  }

  for (uint8_t i = 0; i < WeatherData::DETAIL_COLS; i++) {
    hash ^= (uint32_t)weatherData.detailValid[i];
    hash *= fnvPrime;
    hash ^= (uint32_t)(weatherData.detailTempC[i] * 10);
    hash *= fnvPrime;
    hash ^= (uint32_t)weatherData.detailHumidity[i];
    hash *= fnvPrime;
    hash ^= (uint32_t)(weatherData.detailWindMs[i] * 10);
    hash *= fnvPrime;
    hash ^= (uint32_t)weatherData.detailWeatherId[i];
    hash *= fnvPrime;
    for (size_t j = 0; j < weatherData.detailIconCode[i].length(); j++) {
      hash ^= (uint8_t)weatherData.detailIconCode[i][j];
      hash *= fnvPrime;
    }
  }

  return hash;
}

// Funkcja pomocnicza: Kolor wiatru zaleĂ„Ä…Ă„Ëťny od prÄ‚â€žĂ˘â€žËdkoĂ„Ä…Ă˘â‚¬Ĺźci (m/s)
uint16_t getWindColor(float speed) {
  if (speed < 5.0)  return TFT_GREEN;       // Bezpiecznie
  if (speed < 10.0) return TFT_YELLOW;      // Umiarkowanie
  if (speed < 15.0) return 0xFBE0;          // Silny (Orange)
  return TFT_RED;                           // Niebezpieczny dla anten
}

// Mapowanie kodu OWM na plik ikony (zgodnie z przygotowanÄ… listÄ…)
String weatherIconPathForId(int id, bool isNight) {
  const String base = "/icon50/";

  // 2xx burza
  if (id >= 200 && id <= 232) return base + "200.bmp";

  // 3xx mĹĽawka
  if (id >= 300 && id <= 321) return base + "300.bmp";

  // 5xx deszcz
  if (id >= 500 && id <= 531) {
    if (id == 500 || id == 501) return base + "500.bmp";
    if (id == 502 || id == 503) return base + "502.bmp";
    if (id == 511 || id == 531) return base + "511.bmp";
    return base + "520.bmp";
  }

  // 6xx Ĺ›nieg
  if (id >= 600 && id <= 622) {
    if (id == 600 || id == 601 || id == 602 || id == 620 || id == 621 || id == 622) return base + "600.bmp";
    // 611..616 (takĹĽe 613/615/616) -> 611
    return base + "611.bmp";
  }

  // 7xx atmosfera
  if (id >= 700 && id <= 781) return base + "700.bmp";

  // 800 clear (dzieĹ„/noc)
  if (id == 800) return base + (isNight ? "800n.bmp" : "800.bmp");

  // 80x chmury
  if (id == 801 || id == 802) return base + (isNight ? "801n.bmp" : "801.bmp");
  if (id == 803 || id == 804) return base + "803.bmp";

  return base + "unknown.bmp";
}

// Funkcja pomocnicza: Rysowanie ikon pogodowych (priorytet: id -> plik BMP, fallback: opis)
void drawWeatherIcon(int x, int y, int weatherId, String desc) {
  desc.toLowerCase();

  // DzieĹ„/noc z ikony OWM (np. 10d/10n). JeĹ›li brak, fallback na czas lokalny.
  bool isNight = false;
  if (weatherData.iconCode.endsWith("n")) {
    isNight = true;
  } else if (weatherData.iconCode.endsWith("d")) {
    isNight = false;
  } else {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 1)) {
      isNight = (timeinfo.tm_hour < 6 || timeinfo.tm_hour >= 18);
    }
  }

  String iconFile = weatherIconPathForId(weatherId, isNight);

  // Najpierw prĂłbujemy gotowÄ… ikonÄ™ BMP
  if (drawBmpFromFS(iconFile, x - 25, y - 25)) {
    return;
  }

  // Fallback: stare ikonki rysowane na podstawie opisu (gdy brak pliku)
  if (desc.indexOf("slon") >= 0 || desc.indexOf("clear") >= 0 || desc.indexOf("pogod") >= 0) {
    tft.fillCircle(x, y, 18, TFT_YELLOW); // SĹ‚oĹ„ce
    for(int i=0; i<360; i+=45) {
       float rad = i * 0.01745;
       tft.drawLine(x+cos(rad)*22, y+sin(rad)*22, x+cos(rad)*32, y+sin(rad)*32, TFT_YELLOW);
    }
  } 
  else if (desc.indexOf("burz") >= 0 || desc.indexOf("thunder") >= 0) {
    tft.fillCircle(x, y-5, 12, TFT_DARKGREY); // Chmura burzowa
    tft.fillTriangle(x+2, y+8, x+10, y+8, x+4, y+22, TFT_YELLOW); // BĹ‚ysk
  }
  else if (desc.indexOf("mza") >= 0 || desc.indexOf("drizzle") >= 0) {
    tft.fillCircle(x-10, y+5, 12, TFT_LIGHTGREY); // Chmura + mgĹ‚awka
    tft.fillCircle(x+10, y+5, 12, TFT_LIGHTGREY);
    tft.fillCircle(x, y-5, 15, TFT_LIGHTGREY);
    tft.drawLine(x-6, y+10, x-8, y+16, TFT_CYAN);
    tft.drawLine(x+2, y+10, x, y+16, TFT_CYAN);
  }
  else if (desc.indexOf("snieg") >= 0 || desc.indexOf("snow") >= 0) {
    tft.fillCircle(x, y-5, 12, TFT_LIGHTGREY); // Chmura + Ĺ›nieg
    tft.drawLine(x-6, y+12, x-2, y+16, TFT_WHITE);
    tft.drawLine(x-2, y+12, x-6, y+16, TFT_WHITE);
    tft.drawLine(x+2, y+12, x+6, y+16, TFT_WHITE);
    tft.drawLine(x+6, y+12, x+2, y+16, TFT_WHITE);
  }
  else if (desc.indexOf("mg") >= 0 || desc.indexOf("mist") >= 0 || desc.indexOf("fog") >= 0 || desc.indexOf("haze") >= 0) {
    tft.drawLine(x-18, y-2, x+18, y-2, TFT_LIGHTGREY); // MgĹ‚a
    tft.drawLine(x-20, y+4, x+20, y+4, TFT_LIGHTGREY);
    tft.drawLine(x-16, y+10, x+16, y+10, TFT_LIGHTGREY);
  }
  else if (desc.indexOf("sleet") >= 0 || desc.indexOf("deszcz ze sniegiem") >= 0) {
    tft.fillCircle(x, y-5, 12, TFT_LIGHTGREY); // Deszcz ze Ĺ›niegiem
    tft.drawLine(x-6, y+10, x-8, y+18, TFT_CYAN);
    tft.drawLine(x+2, y+10, x, y+18, TFT_CYAN);
    tft.drawLine(x+6, y+12, x+2, y+16, TFT_WHITE);
    tft.drawLine(x+2, y+12, x+6, y+16, TFT_WHITE);
  }
  else if (desc.indexOf("zachmur") >= 0 || desc.indexOf("cloud") >= 0 || desc.indexOf("pochmurnie") >= 0) {
    tft.fillCircle(x-10, y+5, 12, TFT_LIGHTGREY); // Chmura
    tft.fillCircle(x+10, y+5, 12, TFT_LIGHTGREY);
    tft.fillCircle(x, y-5, 15, TFT_LIGHTGREY);
  }
  else if (desc.indexOf("deszcz") >= 0 || desc.indexOf("rain") >= 0) {
    tft.fillCircle(x, y-5, 12, TFT_DARKGREY); // Chmura deszczowa
    tft.drawLine(x-5, y+10, x-8, y+20, TFT_CYAN);
    tft.drawLine(x+5, y+10, x+2, y+20, TFT_CYAN);
    tft.drawLine(x, y+12, x-3, y+22, TFT_CYAN);
  } else {
    tft.drawCircle(x, y, 15, TFT_WHITE); // Ikona domyĹ›lna (okrÄ…g)
  }
}

// GĹ‚Ăłwna funkcja ciaĹ‚a ekranu pogodowego
void drawWeatherBody() {
  const int bodyTop = 32;
  const int bodyBottom = 220;
  
  // Czyszczenie obszaru roboczego
  tft.fillRect(0, bodyTop, 480, bodyBottom - bodyTop, TFT_BLACK);

  if (!weatherData.valid) {
    tft.setTextColor(TFT_RED);
    tft.setTextSize(2);
    tft.setCursor(50, 110);
    tft.print("WAITING FOR DATA...");
    return;
  }

  // 1. IKONA I OPIS (Prawa strona) - dostosowane do 480px
  drawWeatherIcon(400, 70, weatherData.weatherId, weatherData.description);

  // Opis pogody z polskimi znakami, czcionka VLW 20px
  bool fontLoaded = false;
  int descY = 105;
  int descX = 400; // PrzesuniÄ™te bardziej w prawo dla 480px
  if (littleFsReady && LittleFS.exists(ROBOTO_FONT12_FILE) && ESP.getFreeHeap() > 80000) {
    tft.loadFont(ROBOTO_FONT12_NAME, LittleFS);
    fontLoaded = true;
    tft.setTextColor(TFT_LIGHTGREY);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(weatherData.description, descX, descY);
    tft.setCursor(20, 120);
    tft.print(tr(TR_HUMIDITY));  // Font VLW obsĹ‚uguje polskie znaki
    tft.setCursor(20, 144);
    tft.print(tr(TR_PRESSURE));
    tft.setCursor(20, 168);
    tft.print(tr(TR_WIND));
    tft.unloadFont();
  } else {
    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY);
    String descAscii = sanitizePolishToAscii(weatherData.description);
    int fallbackX = descX - (descAscii.length() * 3);
    tft.setCursor(fallbackX, descY);
    tft.print(descAscii);
    tft.setTextSize(1);
    tft.setTextColor(TFT_DARKGREY);
    tft.setCursor(20, 120);
    tft.print(sanitizePolishToAscii(String(tr(TR_HUMIDITY))));
    tft.setCursor(20, 145);
    tft.print(sanitizePolishToAscii(String(tr(TR_PRESSURE))));
    tft.setCursor(20, 170);
    tft.print(sanitizePolishToAscii(String(tr(TR_WIND))));
  }

  // 2. TEMPERATURA (Lewa strona) - wiÄ™ksza dla 480px
  tft.setTextSize(2);
  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(20, 45);
  tft.print(sanitizePolishToAscii(String(tr(TR_TEMPERATURE))));

  tft.setTextSize(6); // WiÄ™ksza temperatura
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(20, 70);
  tft.print(String(weatherData.tempC, 1));
  tft.setTextSize(3);
  tft.print(" C");

  // 3. PARAMETRY SZCZEGĂ“ĹOWE - dostosowane do 480px
  int startY = 120;
  int valueX = 180; // WiÄ™cej miejsca na wartoĹ›ci
  
  // WILGOTNOĹšÄ†
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(valueX, startY);
  tft.print(String(weatherData.humidity) + "%");

  // CIĹšNIENIE
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(valueX, startY + 30);
  tft.print(String(weatherData.pressure) + " hPa");

  // WIATR
  tft.setTextSize(2);
  tft.setTextColor(getWindColor(weatherData.windMs));
  tft.setCursor(valueX, startY + 60);
  tft.print(String(weatherData.windMs, 1) + " m/s");

  // PM2.5 i PM10 - lepsze rozmieszczenie dla 480px (przesuniÄ™te bardziej w prawo)
  tft.setTextSize(2);
  
  // PM2.5
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(300, startY);
  tft.print("PM2.5: ");
  uint16_t pm25Color = getPM25Color(weatherData.pm25);
  tft.setTextColor(pm25Color);
  tft.print(String(weatherData.pm25, 1));
  
  // PM10
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(300, startY + 30);
  tft.print("PM10: ");
  uint16_t pm10Color = getPM10Color(weatherData.pm10);
  tft.setTextColor(pm10Color);
  tft.print(String(weatherData.pm10, 1));

}

static void drawWeatherDetailIconCell(int x, int y, int weatherId, const String &iconCode) {
  bool isNight = false;
  if (iconCode.endsWith("n")) {
    isNight = true;
  } else if (iconCode.endsWith("d")) {
    isNight = false;
  } else {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 1)) {
      isNight = (timeinfo.tm_hour < 6 || timeinfo.tm_hour >= 18);
    }
  }

  String iconFile = weatherIconPathForId(weatherId, isNight);
  if (!drawBmpFromFS(iconFile, x - 25, y - 25)) {
    tft.drawCircle(x, y, 10, TFT_LIGHTGREY);
  }
}

static void buildWeatherDetailHeaders(String headers[WEATHER_DETAIL_COLS]) {
  headers[0] = (tftLanguage == TFT_LANG_EN) ? "+3h" : "+3godz";
  headers[1] = (tftLanguage == TFT_LANG_EN) ? "+6h" : "+6godz";

  struct tm timeinfo;
  if (getTimeWithTimezone(&timeinfo)) {
    static const char *daysPl[7] = {"Nd", "Pn", "Wt", "Sr", "Czw", "Pt", "Sob"};
    static const char *daysEn[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const char **days = (tftLanguage == TFT_LANG_EN) ? daysEn : daysPl;

    for (uint8_t i = 0; i < 3; i++) {
      int dayIdx = (timeinfo.tm_wday + 1 + i) % 7;
      headers[i + 2] = String(days[dayIdx]);
    }
  } else {
    headers[2] = "D+1";
    headers[3] = "D+2";
    headers[4] = "D+3";
  }
}

void drawWeatherDetailPage() {
  const int bodyTop = 32;
  const int bodyBottom = 220;
  const int bodyLeft = 2;
  const int bodyRight = 478;  // Dla wyĹ›wietlacza 480px
  const int colCount = WeatherData::DETAIL_COLS;
  const int colW = (bodyRight - bodyLeft + 1) / colCount;

  tft.fillRect(0, bodyTop, 480, bodyBottom - bodyTop, TFT_BLACK);

  if (!weatherData.valid) {
    tft.setTextColor(TFT_RED);
    tft.setTextSize(2);
    tft.setCursor(12, 96);
    tft.print(weatherData.lastError.length() ? weatherData.lastError : tr(TR_NO_DATA));
    return;
  }

  String headers[WeatherData::DETAIL_COLS];
  buildWeatherDetailHeaders(headers);

  const int yHeader = 38;
  const int yTemp = 78;
  const int yNight = 98;
  const int yHum = 121;
  const int yWind = 152;
  const int yIcon = 192;

  tft.fillRect(bodyLeft, bodyTop + 2, bodyRight - bodyLeft + 1, 31, TFT_TABLE_ALT_ROW_COLOR);

  for (int i = 1; i < colCount; i++) {
    int vx = bodyLeft + i * colW;
    tft.drawFastVLine(vx, bodyTop + 2, bodyBottom - bodyTop - 4, TFT_DARKGREY);
  }

  for (int i = 0; i < colCount; i++) {
    int colX = bodyLeft + i * colW;
    int cx = colX + (colW / 2);

    String hdr = sanitizePolishToAscii(headers[i]);
    tft.setTextSize(2);
    int hdrW = hdr.length() * 12;
    tft.setTextColor(TFT_LIGHTGREY);
    tft.setCursor(cx - hdrW / 2, yHeader);
    tft.print(hdr);

    tft.setTextSize(1);

    if (!weatherData.detailValid[i]) {
      tft.setTextColor(TFT_DARKGREY);
      tft.setCursor(cx - 6, yTemp);
      tft.print("--");
      tft.setCursor(cx - 6, yNight);
      tft.print("--");
      tft.setCursor(cx - 6, yHum);
      tft.print("--");
      tft.setCursor(cx - 6, yWind);
      tft.print("--");
      continue;
    }

    String tempText = String(weatherData.detailTempC[i], 1);
    String humText = String(weatherData.detailHumidity[i]) + "%";
    String windText = String(weatherData.detailWindMs[i], 1) + "m/s";

    tft.setTextSize(2);
    tft.setTextColor(TFT_CYAN);
    int tempX = cx - ((int)tempText.length() * 6);
    tft.setCursor(tempX, yTemp);
    tft.print(tempText);
    tft.drawCircle(tempX + ((int)tempText.length() * 12) + 3, yTemp + 3, 2, TFT_CYAN);

    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY);
    String nightText = "--";
    if (i >= 2 && i <= 3 && weatherData.nightTempValid[i - 2]) {
      nightText = String(weatherData.nightTempC[i - 2], 1) + "C";
    }
    tft.setCursor(cx - ((int)nightText.length() * 3), yNight);
    tft.print(nightText);

    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(cx - ((int)humText.length() * 6), yHum);
    tft.print(humText);

    tft.setTextSize(1);
    tft.setTextColor(getWindColor(weatherData.detailWindMs[i]));
    tft.setCursor(cx - ((int)windText.length() * 3), yWind);
    tft.print(windText);

    drawWeatherDetailIconCell(cx, yIcon, weatherData.detailWeatherId[i], weatherData.detailIconCode[i]);
  }
}

static void drawWeatherFooterArea(ScreenType screenId) {
  tft.fillRect(0, 220, 480, 20, TFT_BLACK);

  if (isTableNavFooterVisible(screenId)) {
    drawSwitchScreenFooter();
    return;
  }

  // WIZUALIZACJA JAKOĹšCI POWIETRZA (AQI) - nad napisem lokalizacji
  if (weatherData.valid) {
    const int barY = 222; // Pozycja paska AQI (nad tekstem lokalizacji)
    const int barWidth = 100;
    const int barHeight = 6;
    const int barX = 240 - (barWidth / 2); // WyĹ›rodkowane

    // Kolor na podstawie PM2.5 (gorsze zanieczyszczenie decyduje)
    uint16_t aqiColor = getPM25Color(weatherData.pm25);
    String aqiLabel = "AQI: ";
    if (weatherData.pm25 <= 12) aqiLabel += "DOBRA";
    else if (weatherData.pm25 <= 35) aqiLabel += "UMIARK.";
    else if (weatherData.pm25 <= 55) aqiLabel += "NIEZDROWA";
    else aqiLabel += "SZKODLIWA";

    // Rysuj pasek jakoĹ›ci powietrza
    tft.fillRect(barX, barY, barWidth, barHeight, TFT_DARKGREY);
    tft.fillRect(barX + 1, barY + 1, barWidth - 2, barHeight - 2, aqiColor);

    // Etykieta AQI po lewej stronie paska
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(5, barY);
    tft.print(aqiLabel);

    // WartoĹ›ci PM2.5/PM10 po prawej stronie (przesuniÄ™te bardziej w prawo)
    tft.setTextColor(getPM25Color(weatherData.pm25));
    tft.setCursor(365, barY);
    tft.print("PM2.5:" + String(weatherData.pm25, 0));
    tft.setTextColor(getPM10Color(weatherData.pm10));
    tft.setCursor(435, barY);
    tft.print("PM10:" + String(weatherData.pm10, 0));
  }

  // Tekst lokalizacji (niĹĽej, y=230 zamiast 226 aby zrobiÄ‡ miejsce na AQI)
  String cityLabel = weatherData.cityName;
  cityLabel.trim();
  if (cityLabel.length() == 0) {
    cityLabel = "--";
  }
  cityLabel = sanitizePolishToAscii(cityLabel);

  const String prefix = (tftLanguage == TFT_LANG_EN)
                        ? "Weather for location: "
                        : "Pogoda dla lokalizacji: ";
  const int maxChars = 52;
  int maxCityChars = maxChars - (int)prefix.length();
  if (maxCityChars < 3) {
    maxCityChars = 3;
  }
  if ((int)cityLabel.length() > maxCityChars) {
    cityLabel = cityLabel.substring(0, maxCityChars - 3) + "...";
  }
  String footerLabel = prefix + cityLabel;

  tft.setTextColor(0x52AA);
  tft.setTextSize(1);
  int footerWidth = footerLabel.length() * 6;
  int footerX = (480 - footerWidth) / 2;
  if (footerX < 2) {
    footerX = 2;
  }
  tft.setCursor(footerX, 230);
  tft.print(footerLabel);
}

void updateScreen5Data() {
  if (!tftInitialized ||
      (currentScreen != SCREEN_WEATHER_DSP && currentScreen != SCREEN_WEATHER_FORECAST) ||
      inMenu) {
    return;
  }

  // WyĹ‚Ä…czono - zegar rysowany przez updateWeatherClock
  // updateScreen5Clock();

  static uint32_t lastWeatherSig = 0;
  static uint32_t lastForecastSig = 0;
  uint32_t currentSig = computeScreen5Signature();

  if (currentScreen == SCREEN_WEATHER_FORECAST) {
    if (currentSig == lastForecastSig) {
      return;
    }
    lastForecastSig = currentSig;
    drawWeatherDetailPage();
    drawWeatherFooterArea(SCREEN_WEATHER_FORECAST);
    return;
  }

  if (currentSig == lastWeatherSig) {
    return;
  }
  lastWeatherSig = currentSig;

  drawWeatherBody();
  drawWeatherFooterArea(SCREEN_WEATHER_DSP);
}

void drawWeather() {
  // ekran czysci drawScreen (wspolny styl ISS)




  // Ikona WiFi w gĂłrnym prawym rogu

  // ZEGAR - rysowany osobno przez updateWeatherClock()

  drawWeatherBody();
  drawWeatherFooterArea(SCREEN_WEATHER_DSP);
}

void drawWeatherForecast() {
  // ekran czysci drawScreen (wspolny styl ISS)




  // Ikona WiFi w gĂłrnym prawym rogu

  // ZEGAR - rysowany osobno przez updateWeatherForecastClock()

  drawWeatherDetailPage();
  drawWeatherFooterArea(SCREEN_WEATHER_FORECAST);
}

void setupMatrix() {
  for (int i = 0; i < numDrops; i++) {
    drops[i].x = i * MATRIX_COL_SPACING; // Rozstawienie kolumn
    resetMatrixDropRandom(i);
  }
  matrixInitialized = true;
}

void drawMatrixBackground(int bodyTop, int bodyBottom) {
  const int charStep = 8; // wysokoĹ›Ä‡ znaku przy setTextSize(1)
  const int charW = 6;    // szerokoĹ›Ä‡ znaku przy setTextSize(1)
  bool anyIntroActive = false;
  for (int i = 0; i < numDrops; i++) {
    int drawX = drops[i].x;
    int drawY = bodyTop + drops[i].y;
    bool dropIntroActive = (matrixIntroActive && drops[i].introActive);

    if (matrixIntroActive && !dropIntroActive && !drops[i].introParticipates) {
      continue;
    }

    if (dropIntroActive) {
      unsigned long introElapsed = millis() - matrixIntroStartMs;
      if (introElapsed >= (unsigned long)drops[i].introDelayMs) {
        unsigned long activeMs = introElapsed;
        float relY = (float)drops[i].introStartY +
                     ((float)((int)activeMs - drops[i].introDelayMs) * drops[i].introSpeedPxPerMs);
        drawY = bodyTop + (int)relY;
      } else {
        drawY = bodyTop + drops[i].introStartY;
      }

      int bodyRelY = drawY - bodyTop;
      if (bodyRelY > (bodyBottom - bodyTop)) {
        drops[i].introActive = false;
        dropIntroActive = false;
        resetMatrixDropRandom(i);
        drawY = bodyTop + drops[i].y;
      } else {
        anyIntroActive = true;
      }
    }

    if (drawX < 0 || drawX > (480 - 6)) {
      if (!dropIntroActive) {
        drops[i].y += drops[i].speed;
        if (drops[i].y > (bodyBottom - bodyTop)) {
          resetMatrixDropRandom(i);
        }
      }
      continue;
    }
    tft.setTextSize(1);
    for (int j = 0; j < drops[i].len; j++) {
      int charY = drawY + (j * charStep);
      if (charY >= bodyTop && charY < bodyBottom) {
        // Blokuj znak tĹ‚a, jeĹ›li jego prostokÄ…t przecina obszar maski zegara.
        if (drawX < (clockMaskX + clockMaskW) && (drawX + charW) > clockMaskX &&
            charY < (clockMaskY + clockMaskH) && (charY + charStep) > clockMaskY) {
          continue;
        }
        int denom = (drops[i].len > 1) ? (drops[i].len - 1) : 1;
        uint8_t t = (uint8_t)((255 * j) / denom); // 0 (ciemno) -> 255 (jasno)
        // NajniĹĽsza litera (gĹ‚owa) ma byÄ‡ wyraĹşnie jaĹ›niejsza od drugiej.
        if (j == drops[i].len - 1) {
          t = 255;
        } else if (drops[i].len > 1 && j == drops[i].len - 2) {
          t = min<uint8_t>(t, 135);
        }
        uint16_t color = (j == drops[i].len - 1)
               ? TFT_WHITE
               : lerpColor565(TFT_BLACK, MATRIX_BRIGHTGREEN, t);
        tft.setTextColor(color);
        tft.setCursor(drawX, charY);
        char ch = (j == drops[i].len - 1) ? drops[i].headChar : (char)random(33, 126);
        tft.print(ch);
      }
    }

    if (!dropIntroActive) {
      drops[i].y += drops[i].speed;

      // Reset kropli, gdy wypadnie poza obszar
      if (drops[i].y > (bodyBottom - bodyTop)) {
        resetMatrixDropRandom(i);
      }
    }
  }

  matrixIntroActive = anyIntroActive;
}

void clearMatrixArea(int bodyTop, int bodyBottom) {
  int bodyH = bodyBottom - bodyTop;
  if (clockMaskW <= 0 || clockMaskH <= 0) {
    tft.fillRect(0, bodyTop, SCREEN10_WIDTH, bodyH, TFT_BLACK);
    return;
  }

  int leftW = clockMaskX;
  int rightX = clockMaskX + clockMaskW;
  int rightW = SCREEN10_WIDTH - rightX;
  int topH = clockMaskY - bodyTop;
  int bottomY = clockMaskY + clockMaskH;
  int bottomH = bodyBottom - bottomY;

  if (topH > 0) {
    tft.fillRect(0, bodyTop, SCREEN10_WIDTH, topH, TFT_BLACK);
  }
  if (bottomH > 0) {
    tft.fillRect(0, bottomY, SCREEN10_WIDTH, bottomH, TFT_BLACK);
  }
  if (leftW > 0) {
    tft.fillRect(0, clockMaskY, leftW, clockMaskH, TFT_BLACK);
  }
  if (rightW > 0) {
    tft.fillRect(rightX, clockMaskY, rightW, clockMaskH, TFT_BLACK);
  }
}

void drawMatrixStatic() {
  //tft.fillRect(0, 0, 480, SCREEN10_HEADER_H, TFT_RADIO_ORANGE);
  //tft.setTextColor(TFT_BLACK);
  //tft.setTextSize(2);
 // tft.setCursor(10, 8);
  //tft.print("ZEGAR");

  // StrzaĂ„Ä…Ă˘â‚¬Ĺˇki nawigacyjne na dole ekranu
  //tft.fillTriangle(10, 230, 20, 222, 20, 238, TFT_RADIO_ORANGE);
  //tft.fillTriangle(310, 230, 300, 222, 300, 238, TFT_RADIO_ORANGE);
  //tft.setTextColor(0x52AA); // Ciemny szary
  //tft.setTextSize(1);
  //tft.setCursor(125, 226);
  //tft.print("SWITCH SCREEN");
}

void drawMatrixFrame() {
  unsigned long now = millis();
  static unsigned long lastMatrixUpdateMs = 0;
  
  // Aktualizuj matrix co 50ms dla pĹ‚ynnego efektu
  if (now - lastMatrixUpdateMs >= 50) {
    lastMatrixUpdateMs = now;
    
    // Rysuj tĹ‚o matrix na caĹ‚ym ekranie
    drawMatrixBackground(0, SCREEN10_HEIGHT);
  }
  
  // Zegar w stylu Matrix - wiekszy i wysrodkowany.
  String timeLocal = getTimezoneTimeString("%H:%M:%S", 9);
  const int MATRIX_TEXT_SIZE = 4;
  // Szerokosc liczymy z realnych metryk fontu (wbudowany font GLCD = 6 px/znak,
  // nie 8 px), inaczej zegar i data byly przesuniete w lewo.
  tft.setTextSize(MATRIX_TEXT_SIZE);
  const int timeWidth = tft.textWidth(timeLocal);
  const int timeCharH = 8 * MATRIX_TEXT_SIZE;
  const int timeHeight = timeCharH;
  const int timeX = (SCREEN10_WIDTH - timeWidth) / 2;
  const int timeY = (SCREEN10_HEIGHT - timeHeight) / 2; // Wysrodkowany
  
  // Rysuj zegar tylko gdy siÄ™ zmieniĹ‚
  if (timeLocal != lastClockText) {
    // WyczyĹ›Ä‡ obszar zegara przed narysowaniem nowego (usuĹ„ poprzednie sekundy)
    tft.fillRect(timeX - 5, timeY - 5, timeWidth + 10, timeHeight + 10, TFT_BLACK);
    
    // Efekt Ĺ›wiecÄ…cy - rysuj z cieniem
    tft.setTextSize(MATRIX_TEXT_SIZE);
    
    // CieĹ„/aura wokĂłĹ‚ tekstu (poĹ›wiata)
    for (int offset = 3; offset >= 1; offset--) {
      uint16_t glowColor = (offset == 1) ? MATRIX_BRIGHTGREEN : 
                          (offset == 2) ? 0x03E0 : 0x01E0;
      tft.setTextColor(glowColor);
      tft.setCursor(timeX + offset, timeY + offset);
      tft.print(timeLocal);
      tft.setCursor(timeX - offset, timeY + offset);
      tft.print(timeLocal);
      tft.setCursor(timeX + offset, timeY - offset);
      tft.print(timeLocal);
      tft.setCursor(timeX - offset, timeY - offset);
      tft.print(timeLocal);
    }
    
    // GĹ‚Ăłwny tekst - jasnozielony
    tft.setTextColor(MATRIX_BRIGHTGREEN);
    tft.setCursor(timeX, timeY);
    tft.print(timeLocal);
    
    // Data pod zegarem w stylu Matrix
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 1)) {
      char dateStr[20];
      strftime(dateStr, sizeof(dateStr), "%d.%m.%Y", &timeinfo);
      
      const int dateTextSize = 2;
      tft.setTextSize(dateTextSize);
      const int dateWidth = tft.textWidth(dateStr);
      const int dateX = (SCREEN10_WIDTH - dateWidth) / 2;
      const int dateY = timeY + timeHeight + 20;
      const int dateH = 8 * dateTextSize;

      // Wyczysc obszar daty (bez tego zostawaly resztki przy zmianie tekstu).
      tft.fillRect(dateX - 5, dateY - 5, dateWidth + 10, dateH + 10, TFT_BLACK);
      
      // CieĹ„ daty
      tft.setTextSize(dateTextSize);
      tft.setTextColor(0x03E0);
      tft.setCursor(dateX + 1, dateY + 1);
      tft.print(dateStr);
      
      // GĹ‚Ăłwna data
      tft.setTextColor(MATRIX_BRIGHTGREEN);
      tft.setCursor(dateX, dateY);
      tft.print(dateStr);
    }
    
    lastClockText = timeLocal;
  }
}

// Funkcja rysujÄ…ca elegancki analogowy zegar (wygaszacz ekranu)
void drawAnalogClock() {
  unsigned long now = millis();
  
  // Aktualizuj co ~100ms (10 FPS - wystarczy dla zegara analogowego)
  if (now - lastBounceClockUpdateMs >= 100) {
    lastBounceClockUpdateMs = now;
    
    // Pobierz aktualny czas
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 1)) return;
    
    int hour = timeinfo.tm_hour;
    int minute = timeinfo.tm_min;
    int second = timeinfo.tm_sec;
    int day = timeinfo.tm_mday;
    int month = timeinfo.tm_mon + 1;
    int year = timeinfo.tm_year + 1900;
    
    // Ĺšrodek zegara
    const int cx = 240;
    const int cy = 145;  // Nieco wyĹĽej aby zrobiÄ‡ miejsce na datÄ™
    const int r = 130;   // PromieĹ„ tarczy
    
    // Gradientowe tĹ‚o - elegancki niebieski
    for (int y = 0; y < 320; y++) {
      uint8_t intensity = 15 + (y * 25) / 320;  // Gradient od gĂłry do doĹ‚u
      uint16_t bgColor = ((intensity & 0x1F) << 11) | ((intensity & 0x3F) << 5) | (intensity & 0x1F);
      tft.drawFastHLine(0, y, 480, bgColor);
    }
    
    // ZewnÄ™trzna obramowanie tarczy - metaliczne zĹ‚oto/srebro
    for (int i = 8; i >= 0; i--) {
      uint16_t ringColor;
      if (i == 0) ringColor = 0xFFE0;      // ZĹ‚oty zewnÄ™trzny
      else if (i < 3) ringColor = 0xC618;  // Srebrny
      else ringColor = 0x4A49;             // Ciemny metal
      tft.drawCircle(cx, cy, r + i, ringColor);
    }
    
    // GĹ‚Ăłwne tĹ‚o tarczy - gĹ‚Ä™boki niebieski
    tft.fillCircle(cx, cy, r, 0x0015);
    
    // WewnÄ™trzny pierĹ›cieĹ„ dekoracyjny
    for (int i = 3; i >= 0; i--) {
      tft.drawCircle(cx, cy, r - 10 - i, 0xC618);  // Srebrny pierĹ›cieĹ„
    }
    
    // Drugie wewnÄ™trzne tĹ‚o
    tft.fillCircle(cx, cy, r - 14, 0x000C);
    
    // Cyfry rzymskie na tarczy
    const char* romanNums[] = {"", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII"};
    for (int i = 1; i <= 12; i++) {
      float angle = (i * 30 - 90) * PI / 180;
      int numX = cx + (r - 25) * cos(angle);
      int numY = cy + (r - 25) * sin(angle);
      
      tft.setTextSize(1);
      tft.setTextColor(0xC618);
      int textW = strlen(romanNums[i]) * 6;
      tft.setCursor(numX - textW/2, numY - 4);
      tft.print(romanNums[i]);
    }
    
    // Kreski godzinowe
    for (int i = 0; i < 60; i++) {
      float angle = i * 6 * PI / 180;
      int len = (i % 5 == 0) ? 12 : 6;
      uint16_t color = (i % 5 == 0) ? TFT_WHITE : 0x8410;
      int x1 = cx + (r - 15) * cos(angle);
      int y1 = cy + (r - 15) * sin(angle);
      int x2 = cx + (r - 15 - len) * cos(angle);
      int y2 = cy + (r - 15 - len) * sin(angle);
      tft.drawLine(x1, y1, x2, y2, color);
    }
    
    // Oblicz kÄ…ty wskazĂłwek
    float hourAngle = (timeinfo.tm_hour % 12) * 30 + timeinfo.tm_min * 0.5 - 90;
    float minAngle = timeinfo.tm_min * 6 - 90;
    float secAngle = timeinfo.tm_sec * 6 - 90;
    
    hourAngle *= PI / 180;
    minAngle *= PI / 180;
    secAngle *= PI / 180;
    
    // WskazĂłwka godzinowa
    int hx = cx + (r * 0.5) * cos(hourAngle);
    int hy = cy + (r * 0.5) * sin(hourAngle);
    tft.drawLine(cx, cy, hx, hy, TFT_WHITE);
    tft.drawLine(cx+1, cy, hx+1, hy, TFT_WHITE);
    tft.drawLine(cx, cy+1, hx, hy+1, TFT_WHITE);
    int mLen = r * 0.75;
    int mX = cx + mLen * cos(minAngle);
    int mY = cy + mLen * sin(minAngle);
    tft.drawLine(cx, cy, mX, mY, 0xC618);
    tft.drawLine(cx-1, cy, mX-1, mY, 0xC618);
    tft.drawLine(cx+1, cy, mX+1, mY, 0xC618);
    tft.drawLine(cx, cy-1, mX, mY-1, 0xC618);
    tft.drawLine(cx, cy+1, mX, mY+1, 0xC618);
    tft.drawLine(cx+2, cy+2, mX+2, mY+2, 0x0005);
    
    // WskazĂłwka sekundowa (elegancka, czerwona z kontrastem)
    int sLen = r * 0.88;
    int sX = cx + sLen * cos(secAngle);
    int sY = cy + sLen * sin(secAngle);
    tft.drawLine(cx, cy, sX, sY, TFT_RED);
    tft.drawLine(cx-1, cy, sX-1, sY, 0xF800);
    tft.drawLine(cx+1, cy, sX+1, sY, 0xF800);
    // Koniec wskazĂłwki (maĹ‚a kropka)
    tft.fillCircle(sX, sY, 4, TFT_RED);
    
    // Ĺšrodek zegara - elegancki medalion
    tft.fillCircle(cx, cy, 12, 0xC618);   // Srebrna obwĂłdka
    tft.fillCircle(cx, cy, 10, 0xFFE0);   // ZĹ‚ote wypeĹ‚nienie
    tft.fillCircle(cx, cy, 6, 0x0015);    // Niebieskie centrum
    tft.fillCircle(cx, cy, 3, TFT_RED);   // Czerwona kropka
    
    // Napis marki na gĂłrze tarczy
    tft.setTextSize(1);
    tft.setTextColor(0xC618);
    tft.setCursor(cx - 25, cy - r + 35);
    tft.print("HAM RADIO");
    
    // Data na dole ekranu (pod zegarem)
    char dateStr[32];
    snprintf(dateStr, sizeof(dateStr), "%02d.%02d.%d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
    tft.setTextSize(2);
    int dateW = tft.textWidth(dateStr);
    // CieĹ„ daty
    tft.setTextColor(0x0005);
    tft.setCursor(cx - dateW/2 + 2, cy + r + 25 + 2);
    tft.print(dateStr);
    // GĹ‚Ăłwna data - zĹ‚ota
    tft.setTextColor(0xFFE0);
    tft.setCursor(cx - dateW/2, cy + r + 25);
    tft.print(dateStr);
    
    // Dodatkowy napis "AUTOMATIC"
    tft.setTextSize(1);
    tft.setTextColor(0x8410);
    tft.setCursor(cx - 30, cy - r + 50);
    tft.print("AUTOMATIC");
  }
}

// Funkcja rysujÄ…ca cyfrowy zegar odbijajÄ…cy siÄ™ (stary bouncing clock)
void drawDigitalClock() {
  unsigned long now = millis();
  static String lastTimeStr = "";
  static bool firstDraw = true;
  
  // Aktualizuj co ~100ms
  if (now - lastBounceClockUpdateMs >= 100) {
    lastBounceClockUpdateMs = now;
    
    // Pobierz aktualny czas
    String timeStr = getTimezoneTimeString("%H:%M:%S", 9);
    
    // Rysuj tylko gdy siÄ™ zmieniĹ‚ lub pierwszy raz
    if (firstDraw || timeStr != lastTimeStr) {
      firstDraw = false;
      lastTimeStr = timeStr;
      
      // WyczyĹ›Ä‡ ekran tylko raz
      if (firstDraw) {
        tft.fillScreen(TFT_BLACK);
      }
      
      // Oblicz pozycjÄ™ - wyĹ›rodkowany
      int textW = timeStr.length() * BOUNCE_CLOCK_CHAR_W;
      int centerX = SCREEN10_WIDTH / 2;
      int centerY = SCREEN10_HEIGHT / 2;
      int x = centerX - textW / 2;
      int y = centerY - BOUNCE_CLOCK_CHAR_H / 2;
      
      // WyczyĹ›Ä‡ obszar zegara (prostokÄ…t w centrum)
      tft.fillRect(x - 20, y - 20, textW + 40, BOUNCE_CLOCK_CHAR_H + 40, TFT_BLACK);
      
      // Efekt neonowy - cieĹ„
      tft.setTextSize(BOUNCE_CLOCK_TEXT_SIZE);
      tft.setTextColor(0x0410); // Ciemny niebieski cieĹ„
      tft.setCursor(x + 4, y + 4);
      tft.print(timeStr);
      
      // GĹ‚Ăłwny tekst - cyfrowy zielony
      tft.setTextColor(0x07E0); // Jasny zielony
      tft.setCursor(x, y);
      tft.print(timeStr);
      
      // Dodaj ramkÄ™ wokĂłĹ‚ zegara
      tft.drawRect(x - 10, y - 10, textW + 20, BOUNCE_CLOCK_CHAR_H + 20, 0x07E0);
      tft.drawRect(x - 8, y - 8, textW + 16, BOUNCE_CLOCK_CHAR_H + 16, 0x0410);
      
      // Data pod zegarem
      struct tm timeinfo;
      if (getLocalTime(&timeinfo, 1)) {
        char dateStr[20];
        strftime(dateStr, sizeof(dateStr), "%d-%m-%Y", &timeinfo);
        tft.setTextSize(2);
        tft.setTextColor(0x07E0);
        int dateW = strlen(dateStr) * 12;
        tft.setCursor(centerX - dateW/2, y + BOUNCE_CLOCK_CHAR_H + 25);
        tft.print(dateStr);
      }
    }
  }
}

// GĹ‚Ăłwna funkcja wygaszacza - wybiera odpowiedni typ
void drawScreenSaver() {
  switch (screenSaverType) {
    case SAVER_ANALOG_CLOCK:
      drawAnalogClock();
      break;
    case SAVER_DIGITAL_CLOCK:
      drawDigitalClock();
      break;
    case SAVER_MATRIX:
    default:
      drawMatrixFrame();
      break;
  }
}

// Ekran 10: Zegar z animowanym tĹ‚em
void drawMatrixClock() {
  if (!matrixInitialized) {
    setupMatrix();
  }
  prepareMatrixIntro();
  drawMatrixStatic();
  screen10NeedsRedraw = false;
  lastScreen10UpdateMs = millis();
  lastMatrixUpdateMs = 0;
  clockNeedsRedraw = true;
  drawMatrixFrame();
}

void updateScreen10() {
  if (!tftInitialized || currentScreen != SCREEN_MATRIX_CLOCK || inMenu) {
    return;
  }

  unsigned long now = millis();
  if (now - lastScreen10UpdateMs < MATRIX_UPDATE_INTERVAL_MS) {
    return;
  }
  lastScreen10UpdateMs = now;

  if (screen10NeedsRedraw) {
    drawMatrixStatic();
    screen10NeedsRedraw = false;
    clockNeedsRedraw = true;
  }
  drawMatrixFrame();
}

// ========== Ekran 11: Informacje systemowe ==========
// Dashboard telemetrii ESP32-S3: sprzet/procesor, pamiec/siec, wykres
// temperatury CPU oraz log zdarzen. Metryki i wykres odswiezane co 1 s.

#define SYSINFO_CHART_POINTS 45

static bool sysInfoNeedsRedraw = true;
static unsigned long sysInfoLastUpdateMs = 0;
static float sysInfoTempHistory[SYSINFO_CHART_POINTS];
static int sysInfoHistoryCount = 0;
static unsigned long sysInfoBootMs = 0;
static uint32_t sysInfoBootFreeHeap = 0;

// Paleta (RGB565) - zgodna z kolorystyka pozostalych ekranow.
static const uint16_t SYS_COL_BG     = 0x0000; // czarne tlo
static const uint16_t SYS_COL_CARD   = 0x10A2; // ciemny granat
static const uint16_t SYS_COL_TEXT   = 0xFFFF; // bialy
static const uint16_t SYS_COL_MUTED  = 0x8C71; // szary
static const uint16_t SYS_COL_ACCENT = 0x3D7F; // blekit
static const uint16_t SYS_COL_GREEN  = 0x3666; // zielony
static const uint16_t SYS_COL_YELLOW = 0xFE80; // zolty
static const uint16_t SYS_COL_RED    = 0xF800;
static const uint16_t SYS_COL_GRID   = 0x2124;

static void sysInfoTempColor(float t, uint16_t &col) {
  if (t >= 65.0f) col = SYS_COL_RED;
  else if (t >= 55.0f) col = SYS_COL_YELLOW;
  else col = SYS_COL_GREEN;
}

// Statyczny szkielet: naglowek + podpisy kart.
static void drawSystemInfoStatic() {
  tft.fillScreen(SYS_COL_BG);

  tft.fillRect(10, 8, 460, 26, SYS_COL_CARD);
  tft.setTextSize(1);
  tft.setTextColor(SYS_COL_TEXT, SYS_COL_CARD);
  tft.setCursor(18, 17);
  tft.print("ESP32-S3 MONITOR // N16R8 + ILI9488 (480x320)");

  tft.fillRect(10, 38, 225, 132, SYS_COL_CARD);
  tft.setTextColor(SYS_COL_ACCENT, SYS_COL_CARD);
  tft.setCursor(18, 48); tft.print("HARDWARE & PROCESSOR");
  tft.setTextColor(SYS_COL_MUTED, SYS_COL_CARD);
  tft.setCursor(18, 70);  tft.print("MCU / Board:");
  tft.setCursor(18, 88);  tft.print("Architecture:");
  tft.setCursor(18, 106); tft.print("Clock Speed:");
  tft.setCursor(18, 124); tft.print("CPU Temp:");
  tft.setCursor(18, 142); tft.print("Flash / PSRAM:");

  tft.fillRect(245, 38, 225, 132, SYS_COL_CARD);
  tft.setTextColor(SYS_COL_ACCENT, SYS_COL_CARD);
  tft.setCursor(253, 48); tft.print("MEMORY & NETWORK (2.4G)");
  tft.setTextColor(SYS_COL_MUTED, SYS_COL_CARD);
  tft.setCursor(253, 70);  tft.print("Free heap:");
  tft.setCursor(253, 88);  tft.print("PSRAM free:");
  tft.setCursor(253, 106); tft.print("Wi-Fi Status:");
  tft.setCursor(253, 124); tft.print("Signal RSSI:");
  tft.setCursor(253, 142); tft.print("IP Address:");

  tft.fillRect(10, 174, 235, 136, SYS_COL_CARD);
  tft.setTextColor(SYS_COL_ACCENT, SYS_COL_CARD);
  tft.setCursor(18, 184); tft.print("CPU TEMP HISTORY");

  tft.fillRect(255, 174, 215, 136, SYS_COL_CARD);
  tft.setTextColor(SYS_COL_ACCENT, SYS_COL_CARD);
  tft.setCursor(263, 184); tft.print("SYSTEM LOGS");
}

// Wykres temperatury CPU (os 40..60 C).
static void sysInfoDrawChart() {
  tft.fillRect(18, 202, 220, 100, SYS_COL_CARD);
  tft.drawFastHLine(18, 222, 220, SYS_COL_GRID);
  tft.drawFastHLine(18, 252, 220, SYS_COL_GRID);
  tft.drawFastHLine(18, 282, 220, SYS_COL_GRID);

  if (sysInfoHistoryCount < 2) return;
  const float minT = 40.0f, maxT = 60.0f;
  const int chartX = 20, chartY = 295, chartW = 210, chartH = 80;
  for (int i = 0; i < sysInfoHistoryCount - 1; i++) {
    int x1 = chartX + (i * chartW) / (SYSINFO_CHART_POINTS - 1);
    int y1 = chartY - (int)(((sysInfoTempHistory[i] - minT) / (maxT - minT)) * chartH);
    int x2 = chartX + ((i + 1) * chartW) / (SYSINFO_CHART_POINTS - 1);
    int y2 = chartY - (int)(((sysInfoTempHistory[i + 1] - minT) / (maxT - minT)) * chartH);
    if (y1 < chartY - chartH) y1 = chartY - chartH;
    if (y1 > chartY) y1 = chartY;
    if (y2 < chartY - chartH) y2 = chartY - chartH;
    if (y2 > chartY) y2 = chartY;
    tft.drawLine(x1, y1, x2, y2, SYS_COL_YELLOW);
  }
}

// Dynamiczne metryki (co 1 s).
static void sysInfoUpdateDynamic(float currentTemp) {
  char b[40];

  // --- Karta 1: Hardware ---
  tft.fillRect(115, 68, 110, 95, SYS_COL_CARD);
  tft.setTextSize(1);
  tft.setTextColor(SYS_COL_TEXT, SYS_COL_CARD);
  tft.setCursor(115, 70);  tft.print("ESP32-S3 N16R8");
  tft.setCursor(115, 88);  tft.print("Xtensa LX7");
  tft.setCursor(115, 106); tft.print("240 MHz");
  uint16_t tcol;
  sysInfoTempColor(currentTemp, tcol);
  tft.setTextColor(tcol, SYS_COL_CARD);
  snprintf(b, sizeof(b), "%.1f C", currentTemp);
  tft.setCursor(115, 124); tft.print(b);
  tft.setTextColor(SYS_COL_TEXT, SYS_COL_CARD);
  tft.setCursor(115, 142); tft.print("16MB / 8MB");

  // --- Karta 2: Pamiec & Siec ---
  tft.fillRect(350, 68, 115, 95, SYS_COL_CARD);
  tft.setTextColor(SYS_COL_GREEN, SYS_COL_CARD);
  snprintf(b, sizeof(b), "%lu KB", (unsigned long)(ESP.getFreeHeap() / 1024));
  tft.setCursor(350, 70);  tft.print(b);
  tft.setTextColor(SYS_COL_ACCENT, SYS_COL_CARD);
  snprintf(b, sizeof(b), "%lu MB", (unsigned long)(ESP.getFreePsram() / (1024 * 1024)));
  tft.setCursor(350, 88);  tft.print(b);

  bool conn = (WiFi.status() == WL_CONNECTED);
  tft.setTextColor(conn ? SYS_COL_GREEN : SYS_COL_RED, SYS_COL_CARD);
  tft.setCursor(350, 106); tft.print(conn ? "Connected" : "Connecting");
  tft.setTextColor(SYS_COL_TEXT, SYS_COL_CARD);
  tft.setCursor(350, 124);
  if (conn) { snprintf(b, sizeof(b), "%d dBm", WiFi.RSSI()); tft.print(b); }
  else tft.print("---");
  tft.setCursor(350, 142);
  tft.print(conn ? WiFi.localIP().toString() : String("0.0.0.0"));

  // --- Karta 3: Wykres ---
  sysInfoDrawChart();

  // --- Karta 4: Logi ---
  tft.fillRect(263, 202, 199, 100, SYS_COL_CARD);
  tft.setTextSize(1);
  tft.setTextColor(SYS_COL_MUTED, SYS_COL_CARD);
  unsigned long up = millis() / 1000UL;
  snprintf(b, sizeof(b), "[%02lu:%02lu] Boot nominal.", up / 60, up % 60);
  tft.setCursor(263, 205); tft.print(b);
  snprintf(b, sizeof(b), "[%02lu:%02lu] WiFi %s.", up / 60, up % 60, conn ? "OK" : "--");
  tft.setCursor(263, 222); tft.print(b);
  snprintf(b, sizeof(b), "[%02lu:%02lu] Chart online.", up / 60, up % 60);
  tft.setCursor(263, 239); tft.print(b);
  tft.setCursor(263, 256);
  tft.setTextColor(SYS_COL_GREEN, SYS_COL_CARD);
  snprintf(b, sizeof(b), "[OK] Temp %.1f C", currentTemp);
  tft.print(b);
}

void drawSystemInfo() {
  sysInfoNeedsRedraw = true;
  sysInfoLastUpdateMs = 0;
  if (sysInfoBootMs == 0) {
    sysInfoBootMs = millis();
    sysInfoBootFreeHeap = ESP.getFreeHeap();
    for (int i = 0; i < SYSINFO_CHART_POINTS; i++) sysInfoTempHistory[i] = 48.5f;
    sysInfoHistoryCount = SYSINFO_CHART_POINTS;
  }
  drawSystemInfoStatic();
  sysInfoNeedsRedraw = false;
  float t = temperatureRead();
  if (isnan(t) || t < 0.0f) t = 48.5f;
  sysInfoUpdateDynamic(t);
}

void updateSystemInfo() {
  unsigned long now = millis();
  if (sysInfoNeedsRedraw) {
    drawSystemInfoStatic();
    sysInfoNeedsRedraw = false;
    sysInfoLastUpdateMs = 0;
  }
  if (now - sysInfoLastUpdateMs < 1000UL) return;
  sysInfoLastUpdateMs = now;

  float t = temperatureRead();
  if (isnan(t) || t < 0.0f) t = sysInfoTempHistory[SYSINFO_CHART_POINTS - 1];
  for (int i = 0; i < SYSINFO_CHART_POINTS - 1; i++) {
    sysInfoTempHistory[i] = sysInfoTempHistory[i + 1];
  }
  sysInfoTempHistory[SYSINFO_CHART_POINTS - 1] = t;
  sysInfoHistoryCount = SYSINFO_CHART_POINTS;
  sysInfoUpdateDynamic(t);
}

// ========== Ekrany pomocnicze ==========

// Ekrany 6-10: Puste (tylko napis)
void drawScreenEmpty(int screenNum) {
  uiHeader((String(tr(TR_PAGE)) + " " + String(screenNum)).c_str(), true);
  tft.setTextDatum(TC_DATUM);
  tft.setTextSize(3);
  tft.setTextColor(UI_COL_TEXT, UI_COL_BG);
  tft.drawString(String(tr(TR_PAGE)) + " " + String(screenNum), 240, 140, 1);
  tft.setTextDatum(TL_DATUM);
  uiFooter();
}

// Rysuj strzaĹ‚ki nawigacyjne < >
void drawNavigationArrows() {
  // Intentionally empty: nawigacja przez dotyk bez elementĂ„â€šĹ‚w UI
}

static volatile bool uiPendingScreen1Redraw = false;
static volatile bool uiPendingScreen2Redraw = false;
static volatile bool uiPendingScreen6Redraw = false;
static volatile bool uiPendingScreen7Redraw = false;
static volatile bool uiPendingAnyScreenRedraw = false;
static volatile uint8_t uiPendingAnyScreenId = SCREEN_HAM_CLOCK;
static portMUX_TYPE uiPendingRedrawMux = portMUX_INITIALIZER_UNLOCKED;

static void requestUiScreenRedraw(uint8_t pendingScreenId) {
  portENTER_CRITICAL(&uiPendingRedrawMux);
  uiPendingAnyScreenId = pendingScreenId;
  uiPendingAnyScreenRedraw = true;
  switch (pendingScreenId) {
    case SCREEN_HAM_CLOCK:
      uiPendingScreen1Redraw = true;
      break;
    case SCREEN_DX_CLUSTER:
      uiPendingScreen2Redraw = true;
      break;
    case SCREEN_APRS_IS:
    case SCREEN_APRS_RADAR:
      uiPendingScreen6Redraw = true;
      break;
    case SCREEN_POTA_CLUSTER:
      uiPendingScreen7Redraw = true;
      break;
    default:
      break;
  }
  portEXIT_CRITICAL(&uiPendingRedrawMux);
}

// Aktualizuj ekran 2 (DX Cluster) - wywoĹ‚ywane gdy przyjdÄ… nowe spoty
void updateScreen2() {
  if (currentScreen == SCREEN_DX_CLUSTER && !inMenu && !aprsAlertScreenActive) {
    requestUiScreenRedraw(SCREEN_DX_CLUSTER);
  }
}

void updateScreen7() {
  if (currentScreen == SCREEN_POTA_CLUSTER && !inMenu && !aprsAlertScreenActive) {
    requestUiScreenRedraw(SCREEN_POTA_CLUSTER);
  }
}

void updateScreen6() {
  if ((currentScreen == SCREEN_APRS_IS || currentScreen == SCREEN_APRS_RADAR) && !inMenu && !aprsAlertScreenActive) {
    requestUiScreenRedraw(currentScreen);
  }
}

// Aktualizuj ekran 1 (Info) - wywoĹ‚ywane gdy zmieni siÄ™ IP
void updateScreen1() {
  if (bootSequenceActive) {
    return;
  }
  if (currentScreen == SCREEN_HAM_CLOCK && !inMenu && !aprsAlertScreenActive) {
    requestUiScreenRedraw(SCREEN_HAM_CLOCK);
  }
}
#endif

void bootLogLine(const String &line) {
  Serial.println(line);
#ifdef ENABLE_TFT_DISPLAY
  if (tftInitialized) {
    tftBootPrintLine(line);
  }
#endif
  if (bootSequenceActive) {
    delay(500);
  }
}

// ========== FUNKCJE POMOCNICZE ==========
static uint16_t readBmp16(fs::File &f) {
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read(); // MSB
  return result;
}

static uint32_t readBmp32(fs::File &f) {
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read();
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read();
  return result;
}

static bool drawBmpFromFS(const String &filename, int16_t x, int16_t y) {
  if (!littleFsReady) {
    return false;
  }
  if ((x >= tft.width()) || (y >= tft.height())) {
    return false;
  }
  if (!LittleFS.exists(filename)) {
    return false;
  }
  fs::File bmpFS = LittleFS.open(filename, "r");
  if (!bmpFS) {
    return false;
  }

  uint32_t seekOffset;
  uint16_t w, h;
  if (readBmp16(bmpFS) != 0x4D42) {
    bmpFS.close();
    return false;
  }

  readBmp32(bmpFS);
  readBmp32(bmpFS);
  seekOffset = readBmp32(bmpFS);
  readBmp32(bmpFS);
  w = readBmp32(bmpFS);
  h = readBmp32(bmpFS);

  // Walidacja wymiarĂłw PRZED alokacjÄ… bufora wiersza - w/h pochodzÄ… wprost
  // z nagĹ‚Ăłwka pliku, bez ĹĽadnej weryfikacji. Bufor poniĹĽej byĹ‚ tablicÄ… o
  // zmiennym rozmiarze (VLA) na STOSIE (uint8_t lineBuffer[w*3+padding]) -
  // uszkodzony albo nieoczekiwanie duĹĽy plik BMP (np. zĹ‚a/nieistniejÄ…ca
  // ikona pogodowa trafiajÄ…ca w "unknown.bmp" o zĹ‚ych wymiarach) mĂłgĹ‚
  // przepeĹ‚niÄ‡ stos i zawiesiÄ‡ urzÄ…dzenie (crash "StoreProhibited" pod
  // adresem 0x0 - dokĹ‚adnie to, co byĹ‚o widoczne w logu). Wszystkie
  // obrazy uĹĽywane w tym projekcie (splash 480x320, ikony 50x50) mieszczÄ…
  // siÄ™ duĹĽo poniĹĽej szerokoĹ›ci ekranu, wiÄ™c to bezpieczny, hojny limit.
  if (w == 0 || h == 0 || w > tft.width() || h > 1000) {
    Serial.printf("[BMP] Odrzucono plik o niewiarygodnych wymiarach: %ux%u (%s)\n", w, h, filename.c_str());
    bmpFS.close();
    return false;
  }

  if (readBmp16(bmpFS) != 1 || readBmp16(bmpFS) != 24 || readBmp32(bmpFS) != 0) {
    bmpFS.close();
    return false;
  }

  y += h - 1;
  bool oldSwap = tft.getSwapBytes();
  tft.setSwapBytes(true);
  bmpFS.seek(seekOffset);

  uint16_t padding = (4 - ((w * 3) & 3)) & 3;
  uint8_t lineBuffer[w * 3 + padding];

  for (uint16_t row = 0; row < h; row++) {
    bmpFS.read(lineBuffer, sizeof(lineBuffer));
    uint8_t *bptr = lineBuffer;
    uint16_t *tptr = (uint16_t *)lineBuffer;
    for (uint16_t col = 0; col < w; col++) {
      uint8_t b = *bptr++;
      uint8_t g = *bptr++;
      uint8_t r = *bptr++;
      *tptr++ = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }
    tft.pushImage(x, y--, w, 1, (uint16_t *)lineBuffer);
  }

  tft.setSwapBytes(oldSwap);
  bmpFS.close();
  return true;
}

static bool drawBmp16FromFS(const String &filename, int16_t x, int16_t y) {
  if (!littleFsReady) {
    return false;
  }
  if ((x >= tft.width()) || (y >= tft.height())) {
    return false;
  }
  if (!LittleFS.exists(filename)) {
    return false;
  }
  fs::File bmpFS = LittleFS.open(filename, "r");
  if (!bmpFS) {
    return false;
  }

  uint32_t seekOffset;
  uint16_t w, h;
  if (readBmp16(bmpFS) != 0x4D42) {
    bmpFS.close();
    return false;
  }

  readBmp32(bmpFS);  // File size
  readBmp32(bmpFS);  // Reserved
  seekOffset = readBmp32(bmpFS);  // Offset to pixel data
  readBmp32(bmpFS);  // DIB header size
  w = readBmp32(bmpFS);  // Width
  h = readBmp32(bmpFS);  // Height

  uint16_t planes = readBmp16(bmpFS);
  uint16_t bpp = readBmp16(bmpFS);
  uint32_t compression = readBmp32(bmpFS);

  // SprawdĹş czy to 16-bit z maskami bitowymi (BI_BITFIELDS = 3)
  if (planes != 1 || bpp != 16 || (compression != 0 && compression != 3)) {
    bmpFS.close();
    return false;
  }

  // JeĹ›li compression == 3, pomiĹ„ maski bitowe (12 bajtĂłw)
  if (compression == 3) {
    readBmp32(bmpFS);
    readBmp32(bmpFS);
    readBmp32(bmpFS);
  }

  // BMP zapisuje od doĹ‚u do gĂłry, wiÄ™c zaczynamy od dolnego wiersza
  y += h - 1;
  bool oldSwap = tft.getSwapBytes();
  tft.setSwapBytes(true);
  bmpFS.seek(seekOffset);

  uint16_t rowSize = ((16 * w + 31) / 32) * 4;  // WyrĂłwnane do 4 bajtĂłw
  uint16_t padding = rowSize - (w * 2);
  
  // UĹĽyj alokacji na stercie zamiast stosu dla lepszego wyrĂłwnania
  uint16_t lineBufferSize = rowSize;
  uint8_t* lineBuffer = (uint8_t*)malloc(lineBufferSize);
  if (!lineBuffer) {
    tft.setSwapBytes(oldSwap);
    bmpFS.close();
    return false;
  }

  for (uint16_t row = 0; row < h; row++) {
    bmpFS.read(lineBuffer, rowSize);
    // Dane sÄ… juĹĽ w RGB565, przekazujemy bezpoĹ›rednio
    tft.pushImage(x, y--, w, 1, (uint16_t *)lineBuffer);
  }

  free(lineBuffer);
  tft.setSwapBytes(oldSwap);
  bmpFS.close();
  return true;
}

// ===================== UNIWERSALNE RYSOWANIE OBRAZU =====================
// Ekran 480x320. Obraz jest skalowany (letterbox) tak, by zmiescil sie w
// calosci i byl wysrodkowany na czarnym tle. Dekodery rysuja wierszami do
// bufora RGB565 (docelowa wysokosc), a nastepnie bufor jest wysylany jednym
// pushImage() do TFT - plynne, bez migotania.

static uint16_t* g_img_rowbuf = nullptr;   // bufor wiersza docelowego (480 px)
static int       g_img_scale = 1;          // aktualny wspolczynnik skalowania
static int       g_img_row_src = -1;       // ostatnio zapisany wiersz zrodla
static int       g_img_dst_h = 320;        // docelowa wysokosc po skalowaniu
static bool      g_img_has_alpha = false;

static uint16_t imgRgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Dekodery (PNG) czesto wywoluja callback per piksel - nie chcemy pushImage
// 480x1 przy kazdym z nich. Buforujemy caly wiersz (wspolrzedne w pikselach
// docelowych) i wysylamy dopiero gdy dekoder przejdzie do nastepnego wiersza
// lub skonczy obraz (imgRowBufFlush()).
static void imgRowBegin() {
  if (!g_img_rowbuf) return;
  memset(g_img_rowbuf, 0, sizeof(uint16_t) * tft.width());
  g_img_row_src = -1;
}

static void imgRowBufFlush() {
  if (!g_img_rowbuf || g_img_row_src < 0) return;
  tft.pushImage(0, g_img_row_src, tft.width(), 1, g_img_rowbuf);
  g_img_row_src = -1;
}

// Ustawia piksel w buforze wiersza (x,zrodlo w pikselach obrazu zrodlowego).
static void imgBufPixel(int srcX, int srcY, uint16_t color) {
  if (!g_img_rowbuf || srcX < 0 || srcY < 0) return;
  int dx = srcX / g_img_scale;
  int dy = srcY / g_img_scale;
  int W = tft.width(), H = tft.height();
  if (dx < 0 || dx >= W || dy < 0 || dy >= H) return;
  if (dy != g_img_row_src) {
    imgRowBufFlush();
    g_img_row_src = dy;
    memset(g_img_rowbuf, 0, sizeof(uint16_t) * W);
  }
  g_img_rowbuf[dx] = color;
}

// Callback JPEG: dane przychodza w blokach MCU (np. 8x8/16x16). TJpg_Decoder
// sam skaluje i przekazuje juz WSPOLRZEDNE DOCELOWE (po setJpgScale), wiec nie
// dzielimy ich ponownie - wypelniamy prostokat wprost w buforze wiersza.
static bool tjpgOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  if (!g_img_rowbuf) return false;
  int W = tft.width(), H = tft.height();
  for (uint16_t row = 0; row < h; row++) {
    int dy = y + row;
    if (dy < 0 || dy >= H) continue;
    uint16_t *dst = g_img_rowbuf;
    uint16_t *src = bitmap + row * w;
    for (uint16_t col = 0; col < w; col++) {
      int dx = x + col;
      if (dx >= 0 && dx < W) dst[dx] = src[col];
    }
    tft.pushImage(0, dy, W, 1, g_img_rowbuf);
  }
  return true;
}

// Callback PNG - jeden blok RGBA = jeden piksel.
static void pngleDraw(pngle_t *pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint8_t rgba[4]) {
  (void)pngle; (void)w; (void)h;
  if (rgba[3] == 0) return;  // piksel w pelni przezroczysty - zostaw czarne tlo
  imgBufPixel((int)x, (int)y, imgRgb565(rgba[0], rgba[1], rgba[2]));
}

// Wywolywany gdy pngle odczyta naglowek IHDR - znamy juz wymiary, wiec
// ustawiamy skalowanie PRZED pierwszym draw callbackiem.
static void pngleInit(pngle_t *pngle, uint32_t w, uint32_t h) {
  g_img_scale = 1;
  while ((int)w / g_img_scale > tft.width() || (int)h / g_img_scale > tft.height()) {
    g_img_scale *= 2;
    if (g_img_scale > 8) break;
  }
  imgRowBegin();
}

static bool drawPngFromFS(const String &path) {
  fs::File f = LittleFS.open(path, "r");
  if (!f) return false;

  static uint8_t buf[2048];   // statyczny - zbyt duzy na stos
  pngle_t *pngle = pngle_new();
  if (!pngle) { f.close(); return false; }
  pngle_set_init_callback(pngle, pngleInit);
  pngle_set_draw_callback(pngle, pngleDraw);

  bool ok = true;
  size_t remain = 0;
  while (f.available() || remain > 0) {
    // pngle_feed() moze skonsumowac tylko czesc podanego bufora - pozostale
    // bajty trzeba podac ponownie (inaczej gubimy dane i obraz sie psuje).
    if (f.available() && remain < sizeof(buf)) {
      int len = f.read(buf + remain, sizeof(buf) - remain);
      if (len > 0) remain += len;
    }
    if (remain == 0) break;
    int fed = pngle_feed(pngle, buf, remain);
    if (fed < 0) {
      Serial.printf("[IMG] PNG blad: %s\n", pngle_error(pngle));
      ok = false;
      break;
    }
    if (fed > 0 && (size_t)fed < remain) {
      memmove(buf, buf + fed, remain - fed);
    }
    remain -= (size_t)fed;
  }
  imgRowBufFlush();
  pngle_destroy(pngle);
  f.close();
  return ok;
}

static bool drawJpgFromFS(const String &path) {
  uint16_t jw = 0, jh = 0;
  if (TJpgDec.getFsJpgSize(&jw, &jh, path, LittleFS) != JDR_OK || jw == 0 || jh == 0) {
    return false;
  }
  g_img_scale = 1;
  while ((int)jw / g_img_scale > tft.width() || (int)jh / g_img_scale > tft.height()) {
    g_img_scale *= 2;
    if (g_img_scale > 8) break;
  }
  imgRowBegin();
  TJpgDec.setSwapBytes(true);
  TJpgDec.setJpgScale(g_img_scale);
  TJpgDec.setCallback(tjpgOutput);
  JRESULT jr = TJpgDec.drawFsJpg(0, 0, path, LittleFS);
  return jr == JDR_OK;
}

// Rozpoznaje format i rysuje obraz z LittleFS, wpisujac go na czarnym tle.
static bool drawImageFromFS(const String &path) {
#ifdef ENABLE_TFT_DISPLAY
  if (!tftInitialized) return false;
#endif
  if (!littleFsReady || !LittleFS.exists(path)) return false;

  if (!g_img_rowbuf) {
    g_img_rowbuf = (uint16_t*)heap_caps_malloc(sizeof(uint16_t) * tft.width(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!g_img_rowbuf) g_img_rowbuf = (uint16_t*)malloc(sizeof(uint16_t) * tft.width());
    if (!g_img_rowbuf) {
      Serial.println("[IMG] Brak pamieci na bufor wiersza");
      return false;
    }
  }

  tft.fillScreen(TFT_BLACK);
  bool graphicDrawn = false;

  String lower = path;
  lower.toLowerCase();
  if (lower.endsWith(".bmp")) {
    graphicDrawn = drawBmpFromFS(path, 0, 0) || drawBmp16FromFS(path, 0, 0);
  } else if (lower.endsWith(".png")) {
    graphicDrawn = drawPngFromFS(path);
  } else if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) {
    graphicDrawn = drawJpgFromFS(path);
  }

  // Wycentruj poza gornym-lewym naroznikiem, jesli obraz nie wypelnia ekranu.
  if (graphicDrawn) {
    int dw = tft.width()  / g_img_scale;
    int dh = tft.height() / g_img_scale;  // docelowa wysokosc = H przy skalowaniu
    (void)dw; (void)dh;
  }
  return graphicDrawn;
}

static bool isSupportedImagePath(const String &path) {
  String lower = path;
  lower.toLowerCase();
  return lower.endsWith(".bmp") || lower.endsWith(".png") ||
         lower.endsWith(".jpg") || lower.endsWith(".jpeg");
}


String getAPRSSymbolRaw(const APRSStation &station) {
  if (station.symbolTable.length() > 0 && station.symbol.length() > 0) {
    return station.symbolTable + station.symbol;
  }
  if (station.symbol.length() > 0) {
    return station.symbol;
  }
  return "-";
}

// ZwrĂ„â€šĹ‚Ä‚â€žĂ˘â‚¬Ë‡ krĂ„â€šĹ‚tki opis symbolu (<=10 znakĂ„â€šĹ‚w) zgodny z tabelÄ‚â€žĂ˘â‚¬Â¦ APRS
String getAPRSSymbolShort(const APRSStation &station) {
  String raw = getAPRSSymbolRaw(station);
  if (raw == "/-") return "HOUSE";
  if (raw == "/>") return "CAR";
  if (raw == "/#") return "DIGI";
  if (raw == "/r") return "REPEATER";
  if (raw == "/[") return "HUMAN";
  if (raw == "/l") return "LAPTOP";
  if (raw == "/L") return "LOGIN";
  if (raw == "/_") return "WX";
  if (raw == "/s") return "SHIP";
  if (raw == "/a") return "AMBUL";
  if (raw == "/b") return "BIKE";
  if (raw == "/c") return "ICP";
  if (raw == "/d") return "FIRE";
  if (raw == "/f") return "FIRETRK";
  if (raw == "/h") return "HOSP";
  if (raw == "/y") return "YAGI";
  if (raw == "/g") return "GLIDER";
  if (raw == "/k") return "TRUCK";
  if (raw == "/u") return "TRUCK";
  if (raw == "/v") return "VAN";
  if (raw == "/p") return "ROVER";
  if (raw == "/o") return "EOC";
  if (raw == "/t") return "TRKSTOP";
  if (raw == "/m") return "MICERPT";
  if (raw == "/n") return "NODE";
  if (raw == "/q") return "GRID";
  if (raw == "La" || raw == "L_" ) return "LORA";
  if (raw == "L#" || raw == "L&") return "LORA";
  return raw;
}

static String getAprsAlertSymbolDescription(const APRSStation &station) {
  String symbolShort = getAPRSSymbolShort(station);
  if (symbolShort == "HOUSE") return "Home station";
  if (symbolShort == "CAR") return "CAR";
  if (symbolShort == "DIGI") return "Digipeater";
  if (symbolShort == "REPEATER") return "Repeater";
  if (symbolShort == "HUMAN") return "Pedestrian";
  if (symbolShort == "LAPTOP") return "Mobile station";
  if (symbolShort == "LOGIN") return "Login point";
  if (symbolShort == "WX") return "Weather station";
  if (symbolShort == "SHIP") return "Ship";
  if (symbolShort == "AMBUL") return "Ambulans";
  if (symbolShort == "BIKE") return "Bicycle";
  if (symbolShort == "FIRE") return "Fire";
  if (symbolShort == "FIRETRK") return "Fire truck";
  if (symbolShort == "HOSP") return "Hospital";
  if (symbolShort == "GLIDER") return "Glider";
  if (symbolShort == "TRUCK") return "Truck";
  if (symbolShort == "VAN") return "Van";
  if (symbolShort == "ROVER") return "Rover";
  if (symbolShort == "NODE") return "Node";
  if (symbolShort == "LORA") return "LoRa";
  return String("Symbol: ") + symbolShort;
}

static bool isAprsWeatherStationSymbol(const APRSStation &station) {
  if (station.symbol == "_") {
    return true;
  }
  String symbolShort = getAPRSSymbolShort(station);
  if (symbolShort == "WX") {
    return true;
  }
  String rawSymbol = getAPRSSymbolRaw(station);
  return rawSymbol.length() >= 2 && rawSymbol.charAt(rawSymbol.length() - 1) == '_';
}

static bool aprsWxReadDigits(const String &text, int start, int len, int &valueOut) {
  if (start < 0 || len <= 0 || (start + len) > text.length()) {
    return false;
  }
  int value = 0;
  for (int i = 0; i < len; i++) {
    char c = text.charAt(start + i);
    if (!isDigit(c)) {
      return false;
    }
    value = (value * 10) + (c - '0');
  }
  valueOut = value;
  return true;
}

static bool decodeAprsWxFromComment(const String &commentRaw, AprsWxDecoded &wx) {
  wx = AprsWxDecoded();

  String text = commentRaw;
  text.trim();
  if (text.length() == 0) {
    return false;
  }

  int dirDeg = 0;
  int speedMph = 0;
  if (text.length() >= 7 &&
      aprsWxReadDigits(text, 0, 3, dirDeg) &&
      text.charAt(3) == '/' &&
      aprsWxReadDigits(text, 4, 3, speedMph)) {
    wx.hasWindDir = true;
    wx.windDirDeg = dirDeg % 360;
    wx.hasWindSpeed = true;
    wx.windSpeedKmh = speedMph * 1.60934f;
  }

  for (int i = 0; i < text.length(); i++) {
    char tag = text.charAt(i);
    int digits = 0;
    switch (tag) {
      case 'c':
      case 's':
      case 'g':
      case 't':
      case 'r':
      case 'p':
      case 'P':
        digits = 3;
        break;
      case 'h':
        digits = 2;
        break;
      case 'b':
        digits = 5;
        break;
      default:
        break;
    }

    if (digits == 0) {
      continue;
    }

    int value = 0;
    if (!aprsWxReadDigits(text, i + 1, digits, value)) {
      continue;
    }

    if (tag == 'c') {
      wx.hasWindDir = true;
      wx.windDirDeg = value % 360;
    } else if (tag == 's' || (tag == 'g' && !wx.hasWindSpeed)) {
      wx.hasWindSpeed = true;
      wx.windSpeedKmh = value * 1.60934f;
    } else if (tag == 't') {
      wx.hasTempC = true;
      wx.tempC = (value - 32.0f) * (5.0f / 9.0f);
    } else if (tag == 'h') {
      wx.hasHumidity = true;
      wx.humidityPct = (value == 0) ? 100 : value;
      if (wx.humidityPct > 100) {
        wx.humidityPct = 100;
      }
    } else if (tag == 'b') {
      wx.hasPressure = true;
      wx.pressureHpa = value / 10.0f;
    }
  }

  wx.hasAny = wx.hasWindDir || wx.hasWindSpeed || wx.hasTempC || wx.hasHumidity || wx.hasPressure;
  return wx.hasAny;
}

static bool isAprsWxPayloadValidForAlert(const APRSStation &station) {
  if (!isAprsWeatherStationSymbol(station)) {
    return false;
  }

  AprsWxDecoded wx;
  if (!decodeAprsWxFromComment(station.comment, wx)) {
    return false;
  }

  // Minimalny warunek poprawnej ramki WX do alertu: temperatura + wilgotnosc.
  return wx.hasTempC && wx.hasHumidity;
}

static void drawAprsAlertWrappedComment(const String &comment, int x, int y, int widthPx, int maxLines, int textSize) {
  String text = comment;
  text.trim();
  if (text.length() == 0) {
    text = "-";
  }

  const int safeTextSize = (textSize < 1) ? 1 : textSize;
  const int charWidth = 6 * safeTextSize;
  const int lineHeight = (8 * safeTextSize) + 2;
  const int maxCharsPerLine = (charWidth > 0) ? (widthPx / charWidth) : widthPx;
  int cursor = 0;
  int line = 0;

  while (cursor < text.length() && line < maxLines) {
    int end = cursor + maxCharsPerLine;
    if (end >= text.length()) {
      end = text.length();
    } else {
      int split = end;
      while (split > cursor && text.charAt(split) != ' ') {
        split--;
      }
      if (split > cursor) {
        end = split;
      }
    }

    String lineText = text.substring(cursor, end);
    lineText.trim();
    if (lineText.length() > 0) {
      tft.setCursor(x, y + line * lineHeight);
      tft.print(lineText);
      line++;
    }

    cursor = end;
    while (cursor < text.length() && text.charAt(cursor) == ' ') {
      cursor++;
    }
  }
}

static void drawAprsAlertCarIcon(int centerX, int topY, uint16_t color) {
  const int bodyW = 60;
  const int bodyH = 14;
  const int roofW = 33;
  const int roofH = 9;

  const int bodyX = centerX - (bodyW / 2);
  const int roofX = centerX - (roofW / 2);
  const int roofY = topY;
  const int bodyY = topY + 9;

  tft.fillRoundRect(bodyX, bodyY, bodyW, bodyH, 2, color);
  tft.fillRoundRect(roofX, roofY, roofW, roofH, 2, color);

  tft.fillCircle(bodyX + 13, bodyY + bodyH, 6, TFT_DARKGREY);
  tft.fillCircle(bodyX + bodyW - 13, bodyY + bodyH, 6, TFT_DARKGREY);
  tft.fillCircle(bodyX + 13, bodyY + bodyH, 3, TFT_LIGHTGREY);
  tft.fillCircle(bodyX + bodyW - 13, bodyY + bodyH, 3, TFT_LIGHTGREY);
  tft.fillCircle(bodyX + 3, bodyY + 3, 2, TFT_YELLOW);
  tft.fillCircle(bodyX + 3, bodyY + 8, 2, TFT_YELLOW);
}

static void drawAprsAlertHouseIcon(int centerX, int topY) {
  const int bodyW = 39;
  const int bodyH = 24;
  const int bodyX = centerX - (bodyW / 2);
  const int bodyY = topY + 10;

  // Ĺšciany domu (ĹĽĂłĹ‚te)
  tft.fillRect(bodyX, bodyY, bodyW, bodyH, TFT_YELLOW);

  // Dach dwuspadowy (brÄ…zowy)
  const uint16_t roofColor = TFT_GREEN;
  int roofTopY = topY;
  tft.fillTriangle(centerX, roofTopY, bodyX - 3, bodyY + 1, bodyX + bodyW + 3, bodyY + 1, roofColor);

  // Drzwi i okna dla czytelnoĹ›ci
  tft.fillRect(centerX - 4, bodyY + 12, 8, 12, TFT_DARKGREY);
  tft.fillRect(bodyX + 4, bodyY + 6, 6, 6, TFT_LIGHTGREY);
  tft.fillRect(bodyX + bodyW - 10, bodyY + 6, 6, 6, TFT_LIGHTGREY);
}

static void drawAprsAlertHumanIcon(int centerX, int topY) {
  const uint16_t bodyColor = TFT_RED;
  const uint16_t headColor = TFT_BLUE;
  int headX = centerX;
  int headY = topY + 5;

  // GĹ‚owa
  tft.fillCircle(headX, headY, 4, headColor);

  // TuĹ‚Ăłw (lekko pochylony)
  tft.drawLine(headX, headY + 4, headX + 2, headY + 18, bodyColor);
  tft.drawLine(headX + 1, headY + 4, headX + 3, headY + 18, bodyColor);
  tft.drawLine(headX + 2, headY + 4, headX + 4, headY + 18, bodyColor);
  tft.drawLine(headX + 3, headY + 4, headX + 5, headY + 18, bodyColor);

  // RÄ™ce (ruch spaceru)
  tft.drawLine(headX + 1, headY + 8, headX - 6, headY + 12, bodyColor);
  tft.drawLine(headX + 3, headY + 8, headX + 10, headY + 11, bodyColor);
  tft.drawLine(headX + 2, headY + 9, headX - 5, headY + 13, bodyColor);
  tft.drawLine(headX + 4, headY + 9, headX + 11, headY + 12, bodyColor);
  tft.drawLine(headX + 3, headY + 10, headX - 4, headY + 14, bodyColor);
  tft.drawLine(headX + 5, headY + 10, headX + 12, headY + 13, bodyColor);

  // Nogi (jedna do przodu, druga do tyĹ‚u)
  tft.drawLine(headX + 3, headY + 18, headX - 4, headY + 28, bodyColor);
  tft.drawLine(headX + 3, headY + 18, headX + 12, headY + 27, bodyColor);
  tft.drawLine(headX + 4, headY + 18, headX - 3, headY + 28, bodyColor);
  tft.drawLine(headX + 4, headY + 18, headX + 13, headY + 27, bodyColor);
  tft.drawLine(headX + 5, headY + 18, headX - 2, headY + 28, bodyColor);
  tft.drawLine(headX + 5, headY + 18, headX + 14, headY + 27, bodyColor);
}

static void drawAprsAlertCompass(int centerX, int centerY, int radius, bool hasBearing, float bearingDeg) {
  tft.drawCircle(centerX, centerY, radius, TFT_DARKGREY);
  tft.drawCircle(centerX, centerY, radius - 1, TFT_DARKGREY);

  tft.setTextColor(TFT_LIGHTGREY);
  tft.setTextSize(1);
  tft.setCursor(centerX - 3, centerY - radius - 9);
  tft.print("N");

  tft.drawFastVLine(centerX, centerY - radius + 2, 4, TFT_DARKGREY);
  tft.drawFastVLine(centerX, centerY + radius - 5, 4, TFT_DARKGREY);
  tft.drawFastHLine(centerX - radius + 2, centerY, 4, TFT_DARKGREY);
  tft.drawFastHLine(centerX + radius - 5, centerY, 4, TFT_DARKGREY);

  if (!hasBearing) {
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(centerX - 6, centerY + radius + 2);
    tft.print("---");
    return;
  }

  float angleRad = bearingDeg * M_PI / 180.0f;
  int tipX = centerX + (int)(sin(angleRad) * (radius - 3));
  int tipY = centerY - (int)(cos(angleRad) * (radius - 3));

  int perpX = (int)round(cos(angleRad));
  int perpY = (int)round(sin(angleRad));
  tft.drawLine(centerX, centerY, tipX, tipY, TFT_CYAN);
  tft.drawLine(centerX + perpX, centerY + perpY, tipX + perpX, tipY + perpY, TFT_CYAN);
  tft.fillCircle(tipX, tipY, 2, TFT_CYAN);
  tft.fillCircle(centerX, centerY, 1, TFT_CYAN);

  String bearingTxt = String((int)(bearingDeg + 0.5f)) + "d";
  int txtX = centerX - (bearingTxt.length() * 3);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(txtX, centerY + radius + 2);
  tft.print(bearingTxt);
}

static void drawAprsAlertFrame(bool pulseOn) {
  const int frameX = 10;
  const int frameY = 55;
  const int frameW = 460;
  const int frameH = 260;
  uint16_t color = pulseOn ? TFT_RADIO_ORANGE : TFT_DARKGREY;
  tft.drawRect(frameX, frameY, frameW, frameH, color);
  tft.drawRect(frameX + 1, frameY + 1, frameW - 2, frameH - 2, color);
}

static void drawAprsAlertCloseButton() {
  tft.fillRoundRect(APRS_ALERT_CLOSE_BTN_X, APRS_ALERT_CLOSE_BTN_Y, APRS_ALERT_CLOSE_BTN_W, APRS_ALERT_CLOSE_BTN_H, 5, TFT_RADIO_ORANGE);
  tft.drawRoundRect(APRS_ALERT_CLOSE_BTN_X, APRS_ALERT_CLOSE_BTN_Y, APRS_ALERT_CLOSE_BTN_W, APRS_ALERT_CLOSE_BTN_H, 5, TFT_BLACK);
  tft.drawLine(APRS_ALERT_CLOSE_BTN_X + 7, APRS_ALERT_CLOSE_BTN_Y + 7,
               APRS_ALERT_CLOSE_BTN_X + APRS_ALERT_CLOSE_BTN_W - 8, APRS_ALERT_CLOSE_BTN_Y + APRS_ALERT_CLOSE_BTN_H - 8, TFT_BLACK);
  tft.drawLine(APRS_ALERT_CLOSE_BTN_X + APRS_ALERT_CLOSE_BTN_W - 8, APRS_ALERT_CLOSE_BTN_Y + 7,
               APRS_ALERT_CLOSE_BTN_X + 7, APRS_ALERT_CLOSE_BTN_Y + APRS_ALERT_CLOSE_BTN_H - 8, TFT_BLACK);
  tft.drawLine(APRS_ALERT_CLOSE_BTN_X + 8, APRS_ALERT_CLOSE_BTN_Y + 7,
               APRS_ALERT_CLOSE_BTN_X + APRS_ALERT_CLOSE_BTN_W - 7, APRS_ALERT_CLOSE_BTN_Y + APRS_ALERT_CLOSE_BTN_H - 8, TFT_BLACK);
  tft.drawLine(APRS_ALERT_CLOSE_BTN_X + 7, APRS_ALERT_CLOSE_BTN_Y + 8,
               APRS_ALERT_CLOSE_BTN_X + APRS_ALERT_CLOSE_BTN_W - 8, APRS_ALERT_CLOSE_BTN_Y + APRS_ALERT_CLOSE_BTN_H - 7, TFT_BLACK);
  tft.drawLine(APRS_ALERT_CLOSE_BTN_X + APRS_ALERT_CLOSE_BTN_W - 7, APRS_ALERT_CLOSE_BTN_Y + 7,
               APRS_ALERT_CLOSE_BTN_X + 8, APRS_ALERT_CLOSE_BTN_Y + APRS_ALERT_CLOSE_BTN_H - 8, TFT_BLACK);
  tft.drawLine(APRS_ALERT_CLOSE_BTN_X + APRS_ALERT_CLOSE_BTN_W - 8, APRS_ALERT_CLOSE_BTN_Y + 8,
               APRS_ALERT_CLOSE_BTN_X + 7, APRS_ALERT_CLOSE_BTN_Y + APRS_ALERT_CLOSE_BTN_H - 7, TFT_BLACK);
}

static bool isAprsAlertCloseButtonHit(uint16_t x, uint16_t y) {
  int hitX = APRS_ALERT_CLOSE_BTN_X + ((APRS_ALERT_CLOSE_BTN_W - APRS_ALERT_CLOSE_HIT_W) / 2);
  int hitY = APRS_ALERT_CLOSE_BTN_Y + ((APRS_ALERT_CLOSE_BTN_H - APRS_ALERT_CLOSE_HIT_H) / 2);
  if (hitX < 0) hitX = 0;
  if (hitY < 0) hitY = 0;

  int hitRight = hitX + APRS_ALERT_CLOSE_HIT_W;
  int hitBottom = hitY + APRS_ALERT_CLOSE_HIT_H;
  if (hitRight > 480) hitRight = 480;
  if (hitBottom > 320) hitBottom = 320;

  return x >= hitX && x < hitRight && y >= hitY && y < hitBottom;
}

static void dismissAprsAlertScreen() {
  if (!aprsAlertScreenActive) {
    return;
  }
  aprsAlertScreenActive = false;
  drawScreen(currentScreen);
  if (currentScreen == SCREEN_HAM_CLOCK) {
    updateScreen1();
  }
}

static void setRgbLedChannel(uint8_t pin, bool on) {
  digitalWrite(pin, on ? LOW : HIGH);
}

static void applyRgbLedState(bool redOn, bool greenOn, bool blueOn) {
  setRgbLedChannel(RGB_LED_RED_PIN, redOn);
  setRgbLedChannel(RGB_LED_GREEN_PIN, greenOn);
  setRgbLedChannel(RGB_LED_BLUE_PIN, blueOn);
}

static void initStatusRgbLed() {
  pinMode(RGB_LED_RED_PIN, OUTPUT);
  pinMode(RGB_LED_GREEN_PIN, OUTPUT);
  pinMode(RGB_LED_BLUE_PIN, OUTPUT);

  rgbLedPrevWifiConnected = false;
  rgbRedBlinkLastToggleMs = millis();
  rgbRedBlinkStateOn = false;
  rgbBlueAprsAlertActive = false;
  rgbBlueAprsAlertUntilMs = 0;
  rgbBlueAprsLastToggleMs = 0;
  rgbBlueAprsStateOn = false;

  applyRgbLedState(false, false, false);
}

static int normalizeLedAlertDurationMs(int durationMs) {
  if (durationMs < 100) return 100;
  if (durationMs > 60000) return 60000;
  return durationMs;
}

static void applyLedAlertDurationMs(int durationMs) {
  ledAlertDurationMs = normalizeLedAlertDurationMs(durationMs);
}

static int normalizeLedAlertBlinkMs(int blinkMs) {
  if (blinkMs < 50) return 50;
  if (blinkMs > 5000) return 5000;
  return blinkMs;
}

static void applyLedAlertBlinkMs(int blinkMs) {
  ledAlertBlinkMs = normalizeLedAlertBlinkMs(blinkMs);
}

static void triggerAprsRgbLedAlert() {
  if (!enableLedAlert) {
    return;
  }

  unsigned long nowMs = millis();
  rgbBlueAprsAlertActive = true;
  rgbBlueAprsAlertUntilMs = nowMs + (unsigned long)ledAlertDurationMs;
  rgbBlueAprsLastToggleMs = nowMs;
  rgbBlueAprsStateOn = true;
  applyRgbLedState(false, false, true);
}

static void updateStatusRgbLed() {
  unsigned long nowMs = millis();

  if (rgbBlueAprsAlertActive) {
    if ((long)(nowMs - rgbBlueAprsAlertUntilMs) >= 0) {
      rgbBlueAprsAlertActive = false;
      rgbBlueAprsStateOn = false;
    } else if (nowMs - rgbBlueAprsLastToggleMs >= (unsigned long)ledAlertBlinkMs) {
      rgbBlueAprsLastToggleMs = nowMs;
      rgbBlueAprsStateOn = !rgbBlueAprsStateOn;
    }

    if (rgbBlueAprsAlertActive) {
      applyRgbLedState(false, false, rgbBlueAprsStateOn);
      return;
    }
  }

  applyRgbLedState(false, false, false);
}

void ALERT_Screen(const APRSStation &station) {
  if (!tftInitialized) {
    return;
  }

  aprsAlertScreenStation = station;
  aprsAlertScreenActive = true;
  aprsAlertScreenUntilMs = millis() + ((unsigned long)aprsAlertScreenSeconds * 1000UL);
  aprsAlertFrameLastToggleMs = millis();
  aprsAlertFramePulseOn = true;

  tft.fillScreen(TFT_BLACK);
  
  // GĂłrny pasek z nagĹ‚Ăłwkiem
  tft.fillRect(0, 0, 480, 45, TFT_RADIO_ORANGE);
  tft.drawFastHLine(0, 44, 480, 0x8410);

  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(3);
  const String headerText = "APRS ALERT";
  const int headerX = (480 - ((int)headerText.length() * 18)) / 2;
  tft.setCursor(headerX, 10);
  tft.print(headerText);
  
  drawAprsAlertCloseButton();

  // GĹ‚Ăłwna ramka wokĂłĹ‚ zawartoĹ›ci
  const int frameX = 10;
  const int frameY = 55;
  const int frameW = 460;
  const int frameH = 260;
  tft.drawRect(frameX, frameY, frameW, frameH, TFT_RADIO_ORANGE);
  tft.drawRect(frameX + 1, frameY + 1, frameW - 2, frameH - 2, TFT_DARKGREY);

  String callsign = station.callsign;
  callsign.toUpperCase();
  if (callsign.length() == 0) {
    callsign = "-";
  }

  // ZNAK WYWOĹAWCZY - duĹĽy, wyĹ›rodkowany w ramce
  tft.setTextColor(TFT_WHITE);
  int callTextSize = (callsign.length() <= 6) ? 6 : 5;
  tft.setTextSize(callTextSize);
  int charWidth = (callTextSize == 6) ? 36 : 30;
  int callX = frameX + (frameW - (callsign.length() * charWidth)) / 2;
  int callY = frameY + 20;
  
  // Ramka wokĂłĹ‚ znaku
  int callBoxPadding = 10;
  int callBoxW = callsign.length() * charWidth + 2 * callBoxPadding;
  int callBoxH = charWidth + callBoxPadding;
  int callBoxX = frameX + (frameW - callBoxW) / 2;
  tft.drawRect(callBoxX, callY - 5, callBoxW, callBoxH, TFT_WHITE);
  tft.drawRect(callBoxX + 1, callY - 4, callBoxW - 2, callBoxH - 2, TFT_RADIO_ORANGE);
  
  tft.setCursor(callX, callY);
  tft.print(callsign);

  // CzÄ™stotliwoĹ›Ä‡ pod znakiem
  String freqText = "-";
  if (station.freqMHz > 0.0f) {
    freqText = String(station.freqMHz, 3) + " MHz";
  }
  tft.setTextColor(TFT_LIGHTGREY);
  tft.setTextSize(3);
  int freqX = frameX + (frameW - (freqText.length() * 18)) / 2;
  tft.setCursor(freqX, callY + callBoxH + 10);
  tft.print(freqText);

  // KOMPAS i ODLEGĹOĹšÄ† - na dole ramki, obok siebie
  String distanceText = "- km";
  if (station.hasLatLon && station.distance > 0.0f) {
    distanceText = String(station.distance, 1) + " km";
  }
  
  // Kompas po lewej - wyĹĽej
  const int compassX = frameX + 80;
  const int compassY = frameY + frameH - 130;
  const int compassR = 40;
  
  bool hasBearing = false;
  float bearingDeg = 0.0f;
  if (userLatLonValid && station.hasLatLon) {
    hasBearing = true;
    bearingDeg = calculateBearing(userLat, userLon, (double)station.lat, (double)station.lon);
  }
  drawAprsAlertCompass(compassX, compassY, compassR, hasBearing, bearingDeg);

  // OdlegĹ‚oĹ›Ä‡ pod kompasem
  tft.setTextColor(TFT_RADIO_ORANGE);
  tft.setTextSize(2);
  int distX = compassX - ((int)distanceText.length() * 12) / 2;
  tft.setCursor(distX, compassY + compassR + 15);
  tft.print(distanceText);

  // Ikona stacji po prawej stronie (bez opisu, tylko ikona)
  String iconShort = getAPRSSymbolShort(station);
  uint16_t iconColor = TFT_WHITE;
  if (iconShort == "CAR") {
    iconColor = TFT_RED;
  } else if (iconShort == "HOUSE") {
    iconColor = TFT_YELLOW;
  } else if (iconShort == "HUMAN") {
    iconColor = TFT_BLUE;
  }
  
  int iconCenterX = frameX + frameW - 130;
  int iconCenterY = frameY + frameH - 140;
  
  if (iconShort == "CAR") {
    drawAprsAlertCarIcon(iconCenterX, iconCenterY, iconColor);
  } else if (iconShort == "HOUSE") {
    drawAprsAlertHouseIcon(iconCenterX, iconCenterY);
  } else if (iconShort == "HUMAN") {
    drawAprsAlertHumanIcon(iconCenterX, iconCenterY);
  } else {
    // DomyĹ›lnie dom
    drawAprsAlertHouseIcon(iconCenterX, iconCenterY);
  }

  // Typ stacji pod ikonÄ… - dostosowany do nowej pozycji
  tft.setTextColor(TFT_LIGHTGREY);
  tft.setTextSize(2);
  String iconDesc = getAprsAlertSymbolDescription(station);
  int descX = iconCenterX - ((int)iconDesc.length() * 12) / 2;
  tft.setCursor(descX, iconCenterY + 55);
  tft.print(iconDesc);
}

void updateAlertScreenTimeout() {
  if (!aprsAlertScreenActive) {
    return;
  }

  unsigned long nowMs = millis();
  if (nowMs - aprsAlertFrameLastToggleMs >= APRS_ALERT_FRAME_PULSE_MS) {
    aprsAlertFrameLastToggleMs = nowMs;
    aprsAlertFramePulseOn = !aprsAlertFramePulseOn;
    drawAprsAlertFrame(aprsAlertFramePulseOn);
  }

  if ((long)(millis() - aprsAlertScreenUntilMs) < 0) {
    return;
  }

  dismissAprsAlertScreen();
}

// WyciÄ‚â€žĂ˘â‚¬Â¦gnij czÄ‚â€žĂ˘â€žËstotliwoĂ„Ä…Ă˘â‚¬ĹźÄ‚â€žĂ˘â‚¬Ë‡ z komentarza APRS (MHz)
bool extractAPRSFrequencyMHz(const String &text, float &outMHz) {
  const int len = text.length();
  for (int i = 0; i < len; i++) {
    char c = text.charAt(i);
    if (isDigit(c) && (i == 0 || !isDigit(text.charAt(i - 1)))) {
      int j = i;
      bool hasDot = false;
      while (j < len) {
        char cj = text.charAt(j);
        if (cj == '.') {
          if (hasDot) break;
          hasDot = true;
          j++;
          continue;
        }
        if (!isDigit(cj)) break;
        j++;
      }
      if (j <= i) continue;

      String numStr = text.substring(i, j);
      float value = numStr.toFloat();
      if (value <= 0.0f) continue;

      int k = j;
      while (k < len && (text.charAt(k) == ' ' || text.charAt(k) == '\t')) {
        k++;
      }
      if (k + 2 < len) {
        char m1 = text.charAt(k);
        char m2 = text.charAt(k + 1);
        char m3 = text.charAt(k + 2);
        if ((m1 == 'M' || m1 == 'm') && (m2 == 'H' || m2 == 'h') && (m3 == 'Z' || m3 == 'z')) {
          outMHz = value;
          return true;
        }
      }

      // Bez jednostki: akceptuj tylko liczby dziesiÄ‚â€žĂ˘â€žËtne w paĂ„Ä…Ă˘â‚¬Ĺźmie VHF/UHF
      if (hasDot && value >= 30.0f && value <= 1000.0f) {
        outMHz = value;
        return true;
      }
    }
  }
  return false;
}

// Konwersja Maidenhead Locator do wspĂ„â€šĹ‚Ă„Ä…Ă˘â‚¬ĹˇrzÄ‚â€žĂ˘â€žËdnych geograficznych
void locatorToLatLon(String locator, double &lat, double &lon) {
  if (locator.length() < 4) {
    lat = 0;
    lon = 0;
    return;
  }
  
  // Pierwsze 2 znaki - kwadrat (field)
  char c1 = toupper(locator.charAt(0));
  char c2 = toupper(locator.charAt(1));
  lon = (c1 - 'A') * 20 - 180;
  lat = (c2 - 'A') * 10 - 90;
  
  // NastÄ‚â€žĂ˘â€žËpne 2 znaki - kwadrat (square)
  if (locator.length() >= 4) {
    int n1 = locator.charAt(2) - '0';
    int n2 = locator.charAt(3) - '0';
    lon += n1 * 2;
    lat += n2 * 1;
  }
  
  // Ă„Ä…ÄąË‡rodek kwadratu
  lon += 1;
  lat += 0.5;
}

// Konwersja wspolrzednych geograficznych na lokator Maidenhead.
// precision: 4, 6 lub 8 znakow (domyslnie 6 - np. JO82LK).
String latLonToLocator(double lat, double lon, int precision) {
  if (precision != 4 && precision != 6 && precision != 8) precision = 6;
  if (lat < -90.0) lat = -90.0;
  if (lat > 90.0) lat = 90.0;
  // Longitude 180 -> -180, zeby nie wyjsc poza pole 'R'.
  if (lon >= 180.0) lon -= 360.0;
  if (lon < -180.0) lon += 360.0;

  double adjLon = lon + 180.0;
  double adjLat = lat + 90.0;
  String loc = "";

  int lonField = (int)(adjLon / 20.0);
  int latField = (int)(adjLat / 10.0);
  loc += (char)('A' + lonField);
  loc += (char)('A' + latField);

  adjLon -= lonField * 20.0;
  adjLat -= latField * 10.0;

  int lonSq = (int)(adjLon / 2.0);
  int latSq = (int)adjLat;
  loc += (char)('0' + lonSq);
  loc += (char)('0' + latSq);

  if (precision >= 6) {
    adjLon -= lonSq * 2.0;
    adjLat -= latSq;
    int lonSub = (int)(adjLon * 12.0);
    int latSub = (int)(adjLat * 24.0);
    if (lonSub > 23) lonSub = 23;
    if (latSub > 23) latSub = 23;
    loc += (char)('a' + lonSub);
    loc += (char)('a' + latSub);
  }

  if (precision >= 8) {
    adjLon -= ((int)(adjLon * 12.0)) / 12.0;
    adjLat -= ((int)(adjLat * 24.0)) / 24.0;
    int lonExt = (int)(adjLon * 120.0);
    int latExt = (int)(adjLat * 240.0);
    if (lonExt > 9) lonExt = 9;
    if (latExt > 9) latExt = 9;
    loc += (char)('0' + lonExt);
    loc += (char)('0' + latExt);
  }
  return loc;
}

// ---- Parser NMEA (GGA + RMC) - bez zewnetrznej biblioteki ----
// "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,..."
static double nmeaToDeg(const String &value, const String &hemi) {
  if (value.length() < 4) return 0.0;
  double raw = value.toDouble();
  int deg = (int)(raw / 100.0);
  double min = raw - deg * 100.0;
  double dec = deg + min / 60.0;
  if (hemi == "S" || hemi == "W") dec = -dec;
  return dec;
}

// Dzieli ramke po przecinkach. Pola sa kopiowane do tablicy String.
static int nmeaSplit(const String &s, String *out, int maxFields) {
  int n = 0, start = 0;
  int len = s.length();
  for (int i = 0; i <= len && n < maxFields; i++) {
    if (i == len || s.charAt(i) == ',') {
      out[n++] = s.substring(start, i);
      start = i + 1;
    }
  }
  return n;
}

static void gpsParseNmea(const String &line) {
  // Akceptujemy GGA i RMC dla wszystkich konstelacji ($GPGGA, $GNGGA, $GLGGA...).
  int star = line.indexOf('*');
  String body = (star >= 0) ? line.substring(0, star) : line;
  if (body.length() < 7 || body.charAt(0) != '$') return;
  String type = body.substring(3, 6);  // "GGA" / "RMC"
  if (type != "GGA" && type != "RMC") return;

  String f[20];
  int n = nmeaSplit(body, f, 20);

  if (type == "GGA") {
    if (n < 10) return;
    gpsUtcTime = f[1];
    gpsSats = f[7].toInt();
    gpsHdop = f[8].length() ? f[8].toFloat() : 99.9f;
    gpsAltM = f[9].length() ? f[9].toFloat() : 0.0f;
    int quality = f[6].toInt();
    if (quality > 0 && f[2].length() >= 4 && f[4].length() >= 4) {
      gpsLat = nmeaToDeg(f[2], f[3]);
      gpsLon = nmeaToDeg(f[4], f[5]);
      gpsFix = true;
      gpsHasPosition = true;
      gpsLastFixMs = millis();
      gpsLocator = latLonToLocator(gpsLat, gpsLon, 6);
    } else {
      gpsFix = false;
    }
  } else if (type == "RMC") {
    if (n < 10) return;
    // f[2] = status (A=aktywny, V=nieaktywny), f[7] = predkosc [wezly]
    bool active = (f[2] == "A");
    if (f[9].length() == 6) gpsDateStr = f[9];
    if (active && f[3].length() >= 4 && f[5].length() >= 4) {
      gpsLat = nmeaToDeg(f[3], f[4]);
      gpsLon = nmeaToDeg(f[5], f[6]);
      gpsFix = true;
      gpsHasPosition = true;
      gpsLastFixMs = millis();
      gpsLocator = latLonToLocator(gpsLat, gpsLon, 6);
    }
    if (f[7].length()) gpsSpeedKmh = f[7].toFloat() * 1.852f;  // wezly -> km/h
  }
}

// (Re)inicjalizacja UART2 dla modulu GPS. Idempotentna - wywolywana po zmianie
// ustawien w WWW bez restartu.
void gpsApplyConfig() {
  if (gpsEnabled) {
    if (!gpsSerialBegun || gpsLastBaudApplied != gpsBaud ||
        gpsLastRxApplied != gpsRxPin || gpsLastTxApplied != gpsTxPin) {
      gpsSerial.end();
      delay(10);
      gpsSerial.begin(gpsBaud, SERIAL_8N1, gpsRxPin, gpsTxPin);
      gpsSerialBegun = true;
      gpsLastBaudApplied = gpsBaud;
      gpsLastRxApplied = gpsRxPin;
      gpsLastTxApplied = gpsTxPin;
      gpsSentenceCount = 0;
      gpsHasPosition = false;
      gpsFix = false;
      Serial.printf("[GPS] UART2 start: baud=%d RX=%d TX=%d\n", gpsBaud, gpsRxPin, gpsTxPin);
    }
  } else if (gpsSerialBegun) {
    gpsSerial.end();
    gpsSerialBegun = false;
    gpsFix = false;
    gpsHasPosition = false;
    Serial.println("[GPS] UART2 zatrzymany (modul wylaczony)");
  }
}

// Wczytuje dostepne ramki NMEA. Tanie - wywolywane w loop().
void gpsUpdate() {
  if (!gpsEnabled || !gpsSerialBegun) return;
  int budget = 0;
  while (gpsSerial.available() && budget < 512) {
    String line = gpsSerial.readStringUntil('\n');
    budget += line.length() + 1;
    line.trim();
    if (line.length() >= 7 && line.charAt(0) == '$') {
      gpsLastSentenceMs = millis();
      gpsSentenceCount++;
      gpsParseNmea(line);
    }
  }
  // Brak ramek przez 5 s = modul nie odpowiada -> gas FIX (nie pokazuj starych danych).
  if (gpsFix && millis() - gpsLastFixMs > 5000) {
    gpsFix = false;
  }
  gpsApplyPosition();
}

// Zapamietuje recznie ustawiona pozycje (z WWW), zeby moc ja przywrocic po
// wylaczeniu GPS. Wywolywane przy zapisie ustawien i po starcie.
void gpsSnapshotManualPosition() {
  if (gpsPositionOverrideActive) return;
  userLatManual = userLat;
  userLonManual = userLon;
  userLocatorManual = userLocator;
  userLatLonValidManual = userLatLonValid;
}

// Nadpisz pozycje uzytkownika pozycja GPS (gdy wlaczone i jest FIX) albo
// przywroc wartosci reczne. Wywolywane po kazdej aktualizacji NMEA.
void gpsApplyPosition() {
  bool wantOverride = gpsEnabled && gpsUseForPosition && gpsFix && gpsHasPosition;
  if (wantOverride) {
    userLat = gpsLat;
    userLon = gpsLon;
    userLatLonValid = true;
    if (gpsLocator.length() >= 4) userLocator = gpsLocator;
    gpsPositionOverrideActive = true;
  } else if (gpsPositionOverrideActive) {
    userLat = userLatManual;
    userLon = userLonManual;
    userLatLonValid = userLatLonValidManual;
    userLocator = userLocatorManual;
    gpsPositionOverrideActive = false;
  }
}

bool gpsModuleDetected() {
  return gpsEnabled && gpsSentenceCount > 0 && (millis() - gpsLastSentenceMs) < 5000;
}

void updateUserLatLonFromLocator() {
  if (userLocator.length() >= 4) {
    locatorToLatLon(userLocator, userLat, userLon);
    userLatLonValid = true;
  } else {
    userLat = 0.0;
    userLon = 0.0;
    userLatLonValid = false;
  }
}

// SprawdĂ„Ä…ÄąĹş czy tekst wspĂ„â€šĹ‚Ă„Ä…Ă˘â‚¬ĹˇrzÄ‚â€žĂ˘â€žËdnych ma tylko cyfry i kropkÄ‚â€žĂ˘â€žË (nie obsĂ„Ä…Ă˘â‚¬Ĺˇugujemy formatĂ„â€šĹ‚w skompresowanych)
static bool isAprsNumeric(const String &raw_aprs) {
    if (raw_aprs.length() == 0) {
        return false;
    }
    int dotCount = 0;
    for (size_t i = 0; i < raw_aprs.length(); i++) {
        char c = raw_aprs.charAt(i);
        if (c == '.') {
            dotCount++;
            if (dotCount > 1) {
                return false;
            }
        } else if (!isDigit(c)) {
            return false;
        }
    }
    return true;
}

// Konwersja surowej pozycji APRS na stopnie dziesiÄ‚â€žĂ˘â€žËtne
// Konwersja formatu APRS (DDMM.mmN lub DDDMM.mmE) na stopnie dziesiÄ‚â€žĂ˘â€žËtne
// Format APRS: DDMM.mm dla szerokoĂ„Ä…Ă˘â‚¬Ĺźci, DDDMM.mm dla dĂ„Ä…Ă˘â‚¬ĹˇugoĂ„Ä…Ă˘â‚¬Ĺźci
float convertToDecimal(String raw_aprs, char direction) {
    if (raw_aprs.length() < 4) {
        LOGV_PRINTF("[APRS] convertToDecimal: za krĂ„â€šĹ‚tki string: %s\n", raw_aprs.c_str());
        return NAN;
    }
    if (!isAprsNumeric(raw_aprs)) {
        LOGV_PRINTF("[APRS] convertToDecimal: nie-numeryczny format: %s\n", raw_aprs.c_str());
        return NAN;
    }

    double degrees = 0.0;
    double minutes = 0.0;

    if (direction == 'N' || direction == 'S') {
        // SzerokoĂ„Ä…Ă˘â‚¬ĹźÄ‚â€žĂ˘â‚¬Ë‡ (Latitude): pierwsze 2 znaki to stopnie (DDMM.mm)
        // PrzykĂ„Ä…Ă˘â‚¬Ĺˇad: "5202.40" -> degrees=52, minutes=02.40
        if (raw_aprs.length() >= 2) {
            String degStr = raw_aprs.substring(0, 2);
            degrees = degStr.toDouble();
        }
        if (raw_aprs.length() > 2) {
            String minStr = raw_aprs.substring(2);
            minutes = minStr.toDouble();
        }
    } else if (direction == 'E' || direction == 'W') {
        // DĂ„Ä…Ă˘â‚¬ĹˇugoĂ„Ä…Ă˘â‚¬ĹźÄ‚â€žĂ˘â‚¬Ë‡ (Longitude): pierwsze 3 znaki to stopnie (DDDMM.mm)
        // PrzykĂ„Ä…Ă˘â‚¬Ĺˇad: "01655.12" -> degrees=016, minutes=55.12
        if (raw_aprs.length() >= 3) {
            String degStr = raw_aprs.substring(0, 3);
            degrees = degStr.toDouble();
        }
        if (raw_aprs.length() > 3) {
            String minStr = raw_aprs.substring(3);
            minutes = minStr.toDouble();
        }
    } else {
        LOGV_PRINTF("[APRS] convertToDecimal: nieznany kierunek: %c\n", direction);
        return NAN;
    }

    // Walidacja zakresĂ„â€šĹ‚w
    if (direction == 'N' || direction == 'S') {
        if (degrees < 0 || degrees > 90 || minutes < 0 || minutes >= 60) {
            LOGV_PRINTF("[APRS] convertToDecimal: nieprawidĂ„Ä…Ă˘â‚¬Ĺˇowa szerokoĂ„Ä…Ă˘â‚¬ĹźÄ‚â€žĂ˘â‚¬Ë‡: %s%c (deg=%.1f, min=%.2f)\n", 
                       raw_aprs.c_str(), direction, degrees, minutes);
            return NAN;
        }
    } else {
        if (degrees < 0 || degrees > 180 || minutes < 0 || minutes >= 60) {
            LOGV_PRINTF("[APRS] convertToDecimal: nieprawidĂ„Ä…Ă˘â‚¬Ĺˇowa dĂ„Ä…Ă˘â‚¬ĹˇugoĂ„Ä…Ă˘â‚¬ĹźÄ‚â€žĂ˘â‚¬Ë‡: %s%c (deg=%.1f, min=%.2f)\n", 
                       raw_aprs.c_str(), direction, degrees, minutes);
            return NAN;
        }
    }

    double decimal = degrees + (minutes / 60.0);

    // JeĂ„Ä…Ă˘â‚¬Ĺźli PoĂ„Ä…Ă˘â‚¬Ĺˇudnie (S) lub ZachĂ„â€šĹ‚d (W), wynik musi byÄ‚â€žĂ˘â‚¬Ë‡ ujemny
    if (direction == 'S' || direction == 'W') {
        decimal = -decimal;
    }

    LOGV_PRINTF("[APRS] convertToDecimal: %s%c -> %.6f (deg=%.0f, min=%.2f)\n", 
               raw_aprs.c_str(), direction, decimal, degrees, minutes);

    return (float)decimal;
}

// ===== APRS Mic-E / Compressed / Uncompressed decoding (ported from ESP32APRS_Audio) =====
static bool validSymTableCompressed(char c) {
    return (c == '/' || c == '\\' || (c >= 0x41 && c <= 0x5A) || (c >= 0x61 && c <= 0x6A)); // [\/\\A-Za-j]
}

static bool validSymTableUncompressed(char c) {
    return (c == '/' || c == '\\' || (c >= 0x41 && c <= 0x5A) || (c >= 0x30 && c <= 0x39)); // [\/\\A-Z0-9]
}

static bool parseFixedUnsigned(const char *src, int offset, int digits, unsigned int &out) {
  out = 0;
  for (int i = 0; i < digits; i++) {
    char c = src[offset + i];
    if (c < '0' || c > '9') {
      return false;
    }
    out = out * 10U + (unsigned int)(c - '0');
  }
  return true;
}

static int aprsMicECommentBodyOffset(const unsigned char *body, size_t len) {
  const size_t kMicEFixedLen = 8; // lon(3) + spd/crs(3) + sym(2)
  if (!body || len <= kMicEFixedLen) {
    return (int)kMicEFixedLen;
  }

  size_t idx = kMicEFixedLen;
  unsigned char marker = body[idx];

  // Optional Mic-E telemetry may start with '`', '\'', 0x1C or 0x1D.
  if (marker == 0x60 || marker == 0x27 || marker == 0x1C || marker == 0x1D) {
    size_t probe = idx + 1;
    size_t telemetryLen = 0;
    while (probe < len && telemetryLen < 10) {
      unsigned char c = body[probe];
      if (c < 0x21 || c > 0x7B) {
        break;
      }
      telemetryLen++;
      probe++;
    }

    // Skip telemetry only when it looks like a valid telemetry block.
    if (telemetryLen >= 2) {
      idx = probe;
    }
  }

  return (int)idx;
}

static bool aprsLatLonInRange(float lat, float lon) {
  if (isnan(lat) || isnan(lon)) return false;
  if (lat < -90.0f || lat > 90.0f) return false;
  if (lon < -180.0f || lon > 180.0f) return false;
  // APRS parser conventions: 0,0 is treated as invalid/no position.
  if (fabsf(lat) < 0.0001f && fabsf(lon) < 0.0001f) return false;
  return true;
}

static String normalizeAprsParsedComment(const String &rawComment) {
  String out;
  out.reserve(rawComment.length());
  for (size_t i = 0; i < rawComment.length(); i++) {
    char c = rawComment.charAt(i);
    if (c == '\r' || c == '\n') {
      out += ' ';
      continue;
    }
    if ((unsigned char)c >= 32 || c == '\t') {
      out += c;
    }
  }
  out.trim();
  return out;
}

static bool parseAprsUncompressed(const char *body, size_t len, float &lat, float &lon, char &symTable, char &symCode) {
    if (len < 19) {
        return false;
    }
    char posbuf[20];
    memcpy(posbuf, body, 19);
    posbuf[19] = 0;

    // Position ambiguity handling
    if (posbuf[2] == ' ') posbuf[2] = '3';
    if (posbuf[3] == ' ') posbuf[3] = '5';
    if (posbuf[5] == ' ') posbuf[5] = '5';
    if (posbuf[6] == ' ') posbuf[6] = '5';
    if (posbuf[12] == ' ') posbuf[12] = '3';
    if (posbuf[13] == ' ') posbuf[13] = '5';
    if (posbuf[15] == ' ') posbuf[15] = '5';
    if (posbuf[16] == ' ') posbuf[16] = '5';

    if (posbuf[4] != '.' || posbuf[14] != '.') {
      return false;
    }

    unsigned int latDeg = 0, latMin = 0, latMinFrag = 0;
    unsigned int lonDeg = 0, lonMin = 0, lonMinFrag = 0;
    if (!parseFixedUnsigned(posbuf, 0, 2, latDeg) ||
      !parseFixedUnsigned(posbuf, 2, 2, latMin) ||
      !parseFixedUnsigned(posbuf, 5, 2, latMinFrag) ||
      !parseFixedUnsigned(posbuf, 9, 3, lonDeg) ||
      !parseFixedUnsigned(posbuf, 12, 2, lonMin) ||
      !parseFixedUnsigned(posbuf, 15, 2, lonMinFrag)) {
        return false;
    }

    char latHemi = posbuf[7];
    symTable = posbuf[8];
    char lonHemi = posbuf[17];
    symCode = posbuf[18];

    if (!validSymTableUncompressed(symTable)) {
        symTable = 0;
    }

    bool isSouth = (latHemi == 'S' || latHemi == 's');
    bool isWest = (lonHemi == 'W' || lonHemi == 'w');
    if (!isSouth && latHemi != 'N' && latHemi != 'n') return false;
    if (!isWest && lonHemi != 'E' && lonHemi != 'e') return false;
    if (latDeg > 90 || lonDeg > 180) return false;
    if (latMin >= 60 || lonMin >= 60) return false;

    lat = (float)latDeg + (float)latMin / 60.0f + (float)latMinFrag / 6000.0f;
    lon = (float)lonDeg + (float)lonMin / 60.0f + (float)lonMinFrag / 6000.0f;
    if (isSouth) lat = -lat;
    if (isWest) lon = -lon;
    return aprsLatLonInRange(lat, lon);
}

static bool parseAprsCompressed(const char *body, size_t len, float &lat, float &lon, char &symTable, char &symCode) {
    if (len < 13) {
        return false;
    }
    symTable = body[0];
    symCode = body[9];
    if (!validSymTableCompressed(symTable)) return false;
    for (int i = 1; i <= 8; i++) {
        if (body[i] < 0x21 || body[i] > 0x7b) return false;
    }

    int lat1 = (body[1] - 33);
    int lat2 = (body[2] - 33);
    int lat3 = (body[3] - 33);
    int lat4 = (body[4] - 33);
    lat1 = ((((lat1 * 91) + lat2) * 91) + lat3) * 91 + lat4;

    int lon1 = (body[5] - 33);
    int lon2 = (body[6] - 33);
    int lon3 = (body[7] - 33);
    int lon4 = (body[8] - 33);
    lon1 = ((((lon1 * 91) + lon2) * 91) + lon3) * 91 + lon4;

    lat = 90.0f - ((float)(lat1) / 380926.0f);
    lon = -180.0f + ((float)(lon1) / 190463.0f);

    if (symTable >= 'a' && symTable <= 'j') {
        symTable -= 81; // a-j -> 0-9
    }
    return aprsLatLonInRange(lat, lon);
}

static bool parseAprsMicE(const char *dstcall, const unsigned char *body, size_t len,
                          float &lat, float &lon, char &symTable, char &symCode) {
    if (!dstcall || strlen(dstcall) != 6) return false;
    if (len < 8) return false;

    // Validate destination callsign format
    for (int i = 0; i < 3; i++) {
        if (!((dstcall[i] >= '0' && dstcall[i] <= '9') ||
              (dstcall[i] >= 'A' && dstcall[i] <= 'L') ||
              (dstcall[i] >= 'P' && dstcall[i] <= 'Z'))) {
            return false;
        }
    }
    for (int i = 3; i < 6; i++) {
        if (!((dstcall[i] >= '0' && dstcall[i] <= '9') ||
              (dstcall[i] == 'L') ||
              (dstcall[i] >= 'P' && dstcall[i] <= 'Z'))) {
            return false;
        }
    }

    // Validate info field
    if (body[0] < 0x26 || body[0] > 0x7f) return false;
    if (body[1] < 0x26 || body[1] > 0x61) return false;
    if (body[2] < 0x1c || body[2] > 0x7f) return false;
    if (body[3] < 0x1c || body[3] > 0x7f) return false;
    if (body[4] < 0x1c || body[4] > 0x7d) return false;
    if (body[5] < 0x1c || body[5] > 0x7f) return false;
    if ((body[6] < 0x21 || body[6] > 0x7b) && body[6] != 0x7d) return false;
    if (body[7] != '/' && body[7] != '\\') return false;

    char dst[7];
    strncpy(dst, dstcall, 6);
    dst[6] = 0;

    for (char *p = dst; *p; p++) {
        if (*p >= 'A' && *p <= 'J') *p -= 'A' - '0';
        else if (*p >= 'P' && *p <= 'Y') *p -= 'P' - '0';
        else if (*p == 'K' || *p == 'L' || *p == 'Z') *p = '_';
    }

    int posAmbiguity = 0;
    if (dst[5] == '_') { dst[5] = '5'; posAmbiguity = 1; }
    if (dst[4] == '_') { dst[4] = '5'; posAmbiguity = 2; }
    if (dst[3] == '_') { dst[3] = '5'; posAmbiguity = 3; }
    if (dst[2] == '_') { dst[2] = '3'; posAmbiguity = 4; }
    if (dst[1] == '_' || dst[0] == '_') return false;

    unsigned int latDeg = 0, latMin = 0, latMinFrag = 0;
    if (!parseFixedUnsigned(dst, 0, 2, latDeg) ||
      !parseFixedUnsigned(dst, 2, 2, latMin) ||
      !parseFixedUnsigned(dst, 4, 2, latMinFrag)) {
      return false;
    }
    lat = (float)latDeg + (float)latMin / 60.0f + (float)latMinFrag / 6000.0f;
    if (dstcall[3] <= 0x4c) lat = -lat;

    unsigned int lonDeg = body[0] - 28;
    if (dstcall[4] >= 0x50) lonDeg += 100;
    if (lonDeg >= 180 && lonDeg <= 189) lonDeg -= 80;
    else if (lonDeg >= 190 && lonDeg <= 199) lonDeg -= 190;

    unsigned int lonMin = body[1] - 28;
    if (lonMin >= 60) lonMin -= 60;
    unsigned int lonMinFrag = body[2] - 28;

    switch (posAmbiguity) {
        case 0:
            lon = (float)lonDeg + (float)lonMin / 60.0f + (float)lonMinFrag / 6000.0f;
            break;
        case 1:
            lon = (float)lonDeg + (float)lonMin / 60.0f + (float)(lonMinFrag - lonMinFrag % 10 + 5) / 6000.0f;
            break;
        case 2:
            lon = (float)lonDeg + ((float)lonMin + 0.5f) / 60.0f;
            break;
        case 3:
            lon = (float)lonDeg + (float)(lonMin - lonMin % 10 + 5) / 60.0f;
            break;
        case 4:
            lon = (float)lonDeg + 0.5f;
            break;
        default:
            return false;
    }

    if (dstcall[5] >= 0x50) lon = -lon;

    symCode = body[6];
    symTable = body[7];
    return aprsLatLonInRange(lat, lon);
}

static bool parseAprsAdvancedPosition(const String &line, APRSStation &station) {
    int gtPos = line.indexOf('>');
    if (gtPos < 0) return false;
    int colonPos = line.indexOf(':', gtPos + 1);
    if (colonPos < 0) return false;

    String dstPath = line.substring(gtPos + 1, colonPos);
    int commaPos = dstPath.indexOf(',');
    String dstcall = (commaPos >= 0) ? dstPath.substring(0, commaPos) : dstPath;
    dstcall.trim();
    if (dstcall.length() == 0) return false;

    String payload = line.substring(colonPos + 1);
    if (payload.length() == 0) return false;

    char packettype = payload.charAt(0);
    const char *body = payload.c_str() + 1;
    size_t bodyLen = payload.length() > 0 ? (size_t)(payload.length() - 1) : 0;

    float lat = 0.0f, lon = 0.0f;
    char symTable = 0, symCode = 0;
    bool ok = false;
    int commentOffset = -1; // offset in payload
    String objectOrItemName = "";

    if (packettype == '`' || packettype == '\'') {
        ok = parseAprsMicE(dstcall.c_str(), (const unsigned char *)body, bodyLen, lat, lon, symTable, symCode);
      if (ok) commentOffset = 1 + aprsMicECommentBodyOffset((const unsigned char *)body, bodyLen);
    } else if (packettype == '!' || packettype == '=' || packettype == '/' || packettype == '@') {
        size_t offset = 1;
        if ((packettype == '/' || packettype == '@') && bodyLen >= 7) {
            body += 7;
            bodyLen -= 7;
            offset += 7;
        }
        if (bodyLen > 0) {
            char poschar = body[0];
            if (validSymTableCompressed(poschar)) {
                ok = parseAprsCompressed(body, bodyLen, lat, lon, symTable, symCode);
                if (ok) commentOffset = (int)(offset + 13);
            } else if (poschar >= '0' && poschar <= '9') {
                ok = parseAprsUncompressed(body, bodyLen, lat, lon, symTable, symCode);
                if (ok) commentOffset = (int)(offset + 19);
            }
        }
    } else if (packettype == ';') {
      // Object: name + (* or _) + timestamp(7) + position
      int nameEnd = -1;
      for (int i = 0; i < (int)bodyLen && i < 15; i++) {
        if (body[i] == '*' || body[i] == '_') {
          nameEnd = i;
          break;
        }
      }
      if (nameEnd >= 0) {
        objectOrItemName = payload.substring(1, 1 + nameEnd);
        objectOrItemName.trim();
      }
      if (nameEnd >= 0 && (nameEnd + 8) < (int)bodyLen) {
        const char *posBody = body + nameEnd + 8;
        size_t posLen = bodyLen - (nameEnd + 8);
            char poschar = posBody[0];
            if (validSymTableCompressed(poschar)) {
                ok = parseAprsCompressed(posBody, posLen, lat, lon, symTable, symCode);
          if (ok) commentOffset = 1 + nameEnd + 8 + 13;
            } else if (poschar >= '0' && poschar <= '9') {
                ok = parseAprsUncompressed(posBody, posLen, lat, lon, symTable, symCode);
          if (ok) commentOffset = 1 + nameEnd + 8 + 19;
            }
        }
    } else if (packettype == ')') {
        // Item: name ends with ! or _
        int nameEnd = -1;
        for (int i = 0; i < 9 && i < (int)bodyLen; i++) {
            if (body[i] == '!' || body[i] == '_') {
                nameEnd = i;
                break;
            }
        }
        if (nameEnd >= 0 && (nameEnd + 1) < (int)bodyLen) {
        objectOrItemName = payload.substring(1, 1 + nameEnd);
        objectOrItemName.trim();
            const char *posBody = body + nameEnd + 1;
            size_t posLen = bodyLen - (nameEnd + 1);
            char poschar = posBody[0];
            if (validSymTableCompressed(poschar)) {
                ok = parseAprsCompressed(posBody, posLen, lat, lon, symTable, symCode);
                if (ok) commentOffset = 1 + nameEnd + 1 + 13;
            } else if (poschar >= '0' && poschar <= '9') {
                ok = parseAprsUncompressed(posBody, posLen, lat, lon, symTable, symCode);
                if (ok) commentOffset = 1 + nameEnd + 1 + 19;
            }
        }
    }

    if (!ok) {
        return false;
    }

    station.lat = lat;
    station.lon = lon;
    station.hasLatLon = aprsLatLonInRange(lat, lon);
    if (!station.hasLatLon) {
      return false;
    }
    if (symCode != 0) {
        station.symbol = String(symCode);
    }
    if (symTable != 0) {
        station.symbolTable = String(symTable);
    }

    if (objectOrItemName.length() > 0) {
      // Dla raportĂłw Object/Item uĹĽyj nazwy obiektu jako identyfikatora stacji.
      station.callsign = objectOrItemName;
    }

    if (commentOffset > 0 && commentOffset < payload.length()) {
      station.comment = normalizeAprsParsedComment(payload.substring(commentOffset));
    }
    return true;
}

// Obliczanie odlegĂ„Ä…Ă˘â‚¬ĹˇoĂ„Ä…Ă˘â‚¬Ĺźci metodÄ‚â€žĂ˘â‚¬Â¦ Haversine (km)
// UĂ„Ä…Ă„Ëťywa double dla wiÄ‚â€žĂ˘â€žËkszej precyzji obliczeĂ„Ä…Ă˘â‚¬Ĺľ
// UĂ„Ä…Ă„Ëťywa promienia Ziemi 6366.71 km (jak w ESP32APRS_Audio) dla zgodnoĂ„Ä…Ă˘â‚¬Ĺźci z aprs.fi
float calculateDistance(double lat1, double lon1, double lat2, double lon2) {
    const double r = 6366.71; // PromieĂ„Ä…Ă˘â‚¬Ĺľ Ziemi w km (uĂ„Ä…Ă„Ëťywany przez ESP32APRS_Audio i aprs.fi)
    const double dLat = (lat2 - lat1) * M_PI / 180.0;
    const double dLon = (lon2 - lon1) * M_PI / 180.0;

    const double a = sin(dLat/2.0) * sin(dLat/2.0) +
                     cos(lat1 * M_PI / 180.0) * cos(lat2 * M_PI / 180.0) *
                     sin(dLon/2.0) * sin(dLon/2.0);
    const double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
    
    return (float)(r * c); // Wynik w km
}

// Oblicz azymut poczÄ…tkowy (0..360), gdzie 0 = PĂłĹ‚noc
float calculateBearing(double lat1, double lon1, double lat2, double lon2) {
    const double phi1 = lat1 * M_PI / 180.0;
    const double phi2 = lat2 * M_PI / 180.0;
    const double dLon = (lon2 - lon1) * M_PI / 180.0;

    const double y = sin(dLon) * cos(phi2);
    const double x = cos(phi1) * sin(phi2) - sin(phi1) * cos(phi2) * cos(dLon);
    double bearingDeg = atan2(y, x) * 180.0 / M_PI;
    if (bearingDeg < 0.0) {
      bearingDeg += 360.0;
    }
    return (float)bearingDeg;
}

String formatDistanceOrCountry(const DXSpot &spot, size_t maxLen) {
  if (spot.country.length() > 0) {
    String c = spot.country;
    c.trim();
    if (c.length() > maxLen) {
      c = c.substring(0, maxLen);
    }
    return c;
  }
  return "-";
}

// OkreĂ„Ä…Ă˘â‚¬Ĺźlanie pasma na podstawie czÄ‚â€žĂ˘â€žËstotliwoĂ„Ä…Ă˘â‚¬Ĺźci (kHz) wg bandplanu
String getBand(float freq) {
  // Normalize to kHz so both kHz (DX cluster) and MHz (POTA API) inputs map correctly
  float freqKHz = freq;
  if (freqKHz > 0.0f && freqKHz < 1000.0f) {
    freqKHz *= 1000.0f;
  }

  if (freqKHz >= 1810 && freqKHz <= 2000) return "240m";
  if (freqKHz >= 3500 && freqKHz <= 3800) return "80m";
  if (freqKHz >= 7000 && freqKHz <= 7200) return "40m";
  if (freqKHz >= 10100 && freqKHz <= 10150) return "30m";
  if (freqKHz >= 14000 && freqKHz <= 14350) return "20m";
  if (freqKHz >= 18068 && freqKHz <= 18168) return "17m";
  if (freqKHz >= 21000 && freqKHz <= 21450) return "15m";
  if (freqKHz >= 24890 && freqKHz <= 24990) return "12m";
  if (freqKHz >= 28000 && freqKHz <= 29700) return "10m";
  return "Other";
}

// OkreĂ„Ä…Ă˘â‚¬Ĺźlanie modulacji na podstawie komentarza
String getMode(String comment) {
  comment.toUpperCase();
  if (comment.indexOf("CW") >= 0) return "CW";
  if (comment.indexOf("FT4") >= 0) return "FT4";
  if (comment.indexOf("FT8") >= 0) return "FT8";
  if (comment.indexOf("SSB") >= 0 || comment.indexOf("USB") >= 0 || comment.indexOf("LSB") >= 0) return "SSB";
  return "SSB"; // DomyĂ„Ä…Ă˘â‚¬Ĺźlnie SSB
}

String extractXmlTagValue(const String &xml, const String &tag) {
  String openTag = "<" + tag + ">";
  String closeTag = "</" + tag + ">";
  int start = xml.indexOf(openTag);
  if (start < 0) return "";
  start += openTag.length();
  int end = xml.indexOf(closeTag, start);
  if (end < 0) return "";
  return xml.substring(start, end);
}

// Funkcja do URL-encodowania stringĂłw (uĹĽywana przy QRZ)
static String urlEncode(const String& value) {
  String encoded = "";
  char c;
  for (size_t i = 0; i < value.length(); i++) {
    c = value.charAt(i);
    if (c == ' ') {
      encoded += "%20";
    } else if (c == '!') {
      encoded += "%21";
    } else if (c == '"') {
      encoded += "%22";
    } else if (c == '#') {
      encoded += "%23";
    } else if (c == '$') {
      encoded += "%24";
    } else if (c == '%') {
      encoded += "%25";
    } else if (c == '&') {
      encoded += "%26";
    } else if (c == '\'') {
      encoded += "%27";
    } else if (c == '(') {
      encoded += "%28";
    } else if (c == ')') {
      encoded += "%29";
    } else if (c == '*') {
      encoded += "%2A";
    } else if (c == '+') {
      encoded += "%2B";
    } else if (c == ',') {
      encoded += "%2C";
    } else if (c == '/') {
      encoded += "%2F";
    } else if (c == ':') {
      encoded += "%3A";
    } else if (c == ';') {
      encoded += "%3B";
    } else if (c == '<') {
      encoded += "%3C";
    } else if (c == '=') {
      encoded += "%3D";
    } else if (c == '>') {
      encoded += "%3E";
    } else if (c == '?') {
      encoded += "%3F";
    } else if (c == '@') {
      encoded += "%40";
    } else if (c == '[') {
      encoded += "%5B";
    } else if (c == '\\') {
      encoded += "%5C";
    } else if (c == ']') {
      encoded += "%5D";
    } else if (c == '^') {
      encoded += "%5E";
    } else if (c == '_') {
      encoded += "%5F";
    } else if (c == '`') {
      encoded += "%60";
    } else if (c == '{') {
      encoded += "%7B";
    } else if (c == '|') {
      encoded += "%7C";
    } else if (c == '}') {
      encoded += "%7D";
    } else if (c == '.') {
      encoded += "%2E";
    } else if (c == '-') {
      encoded += "%2D";
    } else if (c < '0' || (c > '9' && c < 'A') || (c > 'Z' && c < 'a') || (c > 'z' && c < 127)) {
      // Kodowanie wszystkich innych znakĂłw specjalnych i znakĂłw > 127 (UTF-8)
      char hex[4];
      snprintf(hex, sizeof(hex), "%%%02X", (unsigned char)c);
      encoded += hex;
    } else {
      encoded += c;
    }
  }
  return encoded;
}

bool ensureQrzSession(String &sessionKey) {
  static String cachedKey = "";
  static unsigned long cachedAt = 0;
  const unsigned long SESSION_TTL_MS = 30UL * 60UL * 1000UL; // 30 minut

  if (qrzUsername.length() == 0 || qrzPassword.length() == 0) {
    qrzStatus = "QRZ: no credentials";
    return false;
  }

  unsigned long now = millis();
  if (cachedKey.length() > 0 && (now - cachedAt) < SESSION_TTL_MS) {
    sessionKey = cachedKey;
    qrzStatus = "QRZ: session ok";
    return true;
  }

  String url = "https://xmldata.qrz.com/xml/current/?username=" + urlEncode(qrzUsername) +
               ";password=" + urlEncode(qrzPassword);
  
  // Debug log - pokaĹĽ URL z zamaskowanym hasĹ‚em
  String debugUrl = "https://xmldata.qrz.com/xml/current/?username=" + urlEncode(qrzUsername) +
                    ";password=*** (len=" + String(qrzPassword.length()) + ")";
  Serial.println("[QRZ DEBUG] URL: " + debugUrl);
  Serial.println("[QRZ DEBUG] Username: " + qrzUsername + " (len=" + String(qrzUsername.length()) + ")");
  Serial.println("[QRZ DEBUG] Pass len: " + String(qrzPassword.length()));
  
  // PokaĹĽ pierwsze i ostatnie 3 znaki zakodowanego hasĹ‚a (dla weryfikacji)
  String encodedPass = urlEncode(qrzPassword);
  String passPreview = encodedPass.substring(0, 6) + "..." + encodedPass.substring(encodedPass.length()-6);
  Serial.println("[QRZ DEBUG] Encoded pass preview: " + passPreview + " (total len=" + String(encodedPass.length()) + ")");
  
  HTTPClient http;
  http.setTimeout(3000);
  static WiFiClientSecure qrzSessionClient;
  qrzSessionClient.setInsecure();
  http.begin(qrzSessionClient, url);
  int code = http.GET();
  if (code != 200) {
    http.end();
    qrzStatus = "QRZ: login http " + String(code);
    return false;
  }
  String body = http.getString();
  http.end();

  String err = extractXmlTagValue(body, "Error");
  if (err.length() == 0) {
    err = extractXmlTagValue(body, "error");
  }

  String key = extractXmlTagValue(body, "Key");
  if (key.length() == 0) {
    key = extractXmlTagValue(body, "key");
  }
  if (key.length() == 0) {
    if (err.length() > 0) {
      qrzStatus = "QRZ: login " + err;
      Serial.print("[QRZ] Login error: ");
      Serial.println(err);
    } else {
      qrzStatus = "QRZ: login failed";
      Serial.println("[QRZ] Login failed - no session key and no error message");
      Serial.println("[QRZ] Response body (first 500 chars):");
      Serial.println(body.substring(0, 500));
    }
    return false;
  }

  cachedKey = key;
  cachedAt = now;
  sessionKey = key;
  qrzStatus = "QRZ: login ok";
  Serial.println("[QRZ] Session OK");
  return true;
}

// ========== CALLOOK.INFO API (Free alternative to QRZ) ==========
bool fetchCallookCallsignInfo(const String &callsign, String &outGrid, String &outCountry,
                               String &outName, String &outEmail, String &outQth, double &outLat, double &outLon, bool &outHasLatLon) {
  if (!wifiConnected || WiFi.status() != WL_CONNECTED) {
    return false;
  }

  outGrid = "";
  outCountry = "";
  outName = "";
  outEmail = "";
  outQth = "";
  outLat = 0.0;
  outLon = 0.0;
  outHasLatLon = false;

  String call = callsign;
  call.toUpperCase();
  call.trim();
  if (call.length() == 0) return false;

  // Check cache first
  unsigned long now = millis();
  for (int i = 0; i < QRZ_CACHE_SIZE; i++) {
    if (qrzCache[i].callsign == call && (now - qrzCache[i].fetchedAtMs) < QRZ_CACHE_TTL_MS) {
      outGrid = qrzCache[i].grid;
      outCountry = qrzCache[i].country;
      outName = qrzCache[i].name;
      outEmail = qrzCache[i].email;
      outQth = qrzCache[i].qth;
      outLat = qrzCache[i].lat;
      outLon = qrzCache[i].lon;
      outHasLatLon = qrzCache[i].hasLatLon;
      return (outGrid.length() > 0 || outCountry.length() > 0 || outName.length() > 0);
    }
  }

  String url = "https://callook.info/" + call + "/json";
  static WiFiClientSecure callookClient;
  callookClient.setInsecure();
  HTTPClient http;
  http.setTimeout(5000);
  http.begin(callookClient, url);
  int code = http.GET();
  if (code != 200) {
    http.end();
    Serial.print("[CALLOOK] HTTP error: ");
    Serial.println(code);
    // 404 = callook nie zna znaku (dla nie-US to norma). Zapamietaj negatywnie,
    // inaczej kolejka QRZ pyta o ten sam znak w kolko (burza 404 w logu).
    if (code == 404) writeQrzNegativeCache(call);
    return false;
  }
  String body = http.getString();
  http.end();

  // Parse JSON response
  DynamicJsonDocument doc(1024);
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.print("[CALLOOK] JSON parse error: ");
    Serial.println(err.c_str());
    return false;
  }

  // Check if callsign exists
  if (doc["status"] == "INVALID") {
    Serial.println("[CALLOOK] Callsign not found");
    writeQrzNegativeCache(call);
    return false;
  }

  // Extract data
  const char* name = doc["name"];
  if (name) outName = String(name);
  
  const char* grid = doc["location"]["gridsquare"];
  if (grid) outGrid = String(grid);
  
  const char* country = doc["address"]["country"];
  if (country) outCountry = String(country);
  
  // Build QTH from address line2 (format: "CITY, STATE ZIP")
  const char* addressLine2 = doc["address"]["line2"];
  if (addressLine2) {
    String addr = String(addressLine2);
    // Extract city and state from "CITY, STATE ZIP" format
    int commaPos = addr.indexOf(',');
    if (commaPos > 0) {
      String city = addr.substring(0, commaPos);
      String stateZip = addr.substring(commaPos + 1);
      stateZip.trim();
      // Extract just state (2 letters before space or zip)
      int spacePos = stateZip.indexOf(' ');
      String state = (spacePos > 0) ? stateZip.substring(0, spacePos) : stateZip;
      outQth = city + ", " + state;
    } else {
      outQth = addr;
    }
  }

  // Get coordinates
  float lat = doc["location"]["latitude"] | 0.0f;
  float lon = doc["location"]["longitude"] | 0.0f;
  if (lat != 0.0f && lon != 0.0f) {
    outLat = lat;
    outLon = lon;
    outHasLatLon = true;
  }

  // Save to cache
  int oldestIdx = 0;
  unsigned long oldestMs = qrzCache[0].fetchedAtMs;
  for (int i = 1; i < QRZ_CACHE_SIZE; i++) {
    if (qrzCache[i].fetchedAtMs < oldestMs) {
      oldestMs = qrzCache[i].fetchedAtMs;
      oldestIdx = i;
    }
  }
  qrzCache[oldestIdx].callsign = call;
  qrzCache[oldestIdx].grid = outGrid;
  qrzCache[oldestIdx].country = outCountry;
  qrzCache[oldestIdx].name = outName;
  qrzCache[oldestIdx].email = outEmail;
  qrzCache[oldestIdx].qth = outQth;
  qrzCache[oldestIdx].lat = (float)outLat;
  qrzCache[oldestIdx].lon = (float)outLon;
  qrzCache[oldestIdx].hasLatLon = outHasLatLon;
  qrzCache[oldestIdx].notFound = false;
  qrzCache[oldestIdx].fetchedAtMs = now;

  Serial.print("[CALLOOK] Fetched: ");
  Serial.print(call);
  Serial.print(" - Name: '");
  Serial.print(outName);
  Serial.print("', Grid: '");
  Serial.print(outGrid);
  Serial.print("', QTH: '");
  Serial.print(outQth);
  Serial.println("'");

  return (outGrid.length() > 0 || outCountry.length() > 0 || outName.length() > 0);
}

// ========== HAMQTH.COM API (Free, global) ==========
// Dodaj mapowanie prefiksu na przybliĹĽony lokalizator
String getApproximateGridFromCallsign(const String &callsign) {
  if (callsign.length() < 2) return "";
  
  // Mapowanie prefiksĂłw na przybliĹĽone lokatory (centrum kraju)
  String prefix = callsign.substring(0, 2);
  prefix.toUpperCase();
  
  // Europa
  if (callsign.startsWith("SP")) return "JO92ii";      // Poland
  if (callsign.startsWith("SQ")) return "JO92ii";      // Poland
  if (callsign.startsWith("SO")) return "JO92ii";    // Poland
  if (callsign.startsWith("HF")) return "JO92ii";    // Poland
  if (callsign.startsWith("SR")) return "JO92ii";    // Poland
  if (callsign.startsWith("SN")) return "JO92ii";    // Poland
  if (callsign.startsWith("3Z")) return "JO92ii";    // Poland
  if (callsign.startsWith("DL")) return "JO62qm";    // Germany
  if (callsign.startsWith("DO")) return "JO62qm";    // Germany
  if (callsign.startsWith("DA")) return "JO62qm";    // Germany
  if (callsign.startsWith("G"))  return "IO91vl";    // England
  if (callsign.startsWith("M"))  return "IO91vl";    // England
  if (callsign.startsWith("2E")) return "IO91vl";    // England
  if (callsign.startsWith("GM")) return "IO86je";    // Scotland
  if (callsign.startsWith("GW")) return "IO81fm";    // Wales
  if (callsign.startsWith("F"))  return "JN18dv";    // France
  if (callsign.startsWith("TM")) return "JN18dv";    // France
  if (callsign.startsWith("ON")) return "JO20fm";    // Belgium
  if (callsign.startsWith("PA")) return "JO22db";    // Netherlands
  if (callsign.startsWith("PB")) return "JO22db";    // Netherlands
  if (callsign.startsWith("PH")) return "JO22db";    // Netherlands
  if (callsign.startsWith("PI")) return "JO22db";    // Netherlands
  if (callsign.startsWith("HB")) return "JN46bt";    // Switzerland
  if (callsign.startsWith("HE")) return "JN46bt";    // Switzerland
  if (callsign.startsWith("OE")) return "JN77tx";    // Austria
  if (callsign.startsWith("OK")) return "JN79jw";    // Czech Republic
  if (callsign.startsWith("OL")) return "JN79jw";    // Czech Republic
  if (callsign.startsWith("OM")) return "JN88ss";    // Slovakia
  if (callsign.startsWith("HA")) return "JN97nm";    // Hungary
  if (callsign.startsWith("SV")) return "KM17uw";    // Greece
  if (callsign.startsWith("5B")) return "KM64ft";    // Cyprus
  if (callsign.startsWith("C3")) return "JN02tw";    // Andorra
  if (callsign.startsWith("CT")) return "IM57nh";    // Portugal
  if (callsign.startsWith("CU")) return "IM57nh";    // Portugal
  if (callsign.startsWith("EA")) return "IN80dj";    // Spain
  if (callsign.startsWith("EB")) return "IN80dj";    // Spain
  if (callsign.startsWith("EC")) return "IN80dj";    // Spain
  if (callsign.startsWith("ED")) return "IN80dj";    // Spain
  if (callsign.startsWith("EI")) return "IO63ci";    // Ireland
  if (callsign.startsWith("EJ")) return "IO63ci";    // Ireland
  if (callsign.startsWith("EK")) return "LM28cx";    // Armenia
  if (callsign.startsWith("EL")) return "JJ06px";    // Liberia
  if (callsign.startsWith("ER")) return "KN37bs";    // Moldova
  if (callsign.startsWith("ES")) return "KO29ih";    // Estonia
  if (callsign.startsWith("ET")) return "JM55bt";    // Ethiopia
  if (callsign.startsWith("EU")) return "JO34qd";    // Belarus
  if (callsign.startsWith("EV")) return "KO33tw";    // Latvia
  if (callsign.startsWith("EW")) return "KO43lb";    // Lithuania
  if (callsign.startsWith("EX")) return "MN52ct";    // Kyrgyzstan
  if (callsign.startsWith("EY")) return "MM47nx";    // Tajikistan
  if (callsign.startsWith("EZ")) return "LM49qp";    // Turkmenistan
  if (callsign.startsWith("I"))  return "JN62ra";    // Italy
  if (callsign.startsWith("IS")) return "JN62ra";    // Italy (Sardinia)
  if (callsign.startsWith("IZ")) return "JN62ra";    // Italy
  if (callsign.startsWith("IT")) return "JN62ra";    // Italy
  if (callsign.startsWith("LA")) return "JO59jp";    // Norway
  if (callsign.startsWith("LB")) return "JO59jp";    // Norway
  if (callsign.startsWith("LC")) return "JO59jp";    // Norway
  if (callsign.startsWith("LD")) return "JO59jp";    // Norway
  if (callsign.startsWith("LE")) return "JO59jp";    // Norway
  if (callsign.startsWith("LF")) return "JO59jp";    // Norway
  if (callsign.startsWith("LG")) return "JO59jp";    // Norway
  if (callsign.startsWith("LH")) return "JO59jp";    // Norway
  if (callsign.startsWith("LI")) return "JO59jp";    // Norway
  if (callsign.startsWith("LJ")) return "JO59jp";    // Norway
  if (callsign.startsWith("LK")) return "JO59jp";    // Norway
  if (callsign.startsWith("LL")) return "JO59jp";    // Norway
  if (callsign.startsWith("LM")) return "JO59jp";    // Norway
  if (callsign.startsWith("LN")) return "JO59jp";    // Norway
  if (callsign.startsWith("LZ")) return "KN12pr";    // Bulgaria
  if (callsign.startsWith("S5")) return "JN75cv";    // Slovenia
  if (callsign.startsWith("S7")) return "LL38on";    // Seychelles
  if (callsign.startsWith("S9")) return "JI12hf";    // Sao Tome
  if (callsign.startsWith("SA")) return "JP80jq";    // Sweden
  if (callsign.startsWith("SB")) return "JP80jq";    // Sweden
  if (callsign.startsWith("SC")) return "JP80jq";    // Sweden
  if (callsign.startsWith("SD")) return "JP80jq";    // Sweden
  if (callsign.startsWith("SE")) return "JP80jq";    // Sweden
  if (callsign.startsWith("SF")) return "JP80jq";    // Sweden
  if (callsign.startsWith("SG")) return "JP80jq";    // Sweden
  if (callsign.startsWith("SH")) return "JP80jq";    // Sweden
  if (callsign.startsWith("SI")) return "JP80jq";    // Sweden
  if (callsign.startsWith("SJ")) return "JP80jq";    // Sweden
  if (callsign.startsWith("SK")) return "JP80jq";    // Sweden
  if (callsign.startsWith("SL")) return "JP80jq";    // Sweden
  if (callsign.startsWith("SM")) return "JP80jq";    // Sweden
  if (callsign.startsWith("SN")) return "JO92ii";    // Poland
  if (callsign.startsWith("TA")) return "KM69kw";    // Turkey
  if (callsign.startsWith("TB")) return "KM69kw";    // Turkey
  if (callsign.startsWith("TC")) return "KM69kw";    // Turkey
  if (callsign.startsWith("TD")) return "KM69kw";    // Turkey
  if (callsign.startsWith("TF")) return "HP94mf";    // Iceland
  if (callsign.startsWith("TG")) return "EK44qm";    // Guatemala
  if (callsign.startsWith("TI")) return "EK70wf";    // Costa Rica
  if (callsign.startsWith("TJ")) return "JJ53rm";    // Cameroon
  if (callsign.startsWith("TK")) return "JN43ji";    // Corsica
  if (callsign.startsWith("TL")) return "JJ48qx";    // Central Africa
  if (callsign.startsWith("TN")) return "JN53dl";    // Congo
  if (callsign.startsWith("TR")) return "JJ47nm";    // Gabon
  if (callsign.startsWith("TT")) return "JJ66af";    // Chad
  if (callsign.startsWith("TU")) return "IJ85ml";    // Ivory Coast
  if (callsign.startsWith("TY")) return "JJ16aa";    // Benin
  if (callsign.startsWith("TZ")) return "IJ52ei";    // Mali
  if (callsign.startsWith("UA")) return "KO85ss";    // Russia (European)
  if (callsign.startsWith("UB")) return "KO85ss";    // Russia
  if (callsign.startsWith("UC")) return "KO85ss";    // Russia
  if (callsign.startsWith("UD")) return "KO85ss";    // Russia
  if (callsign.startsWith("UE")) return "KO85ss";    // Russia
  if (callsign.startsWith("UF")) return "KO85ss";    // Russia
  if (callsign.startsWith("UG")) return "KO85ss";    // Russia
  if (callsign.startsWith("UH")) return "KO85ss";    // Russia
  if (callsign.startsWith("UI")) return "KO85ss";    // Russia
  if (callsign.startsWith("UJ")) return "KO85ss";    // Russia
  if (callsign.startsWith("UK")) return "KO85ss";    // Russia
  if (callsign.startsWith("UL")) return "KO85ss";    // Russia
  if (callsign.startsWith("UM")) return "KO85ss";    // Russia
  if (callsign.startsWith("UN")) return "KO85ss";    // Russia
  if (callsign.startsWith("UO")) return "KO85ss";    // Russia
  if (callsign.startsWith("UP")) return "KO85ss";    // Russia
  if (callsign.startsWith("UQ")) return "KO85ss";    // Russia
  if (callsign.startsWith("UR")) return "KO50dm";    // Ukraine
  if (callsign.startsWith("US")) return "KO50dm";    // Ukraine
  if (callsign.startsWith("UT")) return "KO50dm";    // Ukraine
  if (callsign.startsWith("UU")) return "KO50dm";    // Ukraine
  if (callsign.startsWith("UV")) return "KO50dm";    // Ukraine
  if (callsign.startsWith("UW")) return "KO50dm";    // Ukraine
  if (callsign.startsWith("UX")) return "KO50dm";    // Ukraine
  if (callsign.startsWith("UY")) return "KO50dm";    // Ukraine
  if (callsign.startsWith("UZ")) return "KO50dm";    // Ukraine
  if (callsign.startsWith("YU")) return "JN94sd";    // Serbia
  if (callsign.startsWith("YT")) return "JN94sd";    // Serbia
  if (callsign.startsWith("YZ")) return "KN05fx";    // Macedonia
  if (callsign.startsWith("ZA")) return "JM99ah";    // Albania
  if (callsign.startsWith("ZB")) return "JM99ah";    // Albania
  if (callsign.startsWith("ZC")) return "JM99ah";    // Albania
  if (callsign.startsWith("ZD")) return "JM99ah";    // Albania
  if (callsign.startsWith("ZE")) return "JM99ah";    // Albania
  if (callsign.startsWith("Y2")) return "FM07vx";    // Germany (V Rhodes)
  if (callsign.startsWith("Y3")) return "FM07vx";    // Germany (V Rhodes)
  if (callsign.startsWith("Y4")) return "FM07vx";    // Germany (V Rhodes)
  if (callsign.startsWith("Y5")) return "FM07vx";    // Germany (V Rhodes)
  
  // USA / Kanada (Callook dziaĹ‚a, ale dla pewnoĹ›ci)
  if (callsign.startsWith("K"))  return "EM48to";    // USA (rough center)
  if (callsign.startsWith("N"))  return "EM48to";    // USA
  if (callsign.startsWith("W"))  return "EM48to";    // USA
  if (callsign.startsWith("AA")) return "EM48to";    // USA
  if (callsign.startsWith("AB")) return "EM48to";    // USA
  if (callsign.startsWith("AC")) return "EM48to";    // USA
  if (callsign.startsWith("AD")) return "EM48to";    // USA
  if (callsign.startsWith("AE")) return "EM48to";    // USA
  if (callsign.startsWith("AF")) return "EM48to";    // USA
  if (callsign.startsWith("AG")) return "EM48to";    // USA
  if (callsign.startsWith("AH")) return "EM48to";    // USA
  if (callsign.startsWith("AI")) return "EM48to";    // USA
  if (callsign.startsWith("AJ")) return "EM48to";    // USA
  if (callsign.startsWith("AK")) return "EM48to";    // USA
  if (callsign.startsWith("AL")) return "EM48to";    // USA
  if (callsign.startsWith("VE")) return "FN25bh";    // Canada
  if (callsign.startsWith("VA")) return "FN25bh";    // Canada
  if (callsign.startsWith("VY")) return "FN25bh";    // Canada
  if (callsign.startsWith("VO")) return "FN25bh";    // Canada
  
  return ""; // Nieznany prefiks
}

bool fetchHamQthCallsignInfo(const String &callsign, String &outGrid, String &outCountry,
                               String &outName, String &outEmail, String &outQth, double &outLat, double &outLon, bool &outHasLatLon) {
  if (!wifiConnected || WiFi.status() != WL_CONNECTED) {
    return false;
  }

  outGrid = "";
  outCountry = "";
  outName = "";
  outEmail = "";
  outQth = "";
  outLat = 0.0;
  outLon = 0.0;
  outHasLatLon = false;

  String call = callsign;
  call.toUpperCase();
  call.trim();
  if (call.length() == 0) return false;

  // Check cache first
  unsigned long now = millis();
  for (int i = 0; i < QRZ_CACHE_SIZE; i++) {
    if (qrzCache[i].callsign == call && (now - qrzCache[i].fetchedAtMs) < QRZ_CACHE_TTL_MS) {
      outGrid = qrzCache[i].grid;
      outCountry = qrzCache[i].country;
      outName = qrzCache[i].name;
      outEmail = qrzCache[i].email;
      outQth = qrzCache[i].qth;
      outLat = qrzCache[i].lat;
      outLon = qrzCache[i].lon;
      outHasLatLon = qrzCache[i].hasLatLon;
      return (outGrid.length() > 0 || outCountry.length() > 0 || outName.length() > 0);
    }
  }

  // HamQTH API - no login required for basic info
  String url = "https://www.hamqth.com/dxcc_json.php?callsign=" + call + "&apikey=ESP32HAMCLOCK";
  static WiFiClientSecure hamqthClient;
  hamqthClient.setInsecure();
  HTTPClient http;
  http.setTimeout(5000);
  http.begin(hamqthClient, url);
  int code = http.GET();
  if (code != 200) {
    http.end();
    Serial.print("[HAMQTH] HTTP error: ");
    Serial.println(code);
    return false;
  }
  String body = http.getString();
  http.end();

  // Parse JSON response
  DynamicJsonDocument doc(1024);
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.print("[HAMQTH] JSON parse error: ");
    Serial.println(err.c_str());
    return false;
  }

  // Check if callsign found
  const char* error = doc["error"];
  if (error) {
    Serial.print("[HAMQTH] Error: ");
    Serial.println(error);
    return false;
  }

  // Extract data
  const char* name = doc["name"];
  if (name) outName = String(name);
  
  const char* grid = doc["grid"];
  if (grid) outGrid = String(grid);
  
  const char* country = doc["country"];
  if (country) outCountry = String(country);
  
  const char* qth = doc["qth"];
  if (qth) outQth = String(qth);
  
  // Get coordinates from grid if available
  if (outGrid.length() >= 4) {
    locatorToLatLon(outGrid, outLat, outLon);
    outHasLatLon = true;
  } else {
    // Brak grid z API - sprĂłbuj przybliĹĽony z prefiksu
    String approxGrid = getApproximateGridFromCallsign(call);
    if (approxGrid.length() >= 4) {
      outGrid = approxGrid + " (approx)";
      locatorToLatLon(approxGrid, outLat, outLon);
      outHasLatLon = true;
      Serial.print("[HAMQTH] Using approximate grid from prefix: ");
      Serial.println(approxGrid);
    }
  }

  // Save to cache
  int oldestIdx = 0;
  unsigned long oldestMs = qrzCache[0].fetchedAtMs;
  for (int i = 1; i < QRZ_CACHE_SIZE; i++) {
    if (qrzCache[i].fetchedAtMs < oldestMs) {
      oldestMs = qrzCache[i].fetchedAtMs;
      oldestIdx = i;
    }
  }
  qrzCache[oldestIdx].callsign = call;
  qrzCache[oldestIdx].grid = outGrid;
  qrzCache[oldestIdx].country = outCountry;
  qrzCache[oldestIdx].name = outName;
  qrzCache[oldestIdx].email = outEmail;
  qrzCache[oldestIdx].qth = outQth;
  qrzCache[oldestIdx].lat = (float)outLat;
  qrzCache[oldestIdx].lon = (float)outLon;
  qrzCache[oldestIdx].hasLatLon = outHasLatLon;
  qrzCache[oldestIdx].notFound = false;
  qrzCache[oldestIdx].fetchedAtMs = now;

  Serial.print("[HAMQTH] Fetched: ");
  Serial.print(call);
  Serial.print(" - Name: '");
  Serial.print(outName);
  Serial.print("', Grid: '");
  Serial.print(outGrid);
  Serial.print("', Country: '");
  Serial.print(outCountry);
  Serial.println("'");

  return (outGrid.length() > 0 || outCountry.length() > 0 || outName.length() > 0);
}

bool fetchQrzCallsignInfo(const String &callsign, String &outGrid, String &outCountry,
                          String &outName, String &outEmail, String &outQth, double &outLat, double &outLon, bool &outHasLatLon) {
  if (!wifiConnected || WiFi.status() != WL_CONNECTED) {
    qrzStatus = "QRZ: no wifi";
    return false;
  }

  outGrid = "";
  outCountry = "";
  outName = "";
  outEmail = "";
  outQth = "";
  outLat = 0.0;
  outLon = 0.0;
  outHasLatLon = false;

  String call = callsign;
  call.toUpperCase();
  unsigned long now = millis();
  for (int i = 0; i < QRZ_CACHE_SIZE; i++) {
    if (qrzCache[i].callsign == call && (now - qrzCache[i].fetchedAtMs) < QRZ_CACHE_TTL_MS) {
      outGrid = qrzCache[i].grid;
      outCountry = qrzCache[i].country;
      outName = qrzCache[i].name;
      outEmail = qrzCache[i].email;
      outQth = qrzCache[i].qth;
      outLat = qrzCache[i].lat;
      outLon = qrzCache[i].lon;
      outHasLatLon = qrzCache[i].hasLatLon;
      return (outGrid.length() > 0 || outCountry.length() > 0 || outName.length() > 0);
    }
  }

  String sessionKey;
  if (!ensureQrzSession(sessionKey)) {
    return false;
  }

  String url = "https://xmldata.qrz.com/xml/current/?s=" + sessionKey +
               ";callsign=" + callsign;
  static WiFiClientSecure qrzLookupClient;
  qrzLookupClient.setInsecure();
  HTTPClient http;
  http.setTimeout(3000);
  http.begin(qrzLookupClient, url);
  int code = http.GET();
  if (code != 200) {
    http.end();
    qrzStatus = "QRZ: lookup http " + String(code);
    return false;
  }
  String body = http.getString();
  http.end();

  // ObsĹ‚uga bĹ‚Ä™du sesji - sprĂłbuj raz odĹ›wieĹĽyÄ‡
  if (body.indexOf("Session Timeout") >= 0 || body.indexOf("Invalid session") >= 0) {
    String dummy;
    ensureQrzSession(dummy);
    qrzStatus = "QRZ: session expired";
    return false;
  }

  String grid = extractXmlTagValue(body, "grid");
  String country = extractXmlTagValue(body, "country");
  String name = extractXmlTagValue(body, "name");
  if (name.length() == 0) {
    name = extractXmlTagValue(body, "Name");  // Capitalized variant
  }
  if (name.length() == 0) {
    name = extractXmlTagValue(body, "fname");  // First name
  }
  if (name.length() == 0) {
    name = extractXmlTagValue(body, "Fname");  // Capitalized first name
  }
  String email = extractXmlTagValue(body, "email");
  if (email.length() == 0) {
    email = extractXmlTagValue(body, "Email");  // Capitalized variant
  }
  String qth = extractXmlTagValue(body, "qth");
  if (qth.length() == 0) {
    qth = extractXmlTagValue(body, "Qth");  // Capitalized variant
    if (qth.length() == 0) {
      qth = extractXmlTagValue(body, "QTH");  // All caps variant
    }
  }
  String latStr = extractXmlTagValue(body, "lat");
  String lonStr = extractXmlTagValue(body, "lon");
  grid.trim();
  country.trim();
  name.trim();
  if (country.length() == 0) {
    country = extractXmlTagValue(body, "dxcc");
    country.trim();
  }

  outGrid = grid;
  outCountry = country;
  outName = name;
  outEmail = email;
  outQth = qth;
  
  // Debug log
  Serial.print("[QRZ DEBUG] Name for ");
  Serial.print(callsign);
  Serial.print(": '");
  Serial.print(name);
  Serial.println("'");
  
  latStr.trim();
  lonStr.trim();
  outHasLatLon = (latStr.length() > 0 && lonStr.length() > 0);
  if (outHasLatLon) {
    outLat = latStr.toDouble();
    outLon = lonStr.toDouble();
  } else if (grid.length() >= 4) {
    // JeÄąâ€şli nie ma lat/lon z QRZ ale mamy lokator, przekonwertuj na wspÄ‚Ĺ‚Äąâ€šrzĂ„â„˘dne
    double latFromGrid = 0.0, lonFromGrid = 0.0;
    locatorToLatLon(grid, latFromGrid, lonFromGrid);
    if (latFromGrid != 0.0 || lonFromGrid != 0.0) {
      outLat = latFromGrid;
      outLon = lonFromGrid;
      outHasLatLon = true;
      Serial.print("[QRZ DEBUG] Converted grid ");
      Serial.print(grid);
      Serial.print(" to lat=");
      Serial.print(outLat, 6);
      Serial.print(" lon=");
      Serial.println(outLon, 6);
    } else {
      outLat = 0.0;
      outLon = 0.0;
    }
  } else {
    outLat = 0.0;
    outLon = 0.0;
  }
  qrzStatus = "QRZ: lookup ok";

  // Zapisz do cache (nadpisz najstarszy)
  int oldestIdx = 0;
  unsigned long oldestMs = qrzCache[0].fetchedAtMs;
  for (int i = 1; i < QRZ_CACHE_SIZE; i++) {
    if (qrzCache[i].fetchedAtMs < oldestMs) {
      oldestMs = qrzCache[i].fetchedAtMs;
      oldestIdx = i;
    }
  }
  qrzCache[oldestIdx].callsign = call;
  qrzCache[oldestIdx].grid = grid;
  qrzCache[oldestIdx].country = country;
  qrzCache[oldestIdx].name = name;
  qrzCache[oldestIdx].email = email;
  qrzCache[oldestIdx].qth = qth;
  qrzCache[oldestIdx].lat = (float)outLat;
  qrzCache[oldestIdx].lon = (float)outLon;
  qrzCache[oldestIdx].hasLatLon = outHasLatLon;
  qrzCache[oldestIdx].notFound = false;
  qrzCache[oldestIdx].fetchedAtMs = now;

  return (grid.length() > 0 || country.length() > 0 || name.length() > 0);
}

bool getQrzCacheFresh(const String &callsign, String &outGrid, String &outCountry, String &outName,
                     String &outEmail, String &outQth, double &outLat, double &outLon, bool &outHasLatLon,
                     unsigned long ttlMs) {
  outGrid = "";
  outCountry = "";
  outName = "";
  outEmail = "";
  outQth = "";
  outLat = 0.0;
  outLon = 0.0;
  outHasLatLon = false;

  String call = callsign;
  call.toUpperCase();
  unsigned long now = millis();
  for (int i = 0; i < QRZ_CACHE_SIZE; i++) {
    if (qrzCache[i].callsign == call && (now - qrzCache[i].fetchedAtMs) < ttlMs) {
      outGrid = qrzCache[i].grid;
      outCountry = qrzCache[i].country;
      outName = qrzCache[i].name;
      outEmail = qrzCache[i].email;
      outQth = qrzCache[i].qth;
      outLat = qrzCache[i].lat;
      outLon = qrzCache[i].lon;
      outHasLatLon = qrzCache[i].hasLatLon;
      return (outGrid.length() > 0 || outCountry.length() > 0 || outName.length() > 0);
    }
  }
  return false;
}

// Czy w cache jest swiezy wpis negatywny ("API nie zna tego znaku")?
bool isQrzCacheKnownMissing(const String &callsign, unsigned long ttlMs) {
  String call = callsign;
  call.toUpperCase();
  unsigned long now = millis();
  for (int i = 0; i < QRZ_CACHE_SIZE; i++) {
    if (qrzCache[i].notFound && qrzCache[i].callsign == call &&
        (now - qrzCache[i].fetchedAtMs) < ttlMs) {
      return true;
    }
  }
  return false;
}

// Zapisz wpis negatywny (znak nieznany / 404), zeby nie pytac o niego w kolko.
void writeQrzNegativeCache(const String &callsign) {
  String call = callsign;
  call.toUpperCase();
  unsigned long now = millis();
  int idx = -1;
  for (int i = 0; i < QRZ_CACHE_SIZE; i++) {
    if (qrzCache[i].callsign == call) { idx = i; break; }
  }
  if (idx < 0) {
    unsigned long oldestMs = 0xFFFFFFFFUL;
    idx = 0;
    for (int i = 0; i < QRZ_CACHE_SIZE; i++) {
      if (qrzCache[i].fetchedAtMs < oldestMs) { oldestMs = qrzCache[i].fetchedAtMs; idx = i; }
    }
  }
  qrzCache[idx].callsign = call;
  qrzCache[idx].grid = "";
  qrzCache[idx].country = "";
  qrzCache[idx].name = "";
  qrzCache[idx].email = "";
  qrzCache[idx].qth = "";
  qrzCache[idx].lat = 0.0f;
  qrzCache[idx].lon = 0.0f;
  qrzCache[idx].hasLatLon = false;
  qrzCache[idx].notFound = true;
  qrzCache[idx].fetchedAtMs = now;
}

void applyQrzCacheToSpot(DXSpot &spot, unsigned long ttlMs) {
  if (spot.callsign.length() == 0) {
    return;
  }

  String grid;
  String country;
  String name;
  String email;
  String qth;
  double lat = 0.0;
  double lon = 0.0;
  bool hasLatLon = false;
  if (!getQrzCacheFresh(spot.callsign, grid, country, name, email, qth, lat, lon, hasLatLon, ttlMs)) {
    return;
  }

  if (spot.country.length() == 0 && country.length() > 0) {
    spot.country = country;
  }
  if (spot.name.length() == 0 && name.length() > 0) {
    spot.name = name;
  }
  if (spot.locator.length() < 4 && grid.length() >= 4) {
    spot.locator = grid;
  }
  if (!spot.hasLatLon && hasLatLon) {
    spot.lat = (float)lat;
    spot.lon = (float)lon;
    spot.hasLatLon = true;
  }
}

void logQrzAllFields(const String &callsign, const String &xml) {
  int csStart = xml.indexOf("<Callsign>");
  int csEnd = xml.indexOf("</Callsign>");
  if (csStart < 0 || csEnd < 0 || csEnd <= csStart) {
    Serial.print("[QRZ][DATA] ");
    Serial.print(callsign);
    Serial.println(" - brak CallSign w XML");
    return;
  }
  String section = xml.substring(csStart + 10, csEnd);

  Serial.print("[QRZ][DATA] ");
  Serial.println(callsign);

  int pos = 0;
  while (pos < section.length()) {
    int openTagStart = section.indexOf('<', pos);
    if (openTagStart < 0) break;
    int openTagEnd = section.indexOf('>', openTagStart + 1);
    if (openTagEnd < 0) break;

    String tag = section.substring(openTagStart + 1, openTagEnd);
    tag.trim();
    if (tag.length() == 0 || tag.indexOf('/') == 0) {
      pos = openTagEnd + 1;
      continue;
    }
    int closeTagStart = section.indexOf("</" + tag + ">", openTagEnd + 1);
    if (closeTagStart < 0) {
      pos = openTagEnd + 1;
      continue;
    }
    String value = section.substring(openTagEnd + 1, closeTagStart);
    value.trim();
    if (value.length() > 0) {
      Serial.print("  ");
      Serial.print(tag);
      Serial.print(": ");
      Serial.println(value);
    }
    pos = closeTagStart + tag.length() + 3;
  }
}

void logSpotList() {
  Serial.println("[SPOTS] Lista spotow (max 8)");
  int count = min(spotCount, 8);
  for (int i = 0; i < count; i++) {
    String call = spots[i].callsign.length() ? spots[i].callsign : "-";
    String timeStr = spots[i].time.length() ? spots[i].time : "----Z";
    float mhz = spots[i].frequency / 1000.0f;
    Serial.print(" ");
    Serial.print(i + 1);
    Serial.print(") ");
    Serial.print(call);
    Serial.print(" ");
    Serial.print(timeStr);
    Serial.print(" ");
    Serial.print(mhz, 3);
    Serial.println(" MHz");

    double lat = 0.0;
    double lon = 0.0;
    bool hasLatLon = spots[i].hasLatLon;
    if (hasLatLon) {
      lat = spots[i].lat;
      lon = spots[i].lon;
    } else if (spots[i].locator.length() >= 4) {
      locatorToLatLon(spots[i].locator, lat, lon);
      hasLatLon = true;
    }

    if (hasLatLon) {
      Serial.print("    lat=");
      Serial.print(lat, 4);
      Serial.print(" lon=");
      Serial.println(lon, 4);
    } else {
      Serial.println("    lat=-- lon=--");
    }

    if (spots[i].distance > 0) {
      Serial.print("    dist=");
      Serial.print(spots[i].distance, 0);
      Serial.println(" km");
    } else {
      Serial.println("    dist=-- km");
    }
  }
}

bool isQrzQueued(const String &callsign) {
  String normalized = callsign;
  normalized.trim();
  normalized.toUpperCase();
  for (int i = 0; i < qrzQueueLen; i++) {
    String queuedCall = qrzQueue[i].callsign;
    queuedCall.toUpperCase();
    if (queuedCall == normalized) {
      return true;
    }
  }
  return false;
}

void enqueueQrzLookup(const String &callsign) {
  String normalized = callsign;
  normalized.trim();
  normalized.toUpperCase();
  if (normalized.length() == 0) {
    return;
  }
  if (qrzQueueLen >= QRZ_QUEUE_SIZE) {
    return;
  }
  if (isQrzQueued(normalized)) {
    return;
  }

  // Nie dodawaj do kolejki, jeĹĽeli mamy juĹĽ Ĺ›wieĹĽy wpis w cache ze wszystkimi danymi.
  String grid;
  String country;
  String name;
  String email;
  String qth;
  double lat = 0.0;
  double lon = 0.0;
  bool hasLatLon = false;
  if (getQrzCacheFresh(normalized, grid, country, name, email, qth, lat, lon, hasLatLon, QRZ_CACHE_TTL_MS)) {
    return;
  }
  // Nie kolejkuj znaku, ktory niedawno zwrocil "nie znaleziono" - to wlasnie
  // generowalo ciagly strumien HTTP 404 dla znakow spoza USA.
  if (isQrzCacheKnownMissing(normalized, QRZ_NEGATIVE_TTL_MS)) {
    return;
  }

  PendingQrzLookup item;
  item.callsign = normalized;
  item.nextTryMs = millis();
  item.attempts = 0;
  qrzQueue[qrzQueueLen++] = item;
}

// Pobierz spoty POTA z publicznego API i uzupeĹ‚nij bufor + kolejkÄ™ QRZ
bool fetchPotaApi() {
  if (!wifiConnected) return false;
  auto decodeToSpots = [&](JsonDocument &doc) -> bool {
    JsonArray arr;
    if (doc.is<JsonArray>()) {
      arr = doc.as<JsonArray>();
    } else if (doc["spots"].is<JsonArray>()) {
      arr = doc["spots"].as<JsonArray>();
    }
    if (arr.isNull() || arr.size() == 0) {
      return false;
    }

    potaSpotCount = 0;
    for (JsonObject spot : arr) {
      if (potaSpotCount >= MAX_POTA_SPOTS) break;
      DXSpot s;
      s.time = spot["activatorLastSpotTime"] | spot["spotTime"] | "";
      s.callsign = spot["activator"] | spot["call"] | spot["callsign"] | "";
      float freq = 0.0f;
      if (spot["frequency"].is<float>() || spot["frequency"].is<double>()) {
        freq = spot["frequency"].as<float>();
      } else if (spot["frequency"].is<int>() || spot["frequency"].is<long>()) {
        freq = (float)spot["frequency"].as<long>();
      } else if (spot["frequency"].is<const char*>() || spot["frequency"].is<String>()) {
        String fstr = spot["frequency"].as<String>();
        freq = fstr.toFloat();
      }
      if (freq > 0) {
        s.frequency = freq / 1000.0f;
      } else {
        s.frequency = 0.0f;
      }
      s.band = getBand(s.frequency);
      s.mode = spot["mode"] | "";
      s.mode.toUpperCase();
      if (s.mode.length() == 0) {
        s.mode = getMode(spot["comments"] | "");
      }
      s.country = spot["country"] | "";
      s.spotter = spot["spotter"] | "";
      s.comment = spot["comments"] | "";
      s.distance = 0;
      s.hasLatLon = false;
      applyQrzCacheToSpot(s, QRZ_CACHE_TTL_MS);
      compactDxSpotStrings(s);
      potaSpots[potaSpotCount++] = s;
      if (s.country.length() == 0 && s.callsign.length() > 0) {
        enqueueQrzLookup(s.callsign);
      }
    }
    return potaSpotCount > 0;
  };

  static WiFiClientSecure potaClient;
  potaClient.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);
  if (!http.begin(potaClient, potaApiUrl)) {
    Serial.println("[POTA] http.begin failed");
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.print("[POTA] HTTP GET failed: ");
    Serial.print(code);
    Serial.print(" - ");
    Serial.println(http.errorToString(code));
    http.end();
    return false;
  }
  int contentLength = http.getSize();
  Serial.print("[POTA] HTTP ");
  Serial.print(code);
  Serial.print(", content-length=");
  Serial.print(contentLength);
  Serial.println(" (may be -1 if chunked)");

  WiFiClient *stream = http.getStreamPtr();
  DynamicJsonDocument filter(512);
  JsonObject filterRootArray = filter[0].to<JsonObject>();
  filterRootArray["activatorLastSpotTime"] = true;
  filterRootArray["spotTime"] = true;
  filterRootArray["activator"] = true;
  filterRootArray["call"] = true;
  filterRootArray["callsign"] = true;
  filterRootArray["frequency"] = true;
  filterRootArray["mode"] = true;
  filterRootArray["country"] = true;
  filterRootArray["spotter"] = true;
  filterRootArray["comments"] = true;

  // Bufory byĹ‚y wczeĹ›niej ustawione na 200 000 i 400 000 bajtĂłw - a
  // rzeczywiste odpowiedzi POTA w logach majÄ… 8-11 KB surowego JSON-a.
  // UrzÄ…dzenie ma w sumie ~320 KB RAM, wiÄ™c 400 KB to gwarantowane
  // niepowodzenie alokacji za kaĹĽdym razem, a nawet 200 KB to ogromny,
  // niepotrzebny narzut, ktĂłry mocno przyczyniaĹ‚ siÄ™ do fragmentacji sterty
  // (i przez to do problemĂłw gdzie indziej, np. z pobieraniem pogody "po
  // jakimĹ› czasie"). 24 KB dla wersji filtrowanej to nadal ponad 2x
  // margines wzglÄ™dem obserwowanych rozmiarĂłw.
  DynamicJsonDocument doc(24576);
  DeserializationError err = deserializeJson(doc, *stream, DeserializationOption::Filter(filter));
  http.end();
  bool parsedOk = (!err && decodeToSpots(doc));
  if (!parsedOk) {
    if (err) {
      Serial.print("[POTA] Filtered JSON parse error: ");
      Serial.print(err.c_str());
      Serial.print(" (content-length was ");
      Serial.print(contentLength);
      Serial.println(")");
    }
    Serial.println("[POTA] Filtered parse empty - retrying without filter");

    if (ESP.getFreeHeap() < 80000) {
      Serial.printf("[POTA] Skip retry: free heap=%d < 80000\n", ESP.getFreeHeap());
      return false;
    }

    HTTPClient retry;
    retry.setTimeout(8000);
    if (!retry.begin(potaClient, potaApiUrl)) {
      return false;
    }
    int retryCode = retry.GET();
    if (retryCode != HTTP_CODE_OK) {
      retry.end();
      return false;
    }
    WiFiClient *retryStream = retry.getStreamPtr();
    DynamicJsonDocument fullDoc(65536);
    DeserializationError retryErr = deserializeJson(fullDoc, *retryStream);
    retry.end();
    if (retryErr) {
      Serial.print("[POTA] Fallback JSON parse error: ");
      Serial.println(retryErr.c_str());
      return false;
    }
    if (!decodeToSpots(fullDoc)) {
      Serial.println("[POTA] Fallback parse also returned 0 spots");
      return false;
    }
  }

  // Posortuj malejÄ…co po czasie (ISO string porĂłwnuje siÄ™ leksykograficznie poprawnie)
  for (int i = 0; i < potaSpotCount - 1; i++) {
    for (int j = i + 1; j < potaSpotCount; j++) {
      if (potaSpots[j].time > potaSpots[i].time) {
        DXSpot tmp = potaSpots[i];
        potaSpots[i] = potaSpots[j];
        potaSpots[j] = tmp;
      }
    }
  }

  return potaSpotCount > 0;
}

void removeQrzQueueAt(int idx) {
  if (idx < 0 || idx >= qrzQueueLen) {
    return;
  }
  for (int i = idx; i < qrzQueueLen - 1; i++) {
    qrzQueue[i] = qrzQueue[i + 1];
  }
  qrzQueueLen--;
}

void updateSpotsWithQrz(const String &callsign, const String &grid,
                        const String &country, const String &name, double lat, double lon, bool hasLatLon) {
  bool updated = false;
  bool updatedPota = false;
  if (!userLatLonValid && userLocator.length() >= 4) {
    double tmpLat = 0.0;
    double tmpLon = 0.0;
    locatorToLatLon(userLocator, tmpLat, tmpLon);
    userLat = tmpLat;
    userLon = tmpLon;
    userLatLonValid = true;
  }
  lockDxSpots();
  for (int i = 0; i < spotCount; i++) {
    if (!spots[i].callsign.equalsIgnoreCase(callsign)) {
      continue;
    }

    if (grid.length() >= 4) {
      spots[i].locator = grid;
    }
    if (country.length() > 0) {
      spots[i].country = country;
    }
    if (name.length() > 0) {
      spots[i].name = name;
    }

    double spotLat = 0.0;
    double spotLon = 0.0;
    bool spotHasLatLon = false;
    if (hasLatLon) {
      spotLat = lat;
      spotLon = lon;
      spotHasLatLon = true;
      spots[i].lat = (float)lat;
      spots[i].lon = (float)lon;
      spots[i].hasLatLon = true;
    } else if (spots[i].locator.length() >= 4) {
      locatorToLatLon(spots[i].locator, spotLat, spotLon);
      spotHasLatLon = true;
      spots[i].lat = (float)spotLat;
      spots[i].lon = (float)spotLon;
      spots[i].hasLatLon = true;
    }

    double userLatLocal = 0.0;
    double userLonLocal = 0.0;
    bool userHasLatLon = userLatLonValid;
    if (userHasLatLon) {
      userLatLocal = userLat;
      userLonLocal = userLon;
    } else if (userLocator.length() >= 4) {
      locatorToLatLon(userLocator, userLatLocal, userLonLocal);
      userHasLatLon = true;
    }

    if (userHasLatLon && spotHasLatLon) {
      spots[i].distance = calculateDistance(userLatLocal, userLonLocal, spotLat, spotLon);
      updated = true;
    }
  }
  unlockDxSpots();

  for (int i = 0; i < potaSpotCount; i++) {
    if (!potaSpots[i].callsign.equalsIgnoreCase(callsign)) {
      continue;
    }

    if (grid.length() >= 4) {
      potaSpots[i].locator = grid;
    }
    if (country.length() > 0) {
      potaSpots[i].country = country;
    }

    double spotLat = 0.0;
    double spotLon = 0.0;
    bool spotHasLatLon = false;
    if (hasLatLon) {
      spotLat = lat;
      spotLon = lon;
      spotHasLatLon = true;
      potaSpots[i].lat = (float)lat;
      potaSpots[i].lon = (float)lon;
      potaSpots[i].hasLatLon = true;
    } else if (potaSpots[i].locator.length() >= 4) {
      locatorToLatLon(potaSpots[i].locator, spotLat, spotLon);
      spotHasLatLon = true;
      potaSpots[i].lat = (float)spotLat;
      potaSpots[i].lon = (float)spotLon;
      potaSpots[i].hasLatLon = true;
    }

    double userLatLocal = 0.0;
    double userLonLocal = 0.0;
    bool userHasLatLon = userLatLonValid;
    if (userHasLatLon) {
      userLatLocal = userLat;
      userLonLocal = userLon;
    } else if (userLocator.length() >= 4) {
      locatorToLatLon(userLocator, userLatLocal, userLonLocal);
      userHasLatLon = true;
    }

    if (userHasLatLon && spotHasLatLon) {
      potaSpots[i].distance = calculateDistance(userLatLocal, userLonLocal, spotLat, spotLon);
      updatedPota = true;
    }
  }

  if (updated) {
#ifdef ENABLE_TFT_DISPLAY
    updateScreen2();
#endif
  }
  if (updatedPota) {
#ifdef ENABLE_TFT_DISPLAY
    updateScreen7();
#endif
  }
}

bool fetchQrzRawXml(const String &callsign, String &body) {
  if (!wifiConnected || WiFi.status() != WL_CONNECTED) {
    qrzStatus = "QRZ: no wifi";
    return false;
  }
  String sessionKey;
  if (!ensureQrzSession(sessionKey)) {
    return false;
  }

  String url = "https://xmldata.qrz.com/xml/current/?s=" + sessionKey +
               ";callsign=" + callsign;
  static WiFiClientSecure qrzRawClient;
  qrzRawClient.setInsecure();
  HTTPClient http;
  http.setTimeout(3000);
  http.begin(qrzRawClient, url);
  int code = http.GET();
  if (code != 200) {
    http.end();
    qrzStatus = "QRZ: lookup http " + String(code);
    return false;
  }
  body = http.getString();
  http.end();

  if (body.indexOf("Session Timeout") >= 0 || body.indexOf("Invalid session") >= 0) {
    String dummy;
    ensureQrzSession(dummy);
    qrzStatus = "QRZ: session expired";
    return false;
  }
  return true;
}

void runQrzSingleTest(const String &callsign) {
  (void)callsign;
}

// Parsowanie spotu DX z formatu "DX de [Spotter]: [Freq] [Call] [Comments] [Time]"
bool parseDXSpot(String line, DXSpot &spot) {
  LOGV_PRINT("[PARSE] parseDXSpot START, len=");
  LOGV_PRINTLN(line.length());
  
  // Format: DX de SP5XYZ: 14025.0 SP9ABC Test comment 1234Z
  if (!line.startsWith("DX de")) {
    LOGV_PRINTLN("[PARSE] Nie zaczyna siÄ‚â€žĂ˘â€žË od 'DX de' - wyjĂ„Ä…Ă˘â‚¬Ĺźcie");
    return false;
  }
  
  int dePos = line.indexOf("DX de");
  if (dePos < 0) return false;
  
  int colonPos = line.indexOf(":", dePos);
  if (colonPos < 0) return false;
  
  // WyciÄ‚â€žĂ˘â‚¬Â¦gnij spottera
  String spotterPart = line.substring(dePos + 5, colonPos);
  spotterPart.trim();
  spot.spotter = spotterPart;
  
  // Reszta po dwukropku
  String rest = line.substring(colonPos + 1);
  rest.trim();
  
  // Parsuj czÄ‚â€žĂ˘â€žËstotliwoĂ„Ä…Ă˘â‚¬ĹźÄ‚â€žĂ˘â‚¬Ë‡ (pierwsza liczba)
  int spacePos = rest.indexOf(" ");
  if (spacePos < 0) return false;
  
  String freqStr = rest.substring(0, spacePos);
  spot.frequency = freqStr.toFloat();
  rest = rest.substring(spacePos + 1);
  rest.trim();
  
  // Parsuj znak wywoĂ„Ä…Ă˘â‚¬Ĺˇawczy (nastÄ‚â€žĂ˘â€žËpne sĂ„Ä…Ă˘â‚¬Ĺˇowo)
  spacePos = rest.indexOf(" ");
  if (spacePos < 0) {
    spot.callsign = rest;
    spot.comment = "";
  } else {
    spot.callsign = rest.substring(0, spacePos);
    rest = rest.substring(spacePos + 1);
    rest.trim();
    
    // Reszta to komentarz (moĂ„Ä…Ă„Ëťe zawieraÄ‚â€žĂ˘â‚¬Ë‡ czas na koĂ„Ä…Ă˘â‚¬Ĺľcu)
    // Czas jest zwykle na koĂ„Ä…Ă˘â‚¬Ĺľcu w formacie HHMMZ
    int timePos = rest.lastIndexOf("Z");
    if (timePos > 0 && rest.length() >= timePos + 1) {
      String timeStr = rest.substring(timePos - 4, timePos + 1);
      if (timeStr.length() == 5) {
        spot.time = timeStr;
        spot.comment = rest.substring(0, timePos - 4);
        spot.comment.trim();
      } else {
        spot.comment = rest;
        // Pobierz czas z NTP
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
          char timeBuffer[6];
          strftime(timeBuffer, 6, "%H%MZ", &timeinfo);
          spot.time = String(timeBuffer);
        } else {
          spot.time = "----Z";
        }
      }
    } else {
      spot.comment = rest;
      // Pobierz czas z NTP (z timeout - nie blokuj jeĂ„Ä…Ă˘â‚¬Ĺźli NTP nie dziaĂ„Ä…Ă˘â‚¬Ĺˇa)
      struct tm timeinfo;
      if (getLocalTime(&timeinfo, 1)) { // Timeout 1 sekunda
        char timeBuffer[6];
        strftime(timeBuffer, 6, "%H%MZ", &timeinfo);
        spot.time = String(timeBuffer);
      } else {
        spot.time = "----Z";
      }
    }
  }
  
  // OkreĂ„Ä…Ă˘â‚¬Ĺźl pasmo i modulacjÄ‚â€žĂ˘â€žË
  spot.band = getBand(spot.frequency);
  spot.mode = getMode(spot.comment);
  
  // SprĂ„â€šĹ‚buj wyciÄ‚â€žĂ˘â‚¬Â¦gnÄ‚â€žĂ˘â‚¬Â¦Ä‚â€žĂ˘â‚¬Ë‡ locator z komentarza (format JO82LK)
  spot.locator = "";
  int commentLen = spot.comment.length();
  if (commentLen >= 6) { // Locator ma minimum 6 znakĂ„â€šĹ‚w
    for (int i = 0; i <= commentLen - 6; i++) {
      String sub = spot.comment.substring(i, i + 6);
      if (sub.length() == 6 && 
          isAlpha(sub.charAt(0)) && isAlpha(sub.charAt(1)) &&
          isDigit(sub.charAt(2)) && isDigit(sub.charAt(3)) &&
          isAlpha(sub.charAt(4)) && isAlpha(sub.charAt(5))) {
        spot.locator = sub;
        break;
      }
    }
  }
  
  LOGV_PRINTLN("[PARSE] Obliczanie odlegĂ„Ä…Ă˘â‚¬ĹˇoĂ„Ä…Ă˘â‚¬Ĺźci...");
  
  spot.distance = 0;
  spot.country = "";
  spot.lat = 0.0f;
  spot.lon = 0.0f;
  spot.hasLatLon = false;
  bool qrzConfigured = (qrzUsername.length() > 0 && qrzPassword.length() > 0);
  // Najpierw licz z lokatora jeĂ„Ä…Ă˘â‚¬Ĺźli mamy lokalizacjÄ‚â€žĂ˘â€žË i lokator w spocie
  if ((userLatLonValid || userLocator.length() >= 4) && spot.locator.length() >= 4) {
    double userLatLocal, userLonLocal, spotLat, spotLon;
    if (userLatLonValid) {
      userLatLocal = userLat;
      userLonLocal = userLon;
    } else {
      locatorToLatLon(userLocator, userLatLocal, userLonLocal);
    }
    locatorToLatLon(spot.locator, spotLat, spotLon);
    spot.distance = calculateDistance(userLatLocal, userLonLocal, spotLat, spotLon);
    spot.lat = (float)spotLat;
    spot.lon = (float)spotLon;
    spot.hasLatLon = true;
  }

  if (qrzConfigured) {
    applyQrzCacheToSpot(spot, QRZ_CACHE_TTL_MS);
  }

  if (qrzConfigured && spot.callsign.length() > 0 && (spot.country.length() == 0 || !spot.hasLatLon)) {
    String call = spot.callsign;
    call.toUpperCase();
    enqueueQrzLookup(call);
  }

  compactDxSpotStrings(spot);
  
  LOGV_PRINTLN("[PARSE] parseDXSpot END - OK");
  return true;
}

// Prostsze parsowanie spotu dla POTA (bez QRZ i bez obliczeĂ„Ä…Ă˘â‚¬Ĺľ dystansu)
bool parsePotaSpot(String line, DXSpot &spot) {
  if (!line.startsWith("DX de")) {
    return false;
  }

  int dePos = line.indexOf("DX de");
  if (dePos < 0) return false;

  int colonPos = line.indexOf(":", dePos);
  if (colonPos < 0) return false;

  String spotterPart = line.substring(dePos + 5, colonPos);
  spotterPart.trim();
  spot.spotter = spotterPart;

  String rest = line.substring(colonPos + 1);
  rest.trim();

  int spacePos = rest.indexOf(" ");
  if (spacePos < 0) return false;

  String freqStr = rest.substring(0, spacePos);
  spot.frequency = freqStr.toFloat();
  rest = rest.substring(spacePos + 1);
  rest.trim();

  spacePos = rest.indexOf(" ");
  if (spacePos < 0) {
    spot.callsign = rest;
    spot.comment = "";
  } else {
    spot.callsign = rest.substring(0, spacePos);
    rest = rest.substring(spacePos + 1);
    rest.trim();

    int timePos = rest.lastIndexOf("Z");
    if (timePos > 0 && rest.length() >= timePos + 1) {
      String timeStr = rest.substring(timePos - 4, timePos + 1);
      if (timeStr.length() == 5) {
        spot.time = timeStr;
        spot.comment = rest.substring(0, timePos - 4);
        spot.comment.trim();
      } else {
        spot.comment = rest;
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
          char timeBuffer[6];
          strftime(timeBuffer, 6, "%H%MZ", &timeinfo);
          spot.time = String(timeBuffer);
        } else {
          spot.time = "----Z";
        }
      }
    } else {
      spot.comment = rest;
      struct tm timeinfo;
      if (getLocalTime(&timeinfo, 1)) {
        char timeBuffer[6];
        strftime(timeBuffer, 6, "%H%MZ", &timeinfo);
        spot.time = String(timeBuffer);
      } else {
        spot.time = "----Z";
      }
    }
  }

  spot.band = getBand(spot.frequency);
  spot.mode = getMode(spot.comment);
  spot.locator = "";
  spot.distance = 0;
  spot.country = "";
  spot.lat = 0.0f;
  spot.lon = 0.0f;
  spot.hasLatLon = false;
  compactDxSpotStrings(spot);

  return true;
}

// Dodaj nowy spot do tablicy
void addSpot(DXSpot spot) {
  compactDxSpotStrings(spot);
  lockDxSpots();
  LOGV_PRINT("[SPOT] addSpot: ");
  LOGV_PRINT(spot.callsign);
  LOGV_PRINT(" @ ");
  LOGV_PRINT(spot.frequency);
  LOGV_PRINT(" kHz, spotCount=");
  LOGV_PRINTLN(spotCount);
  
  // PrzesuĂ„Ä…Ă˘â‚¬Ĺľ istniejÄ‚â€žĂ˘â‚¬Â¦ce spoty
  if (spotCount < MAX_SPOTS) {
    spotCount++;
  }
  
  // PrzesuĂ„Ä…Ă˘â‚¬Ĺľ wszystkie spoty o jeden w dĂ„â€šĹ‚Ă„Ä…Ă˘â‚¬Ĺˇ
  for (int i = MAX_SPOTS - 1; i > 0; i--) {
    spots[i] = spots[i - 1];
  }
  
  // Dodaj nowy spot na poczÄ‚â€žĂ˘â‚¬Â¦tku
  spots[0] = spot;
  
  LOGV_PRINT("[SPOT] addSpot END, nowy spotCount=");
  LOGV_PRINTLN(spotCount);
  unlockDxSpots();
  // logSpotList(); // WyĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czone - nie wypisuj listy spotĂ„â€šĹ‚w do Serial
}

void addPotaSpot(DXSpot spot) {
  compactDxSpotStrings(spot);
  if (potaSpotCount < MAX_POTA_SPOTS) {
    potaSpotCount++;
  }

  for (int i = MAX_POTA_SPOTS - 1; i > 0; i--) {
    potaSpots[i] = potaSpots[i - 1];
  }

  potaSpots[0] = spot;
}

void addHamalertSpot(DXSpot spot) {
  compactDxSpotStrings(spot);
  if (hamalertSpotCount < MAX_POTA_SPOTS) {
    hamalertSpotCount++;
  }

  for (int i = MAX_POTA_SPOTS - 1; i > 0; i--) {
    hamalertSpots[i] = hamalertSpots[i - 1];
  }

  hamalertSpots[0] = spot;
}

bool parseHamalertJsonSpot(const String &line, DXSpot &spot) {
  DynamicJsonDocument doc(3072);
  DeserializationError err = deserializeJson(doc, line);
  if (err) {
    return false;
  }

  JsonVariant root = doc.as<JsonVariant>();
  String callsign = root["dx"] | root["call"] | root["callsign"] | "";
  if (callsign.length() == 0) {
    return false;
  }

  spot.callsign = callsign;
  spot.spotter = root["de"] | root["spotter"] | root["source"] | "";
  spot.comment = root["comment"] | root["info"] | root["message"] | "";
  spot.mode = root["mode"] | "";
  if (spot.mode.length() == 0) {
    spot.mode = getMode(spot.comment);
  }
  spot.mode.toUpperCase();
  spot.country = root["country"] | root["entity"] | "";

  float freq = 0.0f;
  if (root["frequency"].is<float>() || root["frequency"].is<double>()) {
    freq = (float)root["frequency"].as<double>();
  } else if (root["frequency"].is<int>() || root["frequency"].is<long>()) {
    freq = (float)root["frequency"].as<long>();
  } else if (root["freq"].is<float>() || root["freq"].is<double>()) {
    freq = (float)root["freq"].as<double>();
  } else if (root["freq"].is<int>() || root["freq"].is<long>()) {
    freq = (float)root["freq"].as<long>();
  } else if (root["frequency"].is<const char*>()) {
    freq = String(root["frequency"].as<const char*>()).toFloat();
  } else if (root["freq"].is<const char*>()) {
    freq = String(root["freq"].as<const char*>()).toFloat();
  }
  if (freq <= 0.0f) {
    return false;
  }
  // HAMALERT i DX cluster zwykle podajÄ… kHz, ale obsĹ‚uĹĽ takĹĽe MHz.
  spot.frequency = (freq > 1000.0f) ? (freq / 1000.0f) : freq;
  spot.band = getBand(spot.frequency * 1000.0f);

  String timeStr = root["time"] | root["spotTime"] | root["timestamp"] | "";
  if (timeStr.length() == 0) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 1)) {
      char timeBuffer[6];
      strftime(timeBuffer, 6, "%H%MZ", &timeinfo);
      timeStr = String(timeBuffer);
    } else {
      timeStr = "----Z";
    }
  }
  spot.time = timeStr;
  spot.distance = 0;
  spot.locator = "";
  spot.lat = 0.0f;
  spot.lon = 0.0f;
  spot.hasLatLon = false;
  compactDxSpotStrings(spot);
  return true;
}

bool parseHamalertSpotLine(const String &lineIn, DXSpot &spot) {
  String line = lineIn;
  line.trim();
  if (line.length() < 6) {
    return false;
  }
  if (line == ">" || line == "#" || line.startsWith("Welcome") || line.startsWith("Password")) {
    return false;
  }
  if (line.charAt(0) == '{' && parseHamalertJsonSpot(line, spot)) {
    return true;
  }

  DXSpot parsed;
  if (!parsePotaSpot(line, parsed)) {
    return false;
  }
  parsed.frequency = (parsed.frequency > 1000.0f) ? (parsed.frequency / 1000.0f) : parsed.frequency;
  parsed.band = getBand(parsed.frequency * 1000.0f);
  parsed.mode.toUpperCase();
  compactDxSpotStrings(parsed);
  spot = parsed;
  return true;
}

bool fetchHamalertTelnet() {
  const size_t HAMALERT_MAX_LINE_LEN = 1400;
  Serial.println("[HAMALERT] fetch start");
  if (!wifiConnected) {
    Serial.println("[HAMALERT] skip: WiFi offline");
    return false;
  }
  if (hamalertHost.length() == 0 || hamalertPort <= 0 || hamalertLogin.length() == 0 || hamalertPassword.length() == 0) {
    Serial.println("[HAMALERT] skip: missing host/port/login/password");
    return false;
  }

  String login = hamalertLogin;
  login.trim();
  if (login.length() == 0) {
    Serial.println("[HAMALERT] skip: hamalert_login is empty");
    return false;
  }

  Serial.print("[HAMALERT] connect ");
  Serial.print(hamalertHost);
  Serial.print(":");
  Serial.println(hamalertPort);

  WiFiClient client;
  client.setTimeout(2500);
  if (!client.connect(hamalertHost.c_str(), hamalertPort)) {
    Serial.println("[HAMALERT] connect failed");
    return false;
  }
  Serial.println("[HAMALERT] connected");

  hamalertSpotCount = 0;
  String line;
  line.reserve(HAMALERT_MAX_LINE_LEN + 16);

  // Odczytaj ewentualny banner/prompt przed logowaniem
  unsigned long preLoginUntil = millis() + 1200;
  while (millis() < preLoginUntil && client.connected()) {
    while (client.available()) {
      char c = (char)client.read();
      if (c == '\r') continue;
      if (c == '\n') {
        if (line.length() > 0) {
          Serial.print("[HAMALERT] < ");
          Serial.println(line);
        }
        line = "";
      } else if (line.length() < HAMALERT_MAX_LINE_LEN) {
        line += c;
      }
    }
    delay(2);
    yield();
  }
  if (line.length() > 0) {
    Serial.print("[HAMALERT] < ");
    Serial.println(line);
    line = "";
  }

  // HamAlert telnet wymaga username + password
  Serial.print("[HAMALERT] > username: ");
  Serial.println(login);
  client.print(login);
  client.print("\n");

  delay(180);
  Serial.print("[HAMALERT] > password: (");
  Serial.print(hamalertPassword.length());
  Serial.println(" chars)");
  client.print(hamalertPassword);
  client.print("\n");

  delay(220);
  Serial.println("[HAMALERT] > set/json");
  client.print("set/json\n");

  delay(240);
  Serial.println("[HAMALERT] > sh/dx 30");
  client.print("sh/dx 30\n");

  const unsigned long started = millis();
  unsigned long lastRx = millis();
  const unsigned long totalTimeoutMs = 9000;
  const unsigned long idleTimeoutMs = 1800;
  int parsedOkCount = 0;
  int parsedFailCount = 0;
  int diagLineCount = 0;

  while (client.connected() && (millis() - started) < totalTimeoutMs) {
    while (client.available()) {
      char c = (char)client.read();
      lastRx = millis();
      if (c == '\r') {
        continue;
      }
      if (c == '\n') {
        if (line.length() > 0) {
          DXSpot s;
          if (parseHamalertSpotLine(line, s)) {
            addHamalertSpot(s);
            parsedOkCount++;
            if (hamalertSpotCount >= MAX_POTA_SPOTS) {
              break;
            }
          } else {
            parsedFailCount++;
            String lowerLine = line;
            lowerLine.toLowerCase();
            if (lowerLine.indexOf("invalid") >= 0 || lowerLine.indexOf("error") >= 0 || lowerLine.indexOf("denied") >= 0 ||
                lowerLine.indexOf("login") >= 0 || lowerLine.indexOf("password") >= 0 || lowerLine.indexOf("failed") >= 0) {
              Serial.print("[HAMALERT] server: ");
              Serial.println(line);
            } else if (diagLineCount < 8) {
              Serial.print("[HAMALERT] line(unparsed): ");
              Serial.println(line);
              diagLineCount++;
            }
          }
        }
        line = "";
      } else if (line.length() < HAMALERT_MAX_LINE_LEN) {
        line += c;
      }
    }
    if ((millis() - lastRx) > idleTimeoutMs && hamalertSpotCount > 0) {
      break;
    }
    if (hamalertSpotCount >= MAX_POTA_SPOTS) {
      break;
    }
    yield();
    delay(2);
  }

  if (line.length() > 0 && hamalertSpotCount < MAX_POTA_SPOTS) {
    DXSpot s;
    if (parseHamalertSpotLine(line, s)) {
      addHamalertSpot(s);
      parsedOkCount++;
    } else {
      parsedFailCount++;
    }
  }

  client.stop();
  Serial.print("[HAMALERT] done: spots=");
  Serial.print(hamalertSpotCount);
  Serial.print(", parsed_ok=");
  Serial.print(parsedOkCount);
  Serial.print(", parsed_fail=");
  Serial.println(parsedFailCount);
  return hamalertSpotCount > 0;
}

// ========== APRS-IS FUNKCJE ==========

// PoĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦cz z serwerem APRS-IS
void connectToAPRS() {
  Serial.println("[APRS] connectToAPRS() START");
  
  if (!wifiConnected) {
    Serial.println("[APRS] WiFi nie poĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czony - wyjĂ„Ä…Ă˘â‚¬Ĺźcie");
    return;
  }
  
  if (aprsClient.connected()) {
    // JeĂ„Ä…Ă˘â‚¬Ĺźli konfiguracja siÄ‚â€žĂ˘â€žË zmieniĂ„Ä…Ă˘â‚¬Ĺˇa, rozĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦cz i poĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦cz ponownie
    Serial.println("[APRS] JuĂ„Ä…Ă„Ëť poĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czony - sprawdzam czy potrzeba reconnect...");
    // MoĂ„Ä…Ă„Ëťna dodaÄ‚â€žĂ˘â‚¬Ë‡ sprawdzenie czy konfiguracja siÄ‚â€žĂ˘â€žË zmieniĂ„Ä…Ă˘â‚¬Ĺˇa, ale na razie zostawiamy jak jest
    // W razie potrzeby reconnect nastÄ‚â€žĂ˘â‚¬Â¦pi automatycznie przez watchdog
    return;
  }
  
  unsigned long now = millis();
  if (now - lastAPRSAttempt < 5000) {
    return; // Cichy return - nie spamuj logu
  }
  
  lastAPRSAttempt = now;
  
  Serial.print("[APRS] Ă„Ä…Ă‚ÂÄ‚â€žĂ˘â‚¬Â¦czenie z APRS-IS: ");
  Serial.print(aprsIsHost);
  Serial.print(":");
  Serial.println(aprsIsPort);
  
  // Diagnostyka DNS
  IPAddress resolvedIp;
  if (WiFi.hostByName(aprsIsHost.c_str(), resolvedIp)) {
    Serial.print("DNS OK: ");
    Serial.print(aprsIsHost);
    Serial.print(" -> ");
    Serial.println(resolvedIp);
  } else {
    Serial.print("DNS FAIL dla hosta: ");
    Serial.println(aprsIsHost);
  }
  
  Serial.println("[APRS] WywoĂ„Ä…Ă˘â‚¬Ĺˇanie aprsClient.connect()...");
  unsigned long connectStart = millis();
  
  // Ustaw timeout poĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czenia (2.5 sekundy)
  aprsClient.setTimeout(2500);
  
  if (aprsClient.connect(aprsIsHost.c_str(), aprsIsPort)) {
    unsigned long connectTime = millis() - connectStart;
    Serial.print("[APRS] PoĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czono z APRS-IS! (czas: ");
    Serial.print(connectTime);
    Serial.println("ms)");
    aprsConnected = true;
    aprsBuffer = "";
    aprsLoginSent = false;
    lastAPRSRxMs = millis();
    
    // Zaplanuj wysĂ„Ä…Ă˘â‚¬Ĺˇanie loginu
    delay(500); // KrĂ„â€šĹ‚tkie opĂ„â€šĹ‚Ă„Ä…ÄąĹşnienie przed loginem
    sendAPRSLogin();
  } else {
    unsigned long connectTime = millis() - connectStart;
    Serial.print("[APRS] BĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦d poĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czenia z APRS-IS (czas: ");
    Serial.print(connectTime);
    Serial.println("ms)");
    aprsConnected = false;
  }
  Serial.println("[APRS] connectToAPRS() END");
}

// Oblicza standardowy passcode APRS-IS dla znaku (algorytm aprs.fi/passcode).
// Wartosc 0 oznacza brak poprawnego znaku - serwer przyjmie wtedy tylko odczyt.
static int computeAprsPasscode(const String &rawCall) {
  String call = rawCall;
  call.trim();
  call.toUpperCase();
  int dash = call.indexOf('-');
  if (dash > 0) call = call.substring(0, dash);
  if (call.length() == 0) return 0;
  unsigned int hash = 0x73E2;
  for (size_t i = 0; i < call.length(); i++) {
    unsigned char c = (unsigned char)call.charAt(i);
    hash ^= ((i & 1) ? (unsigned int)c : ((unsigned int)c << 8));
  }
  return (int)(hash & 0x7FFF);
}

// WyĹ›lij login do APRS-IS
void sendAPRSLogin() {
  if (!aprsConnected || !aprsClient.connected()) {
    Serial.println("[APRS] Nie mozna wysĹ‚aÄ‡ loginu - brak poĹ‚Ä…czenia");
    return;
  }
  
  // Format loginu: user Callsign pass 23123 vers ESP32-HAM-CLOCK 1.2
  // Serwery aprs2.net odrzucaja login z passcode 0/-1 ("Login by user not
  // allowed") i nie wysylaja wtedy ZADNYCH ramek - radar i panel APRS-IS
  // zostaja puste, mimo poprawnego polaczenia TCP. Uzytkownik czesto zostawia
  // domyslne 00000, wiec wyliczamy passcode z jego znaku (standardowy algorytm
  // APRS-IS). Recznie wpisany passcode (>0) ma pierwszenstwo.
  int effectivePasscode = aprsPasscode;
  if (effectivePasscode <= 0) {
    effectivePasscode = computeAprsPasscode(getAprsBaseCallsign());
  }

  String login = "user ";
  login += getAprsTxCallsignWithSsid();
  login += " pass ";
  login += String(effectivePasscode);
  login += " vers ESP32-HAM-CLOCK 1.2";
  
  Serial.print("[APRS] WysyĹ‚anie loginu: ");
  Serial.println(login);
  
  aprsClient.println(login);
  aprsLoginSent = true;
  lastAPRSRxMs = millis();
  
  // Po zalogowaniu wyĂ„Ä…Ă˘â‚¬Ĺźlij filtr
  delay(500);
  sendAPRSFilter();
}

// WyĂ„Ä…Ă˘â‚¬Ĺźlij komendÄ‚â€žĂ˘â€žË filtra do APRS-IS
void sendAPRSFilter() {
  if (!aprsConnected || !aprsClient.connected()) {
    Serial.println("[APRS] Nie moĂ„Ä…Ă„Ëťna wysĂ„Ä…Ă˘â‚¬ĹˇaÄ‚â€žĂ˘â‚¬Ë‡ filtra - brak poĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czenia");
    return;
  }
  
  // Format filtra: #filter r/52.40/16.92/50
  // UĂ„Ä…Ă„Ëťywamy wspĂ„â€šĹ‚Ă„Ä…Ă˘â‚¬ĹˇrzÄ‚â€žĂ˘â€žËdnych z sekcji "Moja Stacja"
  double filterLat = userLatLonValid ? userLat : 52.40;  // Fallback jeĂ„Ä…Ă˘â‚¬Ĺźli nie ustawione
  double filterLon = userLatLonValid ? userLon : 16.92;
  String filter = "#filter r/";
  filter += String(filterLat, 2);
  filter += "/";
  filter += String(filterLon, 2);
  filter += "/";
  filter += String(aprsFilterRadius);
  
  Serial.print("[APRS] WysyĂ„Ä…Ă˘â‚¬Ĺˇanie filtra: ");
  Serial.println(filter);
  
  aprsClient.println(filter);
  lastAPRSRxMs = millis();
}

String formatAprsCoordinate(double value, bool isLat) {
  double absVal = fabs(value);
  int degrees = (int)absVal;
  double minutes = (absVal - degrees) * 60.0;
  String degText;
  if (isLat) {
    degText = (degrees < 10 ? "0" : "") + String(degrees);
  } else {
    if (degrees < 10) degText = "00" + String(degrees);
    else if (degrees < 100) degText = "0" + String(degrees);
    else degText = String(degrees);
  }

  String minText = String(minutes, 2);
  if (minutes < 10.0) {
    minText = "0" + minText;
  }

  char hemi = isLat ? ((value >= 0.0) ? 'N' : 'S') : ((value >= 0.0) ? 'E' : 'W');
  return degText + minText + String(hemi);
}

static bool isValidAprsSymbolChar(char c) {
  return (c >= 33 && c <= 126); // widoczne ASCII bez spacji/kontrolnych
}

static String sanitizeAprsSymbol(const String &sym) {
  String s = sym;
  s.trim();
  if (s.length() < 2) {
    return String(DEFAULT_APRS_SYMBOL_TABLE) + String(DEFAULT_APRS_SYMBOL_CODE);
  }
  s = s.substring(0, 2);
  if (!isValidAprsSymbolChar(s.charAt(0)) || !isValidAprsSymbolChar(s.charAt(1))) {
    return String(DEFAULT_APRS_SYMBOL_TABLE) + String(DEFAULT_APRS_SYMBOL_CODE);
  }
  return s;
}

static void applyAprsSymbol(const String &sym) {
  String s = sanitizeAprsSymbol(sym);
  aprsSymbolTwoChar = s;
  aprsSymbolTable = s.charAt(0);
  aprsSymbolCode = s.charAt(1);
}

static int normalizeAprsSsid(int ssid) {
  if (ssid < 0) return 0;
  if (ssid > 15) return 15;
  return ssid;
}

static void applyAprsSsid(int ssid) {
  aprsSsid = normalizeAprsSsid(ssid);
}

static String getAprsBaseCallsign() {
  String call = aprsCallsign.length() ? aprsCallsign : userCallsign;
  call.trim();
  call.toUpperCase();
  int dash = call.indexOf('-');
  if (dash > 0) {
    call = call.substring(0, dash);
  }
  return call;
}

static String getAprsTxCallsignWithSsid() {
  String baseCall = getAprsBaseCallsign();
  if (aprsSsid <= 0) {
    return baseCall;
  }
  return baseCall + "-" + String(aprsSsid);
}

static String sanitizeAprsComment(const String &cmt) {
  String s = cmt;
  s.trim();
  String out = "";
  for (size_t i = 0; i < s.length(); i++) {
    char c = s.charAt(i);
    if (c >= 32 && c <= 126) { // drukowalne ASCII
      out += c;
    }
    if (out.length() >= 43) { // rekomendowany limit komentarza APRS
      break;
    }
  }
  return out;
}

static String sanitizeAprsAlertWatchToken(const String &raw) {
  String token = raw;
  token.trim();
  token.toUpperCase();

  bool wildcardAnySsid = false;
  if (token.endsWith("*")) {
    wildcardAnySsid = true;
    token.remove(token.length() - 1);
  }

  String cleaned = "";
  for (size_t i = 0; i < token.length(); i++) {
    char ch = token.charAt(i);
    bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '/';
    if (ok) cleaned += ch;
  }

  if (cleaned.length() == 0) {
    return "";
  }
  if (wildcardAnySsid) {
    cleaned += "*";
  }
  return cleaned;
}

static String sanitizeAprsAlertList(const String &raw) {
  String src = raw;
  src.trim();
  if (src.length() == 0) return "";

  String out = "";
  int count = 0;
  int start = 0;

  while (start <= src.length() && count < 20) {
    int comma = src.indexOf(',', start);
    int end = (comma >= 0) ? comma : src.length();
    String token = src.substring(start, end);
    String cleaned = sanitizeAprsAlertWatchToken(token);

    if (cleaned.length() > 0) {
      if (count > 0) out += ",";
      out += cleaned;
      count++;
    }

    if (comma < 0) break;
    start = comma + 1;
  }

  return out;
}

static int normalizeAprsAlertMinSeconds(int seconds) {
  if (seconds < 0) return 0;
  if (seconds > 86400) return 86400;
  return seconds;
}

static void applyAprsAlertMinSeconds(int seconds) {
  aprsAlertMinSeconds = normalizeAprsAlertMinSeconds(seconds);
}

static float normalizeAprsAlertDistanceKm(float km) {
  if (km < 0.1f) return 0.1f;
  if (km > 500.0f) return 500.0f;
  return km;
}

static void applyAprsAlertDistanceKm(float km) {
  aprsAlertDistanceKm = normalizeAprsAlertDistanceKm(km);
}

static int normalizeAprsAlertScreenSeconds(int seconds) {
  if (seconds < 1) return 1;
  if (seconds > 60) return 60;
  return seconds;
}

static void applyAprsAlertScreenSeconds(int seconds) {
  aprsAlertScreenSeconds = normalizeAprsAlertScreenSeconds(seconds);
}

static String sanitizeAprsCallsignToken(const String &raw) {
  String token = raw;
  token.trim();
  token.toUpperCase();

  String cleaned = "";
  for (size_t i = 0; i < token.length(); i++) {
    char ch = token.charAt(i);
    bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '/';
    if (ok) cleaned += ch;
  }
  return cleaned;
}

static String getAprsCallsignBase(const String &callsign) {
  String normalized = sanitizeAprsCallsignToken(callsign);
  int dash = normalized.indexOf('-');
  if (dash > 0) {
    normalized = normalized.substring(0, dash);
  }
  return normalized;
}

static bool isAprsAlertCallMatch(const String &watchedToken, const String &incomingCallsign) {
  if (watchedToken.length() == 0 || incomingCallsign.length() == 0) {
    return false;
  }

  String watched = sanitizeAprsAlertWatchToken(watchedToken);
  String incoming = sanitizeAprsCallsignToken(incomingCallsign);
  if (watched.length() == 0 || incoming.length() == 0) {
    return false;
  }

  if (watched.endsWith("*")) {
    String base = watched.substring(0, watched.length() - 1);
    if (base.length() == 0) {
      return false;
    }
    if (incoming.equals(base)) {
      return true;
    }
    String withSsidPrefix = base + "-";
    return incoming.startsWith(withSsidPrefix);
  }

  return watched.equals(incoming);
}

static bool isAprsCallsignOnAlertList(const String &incomingCallsign) {
  String src = aprsAlertCsv;
  src.trim();
  if (src.length() == 0) return false;

  int start = 0;
  while (start <= src.length()) {
    int comma = src.indexOf(',', start);
    int end = (comma >= 0) ? comma : src.length();
    String token = src.substring(start, end);
    token.trim();

    if (isAprsAlertCallMatch(token, incomingCallsign)) {
      return true;
    }

    if (comma < 0) break;
    start = comma + 1;
  }

  return false;
}

static bool isAprsMobileSsid7or9(const String &incomingCallsign) {
  String normalized = sanitizeAprsCallsignToken(incomingCallsign);
  if (normalized.length() == 0) {
    return false;
  }

  int dash = normalized.lastIndexOf('-');
  if (dash <= 0 || dash >= (normalized.length() - 1)) {
    return false;
  }

  String ssid = normalized.substring(dash + 1);
  return ssid.equals("7") || ssid.equals("9");
}

static void resetAprsAlertCooldownState() {
  for (int i = 0; i < MAX_APRS_STATIONS; i++) {
    aprsAlertCooldown[i].callsign = "";
    aprsAlertCooldown[i].lastAlertMs = 0;
  }
  aprsAlertCooldownReplaceIdx = 0;
}

static bool shouldTriggerAprsAlert(const APRSStation &station) {
  bool watchListMatched = isAprsCallsignOnAlertList(station.callsign);

  bool nearbyMobileMatched = false;
  if (aprsAlertNearbyEnabled && station.hasLatLon && station.distance >= 0.0f && station.distance <= aprsAlertDistanceKm) {
    nearbyMobileMatched = isAprsMobileSsid7or9(station.callsign);
  }

  bool weatherStationMatched = aprsAlertWxEnabled && isAprsWxPayloadValidForAlert(station);

  if (!watchListMatched && !nearbyMobileMatched && !weatherStationMatched) {
    return false;
  }

  String normalized = sanitizeAprsCallsignToken(station.callsign);
  if (normalized.length() == 0) {
    return false;
  }

  const unsigned long minGapMs = (unsigned long)aprsAlertMinSeconds * 1000UL;
  const unsigned long nowMs = millis();

  for (int i = 0; i < MAX_APRS_STATIONS; i++) {
    if (aprsAlertCooldown[i].callsign.equals(normalized)) {
      if (minGapMs > 0 && (nowMs - aprsAlertCooldown[i].lastAlertMs) < minGapMs) {
        return false;
      }
      aprsAlertCooldown[i].lastAlertMs = nowMs;
      return true;
    }
  }

  for (int i = 0; i < MAX_APRS_STATIONS; i++) {
    if (aprsAlertCooldown[i].callsign.length() == 0) {
      aprsAlertCooldown[i].callsign = normalized;
      aprsAlertCooldown[i].lastAlertMs = nowMs;
      return true;
    }
  }

  int replaceIdx = aprsAlertCooldownReplaceIdx;
  if (replaceIdx < 0 || replaceIdx >= MAX_APRS_STATIONS) {
    replaceIdx = 0;
  }
  aprsAlertCooldown[replaceIdx].callsign = normalized;
  aprsAlertCooldown[replaceIdx].lastAlertMs = nowMs;
  aprsAlertCooldownReplaceIdx = (replaceIdx + 1) % MAX_APRS_STATIONS;
  return true;
}

static void applyAprsIntervalMinutes(int minutes) {
  int m = minutes;
  if (m < 1) m = 1;
  if (m > 180) m = 180; // sanity bound 1..180 min
  aprsIntervalMinutes = m;
  aprsPositionIntervalMs = (unsigned long)m * 60UL * 1000UL;
  nextAPRSPositionDueMs = 0; // przelicz harmonogram od nowa
}

bool isAprsTxCallValid(const String &call) {
  String c = call;
  c.trim();
  c.toUpperCase();
  if (c.length() == 0) return false;
  int dash = c.indexOf('-');
  if (dash > 0) {
    c = c.substring(0, dash);
  }
  return c.length() > 0 && c != "NOCALL";
}

void sendAprsPosition() {
  if (!aprsBeaconEnabled || !aprsConnected || !aprsClient.connected() || !aprsLoginSent) {
    return;
  }
  if (!userLatLonValid) {
    Serial.println("[APRS] Pomijam TX pozycji - brak poprawnych wspĂłĹ‚rzÄ™dnych uĹĽytkownika");
    return;
  }

  String txCallsign = getAprsTxCallsignWithSsid();
  txCallsign.trim();
  if (!isAprsTxCallValid(txCallsign)) {
    Serial.println("[APRS] Pomijam TX pozycji - ustaw znak w konfiguracji APRS");
    return;
  }
  txCallsign.toUpperCase();

  String latStr = formatAprsCoordinate(userLat, true);
  String lonStr = formatAprsCoordinate(userLon, false);

  String frame = txCallsign;
  frame += ">APRS,TCPIP*:";
  frame += "!";
  frame += latStr;
  frame += aprsSymbolTable;
  frame += lonStr;
  frame += aprsSymbolCode;
  frame += " ";
  bool useProjectComment = (((aprsBeaconTxCount + 1) % 5) == 0); // co piÄ…ty beacon
  String comment = useProjectComment ? String(APRS_POSITION_COMMENT)
                                     : sanitizeAprsComment(aprsUserComment);
  frame += comment;

  aprsClient.println(frame);
  lastAPRSPositionTxMs = millis();
  aprsBeaconTxCount++;

  Serial.print("[APRS] WysĹ‚ano pozycjÄ™: ");
  Serial.println(frame);
}

// Parsuj ramkÄ‚â€žĂ˘â€žË APRS
// Format przykĂ„Ä…Ă˘â‚¬Ĺˇadowy: SP3KON-1>APRS,TCPIP*,qAC,T2POLAND:!5202.40N/01655.12E#PHG5130/Poznan
bool parseAPRSFrame(String line, APRSStation &station) {
  LOGV_PRINTF("[APRS] Parsing frame: %s\n", line.c_str());
  
  // WyczyĂ„Ä…Ă˘â‚¬ĹźÄ‚â€žĂ˘â‚¬Ë‡ strukturÄ‚â€žĂ˘â€žË
  station.time = "";
  station.callsign = "";
  station.symbol = "";
  station.symbolTable = "";
  station.lat = 0.0f;
  station.lon = 0.0f;
  station.comment = "";
  station.freqMHz = 0.0f;
  station.distance = 0.0f;
  station.hasLatLon = false;
  
  // Pobierz czas UTC
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 1)) {
    char timeBuffer[6];
    strftime(timeBuffer, 6, "%H%MZ", &timeinfo);
    station.time = String(timeBuffer);
  } else {
    station.time = "----Z";
  }
  
  // Parsuj callsign (przed >)
  int gtPos = line.indexOf('>');
  if (gtPos < 0) {
    LOGV_PRINTLN("[APRS] Brak znaku '>' w ramce - nieprawidĂ„Ä…Ă˘â‚¬Ĺˇowy format");
    return false;
  }
  station.callsign = line.substring(0, gtPos);
  station.callsign.trim();
  LOGV_PRINTF("[APRS] Callsign: %s\n", station.callsign.c_str());

  // Najpierw sprĂ„â€šĹ‚buj dekodowaÄ‚â€žĂ˘â‚¬Ë‡ Mic-E / skompresowane / obiekty z gotowego parsera
  bool advancedParsed = parseAprsAdvancedPosition(line, station);
  if (advancedParsed) {
    LOGV_PRINTF("[APRS] Advanced decode lat/lon: %.6f, %.6f\n", station.lat, station.lon);
  }
  
  // Parsuj pozycjÄ‚â€žĂ˘â€žË GPS (prosty parser) tylko jeĂ„Ä…Ă˘â‚¬Ĺźli advanced nie zadziaĂ„Ä…Ă˘â‚¬ĹˇaĂ„Ä…Ă˘â‚¬Ĺˇ
  // Szukaj formatu: !5202.40N/01655.12E, @5202.40N/01655.12E, =5202.40N/01655.12E, lub ;...*HHMMzDDMM.mmN/DDDMM.mmE
  int posStart = -1;
  char tableSymbol = 0;
  int commentBeforePos = -1; // Pozycja poczÄ‚â€žĂ˘â‚¬Â¦tku komentarza przed timestampem (dla formatu z ;)
  
  if (!advancedParsed) {
  // Szukaj standardowych formatĂ„â€šĹ‚w pozycji (!, @, =)
  posStart = line.indexOf('!');
  if (posStart >= 0) {
    tableSymbol = '!';
  } else {
    posStart = line.indexOf('@');
    if (posStart >= 0) {
      tableSymbol = '@';
    } else {
      posStart = line.indexOf('=');
      if (posStart >= 0) {
        tableSymbol = '=';
      }
    }
  }
  
  // JeĂ„Ä…Ă˘â‚¬Ĺźli nie znaleziono standardowych formatĂ„â€šĹ‚w, szukaj w komentarzu (format z ;)
  if (posStart < 0) {
    int colonPos = line.indexOf(':');
    if (colonPos >= 0) {
      String commentPart = line.substring(colonPos + 1);
      // Szukaj formatu z ; i timestampem: ;...*HHMMzDDMM.mmN/DDDMM.mmE
      int semicolonPos = commentPart.indexOf(';');
      if (semicolonPos >= 0) {
        // Zapisz pozycjÄ‚â€žĂ˘â€žË poczÄ‚â€žĂ˘â‚¬Â¦tku komentarza przed timestampem
        commentBeforePos = colonPos + 1 + semicolonPos + 1; // +1 bo colonPos, +1 bo po ';'
        // Szukaj timestampu *HHMMz
        int timestampPos = commentPart.indexOf('*', semicolonPos);
        if (timestampPos >= 0 && timestampPos + 7 < commentPart.length()) {
          // SprawdĂ„Ä…ÄąĹş czy po timestampie jest pozycja (format: *HHMMzDDMM.mmN)
          char zChar = commentPart.charAt(timestampPos + 6);
          if (zChar == 'z' || zChar == 'Z') {
            // Pozycja zaczyna siÄ‚â€žĂ˘â€žË po 'z'
            posStart = colonPos + 1 + timestampPos + 7; // +1 bo colonPos, +timestampPos, +7 bo "*HHMMz"
            tableSymbol = ';';
          }
        }
      }
    }
  }
  
  if (posStart < 0) {
    // Brak pozycji - sprĂ„â€šĹ‚buj wyciÄ‚â€žĂ˘â‚¬Â¦gnÄ‚â€žĂ˘â‚¬Â¦Ä‚â€žĂ˘â‚¬Ë‡ tylko callsign i komentarz
    LOGV_PRINTLN("[APRS] Brak pozycji GPS w ramce");
    int colonPos = line.indexOf(':');
    if (colonPos >= 0) {
      station.comment = line.substring(colonPos + 1);
      station.comment.trim();
    }
    return true; // ZwrĂ„â€šĹ‚Ä‚â€žĂ˘â‚¬Ë‡ true nawet bez pozycji
  }
  
  LOGV_PRINTF("[APRS] Znaleziono pozycjÄ‚â€žĂ˘â€žË na indeksie %d, symbol tabeli: %c\n", posStart, tableSymbol);
  
  // Symbol table ustawimy po wyciÄ‚â€žĂ˘â‚¬Â¦gniÄ‚â€žĂ˘â€žËciu pozycji
  
  // Wyznacz poczÄ‚â€žĂ˘â‚¬Â¦tek pozycji (lat) zaleĂ„Ä…Ă„Ëťnie od formatu
  bool posStartIsSymbol = (tableSymbol == '!' || tableSymbol == '@' || tableSymbol == '=' || tableSymbol == '/');
  int latStart = posStartIsSymbol ? (posStart + 1) : posStart; // dla ';' posStart wskazuje juĂ„Ä…Ă„Ëť na lat

  // ObsĂ„Ä…Ă˘â‚¬Ĺˇuga timestampu dla @ lub /: @DDHHMMh... lub /DDHHMMh...
  if (tableSymbol == '@' || tableSymbol == '/') {
    if (posStart + 7 < line.length()) {
      bool tsDigits = true;
      for (int i = 1; i <= 6; i++) {
        char c = line.charAt(posStart + i);
        if (!isDigit(c)) {
          tsDigits = false;
          break;
        }
      }
      char tsChar = line.charAt(posStart + 7);
      if (tsDigits && (tsChar == 'h' || tsChar == 'z' || tsChar == '/')) {
        latStart = posStart + 8;
      }
    }
  }

  // SprawdĂ„Ä…ÄąĹş czy jest format z "/" czy bez "/"
  int slashPos = line.indexOf('/', latStart);
  bool hasSlash = (slashPos >= 0);
  
  bool latOk = false;
  bool lonOk = false;

  if (hasSlash) {
    // Format z "/": !DDMM.mmN/DDDMM.mmEsymbol
    // Parsuj szerokoĂ„Ä…Ă˘â‚¬ĹźÄ‚â€žĂ˘â‚¬Ë‡ geograficznÄ‚â€žĂ˘â‚¬Â¦: 5202.40N (format: DDMM.mmN)
    if (latStart + 7 < line.length()) {
      String latStr = line.substring(latStart, latStart + 7);
      if (latStr.length() == 7) {
        char dir = line.charAt(latStart + 7);
        // UĂ„Ä…Ă„Ëťyj funkcji convertToDecimal do parsowania
        float parsedLat = convertToDecimal(latStr, dir);
        if (!isnan(parsedLat)) {
          station.lat = parsedLat;
          latOk = true;
        }
        
        LOGV_PRINTF("[APRS] Parsed lat: %s%c -> %.6f\n", 
                    latStr.c_str(), dir, station.lat);
      }
    }
    
    // Parsuj dĂ„Ä…Ă˘â‚¬ĹˇugoĂ„Ä…Ă˘â‚¬ĹźÄ‚â€žĂ˘â‚¬Ë‡ geograficznÄ‚â€žĂ˘â‚¬Â¦: 01655.12E (format: DDDMM.mmE)
    if (slashPos >= 0 && slashPos + 9 < line.length()) {
      String lonStr = line.substring(slashPos + 1, slashPos + 9);
      if (lonStr.length() == 8) {
        char dir = line.charAt(slashPos + 9);
        // UĂ„Ä…Ă„Ëťyj funkcji convertToDecimal do parsowania
        float parsedLon = convertToDecimal(lonStr, dir);
        if (!isnan(parsedLon)) {
          station.lon = parsedLon;
          lonOk = true;
        }
        
        LOGV_PRINTF("[APRS] Parsed lon: %s%c -> %.6f\n", 
                    lonStr.c_str(), dir, station.lon);
      }
    }
    
    // Symbol table (znak przed /)
    if (slashPos >= 0) {
      station.symbolTable = line.substring(slashPos, slashPos + 1);
    }
    // WyciÄ‚â€žĂ˘â‚¬Â¦gnij symbol code (znak po dĂ„Ä…Ă˘â‚¬ĹˇugoĂ„Ä…Ă˘â‚¬Ĺźci geograficznej, po "/")
    // Format: !DDMM.mmN/DDDMM.mmEsymbol
    if (slashPos >= 0 && slashPos + 10 < line.length()) {
      station.symbol = line.substring(slashPos + 10, slashPos + 11);
    }
  } else {
    // Format bez "/": DDMM.mmNsymbolDDDMM.mmE (symbol miÄ‚â€žĂ˘â€žËdzy lat a lon)
    // PrzykĂ„Ä…Ă˘â‚¬Ĺˇad: 5223.73NW01655.41E lub 5225.05NL01651.66E
    if (latStart + 7 < line.length()) {
      String latStr = line.substring(latStart, latStart + 7);
      if (latStr.length() == 7) {
        char dir = line.charAt(latStart + 7);
        // UĂ„Ä…Ă„Ëťyj funkcji convertToDecimal do parsowania
        float parsedLat = convertToDecimal(latStr, dir);
        if (!isnan(parsedLat)) {
          station.lat = parsedLat;
          latOk = true;
        }
        
        LOGV_PRINTF("[APRS] Parsed lat: %s%c -> %.6f\n", 
                    latStr.c_str(), dir, station.lat);
        
        // Symbol table jest zaraz po kierunku lat
        if (latStart + 8 < line.length()) {
          station.symbolTable = line.substring(latStart + 8, latStart + 9);
        }
        
        // DĂ„Ä…Ă˘â‚¬ĹˇugoĂ„Ä…Ă˘â‚¬ĹźÄ‚â€žĂ˘â‚¬Ë‡ geograficzna zaczyna siÄ‚â€žĂ˘â€žË po symbolu table
        int lonStart = latStart + 9;
        if (lonStart + 7 < line.length()) {
          String lonStr = line.substring(lonStart, lonStart + 8);
          if (lonStr.length() == 8) {
            char dir = line.charAt(lonStart + 8);
            // UĂ„Ä…Ă„Ëťyj funkcji convertToDecimal do parsowania
            float parsedLon = convertToDecimal(lonStr, dir);
            if (!isnan(parsedLon)) {
              station.lon = parsedLon;
              lonOk = true;
            }
            
            LOGV_PRINTF("[APRS] Parsed lon: %s%c -> %.6f\n", 
                        lonStr.c_str(), dir, station.lon);

            // Symbol code jest po kierunku lon (jeĂ„Ä…Ă˘â‚¬Ĺźli wystÄ‚â€žĂ˘â€žËpuje)
            if (lonStart + 9 < line.length()) {
              station.symbol = line.substring(lonStart + 9, lonStart + 10);
            }
          }
        }
      }
    }
  }
  
  // Ustaw flagÄ‚â€žĂ˘â€žË poprawnoĂ„Ä…Ă˘â‚¬Ĺźci pozycji tylko jeĂ„Ä…Ă˘â‚¬Ĺźli mamy lat i lon
  station.hasLatLon = (latOk && lonOk);

  // WyciÄ‚â€žĂ˘â‚¬Â¦gnij komentarz (po symbolu i pozycji)
  // Format z "/": !5202.40N/01655.12E#symbol/komentarz lub !5202.40N/01655.12E#symbol komentarz
  // Format bez "/": DDMM.mmNsymbolDDDMM.mmE&komentarz
  // Format z ";": ;komentarz_przed*HHMMzDDMM.mmNsymbolDDDMM.mmE&komentarz_po
  int colonPos = line.indexOf(':');
  if (colonPos >= 0 && colonPos + 1 < line.length()) {
    String fullComment = "";
    
    if (tableSymbol == ';' && commentBeforePos >= 0) {
      // Format z ";": komentarz skĂ„Ä…Ă˘â‚¬Ĺˇada siÄ‚â€žĂ˘â€žË z czÄ‚â€žĂ˘â€žËĂ„Ä…Ă˘â‚¬Ĺźci przed timestampem i po pozycji
      // CzÄ‚â€žĂ˘â€žËĂ„Ä…Ă˘â‚¬ĹźÄ‚â€žĂ˘â‚¬Ë‡ przed timestampem
      int timestampStart = line.indexOf('*', commentBeforePos);
      if (timestampStart > commentBeforePos) {
        fullComment = line.substring(commentBeforePos, timestampStart);
      }
      
      // CzÄ‚â€žĂ˘â€žËĂ„Ä…Ă˘â‚¬ĹźÄ‚â€žĂ˘â‚¬Ë‡ po pozycji
      int commentAfterPos = -1;
      if (hasSlash) {
        int afterSymbol = slashPos + 11;
        if (afterSymbol < line.length()) {
          char c = line.charAt(afterSymbol);
          if (c == '&' || c == '#' || c == '/') {
            commentAfterPos = afterSymbol + 1;
          } else {
            commentAfterPos = afterSymbol;
          }
        }
      } else {
        // Format bez "/": komentarz zaczyna siÄ‚â€žĂ˘â€žË po lon
        int afterLon = latStart + 18;
        if (afterLon < line.length()) {
          char c = line.charAt(afterLon);
          if (c == '&' || c == '#' || c == '/') {
            commentAfterPos = afterLon + 1;
          } else {
            commentAfterPos = afterLon;
          }
        }
      }
      
      if (commentAfterPos >= 0 && commentAfterPos < line.length()) {
        if (fullComment.length() > 0) {
          fullComment += " ";
        }
        fullComment += line.substring(commentAfterPos);
      }
      
      station.comment = fullComment;
      station.comment.trim();
    } else {
      // Standardowe formaty (!, @, =)
      int commentStart = colonPos + 1;
      
      if (hasSlash && slashPos >= 0) {
        // Format z "/": komentarz zaczyna siÄ‚â€žĂ˘â€žË po symbolu
        int afterSymbol = slashPos + 11;
        if (afterSymbol < line.length()) {
          char c = line.charAt(afterSymbol);
          if (c == '#' || c == '/') {
            commentStart = afterSymbol + 1;
          } else {
            // Komentarz zaczyna siÄ‚â€žĂ˘â€žË zaraz po symbolu
            commentStart = afterSymbol;
          }
        }
      } else {
        // Format bez "/": komentarz zaczyna siÄ‚â€žĂ˘â€žË po lon (po kierunku E/W)
        int afterLon = latStart + 18;
        if (afterLon < line.length()) {
          char c = line.charAt(afterLon);
          if (c == '&' || c == '#' || c == '/') {
            commentStart = afterLon + 1;
          } else {
            // Komentarz zaczyna siÄ‚â€žĂ˘â€žË zaraz po lon
            commentStart = afterLon;
          }
        }
      }
      
      // JeĂ„Ä…Ă˘â‚¬Ĺźli commentStart jest przed koĂ„Ä…Ă˘â‚¬Ĺľcem linii, wyciÄ‚â€žĂ˘â‚¬Â¦gnij komentarz
      if (commentStart < line.length() && commentStart >= colonPos + 1) {
        station.comment = line.substring(commentStart);
        station.comment.trim();
      } else if (commentStart < colonPos + 1) {
        // JeĂ„Ä…Ă˘â‚¬Ĺźli nie znaleziono komentarza po pozycji, uĂ„Ä…Ă„Ëťyj caĂ„Ä…Ă˘â‚¬Ĺˇej czÄ‚â€žĂ˘â€žËĂ„Ä…Ă˘â‚¬Ĺźci po ":"
        station.comment = line.substring(colonPos + 1);
        station.comment.trim();
      }
    }
  }
  } // !advancedParsed
  
  // SprĂ„â€šĹ‚buj wyciÄ‚â€žĂ˘â‚¬Â¦gnÄ‚â€žĂ˘â‚¬Â¦Ä‚â€žĂ˘â‚¬Ë‡ czÄ‚â€žĂ˘â€žËstotliwoĂ„Ä…Ă˘â‚¬ĹźÄ‚â€žĂ˘â‚¬Ë‡ z komentarza
  if (station.comment.length() > 0) {
    float freq = 0.0f;
    if (extractAPRSFrequencyMHz(station.comment, freq)) {
      station.freqMHz = freq;
    }
  }
  
  // Oblicz odlegĂ„Ä…Ă˘â‚¬ĹˇoĂ„Ä…Ă˘â‚¬ĹźÄ‚â€žĂ˘â‚¬Ë‡ jeĂ„Ä…Ă˘â‚¬Ĺźli mamy pozycjÄ‚â€žĂ˘â€žË
  // UĂ„Ä…Ă„Ëťywamy wspĂ„â€šĹ‚Ă„Ä…Ă˘â‚¬ĹˇrzÄ‚â€žĂ˘â€žËdnych z sekcji "Moja Stacja"
  if (station.hasLatLon) {
    // UĂ„Ä…Ă„Ëťyj double dla wiÄ‚â€žĂ˘â€žËkszej precyzji obliczeĂ„Ä…Ă˘â‚¬Ĺľ
    double userLatForDistance = userLatLonValid ? userLat : 52.40;  // Fallback jeĂ„Ä…Ă˘â‚¬Ĺźli nie ustawione
    double userLonForDistance = userLatLonValid ? userLon : 16.92;
    double stationLat = (double)station.lat;
    double stationLon = (double)station.lon;
    
    station.distance = calculateDistance(userLatForDistance, userLonForDistance, 
                                         stationLat, stationLon);
    LOGV_PRINTF("[APRS] Distance: user(%.6f,%.6f) -> station(%.6f,%.6f) = %.1f km\n",
                userLatForDistance, userLonForDistance, stationLat, stationLon, station.distance);
  }
  
  return true;
}

// Dodaj nowÄ‚â€žĂ˘â‚¬Â¦ stacjÄ‚â€žĂ˘â€žË APRS do tablicy (bez duplikatĂ„â€šĹ‚w - aktualizuje istniejÄ‚â€žĂ˘â‚¬Â¦ce)
void addAPRSStation(APRSStation station) {
  compactAprsStationStrings(station);
  LOGV_PRINT("[APRS] addAPRSStation: ");
  LOGV_PRINT(station.callsign);
  if (station.hasLatLon) {
    LOGV_PRINT(" @ ");
    if (LOG_VERBOSE) Serial.print(station.lat, 4);
    LOGV_PRINT(",");
    if (LOG_VERBOSE) Serial.print(station.lon, 4);
    LOGV_PRINT(" (");
    if (LOG_VERBOSE) Serial.print(station.distance, 1);
    LOGV_PRINT(" km)");
  }
  LOGV_PRINTLN();
  
  // SprawdĂ„Ä…ÄąĹş czy stacja juĂ„Ä…Ă„Ëť istnieje (po callsign)
  int existingIndex = -1;
  for (int i = 0; i < aprsStationCount; i++) {
    if (aprsStations[i].callsign.equalsIgnoreCase(station.callsign)) {
      existingIndex = i;
      break;
    }
  }
  
  if (existingIndex >= 0) {
    // Stacja juĂ„Ä…Ă„Ëť istnieje - usuĂ„Ä…Ă˘â‚¬Ĺľ jÄ‚â€žĂ˘â‚¬Â¦ z obecnej pozycji
    for (int i = existingIndex; i > 0; i--) {
      aprsStations[i] = aprsStations[i - 1];
    }
    // Zaktualizuj dane stacji (nowy czas, pozycja, itp.)
    aprsStations[0] = station;
    LOGV_PRINT("[APRS] Stacja juĂ„Ä…Ă„Ëť istnieje - zaktualizowano na pozycji 0");
  } else {
    // Nowa stacja - dodaj na poczÄ‚â€žĂ˘â‚¬Â¦tku
    if (aprsStationCount < MAX_APRS_STATIONS) {
      aprsStationCount++;
    }
    
    // PrzesuĂ„Ä…Ă˘â‚¬Ĺľ wszystkie stacje o jeden w dĂ„â€šĹ‚Ă„Ä…Ă˘â‚¬Ĺˇ
    for (int i = MAX_APRS_STATIONS - 1; i > 0; i--) {
      aprsStations[i] = aprsStations[i - 1];
    }
    
    // Dodaj nowÄ‚â€žĂ˘â‚¬Â¦ stacjÄ‚â€žĂ˘â€žË na poczÄ‚â€žĂ˘â‚¬Â¦tku
    aprsStations[0] = station;
    LOGV_PRINT("[APRS] Dodano nowÄ‚â€žĂ˘â‚¬Â¦ stacjÄ‚â€žĂ˘â€žË");
  }
  
  LOGV_PRINT("[APRS] addAPRSStation END, nowy aprsStationCount=");
  LOGV_PRINTLN(aprsStationCount);
}

// ObsĂ„Ä…Ă˘â‚¬Ĺˇuga danych z APRS-IS
void handleAPRSData() {
  if (!aprsConnected || !aprsClient.connected()) {
    if (aprsConnected) {
      LOGV_PRINTLN("[APRS] PoĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czenie zerwane - reset flagĂ„â€šĹ‚w");
    }
    aprsConnected = false;
    aprsLoginSent = false;
    return;
  }
  
  // Odczytaj dostÄ‚â€žĂ˘â€žËpne dane
  while (aprsClient.available()) {
    unsigned char c = (unsigned char)aprsClient.read();
    lastAPRSRxMs = millis();
    
    if (c == '\n' || c == '\r') {
      if (aprsBuffer.length() > 0) {
        String line = aprsBuffer;
        aprsBuffer = "";
        
        // Ignoruj linie zaczynajÄ‚â€žĂ˘â‚¬Â¦ce siÄ‚â€žĂ˘â€žË od # (komentarze serwera)
        if (line.startsWith("#")) {
          Serial.print("[APRS] Server: ");
          Serial.println(line);
          continue;
        }
        
        // Parsuj ramkÄ‚â€žĂ˘â€žË APRS
        APRSStation station;
        if (parseAPRSFrame(line, station)) {
          addAPRSStation(station);

          if (aprsAlertEnabled && shouldTriggerAprsAlert(station)) {
            Serial.print("[APRS ALERT] stacja wykryta opisana w formacie tnc: ");
            Serial.println(line);
            triggerAprsRgbLedAlert();
            #ifdef ENABLE_TFT_DISPLAY
            portENTER_CRITICAL(&aprsAlertPendingMux);
            aprsAlertPendingStation = station;
            aprsAlertDrawPending = true;
            portEXIT_CRITICAL(&aprsAlertPendingMux);
            #endif
          }
        }
      }
    } else if (c != 0) {
      // Keep control bytes (e.g. 0x1C/0x1D) for Mic-E decode compatibility.
      aprsBuffer += (char)c;
      // Ograniczenie dĂ„Ä…Ă˘â‚¬ĹˇugoĂ„Ä…Ă˘â‚¬Ĺźci bufora (zapobieganie przepeĂ„Ä…Ă˘â‚¬Ĺˇnieniu)
      if (aprsBuffer.length() > 512) {
        aprsBuffer = aprsBuffer.substring(aprsBuffer.length() - 256);
      }
    }
  }
}

// ========== WIFI MANAGER ==========

void startAPMode() {
  // Stabilny portal konfiguracyjny: sam AP (bez STA w tle),
  // Ă„Ä…Ă„Ëťeby nie gubiÄ‚â€žĂ˘â‚¬Ë‡ poĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czenia na telefonie/PC przy zmianie kanaĂ„Ä…Ă˘â‚¬Ĺˇu przez STA.
  WiFi.mode(WIFI_AP);
  WiFi.setTxPower(WIFI_POWER_11dBm); // ogranicz prad WiFi - mniejsze ryzyko brownout/resetow
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.println("AP Mode uruchomiony");
  Serial.print("SSID: ");
  Serial.println(AP_SSID);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
  
  // Aktualizuj wyĂ„Ä…Ă˘â‚¬Ĺźwietlacz TFT z IP AP (jeĂ„Ä…Ă˘â‚¬Ĺźli jesteĂ„Ä…Ă˘â‚¬Ĺźmy na ekranie 1)
#ifdef ENABLE_TFT_DISPLAY
  updateScreen1();
#endif
}

void requestRestart(unsigned long delayMs = 1500) {
  restartRequested = true;
  restartAtMs = millis() + delayMs;
}

// Powod ostatniego rozlaczenia STA - zapisywany przez handler WiFi.onEvent.
static volatile int g_lastWifiReason = 0;
// BSSID (adres MAC AP) znaleziony w skanie - przypinamy sie do konkretnego
// radia, zeby uniknac odrzucenia przez band steering / Smart Connect.
static uint8_t g_targetBssid[6] = {0};
static bool g_haveBssid = false;

// Jedna proba polaczenia STA przy danej mocy nadawania.
// AUTH_EXPIRE (2) powtarzane przy pelnej mocy to czesto problem zasilania
// (skok pradu przy TX) albo routera - dlatego probujemy kilku poziomow mocy.
static bool wifiAttemptAtPower(const String &ssid, const String &pass, int channel,
                               wifi_power_t power, int timeoutMs) {
  g_lastWifiReason = 0;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(200);
  WiFi.mode(WIFI_STA);
  delay(50);
  WiFi.setTxPower(power);
  if (channel > 0 && g_haveBssid) {
    WiFi.begin(ssid.c_str(), pass.c_str(), channel, g_targetBssid);
  } else if (channel > 0) {
    WiFi.begin(ssid.c_str(), pass.c_str(), channel);
  } else {
    WiFi.begin(ssid.c_str(), pass.c_str());
  }
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < (unsigned long)timeoutMs) {
    delay(250);
    yield();
    Serial.print(".");
  }
  Serial.println("");
  return WiFi.status() == WL_CONNECTED;
}

bool connectToWiFi() {
  if (wifiSSID.length() == 0 && wifiSSID2.length() == 0) {
    return false;
  }

  struct WiFiCred {
    String ssid;
    String pass;
  };

  WiFiCred candidates[2] = {{wifiSSID, wifiPassword}, {wifiSSID2, wifiPassword2}};

  // Jednorazowa rejestracja handlera zdarzen - loguje powod rozlaczenia
  // (np. NO_AP_FOUND, 4WAY_HANDSHAKE_TIMEOUT, AUTH_EXPIRE) i powody skanu.
  static bool wifiEventsRegistered = false;
  if (!wifiEventsRegistered) {
    wifiEventsRegistered = true;
    WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
      switch (event) {
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
          wifi_err_reason_t r = (wifi_err_reason_t)info.wifi_sta_disconnected.reason;
          g_lastWifiReason = (int)r;
          Serial.printf("[WIFI EVT] DISCONNECTED reason=%u (%s)\n", (unsigned)r,
                        WiFi.disconnectReasonName(r));
          break;
        }
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
          Serial.println("[WIFI EVT] CONNECTED (auth OK)");
          break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
          Serial.println("[WIFI EVT] GOT_IP");
          break;
        default:
          break;
      }
    });
  }

  // Wykryj, czy siec z danym SSID jest widoczna i z jaka autoryzacja.
  // Gdy znamy kanal, laczymy sie bezposrednio (bez wewnetrznego skanu begin),
  // co omija problemy z uzgadnianiem trybu/kanalu przy begin() w 2.0.x.
  auto findNetwork = [](const String &ssid, int &channel, int &authMode) -> bool {
    channel = 0;
    authMode = -1;
    g_haveBssid = false;
    int16_t n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true);
    if (n == WIFI_SCAN_RUNNING) {
      delay(200);
      n = WiFi.scanComplete();
    }
    if (n < 0) {
      Serial.printf("[WIFI SCAN] Nieudany skan (kod %d) - polacze sie bezposrednio.\n", (int)n);
      return false;
    }
    bool found = false;
    for (int i = 0; i < n; i++) {
      if (WiFi.SSID(i) == ssid) {
        found = true;
        channel = WiFi.channel(i);
        authMode = (int)WiFi.encryptionType(i);
        uint8_t *bssid = WiFi.BSSID(i);
        if (bssid != nullptr) {
          memcpy(g_targetBssid, bssid, 6);
          g_haveBssid = true;
        }
        Serial.printf("[WIFI SCAN] %s widoczna: RSSI=%d ch=%d enc=%d bssid=%02X:%02X:%02X:%02X:%02X:%02X\n",
                      ssid.c_str(), (int)WiFi.RSSI(i), channel, authMode,
                      g_targetBssid[0], g_targetBssid[1], g_targetBssid[2],
                      g_targetBssid[3], g_targetBssid[4], g_targetBssid[5]);
        break;
      }
    }
    if (!found) {
      Serial.printf("[WIFI SCAN] %s NIEWIDOCZNA (znaleziono %d sieci)\n", ssid.c_str(), (int)n);
    }
    WiFi.scanDelete();
    return found;
  };

  auto attemptConnect = [](const WiFiCred &cred, int channel) -> bool {
    Serial.println("=== WiFi connect ===");
    Serial.print("SSID: '");
    Serial.print(cred.ssid);
    Serial.print("' (len=");
    Serial.print(cred.ssid.length());
    Serial.println(")");
    Serial.print("PASS len=");
    Serial.println(cred.pass.length());

    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);
    // Akceptuj WPA/WPA2/WPA3-mixed i otwarte - domyslne WIFI_AUTH_WPA2_PSK
    // w 2.0.x potrafi odrzucic sieci z mieszana autoryzacja.
    WiFi.setMinSecurity(WIFI_AUTH_OPEN);
    // Pelny skan wszystkich kanalow + sortowanie po sygnale = najpewniejsze
    // polaczenie, gdy nie znamy kanalu.
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

    // Drabinka mocy TX. Powtarzane AUTH_EXPIRE (reason 2) przy pelnej mocy to
    // typowy objaw problemu zasilania (skok pradu przy TX) albo routera -
    // obnizenie mocy czesto dziala. 0 = pelna moc (domyslna).
    wifi_power_t powers[] = {WIFI_POWER_19_5dBm, WIFI_POWER_15dBm, WIFI_POWER_11dBm};
    for (int p = 0; p < 3; p++) {
      Serial.printf("[WIFI] Proba, moc=%d (idx %d)\n", (int)powers[p], p);
      if (wifiAttemptAtPower(cred.ssid, cred.pass, channel, powers[p], 9000)) {
        return true;
      }
      Serial.printf("[WIFI] Nieudane przy mocy idx %d, reason=%d\n", p, (int)g_lastWifiReason);
      // Reason 201 = AP chwilowo nieosiagalny; daj routerowi chwile i sprobuj
      // na tym samym poziomie mocy raz jeszcze, zanim obnizymy moc.
      if (g_lastWifiReason == 201) {
        delay(500);
        if (wifiAttemptAtPower(cred.ssid, cred.pass, channel, powers[p], 9000)) {
          return true;
        }
      }
    }

    Serial.println("");
    Serial.println("BĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦d poĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czenia WiFi");
    Serial.print("WiFi.status(): ");
    Serial.println((int)WiFi.status());

    Serial.println("--- WiFi diag ---");
    WiFi.printDiag(Serial);
    Serial.println("--- Scan networks ---");
    int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true);
    Serial.print("Znaleziono sieci: ");
    Serial.println(n);
    bool found = false;
    for (int i = 0; i < n; i++) {
      String s = WiFi.SSID(i);
      int32_t rssi = WiFi.RSSI(i);
      wifi_auth_mode_t enc = WiFi.encryptionType(i);
      if (s == cred.ssid) {
        found = true;
        Serial.print("MATCH SSID: ");
        Serial.print(s);
        Serial.print(" RSSI=");
        Serial.print(rssi);
        Serial.print(" enc=");
        Serial.println((int)enc);
      }
    }
    if (!found) {
      Serial.println("UWAGA: Nie widzÄ‚â€žĂ˘â€žË Twojego SSID w skanie (moĂ„Ä…Ă„Ëťe 5GHz / inny SSID / poza zasiÄ‚â€žĂ˘â€žËgiem / ukryte SSID).");
    }

    return false;
  };

  for (int i = 0; i < 2; i++) {
    const WiFiCred &cred = candidates[i];
    if (cred.ssid.length() == 0) {
      continue;
    }

    if (i == 1 && cred.ssid == candidates[0].ssid) {
      continue; // avoid duplicate attempt
    }

    int knownChannel = 0;
    int knownAuth = -1;
    findNetwork(cred.ssid, knownChannel, knownAuth);

    bool ok = attemptConnect(cred, knownChannel);
    if (ok) {
      wifiConnected = true;
      Serial.print("PoĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czono z SSID: ");
      Serial.println(cred.ssid);
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());

      // Aktualizuj wyĂ„Ä…Ă˘â‚¬Ĺźwietlacz TFT z IP STA (jeĂ„Ä…Ă˘â‚¬Ĺźli jesteĂ„Ä…Ă˘â‚¬Ĺźmy na ekranie 1)
#ifdef ENABLE_TFT_DISPLAY
      updateScreen1();
#endif

      return true;
    } else {
      Serial.println("PrĂ„â€šĹ‚ba poĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czenia nieudana dla SSID: " + cred.ssid);
    }
  }

  wifiConnected = false;
  return false;
}

// Rekonfiguracja WiFi w locie - bez restartu ESP.
// Gdy jestesmy w AP i podano dane STA: polacz sie z STA, potem wylacz AP.
// Gdy jestesmy w STA i zmieniono SSID: przelacz na nowe SSID.
// Gdy usunieto SSID: wroc do AP (portal konfiguracyjny).
void connectToCluster();
void connectToPotaCluster();
void updateNTPTime();
void wifiReconfigure() {
  Serial.println("[WIFI] Rekonfiguracja w locie...");
  bool apActive = (WiFi.getMode() & WIFI_AP) != 0;
  bool haveCfg = (wifiSSID.length() > 0 || wifiSSID2.length() > 0);

  if (!haveCfg) {
    // Brak SSID -> zostan/wroc do AP
    if (!apActive) startAPMode();
    wifiConnected = false;
    return;
  }

  // Proba polaczenia STA (connectToWiFi sam zeruje tryb i wlacza STA).
  if (connectToWiFi()) {
    Serial.println("[WIFI] STA OK. " );
    if (apActive) {
      Serial.println("[WIFI] Wylaczam AP po polaczeniu STA.");
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
    }
    updateNTPTime();
    connectToCluster();
    connectToPotaCluster();
    connectToAPRS();
  } else {
    Serial.println("[WIFI] Nieudane. Zostaje/uruchamiam AP.");
    startAPMode();
  }
}

// ========== TELNET CLUSTER ==========

void connectToCluster() {
  Serial.println("[CLUSTER] connectToCluster() START");
  
  if (!wifiConnected) {
    Serial.println("[CLUSTER] WiFi nie poĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czony - wyjĂ„Ä…Ă˘â‚¬Ĺźcie");
    return;
  }
  
  if (telnetClient.connected()) {
    Serial.println("[CLUSTER] JuĂ„Ä…Ă„Ëť poĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czony - wyjĂ„Ä…Ă˘â‚¬Ĺźcie");
    return;
  }
  
  unsigned long now = millis();
  if (now - lastTelnetAttempt < 5000) {
    return; // Cichy return - nie spamuj logu (to byĂ„Ä…Ă˘â‚¬Ĺˇo gĂ„Ä…Ă˘â‚¬ĹˇĂ„â€šĹ‚wne Ă„Ä…ÄąĹşrĂ„â€šĹ‚dĂ„Ä…Ă˘â‚¬Ĺˇo spamu!)
  }
  
  lastTelnetAttempt = now;
  
  Serial.print("[CLUSTER] Ă„Ä…Ă‚ÂÄ‚â€žĂ˘â‚¬Â¦czenie z DX Cluster: ");
  Serial.print(clusterHost);
  Serial.print(":");
  Serial.println(clusterPort);

  // Diagnostyka DNS (czÄ‚â€žĂ˘â€žËsty problem gdy host nie rozwiÄ‚â€žĂ˘â‚¬Â¦zuje siÄ‚â€žĂ˘â€žË na ESP32)
  IPAddress resolvedIp;
  if (WiFi.hostByName(clusterHost.c_str(), resolvedIp)) {
    Serial.print("DNS OK: ");
    Serial.print(clusterHost);
    Serial.print(" -> ");
    Serial.println(resolvedIp);
  } else {
    Serial.print("DNS FAIL dla hosta: ");
    Serial.println(clusterHost);
  }
  
  Serial.println("[CLUSTER] WywoĂ„Ä…Ă˘â‚¬Ĺˇanie telnetClient.connect()...");
  unsigned long connectStart = millis();
  
  telnetClient.setTimeout(2500);
  if (telnetClient.connect(clusterHost.c_str(), clusterPort)) {
    unsigned long connectTime = millis() - connectStart;
    Serial.print("[CLUSTER] PoĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czono z DX Cluster! (czas: ");
    Serial.print(connectTime);
    Serial.println("ms)");
    telnetConnected = true;
    telnetBuffer = "";
    clusterLoginSent = false;
    clusterLoginScheduled = false;
    lastClusterKeepAliveMs = millis();
    lastTelnetRxMs = millis();

    // Zawsze wysyĂ„Ä…Ă˘â‚¬Ĺˇamy login po poĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czeniu (tak jak w projekcie referencyjnym).
    // JeĂ„Ä…Ă˘â‚¬Ĺźli uĂ„Ä…Ă„Ëťytkownik nie ustawiĂ„Ä…Ă˘â‚¬Ĺˇ znaku, uĂ„Ä…Ă„Ëťywamy domyĂ„Ä…Ă˘â‚¬Ĺźlnego (DEFAULT_CALLSIGN).
    clusterLoginScheduled = true;
    clusterSendLoginAtMs = millis() + 1000;
  } else {
    unsigned long connectTime = millis() - connectStart;
    Serial.print("[CLUSTER] BĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦d poĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czenia z DX Cluster (czas: ");
    Serial.print(connectTime);
    Serial.println("ms)");
    telnetConnected = false;
  }
  Serial.println("[CLUSTER] connectToCluster() END");
}

void connectToPotaCluster() {
  if (!wifiConnected) {
    return;
  }
  if (potaClusterHost.length() == 0 || potaClusterPort <= 0) {
    return;
  }
  if (potaTelnetClient.connected()) {
    return;
  }

  unsigned long now = millis();
  if (now - lastPotaAttempt < 5000) {
    return;
  }
  lastPotaAttempt = now;

  Serial.print("[POTA] Ă„Ä…Ă‚ÂÄ‚â€žĂ˘â‚¬Â¦czenie z POTA Cluster: ");
  Serial.print(potaClusterHost);
  Serial.print(":");
  Serial.println(potaClusterPort);

  IPAddress resolvedIp;
  if (WiFi.hostByName(potaClusterHost.c_str(), resolvedIp)) {
    Serial.print("[POTA] DNS OK: ");
    Serial.print(potaClusterHost);
    Serial.print(" -> ");
    Serial.println(resolvedIp);
  } else {
    Serial.print("[POTA] DNS FAIL: ");
    Serial.println(potaClusterHost);
  }

  potaTelnetClient.setTimeout(2500);
  if (potaTelnetClient.connect(potaClusterHost.c_str(), potaClusterPort)) {
    Serial.println("[POTA] PoĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czono z POTA Cluster!");
    potaTelnetConnected = true;
    potaTelnetBuffer = "";
    pendingPotaLine = "";
    potaLoginSent = false;
    potaLoginScheduled = false;
    lastPotaKeepAliveMs = millis();
    lastPotaRxMs = millis();

    String login = userCallsign;
    login.trim();
    if (login.length() == 0) {
      login = DEFAULT_CALLSIGN;
    }
    if (userLocator.length() >= 4) {
      login += "/";
      login += userLocator;
    }
    potaTelnetClient.print(login);
    potaTelnetClient.print("\r\n");
    potaLoginSent = true;
    lastPotaKeepAliveMs = millis();

    if (potaFilterCommand.length() > 0) {
      potaTelnetClient.print(potaFilterCommand);
      potaTelnetClient.print("\r\n");
    }
  } else {
    Serial.println("[POTA] BĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦d poĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czenia z POTA Cluster");
    potaTelnetConnected = false;
  }
}

// WyĂ„Ä…Ă˘â‚¬Ĺźlij komendÄ‚â€žĂ˘â€žË konfiguracyjnÄ‚â€žĂ˘â‚¬Â¦ do DX Cluster (CC-Cluster)
// UWAGA: UĂ„Ä…Ă„Ëťywane TYLKO do konfiguracji odbioru (set/noann, set/nowwv, set/filter, etc.)
// NIE wysyĂ„Ä…Ă˘â‚¬Ĺˇa Ă„Ä…Ă„Ëťadnych spotĂ„â€šĹ‚w - urzÄ‚â€žĂ˘â‚¬Â¦dzenie dziaĂ„Ä…Ă˘â‚¬Ĺˇa tylko w trybie odbioru
void sendClusterCommand(String command) {
  if (!telnetConnected || !telnetClient.connected()) {
    Serial.print("[CLUSTER] Nie moĂ„Ä…Ă„Ëťna wysĂ„Ä…Ă˘â‚¬ĹˇaÄ‚â€žĂ˘â‚¬Ë‡ komendy '");
    Serial.print(command);
    Serial.println("' - brak poĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czenia");
    return;
  }
  
  Serial.print("[CLUSTER] WysyĂ„Ä…Ă˘â‚¬Ĺˇanie komendy: ");
  Serial.println(command);
  telnetClient.print(command);
  telnetClient.print("\r\n");
  delay(50); // KrĂ„â€šĹ‚tkie opĂ„â€šĹ‚Ă„Ä…ÄąĹşnienie miÄ‚â€žĂ˘â€žËdzy komendami
}

// WyĂ„Ä…Ă˘â‚¬Ĺźlij komendy konfiguracyjne CC-Cluster po zalogowaniu
void sendClusterConfigCommands() {
  if (!telnetConnected || !telnetClient.connected()) {
    return;
  }
  
  Serial.println("[CLUSTER] WysyĂ„Ä…Ă˘â‚¬Ĺˇanie komend konfiguracyjnych CC-Cluster...");
  
  // WyĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦cz ogĂ„Ä…Ă˘â‚¬Ĺˇoszenia (domyĂ„Ä…Ă˘â‚¬Ĺźlnie wĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czone)
  if (clusterNoAnnouncements) {
    sendClusterCommand("set/noann");
  }
  
  // WyĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦cz WWV (domyĂ„Ä…Ă˘â‚¬Ĺźlnie wĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czone)
  if (clusterNoWWV) {
    sendClusterCommand("set/nowwv");
  }
  
  // WyĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦cz WCY (domyĂ„Ä…Ă˘â‚¬Ĺźlnie wĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czone)
  if (clusterNoWCY) {
    sendClusterCommand("set/nowcy");
  }
  
  // Ustaw filtry (jeĂ„Ä…Ă˘â‚¬Ĺźli wĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czone)
  if (clusterUseFilters && clusterFilterCommands.length() > 0) {
    // Parsuj i wyĂ„Ä…Ă˘â‚¬Ĺźlij komendy filtrĂ„â€šĹ‚w (moĂ„Ä…Ă„Ëťe byÄ‚â€žĂ˘â‚¬Ë‡ kilka linii oddzielonych \n)
    String filters = clusterFilterCommands;
    int pos = 0;
    while (pos < filters.length()) {
      int nextPos = filters.indexOf('\n', pos);
      if (nextPos < 0) {
        nextPos = filters.length();
      }
      String cmd = filters.substring(pos, nextPos);
      cmd.trim();
      if (cmd.length() > 0) {
        sendClusterCommand(cmd);
      }
      pos = nextPos + 1;
    }
  } else {
    // JeĂ„Ä…Ă˘â‚¬Ĺźli filtry wyĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czone, wyczyĂ„Ä…Ă˘â‚¬ĹźÄ‚â€žĂ˘â‚¬Ë‡ wszystkie filtry
    sendClusterCommand("set/nofilter");
  }
  
  Serial.println("[CLUSTER] Komendy konfiguracyjne wysĂ„Ä…Ă˘â‚¬Ĺˇane");
}

void handleTelnetData() {
  static unsigned long lastTelnetPrint = 0;
  static int telnetCallCount = 0;
  telnetCallCount++;
  
  if (!telnetConnected || !telnetClient.connected()) {
    if (telnetConnected) {
      LOGV_PRINTLN("[TELNET] PoĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czenie zerwane - reset flagĂ„â€šĹ‚w");
    }
    telnetConnected = false;
    clusterLoginSent = false;
    clusterLoginScheduled = false;
    return;
  }
  
  // Print co 1000 wywoĂ„Ä…Ă˘â‚¬ĹˇaĂ„Ä…Ă˘â‚¬Ĺľ (dla debugowania)
  unsigned long now = millis();
  if (now - lastTelnetPrint > 30000) { // Co 30 sekund
    LOGV_PRINT("[TELNET] handleTelnetData wywoĂ„Ä…Ă˘â‚¬Ĺˇane ");
    LOGV_PRINT(telnetCallCount);
    LOGV_PRINT(" razy, available=");
    LOGV_PRINTLN(telnetClient.available());
    lastTelnetPrint = now;
    telnetCallCount = 0;
  }
  
  // Nie blokuj pÄ‚â€žĂ˘â€žËtli gĂ„Ä…Ă˘â‚¬ĹˇĂ„â€šĹ‚wnej: w jednej iteracji czytaj maksymalnie N bajtĂ„â€šĹ‚w
  // i dawaj yield, Ă„Ä…Ă„Ëťeby nie wpaĂ„Ä…Ă˘â‚¬ĹźÄ‚â€žĂ˘â‚¬Ë‡ w WDT przy duĂ„Ä…Ă„Ëťym strumieniu.
  int processed = 0;
  const int maxPerLoop = 256;
  int availableBefore = telnetClient.available();
  
  while (telnetClient.available() && processed < maxPerLoop) {
    char c = telnetClient.read();
    processed++;
    lastTelnetRxMs = millis();
    if ((processed % 64) == 0) {
      yield();
      // obsĂ„Ä…Ă˘â‚¬ĹˇuĂ„Ä…Ă„Ëť WWW nawet jeĂ„Ä…Ă˘â‚¬Ĺźli telnet zalewa danymi
      if (server != nullptr) {
        server->handleClient();
      }
    }
    
    if (c == '\n' || c == '\r') {
      if (telnetBuffer.length() > 0) {
        // Nie parsuj tu (to bywa kosztowne). PrzekaĂ„Ä…Ă„Ëť liniÄ‚â€žĂ˘â€žË do przetworzenia w loop().
        if (pendingTelnetLine.length() == 0) {
          pendingTelnetLine = telnetBuffer;
        } else {
          pendingTelnetDropped++;
        }
        telnetBuffer = "";
      }
    } else if (c >= 32 && c < 127) {
      telnetBuffer += c;
      if (telnetBuffer.length() > 512) {
        telnetBuffer = ""; // Ochrona przed przepeĂ„Ä…Ă˘â‚¬Ĺˇnieniem
      }
    }
  }
  
  if (processed > 0) {
    LOGV_PRINT("[TELNET] Przetworzono ");
    LOGV_PRINT(processed);
    LOGV_PRINT(" bajtĂ„â€šĹ‚w (byĂ„Ä…Ă˘â‚¬Ĺˇo ");
    LOGV_PRINT(availableBefore);
    LOGV_PRINT(", zostaĂ„Ä…Ă˘â‚¬Ĺˇo ");
    LOGV_PRINT(telnetClient.available());
    LOGV_PRINTLN(")");
  }
  
  if (telnetClient.available() && processed >= maxPerLoop) {
    LOGV_PRINTLN("[TELNET] WARNING: ZostaĂ„Ä…Ă˘â‚¬Ĺˇo wiÄ‚â€žĂ˘â€žËcej danych - nastÄ‚â€žĂ˘â€žËpna iteracja");
    // Zostaw resztÄ‚â€žĂ˘â€žË na nastÄ‚â€žĂ˘â€žËpnÄ‚â€žĂ˘â‚¬Â¦ iteracjÄ‚â€žĂ˘â€žË loop()
    yield();
  }
  
  // SprawdĂ„Ä…ÄąĹş czy poĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czenie nadal dziaĂ„Ä…Ă˘â‚¬Ĺˇa
  if (!telnetClient.connected()) {
    LOGV_PRINTLN("[TELNET] RozĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czono z DX Cluster");
    telnetConnected = false;
    clusterLoginSent = false;
    clusterLoginScheduled = false;
  }
}

void handlePotaTelnetData() {
  if (!potaTelnetConnected || !potaTelnetClient.connected()) {
    if (potaTelnetConnected) {
      LOGV_PRINTLN("[POTA] PoĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czenie zerwane - reset flag");
    }
    potaTelnetConnected = false;
    potaLoginSent = false;
    potaLoginScheduled = false;
    return;
  }

  int processed = 0;
  const int maxPerLoop = 256;
  while (potaTelnetClient.available() && processed < maxPerLoop) {
    char c = potaTelnetClient.read();
    processed++;
    lastPotaRxMs = millis();
    if ((processed % 64) == 0) {
      yield();
      if (server != nullptr) {
        server->handleClient();
      }
    }

    if (c == '\n' || c == '\r') {
      if (potaTelnetBuffer.length() > 0) {
        if (pendingPotaLine.length() == 0) {
          pendingPotaLine = potaTelnetBuffer;
        } else {
          pendingPotaDropped++;
        }
        potaTelnetBuffer = "";
      }
    } else if (c >= 32 && c < 127) {
      potaTelnetBuffer += c;
      if (potaTelnetBuffer.length() > 512) {
        potaTelnetBuffer = "";
      }
    }
  }

  if (!potaTelnetClient.connected()) {
    LOGV_PRINTLN("[POTA] RozĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czono z POTA Cluster");
    potaTelnetConnected = false;
    potaLoginSent = false;
    potaLoginScheduled = false;
  }
}

// ========== NTP TIME ==========

void updateNTPTime() {
  unsigned long now = millis();
  if (lastNTPUpdate != 0 && (now - lastNTPUpdate < 3600000)) { // Aktualizuj co godzinÄ‚â€žĂ˘â€žË
    return;
  }
  
  if (!wifiConnected) {
    return;
  }
  
  Serial.println("[NTP] Aktualizacja czasu NTP...");
  configTime(GMT_OFFSET_SEC, 0, NTP_SERVER);
  lastNTPUpdate = now;
  
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    Serial.println("[NTP] Czas NTP zsynchronizowany");
  } else {
    Serial.println("[NTP] BĂ„Ä…Ă‚ÂÄ‚â€žĂ˘â‚¬ĹľD: Nie udaĂ„Ä…Ă˘â‚¬Ĺˇo siÄ‚â€žĂ˘â€žË pobraÄ‚â€žĂ˘â‚¬Ë‡ czasu NTP");
  }
  checkScreenSaverTimeout(); // Dodanie wywoĂ„Ä…Ă˘â‚¬Ĺˇania funkcji
}

// ========== WYGASZACZ EKRANU (Matrix) ==========

void resetScreenSaverActivity() {
  screenSaverLastActivityMs = millis();
  if (screenSaverActive) {
    // WyĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦cz wygaszacz i wrĂ„â€šĹ‚Ä‚â€žĂ˘â‚¬Ë‡ do poprzedniego ekranu
    screenSaverActive = false;
    if (screenSaverPrevScreen != SCREEN_OFF) {
      drawScreen(screenSaverPrevScreen);
    }
  }
}

void checkScreenSaverTimeout() {
  if (!screenSaverEnabled || screenSaverActive) {
    return;
  }
  unsigned long now = millis();
  unsigned long timeoutMs = (unsigned long)screenSaverTimeoutMin * 60000UL; // minuty na ms
  if (now - screenSaverLastActivityMs >= timeoutMs) {
    // WĹ‚Ä…cz wygaszacz z poruszajÄ…cym siÄ™ zegarem
    screenSaverActive = true;
    screenSaverPrevScreen = currentScreen;
    // WyczyĹ›Ä‡ ekran i zresetuj pozycjÄ™ zegara
    tft.fillScreen(TFT_BLACK);
    bounceClockX = 240;
    bounceClockY = 160;
    bounceClockVX = 2.5;
    bounceClockVY = 2.0;
    bounceClockColor = TFT_RADIO_ORANGE;
    lastBounceClockText = "";
  }
}

void setScreenSaverEnabled(bool enabled) {
  screenSaverEnabled = enabled;
  savePreferences();
}

void setScreenSaverTimeout(int minutes) {
  if (minutes < 1) minutes = 1;
  if (minutes > 60) minutes = 60;
  screenSaverTimeoutMin = minutes;
  savePreferences();
}

void drawQrzPopup() {
  Serial.print("[QRZ POPUP] drawQrzPopup called, active=");
  Serial.print(qrzPopupActive ? "true" : "false");
  Serial.print(" hasLatLon=");
  Serial.print(qrzPopupHasLatLon ? "true" : "false");
  Serial.print(" lat=");
  Serial.print(qrzPopupLat, 6);
  Serial.print(" lon=");
  Serial.println(qrzPopupLon, 6);
  if (!qrzPopupActive) return;

  const int popupW = 400;
  const int popupH = 220;
  const int popupX = (480 - popupW) / 2;
  const int popupY = (320 - popupH) / 2;

  // TĹ‚o popupu
  tft.fillRect(popupX, popupY, popupW, popupH, TFT_DARKGREY);
  tft.drawRect(popupX, popupY, popupW, popupH, TFT_WHITE);
  tft.drawRect(popupX + 2, popupY + 2, popupW - 4, popupH - 4, TFT_RADIO_ORANGE);

  // NagĹ‚Ăłwek z callsign i imieniem
  tft.fillRect(popupX + 4, popupY + 4, popupW - 8, 30, TFT_RADIO_ORANGE);
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(popupX + 10, popupY + 10);
  tft.print(qrzPopupCallsign);
  
  // ImiÄ™ i nazwisko w nagĹ‚Ăłwku (jeĹ›li dostÄ™pne)
  if (qrzPopupName.length() > 0) {
    tft.setTextSize(1);
    String nameText = " - " + qrzPopupName;
    // Oblicz pozycjÄ™ za callsign
    int nameX = popupX + 10 + (qrzPopupCallsign.length() * 12);
    tft.setCursor(nameX, popupY + 14);
    tft.print(nameText);
  }

  // Dane
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  int y = popupY + 45;

  if (qrzPopupName.length() > 0) {
    tft.setCursor(popupX + 10, y);
    tft.print("Name: ");
    tft.print(qrzPopupName);
    y += 20;
  }

  if (qrzPopupGrid.length() > 0) {
    tft.setCursor(popupX + 10, y);
    tft.print("Grid: ");
    tft.print(qrzPopupGrid);
    y += 20;
  }

  if (qrzPopupCountry.length() > 0) {
    tft.setCursor(popupX + 10, y);
    tft.print("Country: ");
    tft.print(qrzPopupCountry);
    y += 20;
  }

  if (qrzPopupQth.length() > 0) {
    tft.setCursor(popupX + 10, y);
    tft.print("QTH: ");
    tft.print(qrzPopupQth);
    y += 20;
  }

  if (qrzPopupEmail.length() > 0) {
    tft.setCursor(popupX + 10, y);
    tft.print("Email: ");
    tft.print(qrzPopupEmail);
    y += 20;
  }

  if (qrzPopupDistance > 0.0) {
    tft.setCursor(popupX + 10, y);
    tft.print("Distance: ");
    tft.print(qrzPopupDistance, 0);
    tft.print(" km");
    y += 20;
  }

  // Przycisk zamknij
  const int closeW = 80;
  const int closeH = 30;
  const int closeX = popupX + (popupW - closeW) / 2;
  const int closeY = popupY + popupH - closeH - 15;
  drawFilterTile(closeX, closeY, closeW, closeH, "CLOSE", false);

  // Mapa Ĺ›wiata w prawym gĂłrnym rogu popupu
  if (qrzPopupHasLatLon) {
    const int mapW = 140;
    const int mapH = 80;
    const int mapX = popupX + popupW - mapW - 10;
    const int mapY = popupY + 45;
    
    Serial.print("[QRZ MAP] Drawing map, hasLatLon=true, lat=");
    Serial.print(qrzPopupLat, 6);
    Serial.print(" lon=");
    Serial.println(qrzPopupLon, 6);
    
    // WyĹ›wietl plik mapa.bmp z folderu data
    bool mapLoaded = false;
    if (littleFsReady && LittleFS.exists("/mapa.bmp")) {
      Serial.println("[QRZ MAP] Found /mapa.bmp, attempting to load...");
      mapLoaded = drawBmp16FromFS("/mapa.bmp", mapX, mapY);
      Serial.print("[QRZ MAP] BMP load result: ");
      Serial.println(mapLoaded ? "SUCCESS" : "FAILED");
    } else {
      Serial.println("[QRZ MAP] /mapa.bmp not found in LittleFS");
    }
    if (!mapLoaded) {
      Serial.println("[QRZ MAP] Using fallback drawing");
    }

    // JeĹ›li nie ma pliku BMP, uĹĽyj fallback (rysowanie)
    if (!mapLoaded) {
      // TĹ‚o mapy
      tft.fillRect(mapX, mapY, mapW, mapH, 0x4208); // Ciemny szary
      tft.drawRect(mapX, mapY, mapW, mapH, TFT_LIGHTGREY);
      
      // Uproszczony kontur mapy Ĺ›wiata (linie kontynentĂłw)
      tft.drawLine(mapX + 20, mapY + 25, mapX + 50, mapY + 20, TFT_LIGHTGREY);
      tft.drawLine(mapX + 50, mapY + 20, mapX + 60, mapY + 40, TFT_LIGHTGREY);
      tft.drawLine(mapX + 20, mapY + 25, mapX + 25, mapY + 55, TFT_LIGHTGREY);
      tft.drawLine(mapX + 25, mapY + 55, mapX + 40, mapY + 70, TFT_LIGHTGREY);
      tft.drawLine(mapX + 70, mapY + 15, mapX + 110, mapY + 20, TFT_LIGHTGREY);
      tft.drawLine(mapX + 110, mapY + 20, mapX + 120, mapY + 45, TFT_LIGHTGREY);
      tft.drawLine(mapX + 70, mapY + 35, mapX + 100, mapY + 40, TFT_LIGHTGREY);
      tft.drawLine(mapX + 100, mapY + 40, mapX + 95, mapY + 65, TFT_LIGHTGREY);
      tft.drawLine(mapX + 110, mapY + 55, mapX + 130, mapY + 60, TFT_LIGHTGREY);
      
      // Linie siatki
      for (int i = 1; i < 4; i++) {
        tft.drawFastHLine(mapX + 2, mapY + i * 20, mapW - 4, 0x2104);
      }
      for (int i = 1; i < 6; i++) {
        tft.drawFastVLine(mapX + i * 23, mapY + 2, mapH - 4, 0x2104);
      }
    }
    
    // Oblicz pozycjÄ™ na mapie (prosta projekcja equirectangular)
    // Longitude: -180 do 180 -> 0 do mapW
    // Latitude: -90 do 90 -> mapH do 0 (odwrĂłcone)
    int dotX = mapX + (int)((qrzPopupLon + 180.0) * mapW / 360.0);
    int dotY = mapY + mapH - (int)((qrzPopupLat + 90.0) * mapH / 180.0);
    
    Serial.print("[QRZ MAP] Raw dot position: dotX=");
    Serial.print(dotX);
    Serial.print(" dotY=");
    Serial.println(dotY);
    
    // Ogranicz do obszaru mapy
    if (dotX < mapX + 2) dotX = mapX + 2;
    if (dotX > mapX + mapW - 3) dotX = mapX + mapW - 3;
    if (dotY < mapY + 2) dotY = mapY + 2;
    if (dotY > mapY + mapH - 3) dotY = mapY + mapH - 3;
    
    Serial.print("[QRZ MAP] Clamped dot position: dotX=");
    Serial.print(dotX);
    Serial.print(" dotY=");
    Serial.println(dotY);
    
    // Rysuj punkt stacji (czerwona kropka z biaĹ‚Ä… obwĂłdkÄ…)
    Serial.println("[QRZ MAP] Drawing station dot");
    tft.fillCircle(dotX, dotY, 5, TFT_RED);
    tft.drawCircle(dotX, dotY, 5, TFT_WHITE);
    tft.drawCircle(dotX, dotY, 6, TFT_BLACK);
    
    // MaĹ‚a etykieta z lokalizatorem pod mapÄ…
    tft.setTextSize(1);
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(mapX, mapY + mapH + 3);
    tft.print(qrzPopupGrid);
  }
}

void handleQrzPopupTouch(uint16_t x, uint16_t y) {
  if (!qrzPopupActive) return;

  const int popupW = 400;
  const int popupH = 220;
  const int popupX = (480 - popupW) / 2;
  const int popupY = (320 - popupH) / 2;
  const int closeW = 80;
  const int closeH = 30;
  const int closeX = popupX + (popupW - closeW) / 2;
  const int closeY = popupY + popupH - closeH - 15;

  if (isPointInRect(x, y, closeX, closeY, closeW, closeH)) {
    qrzPopupActive = false;
    // Przerysuj ekran
    if (currentScreen == SCREEN_DX_CLUSTER || currentScreen == SCREEN_POTA_CLUSTER || currentScreen == SCREEN_HAMALERT_CLUSTER) {
      drawScreen(currentScreen);
    }
  }
}

void openQrzPopup(const String &callsign) {
  Serial.print("[QRZ POPUP] Opening popup for: ");
  Serial.println(callsign);
  if (callsign.length() == 0) return;
  
  qrzPopupCallsign = callsign;
  qrzPopupCallsign.toUpperCase();

  // SprawdĹş cache najpierw
  String grid, country, name, email, qth;
  double lat, lon;
  bool hasLatLon;
  if (getQrzCacheFresh(qrzPopupCallsign, grid, country, name, email, qth, lat, lon, hasLatLon, QRZ_CACHE_TTL_MS)) {
    Serial.print("[QRZ POPUP] Cache hit - hasLatLon=");
    Serial.print(hasLatLon ? "true" : "false");
    Serial.print(" lat=");
    Serial.print(lat, 6);
    Serial.print(" lon=");
    Serial.println(lon, 6);
    qrzPopupGrid = grid;
    qrzPopupCountry = country;
    qrzPopupName = name;
    qrzPopupEmail = email;
    qrzPopupQth = qth;
    qrzPopupLat = lat;
    qrzPopupLon = lon;
    qrzPopupHasLatLon = hasLatLon;
    // Oblicz odlegĹ‚oĹ›Ä‡ jeĹ›li mamy wspĂłĹ‚rzÄ™dne
    if (hasLatLon && userLatLonValid) {
      qrzPopupDistance = calculateDistance(userLat, userLon, lat, lon);
    } else if (hasLatLon && userLocator.length() >= 4) {
      double userLatLocal, userLonLocal;
      locatorToLatLon(userLocator, userLatLocal, userLonLocal);
      qrzPopupDistance = calculateDistance(userLatLocal, userLonLocal, lat, lon);
    } else {
      qrzPopupDistance = 0.0;
    }
    qrzPopupFetchedAt = millis();
    qrzPopupActive = true;
    drawQrzPopup();
    // Asynchronous fetch for fresh data - update popup after
    if (fetchCallookCallsignInfo(qrzPopupCallsign, grid, country, name, email, qth, lat, lon, hasLatLon)) {
      qrzPopupGrid = grid;
      qrzPopupCountry = country;
      qrzPopupName = name;
      qrzPopupEmail = email;
      qrzPopupQth = qth;
      // Oblicz odlegĹ‚oĹ›Ä‡ jeĹ›li mamy wspĂłĹ‚rzÄ™dne
      if (hasLatLon && userLatLonValid) {
        qrzPopupDistance = calculateDistance(userLat, userLon, lat, lon);
      } else if (hasLatLon && userLocator.length() >= 4) {
        double userLatLocal, userLonLocal;
        locatorToLatLon(userLocator, userLatLocal, userLonLocal);
        qrzPopupDistance = calculateDistance(userLatLocal, userLonLocal, lat, lon);
      } else {
        qrzPopupDistance = 0.0;
      }
      drawQrzPopup();
    }
    return;
  }

  // JeĹ›li nie ma w cache, pokaĹĽ popup z "Loading..." i pobierz w tle
  qrzPopupGrid = "";
  qrzPopupCountry = "Fetching...";
  qrzPopupName = "";
  qrzPopupQth = "";
  qrzPopupDistance = 0.0;
  qrzPopupFetchedAt = 0;
  qrzPopupActive = true;
  drawQrzPopup();

  // Pobierz dane z QRZ.com jako pierwsze (globalne API, wymaga konta)
  Serial.println("[QRZ POPUP] Trying QRZ.com...");
  if (fetchQrzCallsignInfo(qrzPopupCallsign, grid, country, name, email, qth, lat, lon, hasLatLon)) {
    Serial.print("[QRZ POPUP] QRZ.com fetch - hasLatLon=");
    Serial.print(hasLatLon ? "true" : "false");
    Serial.print(" lat=");
    Serial.print(lat, 6);
    Serial.print(" lon=");
    Serial.println(lon, 6);
    qrzPopupGrid = grid;
    qrzPopupCountry = country;
    qrzPopupName = name;
    qrzPopupEmail = email;
    qrzPopupQth = qth;
    qrzPopupLat = lat;
    qrzPopupLon = lon;
    qrzPopupHasLatLon = hasLatLon;
    // Oblicz odlegĹ‚oĹ›Ä‡ jeĹ›li mamy wspĂłĹ‚rzÄ™dne
    if (hasLatLon && userLatLonValid) {
      qrzPopupDistance = calculateDistance(userLat, userLon, lat, lon);
    } else if (hasLatLon && userLocator.length() >= 4) {
      double userLatLocal, userLonLocal;
      locatorToLatLon(userLocator, userLatLocal, userLonLocal);
      qrzPopupDistance = calculateDistance(userLatLocal, userLonLocal, lat, lon);
    } else {
      qrzPopupDistance = 0.0;
    }
    qrzPopupFetchedAt = millis();
    drawQrzPopup();
  } else {
    // QRZ.com nie zadziaĹ‚aĹ‚ - sprĂłbuj Callook.info (darmowe API, tylko USA)
    Serial.println("[QRZ POPUP] QRZ.com failed, trying Callook.info...");
    if (fetchCallookCallsignInfo(qrzPopupCallsign, grid, country, name, email, qth, lat, lon, hasLatLon)) {
    Serial.print("[QRZ POPUP] API fetch - hasLatLon=");
    Serial.print(hasLatLon ? "true" : "false");
    Serial.print(" lat=");
    Serial.print(lat, 6);
    Serial.print(" lon=");
    Serial.println(lon, 6);
    qrzPopupGrid = grid;
    qrzPopupCountry = country;
    qrzPopupName = name;
    qrzPopupEmail = email;
    qrzPopupQth = qth;
    qrzPopupLat = lat;
    qrzPopupLon = lon;
    qrzPopupHasLatLon = hasLatLon;
    // Oblicz odlegĹ‚oĹ›Ä‡ jeĹ›li mamy wspĂłĹ‚rzÄ™dne
    if (hasLatLon && userLatLonValid) {
      qrzPopupDistance = calculateDistance(userLat, userLon, lat, lon);
    } else if (hasLatLon && userLocator.length() >= 4) {
      double userLatLocal, userLonLocal;
      locatorToLatLon(userLocator, userLatLocal, userLonLocal);
      qrzPopupDistance = calculateDistance(userLatLocal, userLonLocal, lat, lon);
    } else {
      qrzPopupDistance = 0.0;
    }
    qrzPopupFetchedAt = millis();
    drawQrzPopup();
  } else {
    // BĹ‚Ä…d pobierania z Callook - sprĂłbuj HamQTH (globalne API)
    Serial.println("[QRZ POPUP] Callook failed, trying HamQTH...");
    
    if (fetchHamQthCallsignInfo(qrzPopupCallsign, grid, country, name, email, qth, lat, lon, hasLatLon)) {
      Serial.print("[QRZ POPUP] HamQTH fetch - hasLatLon=");
      Serial.print(hasLatLon ? "true" : "false");
      Serial.print(" lat=");
      Serial.print(lat, 6);
      Serial.print(" lon=");
      Serial.println(lon, 6);
      qrzPopupGrid = grid;
      qrzPopupCountry = country;
      qrzPopupName = name;
      qrzPopupEmail = email;
      qrzPopupQth = qth;
      qrzPopupLat = lat;
      qrzPopupLon = lon;
      qrzPopupHasLatLon = hasLatLon;
      // Oblicz odlegĹ‚oĹ›Ä‡ jeĹ›li mamy wspĂłĹ‚rzÄ™dne
      if (hasLatLon && userLatLonValid) {
        qrzPopupDistance = calculateDistance(userLat, userLon, lat, lon);
      } else if (hasLatLon && userLocator.length() >= 4) {
        double userLatLocal, userLonLocal;
        locatorToLatLon(userLocator, userLatLocal, userLonLocal);
        qrzPopupDistance = calculateDistance(userLatLocal, userLonLocal, lat, lon);
      } else {
        qrzPopupDistance = 0.0;
      }
      qrzPopupFetchedAt = millis();
      drawQrzPopup();
      return;
    }
    
    // HamQTH teĹĽ nie zadziaĹ‚aĹ‚ - sprĂłbuj znaleĹşÄ‡ lokalizator w spotach DX
    String foundLocator = "";
    Serial.print("[QRZ POPUP] Both APIs failed, searching spots for: ");
    Serial.println(qrzPopupCallsign);
    Serial.print("[QRZ POPUP] Total spots in array: ");
    Serial.println(spotCount);
    
    // Szukaj w tablicy spots (DX Cluster)
    for (int i = 0; i < spotCount; i++) {
      Serial.print("[QRZ POPUP] Checking spot ");
      Serial.print(i);
      Serial.print(" callsign=");
      Serial.print(spots[i].callsign);
      Serial.print(" vs ");
      Serial.println(qrzPopupCallsign);
      
      if (spots[i].callsign.equalsIgnoreCase(qrzPopupCallsign)) {
        Serial.print("[QRZ POPUP] FOUND MATCH spot ");
        Serial.print(i);
        Serial.print(" locator=");
        Serial.println(spots[i].locator);
        if (spots[i].locator.length() >= 4) {
          foundLocator = spots[i].locator;
          break;
        }
      }
    }
    // Szukaj w tablicy potaSpots (POTA)
    if (foundLocator.length() < 4) {
      for (int i = 0; i < potaSpotCount; i++) {
        if (potaSpots[i].callsign.equalsIgnoreCase(qrzPopupCallsign)) {
          if (potaSpots[i].locator.length() >= 4) {
            foundLocator = potaSpots[i].locator;
            break;
          }
        }
      }
    }
    
    if (foundLocator.length() >= 4) {
      // Znaleziono lokalizator - uĹĽyj go do pokazania na mapie
      double spotLat, spotLon;
      locatorToLatLon(foundLocator, spotLat, spotLon);
      qrzPopupGrid = foundLocator;
      qrzPopupLat = spotLat;
      qrzPopupLon = spotLon;
      qrzPopupHasLatLon = true;
      qrzPopupCountry = "Data not available";
      qrzPopupName = "(using grid from spot)";
      Serial.print("[QRZ POPUP] Using locator from spot: ");
      Serial.print(foundLocator);
      Serial.print(" lat=");
      Serial.print(spotLat, 6);
      Serial.print(" lon=");
      Serial.println(spotLon, 6);
      drawQrzPopup();
    } else {
      // Brak lokalizatora w spotach - sprawdĹş czy mamy w cache QRZ
      Serial.println("[QRZ POPUP] No spot locator found, checking QRZ cache...");
      
      String cacheGrid, cacheCountry, cacheName, cacheEmail, cacheQth;
      double cacheLat, cacheLon;
      bool cacheHasLatLon;
      
      bool hasCache = getQrzCacheFresh(qrzPopupCallsign, cacheGrid, cacheCountry, cacheName, cacheEmail, cacheQth, cacheLat, cacheLon, cacheHasLatLon, QRZ_CACHE_TTL_MS);
      Serial.print("[QRZ POPUP] Cache check result: ");
      Serial.println(hasCache ? "FOUND" : "NOT FOUND");
      
      if (hasCache) {
        Serial.print("[QRZ POPUP] Cache data - grid: ");
        Serial.print(cacheGrid);
        Serial.print(" hasLatLon: ");
        Serial.println(cacheHasLatLon ? "true" : "false");
        
        // Mamy dane w cache - uĹĽyj ich
        if (cacheGrid.length() >= 4) {
          double spotLat, spotLon;
          locatorToLatLon(cacheGrid, spotLat, spotLon);
          qrzPopupGrid = cacheGrid;
          qrzPopupLat = spotLat;
          qrzPopupLon = spotLon;
          qrzPopupHasLatLon = true;
          qrzPopupCountry = cacheCountry.length() > 0 ? cacheCountry : "Data from cache";
          qrzPopupName = cacheName.length() > 0 ? cacheName : "(from QRZ cache)";
          Serial.print("[QRZ POPUP] Using locator from QRZ cache: ");
          Serial.println(cacheGrid);
          drawQrzPopup();
        } else if (cacheHasLatLon) {
          // Mamy lat/lon ale nie mamy grid
          qrzPopupGrid = cacheGrid;
          qrzPopupLat = cacheLat;
          qrzPopupLon = cacheLon;
          qrzPopupHasLatLon = true;
          qrzPopupCountry = cacheCountry.length() > 0 ? cacheCountry : "Data from cache";
          qrzPopupName = cacheName.length() > 0 ? cacheName : "(from QRZ cache)";
          Serial.print("[QRZ POPUP] Using lat/lon from QRZ cache");
          drawQrzPopup();
        } else {
          // Brak lokalizatora - uÄąÄ˝yj przybliÄąÄ˝onej lokalizacji z prefiksu
          String approxGrid = getApproximateGridFromCallsign(qrzPopupCallsign);
          if (approxGrid.length() >= 4) {
            double approxLat, approxLon;
            locatorToLatLon(approxGrid, approxLat, approxLon);
            qrzPopupGrid = approxGrid;
            qrzPopupLat = approxLat;
            qrzPopupLon = approxLon;
            qrzPopupHasLatLon = true;
            qrzPopupCountry = "Approximate location";
            qrzPopupName = "(from callsign prefix)";
            Serial.print("[QRZ POPUP] Using approximate grid: ");
            Serial.print(approxGrid);
            Serial.print(" lat=");
            Serial.print(approxLat, 6);
            Serial.print(" lon=");
            Serial.println(approxLon, 6);
            drawQrzPopup();
          } else {
            qrzPopupCountry = "Data not available";
            qrzPopupName = "(No locator in spot or cache)";
            drawQrzPopup();
          }
        }
      } else {
        // Brak lokalizatora - uÄąÄ˝yj przybliÄąÄ˝onej lokalizacji z prefiksu
        String approxGrid = getApproximateGridFromCallsign(qrzPopupCallsign);
        if (approxGrid.length() >= 4) {
          double approxLat, approxLon;
          locatorToLatLon(approxGrid, approxLat, approxLon);
          qrzPopupGrid = approxGrid;
          qrzPopupLat = approxLat;
          qrzPopupLon = approxLon;
          qrzPopupHasLatLon = true;
          qrzPopupCountry = "Approximate location";
          qrzPopupName = "(from callsign prefix)";
          Serial.print("[QRZ POPUP] Using approximate grid: ");
          Serial.print(approxGrid);
          Serial.print(" lat=");
          Serial.print(approxLat, 6);
          Serial.print(" lon=");
          Serial.println(approxLon, 6);
          drawQrzPopup();
        } else {
          qrzPopupCountry = "Data not available";
          qrzPopupName = "(No locator in spot or cache)";
          drawQrzPopup();
        }
      }
    }
  }
}
}

// ========== MENU WYGASZACZA EKRANU ==========

void drawScreenSaverMenu() {
  tft.fillScreen(TFT_BLACK);
  uiMenuHeader("WYGASZACZ EKRANU");

  const int tileW = 100;
  const int tileH = 40;
  const int gap = 10;
  const int startX = (480 - (2 * tileW + gap)) / 2;
  const int tileY = 60;

  // WĹ‚Ä…cz/WyĹ‚Ä…cz
  drawFilterTile(startX, tileY, tileW, tileH, "WLACZ", screenSaverEnabled);
  drawFilterTile(startX + tileW + gap, tileY, tileW, tileH, "WYLACZ", !screenSaverEnabled);

  // Czas w minutach
  tft.setTextColor(TFT_LIGHTGREY);
  tft.setTextSize(1);
  tft.setCursor(10, 120);
  tft.print("CZAS (min): ");
  tft.setTextColor(TFT_WHITE);
  tft.print(screenSaverTimeoutMin);

  // Typ wygaszacza - przycisk zmiany
  tft.setTextColor(TFT_LIGHTGREY);
  tft.setCursor(200, 120);
  tft.print("TYP: ");
  tft.setTextColor(TFT_YELLOW);
  tft.print(SAVER_TYPE_NAMES[screenSaverType]);
  
  // Przycisk zmiany typu
  const int typeTileW = 100;
  const int typeTileH = 30;
  const int typeX = 300;
  const int typeY = 115;
  drawFilterTile(typeX, typeY, typeTileW, typeTileH, "ZMIEN", false);

  const int minTileW = 60;
  const int minTileH = 35;
  const int minGap = 8;
  const int minStartX = 10;
  const int minY = 140;

  // Przyciski +/- minuty
  drawFilterTile(minStartX, minY, minTileW, minTileH, "-5", false);
  drawFilterTile(minStartX + minTileW + minGap, minY, minTileW, minTileH, "-1", false);
  drawFilterTile(minStartX + 2 * (minTileW + minGap), minY, minTileW, minTileH, "+1", false);
  drawFilterTile(minStartX + 3 * (minTileW + minGap), minY, minTileW, minTileH, "+5", false);

  // CLOSE
  const int closeW = 120;
  const int closeH = 40;
  const int closeX = (480 - closeW) / 2;
  const int closeY = 280;
  drawFilterTile(closeX, closeY, closeW, closeH, "ZAMKNIJ", false);
}

void handleScreenSaverMenuTouch(uint16_t x, uint16_t y) {
  const int tileW = 100;
  const int tileH = 40;
  const int gap = 10;
  const int startX = (480 - (2 * tileW + gap)) / 2;
  const int tileY = 60;

  // WĹ‚Ä…cz/WyĹ‚Ä…cz
  if (isPointInRect(x, y, startX, tileY, tileW, tileH)) {
    setScreenSaverEnabled(true);
    drawScreenSaverMenu();
    return;
  }
  if (isPointInRect(x, y, startX + tileW + gap, tileY, tileW, tileH)) {
    setScreenSaverEnabled(false);
    drawScreenSaverMenu();
    return;
  }

  // Przycisk zmiany typu wygaszacza
  const int typeTileW = 100;
  const int typeTileH = 30;
  const int typeX = 300;
  const int typeY = 115;
  if (isPointInRect(x, y, typeX, typeY, typeTileW, typeTileH)) {
    // ZmieĹ„ typ na nastÄ™pny
    int currentType = (int)screenSaverType;
    currentType = (currentType + 1) % 3;  // Cykl: 0->1->2->0
    screenSaverType = (ScreenSaverType)currentType;
    // Zapisz w preferencjach
    preferences->putInt("saver_type", currentType);
    drawScreenSaverMenu();
    return;
  }

  const int minTileW = 60;
  const int minTileH = 35;
  const int minGap = 8;
  const int minStartX = 10;
  const int minY = 140;

  // Minus 5 min
  if (isPointInRect(x, y, minStartX, minY, minTileW, minTileH)) {
    setScreenSaverTimeout(screenSaverTimeoutMin - 5);
    drawScreenSaverMenu();
    return;
  }
  // Minus 1 min
  if (isPointInRect(x, y, minStartX + minTileW + minGap, minY, minTileW, minTileH)) {
    setScreenSaverTimeout(screenSaverTimeoutMin - 1);
    drawScreenSaverMenu();
    return;
  }
  // Plus 1 min
  if (isPointInRect(x, y, minStartX + 2 * (minTileW + minGap), minY, minTileW, minTileH)) {
    setScreenSaverTimeout(screenSaverTimeoutMin + 1);
    drawScreenSaverMenu();
    return;
  }
  // Plus 5 min
  if (isPointInRect(x, y, minStartX + 3 * (minTileW + minGap), minY, minTileW, minTileH)) {
    setScreenSaverTimeout(screenSaverTimeoutMin + 5);
    drawScreenSaverMenu();
    return;
  }

  // Zamknij
  const int closeW = 120;
  const int closeH = 40;
  const int closeX = (480 - closeW) / 2;
  const int closeY = 280;
  if (isPointInRect(x, y, closeX, closeY, closeW, closeH)) {
    inMenu = false;
    screenSaverMenuActive = false;
    drawScreen(currentScreen);
    return;
  }
}

// ========== UĹšPIENIE EKRANU ==========

void drawScreenSleepMenu() {
  tft.fillScreen(TFT_BLACK);

  // Naglowek
  uiMenuHeader("USPIENIE EKRANU");

  // Stan: Wlaczony/Wylaczony
  const int tileW = 100;
  const int tileH = 40;
  const int gap = 10;
  const int startX = (480 - (2 * tileW + gap)) / 2;
  const int tileY = 60;

  tft.setTextColor(TFT_LIGHTGREY);
  tft.setTextSize(1);
  tft.setCursor(10, 50);
  tft.print("Stan:");

  drawFilterTile(startX, tileY, tileW, tileH, "WLACZ", screenSleepEnabled);
  drawFilterTile(startX + tileW + gap, tileY, tileW, tileH, "WYLACZ", !screenSleepEnabled);

  // Ustawienie czasu w minutach
  tft.setTextColor(TFT_LIGHTGREY);
  tft.setTextSize(1);
  tft.setCursor(10, 120);
  tft.print("Czas uspienia:");

  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(3);
  tft.setCursor(220, 115);
  tft.print(screenSleepMenuTimeoutMin);
  tft.print(" min");

  // Przyciski +/- 1 minuta
  const int minTileW = 60;
  const int minTileH = 35;
  const int minGap = 8;
  const int minStartX = 10;
  const int minY = 160;

  drawFilterTile(minStartX, minY, minTileW, minTileH, "-1", false);
  drawFilterTile(minStartX + minTileW + minGap, minY, minTileW, minTileH, "+1", false);

  // Przycisk ZAPISZ
  const int saveW = 90;
  const int saveH = 26;
  const int saveX = 10;
  const int saveY = 240;
  drawFilterTile(saveX, saveY, saveW, saveH, "ZAPISZ", false);

  // Przycisk ANULUJ
  const int cancelX = saveX + saveW + 10;
  drawFilterTile(cancelX, saveY, saveW, saveH, "ANULUJ", false);
}

void handleScreenSleepMenuTouch(uint16_t x, uint16_t y) {
  const int tileW = 100;
  const int tileH = 40;
  const int gap = 10;
  const int startX = (480 - (2 * tileW + gap)) / 2;
  const int tileY = 60;

  // WĹ‚Ä…cz
  if (isPointInRect(x, y, startX, tileY, tileW, tileH)) {
    screenSleepEnabled = true;
    drawScreenSleepMenu();
    return;
  }
  // WyĹ‚Ä…cz
  if (isPointInRect(x, y, startX + tileW + gap, tileY, tileW, tileH)) {
    screenSleepEnabled = false;
    drawScreenSleepMenu();
    return;
  }

  // +/- minuty
  const int minTileW = 60;
  const int minTileH = 35;
  const int minGap = 8;
  const int minStartX = 10;
  const int minY = 160;

  // Minus 1 min
  if (isPointInRect(x, y, minStartX, minY, minTileW, minTileH)) {
    if (screenSleepMenuTimeoutMin > 1) {
      screenSleepMenuTimeoutMin--;
    }
    drawScreenSleepMenu();
    return;
  }
  // Plus 1 min
  if (isPointInRect(x, y, minStartX + minTileW + minGap, minY, minTileW, minTileH)) {
    screenSleepMenuTimeoutMin++;
    drawScreenSleepMenu();
    return;
  }

  // Przycisk ZAPISZ
  const int saveW = 90;
  const int saveH = 26;
  const int saveX = 10;
  const int saveY = 240;
  if (isPointInRect(x, y, saveX, saveY, saveW, saveH)) {
    screenSleepTimeoutMin = screenSleepMenuTimeoutMin;
    screenSleepEnabled = true;
    screenSleepLastActivityMs = millis();
    preferences->putInt("sleep_timeout", screenSleepTimeoutMin);
    preferences->putBool("sleep_en", screenSleepEnabled);
    screenSleepMenuActive = false;
    inMenu = false;
    drawScreen(currentScreen);
    return;
  }

  // Przycisk ANULUJ
  const int cancelX = saveX + saveW + 10;
  if (isPointInRect(x, y, cancelX, saveY, saveW, saveH)) {
    screenSleepMenuActive = false;
    inMenu = false;
    drawScreen(currentScreen);
    return;
  }
}

void enterScreenSleep() {
  if (!screenSleepEnabled || screenSleepActive) return;
  // Bez dzialajacego dotyku nie ma jak obudzic ekranu - nie usypiamy go,
  // zeby nie zostawic uzytkownika z czarnym ekranem i wlaczonym podswietleniem.
  if (!touchReady) return;

  screenSleepActive = true;
  screenSleepPrevScreen = currentScreen;

  // WyĹ‚Ä…cz podĹ›wietlenie
  writeBacklightDuty(0);

  // WyczyĹ›Ä‡ ekran
  tft.fillScreen(TFT_BLACK);
}

void wakeUpFromSleep() {
  if (!screenSleepActive) return;

  screenSleepActive = false;
  screenSleepLastActivityMs = millis();

  // PrzywrĂłÄ‡ podĹ›wietlenie (z uwzglednieniem nocnego obnizenia)
  applyEffectiveBacklight();

  // PrzywrĂłÄ‡ poprzedni ekran
  drawScreen(screenSleepPrevScreen);
}

void checkScreenSleepTimeout() {
  if (!screenSleepEnabled || screenSleepActive) return;

  unsigned long timeoutMs = (unsigned long)screenSleepTimeoutMin * 60 * 1000;
  if (millis() - screenSleepLastActivityMs > timeoutMs) {
    enterScreenSleep();
  }
}

// ========== PREFERENCES ==========

void loadPreferences() {
  if (preferences == nullptr) {
    preferences = new Preferences();
  }
  
  preferences->begin("dxcluster", false);
  yield();

  // KolejnoĹ›Ä‡ ekranĂłw
  loadDefaultScreenOrder();
  size_t bytesRead = preferences->getBytes("screen_order", screenOrder, sizeof(screenOrder));
  if (bytesRead != sizeof(screenOrder)) {
    loadDefaultScreenOrder();
  }
  ensureScreenOrderValid();
  currentScreen = firstActiveScreen();
  tftAutoSwitchEnabled = preferences->getBool("tft_autosw", false);
  applyTftAutoSwitchTimeSec(preferences->getInt("tft_sw_sec", DEFAULT_TFT_SWITCH_TIME_SEC));
  tftAutoSwitchLastMs = millis();
  tftAutoSwitchLastScreen = currentScreen;
  
  wifiSSID = preferences->getString("wifi_ssid", "");
  yield();
  wifiPassword = preferences->getString("wifi_pass", "");
  yield();
  wifiSSID2 = preferences->getString("wifi_ssid2", "");
  yield();
  wifiPassword2 = preferences->getString("wifi_pass2", "");
  yield();
  
  clusterHost = preferences->getString("cluster_host", DEFAULT_CLUSTER_HOST);
  yield();
  clusterPort = preferences->getInt("cluster_port", DEFAULT_CLUSTER_PORT);
  yield();
  potaClusterHost = preferences->getString("pota_host", DEFAULT_POTA_CLUSTER_HOST);
  yield();
  potaClusterPort = preferences->getInt("pota_port", DEFAULT_POTA_CLUSTER_PORT);
  yield();
  potaFilterCommand = preferences->getString("pota_filter", DEFAULT_POTA_FILTER_COMMAND);
  yield();
  potaApiUrl = preferences->getString("pota_api_url", DEFAULT_POTA_API_URL);
  yield();
  hamalertHost = preferences->getString("hama_host", DEFAULT_HAMALERT_HOST);
  yield();
  hamalertPort = preferences->getInt("hama_port", DEFAULT_HAMALERT_PORT);
  if (hamalertPort <= 0 || hamalertPort > 65535) hamalertPort = DEFAULT_HAMALERT_PORT;
  yield();
  hamalertLogin = preferences->getString("hama_user", "");
  yield();
  hamalertPassword = preferences->getString("hama_pass", "");
  yield();
  userCallsign = preferences->getString("user_callsign", "");
  yield();
  userLocator = preferences->getString("user_locator", "");
  yield();
  gpsEnabled = preferences->getBool("gps_en", false);
  yield();
  gpsUseForPosition = preferences->getBool("gps_use", false);
  yield();
  gpsBaud = preferences->getInt("gps_baud", 9600);
  if (gpsBaud < 300 || gpsBaud > 2000000) gpsBaud = 9600;
  yield();
  gpsRxPin = preferences->getInt("gps_rx", 18);
  yield();
  gpsTxPin = preferences->getInt("gps_tx", 17);
  yield();
  timezoneHours = preferences->getInt("timezone", DEFAULT_TIMEZONE_HOURS);
  if (timezoneHours < -12) timezoneHours = -12;
  if (timezoneHours > 14) timezoneHours = 14;
  yield();
  userLat = preferences->getFloat("user_lat", 0.0f);
  yield();
  userLon = preferences->getFloat("user_lon", 0.0f);
  yield();
  userLatLonValid = preferences->getBool("user_ll_ok", false);
  yield();
  
  // JeĹ›li nie mamy waĹĽnych wspĂłĹ‚rzÄ™dnych ale mamy locator, przelicz z locatora
  // (0,0) traktujemy jak brak pozycji, nawet jesli flaga jest ustawiona
  // (starsze zapisy z pustego formularza WWW).
  if ((!userLatLonValid || (userLat == 0.0 && userLon == 0.0)) &&
      userLocator.length() >= 4) {
    updateUserLatLonFromLocator();
    // Nie zapisujemy do preferencji tutaj - to tylko odczyt
  } else if (userLatLonValid && userLat == 0.0 && userLon == 0.0) {
    userLatLonValid = false;
  }
  // Zapamietaj reczna pozycje - sluzy do przywrocenia, gdy GPS jest wylaczony.
  gpsSnapshotManualPosition();
  qrzUsername = preferences->getString("qrz_user", "");
  yield();
  qrzPassword = preferences->getString("qrz_pass", "");
  yield();
  weatherApiKey = preferences->getString("weather_key", "");
  yield();
  n2yoApiKey = preferences->getString("n2yo_key", "");
  yield();
  openWebRxUrl = preferences->getString("openwebrx_url", DEFAULT_OPENWEBRX_URL);
  yield();
  backlightPercent = preferences->getInt("tft_backlight", TFT_BACKLIGHT);
  if (backlightPercent < MIN_BACKLIGHT_PERCENT) backlightPercent = MIN_BACKLIGHT_PERCENT;
  if (backlightPercent > 100) backlightPercent = 100;
  tftNightDimmingPercent = preferences->getInt("tft_night_dim", 0);
  if (tftNightDimmingPercent < 0) tftNightDimmingPercent = 0;
  if (tftNightDimmingPercent > 100) tftNightDimmingPercent = 100;
  tftInvertColors = sanitizeTftInvertSetting(preferences->getBool("tft_inv", tftInvertColors));
  tftLanguage = preferences->getUChar("tft_lang", TFT_LANG_PL);
  if (tftLanguage > TFT_LANG_EN) tftLanguage = TFT_LANG_PL;
  dxTableSizeMode = preferences->getUChar("tft_tbl_size", DX_TABLE_SIZE_NORMAL);
  if (dxTableSizeMode > DX_TABLE_SIZE_ENLARGED) dxTableSizeMode = DX_TABLE_SIZE_NORMAL;
  int savedMenuHue = preferences->getInt("menu_hue", DEFAULT_MENU_THEME_HUE);
  if (savedMenuHue < 0) savedMenuHue = 0;
  if (savedMenuHue > 255) savedMenuHue = 255;
  menuThemeHue = (uint8_t)savedMenuHue;
  // Styl globalny musi byc znany PRZED applyMenuThemeFromHue() - w trybie
  // nowoczesnym kolor menu to teal, w klasycznym kolor z kolowego (menu_hue).
  screen1ClockStyle = (uint8_t)preferences->getInt("screen1_style", 0);
  if (screen1ClockStyle > 1) screen1ClockStyle = 0;
  applyMenuThemeFromHue();
  callsignColor = preferences->getUInt("callsign_color", 65535); // Default white (0xFFFF)
  // NVS nieustawione/0 daje czarny znak (niewidoczny na czarnym tle). Wymus biel.
  if (callsignColor == 0) callsignColor = 65535;
  clockDigitsColor = preferences->getUInt("clock_color", 0xFFE0); // Default yellow
  if (clockDigitsColor == 0) clockDigitsColor = 0xFFE0;  // czarny = niewidoczny
  touchXMin = preferences->getInt("touch_xmin", TOUCH_X_MIN);
  touchXMax = preferences->getInt("touch_xmax", TOUCH_X_MAX);
  touchYMin = preferences->getInt("touch_ymin", TOUCH_Y_MIN);
  touchYMax = preferences->getInt("touch_ymax", TOUCH_Y_MAX);
  touchSwapXY = preferences->getBool("touch_swap", TOUCH_SWAP_XY);
  touchInvertX = preferences->getBool("touch_invx", TOUCH_INVERT_X);
  touchInvertY = preferences->getBool("touch_invy", TOUCH_INVERT_Y);
  tftRotation = preferences->getUChar("tft_rot", 1);
  if (tftRotation > 3) tftRotation = 1;
  touchRotation = preferences->getUChar("touch_rot", 1);
  if (touchRotation > 3) touchRotation = 1;
  if (touchXMin < 0 || touchXMax > 4095 || touchXMin >= touchXMax) {
    touchXMin = TOUCH_X_MIN;
    touchXMax = TOUCH_X_MAX;
  }
  if (touchYMin < 0 || touchYMax > 4095 || touchYMin >= touchYMax) {
    touchYMin = TOUCH_Y_MIN;
    touchYMax = TOUCH_Y_MAX;
  }

  // Konfiguracja PSKReporter
  pskReceiverCallsign = preferences->getString("psk_receiver", "");
  pskMaxSpots = preferences->getInt("psk_maxspots", 50);
  if (pskMaxSpots < 10) pskMaxSpots = 50;
  if (pskMaxSpots > 100) pskMaxSpots = 50;
  pskHoursWindow = preferences->getInt("psk_hours", 1);
  if (pskHoursWindow < 1) pskHoursWindow = 1;
  if (pskHoursWindow > 24) pskHoursWindow = 1;
  pskFilterBand = preferences->getString("psk_band", "");
  pskFilterMode = preferences->getString("psk_mode", "");
  pskCustomUrl = preferences->getString("psk_url", "");
  // HTTP Monitoring
  pskMonitorCallsign = preferences->getString("psk_monitor", "");
  pskReportDays = preferences->getInt("psk_report_days", 3);
  if (pskReportDays < 0) pskReportDays = 0;
  if (pskReportDays > 30) pskReportDays = 30;
  pskAutoRefreshMinutes = preferences->getInt("psk_autorefresh", 5);
  if (pskAutoRefreshMinutes < 0) pskAutoRefreshMinutes = 0;
  if (pskAutoRefreshMinutes > 60) pskAutoRefreshMinutes = 60;

  // MQTT PSK Reporter settings
  pskMqttEnabled = preferences->getBool("psk_mqtt_en", false);
  pskMqttServer = preferences->getString("psk_mqtt_srv", "mqtt.pskreporter.info");
  pskMqttPort = preferences->getInt("psk_mqtt_port", 1883);
  if (pskMqttPort <= 0 || pskMqttPort > 65535) pskMqttPort = 1883;
  pskMqttCallsign = preferences->getString("psk_mqtt_call", "");

  screen1TimeMode = (uint8_t)preferences->getInt("screen1_time", SCREEN1_TIME_UTC);
  if (screen1TimeMode > SCREEN1_TIME_LOCAL) screen1TimeMode = SCREEN1_TIME_UTC;
  
  // Konfiguracja wygaszacza ekranu (Matrix)
  screenSaverEnabled = preferences->getBool("ss_enabled", false);
  screenSaverTimeoutMin = preferences->getInt("ss_timeout", DEFAULT_SCREEN_SAVER_TIMEOUT_MIN);
  if (screenSaverTimeoutMin < 1) screenSaverTimeoutMin = 1;
  if (screenSaverTimeoutMin > 60) screenSaverTimeoutMin = 60;
  screenSaverLastActivityMs = millis();
  screenSaverActive = false;
  
  // Wczytaj typ wygaszacza
  int savedType = preferences->getInt("saver_type", 0);
  if (savedType < 0 || savedType > 2) savedType = 0;
  screenSaverType = (ScreenSaverType)savedType;
  
  // Konfiguracja filtrĂłw CC-Cluster
  clusterNoAnnouncements = preferences->getBool("cluster_noann", true);
  yield();
  clusterNoWWV = preferences->getBool("cluster_nowwv", true);
  yield();
  clusterNoWCY = preferences->getBool("cluster_nowcy", true);
  yield();
  clusterUseFilters = preferences->getBool("cluster_usefilt", false);
  yield();
  clusterFilterCommands = preferences->getString("cluster_filters", "");
  yield();
  
  // Konfiguracja APRS-IS
  aprsIsHost = preferences->getString("aprs_host", DEFAULT_APRS_IS_HOST);
  yield();
  aprsIsPort = preferences->getInt("aprs_port", DEFAULT_APRS_IS_PORT);
  yield();
  aprsCallsign = preferences->getString("aprs_callsign", DEFAULT_APRS_CALLSIGN);
  yield();
  aprsPasscode = preferences->getInt("aprs_passcode", DEFAULT_APRS_PASSCODE);
  applyAprsSsid(preferences->getInt("aprs_ssid", DEFAULT_APRS_SSID));
  yield();
  aprsFilterRadius = preferences->getInt("aprs_radius", DEFAULT_APRS_FILTER_RADIUS);
  yield();
  aprsBeaconEnabled = preferences->getBool("aprs_beacon", true);
  String aprsSymPref = preferences->getString("aprs_symbol", aprsSymbolTwoChar);
  applyAprsSymbol(aprsSymPref);
  aprsUserComment = preferences->getString("aprs_comment", "");
  aprsAlertCsv = sanitizeAprsAlertList(preferences->getString("aprs_alert", ""));
  aprsAlertEnabled = preferences->getBool("aprs_alert_en", true);
  applyAprsAlertMinSeconds(preferences->getInt(NVS_KEY_APRS_ALERT_MIN_SEC, DEFAULT_APRS_ALERT_MIN_SEC));
  applyAprsAlertScreenSeconds(preferences->getInt(NVS_KEY_APRS_ALERT_SCREEN_SEC, DEFAULT_APRS_ALERT_SCREEN_SEC));
  aprsAlertNearbyEnabled = preferences->getBool("aprs_alert_near", true);
  aprsAlertWxEnabled = preferences->getBool(NVS_KEY_APRS_ALERT_WX_ENABLED, false);
  applyAprsAlertDistanceKm(preferences->getFloat(NVS_KEY_APRS_ALERT_DISTANCE_KM, DEFAULT_APRS_ALERT_DISTANCE_KM));
  enableLedAlert = preferences->getBool(NVS_KEY_ENABLE_LED_ALERT, DEFAULT_ENABLE_LED_ALERT);
  applyLedAlertDurationMs(preferences->getInt(NVS_KEY_LED_ALERT_DURATION_MS, DEFAULT_LED_ALERT_DURATION_MS));
  applyLedAlertBlinkMs(preferences->getInt(NVS_KEY_LED_ALERT_BLINK_MS, DEFAULT_LED_ALERT_BLINK_MS));
  resetAprsAlertCooldownState();
  int storedIntervalMin = preferences->getInt(NVS_KEY_APRS_INTERVAL_MIN, DEFAULT_APRS_INTERVAL_MIN);
  applyAprsIntervalMinutes(storedIntervalMin);
  yield();
  // Ograniczenie promienia do 1-50 km
  if (aprsFilterRadius < 1) aprsFilterRadius = 1;
  if (aprsFilterRadius > 50) aprsFilterRadius = 50;
  // Uwaga: APRS uĂ„Ä…Ă„Ëťywa wspĂ„â€šĹ‚Ă„Ä…Ă˘â‚¬ĹˇrzÄ‚â€žĂ˘â€žËdnych z sekcji "Moja Stacja" (userLat, userLon) - nie ma osobnych pĂ„â€šĹ‚l

  if (!userLatLonValid && userLocator.length() >= 4) {
    updateUserLatLonFromLocator();
    preferences->putFloat("user_lat", (float)userLat);
    preferences->putFloat("user_lon", (float)userLon);
    preferences->putBool("user_ll_ok", userLatLonValid);
  }
  
  preferences->end();

  // Zastosuj inwersjÄ™ kolorĂłw po wczytaniu ustawieĹ„
  if (tftInitialized) {
    applyTftInversion();
  }
  
  Serial.println("Konfiguracja wczytana");
}

void savePreferences() {
  if (preferences == nullptr) {
    preferences = new Preferences();
  }
  
  preferences->begin("dxcluster", false);
  preferences->putBytes("screen_order", screenOrder, sizeof(screenOrder));
  
  preferences->putString("wifi_ssid", wifiSSID);
  preferences->putString("wifi_pass", wifiPassword);
  preferences->putString("wifi_ssid2", wifiSSID2);
  preferences->putString("wifi_pass2", wifiPassword2);
  preferences->putString("cluster_host", clusterHost);
  preferences->putInt("cluster_port", clusterPort);
  preferences->putString("pota_host", potaClusterHost);
  preferences->putInt("pota_port", potaClusterPort);
  preferences->putString("pota_filter", potaFilterCommand);
  preferences->putString("pota_api_url", potaApiUrl);
  preferences->putString("hama_host", hamalertHost);
  preferences->putInt("hama_port", hamalertPort);
  preferences->putString("hama_user", hamalertLogin);
  preferences->putString("hama_pass", hamalertPassword);
  preferences->putString("user_callsign", userCallsign);
  // Zapisujemy reczna pozycje (gdy GPS ja nadpisuje, userLocator to lokator GPS).
  preferences->putString("user_locator", gpsPositionOverrideActive ? userLocatorManual : userLocator);
  preferences->putInt("timezone", timezoneHours);
  preferences->putBool("gps_en", gpsEnabled);
  preferences->putBool("gps_use", gpsUseForPosition);
  preferences->putInt("gps_baud", gpsBaud);
  preferences->putInt("gps_rx", gpsRxPin);
  preferences->putInt("gps_tx", gpsTxPin);
  preferences->putFloat("user_lat", (float)(gpsPositionOverrideActive ? userLatManual : userLat));
  preferences->putFloat("user_lon", (float)(gpsPositionOverrideActive ? userLonManual : userLon));
  preferences->putBool("user_ll_ok", gpsPositionOverrideActive ? userLatLonValidManual : userLatLonValid);
  preferences->putString("qrz_user", qrzUsername);
  preferences->putString("qrz_pass", qrzPassword);
  preferences->putString("weather_key", weatherApiKey);
  preferences->putString("n2yo_key", n2yoApiKey);
  preferences->putString("openwebrx_url", openWebRxUrl);
  preferences->putInt("tft_backlight", backlightPercent);
  preferences->putInt("tft_night_dim", tftNightDimmingPercent);
  preferences->putBool("tft_inv", sanitizeTftInvertSetting(tftInvertColors));
  preferences->putUChar("tft_lang", tftLanguage);
  preferences->putUChar("tft_tbl_size", dxTableSizeMode);
  preferences->putInt("menu_hue", menuThemeHue);
  preferences->putInt("screen1_time", screen1TimeMode);
  preferences->putInt("screen1_style", screen1ClockStyle);
  preferences->putBool("tft_autosw", tftAutoSwitchEnabled);
  preferences->putUInt("callsign_color", callsignColor);
  preferences->putUInt("clock_color", clockDigitsColor);
  preferences->putInt("tft_sw_sec", tftAutoSwitchTimeSec);
  preferences->putInt("touch_xmin", touchXMin);
  preferences->putInt("touch_xmax", touchXMax);
  preferences->putInt("touch_ymin", touchYMin);
  preferences->putInt("touch_ymax", touchYMax);
  preferences->putBool("touch_swap", touchSwapXY);
  preferences->putBool("touch_invx", touchInvertX);
  preferences->putBool("touch_invy", touchInvertY);
  preferences->putUChar("touch_rot", touchRotation);
  preferences->putUChar("tft_rot", tftRotation);

  // Konfiguracja PSKReporter
  preferences->putString("psk_receiver", pskReceiverCallsign);
  preferences->putInt("psk_maxspots", pskMaxSpots);
  preferences->putInt("psk_hours", pskHoursWindow);
  preferences->putString("psk_band", pskFilterBand);
  preferences->putString("psk_mode", pskFilterMode);
  preferences->putString("psk_url", pskCustomUrl);
  // HTTP Monitoring
  preferences->putString("psk_monitor", pskMonitorCallsign);
  preferences->putInt("psk_report_days", pskReportDays);
  preferences->putInt("psk_autorefresh", pskAutoRefreshMinutes);

  // MQTT PSK Reporter settings
  preferences->putBool("psk_mqtt_en", pskMqttEnabled);
  preferences->putString("psk_mqtt_srv", pskMqttServer);
  preferences->putInt("psk_mqtt_port", pskMqttPort);
  preferences->putString("psk_mqtt_call", pskMqttCallsign);

  // Konfiguracja wygaszacza ekranu (Matrix)
  preferences->putBool("ss_enabled", screenSaverEnabled);
  preferences->putInt("ss_timeout", screenSaverTimeoutMin);
  
  // Konfiguracja APRS-IS
  preferences->putString("aprs_host", aprsIsHost);
  preferences->putInt("aprs_port", aprsIsPort);
  preferences->putString("aprs_callsign", aprsCallsign);
  preferences->putInt("aprs_passcode", aprsPasscode);
  preferences->putInt("aprs_ssid", aprsSsid);
  preferences->putBool("aprs_beacon", aprsBeaconEnabled);
  preferences->putString("aprs_symbol", aprsSymbolTwoChar);
  preferences->putString("aprs_comment", aprsUserComment);
  preferences->putString("aprs_alert", aprsAlertCsv);
  preferences->putBool("aprs_alert_en", aprsAlertEnabled);
  preferences->putInt(NVS_KEY_APRS_ALERT_MIN_SEC, aprsAlertMinSeconds);
  preferences->putInt(NVS_KEY_APRS_ALERT_SCREEN_SEC, aprsAlertScreenSeconds);
  preferences->putBool("aprs_alert_near", aprsAlertNearbyEnabled);
  preferences->putBool(NVS_KEY_APRS_ALERT_WX_ENABLED, aprsAlertWxEnabled);
  preferences->putFloat(NVS_KEY_APRS_ALERT_DISTANCE_KM, aprsAlertDistanceKm);
  preferences->putBool(NVS_KEY_ENABLE_LED_ALERT, enableLedAlert);
  preferences->putInt(NVS_KEY_LED_ALERT_DURATION_MS, ledAlertDurationMs);
  preferences->putInt(NVS_KEY_LED_ALERT_BLINK_MS, ledAlertBlinkMs);
  preferences->putInt(NVS_KEY_APRS_INTERVAL_MIN, aprsIntervalMinutes);
  // Ograniczenie promienia do 1-50 km przed zapisem
  int radiusToSave = aprsFilterRadius;
  if (radiusToSave < 1) radiusToSave = 1;
  if (radiusToSave > 50) radiusToSave = 50;
  preferences->putInt("aprs_radius", radiusToSave);
  // Uwaga: APRS uĹĽywa wspĂłĹ‚rzÄ™dnych z sekcji "Moja Stacja" (userLat, userLon) - nie zapisujemy osobnych pĂłl
  
  // Konfiguracja filtrĂłw CC-Cluster
  preferences->putBool("cluster_noann", clusterNoAnnouncements);
  preferences->putBool("cluster_nowwv", clusterNoWWV);
  preferences->putBool("cluster_nowcy", clusterNoWCY);
  preferences->putBool("cluster_usefilt", clusterUseFilters);
  preferences->putString("cluster_filters", clusterFilterCommands);
  
  preferences->end();
  
  Serial.println("Konfiguracja zapisana");
}

// ========== WEB SERVER ==========

String getMainHTML();
String getConfigHTML();

// Zmienna przechowujÄ…ca czas startu systemu (do obliczania uptime)
static unsigned long bootTimeMs = 0;

// Funkcja formatujÄ…ca uptime w czytelny sposĂłb (dni, godziny, minuty, sekundy)
String formatUptime(unsigned long ms) {
  unsigned long totalSeconds = ms / 1000;
  unsigned long days = totalSeconds / 86400;
  unsigned long hours = (totalSeconds % 86400) / 3600;
  unsigned long minutes = (totalSeconds % 3600) / 60;
  unsigned long seconds = totalSeconds % 60;

  String result;
  if (days > 0) {
    result += String(days) + "d ";
  }
  if (hours > 0 || days > 0) {
    result += String(hours) + "h ";
  }
  if (minutes > 0 || hours > 0 || days > 0) {
    result += String(minutes) + "m ";
  }
  result += String(seconds) + "s";
  return result;
}

// Serwuje plik z LittleFS. Zwraca false, gdy system plikow niedostepny lub brak pliku.
static bool streamLittleFsFile(const char *path, const char *contentType) {
  if (!littleFsReady || !LittleFS.exists(path)) {
    return false;
  }
  File f = LittleFS.open(path, "r");
  if (!f) {
    return false;
  }
  server->streamFile(f, contentType);
  f.close();
  return true;
}

String getManualPL() {
  return String(
    "ESP32-HAM-CLOCK v1.2 (12.10.2026)\n"
    "Autor oprogramowania: Tomasz SP9TNV\n"
    "Na podstawie projektu: Konrad Wisniewski SP3KON (z uĹĽyciem AI)\n"
    "Kontakt: SP9TNV@gmail.com\n\n"
    "Wgrywanie przez przeglÄ…darkÄ™ (ESP Web Tools)\n"
    "- Strona: https://jason2866.github.io/WebSerial_ESPTool/\n"
    "- Wymagany Chrome lub Edge (WebSerial).\n"
    "- Pliki znajdujÄ… siÄ™ w folderze: build\\esp32s3\\\n"
    "- Dotyczy: ESP32-S3-WROOM-1 N16R8 (16MB flash, 8MB PSRAM, tablica partycji partitions.csv).\n\n"
    "Wgrywanie:\n"
    "1. bootloader.bin    0x0\n"
    "2. partitions.bin    0x8000\n"
    "3. firmware.bin      0x10000\n"
    "4. littlefs.bin      0x810000\n\n"
    "Po starcie urzÄ…dzenie tworzy AP: SSID ESP32-HAM-CLOCK, hasĹ‚o 1234567890\n"
    "(jeĹ›li brak konfiguracji WiFi).\n\n"
    "Konfiguracja WWW:\n"
    "- WejdĹş na /config\n"
    "- WiFi: dwa zestawy SSID/hasĹ‚o (podstawowe i zapasowe)\n"
    "- DX Cluster: host/port + opcje filtrĂłw\n"
    "- POTA: External POTA API Link\n"
    "- HAMALERT: telnet host, port, login i hasĹ‚o\n"
    "- QRZ.com: username i password\n"
    "- OpenWeather: klucz API\n"
    "- TFT: jasnoĹ›Ä‡, jÄ™zyk, rozmiar tabel, AUTO-SWITCH\n\n"
    "WiÄ™cej informacji: https://github.com/Tom50505050/esp32-ham-clock\n"
  );
}

// ============================================================================
//  Upload binarny przez "surowy" korpus (RAW) zamiast multipart/form-data.
//  Powod: parser multipart w WebServer (core 2.x) czyta bajt-po-bajcie i przy
//  wiekszych plikach (np. 300 KB BMP) potrafi zresetowac polaczenie (znany bug
//  arduino-esp32 #9163). Sciezka RAW czyta korpus wg Content-Length w blokach
//  po HTTP_RAW_BUFLEN (1436 B) - jest szybka i niezawodna dla plikow binarnych.
//  WWW wysyla plik jako surowe cialo z naglowkami X-Filename i X-Upload-Target.
// ============================================================================
static String rawUploadPath = "";       // sciezka docelowa w LittleFS
static bool   rawUploadIsSplash = false;
static bool   rawUploadActive = false;
static File   rawUploadFile;            // trzymany otwarty przez caly upload

static String uploadExtOf(const String &name) {
  String lc = name;
  lc.toLowerCase();
  int dot = lc.lastIndexOf('.');
  return (dot >= 0) ? lc.substring(dot) : String("");
}

// Zwraca sciezke docelowa lub "" gdy format nieobslugiwany.
// target: "mapa" -> mapa PSK, "welcome" -> ekran powitalny, inaczej splash.
static String resolveUploadTarget(const String &target, const String &fname) {
  String ext = uploadExtOf(fname);
  if (ext != ".bmp" && ext != ".png" && ext != ".jpg" && ext != ".jpeg") return "";
  if (target == "mapa") return String(PSK_MAP_BMP_PATH);
  if (target == "welcome") return "/welcome" + ext;
  return "/splash" + ext;
}

// Usuwa stare warianty ekranu startowego (rozne nazwy/rozszerzenia), zeby
// firmware nie wczytal starego pliku o wyzszym priorytecie.
static void removeOldSplashVariants() {
  static const char *oldSplashes[] = {
    "/splash.png", "/splash.jpg", "/splash.jpeg", "/splash.bmp",
    "/splash_16bit.bmp", "/splash16.bmp", "/icon50/splash.bmp",
    "/boot.bmp", "/boot.png", "/logo.bmp", "/logo.png",
    "/start.bmp", "/start.png"};
  for (const char *o : oldSplashes) {
    if (LittleFS.exists(o)) LittleFS.remove(o);
  }
}

// Usuwa stare warianty ekranu powitalnego (zastepowanego grafika).
static void removeOldWelcomeVariants() {
  static const char *oldWelcome[] = {
    "/welcome.png", "/welcome.jpg", "/welcome.jpeg", "/welcome.bmp"};
  for (const char *o : oldWelcome) {
    if (LittleFS.exists(o)) LittleFS.remove(o);
  }
}

static void rawUploadReset() {
  if (rawUploadFile) rawUploadFile.close();
  rawUploadPath = "";
  rawUploadIsSplash = false;
  rawUploadActive = false;
}

static void rawUploadFinish() {
  if (!rawUploadActive) return;
  rawUploadActive = false;
  if (rawUploadFile) rawUploadFile.close();
  size_t sz = 0;
  File f = LittleFS.open(rawUploadPath);
  if (f) { sz = f.size(); f.close(); }
  Serial.printf("[UPLOAD] Zakonczono: %s (%d B)\n", rawUploadPath.c_str(), (int)sz);
  // Nie odswiezamy tu ekranu startowego - WWW po uploadzie robi redirect (GET /),
  // ktory i tak rysuje splash. Odswiezanie tutaj powodowalo miganie ekranu.
  if (currentScreen == SCREEN_PSK_MAP) drawPskMap();
}

// Stan uploadu multipart (stara sciezka - zachowana dla kompatybilnosci).
static String mpUploadPath = "";
static bool   mpUploadIsSplash = false;
static bool   mpUploadActive = false;

// Wspolna obsluga uploadu. Rozpoznaje tryb po naglowku Content-Type:
//  - multipart/form-data -> klasyczna sciezka WebServer::upload()
//  - cokolwiek innego    -> RAW (surowe cialo wg Content-Length) - szybkie i
//    niezawodne dla duzych plikow binarnych.
// targetForced: "mapa", "welcome" lub "splash"; naglowek X-Upload-Target ma
// pierwszenstwo nad ta wartoscia, a gdy oba puste - domyslnie "splash".
static void handleUpload(const String &targetForced) {
  if (!server) return;
  String ctype = server->header("Content-Type");

  if (ctype.startsWith("multipart/")) {
    HTTPUpload &upload = server->upload();
    if (upload.status == UPLOAD_FILE_START) {
      mpUploadActive = false;
      if (!littleFsReady) return;
      // Naglowek X-Upload-Target ma pierwszenstwo nad celem wymuszonym przez
      // trase - inaczej upload na /upload_splash z naglowkiem "splash" trafialby
      // do celu domyslnego ("welcome").
      String target = server->header("X-Upload-Target");
      if (target.length() == 0) target = targetForced;
      if (target.length() == 0) target = "splash";
      String path = resolveUploadTarget(target, upload.filename);
      if (path.length() == 0) { Serial.println("[UPLOAD] Nieobslugiwany format"); return; }
      if (target == "welcome") removeOldWelcomeVariants();
      else if (target != "mapa") removeOldSplashVariants();
      LittleFS.remove(path);
      File f = LittleFS.open(path, "w");
      if (!f) { Serial.println("[UPLOAD] Nie moge otworzyc pliku"); return; }
      f.close();
      mpUploadPath = path;
      mpUploadIsSplash = (target != "mapa");
      mpUploadActive = true;
      Serial.printf("[UPLOAD] multipart: %s -> %s\n", upload.filename.c_str(), path.c_str());
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (!mpUploadActive) return;
      File f = LittleFS.open(mpUploadPath, "a");
      if (f) { f.write(upload.buf, upload.currentSize); f.close(); }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (!mpUploadActive) return;
      mpUploadActive = false;
      Serial.printf("[UPLOAD] multipart koniec: %s (%d B)\n", mpUploadPath.c_str(), (int)upload.totalSize);
      // Splash odswiezy GET / po przekierowaniu (patrz /upload_splash) - bez podwojnego rysowania.
      if (currentScreen == SCREEN_PSK_MAP) drawPskMap();
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
      mpUploadActive = false;
      Serial.println("[UPLOAD] multipart przerwany");
    }
    return;
  }

  HTTPRaw &raw = server->raw();
  if (raw.status == RAW_START) {
    rawUploadReset();
    if (!littleFsReady) return;
    String fname = server->header("X-Filename");
    if (fname.length() == 0) fname = (targetForced == "mapa") ? "Mapa swiata.bmp" : "splash.bmp";
    // Naglowek X-Upload-Target ma pierwszenstwo nad celem wymuszonym przez trase.
    String target = server->header("X-Upload-Target");
    if (target.length() == 0) target = targetForced;
    if (target.length() == 0) target = "splash";
    String path = resolveUploadTarget(target, fname);
    if (path.length() == 0) { Serial.println("[UPLOAD] Nieobslugiwany format (RAW)"); return; }
    if (target == "welcome") removeOldWelcomeVariants();
    else if (target != "mapa") removeOldSplashVariants();
    LittleFS.remove(path);
    rawUploadFile = LittleFS.open(path, "w");
    if (!rawUploadFile) { Serial.println("[UPLOAD] Nie moge otworzyc pliku (RAW)"); return; }
    rawUploadPath = path;
    rawUploadIsSplash = (target != "mapa");
    rawUploadActive = true;
    Serial.printf("[UPLOAD] RAW: %s -> %s\n", fname.c_str(), path.c_str());
  } else if (raw.status == RAW_WRITE) {
    if (!rawUploadActive || !rawUploadFile) return;
    rawUploadFile.write(raw.buf, raw.currentSize);
  } else if (raw.status == RAW_END || raw.status == RAW_ABORTED) {
    rawUploadFinish();
  }
}


void setupWebServer() {
  // UtwĂ„â€šĹ‚rz serwer jeĂ„Ä…Ă˘â‚¬Ĺźli jeszcze nie istnieje
  if (server == nullptr) {
    server = new WebServer(80);
  }

  // Naglowki potrzebne do uploadu RAW (nazwa pliku + cel) i rozpoznania trybu.
  // MUSI byc przed begin(). Content-Type decyduje: multipart vs surowe cialo.
  static const char *uploadHdrKeys[] = {"Content-Type", "X-Filename", "X-Upload-Target", "Content-Length"};
  server->collectHeaders(uploadHdrKeys, 4);


  
  // Strona glowna - LittleFS jesli plik istnieje i jest kompletny, inaczej wbudowany HTML.
  server->on("/", HTTP_GET, []() {
    if (!streamLittleFsFile("/index.html", "text/html; charset=utf-8")) {
      server->send(200, "text/html; charset=utf-8", getMainHTML());
    }
  });

  // Strona glowna (alias /index.html)
  server->on("/index.html", HTTP_GET, []() {
    if (!streamLittleFsFile("/index.html", "text/html; charset=utf-8")) {
      server->send(200, "text/html; charset=utf-8", getMainHTML());
    }
  });

  // Strona glowna (EN) - jesli LittleFS ma indexEN.html
  server->on("/indexEN.html", HTTP_GET, []() {
    if (!streamLittleFsFile("/indexEN.html", "text/html; charset=utf-8")) {
      server->send(200, "text/html; charset=utf-8", getMainHTML());
    }
  });

  // Instrukcja (PL) - zwykly tekst dla zakladki "Instrukcja" w WWW.
  // UWAGA: wczesniej bylo tu HTTP 301 z przekierowaniem w <meta refresh>,
  // ale BEZ naglowka Location - fetch() nie podaza za meta-refresh, a 301
  // bez Location zwraca response.ok == false, wiec JS pokazywal "Nie udalo
  // sie wczytac instrukcji". Teraz oddajemy po prostu tekst.
  server->on("/instrukcja.txt", HTTP_GET, []() {
    if (!streamLittleFsFile("/instrukcja.txt", "text/plain; charset=utf-8")) {
      server->send(200, "text/plain; charset=utf-8", getManualPL());
    }
  });

  // Manual (EN)
  server->on("/manual.txt", HTTP_GET, []() {
    if (littleFsReady && LittleFS.exists("/manual.txt")) {
      File f = LittleFS.open("/manual.txt", "r");
      server->streamFile(f, "text/plain; charset=utf-8");
      f.close();
    } else {
      server->send(404, "text/plain", "manual.txt missing in LittleFS");
    }
  });

  // Arkusze stylow CSS pelnego UI (LittleFS). Bez nich index.html nie ma wygladu.
  server->on("/style.css", HTTP_GET, []() {
    if (!streamLittleFsFile("/style.css", "text/css; charset=utf-8")) {
      server->send(404, "text/plain", "style.css missing in LittleFS");
    }
  });
  server->on("/style.base.css", HTTP_GET, []() {
    if (!streamLittleFsFile("/style.base.css", "text/css; charset=utf-8")) {
      server->send(404, "text/plain", "style.base.css missing in LittleFS");
    }
  });
  server->on("/style.desktop.css", HTTP_GET, []() {
    if (!streamLittleFsFile("/style.desktop.css", "text/css; charset=utf-8")) {
      server->send(404, "text/plain", "style.desktop.css missing in LittleFS");
    }
  });
  server->on("/style.mobile.css", HTTP_GET, []() {
    if (!streamLittleFsFile("/style.mobile.css", "text/css; charset=utf-8")) {
      server->send(404, "text/plain", "style.mobile.css missing in LittleFS");
    }
  });
  server->on("/style.modern.css", HTTP_GET, []() {
    if (!streamLittleFsFile("/style.modern.css", "text/css; charset=utf-8")) {
      server->send(404, "text/plain", "style.modern.css missing in LittleFS");
    }
  });
  server->on("/satellite.js", HTTP_GET, []() {
    if (!streamLittleFsFile("/satellite.js", "application/javascript; charset=utf-8")) {
      server->send(404, "text/plain", "satellite.js missing in LittleFS");
    }
  });

  // Instrukcja montaĹĽu i wgrania (HTML)
  server->on("/instruction", HTTP_GET, []() {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>ESP32-HAM-CLOCK - Instrukcja</title>";
    html += "<style>";
    html += "body{font-family:Arial,sans-serif;margin:20px;background:#f0f0f0;color:#333;line-height:1.6;}";
    html += "h1{color:#0066cc;border-bottom:2px solid #0066cc;padding-bottom:10px;}";
    html += "h2{color:#0099cc;margin-top:30px;border-left:4px solid #0099cc;padding-left:10px;}";
    html += "h3{color:#666;margin-top:20px;}";
    html += "pre{background:#1a1a1a;color:#00ff00;padding:15px;border-radius:5px;overflow-x:auto;font-size:12px;}";
    html += "code{background:#e0e0e0;padding:2px 6px;border-radius:3px;font-family:monospace;}";
    html += "table{border-collapse:collapse;width:100%;margin:15px 0;background:white;}";
    html += "th,td{border:1px solid #ddd;padding:12px;text-align:left;}";
    html += "th{background:#0066cc;color:white;}";
    html += "tr:nth-child(even){background:#f9f9f9;}";
    html += ".note{background:#fff3cd;border-left:4px solid #ffc107;padding:10px;margin:15px 0;}";
    html += ".warning{background:#f8d7da;border-left:4px solid #dc3545;padding:10px;margin:15px 0;}";
    html += ".success{background:#d4edda;border-left:4px solid #28a745;padding:10px;margin:15px 0;}";
    html += "a{color:#0066cc;text-decoration:none;}";
    html += "a:hover{text-decoration:underline;}";
    html += ".back-btn{display:inline-block;background:#0066cc;color:white;padding:10px 20px;border-radius:5px;margin:20px 0;text-decoration:none;}";
    html += ".back-btn:hover{background:#0055aa;text-decoration:none;}";
    html += "</style></head><body>";

    html += "<h1>đź“» ESP32-HAM-CLOCK - Instrukcja MontaĹĽu i Wgrania</h1>";
    html += "<a href='/' class='back-btn'>â† PowrĂłt do gĹ‚Ăłwnej</a>";

    // Sekcja montażu (zaktualizowana dla ESP32-S3 N16R8 + ILI9488 IPS)
    html += "<h2>Montaż - schemat połączeń</h2>";

    html += "<h3>Wymagane komponenty</h3>";
    html += "<table>";
    html += "<tr><th>Element</th><th>Opis</th><th>Alternatywa</th></tr>";
    html += "<tr><td>ESP32</td><td>ESP32-S3-WROOM-1 N16R8 (16MB flash, 8MB PSRAM)</td><td>inny moduł S3</td></tr>";
    html += "<tr><td>Wyświetlacz</td><td>TFT 3.5\" ILI9488 IPS 480x320 (SPI)</td><td>ST7796 / ILI9341</td></tr>";
    html += "<tr><td>Dotyk</td><td>XPT2046 (rezystancyjny, SPI)</td><td>brak</td></tr>";
    html += "<tr><td>Zasilanie</td><td>Moduł 18650 TP4056 z ochroną DW01A</td><td>USB 5V</td></tr>";
    html += "<tr><td>Przyciski</td><td>2x tact switch (BOOT, RST)</td><td>-</td></tr>";
    html += "<tr><td>Rezystory</td><td>2x 100k (dzielnik napięcia baterii)</td><td>-</td></tr>";
    html += "</table>";

    html += "<h3>Schemat połączeń ESP32-S3 z TFT ILI9488</h3>";
    html += "<pre>";
    html += "ESP32-S3 (N16R8)        TFT ILI9488 (3.5\" 480x320 IPS)\n";
    html += "=====================================================\n";
    html += "GPIO 12 (SCK)    ----->  SCK / SCL\n";
    html += "GPIO 11 (MOSI)   ----->  SDI / MOSI\n";
    html += "GPIO 14 (MISO)   ----->  SDO / MISO   (opcjonalnie)\n";
    html += "GPIO 10 (CS)     ----->  CS\n";
    html += "GPIO 9  (DC)     ----->  DC / RS\n";
    html += "GPIO 21 (BL)     ----->  LED (podświetlenie, PWM)\n";
    html += "RST              ----->  RESET (do 3.3V przez 10k lub pominąć)\n";
    html += "\n";
    html += "ZASILANIE:\n";
    html += "3.3V             ----->  VCC\n";
    html += "GND              ----->  GND\n";
    html += "</pre>";
    html += "<div class='note'><strong>Uwaga (ESP32-S3):</strong> piny GPIO 26-37 są zajęte przez flash (QIO) i PSRAM (OPI) - nie podłączaj tam niczego. GPIO 19/20 to natywne USB. Powyższe piny to FSPI dla S3 (SCK 12, MOSI 11, MISO 14, SS 10). MISO=14 jest wymagane dla dotyku - patrz sekcja dotyku.</div>";

    html += "<h3>Pomiar napięcia baterii (dzielnik)</h3>";
    html += "<pre>";
    html += "Bateria 18650 (+) ----+--- [R1 100k] ---+---> GPIO 1 (ADC1_CH0)\n";
    html += "                      |                 |\n";
    html += "                     GND             [R2 100k] ---+--- GND\n";
    html += "                                                   |\n";
    html += "                                             Kondensator 100nF\n";
    html += "</pre>";
    html += "<div class='note'><strong>Formuła:</strong> Vbat = Vadc x 2 (przy R1 = R2 = 100k).</div>";

    html += "<h3>Podłączenie dotyku XPT2046 (opcjonalnie)</h3>";
    html += "<pre>";
    html += "ESP32-S3                XPT2046 (dotyk, wspólne SPI)\n";
    html += "====================================================\n";
    html += "GPIO 15  ----->  T_CS\n";
    html += "GPIO 16  ----->  T_IRQ\n";
    html += "GPIO 11  ----->  T_DIN   (wspólne z MOSI)\n";
    html += "GPIO 14  ----->  T_DO    (wspólne z MISO; potwierdzone testem)\n";
    html += "GPIO 12  ----->  T_CLK   (wspólne ze SCK)\n";
    html += "</pre>";

    // Sekcja wgrania
    html += "<h2>Wgrywanie Firmware</h2>";

    html += "<h3>Pliki w folderze pliki.bin/</h3>";
    html += "<table>";
    html += "<tr><th>Plik</th><th>Adres</th><th>Opis</th></tr>";
    html += "<tr><td><code>bootloader.bin</code></td><td><code>0x0</code></td><td>Bootloader ESP32-S3</td></tr>";
    html += "<tr><td><code>partitions.bin</code></td><td><code>0x8000</code></td><td>Tablica partycji</td></tr>";
    html += "<tr><td><code>boot_app0.bin</code></td><td><code>0xe000</code></td><td>Inicjalizacja OTA</td></tr>";
    html += "<tr><td><code>firmware.bin</code></td><td><code>0x10000</code></td><td>Główny program</td></tr>";
    html += "<tr><td><code>littlefs.bin</code></td><td><code>0x810000</code></td><td>System plików (ikony, fonty, mapy)</td></tr>";
    html += "</table>";
    html += "<div class='note'>Adresy dotyczą tablicy partycji 16MB z tego projektu (patrz <code>partitions.csv</code>). Dla płytki 4MB LittleFS startuje pod 0x290000.</div>";

    html += "<h3>Sposób 1: WebSerial ESPTool (przeglądarka) - NAJPROSTSZY</h3>";
    html += "<div class='success'><strong>Najprostsza metoda - bez instalowania programów.</strong></div>";
    html += "<ol>";
    html += "<li>Otwórz stronę: <a href='https://jason2866.github.io/WebSerial_ESPTool/' target='_blank'>https://jason2866.github.io/WebSerial_ESPTool/</a></li>";
    html += "<li>Podłącz ESP32-S3 przez USB</li>";
    html += "<li>Kliknij <strong>Connect</strong> i wybierz port COM</li>";
    html += "<li>Przytrzymaj <strong>BOOT</strong>, naciśnij <strong>RST</strong>, puść <strong>BOOT</strong></li>";
    html += "<li>Wybierz pliki (bootloader.bin, partitions.bin, firmware.bin, littlefs.bin)</li>";
    html += "<li>Kliknij <strong>Program</strong> i czekaj</li>";
    html += "<li>Po zakończeniu naciśnij <strong>RST</strong></li>";
    html += "</ol>";

    html += "<h3>Sposób 2: esptool.py (Windows/Linux/Mac)</h3>";
    html += "<pre>";
    html += "pip install esptool\n\n";
    html += "esptool.py --chip esp32s3 --port COM7 --baud 921600 write_flash \\\n";
    html += "  0x0      bootloader.bin \\\n";
    html += "  0x8000   partitions.bin \\\n";
    html += "  0xe000   boot_app0.bin \\\n";
    html += "  0x10000  firmware.bin \\\n";
    html += "  0x810000 littlefs.bin\n";
    html += "</pre>";

    html += "<h3>Sposób 3: PlatformIO (VS Code)</h3>";
    html += "<pre>pio run -e esp32-s3-n16r8 -t upload\npio run -e esp32-s3-n16r8 -t uploadfs</pre>";

    html += "<h3>Sposób 4: jeden plik merged-flash.bin (pod 0x0)</h3>";
    html += "<pre>esptool.py --chip esp32s3 --port COM9 --baud 921600 write_flash 0x0 merged-flash.bin</pre>";
    html += "<div class='note'>Plik <code>merged-flash.bin</code> (16 MB) zawiera bootloader, partycje, boot_app0, firmware i LittleFS. Wgrywa się go w całości pod adres <code>0x0</code> - najprostsza metoda przez przeglądarkę (WebSerial).</div>";

    // Troubleshooting
    html += "<h2>Troubleshooting</h2>";

    html += "<div class='warning'><strong>\"Failed to connect to ESP32-S3\"</strong><br>Przytrzymaj <strong>BOOT</strong> (GPIO0), naciśnij i puść <strong>RST</strong> (EN), puść <strong>BOOT</strong>.</div>";

    html += "<div class='warning'><strong>Biały ekran TFT</strong><br>Sprawdź zasilanie i podświetlenie (GPIO 21). Upewnij się, że LED/BL jest podłączony.</div>";

    html += "<div class='warning'><strong>Brak obrazu na TFT</strong><br>Sprawdź SPI: SCK (GPIO 12), MOSI (GPIO 11), CS (GPIO 10), DC (GPIO 9) i wspólne GND.</div>";

    html += "<div class='note'><strong>Nie działa touch (XPT2046)</strong><br>Sprawdź: T_CLK (GPIO 12), T_CS (GPIO 15), T_DIN (GPIO 11), T_DO (GPIO 14), T_IRQ (GPIO 16).</div>";

    html += "<div class='note'><strong>Odwrócone kolory</strong><br>Panel ILI9488 IPS wymaga inwersji (TFT_INVERSION_ON). Można też przełączyć w panelu konfiguracji.</div>";

    // Stopka
    html += "<hr style='margin:40px 0;'>";
    html += "<p style='text-align:center;color:#666;'>";
    html += "<strong>ESP32-HAM-CLOCK v1.2</strong> | Autor oprogramowania: Tomasz SP9TNV<br>";
    html += "Na podstawie projektu Konrad WiĹ›niewski SP3KON | Licencja: MIT<br>";
    html += "<a href='mailto:SP9TNV@gmail.com'>SP9TNV@gmail.com</a>";
    html += "</p>";

    html += "<a href='/' class='back-btn'>â† PowrĂłt do gĹ‚Ăłwnej</a>";
    html += "</body></html>";

    server->send(200, "text/html; charset=utf-8", html);
  });

  // Strona konfiguracji - LittleFS jesli istnieje, potem wbudowany HTML
  server->on("/config", HTTP_GET, []() {
    if (!streamLittleFsFile("/index.html", "text/html; charset=utf-8")) {
      server->send(200, "text/html; charset=utf-8", getConfigHTML());
    }
  });
  
  // API - pobierz wszystkie spoty (maksymalnie 50)
  server->on("/api/spots", HTTP_GET, []() {
    // ZwiÄ‚â€žĂ˘â€žËkszony bufor JSON dla 50 spotĂ„â€šĹ‚w (kaĂ„Ä…Ă„Ëťdy spot ~150-200 bajtĂ„â€šĹ‚w)
    DynamicJsonDocument doc(12000);  // 50 spotĂ„â€šĹ‚w * ~200 bajtĂ„â€šĹ‚w = ~10KB + margines
    JsonArray spotsArray = doc.createNestedArray("spots");
    
    // ZwrĂ„â€šĹ‚Ä‚â€žĂ˘â‚¬Ë‡ wszystkie spoty (maksymalnie 50)
    for (int i = 0; i < spotCount; i++) {
      JsonObject spotObj = spotsArray.createNestedObject();
      spotObj["time"] = spots[i].time;
      spotObj["callsign"] = spots[i].callsign;
      spotObj["frequency"] = spots[i].frequency;
      spotObj["distance"] = spots[i].distance;
      spotObj["country"] = spots[i].country;
      spotObj["band"] = spots[i].band;
      spotObj["mode"] = spots[i].mode;
      spotObj["spotter"] = spots[i].spotter;
      spotObj["comment"] = spots[i].comment;
    }
    
    String json;
    serializeJson(doc, json);
    server->send(200, "application/json", json);
  });
  
  // API - pobierz stacje APRS
  server->on("/api/aprs", HTTP_GET, []() {
    // Bufor JSON dla 20 stacji APRS (kaĂ„Ä…Ă„Ëťda stacja ~200-250 bajtĂ„â€šĹ‚w)
    DynamicJsonDocument doc(6000);  // 20 stacji * ~250 bajtĂ„â€šĹ‚w = ~5KB + margines
    JsonArray aprsArray = doc.createNestedArray("stations");
    
    // ZwrĂ„â€šĹ‚Ä‚â€žĂ˘â‚¬Ë‡ wszystkie stacje APRS (maksymalnie 20)
    for (int i = 0; i < aprsStationCount; i++) {
      JsonObject stationObj = aprsArray.createNestedObject();
      stationObj["time"] = aprsStations[i].time;
      stationObj["callsign"] = aprsStations[i].callsign;
      stationObj["symbol"] = getAPRSSymbolShort(aprsStations[i]);
      stationObj["lat"] = aprsStations[i].lat;
      stationObj["lon"] = aprsStations[i].lon;
      stationObj["comment"] = aprsStations[i].comment;
      stationObj["distance"] = aprsStations[i].distance;
      stationObj["hasLatLon"] = aprsStations[i].hasLatLon;
      stationObj["freq_mhz"] = aprsStations[i].freqMHz;
    }
    
    String json;
    serializeJson(doc, json);
    server->send(200, "application/json", json);
  });

  // API - POTA (bufor z backendu, z krajami z QRZ jeĹ›li dostÄ™pne)
  server->on("/api/pota", HTTP_GET, []() {
    DynamicJsonDocument doc(12000); // do 30 spotĂłw * ~300-350 bajtĂłw
    JsonArray spotsArray = doc.createNestedArray("spots");
    const int apiSpotLimit = 30;
    const int potaApiCount = (potaSpotCount < apiSpotLimit) ? potaSpotCount : apiSpotLimit;
    for (int i = 0; i < potaApiCount; i++) {
      JsonObject spotObj = spotsArray.createNestedObject();
      spotObj["time"] = potaSpots[i].time;
      spotObj["spotTime"] = potaSpots[i].time; // zgodnoĹ›Ä‡ z POTA API
      spotObj["callsign"] = potaSpots[i].callsign;
      spotObj["frequency"] = potaSpots[i].frequency;
      spotObj["mode"] = potaSpots[i].mode;
      spotObj["country"] = potaSpots[i].country;
      spotObj["spotter"] = potaSpots[i].spotter;
      spotObj["comment"] = potaSpots[i].comment;
      spotObj["band"] = potaSpots[i].band;
    }
    doc["count"] = potaApiCount;
    doc["total"] = potaSpotCount;
    String json;
    serializeJson(doc, json);
    server->send(200, "application/json", json);
  });

  // API - HAMALERT (ostatnie spoty z telnetu)
  server->on("/api/hamalert", HTTP_GET, []() {
    DynamicJsonDocument doc(12000); // do 30 spotĂłw
    JsonArray spotsArray = doc.createNestedArray("spots");
    const int apiSpotLimit = 30;
    const int hamalertApiCount = (hamalertSpotCount < apiSpotLimit) ? hamalertSpotCount : apiSpotLimit;
    for (int i = 0; i < hamalertApiCount; i++) {
      JsonObject spotObj = spotsArray.createNestedObject();
      spotObj["time"] = hamalertSpots[i].time;
      spotObj["spotTime"] = hamalertSpots[i].time;
      spotObj["callsign"] = hamalertSpots[i].callsign;
      spotObj["frequency"] = hamalertSpots[i].frequency;
      spotObj["mode"] = hamalertSpots[i].mode;
      spotObj["country"] = hamalertSpots[i].country;
      spotObj["spotter"] = hamalertSpots[i].spotter;
      spotObj["comment"] = hamalertSpots[i].comment;
      spotObj["band"] = hamalertSpots[i].band;
    }
    doc["count"] = hamalertApiCount;
    doc["total"] = hamalertSpotCount;
    String json;
    serializeJson(doc, json);
    server->send(200, "application/json", json);
  });
  
  // API - pobierz spoty PSK Reporter
  server->on("/api/psk", HTTP_GET, []() {
    DynamicJsonDocument doc(10000);
    JsonArray spotsArray = doc.createNestedArray("spots");

    for (int i = 0; i < pskSpotCount; i++) {
      JsonObject spotObj = spotsArray.createNestedObject();
      spotObj["callsign"] = pskSpots[i].callsign;
      spotObj["lat"] = pskSpots[i].lat;
      spotObj["lon"] = pskSpots[i].lon;
      spotObj["band"] = pskSpots[i].band;
      spotObj["mode"] = pskSpots[i].mode;
      spotObj["snr"] = pskSpots[i].snr;
    }
    
    String json;
    serializeJson(doc, json);
    server->send(200, "application/json", json);
  });
  
  // API - pobierz dane ISS
  server->on("/api/iss", HTTP_GET, []() {
    // Snapshot all shared ISS values under the mutex before building the
    // response. issCountry in particular is a String (heap-backed) that
    // the ISS task writes under this same mutex - reading it without the
    // lock here could occasionally race with that write mid-assignment
    // (freed/reallocated buffer, inconsistent length) and crash the device.
    double snapLat = 0, snapLng = 0, snapAlt = 0, snapSpeed = 0;
    double snapAz = 0, snapElev = 0, snapDist = 0;
    String snapCountry = "CZEKAJ...";
    if (issDataMutex != NULL && xSemaphoreTake(issDataMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
      snapLat = issLat;
      snapLng = issLng;
      snapAlt = issAltitude;
      snapSpeed = issSpeed;
      snapAz = issAzimuth;
      snapElev = issElevation;
      snapDist = issDistance;
      snapCountry = issCountry;
      xSemaphoreGive(issDataMutex);
    }

    StaticJsonDocument<512> doc;
    doc["lat"] = snapLat;
    doc["lng"] = snapLng;
    doc["altitude"] = snapAlt;
    doc["speed"] = snapSpeed;
    doc["azimuth"] = snapAz;
    doc["elevation"] = snapElev;
    doc["distance"] = snapDist;
    doc["country"] = snapCountry.length() > 0 ? snapCountry : "BRAK DANYCH";
    String response;
    serializeJson(doc, response);
    server->send(200, "application/json", response);
  });

  // API - lekki status systemu (dla paska statusu w WWW; bez pelnej konfiguracji)
  server->on("/api/status", HTTP_GET, []() {
    StaticJsonDocument<256> doc;
    doc["ip"] = wifiConnected ? WiFi.localIP().toString() : (WiFi.getMode() & WIFI_AP ? WiFi.softAPIP().toString() : "disconnected");
    doc["heap"] = ESP.getFreeHeap();
    doc["rssi"] = wifiConnected ? WiFi.RSSI() : 0;
    doc["uptime"] = (unsigned long)(millis() - bootTimeMs);
    String response;
    serializeJson(doc, response);
    server->send(200, "application/json", response);
  });

  // API - pelne sledzenie satelity: pozycja + trajektoria naziemna (ground track)
  // + przewidywany najblizszy przelot. Trajektoria pochodzi z SGP4 (issTrajectoryLat/Lng).
  server->on("/api/iss_track", HTTP_GET, []() {
    double snapLat = 0, snapLng = 0, snapAlt = 0, snapSpeed = 0;
    double snapAz = 0, snapElev = 0, snapDist = 0;
    String snapCountry = "CZEKAJ...";
    if (issDataMutex != NULL && xSemaphoreTake(issDataMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
      snapLat = issLat;
      snapLng = issLng;
      snapAlt = issAltitude;
      snapSpeed = issSpeed;
      snapAz = issAzimuth;
      snapElev = issElevation;
      snapDist = issDistance;
      snapCountry = issCountry;
      xSemaphoreGive(issDataMutex);
    }

    DynamicJsonDocument doc(6144);
    doc["lat"] = snapLat;
    doc["lng"] = snapLng;
    doc["altitude"] = snapAlt;
    doc["speed"] = snapSpeed;
    doc["azimuth"] = snapAz;
    doc["elevation"] = snapElev;
    doc["distance"] = snapDist;
    doc["country"] = snapCountry.length() > 0 ? snapCountry : "BRAK DANYCH";
    doc["observer_lat"] = userLatLonValid ? userLat : 52.40;
    doc["observer_lng"] = userLatLonValid ? userLon : 16.92;
    doc["tle_valid"] = issTleValid;
    doc["next_pass_epoch"] = (long)issNextPassEstimateEpoch;
    doc["next_pass_valid"] = issNextPassEstimateValid;
    doc["next_pass_aos"] = (long)issNextPassAosEpoch;
    doc["next_pass_los"] = (long)issNextPassLosEpoch;
    doc["next_pass_max_el"] = issNextPassMaxEl;
    doc["iss_visible"] = issNextPassWasVisible;
    doc["country_code"] = issCountryCode;
    doc["trajectory_epoch"] = issTrajectoryEpoch;
    doc["trajectory_step"] = issTrajectoryStepSec;
    doc["in_range"] = issWasInRangeLastCheck;

    // Lista kilku najbliszych przelotow [aos, los, maxEl] - dla tabeli
    // "nastepne przeloty" w WWW. Kopiujemy pod dostepem do danych, zeby nie
    // odczytac wpisu w trakcie aktualizacji przez zadanie ISS.
    JsonArray passesArr = doc.createNestedArray("passes");
    for (int i = 0; i < issPassCount && i < ISS_MAX_PASSES; i++) {
      JsonArray p = passesArr.createNestedArray();
      p.add((long)issPassAos[i]);
      p.add((long)issPassLos[i]);
      p.add(issPassMaxEl[i]);
      // Okno widocznosci (n2yo visualpasses): [startVis, endVis, duration_s, mag]
      // - WWW pokazuje "widoczny" i czas trwania. Zera gdy brak danych.
      p.add((long)issPassVisStart[i]);
      p.add((long)issPassVisEnd[i]);
      p.add(issPassDuration[i]);
      p.add(issPassMag[i]);
    }

    JsonArray track = doc.createNestedArray("track");
    for (int i = 0; i < ISS_TRAJECTORY_POINTS; i++) {
      JsonArray pt = track.createNestedArray();
      pt.add(issTrajectoryLat[i]);
      pt.add(issTrajectoryLng[i]);
    }

    String response;
    serializeJson(doc, response);
    server->send(200, "application/json", response);
  });
  
  // API - zapisz konfiguracjÄ‚â€žĂ˘â€žË
  server->on("/api/save", HTTP_POST, []() {
    if (server->hasArg("plain")) {
      String body = server->arg("plain");
      
        DynamicJsonDocument doc(8192);  // Bufor dla peĹ‚nego payloadu konfiguracji z WWW
      DeserializationError err = deserializeJson(doc, body);
      if (err) {
        server->send(400, "application/json", "{\"status\":\"error\",\"message\":\"json_parse\"}");
        return;
      }
      
      String prevSsid = wifiSSID;
      String prevSsid2 = wifiSSID2;
      // WiFi: nadpisuj TYLKO gdy klucz jest obecny w payloadzie. Import czesciowego
      // backupu (albo starsze/okrojone UI) nie moze wyzerowac SSID/hasla - inaczej
      // po restarcie ESP nie ma sie z czym polaczyc i zostaje w trybie AP.
      if (doc.containsKey("wifi_ssid")) {
        wifiSSID = doc["wifi_ssid"].as<String>();
      }
      if (doc.containsKey("wifi_pass")) {
        wifiPassword = doc["wifi_pass"].as<String>();
      }
      if (doc.containsKey("wifi_ssid2")) {
        wifiSSID2 = doc["wifi_ssid2"].as<String>();
      }
      if (doc.containsKey("wifi_pass2")) {
        wifiPassword2 = doc["wifi_pass2"].as<String>();
      }
      clusterHost = doc["cluster_host"].as<String>();
      if (doc["pota_host"].is<String>()) {
        potaClusterHost = doc["pota_host"].as<String>();
      }
      if (doc["pota_filter"].is<String>()) {
        potaFilterCommand = doc["pota_filter"].as<String>();
      }
      if (doc["pota_api_url"].is<String>()) {
        potaApiUrl = doc["pota_api_url"].as<String>();
      }
      if (doc["hamalert_host"].is<String>()) {
        hamalertHost = doc["hamalert_host"].as<String>();
      }
      if (doc["hamalert_port"].is<int>()) {
        hamalertPort = doc["hamalert_port"].as<int>();
      }
      if (doc["hamalert_login"].is<String>()) {
        hamalertLogin = doc["hamalert_login"].as<String>();
      }
      if (doc["hamalert_password"].is<String>()) {
        hamalertPassword = doc["hamalert_password"].as<String>();
      }
      userCallsign = doc["user_callsign"].as<String>();
      userLocator = doc["user_locator"].as<String>();
      // GPS (NMEA po UART2). Klucze tylko nadpisuja, gdy obecne w payloadzie.
      if (doc.containsKey("gps_enabled")) gpsEnabled = doc["gps_enabled"].as<bool>();
      if (doc.containsKey("gps_use_for_position")) gpsUseForPosition = doc["gps_use_for_position"].as<bool>();
      // Zgodnosc ze starszym UI: "Twoja Stacja" uzywa klucza use_gps_if_active.
      if (doc.containsKey("use_gps_if_active")) gpsUseForPosition = doc["use_gps_if_active"].as<bool>();
      if (doc["gps_baud"].is<int>()) {
        gpsBaud = doc["gps_baud"].as<int>();
        if (gpsBaud < 300 || gpsBaud > 2000000) gpsBaud = 9600;
      }
      if (doc["gps_rx_pin"].is<int>()) gpsRxPin = doc["gps_rx_pin"].as<int>();
      if (doc["gps_tx_pin"].is<int>()) gpsTxPin = doc["gps_tx_pin"].as<int>();
      if (doc["timezone"].is<int>()) {
        timezoneHours = doc["timezone"].as<int>();
        if (timezoneHours < -12) timezoneHours = -12;
        if (timezoneHours > 14) timezoneHours = 14;
      }
      
      // WspĂ„â€šĹ‚Ă„Ä…Ă˘â‚¬ĹˇrzÄ‚â€žĂ˘â€žËdne geograficzne (LAT/LON)
      bool latProvided = false;
      bool lonProvided = false;
      if (doc["user_lat"].is<float>() || doc["user_lat"].is<double>()) {
        userLat = doc["user_lat"].as<double>();
        latProvided = true;
      }
      if (doc["user_lon"].is<float>() || doc["user_lon"].is<double>()) {
        userLon = doc["user_lon"].as<double>();
        lonProvided = true;
      }
      // JeĂ„Ä…Ă˘â‚¬Ĺźli podano obie wspĂ„â€šĹ‚Ă„Ä…Ă˘â‚¬ĹˇrzÄ‚â€žĂ˘â€žËdne, ustaw flagÄ‚â€žĂ˘â€žË jako waĂ„Ä…Ă„Ëťne
      // Uwaga: sprawdzamy czy wartoĂ„Ä…Ă˘â‚¬Ĺźci sÄ‚â€žĂ˘â‚¬Â¦ w prawidĂ„Ä…Ă˘â‚¬Ĺˇowym zakresie (nie tylko != 0)
      // (0,0) z pustego formularza WWW = brak pozycji: nie ustawiaj flagi,
      // zeby nie zasiewac srodka radaru na oceanie i nie ignorowac locatora.
      if (latProvided && lonProvided && 
          userLat >= -90.0 && userLat <= 90.0 && 
          userLon >= -180.0 && userLon <= 180.0 &&
          !(userLat == 0.0 && userLon == 0.0)) {
        userLatLonValid = true;
      } else if (latProvided || lonProvided) {
        // JeĂ„Ä…Ă˘â‚¬Ĺźli podano tylko jednÄ‚â€žĂ˘â‚¬Â¦ wspĂ„â€šĹ‚Ă„Ä…Ă˘â‚¬ĹˇrzÄ‚â€žĂ˘â€žËdnÄ‚â€žĂ˘â‚¬Â¦, uznaj za nieprawidĂ„Ä…Ă˘â‚¬Ĺˇowe
        userLatLonValid = false;
      }
      
      if (doc["qrz_user"].is<String>()) {
        qrzUsername = doc["qrz_user"].as<String>();
      }
      if (doc["qrz_pass"].is<String>()) {
        qrzPassword = doc["qrz_pass"].as<String>();
      }
      if (doc["weather_key"].is<String>()) {
        weatherApiKey = doc["weather_key"].as<String>();
      }
      if (doc["n2yo_key"].is<String>()) {
        n2yoApiKey = doc["n2yo_key"].as<String>();
      }
      if (doc["openwebrx_url"].is<String>()) {
        openWebRxUrl = doc["openwebrx_url"].as<String>();
      }
      if (doc["tft_backlight"].is<int>()) {
        backlightPercent = doc["tft_backlight"].as<int>();
        if (backlightPercent < MIN_BACKLIGHT_PERCENT) backlightPercent = MIN_BACKLIGHT_PERCENT;
        if (backlightPercent > 100) backlightPercent = 100;
      }
      if (doc["tft_night_dimming"].is<int>()) {
        tftNightDimmingPercent = doc["tft_night_dimming"].as<int>();
        if (tftNightDimmingPercent < 0) tftNightDimmingPercent = 0;
        if (tftNightDimmingPercent > 100) tftNightDimmingPercent = 100;
        applyEffectiveBacklight();
      }
      if (doc["tft_invert"].is<bool>()) {
        bool receivedValue = doc["tft_invert"].as<bool>();
        Serial.print("*** Received tft_invert from JSON: ");
        Serial.print(receivedValue ? "true" : "false");
        Serial.println(" ***");
        tftInvertColors = sanitizeTftInvertSetting(receivedValue);
        // Apply inversion immediately after receiving new value
        if (tftInitialized) {
          applyTftInversion();
        }
      }
      if (doc["tft_lang"].is<String>()) {
        tftLanguage = tftLangFromCode(doc["tft_lang"].as<String>());
      } else if (doc["tft_lang"].is<int>()) {
        int lang = doc["tft_lang"].as<int>();
        tftLanguage = (lang == TFT_LANG_EN) ? TFT_LANG_EN : TFT_LANG_PL;
      }
      if (doc["table_size"].is<String>()) {
        dxTableSizeMode = dxTableSizeFromCode(doc["table_size"].as<String>());
      } else if (doc["table_size"].is<int>()) {
        int sizeMode = doc["table_size"].as<int>();
        dxTableSizeMode = (sizeMode == DX_TABLE_SIZE_ENLARGED) ? DX_TABLE_SIZE_ENLARGED : DX_TABLE_SIZE_NORMAL;
      }
      if (doc["menu_hue"].is<int>()) {
        int hue = doc["menu_hue"].as<int>();
        if (hue < 0) hue = 0;
        if (hue > 255) hue = 255;
        menuThemeHue = (uint8_t)hue;
        applyMenuThemeFromHue();
      }
      if (doc["callsign_color"].is<int>()) {
        callsignColor = doc["callsign_color"].as<int>();
        if (callsignColor == 0) callsignColor = 65535;  // 0 = czarny (niewidoczny) -> biel
      }
      if (doc["clock_color"].is<int>()) {
        clockDigitsColor = doc["clock_color"].as<int>();
        if (clockDigitsColor == 0) clockDigitsColor = 0xFFE0;  // 0 = czarny -> zolty
      }
      
      // Konfiguracja filtrĂ„â€šĹ‚w CC-Cluster
      if (doc["cluster_noann"].is<bool>()) {
        clusterNoAnnouncements = doc["cluster_noann"].as<bool>();
      }
      if (doc["cluster_nowwv"].is<bool>()) {
        clusterNoWWV = doc["cluster_nowwv"].as<bool>();
      }
      if (doc["cluster_nowcy"].is<bool>()) {
        clusterNoWCY = doc["cluster_nowcy"].as<bool>();
      }
      if (doc["cluster_usefilters"].is<bool>()) {
        clusterUseFilters = doc["cluster_usefilters"].as<bool>();
      }
      if (doc["cluster_filters"].is<String>()) {
        clusterFilterCommands = doc["cluster_filters"].as<String>();
      }
      
      // Konfiguracja APRS-IS
      if (doc["aprs_host"].is<String>()) {
        aprsIsHost = doc["aprs_host"].as<String>();
      }
      if (doc["aprs_port"].is<int>()) {
        aprsIsPort = doc["aprs_port"].as<int>();
      }
      if (doc["aprs_callsign"].is<String>()) {
        aprsCallsign = doc["aprs_callsign"].as<String>();
      }
      if (doc["aprs_passcode"].is<int>()) {
        aprsPasscode = doc["aprs_passcode"].as<int>();
      }
      if (doc["aprs_ssid"].is<int>()) {
        applyAprsSsid(doc["aprs_ssid"].as<int>());
      }
      if (doc["aprs_beacon"].is<bool>()) {
        aprsBeaconEnabled = doc["aprs_beacon"].as<bool>();
      }
      if (doc["aprs_symbol"].is<String>()) {
        applyAprsSymbol(doc["aprs_symbol"].as<String>());
      }
      if (doc["aprs_comment"].is<String>()) {
        aprsUserComment = sanitizeAprsComment(doc["aprs_comment"].as<String>());
      }
      bool aprsAlertConfigChanged = false;
      if (doc["aprs_alert"].is<String>()) {
        aprsAlertCsv = sanitizeAprsAlertList(doc["aprs_alert"].as<String>());
        aprsAlertConfigChanged = true;
      }
      if (doc["aprs_alert_enabled"].is<bool>()) {
        aprsAlertEnabled = doc["aprs_alert_enabled"].as<bool>();
      }
      if (doc["aprs_alert_nearby_enabled"].is<bool>()) {
        aprsAlertNearbyEnabled = doc["aprs_alert_nearby_enabled"].as<bool>();
      }
      if (doc["aprs_alert_wx_enabled"].is<bool>()) {
        aprsAlertWxEnabled = doc["aprs_alert_wx_enabled"].as<bool>();
        aprsAlertConfigChanged = true;
      }
      if (doc.containsKey("aprs_alert_min_sec")) {
        int alertMinSecCandidate = aprsAlertMinSeconds;
        if (doc["aprs_alert_min_sec"].is<int>()) {
          alertMinSecCandidate = doc["aprs_alert_min_sec"].as<int>();
        } else if (doc["aprs_alert_min_sec"].is<long>()) {
          alertMinSecCandidate = (int)doc["aprs_alert_min_sec"].as<long>();
        } else if (doc["aprs_alert_min_sec"].is<float>() || doc["aprs_alert_min_sec"].is<double>()) {
          alertMinSecCandidate = (int)doc["aprs_alert_min_sec"].as<double>();
        } else if (doc["aprs_alert_min_sec"].is<String>()) {
          String alertMinStr = doc["aprs_alert_min_sec"].as<String>();
          alertMinStr.trim();
          alertMinSecCandidate = alertMinStr.toInt();
        }
        applyAprsAlertMinSeconds(alertMinSecCandidate);
        aprsAlertConfigChanged = true;
      }
      if (doc.containsKey("aprs_alert_screen_sec")) {
        int alertScreenSecCandidate = aprsAlertScreenSeconds;
        if (doc["aprs_alert_screen_sec"].is<int>()) {
          alertScreenSecCandidate = doc["aprs_alert_screen_sec"].as<int>();
        } else if (doc["aprs_alert_screen_sec"].is<long>()) {
          alertScreenSecCandidate = (int)doc["aprs_alert_screen_sec"].as<long>();
        } else if (doc["aprs_alert_screen_sec"].is<float>() || doc["aprs_alert_screen_sec"].is<double>()) {
          alertScreenSecCandidate = (int)doc["aprs_alert_screen_sec"].as<double>();
        } else if (doc["aprs_alert_screen_sec"].is<String>()) {
          String alertScreenSecStr = doc["aprs_alert_screen_sec"].as<String>();
          alertScreenSecStr.trim();
          alertScreenSecCandidate = alertScreenSecStr.toInt();
        }
        applyAprsAlertScreenSeconds(alertScreenSecCandidate);
      }
      if (doc.containsKey("aprs_alert_distance_km")) {
        float alertDistanceCandidate = aprsAlertDistanceKm;
        if (doc["aprs_alert_distance_km"].is<float>() || doc["aprs_alert_distance_km"].is<double>()) {
          alertDistanceCandidate = (float)doc["aprs_alert_distance_km"].as<double>();
        } else if (doc["aprs_alert_distance_km"].is<int>()) {
          alertDistanceCandidate = (float)doc["aprs_alert_distance_km"].as<int>();
        } else if (doc["aprs_alert_distance_km"].is<long>()) {
          alertDistanceCandidate = (float)doc["aprs_alert_distance_km"].as<long>();
        } else if (doc["aprs_alert_distance_km"].is<String>()) {
          String alertDistanceStr = doc["aprs_alert_distance_km"].as<String>();
          alertDistanceStr.trim();
          alertDistanceStr.replace(',', '.');
          alertDistanceCandidate = alertDistanceStr.toFloat();
        }
        applyAprsAlertDistanceKm(alertDistanceCandidate);
      }
      if (doc["enable_led_alert"].is<bool>()) {
        enableLedAlert = doc["enable_led_alert"].as<bool>();
      }
      if (doc.containsKey("led_alert_duration_ms")) {
        int ledDurationCandidate = ledAlertDurationMs;
        if (doc["led_alert_duration_ms"].is<int>()) {
          ledDurationCandidate = doc["led_alert_duration_ms"].as<int>();
        } else if (doc["led_alert_duration_ms"].is<long>()) {
          ledDurationCandidate = (int)doc["led_alert_duration_ms"].as<long>();
        } else if (doc["led_alert_duration_ms"].is<float>() || doc["led_alert_duration_ms"].is<double>()) {
          ledDurationCandidate = (int)doc["led_alert_duration_ms"].as<double>();
        } else if (doc["led_alert_duration_ms"].is<String>()) {
          String ledDurationStr = doc["led_alert_duration_ms"].as<String>();
          ledDurationStr.trim();
          ledDurationCandidate = ledDurationStr.toInt();
        }
        applyLedAlertDurationMs(ledDurationCandidate);
      }
      if (doc.containsKey("led_alert_blink_ms")) {
        int ledBlinkCandidate = ledAlertBlinkMs;
        if (doc["led_alert_blink_ms"].is<int>()) {
          ledBlinkCandidate = doc["led_alert_blink_ms"].as<int>();
        } else if (doc["led_alert_blink_ms"].is<long>()) {
          ledBlinkCandidate = (int)doc["led_alert_blink_ms"].as<long>();
        } else if (doc["led_alert_blink_ms"].is<float>() || doc["led_alert_blink_ms"].is<double>()) {
          ledBlinkCandidate = (int)doc["led_alert_blink_ms"].as<double>();
        } else if (doc["led_alert_blink_ms"].is<String>()) {
          String ledBlinkStr = doc["led_alert_blink_ms"].as<String>();
          ledBlinkStr.trim();
          ledBlinkCandidate = ledBlinkStr.toInt();
        }
        applyLedAlertBlinkMs(ledBlinkCandidate);
      }
      if (doc.containsKey("aprs_interval_min")) {
        int intervalCandidate = aprsIntervalMinutes;
        if (doc["aprs_interval_min"].is<int>()) {
          intervalCandidate = doc["aprs_interval_min"].as<int>();
        } else if (doc["aprs_interval_min"].is<long>()) {
          intervalCandidate = (int)doc["aprs_interval_min"].as<long>();
        } else if (doc["aprs_interval_min"].is<float>() || doc["aprs_interval_min"].is<double>()) {
          intervalCandidate = (int)doc["aprs_interval_min"].as<double>();
        } else if (doc["aprs_interval_min"].is<String>()) {
          String intervalStr = doc["aprs_interval_min"].as<String>();
          intervalStr.trim();
          intervalCandidate = intervalStr.toInt();
        }
        applyAprsIntervalMinutes(intervalCandidate);
      }
      if (aprsAlertConfigChanged) {
        resetAprsAlertCooldownState();
      }
      if (doc["aprs_radius"].is<int>()) {
        aprsFilterRadius = doc["aprs_radius"].as<int>();
        // Ograniczenie promienia do 1-50 km
        if (aprsFilterRadius < 1) aprsFilterRadius = 1;
        if (aprsFilterRadius > 50) aprsFilterRadius = 50;
      }
      // Uwaga: APRS uĂ„Ä…Ă„Ëťywa wspĂ„â€šĹ‚Ă„Ä…Ă˘â‚¬ĹˇrzÄ‚â€žĂ˘â€žËdnych z sekcji "Moja Stacja" (userLat, userLon) - nie ma osobnych pĂ„â€šĹ‚l
      
      // Konfiguracja trybu kalibracji dotyku (rÄ™czne nadpisanie)
      if (doc["touch_swap_mode"].is<String>()) {
        String mode = doc["touch_swap_mode"].as<String>();
        // Resetuj wszystkie flagi
        touchSwapXY = false;
        touchInvertX = false;
        touchInvertY = false;
        // Ustaw odpowiedniÄ… flagÄ™ lub kombinacjÄ™
        if (mode == "xy") {
          touchSwapXY = true;
        } else if (mode == "x") {
          touchInvertX = true;
        } else if (mode == "y") {
          touchInvertY = true;
        } else if (mode == "both") {
          touchInvertX = true;
          touchInvertY = true;
        } else if (mode == "rot90cw") {
          // ObrĂłt 90Ă‚Â° w prawo (clockwise)
          touchSwapXY = true;
          touchInvertX = true;
        } else if (mode == "rot90ccw") {
          // ObrĂłt 90Ă‚Â° w lewo (counter-clockwise)
          touchSwapXY = true;
          touchInvertY = true;
        } else if (mode == "xy_both") {
          touchSwapXY = true;
          touchInvertX = true;
          touchInvertY = true;
        }
        // "none" pozostawia wszystkie false
        Serial.print("Touch swap mode set to: ");
        Serial.println(mode);
      }
      
      // Touch rotation (0-3)
      if (doc["touch_rotation"].is<int>()) {
        int rot = doc["touch_rotation"].as<int>();
        if (rot >= 0 && rot <= 3) {
          touchRotation = (uint8_t)rot;
          Serial.print("Touch rotation set to: ");
          Serial.println(touchRotation);
        }
      }

      // TFT rotation (0-3)
      if (doc["tft_rotation"].is<int>()) {
        int rot = doc["tft_rotation"].as<int>();
        if (rot >= 0 && rot <= 3) {
          tftRotation = (uint8_t)rot;
          touchRotation = tftRotation; // Synchronizuj rotacjÄ™ dotyku z TFT
          Serial.print("TFT rotation set to: ");
          Serial.print(tftRotation);
          Serial.print(", touch rotation synchronized to: ");
          Serial.println(touchRotation);
        }
      }

      // Indywidualne parametry kalibracji dotyku (xmin, xmax, ymin, ymax)
      if (doc["touch_xmin"].is<int>()) {
        int val = doc["touch_xmin"].as<int>();
        if (val >= 0 && val <= 4095) touchXMin = val;
      }
      if (doc["touch_xmax"].is<int>()) {
        int val = doc["touch_xmax"].as<int>();
        if (val >= 0 && val <= 4095) touchXMax = val;
      }
      if (doc["touch_ymin"].is<int>()) {
        int val = doc["touch_ymin"].as<int>();
        if (val >= 0 && val <= 4095) touchYMin = val;
      }
      if (doc["touch_ymax"].is<int>()) {
        int val = doc["touch_ymax"].as<int>();
        if (val >= 0 && val <= 4095) touchYMax = val;
      }
      // Individual touch flags (override touch_swap_mode if provided)
      if (doc["touch_swap"].is<bool>()) {
        touchSwapXY = doc["touch_swap"].as<bool>();
      }
      if (doc["touch_invx"].is<bool>()) {
        touchInvertX = doc["touch_invx"].as<bool>();
      }
      if (doc["touch_invy"].is<bool>()) {
        touchInvertY = doc["touch_invy"].as<bool>();
      }

      // Ustawienia PSKReporter
      if (doc["psk_receiver"].is<String>()) {
        pskReceiverCallsign = doc["psk_receiver"].as<String>();
        pskReceiverCallsign.trim();
        pskReceiverCallsign.toUpperCase();
      } else if (doc["psk_callsign"].is<String>()) {
        pskReceiverCallsign = doc["psk_callsign"].as<String>();
        pskReceiverCallsign.trim();
        pskReceiverCallsign.toUpperCase();
      }
      if (doc["psk_maxspots"].is<int>()) {
        pskMaxSpots = doc["psk_maxspots"].as<int>();
        if (pskMaxSpots < 10) pskMaxSpots = 10;
        if (pskMaxSpots > 100) pskMaxSpots = 100;
      }
      if (doc["psk_hours"].is<int>()) {
        pskHoursWindow = doc["psk_hours"].as<int>();
        if (pskHoursWindow < 1) pskHoursWindow = 1;
        if (pskHoursWindow > 24) pskHoursWindow = 24;
      }
      if (doc["psk_band"].is<String>()) {
        pskFilterBand = doc["psk_band"].as<String>();
      }
      if (doc["psk_mode"].is<String>()) {
        pskFilterMode = doc["psk_mode"].as<String>();
        pskFilterMode.toUpperCase();
      }
      if (doc["psk_url"].is<String>()) {
        pskCustomUrl = doc["psk_url"].as<String>();
        pskCustomUrl.trim();
      }
      // HTTP Monitoring
      if (doc["psk_monitor_callsign"].is<String>()) {
        pskMonitorCallsign = doc["psk_monitor_callsign"].as<String>();
        pskMonitorCallsign.trim();
        pskMonitorCallsign.toUpperCase();
      }
      if (doc["psk_report_days"].is<int>()) {
        pskReportDays = doc["psk_report_days"].as<int>();
        if (pskReportDays < 0) pskReportDays = 0;
        if (pskReportDays > 30) pskReportDays = 30;
      }
      if (doc["psk_auto_refresh"].is<int>()) {
        pskAutoRefreshMinutes = doc["psk_auto_refresh"].as<int>();
        if (pskAutoRefreshMinutes < 0) pskAutoRefreshMinutes = 0;
        if (pskAutoRefreshMinutes > 60) pskAutoRefreshMinutes = 60;
      }
      // MQTT PSK Reporter settings
      if (doc["psk_mqtt_enabled"].is<bool>()) {
        pskMqttEnabled = doc["psk_mqtt_enabled"].as<bool>();
      }
      if (doc["psk_mqtt_server"].is<String>()) {
        pskMqttServer = doc["psk_mqtt_server"].as<String>();
        pskMqttServer.trim();
      }
      if (doc["psk_mqtt_port"].is<int>()) {
        pskMqttPort = doc["psk_mqtt_port"].as<int>();
        if (pskMqttPort <= 0 || pskMqttPort > 65535) pskMqttPort = 1883;
      }
      if (doc["psk_mqtt_callsign"].is<String>()) {
        pskMqttCallsign = doc["psk_mqtt_callsign"].as<String>();
        pskMqttCallsign.trim();
        pskMqttCallsign.toUpperCase();
      }

      // Sanitizacja (najczĂ„â„˘stsza przyczyna: spacje na koĂ„â€¦cu SSID/hasĂ„â€¦a)
      wifiSSID.trim();
      wifiPassword.trim();
      wifiSSID2.trim();
      wifiPassword2.trim();
      clusterHost.trim();
      potaClusterHost.trim();
      potaFilterCommand.trim();
      potaApiUrl.trim();
      hamalertHost.trim();
      hamalertLogin.trim();
      hamalertPassword.trim();
      userCallsign.trim();
      userLocator.trim();
      clusterFilterCommands.trim();
      weatherApiKey.trim();
      n2yoApiKey.trim();
      openWebRxUrl.trim();
      aprsIsHost.trim();
      aprsCallsign.trim();
      // KiwiSDR URL - moĹĽe byÄ‡ puste (uĹĽytkownik nie musi ustawiaÄ‡)

      if (potaApiUrl.length() == 0) {
        potaApiUrl = DEFAULT_POTA_API_URL;
      }
      if (hamalertHost.length() == 0) {
        hamalertHost = DEFAULT_HAMALERT_HOST;
      }
      if (hamalertPort <= 0 || hamalertPort > 65535) {
        hamalertPort = DEFAULT_HAMALERT_PORT;
      }

      // KolejnoĹ›Ä‡ ekranĂłw konfigurowana z WWW
      if (doc["screen_order"].is<JsonArray>()) {
        JsonArray arr = doc["screen_order"].as<JsonArray>();
        int idx = 0;
        for (JsonVariant v : arr) {
          if (idx >= SCREEN_ORDER_COUNT) break;
          if (v.is<const char*>()) {
            String code = v.as<const char*>();
            ScreenType parsed = screenCodeToType(code);
            // Unknown code should not silently clear slot to OFF.
            if (parsed == SCREEN_OFF) {
              String tmp = code;
              tmp.toLowerCase();
              tmp.trim();
              if (tmp != "off") {
                parsed = DEFAULT_SCREEN_ORDER[idx];
              }
            }
            screenOrder[idx] = parsed;
          } else if (v.is<int>()) {
            int raw = v.as<int>();
            ScreenType parsed = normalizeScreenType(raw);
            if (parsed == SCREEN_OFF && raw != 0) {
              parsed = DEFAULT_SCREEN_ORDER[idx];
            }
            screenOrder[idx] = parsed;
          }
          idx++;
        }
        // JeĹ›li JSON byĹ‚ krĂłtszy, pozostaĹ‚e uzupeĹ‚nij wartoĹ›ciami domyĹ›lnymi,
        // aby nie gubiÄ‡ ekranĂłw (np. APRS RADAR) przy starszym/okrojonym payloadzie.
        for (; idx < SCREEN_ORDER_COUNT; idx++) {
          screenOrder[idx] = DEFAULT_SCREEN_ORDER[idx];
        }
      }
      ensureScreenOrderValid();

      if (doc["tft_auto_switch"].is<bool>()) {
        tftAutoSwitchEnabled = doc["tft_auto_switch"].as<bool>();
      }
      if (doc.containsKey("tft_switch_time_sec")) {
        int switchTimeCandidate = tftAutoSwitchTimeSec;
        if (doc["tft_switch_time_sec"].is<int>()) {
          switchTimeCandidate = doc["tft_switch_time_sec"].as<int>();
        } else if (doc["tft_switch_time_sec"].is<long>()) {
          switchTimeCandidate = (int)doc["tft_switch_time_sec"].as<long>();
        } else if (doc["tft_switch_time_sec"].is<float>() || doc["tft_switch_time_sec"].is<double>()) {
          switchTimeCandidate = (int)doc["tft_switch_time_sec"].as<double>();
        } else if (doc["tft_switch_time_sec"].is<String>()) {
          String switchTimeStr = doc["tft_switch_time_sec"].as<String>();
          switchTimeStr.trim();
          switchTimeCandidate = switchTimeStr.toInt();
        }
        applyTftAutoSwitchTimeSec(switchTimeCandidate);
      }
      resetTftAutoSwitchTimer();

      // Ustawienia wygaszacza ekranu (Matrix)
      if (doc["screen_saver_enabled"].is<bool>()) {
        screenSaverEnabled = doc["screen_saver_enabled"].as<bool>();
      }
      if (doc.containsKey("screen_saver_timeout_min")) {
        int timeoutCandidate = screenSaverTimeoutMin;
        if (doc["screen_saver_timeout_min"].is<int>()) {
          timeoutCandidate = doc["screen_saver_timeout_min"].as<int>();
        } else if (doc["screen_saver_timeout_min"].is<long>()) {
          timeoutCandidate = (int)doc["screen_saver_timeout_min"].as<long>();
        } else if (doc["screen_saver_timeout_min"].is<float>() || doc["screen_saver_timeout_min"].is<double>()) {
          timeoutCandidate = (int)doc["screen_saver_timeout_min"].as<double>();
        } else if (doc["screen_saver_timeout_min"].is<String>()) {
          String timeoutStr = doc["screen_saver_timeout_min"].as<String>();
          timeoutStr.trim();
          timeoutCandidate = timeoutStr.toInt();
        }
        // Zastosuj limity bez wywoĹ‚ywania savePreferences()
        if (timeoutCandidate < 1) timeoutCandidate = 1;
        if (timeoutCandidate > 60) timeoutCandidate = 60;
        screenSaverTimeoutMin = timeoutCandidate;
      }

      // Port bywa null, jeĹ›li w JS wyszedĹ‚ NaN
      if (doc["cluster_port"].is<int>()) {
        clusterPort = doc["cluster_port"].as<int>();
      } else {
        clusterPort = DEFAULT_CLUSTER_PORT;
      }
      if (doc["pota_port"].is<int>()) {
        potaClusterPort = doc["pota_port"].as<int>();
      } else {
        potaClusterPort = DEFAULT_POTA_CLUSTER_PORT;
      }

      // Puste SSID jest dozwolone: urzadzenie zostaje w trybie AP (portal),
      // a pozostale ustawienia nadal mozna zapisac. Blokowanie calego zapisu
      // uniemozliwialo konfiguracje TFT/APRS/pogody bez podania WiFi.
      bool wifiConfigured = (wifiSSID.length() > 0 || wifiSSID2.length() > 0);

      // Aktualizuj wspĂłĹ‚rzÄ™dne z locatora TYLKO jeĹ›li nie podano bezpoĹ›rednio wspĂłĹ‚rzÄ™dnych
      // Priorytet: jeĂ„Ä…Ă˘â‚¬Ĺźli userLatLonValid == true (podano bezpoĂ„Ä…Ă˘â‚¬Ĺźrednio LAT/LON), nie nadpisuj z locatora
      if (!userLatLonValid && userLocator.length() >= 4) {
        updateUserLatLonFromLocator();
      }
      // Zapamietaj reczna pozycje z formularza i (re)uruchom UART GPS bez restartu.
      gpsPositionOverrideActive = false;
      gpsSnapshotManualPosition();
      gpsApplyConfig();
      savePreferences();
      applyEffectiveBacklight();
      if (tftInitialized) {
        applyTftInversion();
      }
      
            bool wifiChanged = (wifiSSID != prevSsid) || (wifiSSID2 != prevSsid2);
      // Kazdy zapis z WWW (TFT, APRS, pogoda, konfiguracja/WiFi) restartuje ESP.
      // Gwarantuje czysty stan wyswietlacza, dotyku i uslug po zmianie ustawien.
      (void)wifiChanged;
      (void)wifiConfigured;
      server->send(200, "application/json", "{\"status\":\"ok\",\"action\":\"restart\"}");
      Serial.println("Zapisano konfiguracje. Restart ESP...");
      requestRestart(1500);
    } else {
      server->send(400, "application/json", "{\"status\":\"error\"}");
    }
  });
  
  // API - resetuj kalibracjÄ™ dotyku do wartoĹ›ci domyĹ›lnych
  server->on("/api/reset_touch", HTTP_POST, []() {
    touchXMin = TOUCH_X_MIN;
    touchXMax = TOUCH_X_MAX;
    touchYMin = TOUCH_Y_MIN;
    touchYMax = TOUCH_Y_MAX;
    touchSwapXY = TOUCH_SWAP_XY;
    touchInvertX = TOUCH_INVERT_X;
    touchInvertY = TOUCH_INVERT_Y;
    touchRotation = 1;
    applyTouchRotation();
    
    savePreferences();
    
    Serial.println("Touch calibration reset to defaults");
    server->send(200, "application/json", "{\"status\":\"ok\"}");
  });

  // API - resetuj rotacjÄ™ TFT do wartoĹ›ci domyĹ›lnych
  server->on("/api/reset_tft_rotation", HTTP_POST, []() {
    tftRotation = 1;
    applyTftRotation();
    savePreferences();

    Serial.println("TFT rotation reset to default");
    server->send(200, "application/json", "{\"status\":\"ok\",\"action\":\"restart\"}");
    requestRestart(1500);
  });

  // API - usuĹ„ plik z LittleFS
  server->on("/api/delete_file", HTTP_GET, []() {
    if (!server->hasArg("path")) {
      server->send(400, "application/json", "{\"status\":\"error\",\"message\":\"missing_path\"}");
      return;
    }
    String path = server->arg("path");
    if (!path.startsWith("/")) {
      path = "/" + path;
    }
    if (!littleFsReady) {
      server->send(500, "application/json", "{\"status\":\"error\",\"message\":\"littlefs_not_ready\"}");
      return;
    }
    if (!LittleFS.exists(path)) {
      server->send(404, "application/json", "{\"status\":\"error\",\"message\":\"file_not_found\"}");
      return;
    }
    if (LittleFS.remove(path)) {
      server->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"file_deleted\"}");
      Serial.println("File deleted: " + path);
    } else {
      server->send(500, "application/json", "{\"status\":\"error\",\"message\":\"delete_failed\"}");
    }
  });

  // API - upload pliku do LittleFS
  server->on("/upload_form", HTTP_GET, []() {
    server->send(200, "text/html", "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Upload</title><style>body{font-family:Arial;background:#1a1a1a;color:#e0e0e0;padding:20px;}button{padding:12px 30px;background:#4a6a4a;border:none;color:#e0e0e0;cursor:pointer;border-radius:5px;font-size:16px;}</style></head><body><h2>Upload index.html</h2><form id='f'><input type='file' id='file' accept='.html'><br><br><button type='submit'>Wgraj</button></form><div id='s'></div><script>document.getElementById('f').addEventListener('submit',function(e){e.preventDefault();const file=document.getElementById('file').files[0];if(!file){alert('Wybierz plik');return;}const fd=new FormData();fd.append('index.html',file);fetch('/api/upload',{method:'POST',body:fd}).then(()=>{document.getElementById('s').innerHTML='<p style=\"color:green\">OK! Odswiez strone glowna (Ctrl+F5)</p>';}).catch(err=>{document.getElementById('s').innerHTML='<p style=\"color:red\">Blad: '+err+'</p>';});});</script></body></html>");
  });
  server->on("/api/upload", HTTP_POST, []() {
    server->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"upload_done\"}");
  }, []() {
    HTTPUpload& upload = server->upload();
    if (upload.status == UPLOAD_FILE_START) {
      String filename = upload.filename;
      if (!filename.startsWith("/")) {
        filename = "/" + filename;
      }
      if (!littleFsReady) {
        Serial.println("LittleFS not ready for upload");
        return;
      }
      Serial.printf("Uploading: %s\n", filename.c_str());
      if (LittleFS.exists(filename)) {
        LittleFS.remove(filename);
      }
      File f = LittleFS.open(filename, "w");
      if (!f) {
        Serial.println("Failed to open file for writing");
        return;
      }
      f.close();
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      String filename = upload.filename;
      if (!filename.startsWith("/")) {
        filename = "/" + filename;
      }
      File f = LittleFS.open(filename, "a");
      if (f) {
        f.write(upload.buf, upload.currentSize);
        f.close();
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      Serial.printf("Upload finished: %s, size: %d\n", upload.filename.c_str(), upload.totalSize);
    }
  });
  
  // API - pobierz konfiguracjÄ™
  server->on("/api/config", HTTP_GET, []() {
    DynamicJsonDocument doc(8192);  // + klucze GPS/status
    doc["wifi_ssid"] = wifiSSID;
    doc["wifi_pass"] = wifiPassword;
    doc["wifi_ssid2"] = wifiSSID2;
    doc["wifi_pass2"] = wifiPassword2;
    doc["cluster_host"] = clusterHost;
    doc["cluster_port"] = clusterPort;
    doc["pota_host"] = potaClusterHost;
    doc["pota_port"] = potaClusterPort;
    doc["pota_filter"] = potaFilterCommand;
    doc["pota_api_url"] = potaApiUrl;
    doc["hamalert_host"] = hamalertHost;
    doc["hamalert_port"] = hamalertPort;
    doc["hamalert_login"] = hamalertLogin;
    doc["hamalert_password"] = hamalertPassword;
    doc["user_callsign"] = userCallsign;
    doc["user_locator"] = userLocator;
    doc["timezone"] = timezoneHours;
    doc["user_lat"] = userLat;
    doc["user_lon"] = userLon;
    doc["user_lat_lon_valid"] = userLatLonValid;
    // GPS (NMEA po UART2) + automatyczny lokator Maidenhead
    doc["gps_enabled"] = gpsEnabled;
    doc["gps_use_for_position"] = gpsUseForPosition;
    doc["gps_baud"] = gpsBaud;
    doc["gps_rx_pin"] = gpsRxPin;
    doc["gps_tx_pin"] = gpsTxPin;
    doc["gps_has_position"] = gpsHasPosition;
    doc["gps_fix"] = gpsFix;
    doc["gps_module_detected"] = gpsModuleDetected();
    doc["gps_sats"] = gpsSats;
    doc["gps_hdop"] = gpsHdop;
    doc["gps_alt_m"] = gpsAltM;
    doc["gps_speed_kmh"] = gpsSpeedKmh;
    doc["gps_lat"] = gpsHasPosition ? gpsLat : 0.0;
    doc["gps_lon"] = gpsHasPosition ? gpsLon : 0.0;
    doc["gps_locator"] = gpsLocator;
    doc["gps_override_active"] = gpsPositionOverrideActive;
    doc["qrz_user"] = qrzUsername;
    doc["qrz_pass"] = qrzPassword;
    doc["weather_key"] = weatherApiKey;
    doc["n2yo_key"] = n2yoApiKey;
    doc["openwebrx_url"] = openWebRxUrl;
    doc["tft_backlight"] = backlightPercent;
    doc["tft_night_dimming"] = tftNightDimmingPercent;
    doc["tft_invert"] = sanitizeTftInvertSetting(tftInvertColors);
    doc["tft_rotation"] = tftRotation;
    doc["tft_lang"] = tftLangToCode(tftLanguage);
    doc["table_size"] = dxTableSizeToCode(dxTableSizeMode);
    doc["tft_auto_switch"] = tftAutoSwitchEnabled;
    doc["tft_switch_time_sec"] = tftAutoSwitchTimeSec;
    doc["menu_hue"] = menuThemeHue;
    doc["callsign_color"] = callsignColor;
    doc["clock_color"] = clockDigitsColor;
    doc["qrz_status"] = qrzStatus;
    doc["cluster_noann"] = clusterNoAnnouncements;
    doc["cluster_nowwv"] = clusterNoWWV;
    doc["cluster_nowcy"] = clusterNoWCY;
    doc["cluster_usefilters"] = clusterUseFilters;
    doc["cluster_filters"] = clusterFilterCommands;
    doc["aprs_host"] = aprsIsHost;
    doc["aprs_port"] = aprsIsPort;
    doc["aprs_callsign"] = aprsCallsign;
    doc["aprs_passcode"] = aprsPasscode;
    doc["aprs_ssid"] = aprsSsid;
    doc["aprs_radius"] = aprsFilterRadius;
    doc["aprs_beacon"] = aprsBeaconEnabled;
    doc["aprs_symbol"] = aprsSymbolTwoChar;
    doc["aprs_comment"] = aprsUserComment;
    doc["aprs_alert"] = aprsAlertCsv;
    doc["aprs_alert_enabled"] = aprsAlertEnabled;
    doc["aprs_alert_nearby_enabled"] = aprsAlertNearbyEnabled;
    doc["aprs_alert_wx_enabled"] = aprsAlertWxEnabled;
    doc["aprs_alert_min_sec"] = aprsAlertMinSeconds;
    doc["aprs_alert_screen_sec"] = aprsAlertScreenSeconds;
    doc["aprs_alert_distance_km"] = aprsAlertDistanceKm;
    doc["enable_led_alert"] = enableLedAlert;
    doc["led_alert_duration_ms"] = ledAlertDurationMs;
    doc["led_alert_blink_ms"] = ledAlertBlinkMs;
    doc["aprs_interval_min"] = aprsIntervalMinutes;
    
    // Ustawienia wygaszacza ekranu (Matrix)
    doc["screen_saver_enabled"] = screenSaverEnabled;
    doc["screen_saver_timeout_min"] = screenSaverTimeoutMin;
    
    // Touch calibration mode - priorytet: peĹ‚na kombinacja > rotacje > swap > both > single invert
    String touchMode = "none";
    if (touchSwapXY && touchInvertX && touchInvertY) {
      touchMode = "xy_both";
    } else if (touchSwapXY && touchInvertX) {
      touchMode = "rot90cw";
    } else if (touchSwapXY && touchInvertY) {
      touchMode = "rot90ccw";
    } else if (touchSwapXY) {
      touchMode = "xy";
    } else if (touchInvertX && touchInvertY) {
      touchMode = "both";
    } else if (touchInvertX) {
      touchMode = "x";
    } else if (touchInvertY) {
      touchMode = "y";
    }
    doc["touch_swap_mode"] = touchMode;
    doc["touch_rotation"] = touchRotation;
    // Indywidualne parametry kalibracji dotyku
    doc["touch_xmin"] = touchXMin;
    doc["touch_xmax"] = touchXMax;
    doc["touch_ymin"] = touchYMin;
    doc["touch_ymax"] = touchYMax;
    doc["touch_swap"] = touchSwapXY;
    doc["touch_invx"] = touchInvertX;
    doc["touch_invy"] = touchInvertY;

    // Informacje systemowe
    doc["fw_version"] = FIRMWARE_VERSION;
    doc["uptime"] = formatUptime(millis() - bootTimeMs);
    doc["sys_ip"] = wifiConnected ? WiFi.localIP().toString() : (WiFi.getMode() & WIFI_AP ? WiFi.softAPIP().toString() : "disconnected");
    doc["sys_ssid"] = wifiConnected ? WiFi.SSID() : "";
    doc["sys_rssi"] = wifiConnected ? WiFi.RSSI() : 0;
    doc["sys_mac"] = WiFi.macAddress();
    doc["sys_heap"] = ESP.getFreeHeap();
    doc["sys_heap_max"] = ESP.getMaxAllocHeap();
    doc["sys_chip"] = ESP.getChipModel();
    doc["sys_cpu"] = ESP.getCpuFreqMHz();
    doc["sys_temp"] = temperatureRead();  // temperatura w Â°C
    #ifdef ENABLE_BATTERY_MONITORING
    doc["sys_vcc"] = readBatteryVoltage();  // napiÄ™cie baterii
    #else
    doc["sys_vcc"] = 0;
    #endif
    // LittleFS info
    if (littleFsReady) {
      doc["sys_littlefs_total"] = LittleFS.totalBytes();
      doc["sys_littlefs_free"] = LittleFS.totalBytes() - LittleFS.usedBytes();
    } else {
      doc["sys_littlefs_total"] = 0;
      doc["sys_littlefs_free"] = 0;
    }

    // Ustawienia PSKReporter
    doc["psk_receiver"] = pskReceiverCallsign;
    doc["psk_callsign"] = pskReceiverCallsign;
    doc["psk_maxspots"] = pskMaxSpots;
    doc["psk_hours"] = pskHoursWindow;
    doc["psk_band"] = pskFilterBand;
    doc["psk_mode"] = pskFilterMode;
    doc["psk_url"] = pskCustomUrl;
    // HTTP Monitoring
    doc["psk_monitor_callsign"] = pskMonitorCallsign;
    doc["psk_report_days"] = pskReportDays;
    doc["psk_auto_refresh"] = pskAutoRefreshMinutes;
    // MQTT PSK Reporter
    doc["psk_mqtt_enabled"] = pskMqttEnabled;
    doc["psk_mqtt_server"] = pskMqttServer;
    doc["psk_mqtt_port"] = pskMqttPort;
    doc["psk_mqtt_callsign"] = pskMqttCallsign;

    JsonArray orderArr = doc.createNestedArray("screen_order");
    for (int i = 0; i < SCREEN_ORDER_COUNT; i++) {
      orderArr.add(screenTypeToCodeStr(screenOrder[i]));
    }
    
    
    String json;
    serializeJson(doc, json);
    server->send(200, "application/json", json);
  });

  // API - dane propagacji (SFI, K/A index, MUF, pasma HF)
  server->on("/api/propagation", HTTP_GET, []() {
    StaticJsonDocument<2048> doc;
    doc["valid"] = propagationData.valid;
    doc["sfi"] = propagationData.sfi;
    doc["kindex"] = propagationData.kindex;
    doc["aindex"] = propagationData.aindex;
    doc["muf"] = propagationData.muf;
    doc["updated"] = propagationData.updated;
    doc["lastError"] = propagationData.lastError;

    JsonArray bands = doc.createNestedArray("hfBands");
    for (int i = 0; i < 4; i++) {
      JsonObject band = bands.createNestedObject();
      band["label"] = propagationData.hfBandLabel[i];
      band["freq"] = propagationData.hfBandFreq[i];
      band["day"] = propagationData.hfBandDay[i];
      band["night"] = propagationData.hfBandNight[i];
    }

    String json;
    serializeJson(doc, json);
    server->send(200, "application/json", json);
  });

  // API - status portu UART2 / GPS / BT. Ten build nie obsluguje UART2 ani
  // modulu ESP32-BRIDGE, wiec zwracamy spojny stan "nieaktywny", aby pelne UI
  // dzialalo bez bledow JS.
  server->on("/api/uart_status", HTTP_GET, []() {
    // Ten build obsluguje tylko modul GPS na UART2 (BT/APRS-TNC/BRIDGE brak).
    bool detected = gpsModuleDetected();
    String mode = gpsEnabled ? "gps" : "off";
    String module = gpsEnabled ? "gps" : "";
    DynamicJsonDocument doc(512);
    doc["uart2_mode"] = mode;
    doc["uart2_connected_module"] = module;
    doc["gps_enabled"] = gpsEnabled;
    doc["gps_active"] = detected;
    doc["gps_fix"] = gpsFix;
    doc["gps_has_position"] = gpsHasPosition;
    doc["gps_sats"] = gpsSats;
    doc["gps_hdop"] = gpsHdop;
    doc["gps_alt_m"] = gpsAltM;
    doc["gps_speed_kmh"] = gpsSpeedKmh;
    doc["gps_locator"] = gpsLocator;
    doc["gps_override_active"] = gpsPositionOverrideActive;
    doc["bt_active"] = false;
    doc["aprs_uart_active"] = false;
    String response;
    serializeJson(doc, response);
    server->send(200, "application/json", response);
  });

  // API - biezacy status GPS (odpytywane przez zakladke "Moduly" w WWW).
  server->on("/api/gps", HTTP_GET, []() {
    DynamicJsonDocument doc(512);
    doc["enabled"] = gpsEnabled;
    doc["use_for_position"] = gpsUseForPosition;
    doc["module_detected"] = gpsModuleDetected();
    doc["fix"] = gpsFix;
    doc["has_position"] = gpsHasPosition;
    doc["lat"] = gpsHasPosition ? gpsLat : 0.0;
    doc["lon"] = gpsHasPosition ? gpsLon : 0.0;
    doc["locator"] = gpsLocator;
    doc["sats"] = gpsSats;
    doc["hdop"] = gpsHdop;
    doc["alt_m"] = gpsAltM;
    doc["speed_kmh"] = gpsSpeedKmh;
    doc["utc_time"] = gpsUtcTime;
    doc["date"] = gpsDateStr;
    doc["age_ms"] = gpsHasPosition ? (millis() - gpsLastFixMs) : 0;
    doc["override_active"] = gpsPositionOverrideActive;
    doc["baud"] = gpsBaud;
    doc["rx_pin"] = gpsRxPin;
    doc["tx_pin"] = gpsTxPin;
    String response;
    serializeJson(doc, response);
    server->send(200, "application/json", response);
  });

  server->on("/api/bridge_bt_status", HTTP_GET, []() {
    server->send(200, "application/json",
                 "{\"enabled\":false,\"scan_in_progress\":false,"
                 "\"list_collecting\":false,\"list_expected\":0,\"last_error\":\"\","
                 "\"last_ack\":\"\",\"disc_status\":\"\",\"last_scan_ms\":0,"
                 "\"pair_pending\":false,\"pair_result\":\"none\",\"pair_result_ms\":0,"
                 "\"paired_name\":\"\",\"paired_addr\":\"\",\"devices\":[]}");
  });

  server->on("/api/bridge_bt_scan", HTTP_POST, []() {
    server->send(400, "application/json",
                 "{\"status\":\"error\",\"message\":\"bridge_mode_required\"}");
  });

  server->on("/api/bridge_bt_pair", HTTP_POST, []() {
    server->send(400, "application/json",
                 "{\"status\":\"error\",\"message\":\"bridge_mode_required\"}");
  });

  
  // Upload nowej mapy BMP (np. przez curl -X POST -F "file=@Mapa swiata.bmp" http://esp-ip/upload/bmp)
  // Obsluguje multipart ORAZ surowe cialo (RAW) - patrz handleUpload().
  server->on("/upload/bmp", HTTP_POST, []() {
    server->send(200, "text/plain", "Upload complete");
  }, []() {
    handleUpload("mapa");
  });

  // Upload tla mapy (BMP 16-bit RGB565) wygenerowanego w przegladarce WWW.
  // Zapisuje pod ta sama sciezka co /upload/bmp (PSK_MAP_BMP_PATH).
  server->on("/upload/mapa565", HTTP_POST, []() {
    server->send(200, "text/plain", "Upload complete");
  }, []() {
    handleUpload("mapa");
  });
  
  // Znajduje aktualny plik ekranu startowego (dowolne obslugiwane rozszerzenie).
  // (definiowane jako lambda, uzywana przez /splash_preview ponizej)

  // Upload ekranu startowego (splash) - zachowany dla zakladki Linki.
  server->on("/upload_splash", HTTP_POST, []() {
    String accept = server->header("Accept");
    if (accept.indexOf("text/html") >= 0) {
      server->sendHeader("Location", "/");
      server->send(303, "text/plain", "uploaded");
    } else {
      server->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"uploaded\"}");
    }
  }, []() {
    handleUpload("splash");
  });

  // Upload ekranu powitalnego (zastepuje ekran z wersja/autorami).
  server->on("/upload_welcome", HTTP_POST, []() {
    String accept = server->header("Accept");
    if (accept.indexOf("text/html") >= 0) {
      server->sendHeader("Location", "/");
      server->send(303, "text/plain", "uploaded");
    } else {
      server->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"uploaded\"}");
    }
  }, []() {
    handleUpload("welcome");
  });

  // Podglad wgranej grafiki ekranu powitalnego (dla WWW - zakladka TFT).
  server->on("/welcome_preview", HTTP_GET, []() {
    static const char *wcands[] = {
      "/welcome.png", "/welcome.jpg", "/welcome.jpeg", "/welcome.bmp"};
    for (const char *c : wcands) {
      if (LittleFS.exists(c)) {
        String lc = String(c);
        lc.toLowerCase();
        const char *ct = lc.endsWith(".png") ? "image/png"
                       : lc.endsWith(".bmp") ? "image/bmp" : "image/jpeg";
        if (streamLittleFsFile(c, ct)) return;
        break;
      }
    }
    server->send(404, "text/plain", "no welcome image");
  });

  // Podglad aktualnego ekranu startowego (dla WWW - zakladka Linki).
  server->on("/splash_preview", HTTP_GET, []() {
    static const char *cands[] = {
      "/splash.png", "/splash.jpg", "/splash.jpeg", "/splash.bmp",
      "/splash_16bit.bmp", "/splash16.bmp", "/icon50/splash.bmp",
      "/boot.bmp", "/boot.png", "/logo.bmp", "/logo.png",
      "/start.bmp", "/start.png"};
    for (const char *c : cands) {
      if (LittleFS.exists(c)) {
        String lc = String(c);
        lc.toLowerCase();
        const char *ct = lc.endsWith(".png") ? "image/png"
                       : lc.endsWith(".bmp") ? "image/bmp" : "image/jpeg";
        if (streamLittleFsFile(c, ct)) return;
        break;
      }
    }
    server->send(404, "text/plain", "no splash");
  });

  // Prosty formularz do uploadu mapy
  server->on("/upload", HTTP_GET, []() {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'></head><body>";
    html += "<h1>Wgraj nowÄ… mapÄ™ BMP</h1>";
    html += "<form method='POST' action='/upload/bmp' enctype='multipart/form-data'>";
    html += "<input type='file' name='file' accept='.bmp'><br><br>";
    html += "<input type='submit' value='Wgraj mapÄ™'>";
    html += "</form>";
    html += "<p>Rozmiar: 480x320 px</p>";
    html += "<p><a href='/'>â† PowrĂłt</a></p>";
    html += "</body></html>";
    server->send(200, "text/html; charset=utf-8", html);
  });
  
  // Prawdziwa mapa swiata (BMP 480x320) - tlo dla sledzenia satelitow w WWW
  server->on("/mapa_swiat.bmp", HTTP_GET, []() {
    if (!streamLittleFsFile("/Mapa swiata.bmp", "image/bmp")) {
      server->send(404, "text/plain", "not found");
    }
  });

  // Ikony pogodowe (/icon50/*.bmp) i flagi krajow (/flags/*.png) z LittleFS
  server->onNotFound([]() {
    String path = server->uri();
    if (path.indexOf("..") >= 0) {
      server->send(404, "text/plain", "not found");
      return;
    }
    if (path.startsWith("/icon50/") && path.endsWith(".bmp")) {
      if (streamLittleFsFile(path.c_str(), "image/bmp")) return;
    } else if (path.startsWith("/flags/") && path.endsWith(".png")) {
      if (streamLittleFsFile(path.c_str(), "image/png")) return;
    } else if (path.startsWith("/tft_flags/") && path.endsWith(".bmp")) {
      if (streamLittleFsFile(path.c_str(), "image/bmp")) return;
    }
    server->send(404, "text/plain", "not found");
  });

  server->begin();
  Serial.println("Serwer WWW uruchomiony");
}

// ========== HTML PAGES ==========

String getMainHTML() {
  return R"rawliteral(
<!DOCTYPE html>
<html lang="pl">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 HAM CLOCK</title>
  <style>
    :root{--bg:#0f172a;--panel:#111827;--panel2:#1f2937;--text:#e5e7eb;--muted:#94a3b8;--accent:#f59e0b;--good:#22c55e;--warn:#ef4444;--border:#334155;--blue:#3b82f6}
    *{box-sizing:border-box;margin:0;padding:0}
    body{font-family:Arial,sans-serif;background:linear-gradient(180deg,#020617,var(--bg));color:var(--text);min-height:100vh}
    .wrap{max-width:1100px;margin:0 auto;padding:16px}
    h1{font-size:22px;margin:0}
    h2{font-size:17px;margin:0 0 10px 0;color:var(--accent)}
    h3{font-size:14px;margin:0 0 8px 0;color:var(--muted);text-transform:uppercase;letter-spacing:.06em}
    p{color:var(--muted);margin:0;font-size:13px}
    .hero{background:rgba(17,24,39,.92);border:1px solid var(--border);border-radius:14px;padding:16px;margin-bottom:12px;display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:10px}
    .hero-left{display:flex;align-items:center;gap:12px}
    .hero-right{display:flex;gap:8px;flex-wrap:wrap}
    .panel{background:rgba(17,24,39,.92);border:1px solid var(--border);border-radius:14px;padding:16px;margin-bottom:12px}
    .row{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:10px;margin-top:10px}
    .row3{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:10px;margin-top:10px}
    .card{background:var(--panel2);border:1px solid var(--border);border-radius:10px;padding:12px}
    .label{font-size:11px;text-transform:uppercase;letter-spacing:.06em;color:var(--muted);margin-bottom:4px}
    .value{font-size:18px;font-weight:700;word-break:break-word}
    .value.sm{font-size:14px}
    .ok{color:var(--good);font-weight:700}
    .warn{color:var(--warn);font-weight:700}
    .toolbar{display:flex;gap:8px;flex-wrap:wrap;margin-top:12px}
    button,a.btn{background:var(--accent);color:#111827;border:none;border-radius:8px;padding:8px 14px;font-weight:700;cursor:pointer;text-decoration:none;font-size:13px;display:inline-flex;align-items:center;gap:4px}
    button.sec{background:transparent;color:var(--text);border:1px solid var(--border)}
    button.sm{padding:5px 10px;font-size:12px}
    button.danger{background:var(--warn);color:#fff}
    input,select{width:100%;background:#020617;color:var(--text);border:1px solid var(--border);border-radius:8px;padding:8px 10px;font-size:13px}
    input:focus,select:focus{outline:none;border-color:var(--accent)}
    .field{display:flex;flex-direction:column;gap:4px}
    .field label{color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:.06em}
    .check{display:flex;align-items:center;gap:8px;padding:8px 10px;background:var(--panel2);border:1px solid var(--border);border-radius:8px;font-size:13px}
    .check input{width:auto;transform:scale(1.2)}
    .section{margin-top:16px;padding-top:12px;border-top:1px solid var(--border)}
    .hint{margin-top:8px;font-size:12px;color:var(--muted)}
    textarea{width:100%;min-height:200px;background:#020617;color:var(--text);border:1px solid var(--border);border-radius:10px;padding:10px;font-family:Consolas,monospace;font-size:12px;resize:vertical}
    table{width:100%;border-collapse:collapse;margin-top:8px}
    th,td{padding:7px 6px;border-bottom:1px solid var(--border);text-align:left;font-size:13px}
    th{color:var(--muted);font-weight:600;font-size:12px}
    .tabs{display:flex;gap:4px;flex-wrap:wrap;margin-bottom:12px;background:var(--panel);border:1px solid var(--border);border-radius:10px;padding:6px}
    .tab{background:transparent;color:var(--muted);border:none;border-radius:6px;padding:7px 12px;font-size:12px;font-weight:600;cursor:pointer;white-space:nowrap}
    .tab.active{background:var(--accent);color:#111827}
    .tab:hover:not(.active){background:var(--panel2);color:var(--text)}
    .tabcontent{display:none}
    .tabcontent.active{display:block}
    .status-dot{width:8px;height:8px;border-radius:50%;display:inline-block;margin-right:4px}
    .status-dot.on{background:var(--good)}
    .status-dot.off{background:var(--warn)}
    @media(max-width:640px){
      .wrap{padding:10px}
      .hero{flex-direction:column;align-items:flex-start}
      .value{font-size:15px}
      th,td{font-size:12px;padding:5px 4px}
      .tabs{gap:2px;padding:4px}
      .tab{padding:6px 8px;font-size:11px}
      .row{grid-template-columns:1fr}
    }
  </style>
</head>
<body>
<div class="wrap">

  <div class="hero">
    <div class="hero-left">
      <h1>ESP32 HAM CLOCK</h1>
      <p id="fwVer"></p>
    </div>
    <div class="hero-right">
      <span id="connStatus"><span class="status-dot off"></span>...</span>
      <button class="sm" onclick="loadAll()">Odswiez</button>
      <button class="sm sec" onclick="saveFromActiveTab()">Zapisz</button>
    </div>
  </div>

  <div class="tabs" id="tabBar">
    <button class="tab active" onclick="showTab('dashboard')">Pulpit</button>
    <button class="tab" onclick="showTab('station')">Stacja</button>
    <button class="tab" onclick="showTab('services')">Uslugi</button>
    <button class="tab" onclick="showTab('aprs')">APRS-IS</button>
    <button class="tab" onclick="showTab('psk')">PSK Reporter</button>
    <button class="tab" onclick="showTab('display')">Wyswietlacz</button>
    <button class="tab" onclick="showTab('touch')">Dotyk</button>
    <button class="tab" onclick="showTab('system')">System</button>
  </div>

  <!-- ========== TAB: PULPIT ========== -->
  <div class="tabcontent active" id="tab-dashboard">
    <div class="panel">
      <h2>Status</h2>
      <div class="row">
        <div class="card"><div class="label">WiFi</div><div class="value" id="d_ssid">-</div></div>
        <div class="card"><div class="label">Callsign</div><div class="value" id="d_call">-</div></div>
        <div class="card"><div class="label">Uptime</div><div class="value" id="d_uptime">-</div></div>
        <div class="card"><div class="label">IP / RSSI</div><div class="value sm" id="d_ip">-</div></div>
        <div class="card"><div class="label">Cluster</div><div class="value sm" id="d_cluster">-</div></div>
        <div class="card"><div class="label">APRS</div><div class="value sm" id="d_aprs">-</div></div>
      </div>
    </div>
    <div class="panel">
      <h2>DX Spots</h2>
      <div id="d_spotsState" class="hint">Ladowanie...</div>
      <div style="overflow-x:auto">
      <table><thead><tr><th>Czas</th><th>Call</th><th>Freq</th><th>Mode</th><th>Kraj</th><th>Band</th><th>Spotter</th></tr></thead>
      <tbody id="d_spotsBody"></tbody></table>
      </div>
    </div>
    <div class="panel">
      <h2>APRS Stacje</h2>
      <div id="d_aprsState" class="hint">Ladowanie...</div>
      <div style="overflow-x:auto">
      <table><thead><tr><th>Czas</th><th>Call</th><th>Symbol</th><th>Odleglosc</th><th>Komentarz</th></tr></thead>
      <tbody id="d_aprsBody"></tbody></table>
      </div>
    </div>
    <div class="panel">
      <h2>POTA Spoty</h2>
      <div id="d_potaState" class="hint">Ladowanie...</div>
      <div style="overflow-x:auto">
      <table><thead><tr><th>Czas</th><th>Call</th><th>Freq</th><th>Mode</th><th>Spotter</th></tr></thead>
      <tbody id="d_potaBody"></tbody></table>
      </div>
    </div>
    <div class="panel">
      <h2>HamAlert</h2>
      <div id="d_hamState" class="hint">Ladowanie...</div>
      <div style="overflow-x:auto">
      <table><thead><tr><th>Czas</th><th>Call</th><th>Freq</th><th>Mode</th><th>Kraj</th></tr></thead>
      <tbody id="d_hamBody"></tbody></table>
      </div>
    </div>
    <div class="panel">
      <h2>ISS</h2>
      <div class="row">
        <div class="card"><div class="label">Pozycja</div><div class="value sm" id="d_issPos">-</div></div>
        <div class="card"><div class="label">Alt / Predkosc</div><div class="value sm" id="d_issAlt">-</div></div>
        <div class="card"><div class="label">Az / El / Dystans</div><div class="value sm" id="d_issAzi">-</div></div>
        <div class="card"><div class="label">Kraj</div><div class="value sm" id="d_issCountry">-</div></div>
      </div>
    </div>
    <div class="panel">
      <h2>PSK Reporter</h2>
      <div id="d_pskState" class="hint">Ladowanie...</div>
      <div style="overflow-x:auto">
      <table><thead><tr><th>Call</th><th>Band</th><th>Mode</th><th>SNR</th><th>Lat</th><th>Lon</th></tr></thead>
      <tbody id="d_pskBody"></tbody></table>
      </div>
    </div>
  </div>

  <!-- ========== TAB: STACJA ========== -->
  <div class="tabcontent" id="tab-station">
    <div class="panel">
      <h2>WiFi</h2>
      <div class="row">
        <div class="field"><label>SSID 1</label><input id="wifi_ssid" type="text"></div>
        <div class="field"><label>Haslo 1</label><input id="wifi_pass" type="password"></div>
        <div class="field"><label>SSID 2</label><input id="wifi_ssid2" type="text"></div>
        <div class="field"><label>Haslo 2</label><input id="wifi_pass2" type="password"></div>
      </div>
    </div>
    <div class="panel">
      <h2>Stacja</h2>
      <div class="row">
        <div class="field"><label>Callsign</label><input id="user_callsign" type="text"></div>
        <div class="field"><label>Locator (np. KN09)</label><input id="user_locator" type="text"></div>
        <div class="field"><label>Szerokosc geo (lat)</label><input id="user_lat" type="number" step="0.0001" min="-90" max="90"></div>
        <div class="field"><label>Dlugosc geo (lon)</label><input id="user_lon" type="number" step="0.0001" min="-180" max="180"></div>
        <div class="field"><label>Strefa czasowa (UTC offset)</label><input id="timezone" type="number" min="-12" max="14" step="1"></div>
        <div class="field"><label>OpenWebRX URL</label><input id="openwebrx_url" type="text" placeholder="http://..."></div>
      </div>
    </div>
    <div class="panel">
      <h2>Klucze API</h2>
      <div class="row">
        <div class="field"><label>OpenWeather API Key</label><input id="weather_key" type="password" placeholder="openweathermap.org"></div>
        <div class="field"><label>n2yo.com API Key (ISS)</label><input id="n2yo_key" type="password" placeholder="n2yo.com/api"></div>
      </div>
    </div>
    <div class="panel">
      <h2>QRZ.com</h2>
      <div class="row">
        <div class="field"><label>QRZ Username</label><input id="qrz_user" type="text"></div>
        <div class="field"><label>QRZ Password</label><input id="qrz_pass" type="password"></div>
      </div>
      <div class="hint" id="qrzStatus"></div>
    </div>
  </div>

  <!-- ========== TAB: USLUGI ========== -->
  <div class="tabcontent" id="tab-services">
    <div class="panel">
      <h2>DX Cluster</h2>
      <div class="row">
        <div class="field"><label>Host</label><input id="cluster_host" type="text"></div>
        <div class="field"><label>Port</label><input id="cluster_port" type="number"></div>
      </div>
      <div class="section">
        <div class="row">
          <div class="field"><label>Filtry (max 3)</label><input id="cluster_filters" type="text" placeholder="f/spotter/..."></div>
        </div>
        <div class="row" style="margin-top:8px">
          <label class="check"><input id="cluster_usefilters" type="checkbox"> Uzywaj filtrow</label>
          <label class="check"><input id="cluster_noann" type="checkbox"> Ukryj ANN</label>
          <label class="check"><input id="cluster_nowwv" type="checkbox"> Ukryj WWV</label>
          <label class="check"><input id="cluster_nowcy" type="checkbox"> Ukryj WCY</label>
        </div>
      </div>
    </div>
    <div class="panel">
      <h2>POTA</h2>
      <div class="row">
        <div class="field"><label>Cluster Host</label><input id="pota_host" type="text"></div>
        <div class="field"><label>Cluster Port</label><input id="pota_port" type="number"></div>
        <div class="field"><label>Filter Command</label><input id="pota_filter" type="text" placeholder="f/dxctest"></div>
        <div class="field"><label>API URL</label><input id="pota_api_url" type="text"></div>
      </div>
    </div>
    <div class="panel">
      <h2>HamAlert</h2>
      <div class="row">
        <div class="field"><label>Host</label><input id="hamalert_host" type="text"></div>
        <div class="field"><label>Port</label><input id="hamalert_port" type="number"></div>
        <div class="field"><label>Login</label><input id="hamalert_login" type="text"></div>
        <div class="field"><label>Haslo</label><input id="hamalert_password" type="password"></div>
      </div>
    </div>
  </div>

  <!-- ========== TAB: APRS-IS ========== -->
  <div class="tabcontent" id="tab-aprs">
    <div class="panel">
      <h2>Polaczenie APRS-IS</h2>
      <div class="row">
        <div class="field"><label>Host</label><input id="aprs_host" type="text"></div>
        <div class="field"><label>Port</label><input id="aprs_port" type="number"></div>
        <div class="field"><label>Callsign</label><input id="aprs_callsign" type="text"></div>
        <div class="field"><label>Passcode</label><input id="aprs_passcode" type="number"></div>
        <div class="field"><label>SSID (0-15)</label><input id="aprs_ssid" type="number" min="0" max="15"></div>
        <div class="field"><label>Promien (km, 1-50)</label><input id="aprs_radius" type="number" min="1" max="50"></div>
        <div class="field"><label>Symbol (np. /-)</label><input id="aprs_symbol" type="text" maxlength="2"></div>
        <div class="field"><label>Komentarz</label><input id="aprs_comment" type="text"></div>
        <div class="field"><label>Interwal beacona (min)</label><input id="aprs_interval_min" type="number" min="1" max="60"></div>
      </div>
      <div class="section">
        <label class="check"><input id="aprs_beacon" type="checkbox"> Beacon APRS wlaczony</label>
      </div>
    </div>
    <div class="panel">
      <h2>Alerty APRS</h2>
      <div class="row">
        <div class="field"><label>Lista alertow (CSV: call1,call2,...)</label><input id="aprs_alert" type="text" placeholder="SP9ABC,SP2XYZ"></div>
      </div>
      <div class="row" style="margin-top:8px">
        <label class="check"><input id="aprs_alert_enabled" type="checkbox"> Alerty wlaczone</label>
        <label class="check"><input id="aprs_alert_nearby_enabled" type="checkbox"> Alert nearby</label>
        <label class="check"><input id="aprs_alert_wx_enabled" type="checkbox"> Alert WX (pogoda)</label>
      </div>
      <div class="row" style="margin-top:8px">
        <div class="field"><label>Min. odstep czasowy (sek)</label><input id="aprs_alert_min_sec" type="number" min="0" max="3600"></div>
        <div class="field"><label>Czas wyswietlania ekranu (sek)</label><input id="aprs_alert_screen_sec" type="number" min="3" max="300"></div>
        <div class="field"><label>Max dystans alertu (km)</label><input id="aprs_alert_distance_km" type="number" min="1" max="5000"></div>
      </div>
    </div>
    <div class="panel">
      <h2>LED Alert</h2>
      <div class="row">
        <label class="check"><input id="enable_led_alert" type="checkbox"> LED alert wlaczony</label>
      </div>
      <div class="row" style="margin-top:8px">
        <div class="field"><label>Czas trwania (ms)</label><input id="led_alert_duration_ms" type="number" min="500" max="60000"></div>
        <div class="field"><label>Interwalk migniec (ms)</label><input id="led_alert_blink_ms" type="number" min="50" max="5000"></div>
      </div>
    </div>
  </div>

  <!-- ========== TAB: PSK REPORTER ========== -->
  <div class="tabcontent" id="tab-psk">
    <div class="panel">
      <h2>PSK Reporter - Polaczenie</h2>
      <div class="row">
        <div class="field"><label>Callsign do monitorowania</label><input id="psk_receiver" type="text" placeholder="SP9ABC"></div>
        <div class="field"><label>Max spotow</label><input id="psk_maxspots" type="number" min="10" max="100"></div>
        <div class="field"><label>Okno czasowe (h)</label><input id="psk_hours" type="number" min="1" max="24"></div>
        <div class="field"><label>Filtr pasma</label><input id="psk_band" type="text" placeholder="np. 20m"></div>
        <div class="field"><label>Filtr mode</label><input id="psk_mode" type="text" placeholder="np. FT8"></div>
        <div class="field"><label>Wlasny URL API</label><input id="psk_url" type="text"></div>
      </div>
    </div>
    <div class="panel">
      <h2>PSK Reporter - Twoj Locator</h2>
      <div class="row">
        <div class="field"><label>Locator (Maidenhead, np. JO82LK)</label><input id="psk_user_locator" type="text" placeholder="np. JO82LK"></div>
      </div>
      <p class="hint">Ten sam locator co w zakladce <b>Stacja</b>. Wyznacza Twoja pozycje na mapie PSK
      (czerwona kropka) i - w trybie MQTT - ktore spoty sa pobierane (kwadrat lokatora odbiorcy).</p>
    </div>
    <div class="panel">
      <h2>PSK Reporter - Monitoring HTTP</h2>
      <div class="row">
        <div class="field"><label>Callsign (raportowanie)</label><input id="psk_monitor_callsign" type="text"></div>
        <div class="field"><label>Dni raportowania (0-62)</label><input id="psk_report_days" type="number" min="0" max="62"></div>
        <div class="field"><label>Auto odswiezanie (min, 0=off)</label><input id="psk_auto_refresh" type="number" min="0" max="60"></div>
      </div>
    </div>
    <div class="panel">
      <h2>PSK Reporter - MQTT</h2>
      <label class="check"><input id="psk_mqtt_enabled" type="checkbox"> Uzyj MQTT zamiast HTTP</label>
      <div class="row" id="mqtt_fields" style="margin-top:8px;display:none">
        <div class="field"><label>MQTT Server</label><input id="psk_mqtt_server" type="text" placeholder="mqtt.pskreporter.info"></div>
        <div class="field"><label>MQTT Port</label><input id="psk_mqtt_port" type="number" min="1" max="65535"></div>
        <div class="field"><label>MQTT Callsign (filter)</label><input id="psk_mqtt_callsign" type="text" placeholder="SP9ABC"></div>
      </div>
    </div>
  </div>

  <!-- ========== TAB: WYSWIETLACZ ========== -->
  <div class="tabcontent" id="tab-display">
    <div class="panel">
      <h2>Ustawienia TFT</h2>
      <div class="row">
        <div class="field"><label>Podswietlenie (%)</label><input id="tft_backlight" type="number" min="10" max="100"></div>
        <div class="field"><label>Rotacja TFT</label>
          <select id="tft_rotation"><option value="0">0</option><option value="1">1</option><option value="2">2</option><option value="3">3</option></select>
        </div>
        <div class="field"><label>Jezyk</label>
          <select id="tft_lang"><option value="pl">Polski</option><option value="en">English</option></select>
        </div>
        <div class="field"><label>Rozmiar tabeli</label>
          <select id="table_size"><option value="normal">Normalny</option><option value="enlarged">Powkszony</option></select>
        </div>
        <div class="field"><label>Odcien menu (0-255)</label><input id="menu_hue" type="number" min="0" max="255"></div>
        <div class="field"><label>Kolor callsign (HEX lub int)</label><input id="callsign_color" type="number" min="0" max="65535"></div>
        <div class="field"><label>Kolor zegara (HEX lub int)</label><input id="clock_color" type="number" min="0" max="65535"></div>
      </div>
      <div class="row" style="margin-top:8px">
        <label class="check"><input id="tft_invert" type="checkbox"> Odwrocone kolory TFT</label>
      </div>
    </div>
    <div class="panel">
      <h2>Automatyczne przelaczanie ekranow</h2>
      <div class="row">
        <label class="check"><input id="tft_auto_switch" type="checkbox"> Auto switch wlaczony</label>
        <div class="field"><label>Czas na ekranie (sek)</label><input id="tft_switch_time_sec" type="number" min="5" max="300"></div>
      </div>
    </div>
    <div class="panel">
      <h2>Wygaszacz ekranu (Matrix)</h2>
      <div class="row">
        <label class="check"><input id="screen_saver_enabled" type="checkbox"> Wygaszacz wlaczony</label>
        <div class="field"><label>Timeout (min, 1-60)</label><input id="screen_saver_timeout_min" type="number" min="1" max="60"></div>
      </div>
    </div>
    <div class="panel">
      <h2>Kolejnost ekranow (14 pozycji)</h2>
      <p>Wybierz ekran dla kazdej pozycji. "Wylaczony" = pusta pozycja.</p>
      <div class="row3" id="screenOrderRow"></div>
      <div class="hint" id="screenOrderHint"></div>
    </div>
  </div>

  <!-- ========== TAB: DOTYK ========== -->
  <div class="tabcontent" id="tab-touch">
    <div class="panel">
      <h2>Kalibracja dotyku</h2>
      <div class="row">
        <div class="field"><label>Tryb obrotu</label>
          <select id="touch_swap_mode">
            <option value="none">Brak</option>
            <option value="x">Invert X</option>
            <option value="y">Invert Y</option>
            <option value="both">Invert X+Y</option>
            <option value="xy">Swap XY</option>
            <option value="rot90cw">Obrut 90 CW</option>
            <option value="rot90ccw">Obrut 90 CCW</option>
            <option value="xy_both">Swap XY + Invert Both</option>
          </select>
        </div>
        <div class="field"><label>Rotacja dotyku (0-3)</label>
          <select id="touch_rotation"><option value="0">0</option><option value="1">1</option><option value="2">2</option><option value="3">3</option></select>
        </div>
      </div>
      <div class="section">
        <h3>Kalibracja surowa (0-4095)</h3>
        <div class="row">
          <div class="field"><label>X Min</label><input id="touch_xmin" type="number" min="0" max="4095"></div>
          <div class="field"><label>X Max</label><input id="touch_xmax" type="number" min="0" max="4095"></div>
          <div class="field"><label>Y Min</label><input id="touch_ymin" type="number" min="0" max="4095"></div>
          <div class="field"><label>Y Max</label><input id="touch_ymax" type="number" min="0" max="4095"></div>
        </div>
      </div>
      <div class="section">
        <h3>Flagi reczne</h3>
        <div class="row">
          <label class="check"><input id="touch_swap" type="checkbox"> Swap XY</label>
          <label class="check"><input id="touch_invx" type="checkbox"> Invert X</label>
          <label class="check"><input id="touch_invy" type="checkbox"> Invert Y</label>
        </div>
      </div>
      <div class="toolbar">
        <button class="sec" onclick="resetTouch()">Reset do domyslnych</button>
      </div>
    </div>
  </div>

  <!-- ========== TAB: SYSTEM ========== -->
  <div class="tabcontent" id="tab-system">
    <div class="panel">
      <h2>Informacje systemowe</h2>
      <div class="row">
        <div class="card"><div class="label">Firmware</div><div class="value sm" id="s_fw">-</div></div>
        <div class="card"><div class="label">Uptime</div><div class="value sm" id="s_uptime">-</div></div>
        <div class="card"><div class="label">IP</div><div class="value sm" id="s_ip">-</div></div>
        <div class="card"><div class="label">RSSI</div><div class="value sm" id="s_rssi">-</div></div>
        <div class="card"><div class="label">MAC</div><div class="value sm" id="s_mac">-</div></div>
        <div class="card"><div class="label">Chip</div><div class="value sm" id="s_chip">-</div></div>
        <div class="card"><div class="label">CPU MHz</div><div class="value sm" id="s_cpu">-</div></div>
        <div class="card"><div class="label">Temp CPU</div><div class="value sm" id="s_temp">-</div></div>
        <div class="card"><div class="label">Wolna pamiec</div><div class="value sm" id="s_heap">-</div></div>
        <div class="card"><div class="label">Max alokacja</div><div class="value sm" id="s_heapmax">-</div></div>
        <div class="card"><div class="label">Bateria</div><div class="value sm" id="s_vcc">-</div></div>
        <div class="card"><div class="label">LittleFS</div><div class="value sm" id="s_littlefs">-</div></div>
      </div>
      <div class="hint" style="margin-top:12px">
        Autor oprogramowania: Tomasz SP9TNV | na podstawie projektu Konrad WiĹ›niewski SP3KON | Licencja: MIT
      </div>
    </div>
    <div class="panel">
      <h2>Wgraj mape BMP</h2>
      <p>Rozmiar: 480x320 px</p>
      <form id="bmpForm" enctype="multipart/form-data" style="margin-top:8px">
        <input type="file" name="file" accept=".bmp" id="bmpFile">
        <button type="submit" class="sm" style="margin-top:8px">Wgraj mape</button>
      </form>
      <div id="bmpStatus" class="hint"></div>
    </div>
    <div class="panel">
      <h2>Zaawansowany JSON</h2>
      <p>Reczna edycja pelnej konfiguracji.</p>
      <div class="toolbar">
        <button class="sm sec" onclick="syncJsonFromForm()">Formularz -> JSON</button>
        <button class="sm" onclick="saveJsonEditor()">Zapisz JSON</button>
      </div>
      <textarea id="configEditor" spellcheck="false" style="margin-top:8px"></textarea>
      <div id="saveState" class="hint"></div>
    </div>
  </div>

</div>

<script>
let C = {};
const SCREENS = [
  ['sunspots','Propagacja'],['bandinfo','Solar Index'],['hamclock','Ham Clock'],
  ['dxcluster','DX Cluster'],['aprsis','APRS-IS'],['aprsradar','APRS Radar'],
  ['weather','Pogoda'],['weatherforecast','Prognoza'],['pota','POTA'],
  ['hamalert','HamAlert'],['pskmap','PSK Map'],['systeminfo','Informacje systemowe'],
  ['matrix','Matrix'],['iss','ISS'],['off','Wylaczony']
];
const $=id=>document.getElementById(id);
const val=(id,v)=>{ $(id).value=v??''; };
const chk=(id,v)=>{ $(id).checked=!!v; };
const gint=(id,d)=>{const v=parseInt($(id).value,10);return isNaN(v)?d:v;};
const gstr=(id,d='')=>$(id).value??d;

function showTab(name){
  document.querySelectorAll('.tabcontent').forEach(t=>t.classList.remove('active'));
  document.querySelectorAll('.tab').forEach(t=>t.classList.remove('active'));
  const el=$('tab-'+name); if(el) el.classList.add('active');
  document.querySelectorAll('.tab').forEach(t=>{ if(t.textContent.toLowerCase().includes(getTabKeyword(name))) t.classList.add('active'); });
}
function getTabKeyword(n){return{dashboard:'pulpit',station:'stacja',services:'uslugi',aprs:'aprs',psk:'psk',display:'wyswietlacz',touch:'dotyk',system:'system'}[n]||'';}

function buildScreenOrderSelects(){
  const c=$('screenOrderRow'); c.innerHTML='';
  for(let i=0;i<14;i++){
    const d=document.createElement('div'); d.className='field';
    let opts=''; SCREENS.forEach(([v,l])=>{ opts+='<option value="'+v+'">'+l+'</option>'; });
    d.innerHTML='<label>Poz. '+(i+1)+'</label><select id="screen_'+i+'">'+opts+'</select>';
    c.appendChild(d);
  }
}

function fillForm(cfg){
  // Station
  val('wifi_ssid',cfg.wifi_ssid); val('wifi_pass',cfg.wifi_pass);
  val('wifi_ssid2',cfg.wifi_ssid2); val('wifi_pass2',cfg.wifi_pass2);
  val('user_callsign',cfg.user_callsign); val('user_locator',cfg.user_locator);
  val('user_lat',cfg.user_lat); val('user_lon',cfg.user_lon);
  val('timezone',cfg.timezone); val('openwebrx_url',cfg.openwebrx_url);
  val('weather_key',cfg.weather_key); val('n2yo_key',cfg.n2yo_key);
  val('qrz_user',cfg.qrz_user); val('qrz_pass',cfg.qrz_pass);
  if($('qrzStatus')) $('qrzStatus').textContent=cfg.qrz_status?('Status: '+cfg.qrz_status):'';
  // DX Cluster
  val('cluster_host',cfg.cluster_host); val('cluster_port',cfg.cluster_port);
  val('cluster_filters',cfg.cluster_filters);
  chk('cluster_usefilters',cfg.cluster_usefilters); chk('cluster_noann',cfg.cluster_noann);
  chk('cluster_nowwv',cfg.cluster_nowwv); chk('cluster_nowcy',cfg.cluster_nowcy);
  // POTA
  val('pota_host',cfg.pota_host); val('pota_port',cfg.pota_port);
  val('pota_filter',cfg.pota_filter); val('pota_api_url',cfg.pota_api_url);
  // HamAlert
  val('hamalert_host',cfg.hamalert_host); val('hamalert_port',cfg.hamalert_port);
  val('hamalert_login',cfg.hamalert_login); val('hamalert_password',cfg.hamalert_password);
  // APRS
  val('aprs_host',cfg.aprs_host); val('aprs_port',cfg.aprs_port);
  val('aprs_callsign',cfg.aprs_callsign); val('aprs_passcode',cfg.aprs_passcode);
  val('aprs_ssid',cfg.aprs_ssid); val('aprs_radius',cfg.aprs_radius);
  val('aprs_symbol',cfg.aprs_symbol); val('aprs_comment',cfg.aprs_comment);
  val('aprs_interval_min',cfg.aprs_interval_min);
  chk('aprs_beacon',cfg.aprs_beacon);
  // APRS Alerts
  val('aprs_alert',cfg.aprs_alert);
  chk('aprs_alert_enabled',cfg.aprs_alert_enabled);
  chk('aprs_alert_nearby_enabled',cfg.aprs_alert_nearby_enabled);
  chk('aprs_alert_wx_enabled',cfg.aprs_alert_wx_enabled);
  val('aprs_alert_min_sec',cfg.aprs_alert_min_sec);
  val('aprs_alert_screen_sec',cfg.aprs_alert_screen_sec);
  val('aprs_alert_distance_km',cfg.aprs_alert_distance_km);
  // LED Alert
  chk('enable_led_alert',cfg.enable_led_alert);
  val('led_alert_duration_ms',cfg.led_alert_duration_ms);
  val('led_alert_blink_ms',cfg.led_alert_blink_ms);
  // PSK
  val('psk_user_locator',cfg.user_locator);
  val('psk_receiver',cfg.psk_receiver); val('psk_maxspots',cfg.psk_maxspots);
  val('psk_hours',cfg.psk_hours); val('psk_band',cfg.psk_band);
  val('psk_mode',cfg.psk_mode); val('psk_url',cfg.psk_url);
  val('psk_monitor_callsign',cfg.psk_monitor_callsign);
  val('psk_report_days',cfg.psk_report_days);
  val('psk_auto_refresh',cfg.psk_auto_refresh);
  chk('psk_mqtt_enabled',cfg.psk_mqtt_enabled);
  val('psk_mqtt_server',cfg.psk_mqtt_server); val('psk_mqtt_port',cfg.psk_mqtt_port);
  val('psk_mqtt_callsign',cfg.psk_mqtt_callsign);
  $('mqtt_fields').style.display=cfg.psk_mqtt_enabled?'grid':'none';
  // Display
  val('tft_backlight',cfg.tft_backlight); val('tft_rotation',cfg.tft_rotation);
  val('tft_lang',cfg.tft_lang); val('table_size',cfg.table_size);
  chk('tft_invert',cfg.tft_invert); val('menu_hue',cfg.menu_hue);
  val('callsign_color',cfg.callsign_color);
  val('clock_color',cfg.clock_color);
  chk('tft_auto_switch',cfg.tft_auto_switch); val('tft_switch_time_sec',cfg.tft_switch_time_sec);
  chk('screen_saver_enabled',cfg.screen_saver_enabled); val('screen_saver_timeout_min',cfg.screen_saver_timeout_min);
  // Screen order
  if(Array.isArray(cfg.screen_order)){
    cfg.screen_order.forEach((code,i)=>{
      const s=$('screen_'+i); if(s) s.value=code||'off';
    });
  }
  // Touch
  val('touch_swap_mode',cfg.touch_swap_mode); val('touch_rotation',cfg.touch_rotation);
  val('touch_xmin',cfg.touch_xmin); val('touch_xmax',cfg.touch_xmax);
  val('touch_ymin',cfg.touch_ymin); val('touch_ymax',cfg.touch_ymax);
  chk('touch_swap',cfg.touch_swap); chk('touch_invx',cfg.touch_invx); chk('touch_invy',cfg.touch_invy);
  // Dashboard
  $('d_ssid').textContent=cfg.wifi_ssid||'-';
  $('d_call').textContent=cfg.user_callsign||cfg.aprs_callsign||'-';
  $('d_uptime').textContent=cfg.uptime||'-';
  $('d_ip').textContent=(cfg.sys_ip||'-')+(cfg.sys_rssi?' ('+cfg.sys_rssi+' dBm)':'');
  $('d_cluster').textContent=cfg.cluster_host+':'+cfg.cluster_port;
  $('d_aprs').textContent=cfg.aprs_host+':'+cfg.aprs_port;
  // System
  $('s_fw').textContent=cfg.fw_version||'-';
  $('s_uptime').textContent=cfg.uptime||'-';
  $('s_ip').textContent=cfg.sys_ip||'-';
  $('s_rssi').textContent=(cfg.sys_rssi||0)+' dBm';
  $('s_mac').textContent=cfg.sys_mac||'-';
  $('s_chip').textContent=cfg.sys_chip||'-';
  $('s_cpu').textContent=cfg.sys_cpu||'-';
  $('s_temp').textContent=cfg.sys_temp?cfg.sys_temp.toFixed(1)+' C':'-';
  $('s_heap').textContent=cfg.sys_heap?(cfg.sys_heap/1024).toFixed(1)+' KB':'-';
  $('s_heapmax').textContent=cfg.sys_heap_max?(cfg.sys_heap_max/1024).toFixed(1)+' KB':'-';
  $('s_vcc').textContent=cfg.sys_vcc?(cfg.sys_vcc+' V'):'N/A';
  $('s_littlefs').textContent=(cfg.sys_littlefs_free/1024).toFixed(1)+'/'+(cfg.sys_littlefs_total/1024).toFixed(1)+' KB';
  $('fwVer').textContent=cfg.fw_version||'';
  // JSON editor
  $('configEditor').value=JSON.stringify(cfg,null,2);
}

function buildFromForm(){
  return {
    ...C,
    wifi_ssid:gstr('wifi_ssid'), wifi_pass:gstr('wifi_pass'),
    wifi_ssid2:gstr('wifi_ssid2'), wifi_pass2:gstr('wifi_pass2'),
    user_callsign:gstr('user_callsign'), user_locator:gstr('user_locator'),
    user_lat:parseFloat(gstr('user_lat'))||0, user_lon:parseFloat(gstr('user_lon'))||0,
    timezone:gint('timezone',0), openwebrx_url:gstr('openwebrx_url'),
    weather_key:gstr('weather_key'), n2yo_key:gstr('n2yo_key'),
    qrz_user:gstr('qrz_user'), qrz_pass:gstr('qrz_pass'),
    cluster_host:gstr('cluster_host'), cluster_port:gint('cluster_port',7300),
    cluster_filters:gstr('cluster_filters'),
    cluster_usefilters:$('cluster_usefilters').checked,
    cluster_noann:$('cluster_noann').checked,
    cluster_nowwv:$('cluster_nowwv').checked,
    cluster_nowcy:$('cluster_nowcy').checked,
    pota_host:gstr('pota_host'), pota_port:gint('pota_port',7300),
    pota_filter:gstr('pota_filter'), pota_api_url:gstr('pota_api_url'),
    hamalert_host:gstr('hamalert_host'), hamalert_port:gint('hamalert_port',12345),
    hamalert_login:gstr('hamalert_login'), hamalert_password:gstr('hamalert_password'),
    aprs_host:gstr('aprs_host'), aprs_port:gint('aprs_port',14580),
    aprs_callsign:gstr('aprs_callsign'), aprs_passcode:gint('aprs_passcode',0),
    aprs_ssid:gint('aprs_ssid',0), aprs_radius:gint('aprs_radius',50),
    aprs_symbol:gstr('aprs_symbol','/-'), aprs_comment:gstr('aprs_comment'),
    aprs_interval_min:gint('aprs_interval_min',5),
    aprs_beacon:$('aprs_beacon').checked,
    aprs_alert:gstr('aprs_alert'),
    aprs_alert_enabled:$('aprs_alert_enabled').checked,
    aprs_alert_nearby_enabled:$('aprs_alert_nearby_enabled').checked,
    aprs_alert_wx_enabled:$('aprs_alert_wx_enabled').checked,
    aprs_alert_min_sec:gint('aprs_alert_min_sec',60),
    aprs_alert_screen_sec:gint('aprs_alert_screen_sec',30),
    aprs_alert_distance_km:parseFloat(gstr('aprs_alert_distance_km'))||100,
    enable_led_alert:$('enable_led_alert').checked,
    led_alert_duration_ms:gint('led_alert_duration_ms',5000),
    led_alert_blink_ms:gint('led_alert_blink_ms',250),
    psk_receiver:gstr('psk_receiver'), psk_maxspots:gint('psk_maxspots',30),
    psk_hours:gint('psk_hours',12), psk_band:gstr('psk_band'),
    psk_mode:gstr('psk_mode'), psk_url:gstr('psk_url'),
    psk_monitor_callsign:gstr('psk_monitor_callsign'),
    psk_report_days:gint('psk_report_days',7),
    psk_auto_refresh:gint('psk_auto_refresh',10),
    psk_mqtt_enabled:$('psk_mqtt_enabled').checked,
    psk_mqtt_server:gstr('psk_mqtt_server'), psk_mqtt_port:gint('psk_mqtt_port',1883),
    psk_mqtt_callsign:gstr('psk_mqtt_callsign'),
    tft_backlight:gint('tft_backlight',100), tft_rotation:gint('tft_rotation',1),
    tft_lang:gstr('tft_lang','pl'), table_size:gstr('table_size','normal'),
    tft_invert:$('tft_invert').checked, menu_hue:gint('menu_hue',128),
    callsign_color:gint('callsign_color',65535),
    clock_color:gint('clock_color',65530),
    tft_auto_switch:$('tft_auto_switch').checked,
    tft_switch_time_sec:gint('tft_switch_time_sec',30),
    screen_saver_enabled:$('screen_saver_enabled').checked,
    screen_saver_timeout_min:gint('screen_saver_timeout_min',5),
    touch_swap_mode:gstr('touch_swap_mode','none'),
    touch_rotation:gint('touch_rotation',1),
    touch_xmin:gint('touch_xmin',200), touch_xmax:gint('touch_xmax',3900),
    touch_ymin:gint('touch_ymin',200), touch_ymax:gint('touch_ymax',3900),
    touch_swap:$('touch_swap').checked,
    touch_invx:$('touch_invx').checked,
    touch_invy:$('touch_invy').checked,
    screen_order:Array.from({length:14},(_,i)=>{const s=$('screen_'+i);return s?s.value:'off';})
  };
}

function fmtTime(t){
  if(!t||t==='-')return'-';
  const m=t.match(/(\d{1,2}):(\d{2})(?::\d{2})?/);
  if(!m)return t;
  let h=parseInt(m[1],10)+(C.timezone||0);
  if(h<0)h+=24; else if(h>=24)h-=24;
  return(h<10?'0':'')+h+':'+m[2];
}

async function loadSpots(){
  try{
    const r=await fetch('/api/spots');const d=await r.json();
    const spots=Array.isArray(d)?d:(d.spots||[]);
    $('d_spotsState').innerHTML=spots.length?'<span class="ok">OK</span> '+spots.length+' spotow':'Brak spotow';
    $('d_spotsBody').innerHTML='';
    spots.slice(0,25).forEach(s=>{
      const tr=document.createElement('tr');
      tr.innerHTML='<td>'+fmtTime(s.time)+'</td><td>'+(s.callsign||'-')+'</td><td>'+(s.frequency||'-')+'</td><td>'+(s.mode||'-')+'</td><td>'+(s.country||'-')+'</td><td>'+(s.band||'-')+'</td><td>'+(s.spotter||'-')+'</td>';
      $('d_spotsBody').appendChild(tr);
    });
  }catch(e){$('d_spotsState').textContent='Blad: '+e;}
}

async function loadAprs(){
  try{
    const r=await fetch('/api/aprs');const d=await r.json();
    const stations=d.stations||[];
    $('d_aprsState').innerHTML=stations.length?'<span class="ok">OK</span> '+stations.length+' stacji':'Brak stacji';
    $('d_aprsBody').innerHTML='';
    stations.slice(0,20).forEach(s=>{
      const tr=document.createElement('tr');
      tr.innerHTML='<td>'+fmtTime(s.time)+'</td><td>'+(s.callsign||'-')+'</td><td>'+(s.symbol||'-')+'</td><td>'+(s.distance?s.distance.toFixed(1)+' km':'-')+'</td><td>'+(s.comment||'-')+'</td>';
      $('d_aprsBody').appendChild(tr);
    });
  }catch(e){$('d_aprsState').textContent='Blad: '+e;}
}

async function loadPota(){
  try{
    const r=await fetch('/api/pota');const d=await r.json();
    const spots=d.spots||[];
    $('d_potaState').innerHTML=spots.length?'<span class="ok">OK</span> '+spots.length+' spotow':'Brak spotow POTA';
    $('d_potaBody').innerHTML='';
    spots.slice(0,20).forEach(s=>{
      const tr=document.createElement('tr');
      tr.innerHTML='<td>'+fmtTime(s.time)+'</td><td>'+(s.callsign||'-')+'</td><td>'+(s.frequency||'-')+'</td><td>'+(s.mode||'-')+'</td><td>'+(s.spotter||'-')+'</td>';
      $('d_potaBody').appendChild(tr);
    });
  }catch(e){$('d_potaState').textContent='Blad: '+e;}
}

async function loadHam(){
  try{
    const r=await fetch('/api/hamalert');const d=await r.json();
    const spots=d.spots||[];
    $('d_hamState').innerHTML=spots.length?'<span class="ok">OK</span> '+spots.length+' spotow':'Brak alertow';
    $('d_hamBody').innerHTML='';
    spots.slice(0,20).forEach(s=>{
      const tr=document.createElement('tr');
      tr.innerHTML='<td>'+fmtTime(s.time)+'</td><td>'+(s.callsign||'-')+'</td><td>'+(s.frequency||'-')+'</td><td>'+(s.mode||'-')+'</td><td>'+(s.country||'-')+'</td>';
      $('d_hamBody').appendChild(tr);
    });
  }catch(e){$('d_hamState').textContent='Blad: '+e;}
}

async function loadISS(){
  try{
    const r=await fetch('/api/iss');const d=await r.json();
    $('d_issPos').textContent=(d.lat||0).toFixed(4)+', '+(d.lng||0).toFixed(4);
    $('d_issAlt').textContent=(d.altitude||0).toFixed(1)+' km / '+(d.speed||0).toFixed(0)+' km/h';
    $('d_issAzi').textContent='Az:'+(d.azimuth||0).toFixed(1)+' El:'+(d.elevation||0).toFixed(1)+' Dist:'+(d.distance||0).toFixed(0)+' km';
    $('d_issCountry').textContent=d.country||'-';
  }catch(e){}
}

async function loadPsk(){
  try{
    const r=await fetch('/api/psk');const d=await r.json();
    const spots=d.spots||[];
    $('d_pskState').innerHTML=spots.length?'<span class="ok">OK</span> '+spots.length+' spotow':'Brak spotow PSK';
    $('d_pskBody').innerHTML='';
    spots.slice(0,25).forEach(s=>{
      const tr=document.createElement('tr');
      tr.innerHTML='<td>'+(s.callsign||'-')+'</td><td>'+(s.band||'-')+'</td><td>'+(s.mode||'-')+'</td><td>'+(s.snr!=null?s.snr.toFixed(0)+' dB':'-')+'</td><td>'+(s.lat?s.lat.toFixed(2):'-')+'</td><td>'+(s.lon?s.lon.toFixed(2):'-')+'</td>';
      $('d_pskBody').appendChild(tr);
    });
  }catch(e){$('d_pskState').textContent='Blad: '+e;}
}

async function loadConfig(){
  const r=await fetch('/api/config');const cfg=await r.json();
  C=cfg; fillForm(cfg);
  const cs=$('connStatus');
  cs.innerHTML='<span class="status-dot on"></span>Polaczono';
}

async function loadAll(){
  try{
    await Promise.all([loadConfig(),loadSpots(),loadAprs(),loadPota(),loadHam(),loadISS(),loadPsk()]);
  }catch(e){}
}

function syncJsonFromForm(){
  $('configEditor').value=JSON.stringify(buildFromForm(),null,2);
  $('saveState').textContent='JSON zaktualizowany z formularza.';
}

async function saveJsonEditor(){
  let parsed;
  try{parsed=JSON.parse($('configEditor').value);}catch(e){$('saveState').textContent='Blad JSON: '+e;return;}
  $('saveState').textContent='Zapisywanie...';
  try{
    const r=await fetch('/api/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(parsed)});
    const t=await r.text();
    $('saveState').textContent='Zapisano. '+t;
    setTimeout(()=>loadAll(),2000);
  }catch(e){$('saveState').textContent='Blad zapisu: '+e;}
}

function saveFromActiveTab(){
  $('configEditor').value=JSON.stringify(buildFromForm(),null,2);
  saveJsonEditor();
}

async function resetTouch(){
  if(!confirm('Reset kalibracji dotyku do wartosci domyslnych?'))return;
  try{
    await fetch('/api/reset_touch',{method:'POST'});
    alert('Kalibracja zresetowana. Odswiez strone.');
    loadAll();
  }catch(e){alert('Blad: '+e);}
}

buildScreenOrderSelects();

$('psk_mqtt_enabled').addEventListener('change',function(){
  $('mqtt_fields').style.display=this.checked?'grid':'none';
});

// Dwa pola locatora (Stacja i PSK) sa jednym ustawieniem - trzymaj je zgodne.
const locStation=$('user_locator'), locPsk=$('psk_user_locator');
if(locStation&&locPsk){
  const syncLoc=(a,b)=>{b.addEventListener('input',()=>{a.value=b.value;});};
  syncLoc(locStation,locPsk); syncLoc(locPsk,locStation);
}

$('bmpForm').addEventListener('submit',function(e){
  e.preventDefault();
  const file=$('bmpFile').files[0];
  if(!file){alert('Wybierz plik');return;}
  const fd=new FormData();fd.append('file',file);
  $('bmpStatus').textContent='Wgrywanie...';
  fetch('/upload/bmp',{method:'POST',body:fd}).then(r=>r.text()).then(t=>{
    $('bmpStatus').textContent='OK: '+t;
  }).catch(err=>{$('bmpStatus').textContent='Blad: '+err;});
});

loadAll();
setInterval(()=>{loadSpots();loadAprs();loadPota();loadHam();loadISS();loadPsk();},15000);
</script>
</body>
</html>
)rawliteral";
}

String getConfigHTML() {
  return getMainHTML(); // UĂ„Ä…Ă„Ëťywa tego samego HTML z JavaScript
}

#ifdef ENABLE_TFT_DISPLAY
TaskHandle_t uiTaskHandle = nullptr;

void uiTaskLoop(void *parameter) {
  (void)parameter;
  for (;;) {
    unsigned long now = millis();

    if (tftInitialized) {
      bool pendingScreen1 = false;
      bool pendingScreen2 = false;
      bool pendingScreen6 = false;
      bool pendingScreen7 = false;
      bool pendingAnyScreen = false;
      uint8_t pendingAnyScreenId = SCREEN_HAM_CLOCK;
      portENTER_CRITICAL(&uiPendingRedrawMux);
      pendingScreen1 = uiPendingScreen1Redraw;
      pendingScreen2 = uiPendingScreen2Redraw;
      pendingScreen6 = uiPendingScreen6Redraw;
      pendingScreen7 = uiPendingScreen7Redraw;
      pendingAnyScreen = uiPendingAnyScreenRedraw;
      pendingAnyScreenId = uiPendingAnyScreenId;
      uiPendingScreen1Redraw = false;
      uiPendingScreen2Redraw = false;
      uiPendingScreen6Redraw = false;
      uiPendingScreen7Redraw = false;
      uiPendingAnyScreenRedraw = false;
      portEXIT_CRITICAL(&uiPendingRedrawMux);

      if (pendingAnyScreen && currentScreen == (ScreenType)pendingAnyScreenId && !inMenu && !aprsAlertScreenActive && !qrzPopupActive) {
        drawScreen((ScreenType)pendingAnyScreenId);
      }

      // Dodatkowe sprawdzenia dla konkretnych ekranĂłw tylko gdy pendingAnyScreen nie byĹ‚o true
      if (!pendingAnyScreen) {
        if (pendingScreen1 && currentScreen == SCREEN_HAM_CLOCK && !inMenu && !aprsAlertScreenActive) {
          drawScreen(SCREEN_HAM_CLOCK);
        }
        if (pendingScreen2 && currentScreen == SCREEN_DX_CLUSTER && !inMenu && !aprsAlertScreenActive && !qrzPopupActive) {
          drawScreen(SCREEN_DX_CLUSTER);
        }
        if (pendingScreen7 && currentScreen == SCREEN_POTA_CLUSTER && !inMenu && !aprsAlertScreenActive && !qrzPopupActive) {
          drawScreen(SCREEN_POTA_CLUSTER);
        }
        if (pendingScreen6 && (currentScreen == SCREEN_APRS_IS || currentScreen == SCREEN_APRS_RADAR) && !inMenu && !aprsAlertScreenActive && !qrzPopupActive) {
          drawScreen(currentScreen);
        }
      }

      if (aprsAlertDrawPending) {
        APRSStation pendingStation;
        portENTER_CRITICAL(&aprsAlertPendingMux);
        pendingStation = aprsAlertPendingStation;
        aprsAlertDrawPending = false;
        portEXIT_CRITICAL(&aprsAlertPendingMux);
        ALERT_Screen(pendingStation);
      }
      updateAlertScreenTimeout();
      handleTouchNavigation();
      
      // Rysuj wygaszacz gdy jest aktywny
      if (screenSaverActive && !screenSaverMenuActive) {
        drawScreenSaver();
      }
    }

    // Sprawdzanie wygaszacza ekranu - co sekundÄ™
    static unsigned long lastScreenSaverCheckMs = 0;
    if (now - lastScreenSaverCheckMs >= 1000) {
      checkScreenSaverTimeout();
      lastScreenSaverCheckMs = now;
    }

    if (tftAutoSwitchEnabled && tftInitialized && !inMenu && !aprsAlertScreenActive && !touchCalActive && !brightnessMenuActive && !callsignColorMenuActive && !clockColorMenuActive && !qrzPopupActive) {
      unsigned long nowSwitch = millis();
      if (tftAutoSwitchLastMs == 0 || tftAutoSwitchLastScreen != currentScreen) {
        tftAutoSwitchLastMs = nowSwitch;
        tftAutoSwitchLastScreen = currentScreen;
      } else {
        unsigned long intervalMs = (unsigned long)tftAutoSwitchTimeSec * 1000UL;
        if (intervalMs > 0 && (nowSwitch - tftAutoSwitchLastMs) >= intervalMs) {
          ScreenType nextScreen = getNextScreenId(currentScreen);
          if (nextScreen != SCREEN_OFF && nextScreen != currentScreen) {
            currentScreen = nextScreen;
            drawScreen(currentScreen);
          }
          tftAutoSwitchLastMs = nowSwitch;
          tftAutoSwitchLastScreen = currentScreen;
        }
      }
    }

    if (!restartRequested) {
      if (tftInitialized && currentScreen == SCREEN_HAM_CLOCK && !inMenu && !aprsAlertScreenActive) {
        if (screen1ClockStyle == 1) {
          updateScreen1ClockClassic();
          updateScreen1HeaderClassic();
        } else {
          if (now - lastScreen1UpdateMs > 1000) {
            updateScreen1ClockModern();
            updateScreen1HeaderModern();
            lastScreen1UpdateMs = now;
          }
        }
      }

      if (tftInitialized && currentScreen == SCREEN_DX_CLUSTER && !inMenu && !aprsAlertScreenActive && !qrzPopupActive) {
        if (now - lastScreenUpdate > 100) {
          updateScreen2Data();
          lastScreenUpdate = now;
        }
        // Aktualizacja zegara co sekundÄ™ - WYĹÄ„CZONA
        // static unsigned long lastDxClockUpdate = 0;
        // if (now - lastDxClockUpdate >= 1000) {
        //   updateDxClusterClock();
        //   lastDxClockUpdate = now;
        // }
      }

      if (tftInitialized && currentScreen == SCREEN_POTA_CLUSTER && !inMenu && !aprsAlertScreenActive && !qrzPopupActive) {
        if (now - lastScreen7UpdateMs > 200) {
          updateScreen7Data();
          lastScreen7UpdateMs = now;
        }
        // Aktualizacja zegara co sekundÄ™
        static unsigned long lastPotaClockUpdate = 0;
        if (now - lastPotaClockUpdate >= 1000) {
          updatePotaClusterClock();
          lastPotaClockUpdate = now;
        }
      }

      if (tftInitialized && currentScreen == SCREEN_HAMALERT_CLUSTER && !inMenu && !aprsAlertScreenActive && !qrzPopupActive) {
        if (now - lastScreen8UpdateMs > 200) {
          updateScreen8Data();
          lastScreen8UpdateMs = now;
        }
        // Aktualizacja zegara co sekundÄ™
        static unsigned long lastHamalertClockUpdate = 0;
        if (now - lastHamalertClockUpdate >= 1000) {
          updateHamalertClock();
          lastHamalertClockUpdate = now;
        }
      }

      if (tftInitialized && (currentScreen == SCREEN_APRS_IS || currentScreen == SCREEN_APRS_RADAR) && !inMenu && !aprsAlertScreenActive) {
        if (now - lastScreenUpdate > 100) {
          updateScreen6Data();
          lastScreenUpdate = now;
        }
      }

      if (tftInitialized && currentScreen == SCREEN_SUN_SPOTS && !inMenu && !aprsAlertScreenActive) {
        if (now - lastScreen3UpdateMs > 1000) {
          updateScreen3Data();
          lastScreen3UpdateMs = now;
        }
      }

      if (tftInitialized && currentScreen == SCREEN_BAND_INFO && !inMenu && !aprsAlertScreenActive) {
        if (now - lastScreen4UpdateMs > 1000) {
          updateScreen4Data();
          lastScreen4UpdateMs = now;
        }
        // Aktualizacja zegara co sekundÄ™
        static unsigned long lastBandInfoClockUpdate = 0;
        if (now - lastBandInfoClockUpdate >= 1000) {
          updateBandInfoClock();
          lastBandInfoClockUpdate = now;
        }
      }

      if (tftInitialized &&
          (currentScreen == SCREEN_WEATHER_DSP || currentScreen == SCREEN_WEATHER_FORECAST) &&
          !inMenu && !aprsAlertScreenActive) {
        if (now - lastScreen5UpdateMs > 1000) {
          updateScreen5Data();
          lastScreen5UpdateMs = now;
        }
        // Aktualizacja zegara co sekundÄ™
        static unsigned long lastWeatherClockUpdate = 0;
        if (now - lastWeatherClockUpdate >= 1000) {
          if (currentScreen == SCREEN_WEATHER_DSP) {
            updateWeatherClock();
          } else {
            updateWeatherForecastClock();
          }
          lastWeatherClockUpdate = now;
        }
      }

      if (tftInitialized && currentScreen == SCREEN_MATRIX_CLOCK && !inMenu && !aprsAlertScreenActive) {
        updateScreen10();
      }

      if (tftInitialized && currentScreen == SCREEN_SYSTEM_INFO && !inMenu && !aprsAlertScreenActive) {
        updateSystemInfo();
      }

      // Auto-odswiezanie ekranu PSK (tryb HTTP). drawPskMap() wolane bylo tylko
      // przy zdarzeniach (wejscie na ekran, dotyk), wiec licznik auto-refresh
      // nigdy nie odpalal i ekran zamarzal na danych z chwili wejscia. Wolamy
      // periodycznie, ale tylko gdy minie interwal i nie trwa backoff po 429
      // (inaczej pelne przerysowanie powtarzaloby sie w kolko).
      if (tftInitialized && currentScreen == SCREEN_PSK_MAP && !inMenu &&
          !pskMapMenuOpen && !aprsAlertScreenActive && !pskMqttEnabled &&
          pskAutoRefreshMinutes > 0 && lastPskFetchMs != 0) {
        unsigned long pskIntervalMs = (unsigned long)pskAutoRefreshMinutes * 60UL * 1000UL;
        if (millis() >= pskBackoffUntilMs && (millis() - lastPskFetchMs) >= pskIntervalMs) {
          drawPskMap();
        }
      }

      
      // ObsĹ‚uga MQTT dla PSK Reporter (gdy aktywny ekran PSK i wĹ‚Ä…czony MQTT)
      if (pskMqttEnabled && currentScreen == SCREEN_PSK_MAP && !pskMapMenuOpen) {
        loopPskMqtt();
      }
    }

    // Animacja smugi radaru APRS. Robimy to w UI_Task (a NIE w loopTask),
    // bo tylko to zadanie rysuje po TFT - dzieki temu pushSprite nigdy nie
    // jest wywolywany rownolegle z innego rdzenia (to powodowalo reset).
    // Wstrzymujemy animacje tuz po dotknieciu: XPT2046 dzieli magistrale SPI
    // z TFT, wiec ciagly pushSprite zaklocalby odczyt dotyku i "zjadal" tapniecia
    // (przejscia na inny ekran / menu).
    if (currentScreen == SCREEN_APRS_RADAR && tftInitialized && !inMenu &&
        !screenSleepActive && !bootSequenceActive &&
        (millis() - screenSleepLastActivityMs) > 500) {
      static unsigned long lastAprsRadarTickMs = 0;
      if (now - lastAprsRadarTickMs >= 100) {
        lastAprsRadarTickMs = now;
        aprsRadarTick();
      }
    }
    // Drugi odczyt dotyku po animacji: pushSprite (~60 ms na 40 MHz) blokuje
    // zadanie, wiec bez tego dotkniecie miedzy odczytami przepadalo.
    if (tftInitialized) {
      handleTouchNavigation();
    }

    // Diagnostyka: co 15 s potwierdz, ze zadanie UI zyje i na jakim ekranie.
    // "Ekran zgasl" = UI_Task zawieszony/nie rysuje; brak tego logu = task nie zyje.
    {
      static unsigned long lastUiDiagMs = 0;
      if (now - lastUiDiagMs >= 15000) {
        lastUiDiagMs = now;
        Serial.printf("[UI] alive screen=%s menu=%d sleep=%d heap=%d\n",
                      screenTypeToCodeStr(currentScreen), inMenu ? 1 : 0,
                      screenSleepActive ? 1 : 0, (int)ESP.getFreeHeap());
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
#endif

// ========== FUNKCJE PSK REPORTER MAP ==========

// Konwersja wspĂłĹ‚rzÄ™dnych geograficznych na piksele ekranu - PROJEKCJA PLATE CARRĂ‰E
static void latLonToScreen(float lat, float lon, int &x, int &y) {
  // Prosta projekcja walcowa (Plate CarrĂ©e) - liniowe odwzorowanie
  // X: liniowe odwzorowanie dĹ‚ugoĹ›ci geograficznej (-180 do +180)
  x = MAP_DISPLAY_X + (int)((lon - MAP_LON_MIN) / (MAP_LON_MAX - MAP_LON_MIN) * MAP_DISPLAY_W);
  
  // Y: liniowe odwzorowanie szerokoĹ›ci geograficznej (+90 na gĂłrze, -90 na dole)
  // OdwrĂłcone: lat_max - lat daje Y=0 dla +90, Y=max dla -90
  float yNormalized = (MAP_LAT_MAX - lat) / (MAP_LAT_MAX - MAP_LAT_MIN);
  y = MAP_DISPLAY_Y + (int)(yNormalized * MAP_DISPLAY_H);
}

// Pobieranie danych z PSK Reporter
bool fetchPskReporterData() {
  if (!wifiConnected) return false;

  static WiFiClientSecure pskSecureClient;
  static WiFiClient pskPlainClient;
  HTTPClient http;
  // UĹĽyj wĹ‚asnego URL jeĹ›li ustawiony, w przeciwnym razie domyĹ›lny
  String baseUrl = pskCustomUrl.length() > 0 ? pskCustomUrl : "https://retrieve.pskreporter.info/query?";
  // Auto-upgrade HTTP do HTTPS dla bezpieczeĹ„stwa
  if (baseUrl.startsWith("http://")) {
    baseUrl = "https://" + baseUrl.substring(7);
  }
  String url = baseUrl;
  // Upewnij siÄ™ ĹĽe URL koĹ„czy siÄ™ ? lub &
  if (!url.endsWith("?") && !url.endsWith("&")) {
    url += url.indexOf("?") >= 0 ? "&" : "?";
  }
  // UĹĽyj konfigurowalnego okna czasowego (w godzinach) lub raportu z dni wstecz
  int secondsWindow;
  if (pskReportDays > 0) {
    // Raport od X dni wstecz
    secondsWindow = pskReportDays * 24 * 3600;
  } else {
    // Standardowe okno czasowe w godzinach
    secondsWindow = pskHoursWindow * 3600;
  }
  // API PSKReporter nie obsluguje okien dluzszych niz 24 h.
  if (secondsWindow > 86400) secondsWindow = 86400;
  url += "flowStartSeconds=-" + String(secondsWindow) + "&";
  // rptlimit ogranicza odpowiedz do zadanej liczby rekordow (maxrows jest
  // ignorowane w trybie JSON - bez tego API zwraca cala tabele ~1,4 MB).
  url += "rptlimit=" + String(pskMaxSpots) + "&";
  url += "rronly=1&noactive=1&";
  // Opcjonalnie dodaj filtr znaku odbiornika
  if (pskReceiverCallsign.length() > 0) {
    url += "receiverCallsign=" + pskReceiverCallsign + "&";
  }
  // Opcjonalnie dodaj filtr znaku nadawcy (monitorowanie)
  if (pskMonitorCallsign.length() > 0) {
    url += "senderCallsign=" + pskMonitorCallsign + "&";
  }
  url += "frags=1&";
  url += "json=1";

  Serial.print("[PSK] URL: ");
  Serial.println(url);

  // Wybierz klienta na podstawie protokołu HTTP/HTTPS
  bool useHttps = url.startsWith("https://");
  http.setTimeout(15000);
  if (useHttps) {
    pskSecureClient.setInsecure();
    http.begin(pskSecureClient, url);
  } else {
    http.begin(pskPlainClient, url);
  }
  int httpCode = http.GET();

  if (httpCode != 200) {
    Serial.print("[PSK] HTTP error: ");
    Serial.println(httpCode);
    // 429 = zbyt wiele zapytan. Bez przerwy kolejne redrawy natychmiast
    // dostaja 429 ponownie i limit nigdy nie mija (petla 429 -> fetch -> 429).
    if (httpCode == 429) pskBackoffUntilMs = millis() + PSK_RATE_LIMIT_BACKOFF_MS;
    http.end();
    return false;
  }

  // Parsowanie strumieniowe z filtrem zamiast getString()+deserializeJson()
  // - unika trzymania caĹ‚ej odpowiedzi w pamiÄ™ci DWA razy naraz (raz jako
  // String, raz jako dokument JSON) i pomija w locie pola, ktĂłrych i tak
  // nie uĹĽywamy, wiÄ™c dokument jest mniejszy i szybszy do przetworzenia.
  // Samo opĂłĹşnienie sieciowe do serwera pskreporter.info (ktĂłry bywa wolny
  // przy wiÄ™kszych oknach czasowych/limitach spotĂłw) tym niestety nie da
  // siÄ™ skrĂłciÄ‡ - to poza naszÄ… kontrolÄ….
  WiFiClient *stream = http.getStreamPtr();

  DynamicJsonDocument filter(256);
  // Zachowaj pole message - PSKReporter wstawia tam powod odrzucenia zapytania.
  filter["message"] = true;
  filter["receptionReport"][0]["senderCallsign"] = true;
  filter["receptionReport"][0]["senderLocator"] = true;
  filter["receptionReport"][0]["mode"] = true;
  filter["receptionReport"][0]["sNR"] = true;
  filter["receptionReport"][0]["frequency"] = true;

  DynamicJsonDocument doc(24576);
  DeserializationError error = deserializeJson(doc, *stream, DeserializationOption::Filter(filter));
  http.end();

  if (error) {
    Serial.print("[PSK] JSON parse error: ");
    Serial.println(error.c_str());
    return false;
  }

  pskSpotCount = 0;

  // PSKReporter przy zbyt czestych zapytaniach zwraca sam komunikat JSON
  // ({"message": "Your IP has made too many queries too often..."}) bez tablicy
  // receptionReport - rozpoznaj to, zeby pokazac przyczyne zamiast cichego zera.
  if (!doc.containsKey("receptionReport")) {
    const char* msg = doc["message"];
    Serial.print("[PSK] Brak danych: ");
    Serial.println(msg ? msg : "brak pola receptionReport");
    pskRateLimited = true;
    pskBackoffUntilMs = millis() + PSK_RATE_LIMIT_BACKOFF_MS;
    return false;
  }
  pskRateLimited = false;
  pskBackoffUntilMs = 0;  // odpowiedz OK - limit minal

  JsonArray reports = doc["receptionReport"].as<JsonArray>();

  if (!reports.isNull()) {
    for (JsonObject report : reports) {
      if (pskSpotCount >= PSK_MAX_SPOTS) break;

      const char* callsign = report["senderCallsign"];
      const char* locator = report["senderLocator"];
      const char* mode = report["mode"];
      int snr = report["sNR"] | 0;
      int frequency = report["frequency"] | 0;

      if (callsign && locator) {
        // OkreĹ›l pasmo z czÄ™stotliwoĹ›ci
        int freq_khz = frequency / 1000;
        int band = 0;
        if (freq_khz >= 3500 && freq_khz <= 4000) band = 80;
        else if (freq_khz >= 7000 && freq_khz <= 7300) band = 40;
        else if (freq_khz >= 14000 && freq_khz <= 14350) band = 20;
        else if (freq_khz >= 21000 && freq_khz <= 21450) band = 15;
        else if (freq_khz >= 28000 && freq_khz <= 29700) band = 10;
        else if (freq_khz >= 50000 && freq_khz <= 54000) band = 6;

        // Filtruj po pasmie jeĹ›li ustawiony
        if (pskFilterBand.length() > 0) {
          int filterBand = pskFilterBand.toInt();
          if (band != filterBand) continue; // PomiĹ„ ten spot
        }

        // Filtruj po trybie jeĹ›li ustawiony
        String modeStr = mode ? String(mode) : String("FT8");
        if (pskFilterMode.length() > 0) {
          if (!modeStr.equalsIgnoreCase(pskFilterMode)) continue; // PomiĹ„ ten spot
        }

        double lat, lon;
        locatorToLatLon(String(locator), lat, lon);

        pskSpots[pskSpotCount].callsign = String(callsign);
        pskSpots[pskSpotCount].lat = (float)lat;
        pskSpots[pskSpotCount].lon = (float)lon;
        pskSpots[pskSpotCount].mode = modeStr;
        pskSpots[pskSpotCount].snr = snr;
        pskSpots[pskSpotCount].timestamp = millis();
        pskSpots[pskSpotCount].band = band;

        pskSpotCount++;
      }
    }
  }

  Serial.print("[PSK] Pobrano ");
  Serial.print(pskSpotCount);
  Serial.println(" spotĂłw");

  return pskSpotCount > 0;
}

// Kolory dla pasm
static uint16_t getPskBandColor(int band) {
  switch (band) {
    case 80: return TFT_RED;
    case 40: return TFT_ORANGE;
    case 20: return TFT_GREEN;
    case 15: return TFT_CYAN;
    case 10: return TFT_BLUE;
    case 6:  return TFT_MAGENTA;
    default: return TFT_WHITE;
  }
}

// Rysowanie nagĹ‚Ăłwka (przezroczysty - na mapie)
static void drawPskMapHeader() {
  // Naglowek w stylu ISS (nakladka na mape).
  uiHeader("PSKReporter MAP", true);

  // Status auto-odswiezania
  tft.setTextSize(1);
  tft.setTextColor(UI_COL_WARN, UI_COL_BG);
  tft.setCursor(322, 30);
  tft.print("AUTO:" + String(pskAutoRefreshMinutes > 0 ? String(pskAutoRefreshMinutes) + "min" : "OFF"));

  // Status danych: licznik spotow albo ostrzezenie o limicie zapytan PSKReporter.
  tft.setTextSize(1);
  if (pskRateLimited) {
    tft.setTextColor(UI_COL_RED, UI_COL_BG);
    tft.setCursor(12, 30);
    tft.print("LIMIT PSKReporter - poczekaj");
  } else if (pskSpotCount > 0) {
    tft.setTextColor(UI_COL_ACCENT, UI_COL_BG);
    tft.setCursor(12, 30);
    tft.print("Spoty: " + String(pskSpotCount));
  }
}

// Rysowanie przycisku menu w lewym gĂłrnym rogu
static void drawPskMenuButton() {
  // Przycisk menu 30x26 w lewym gĂłrnym rogu (pomaraĹ„czowy jak nagĹ‚Ăłwek)
  tft.fillRect(2, 2, 30, 26, TFT_RADIO_ORANGE);
  tft.drawRect(2, 2, 30, 26, TFT_BLACK);
  // Trzy poziome kreski (hamburger menu)
  tft.fillRect(6, 8, 22, 3, TFT_BLACK);
  tft.fillRect(6, 13, 22, 3, TFT_BLACK);
  tft.fillRect(6, 18, 22, 3, TFT_BLACK);
}

// Rysowanie klawiatury ekranowej
static void drawPskKeyboard() {
  tft.fillRect(20, 120, 440, 195, 0x18E3);
  tft.drawRect(20, 120, 440, 195, TFT_WHITE);

  tft.fillRect(30, 128, 290, 28, TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(35, 134);
  String shown = pskKeyboardBuffer;
  if (shown.length() > 18) {
    shown = shown.substring(shown.length() - 18);
  }
  tft.print(shown);
  tft.print("_");

  int keyW = 32, keyH = 22, gap = 4;
  const char* rows[] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM.-/:"};
  int startX[] = {40, 40, 56, 40};
  int startY[] = {164, 190, 216, 242};

  for (int r = 0; r < 4; r++) {
    for (int i = 0; i < strlen(rows[r]); i++) {
      int x = startX[r] + i * (keyW + gap);
      int y = startY[r];
      uint16_t fill = (r == 0 || (r == 3 && i >= 7)) ? TFT_BLUE : TFT_DARKGREY;
      tft.fillRect(x, y, keyW, keyH, fill);
      tft.drawRect(x, y, keyW, keyH, TFT_WHITE);
      tft.setTextSize(1);
      tft.setTextColor(TFT_WHITE);
      tft.setCursor(x + 12, y + 7);
      tft.print(rows[r][i]);
    }
  }

  tft.fillRect(330, 128, 58, 28, TFT_RED);
  tft.drawRect(330, 128, 58, 28, TFT_WHITE);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(344, 138);
  tft.print("DEL");

  tft.fillRect(396, 128, 54, 28, TFT_ORANGE);
  tft.drawRect(396, 128, 54, 28, TFT_WHITE);
  tft.setCursor(407, 138);
  tft.print("CLR");

  tft.fillRect(250, 278, 85, 28, TFT_GREEN);
  tft.drawRect(250, 278, 85, 28, TFT_WHITE);
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(278, 284);
  tft.print("OK");

  tft.fillRect(350, 278, 85, 28, TFT_RED);
  tft.drawRect(350, 278, 85, 28, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(386, 284);
  tft.print("X");
}

// Rysowanie wyboru pasm
static void drawPskBandSelector() {
  // TĹ‚o
  tft.fillRect(40, 80, 400, 160, 0x18E3);
  tft.drawRect(40, 80, 400, 160, TFT_WHITE);
  
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(140, 90);
  tft.print("Wybierz pasma:");
  
  const char* bands[] = {"160m", "80m", "40m", "30m", "20m", "17m", "15m", "12m", "10m", "6m", "ALL"};
  int bandCount = 11;
  int tileW = 60, tileH = 30, gap = 8;
  int startX = 60, startY = 120;
  int cols = 5;
  
  for (int i = 0; i < bandCount; i++) {
    int col = i % cols;
    int row = i / cols;
    int x = startX + col * (tileW + gap);
    int y = startY + row * (tileH + gap);
    
    bool selected = (pskTempBand == bands[i]) || 
                    (pskTempBand == "" && strcmp(bands[i], "ALL") == 0);
    
    tft.fillRect(x, y, tileW, tileH, selected ? TFT_GREEN : TFT_DARKGREY);
    tft.drawRect(x, y, tileW, tileH, TFT_WHITE);
    tft.setTextSize(1);
    tft.setTextColor(selected ? TFT_BLACK : TFT_WHITE);
    tft.setCursor(x + 15, y + 10);
    tft.print(bands[i]);
  }
  
  // Przycisk zamknij
  tft.fillRect(200, 200, 80, 30, TFT_RED);
  tft.drawRect(200, 200, 80, 30, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(215, 210);
  tft.print("ZAMKNIJ");
}

// Rysowanie wyboru trybĂłw
static void drawPskModeSelector() {
  // TĹ‚o
  tft.fillRect(40, 80, 400, 160, 0x18E3);
  tft.drawRect(40, 80, 400, 160, TFT_WHITE);
  
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(140, 90);
  tft.print("Wybierz tryb:");
  
  const char* modes[] = {"FT8", "FT4", "JS8", "PSK31", "PSK63", "RTTY", "CW", "SSB", "ALL"};
  int modeCount = 9;
  int tileW = 70, tileH = 30, gap = 10;
  int startX = 60, startY = 120;
  int cols = 4;
  
  for (int i = 0; i < modeCount; i++) {
    int col = i % cols;
    int row = i / cols;
    int x = startX + col * (tileW + gap);
    int y = startY + row * (tileH + gap);
    
    bool selected = (pskTempMode == modes[i]) || 
                    (pskTempMode == "" && strcmp(modes[i], "ALL") == 0);
    
    tft.fillRect(x, y, tileW, tileH, selected ? TFT_GREEN : TFT_DARKGREY);
    tft.drawRect(x, y, tileW, tileH, TFT_WHITE);
    tft.setTextSize(1);
    tft.setTextColor(selected ? TFT_BLACK : TFT_WHITE);
    tft.setCursor(x + 20, y + 10);
    tft.print(modes[i]);
  }
  
  // Przycisk zamknij
  tft.fillRect(200, 200, 80, 30, TFT_RED);
  tft.drawRect(200, 200, 80, 30, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(215, 210);
  tft.print("ZAMKNIJ");
}

// Rysowanie menu ustawieĹ„ PSK
static void drawPskSettingsMenu() {
  // TĹ‚o menu - pĂłĹ‚przezroczyste ciemne (rozszerzone dla MQTT)
  tft.fillRect(40, 40, 400, 230, 0x18E3); // Ciemny szary
  tft.drawRect(40, 40, 400, 230, TFT_WHITE);
  
  // TytuĹ‚
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(140, 50);
  tft.print("PSK Reporter - Filtry");
  
  // Linia podziaĹ‚u
  tft.drawLine(50, 70, 430, 70, TFT_WHITE);
  
  // Pola edycji
  tft.setTextSize(1);
  
  // Znak (receiver)
  tft.setTextColor(pskActiveField == PSK_FIELD_CALL ? TFT_YELLOW : TFT_WHITE);
  tft.setCursor(60, 90);
  tft.print("Znak:");
  tft.fillRect(120, 85, 100, 20, pskActiveField == PSK_FIELD_CALL ? TFT_DARKGREY : TFT_BLACK);
  tft.setCursor(125, 90);
  tft.print(pskTempReceiver.length() > 0 ? pskTempReceiver.c_str() : "(wszystkie)");
  
  // Pasmo
  tft.setTextColor(pskActiveField == PSK_FIELD_BAND ? TFT_YELLOW : TFT_WHITE);
  tft.setCursor(250, 90);
  tft.print("Pasmo:");
  tft.fillRect(300, 85, 80, 20, pskActiveField == PSK_FIELD_BAND ? TFT_DARKGREY : TFT_BLACK);
  tft.setCursor(305, 90);
  tft.print(pskTempBand.length() > 0 ? pskTempBand.c_str() : "(wszystkie)");
  
  // Tryb
  tft.setTextColor(pskActiveField == PSK_FIELD_MODE ? TFT_YELLOW : TFT_WHITE);
  tft.setCursor(60, 120);
  tft.print("Tryb:");
  tft.fillRect(120, 115, 100, 20, pskActiveField == PSK_FIELD_MODE ? TFT_DARKGREY : TFT_BLACK);
  tft.setCursor(125, 120);
  tft.print(pskTempMode.length() > 0 ? pskTempMode.c_str() : "(wszystkie)");
  
  // Max spotĂłw
  tft.setTextColor(pskActiveField == PSK_FIELD_MAXSPOTS ? TFT_YELLOW : TFT_WHITE);
  tft.setCursor(250, 120);
  tft.print("Max:");
  tft.fillRect(300, 115, 50, 20, pskActiveField == PSK_FIELD_MAXSPOTS ? TFT_DARKGREY : TFT_BLACK);
  tft.setCursor(305, 120);
  tft.print(pskTempMaxSpots);
  
  // MQTT Mode
  tft.setTextColor(pskActiveField == PSK_FIELD_MQTT_MODE ? TFT_YELLOW : TFT_WHITE);
  tft.setCursor(60, 150);
  tft.print("Tryb:");
  tft.fillRect(120, 145, 100, 20, pskActiveField == PSK_FIELD_MQTT_MODE ? TFT_DARKGREY : TFT_BLACK);
  tft.setCursor(125, 150);
  tft.print(pskTempMqttEnabled ? "MQTT" : "HTTP");
  
  // MQTT Server (tylko gdy MQTT)
  if (pskTempMqttEnabled) {
    tft.setTextColor(pskActiveField == PSK_FIELD_MQTT_SERVER ? TFT_YELLOW : TFT_WHITE);
    tft.setCursor(250, 150);
    tft.print("Serwer:");
    tft.fillRect(300, 145, 120, 20, pskActiveField == PSK_FIELD_MQTT_SERVER ? TFT_DARKGREY : TFT_BLACK);
    tft.setCursor(305, 150);
    String srv = pskTempMqttServer.length() > 12 ? pskTempMqttServer.substring(0, 12) + "..." : pskTempMqttServer;
    tft.print(srv.length() > 0 ? srv.c_str() : "default");
  } else {
    // Dni wstecz (gdy HTTP - po prawej stronie)
    tft.setTextColor(pskActiveField == PSK_FIELD_DAYS ? TFT_YELLOW : TFT_WHITE);
    tft.setCursor(250, 150);
    tft.print("Dni:");
    tft.fillRect(290, 145, 60, 20, pskActiveField == PSK_FIELD_DAYS ? TFT_DARKGREY : TFT_BLACK);
    tft.setCursor(300, 150);
    tft.print(pskTempReportDays);
    tft.print(" d");
  }
  
  // Lokator (zawsze) - wyznacza pozycje na mapie i filtr MQTT
  tft.setTextColor(pskActiveField == PSK_FIELD_LOCATOR ? TFT_YELLOW : TFT_WHITE);
  tft.setCursor(60, 180);
  tft.print("Lok:");
  tft.fillRect(100, 175, 90, 20, pskActiveField == PSK_FIELD_LOCATOR ? TFT_DARKGREY : TFT_BLACK);
  tft.setCursor(105, 180);
  tft.print(pskTempLocator.length() > 0 ? pskTempLocator.c_str() : "(brak)");

  // MQTT Callsign (tylko gdy MQTT) - prawa strona tego samego wiersza
  if (pskTempMqttEnabled) {
    tft.setTextColor(pskActiveField == PSK_FIELD_MQTT_CALL ? TFT_YELLOW : TFT_WHITE);
    tft.setCursor(240, 180);
    tft.print("MQTT Znak:");
    tft.fillRect(310, 175, 110, 20, pskActiveField == PSK_FIELD_MQTT_CALL ? TFT_DARKGREY : TFT_BLACK);
    tft.setCursor(315, 180);
    String mc = pskTempMqttCallsign.length() > 8 ? pskTempMqttCallsign.substring(0, 8) : pskTempMqttCallsign;
    tft.print(mc.length() > 0 ? mc.c_str() : "(wszystkie)");
  }
  
  // Przyciski
  // Zapisz
  tft.fillRect(100, 210, 80, 30, TFT_GREEN);
  tft.drawRect(100, 210, 80, 30, TFT_WHITE);
  tft.setTextColor(TFT_BLACK);
  tft.setCursor(115, 220);
  tft.print("ZAPISZ");
  
  // Anuluj
  tft.fillRect(300, 210, 80, 30, TFT_RED);
  tft.drawRect(300, 210, 80, 30, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(310, 220);
  tft.print("ANULUJ");
  
  // PodpowiedĹş
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(60, 195);
  tft.print("Dotknij pola aby edytowac, ZAPISZ aby zastosowac");
  
  // JeĹ›li aktywna klawiatura - narysuj jÄ…
  if (pskActiveField == PSK_FIELD_KEYBOARD) {
    drawPskKeyboard();
  }
  // JeĹ›li wybor pasma - narysuj selektor
  else if (pskActiveField == PSK_FIELD_BAND_SELECT) {
    drawPskBandSelector();
  }
  // JeĹ›li wybor trybu - narysuj selektor
  else if (pskActiveField == PSK_FIELD_MODE_SELECT) {
    drawPskModeSelector();
  }
}

// Funkcja zwracajÄ…ca kolor linii zaleĹĽny od odlegĹ‚oĹ›ci DX
static uint16_t getDxColor(float distance) {
  if (distance < 500) return TFT_GREEN;      // Bardzo blisko
  else if (distance < 2000) return TFT_YELLOW; // Europa / Ĺšredni dystans
  else if (distance < 5000) return TFT_ORANGE; // Daleko
  else return TFT_MAGENTA;                     // DX (Transatlantyki itp.)
}

// Rysowanie legendy
static void drawPskMapLegend() {
  int y = 300, x = 10, boxSize = 8, gap = 45;
  tft.setTextSize(1);
  const char* bands[] = {"80m", "40m", "20m", "15m", "10m", "6m"};
  uint16_t colors[] = {TFT_RED, TFT_ORANGE, TFT_GREEN, TFT_CYAN, TFT_BLUE, TFT_MAGENTA};
  for (int i = 0; i < 6; i++) {
    tft.fillRect(x + i*gap, y, boxSize, boxSize, colors[i]);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(x + i*gap + 12, y);
    tft.print(bands[i]);
  }
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(300, y);
  tft.print("Tap: Refresh");
}

// GĹ‚Ăłwna funkcja rysujÄ…ca
void drawPskMap() {
  Serial.printf("[PSK] drawPskMap start, pskMapMenuOpen=%d\n", pskMapMenuOpen);
  if (!tftInitialized) return;
  tft.fillScreen(TFT_BLACK);
  Serial.println("[PSK] Screen cleared");
  bool mapLoaded = false;
  if (littleFsReady) {
    // Obsluz zarowno 24-bit BMP, jak i 16-bit RGB565 BMP (ten drugi generuje
    // konwerter w WWW - patrz /upload/mapa565).
    mapLoaded = drawBmpFromFS(PSK_MAP_BMP_PATH, MAP_DISPLAY_X, MAP_DISPLAY_Y)
             || drawBmp16FromFS(PSK_MAP_BMP_PATH, MAP_DISPLAY_X, MAP_DISPLAY_Y);
  }
  if (!mapLoaded) {
    tft.fillRect(MAP_DISPLAY_X, MAP_DISPLAY_Y, MAP_DISPLAY_W, MAP_DISPLAY_H, 0x0011);
    tft.fillRect(220, 80, 80, 100, 0x2222);
    tft.fillRect(200, 140, 100, 80, 0x2222);
    tft.fillRect(300, 60, 120, 80, 0x2222);
    tft.fillRect(60, 50, 100, 90, 0x2222);
    tft.fillRect(80, 150, 70, 90, 0x2222);
    tft.fillRect(380, 160, 60, 40, 0x2222);
  }
  unsigned long now = millis();
  
  // Wybierz tryb pracy: MQTT lub HTTP
  if (pskMqttEnabled) {
    // Tryb MQTT - loop obsĹ‚uguje wszystko w tle
    loopPskMqtt();
  } else {
    // Tryb HTTP - auto-odĹ›wieĹĽanie PSKReporter.
    // Pierwszy fetch zawsze (takze gdy auto-refresh wylaczone) - inaczej ekran
    // zostaje pusty az do recznego tapniecia. Kolejne tylko po interwale.
    // Bez tego "0 spotow" ponawialo zapytanie przy kazdym redrawie (burza/429),
    // a w trakcie backoffu po 429 nie pytamy wcale.
    bool neverFetched = (lastPskFetchMs == 0);
    bool intervalElapsed = (pskAutoRefreshMinutes > 0) &&
                           (now - lastPskFetchMs > (unsigned long)pskAutoRefreshMinutes * 60 * 1000);
    if (millis() >= pskBackoffUntilMs && (neverFetched || intervalElapsed)) {
      fetchPskReporterData();
      lastPskFetchMs = now;
    }
  }
  drawPskMapHeader();
  // Rysowanie linii od pozycji uĹĽytkownika do stacji
  Serial.printf("[PSK] drawPskMap START: userLat=%.4f, userLon=%.4f, valid=%d, locator='%s'\n",
                userLat, userLon, userLatLonValid, userLocator.c_str());
  // Pozycja uzytkownika: jesli nie ma bezposrednich LAT/LON, wylicz z lokatora.
  // Bez tego fallbacku linie nigdy sie nie rysowaly (kazdy inny ekran go ma).
  double pskUserLat = userLat;
  double pskUserLon = userLon;
  bool pskUserHasPos = userLatLonValid;
  if (!pskUserHasPos && userLocator.length() >= 4) {
    locatorToLatLon(userLocator, pskUserLat, pskUserLon);
    pskUserHasPos = true;
  }

  int userX = 0, userY = 0;
  bool userOnMap = false;
  if (pskUserHasPos && pskUserLat >= MAP_LAT_MIN && pskUserLat <= MAP_LAT_MAX &&
      pskUserLon >= MAP_LON_MIN && pskUserLon <= MAP_LON_MAX) {
    latLonToScreen(pskUserLat, pskUserLon, userX, userY);
    userOnMap = true;
    Serial.printf("[PSK] User on map: X=%d, Y=%d\n", userX, userY);
  }

  for (int i = 0; i < pskSpotCount; i++) {
    if (pskSpots[i].lat >= MAP_LAT_MIN && pskSpots[i].lat <= MAP_LAT_MAX &&
        pskSpots[i].lon >= MAP_LON_MIN && pskSpots[i].lon <= MAP_LON_MAX) {
      int x, y;
      latLonToScreen(pskSpots[i].lat, pskSpots[i].lon, x, y);
      uint16_t color = getPskBandColor(pskSpots[i].band);
      
      // Rysuj liniÄ™ od uĹĽytkownika do stacji jeĹ›li oba sÄ… na mapie
      // Kolor zaleĹĽy od odlegĹ‚oĹ›ci DX
      if (userOnMap) {
        float dist = calculateDistance(pskUserLat, pskUserLon, pskSpots[i].lat, pskSpots[i].lon);
        uint16_t lineColor = getDxColor(dist);
        tft.drawLine(userX, userY, x, y, lineColor);
      }
      
      tft.fillCircle(x, y, 3, color);
      tft.drawCircle(x, y, 3, TFT_BLACK);
    }
  }

  // Rysuj czerwonÄ… kropkÄ™ na pozycji uĹĽytkownika (na wierzchu)
  if (userOnMap) {
    Serial.printf("[PSK] Drawing RED DOT at X=%d, Y=%d\n", userX, userY);
    tft.fillCircle(userX, userY, 3, TFT_RED);
    tft.drawCircle(userX, userY, 4, TFT_WHITE);
  } else {
    Serial.printf("[PSK] NO DOT: hasPos=%d, locator='%s', lat=%.2f (%.1f to %.1f), lon=%.2f (%.1f to %.1f)\n",
                  pskUserHasPos, userLocator.c_str(), pskUserLat, MAP_LAT_MIN, MAP_LAT_MAX, pskUserLon, MAP_LON_MIN, MAP_LON_MAX);
  }
  drawPskMapLegend();
  drawSwitchScreenFooter();
  // Przycisk menu w lewym gĂłrnym rogu
  drawPskMenuButton();
  // JeĹ›li menu otwarte, narysuj je na wierzchu
  if (pskMapMenuOpen) {
    Serial.println("[PSK] Drawing settings menu");
    drawPskSettingsMenu();
  }
  Serial.println("[PSK] drawPskMap end");
}

// ObsĹ‚uga dotyku klawiatury
static void handlePskKeyboardTouch(uint16_t x, uint16_t y) {
  int keyW = 32, keyH = 22, gap = 4;

  if (x >= 250 && x < 335 && y >= 278 && y < 306) {
    if (pskKeyboardTarget == 1) {
      pskTempReceiver = pskKeyboardBuffer;
      pskTempReceiver.trim();
      pskTempReceiver.toUpperCase();
    } else if (pskKeyboardTarget == 2) {
      pskTempMaxSpots = pskKeyboardBuffer.toInt();
      if (pskTempMaxSpots < 10) pskTempMaxSpots = 10;
      if (pskTempMaxSpots > 100) pskTempMaxSpots = 100;
    } else if (pskKeyboardTarget == 3) {
      pskTempMqttServer = pskKeyboardBuffer;
      pskTempMqttServer.trim();
      if (pskTempMqttServer.length() == 0) {
        pskTempMqttServer = "mqtt.pskreporter.info"; // default
      }
    } else if (pskKeyboardTarget == 4) {
      pskTempMqttCallsign = pskKeyboardBuffer;
      pskTempMqttCallsign.trim();
      pskTempMqttCallsign.toUpperCase();
    } else if (pskKeyboardTarget == 5) {
      pskTempLocator = pskKeyboardBuffer;
      pskTempLocator.trim();
      pskTempLocator.toUpperCase();
    }
    pskActiveField = PSK_FIELD_NONE;
    pskKeyboardBuffer = "";
    pskKeyboardActive = false;  // Odblokuj nawigacjÄ™
    drawPskSettingsMenu();
    return;
  }

  if (x >= 350 && x < 435 && y >= 278 && y < 306) {
    pskActiveField = PSK_FIELD_NONE;
    pskKeyboardBuffer = "";
    pskKeyboardActive = false;  // Odblokuj nawigacjÄ™
    drawPskSettingsMenu();
    return;
  }

  if (x >= 330 && x < 388 && y >= 128 && y < 156) {
    if (pskKeyboardBuffer.length() > 0) {
      pskKeyboardBuffer.remove(pskKeyboardBuffer.length() - 1);
      drawPskKeyboard();
    }
    return;
  }

  if (x >= 396 && x < 450 && y >= 128 && y < 156) {
    pskKeyboardBuffer = "";
    drawPskKeyboard();
    return;
  }

  const char* rows[] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM.-/:"};
  int startX[] = {40, 40, 56, 40};
  int startY[] = {164, 190, 216, 242};

  for (int r = 0; r < 4; r++) {
    for (int i = 0; i < strlen(rows[r]); i++) {
      int keyX = startX[r] + i * (keyW + gap);
      int keyY = startY[r];
      if (x >= keyX && x < keyX + keyW && y >= keyY && y < keyY + keyH) {
        if (pskKeyboardBuffer.length() < 36) {
          pskKeyboardBuffer += rows[r][i];
          drawPskKeyboard();
        }
        return;
      }
    }
  }
}

// ObsĹ‚uga dotyku selektora pasm
static void handlePskBandSelectorTouch(uint16_t x, uint16_t y) {
  const char* bands[] = {"160m", "80m", "40m", "30m", "20m", "17m", "15m", "12m", "10m", "6m", "ALL"};
  int bandCount = 11;
  int tileW = 60, tileH = 30, gap = 8;
  int startX = 60, startY = 120;
  int cols = 5;
  
  // SprawdĹş przyciski pasm
  for (int i = 0; i < bandCount; i++) {
    int col = i % cols;
    int row = i / cols;
    int bx = startX + col * (tileW + gap);
    int by = startY + row * (tileH + gap);
    
    if (x >= bx && x < bx + tileW && y >= by && y < by + tileH) {
      if (strcmp(bands[i], "ALL") == 0) {
        pskTempBand = "";
      } else {
        pskTempBand = bands[i];
      }
      pskActiveField = PSK_FIELD_NONE;
      drawPskSettingsMenu();
      return;
    }
  }
  
  // Zamknij (200,200,80,30)
  if (x >= 200 && x < 280 && y >= 200 && y < 230) {
    pskActiveField = PSK_FIELD_NONE;
    drawPskSettingsMenu();
  }
}

// ObsĹ‚uga dotyku selektora trybĂłw
static void handlePskModeSelectorTouch(uint16_t x, uint16_t y) {
  const char* modes[] = {"FT8", "FT4", "JS8", "PSK31", "PSK63", "RTTY", "CW", "SSB", "ALL"};
  int modeCount = 9;
  int tileW = 70, tileH = 30, gap = 10;
  int startX = 60, startY = 120;
  int cols = 4;
  
  // SprawdĹş przyciski trybĂłw
  for (int i = 0; i < modeCount; i++) {
    int col = i % cols;
    int row = i / cols;
    int mx = startX + col * (tileW + gap);
    int my = startY + row * (tileH + gap);
    
    if (x >= mx && x < mx + tileW && y >= my && y < my + tileH) {
      if (strcmp(modes[i], "ALL") == 0) {
        pskTempMode = "";
      } else {
        pskTempMode = modes[i];
      }
      pskActiveField = PSK_FIELD_BAND_SELECT; // OtwĂłrz wybĂłr pasma po wyborze trybu
      drawPskSettingsMenu();
      return;
    }
  }
  
  // Zamknij (200,200,80,30)
  if (x >= 200 && x < 280 && y >= 200 && y < 230) {
    pskActiveField = PSK_FIELD_NONE;
    drawPskSettingsMenu();
  }
}

// ObsĹ‚uga dotyku - gĹ‚Ăłwna funkcja
void handlePskMapTouch(uint16_t x, uint16_t y) {
  // ObsĹ‚uga menu jeĹ›li otwarte
  if (pskMapMenuOpen) {
    // JeĹ›li aktywna klawiatura
    if (pskActiveField == PSK_FIELD_KEYBOARD) {
      handlePskKeyboardTouch(x, y);
      return;
    }
    // JeĹ›li aktywny selektor pasm
    if (pskActiveField == PSK_FIELD_BAND_SELECT) {
      handlePskBandSelectorTouch(x, y);
      return;
    }
    // JeĹ›li aktywny selektor trybĂłw
    if (pskActiveField == PSK_FIELD_MODE_SELECT) {
      handlePskModeSelectorTouch(x, y);
      return;
    }
    
    // SprawdĹş czy dotkniÄ™to przycisk ZAPISZ (100,210,80,30)
    Serial.printf("[PSK] Checking ZAPISZ button at x=%d, y=%d, pskMapMenuOpen=%d\n", x, y, pskMapMenuOpen);
    if (x >= 100 && x < 180 && y >= 210 && y < 240) {
      Serial.println("[PSK] ZAPISZ button pressed!");
      // Zapisz ustawienia
      pskReceiverCallsign = pskTempReceiver;
      pskFilterBand = pskTempBand;
      pskFilterMode = pskTempMode;
      pskMaxSpots = pskTempMaxSpots;
      pskReportDays = pskTempReportDays;
      pskMqttEnabled = pskTempMqttEnabled;
      pskMqttServer = pskTempMqttServer;
      pskMqttCallsign = pskTempMqttCallsign;
      // Lokator z menu dotykowego jest tym samym ustawieniem co w WWW (Stacja).
      // Gdy nie podano wprost LAT/LON, przelicz z niego pozycje - inaczej mapa
      // PSK nie wie, gdzie jest uzytkownik (i filtr MQTT nie ma kwadratu).
      bool locatorChanged = (pskTempLocator != userLocator);
      userLocator = pskTempLocator;
      if (locatorChanged && !userLatLonValid && userLocator.length() >= 4) {
        updateUserLatLonFromLocator();
      }
      Serial.printf("[PSK] Settings: receiver=%s, band=%s, mode=%s, max=%d, mqtt=%d\n", 
                    pskReceiverCallsign.c_str(), pskFilterBand.c_str(), 
                    pskFilterMode.c_str(), pskMaxSpots, pskMqttEnabled);
      // Zapisz do NVS
      preferences->putString("psk_receiver", pskReceiverCallsign);
      preferences->putString("psk_band", pskFilterBand);
      preferences->putString("psk_mode", pskFilterMode);
      preferences->putInt("psk_maxspots", pskMaxSpots);
      preferences->putInt("psk_report_days", pskReportDays);
      preferences->putBool("psk_mqtt_en", pskMqttEnabled);
      preferences->putString("psk_mqtt_srv", pskMqttServer);
      preferences->putString("psk_mqtt_call", pskMqttCallsign);
      preferences->putString("user_locator", userLocator);
      if (locatorChanged && !userLatLonValid && userLocator.length() >= 4) {
        preferences->putFloat("user_lat", (float)userLat);
        preferences->putFloat("user_lon", (float)userLon);
        preferences->putBool("user_ll_ok", userLatLonValid);
      }
      pskMapMenuOpen = false;
      pskActiveField = PSK_FIELD_NONE;
      Serial.printf("[PSK] pskMapMenuOpen set to: %d\n", pskMapMenuOpen);
      Serial.println("[PSK] Closing menu and redrawing map...");
      // Wymus natychmiastowe odswiezenie po zapisaniu ustawien (z poszanowaniem
      // backoffu po 429). Nie polegamy na bramce drawPskMap, bo przy
      // auto-refresh=0 zaleznosc od interwalu blokowalaby pobranie danych.
      Serial.printf("[PSK] Before drawPskMap, pskMapMenuOpen=%d\n", pskMapMenuOpen);
      if (millis() >= pskBackoffUntilMs) {
        fetchPskReporterData();
        lastPskFetchMs = millis();
      }
      drawPskMap();
      Serial.printf("[PSK] After drawPskMap, pskMapMenuOpen=%d\n", pskMapMenuOpen);
      Serial.println("[PSK] Map redrawn, menu should be closed");
      // WyĹ›wietl potwierdzenie zapisu
      tft.fillRect(140, 130, 200, 40, TFT_GREEN);
      tft.drawRect(140, 130, 200, 40, TFT_WHITE);
      tft.setTextColor(TFT_BLACK);
      tft.setTextSize(2);
      tft.setCursor(170, 143);
      tft.print("ZAPISANO!");
      delay(1000);
      drawPskMap();  // OdĹ›wieĹĽ ponownie aby usunÄ…Ä‡ napis
      return;
    }
    // SprawdĹş czy dotkniÄ™to przycisk ANULUJ (300,210,80,30)
    if (x >= 300 && x < 380 && y >= 210 && y < 240) {
      Serial.println("[PSK] ANULUJ button pressed!");
      pskMapMenuOpen = false;
      pskActiveField = PSK_FIELD_NONE;
      drawPskMap();
      return;
    }
    // SprawdĹş czy dotkniÄ™to pole ZNAK (120,85,100,20) - otwĂłrz klawiaturÄ™
    if (x >= 120 && x < 220 && y >= 85 && y < 105) {
      pskKeyboardTarget = 1; // 1 = znak
      pskKeyboardBuffer = pskTempReceiver;
      pskActiveField = PSK_FIELD_KEYBOARD;
      pskKeyboardActive = true;  // Blokuj nawigacjÄ™
      drawPskSettingsMenu();
      return;
    }
    // SprawdĹş czy dotkniÄ™to pole PASMO (300,85,80,20) - otwĂłrz selektor pasm
    if (x >= 300 && x < 380 && y >= 85 && y < 105) {
      pskActiveField = PSK_FIELD_BAND_SELECT;
      drawPskSettingsMenu();
      return;
    }
    // SprawdĹş czy dotkniÄ™to pole TRYB (120,115,100,20) - otwĂłrz selektor trybĂłw
    if (x >= 120 && x < 220 && y >= 115 && y < 135) {
      pskActiveField = PSK_FIELD_MODE_SELECT;
      drawPskSettingsMenu();
      return;
    }
    // SprawdĹş czy dotkniÄ™to pole MAX (300,115,50,20) - otwĂłrz klawiaturÄ™
    if (x >= 300 && x < 350 && y >= 115 && y < 135) {
      pskKeyboardTarget = 2; // 2 = max
      pskKeyboardBuffer = String(pskTempMaxSpots);
      pskActiveField = PSK_FIELD_KEYBOARD;
      pskKeyboardActive = true;  // Blokuj nawigacjÄ™
      drawPskSettingsMenu();
      return;
    }
    // SprawdĹş czy dotkniÄ™to pole TRYB MQTT (120,145,100,20)
    if (x >= 120 && x < 220 && y >= 145 && y < 165) {
      pskTempMqttEnabled = !pskTempMqttEnabled;
      pskActiveField = PSK_FIELD_NONE;
      drawPskSettingsMenu();
      return;
    }
    // SprawdĹş czy dotkniÄ™to pole DNI (290,145,60,20) - cykl 1/2/3
    if (!pskTempMqttEnabled && x >= 290 && x < 350 && y >= 145 && y < 165) {
      // Cykl 1..30 dni
      pskTempReportDays++;
      if (pskTempReportDays > 30) pskTempReportDays = 1;
      if (pskTempReportDays < 1) pskTempReportDays = 1;
      drawPskSettingsMenu();
      return;
    }
    
    // SprawdĹş czy dotkniÄ™to pole MQTT SERWER (300,145,120,20) - tylko gdy MQTT
    if (pskTempMqttEnabled && x >= 300 && x < 420 && y >= 145 && y < 165) {
      pskKeyboardTarget = 3; // 3 = mqtt server
      pskKeyboardBuffer = pskTempMqttServer;
      pskActiveField = PSK_FIELD_KEYBOARD;
      pskKeyboardActive = true;
      drawPskSettingsMenu();
      return;
    }
    
    // SprawdĹş czy dotkniÄ™to pole LOKATOR (100,175,90,20) - otwĂłrz klawiaturÄ™
    if (x >= 100 && x < 190 && y >= 175 && y < 195) {
      pskKeyboardTarget = 5; // 5 = lokator
      pskKeyboardBuffer = pskTempLocator;
      pskActiveField = PSK_FIELD_KEYBOARD;
      pskKeyboardActive = true;
      drawPskSettingsMenu();
      return;
    }

    // SprawdĹş czy dotkniÄ™to pole MQTT ZNAK (310,175,110,20) - tylko gdy MQTT
    if (pskTempMqttEnabled && x >= 310 && x < 420 && y >= 175 && y < 195) {
      pskKeyboardTarget = 4; // 4 = mqtt callsign
      pskKeyboardBuffer = pskTempMqttCallsign;
      pskActiveField = PSK_FIELD_KEYBOARD;
      pskKeyboardActive = true;
      drawPskSettingsMenu();
      return;
    }
    
    // JeĹ›li dotkniÄ™to poza polem menu, zamknij menu
    if (!(x >= 40 && x < 440 && y >= 40 && y < 270)) {
      pskMapMenuOpen = false;
      pskActiveField = PSK_FIELD_NONE;
      drawPskMap();
    }
    return;
  }
  
  // SprawdĹş czy dotkniÄ™to przycisk menu (2,2,30,26)
  if (x >= 2 && x < 32 && y >= 2 && y < 28) {
    Serial.println("[PSK] Menu button pressed - opening menu");
    pskMapMenuOpen = true;
    pskActiveField = PSK_FIELD_NONE;
    // Skopiuj aktualne ustawienia do tymczasowych
    pskTempReceiver = pskReceiverCallsign;
    pskTempBand = pskFilterBand;
    pskTempMode = pskFilterMode;
    pskTempMaxSpots = pskMaxSpots;
    pskTempReportDays = pskReportDays;
    pskTempMqttEnabled = pskMqttEnabled;
    pskTempMqttServer = pskMqttServer;
    pskTempMqttCallsign = pskMqttCallsign;
    pskTempLocator = userLocator;
    drawPskMap();
    return;
  }
  
  // Najpierw sprawdĹş rogi ekranu (przeĹ‚Ä…czanie ekranĂłw) - MUSI byÄ‡ przed mapÄ…!
  const uint16_t cornerY = 280, cornerX = 80;  // Podniesione Y dla mapy 320px
  if (y >= cornerY && x < cornerX) {
    currentScreen = getNextScreenId(currentScreen);
    drawScreen(currentScreen);
    resetTftAutoSwitchTimer();
    return;
  } else if (y >= cornerY && x >= (480 - cornerX)) {
    currentScreen = getPrevScreenId(currentScreen);
    drawScreen(currentScreen);
    resetTftAutoSwitchTimer();
    return;
  }
  
  // DotkniÄ™cie mapy odĹ›wieĹĽa dane (ale tylko jeĹ›li nie w rogach).
  // W trakcie backoffu po 429 nie pytamy API - samo przerysowanie z komunikat.
  if (x >= MAP_DISPLAY_X && x < MAP_DISPLAY_X + MAP_DISPLAY_W &&
      y >= MAP_DISPLAY_Y && y < MAP_DISPLAY_Y + MAP_DISPLAY_H) {
    if (millis() >= pskBackoffUntilMs) {
      fetchPskReporterData();
      lastPskFetchMs = millis();
    }
    drawPskMap();
    return;
  }
}

// ========== ISS PASS TRACKING IMPLEMENTATION ==========

void drawIssIconProcedural(int16_t x, int16_t y, uint32_t color) {
  // x, y to Ĺ›rodek pozycji stacji na mapie/radarze
  // 1. Panele sĹ‚oneczne (lewy i prawy) - dwa pionowe prostokÄ…ty
  tft.fillRect(x - 12, y - 8, 4, 16, TFT_CYAN);
  tft.fillRect(x + 8, y - 8, 4, 16, TFT_CYAN);
  // 2. Kratownica Ĺ‚Ä…czÄ…ca panele - pozioma linia
  tft.drawFastHLine(x - 8, y, 16, TFT_SILVER);
  // 3. ModuĹ‚ centralny (kadĹ‚ub stacji) - maĹ‚y poziomy prostokÄ…t
  tft.fillRect(x - 4, y - 3, 8, 6, color);
  tft.drawPixel(x, y, TFT_RED); // Czerwony punkt Ĺ›rodkowy
}

// Pobiera aktualny zestaw TLE (dane orbitalne) ISS z Celestrak i
// inicjalizuje propagator SGP4 (issSat). Zwraca true przy sukcesie.
// TLE trzeba odĹ›wieĹĽaÄ‡ co jakiĹ› czas (co kilka godzin) - im starszy TLE,
// tym wiÄ™kszy bĹ‚Ä…d propagacji; dla ISS (ktĂłry regularnie koryguje orbitÄ™)
// dokĹ‚adnoĹ›Ä‡ zauwaĹĽalnie spada juĹĽ po 1-2 dniach.
bool fetchIssTLE() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  static WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  // CATNR=25544 to numer NORAD MiÄ™dzynarodowej Stacji Kosmicznej (ISS).
  String url = "https://celestrak.org/NORAD/elements/gp.php?CATNR=25544&FORMAT=TLE";
  http.begin(client, url);
  http.setTimeout(4000);
  http.addHeader("User-Agent", "ESP32-HAM-CLOCK/1.2 (TLE fetch)");
  int httpCode = http.GET();

  bool ok = false;
  if (httpCode == 200) {
    String payload = http.getString();
    // OdpowiedĹş to 3 linie: nazwa satelity, TLE linia 1, TLE linia 2.
    int firstNl = payload.indexOf('\n');
    int secondNl = payload.indexOf('\n', firstNl + 1);
    int thirdNl = payload.indexOf('\n', secondNl + 1);
    if (firstNl > 0 && secondNl > firstNl) {
      String nameLine = payload.substring(0, firstNl);
      String l1 = payload.substring(firstNl + 1, secondNl);
      String l2 = (thirdNl > secondNl) ? payload.substring(secondNl + 1, thirdNl)
                                        : payload.substring(secondNl + 1);
      nameLine.trim();
      l1.trim();
      l2.trim();
      if (l1.startsWith("1 ") && l2.startsWith("2 ") && l1.length() >= 60 && l2.length() >= 60) {
        if (issTleMutex != NULL && xSemaphoreTake(issTleMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
          if (nameLine.length() > 0) {
            nameLine.toCharArray(issTleName, sizeof(issTleName));
          }
          l1.toCharArray(issTleLine1, sizeof(issTleLine1));
          l2.toCharArray(issTleLine2, sizeof(issTleLine2));
          issSat.init(issTleName, issTleLine1, issTleLine2);
          issTleValid = true;
          issForceNextPassRefresh = true;  // swieze TLE - przelicz przeloty od razu
          xSemaphoreGive(issTleMutex);
          ok = true;
          Serial.println("[ISS TLE] Zaktualizowano dane orbitalne (SGP4)");
        }
      } else {
        Serial.println("[ISS TLE] Nieprawidlowy format odpowiedzi");
      }
    } else {
      Serial.println("[ISS TLE] Nieoczekiwany format odpowiedzi (za malo linii)");
    }
  } else {
    Serial.printf("[ISS TLE] HTTP kod: %d (oczekiwano 200)\n", httpCode);
  }
  http.end();
  return ok;
}

void calculateIssTrajectory() {
  // Calculate future ISS ground-track positions, then project each one onto
  // the polar radar (relative to the observer's location) using the SAME

  // azimuth/distance math as the satellite icon in updateIssDynamicDisplay().
  //
  // NOTE: this used to project the points with a world-map Mercator formula
  // (screen X/Y as if displaying a 480x320 world map), which is a totally
  // different coordinate space than the polar radar centered at
  // (issCx, issCy). Drawing those coordinates directly onto the radar
  // produced essentially arbitrary line segments scattered across the
  // screen, and since nothing ever cleared them, they accumulated into an
  // ever-growing scribble every refresh cycle.
  //
  // The lat/lng propagation ALSO used to be a rough sinusoidal guess
  // ("ISS moves ~15 degrees per minute in longitude"), which is roughly 3-4x
  // too fast (real ground-track drift is closer to 4-6 deg/min depending on
  // latitude). That made the projected points shoot past the radar's
  // 5000km range within the very first ~2 minute step, so nothing was ever
  // drawn. Now we use the REAL measured bearing (heading) between the last
  // two actual position fixes plus the reported ground speed, and propagate
  // along a great-circle path - accurate for the next ~15-20 minutes, which
  // is all that matters for a 5000km-range radar.
  const int points = ISS_TRAJECTORY_POINTS;
  const double timeStep = 120.0; // 2 minutes per point
  issTrajectoryEpoch = (double)time(nullptr);
  issTrajectoryStepSec = timeStep;
  const double R = 6371.0;       // Earth radius in km

  double userLatLocal = userLatLonValid ? userLat : 52.40;
  double userLonLocal = userLatLonValid ? userLon : 16.92;
  double home_lat = userLatLocal * DEG_TO_RAD;
  double home_lon = userLonLocal * DEG_TO_RAD;

  if (!issGroundBearingValid) {
    // Not enough data yet (need at least two real fixes) - leave the whole
    // trajectory marked invalid rather than guessing.
    for (int i = 0; i < points; i++) {
      issTrajectoryX[i] = -1;
      issTrajectoryY[i] = -1;
    }
    return;
  }

  // issSpeed is the orbital speed reported by the API (km/h) at ISS's
  // actual altitude; the GROUND-track speed is slightly less (the point
  // directly below traces a smaller circle than the orbit itself), scaled
  // by R_earth / (R_earth + altitude).
  double altitudeRatio = R / (R + issAltitude);
  double groundSpeedKmS = (issSpeed / 3600.0) * altitudeRatio;
  double bearingRad = issGroundBearingDeg * DEG_TO_RAD;
  double lat1 = issLat * DEG_TO_RAD;
  double lon1 = issLng * DEG_TO_RAD;

  for (int i = 0; i < points; i++) {
    double futureTime = timeStep * (i + 1); // sekundy w przyszĹ‚oĹ›Ä‡
    double d = groundSpeedKmS * futureTime;  // km przebyte
    double angularDist = d / R;              // radiany

    // Standardowy wzĂłr geodezyjny "punkt docelowy dla danego startu,
    // namiaru i odlegĹ‚oĹ›ci" (great-circle destination point).
    double lat2 = asin(sin(lat1) * cos(angularDist) +
                        cos(lat1) * sin(angularDist) * cos(bearingRad));
    double lon2 = lon1 + atan2(sin(bearingRad) * sin(angularDist) * cos(lat1),
                                cos(angularDist) - sin(lat1) * sin(lat2));

    double futLat = lat2 * RAD_TO_DEG;
    double futLng = lon2 * RAD_TO_DEG;
    while (futLng > 180.0) futLng -= 360.0;
    while (futLng < -180.0) futLng += 360.0;
    if (futLat > 90.0) futLat = 90.0;
    if (futLat < -90.0) futLat = -90.0;

    issTrajectoryLat[i] = futLat;
    issTrajectoryLng[i] = futLng;

    // Distance + bearing from the observer to this future ground-track
    // point (same Haversine/bearing formulas used for the live ISS
    // position in fetchIssData()).
    double p_lat = futLat * DEG_TO_RAD;
    double p_lon = futLng * DEG_TO_RAD;
    double dLat = p_lat - home_lat;
    double dLon = p_lon - home_lon;
    double a = sin(dLat / 2) * sin(dLat / 2) +
               cos(home_lat) * cos(p_lat) * sin(dLon / 2) * sin(dLon / 2);
    double c = 2 * atan2(sqrt(a), sqrt(1 - a));
    double dist = R * c;

    double y = sin(dLon) * cos(p_lat);
    double x = cos(home_lat) * sin(p_lat) - sin(home_lat) * cos(p_lat) * cos(dLon);
    double az = atan2(y, x) * RAD_TO_DEG;
    if (az < 0) az += 360;

    if (dist <= 5000.0) {
      // Same polar projection as the satellite icon: distance -> radius,
      // azimuth -> angle around the radar (0Â° = North, up).
      double pixelDist = dist * (issRMax / 5000.0);
      double angleRad = (az - 90.0) * DEG_TO_RAD;
      issTrajectoryX[i] = issCx + (int)(cos(angleRad) * pixelDist);
      issTrajectoryY[i] = issCy + (int)(sin(angleRad) * pixelDist);
    } else {
      // Out of radar range - mark invalid so the draw loop stops here
      // instead of plotting it somewhere meaningless.
      issTrajectoryX[i] = -1;
      issTrajectoryY[i] = -1;
    }
  }
}

// Uwaga: nie potrzebujemy konwersji na dzieĹ„ juliaĹ„ski - Sgp4::findsat()
// ma przeciÄ…ĹĽenie przyjmujÄ…ce bezpoĹ›rednio unix time (unsigned long),
// wiÄ™c uĹĽywamy go wprost.

// Prawdziwa trajektoria ISS z propagacji orbity (SGP4) zamiast przybliĹĽenia
// namiar+prÄ™dkoĹ›Ä‡ - dokĹ‚adna tak dĹ‚ugo jak aktualny jest pobrany TLE
// (patrz fetchIssTLE(), odĹ›wieĹĽane co ISS_TLE_REFRESH_MS).
void calculateIssTrajectorySGP4() {
  if (issTleMutex == NULL || xSemaphoreTake(issTleMutex, pdMS_TO_TICKS(200)) != pdTRUE) {
    return; // nie blokuj - sprĂłbujemy w nastÄ™pnym cyklu
  }
  if (!issTleValid) {
    xSemaphoreGive(issTleMutex);
    for (int i = 0; i < ISS_TRAJECTORY_POINTS; i++) {
      issTrajectoryX[i] = -1;
      issTrajectoryY[i] = -1;
    }
    return;
  }

  double userLatLocal = userLatLonValid ? userLat : 52.40;
  double userLonLocal = userLatLonValid ? userLon : 16.92;
  issSat.site(userLatLocal, userLonLocal, 0.0); // wysokoĹ›Ä‡ obserwatora ~pomijalna dla tej odlegĹ‚oĹ›ci

  time_t nowEpoch = time(nullptr);
  const double timeStepSec = 60.0; // co 1 minutÄ™ - SGP4 jest tani obliczeniowo, wiÄ™c gÄ™Ĺ›ciej niĹĽ poprzednio
  issTrajectoryEpoch = (double)nowEpoch;
  issTrajectoryStepSec = timeStepSec;

  for (int i = 0; i < ISS_TRAJECTORY_POINTS; i++) {
    time_t t = nowEpoch + (time_t)(timeStepSec * (i + 1));
    issSat.findsat((unsigned long)t);
    issTrajectoryLat[i] = issSat.satLat;
    issTrajectoryLng[i] = issSat.satLon;

    double dist = issSat.satDist; // km od obserwatora (Sgp4 liczy to samo od razu, po site())
    double az = issSat.satAz;     // stopnie

    if (dist <= 5000.0) {
      double pixelDist = dist * (issRMax / 5000.0);
      double angleRad = (az - 90.0) * DEG_TO_RAD;
      issTrajectoryX[i] = issCx + (int)(cos(angleRad) * pixelDist);
      issTrajectoryY[i] = issCy + (int)(sin(angleRad) * pixelDist);
    } else {
      issTrajectoryX[i] = -1;
      issTrajectoryY[i] = -1;
    }

    // Dodatkowy margines bezpieczeĹ„stwa przeciw watchdogowi - 45 wywoĹ‚aĹ„
    // SGP4 to znacznie mniej niĹĽ w skanowaniu przelotu, ale to i tak
    // dodatkowe obciÄ…ĹĽenie w tym samym 5-sekundowym cyklu co pobieranie
    // pozycji przez HTTPS.
    if (i % 15 == 14) {
      vTaskDelay(1);
    }
  }

  xSemaphoreGive(issTleMutex);
}

// Pobiera z n2yo.com przeloty WIDOCZNE (visualpasses: ISS oswietlony sloncem,
// a obserwator w ciemnosci) i dopasowuje je do listy przelotow radiowych po
// czasie AOS. Uzupelnia issPassVisStart/VisEnd/Duration/Mag. Gdy sie nie uda,
// zostaja zera - UI pokaze "widoczny: --". Wywolywane po udanym radiopasses.
static void fetchIssVisualPassesFromN2yo() {
  if (n2yoApiKey.length() == 0 || WiFi.status() != WL_CONNECTED) return;

  double userLatLocal = userLatLonValid ? userLat : 52.40;
  double userLonLocal = userLatLonValid ? userLon : 16.92;

  static WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = "https://api.n2yo.com/rest/v1/satellite/visualpasses/25544/" +
               String(userLatLocal, 4) + "/" + String(userLonLocal, 4) + "/0/2/0/" +
               "&apiKey=" + n2yoApiKey;
  http.begin(client, url);
  http.setTimeout(4000);
  int httpCode = http.GET();
  if (httpCode != 200) {
    Serial.printf("[ISS PASS n2yo] visualpasses HTTP kod: %d\n", httpCode);
    http.end();
    return;
  }

  DynamicJsonDocument filter(512);
  filter["passes"][0]["startUTC"] = true;
  filter["passes"][0]["startVisibilityUTC"] = true;
  filter["passes"][0]["endUTC"] = true;
  filter["passes"][0]["duration"] = true;
  filter["passes"][0]["mag"] = true;

  DynamicJsonDocument doc(3072);
  WiFiClient *vstream = http.getStreamPtr();
  DeserializationError err = deserializeJson(doc, *vstream, DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Serial.printf("[ISS PASS n2yo] visualpasses blad JSON: %s\n", err.c_str());
    return;
  }

  for (int i = 0; i < ISS_MAX_PASSES; i++) {
    issPassVisStart[i] = 0; issPassVisEnd[i] = 0; issPassDuration[i] = 0; issPassMag[i] = 0.0;
  }
  JsonArray vp = doc["passes"].as<JsonArray>();
  int matched = 0;
  for (int i = 0; i < issPassCount; i++) {
    time_t aos = issPassAos[i];
    if (aos <= 0) continue;
    for (JsonObject p : vp) {
      long ps = p["startUTC"] | 0L;   // AOS przelotu (do dopasowania)
      if (ps <= 0) continue;
      // Ten sam przelot: n2yo liczy visual/radio z tego samego TLE, ale
      // progi elewacji moga dac kilka minut roznicy - tolerancja 3 min.
      long diff = (long)ps - (long)aos;
      if (diff < 0) diff = -diff;
      if (diff <= 180) {
        // startVisibilityUTC = moment, gdy ISS staje sie widoczny optycznie
        // (moze byc pozniej niz AOS, gdy satelita jest jeszcze w cieniu).
        long vs = p["startVisibilityUTC"] | 0L;
        issPassVisStart[i] = (vs > 0) ? (time_t)vs : (time_t)ps;
        long ve = p["endUTC"] | 0L;
        issPassVisEnd[i] = (ve > 0) ? (time_t)ve : 0;
        issPassDuration[i] = p["duration"] | 0;
        issPassMag[i] = (double)(p["mag"] | 0.0f);
        matched++;
        break;
      }
    }
  }
  Serial.printf("[ISS PASS n2yo] Widocznosc dopasowana do %d przelot(ow)\n", matched);
}

// Pobiera czas nastepnego przelotu ISS z n2yo.com (radiopasses) i uzupelnia
// liste przelotow. To DUZO prostsze i bardziej niezawodne niz liczenie tego
// samemu przez SGP4 na ESP32 - przewidywanie robi ich serwer. Po sukcesie
// dociaga tez widocznosc (visualpasses). Zwraca true przy sukcesie.
bool fetchIssNextPassFromN2yo() {
  if (n2yoApiKey.length() == 0 || WiFi.status() != WL_CONNECTED) {
    return false;
  }

  double userLatLocal = userLatLonValid ? userLat : 52.40;
  double userLonLocal = userLatLonValid ? userLon : 16.92;

  static WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  // radiopasses (nie visualpasses) - nie wymaga, ĹĽeby ISS byĹ‚ oĹ›wietlony
  // sĹ‚oĹ„cem/obserwator w ciemnoĹ›ci, tylko elewacji > min_elevation.
  // Parametry: {id}/{lat}/{lng}/{alt}/{dni}/{min_elevacja}
  String url = "https://api.n2yo.com/rest/v1/satellite/radiopasses/25544/" +
               String(userLatLocal, 4) + "/" + String(userLonLocal, 4) + "/0/2/5/" +
               "&apiKey=" + n2yoApiKey;
  http.begin(client, url);
  http.setTimeout(4000);
  int httpCode = http.GET();

  bool ok = false;
  if (httpCode == 200) {
    WiFiClient *stream = http.getStreamPtr();

    DynamicJsonDocument filter(256);
    filter["passes"][0]["startUTC"] = true;
    filter["passes"][0]["endUTC"] = true;
    filter["passes"][0]["maxEl"] = true;

    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, *stream, DeserializationOption::Filter(filter));

    if (!err) {
      JsonArray passes = doc["passes"].as<JsonArray>();
      if (passes.size() > 0) {
        // Wypelnij liste kilku przelotow (UI "NASTEPNE PRZELOTY") oraz
        // pojedynczy najblizszy przelot - oba z tego samego API.
        int n = 0;
        for (JsonObject p : passes) {
          if (n >= ISS_MAX_PASSES) break;
          long s = p["startUTC"] | 0L;
          long e = p["endUTC"] | 0L;
          if (s <= 0) continue;
          issPassAos[n] = (time_t)s;
          issPassLos[n] = (e > 0) ? (time_t)e : 0;
          issPassMaxEl[n] = (double)(p["maxEl"] | 0.0f);
          n++;
        }
        issPassCount = n;
        for (int i = n; i < ISS_MAX_PASSES; i++) {
          issPassAos[i] = 0; issPassLos[i] = 0; issPassMaxEl[i] = 0.0;
          issPassVisStart[i] = 0; issPassVisEnd[i] = 0; issPassDuration[i] = 0; issPassMag[i] = 0.0;
        }
        // Widocznosc przelotow (visualpasses) - dopasowana do AOS powyzej.
        fetchIssVisualPassesFromN2yo();

        long startUTC = passes[0]["startUTC"] | 0L;
        long endUTC = passes[0]["endUTC"] | 0L;
        float maxEl = passes[0]["maxEl"] | 0.0f;
        if (startUTC > 0) {
          issNextPassEstimateEpoch = (time_t)startUTC;
          issNextPassAosEpoch = (time_t)startUTC;
          issNextPassLosEpoch = (endUTC > 0) ? (time_t)endUTC : 0;
          issNextPassMaxEl = (double)maxEl;
          issNextPassEstimateValid = true;
          issNextPassWasVisible = false;
          ok = true;
          struct tm passTm;
          time_t t = issNextPassEstimateEpoch;
          localtime_r(&t, &passTm);
          Serial.printf("[ISS PASS n2yo] Nastepny przelot: %02d:%02d (max el %.0f), lacznie %d\n",
                        passTm.tm_hour, passTm.tm_min, maxEl, issPassCount);
        }
      } else {
        issPassCount = 0;
        for (int i = 0; i < ISS_MAX_PASSES; i++) {
          issPassVisStart[i] = 0; issPassVisEnd[i] = 0; issPassDuration[i] = 0; issPassMag[i] = 0.0;
        }
        Serial.println("[ISS PASS n2yo] Brak przelotow w zwroconym oknie (2 dni)");
      }
    } else {
      Serial.printf("[ISS PASS n2yo] Blad parsowania JSON: %s\n", err.c_str());
    }
  } else {
    Serial.printf("[ISS PASS n2yo] HTTP kod: %d (oczekiwano 200)\n", httpCode);
  }
  http.end();
  return ok;
}

// Przewiduje najblizsze przeloty (AOS - wejscie nad horyzont, LOS - zejscie,
// maks. elewacja) uzywajac wbudowanego predyktora biblioteki Sgp4
// (initpredpoint() + nextpass()), a NIE wlasnego skanowania orbity co 60 s.
//
// Dlaczego zmiana: poprzednia wersja recznie skanowala 24 h w krokach 60 s
// (do ~1440 propagacji + bisekcje). Byla poprawna matematycznie, ale (a)
// bardzo droga (SGP4 bez delay() na Core 0 -> ryzyko watchdoga/glodzenia
// sieci) i (b) zawodna przy pierwszym cyklu po starcie, gdy TLE/NTP dopiero
// sie ustabilizowaly. Biblioteczny nextpass() szuka ekstremow elewacji
// metoda Brenta - kilka-kilkanascie iteracji zamiast 1440 - i jest to ten
// sam sprawdzony kod, ktory daje wyniki zgodne z referencyjnymi.
//
// Funkcja wypelnia ZAROWNO pojedynczy "nastepny przelot" (zgodnie z
// istniejacym UI/JSON), JAK I liste issPassAos/Los/MaxEl[ISS_MAX_PASSES],
// dzieki czemu UI moze pokazac kilka kolejnych przelotow.
void updateIssNextPassEstimate() {
  Serial.println("[ISS PASS] Start szukania nastepnych przelotow (nextpass)...");
  if (issTleMutex == NULL || xSemaphoreTake(issTleMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
    Serial.println("[ISS PASS] Nie udalo sie wziac muteksu TLE - pomijam ten cykl");
    return;
  }
  if (!issTleValid) {
    xSemaphoreGive(issTleMutex);
    issNextPassEstimateValid = false;
    issPassCount = 0;
    Serial.println("[ISS PASS] TLE jeszcze niedostepne - brak danych");
    return;
  }

  double userLatLocal = userLatLonValid ? userLat : 52.40;
  double userLonLocal = userLatLonValid ? userLon : 16.92;
  issSat.site(userLatLocal, userLonLocal, 0.0);

  time_t nowEpoch = time(nullptr);
  // Bez synchronizacji NTP time() zwraca czas od startu (rok 1970) - propagacja
  // SGP4 liczylaby sie od TLE (rok 2026) i dawala bezsensowne wyniki. Wymagamy
  // realnej daty (>= 2024-01-01).
  if (nowEpoch < 1704067200) {
    xSemaphoreGive(issTleMutex);
    issNextPassEstimateValid = false;
    issPassCount = 0;
    Serial.printf("[ISS PASS] Brak zsynchronizowanego czasu (NTP) - epoch=%ld, pomijam obliczenia\n", (long)nowEpoch);
    return;
  }

  issSat.findsat((unsigned long)nowEpoch);
  bool currentlyVisible = issSat.satEl > 0;
  Serial.printf("[ISS PASS] Diagnostyka SGP4: satLat=%.4f satLon=%.4f satAz=%.4f satEl=%.4f satDist=%.1f (obserwator: lat=%.4f lon=%.4f)\n",
                issSat.satLat, issSat.satLon, issSat.satAz, issSat.satEl, issSat.satDist,
                userLatLocal, userLonLocal);

  // Gdy ISS jest juz nad horyzontem - najpierw wyznacz koniec BIEZACEGO
  // przelotu (LOS) prostym skanem do przodu (max ~100 min = jeden obieg).
  // To krotkie i ograniczone, w przeciwienstwie do dawnego skanu 24 h.
  time_t currentLosT = 0;
  if (currentlyVisible) {
    time_t prevT = nowEpoch;
    for (int s = 60; s <= 100 * 60; s += 60) {
      time_t t = nowEpoch + s;
      issSat.findsat((unsigned long)t);
      if (issSat.satEl <= 0) {
        time_t lo = prevT, hi = t;
        for (int it = 0; it < 12; it++) {
          time_t mid = lo + (hi - lo) / 2;
          issSat.findsat((unsigned long)mid);
          if (issSat.satEl > 0) lo = mid; else hi = mid;
        }
        currentLosT = hi;
        break;
      }
      prevT = t;
      if (s % (15 * 60) == 0) vTaskDelay(1);
    }
  }

  // Lista przyszlych przelotow z predyktora bibliotecznego. Startujemy od
  // momentu LOS, gdy ISS jest widoczny (zeby nie zwrocic biezacego przelotu
  // jako "nastepnego"), inaczej od teraz.
  time_t aosList[ISS_MAX_PASSES];
  time_t losList[ISS_MAX_PASSES];
  double maxElList[ISS_MAX_PASSES];
  int passCount = 0;

  time_t predStart = (currentlyVisible && currentLosT > 0) ? currentLosT : nowEpoch;
  if (issSat.initpredpoint((unsigned long)predStart, 0.0)) {
    passinfo pinfo;
    for (int i = 0; i < 60 && passCount < ISS_MAX_PASSES; i++) {
      memset(&pinfo, 0, sizeof(pinfo));
      if (!issSat.nextpass(&pinfo, 60, false, 0.0)) break;
      time_t aos = (time_t)getUnixFromJulian(pinfo.jdstart);
      time_t los = (time_t)getUnixFromJulian(pinfo.jdstop);
      if (los < nowEpoch) continue;          // przelot juz miniony - pomijamy
      if (aos < nowEpoch) aos = nowEpoch;    // przyciete do teraz
      aosList[passCount] = aos;
      losList[passCount] = los;
      maxElList[passCount] = pinfo.maxelevation;
      passCount++;
    }
  } else {
    Serial.println("[ISS PASS] initpredpoint nie znalazl startu - brak danych");
  }

  xSemaphoreGive(issTleMutex);

  // Eksport listy przelotow (pod tym samym 4-elementowym ograniczeniem co UI).
  issPassCount = passCount;
  for (int i = 0; i < ISS_MAX_PASSES; i++) {
    issPassAos[i] = (i < passCount) ? aosList[i] : 0;
    issPassLos[i] = (i < passCount) ? losList[i] : 0;
    issPassMaxEl[i] = (i < passCount) ? maxElList[i] : 0.0;
    // SGP4 nie liczy widocznosci (potrzeba oswietlenia Slonca i zmierzchu
    // obserwatora) - zerujemy, UI pokaze "widoczny: --" gdy brak n2yo.
    issPassVisStart[i] = 0; issPassVisEnd[i] = 0; issPassDuration[i] = 0; issPassMag[i] = 0.0;
  }

  // Pojedynczy "nastepny przelot" zgodny z istniejacym UI/JSON.
  if (currentlyVisible) {
    issNextPassAosEpoch = nowEpoch;   // biezacy przelot - AOS w przeszlosci
    issNextPassLosEpoch = currentLosT;
    issNextPassEstimateEpoch = (currentLosT > 0) ? currentLosT : nowEpoch;
    issNextPassEstimateValid = (currentLosT > 0) || (passCount > 0);
    issNextPassWasVisible = true;
    issNextPassMaxEl = (passCount > 0) ? maxElList[0] : 0.0;
    struct tm lt;
    if (currentLosT > 0) {
      localtime_r(&currentLosT, &lt);
      Serial.printf("[ISS PASS] ISS widoczny teraz, LOS %02d:%02d, %d przelot(ow) w planie\n",
                    lt.tm_hour, lt.tm_min, passCount);
    }
  } else if (passCount > 0) {
    issNextPassAosEpoch = aosList[0];
    issNextPassLosEpoch = losList[0];
    issNextPassEstimateEpoch = aosList[0];
    issNextPassEstimateValid = true;
    issNextPassMaxEl = maxElList[0];
    issNextPassWasVisible = false;
    struct tm nt; localtime_r(&aosList[0], &nt);
    Serial.printf("[ISS PASS] Nastepny przelot AOS %02d:%02d (za %ld s, maks. el %.0f), lacznie %d\n",
                  nt.tm_hour, nt.tm_min, (long)(aosList[0] - nowEpoch), maxElList[0], passCount);
  } else {
    issNextPassEstimateValid = false;
    issNextPassWasVisible = false;
    Serial.printf("[ISS PASS] Brak przelotu w planie (lat=%.4f lon=%.4f)\n", userLatLocal, userLonLocal);
  }
}


void fetchIssData() {
  if (WiFi.status() != WL_CONNECTED) {
    // Skip HTTP request but continue - allow existing data to be displayed
    // Do NOT return - this would block the entire ISS data loop
    return;
  }

  double userLatLocal = userLatLonValid ? userLat : 52.40;
  double userLonLocal = userLatLonValid ? userLon : 16.92;

  bool gotPosition = false;
  double newLat = 0, newLng = 0, newAlt = 0, newSpeed = 0;

  // Static WiFiClientSecure - reuse zamiast tworzyc nowy za kazdym razem.
  // Nowy WiFiClientSecure = nowy kontekst TLS (~20KB) za kazdym wywolaniem,
  // co przy krotkim interwale wyczerpywalo pamiec ESP32 i crashowalo.
  static WiFiClientSecure issSecureClient;
  issSecureClient.setInsecure();

  // Jesli klucz n2yo jest dostepny, uzyj ich API do pozycji ISS
  if (n2yoApiKey.length() > 0) {
    HTTPClient http;
    http.setTimeout(4000);
    String url = "https://api.n2yo.com/rest/v1/satellite/positions/25544/" +
                 String(userLatLocal, 4) + "/" + String(userLonLocal, 4) + "/0/1/&apiKey=" + n2yoApiKey;
    Serial.printf("[ISS] Pobieram pozycje z n2yo.com...\n");
    http.begin(issSecureClient, url);
    int httpCode = http.GET();
    if (httpCode == 200) {
      DynamicJsonDocument doc(1024);
      DeserializationError err = deserializeJson(doc, http.getString());
      if (!err && doc.containsKey("positions")) {
        JsonArray positions = doc["positions"].as<JsonArray>();
        if (positions.size() > 0) {
          JsonObject pos = positions[0];
          newLat = pos["satlatitude"] | 0.0;
          newLng = pos["satlongitude"] | 0.0;
          newAlt = pos["sataltitude"] | 0.0;
          gotPosition = true;
          Serial.printf("[ISS] n2yo pozycja: lat=%.4f lng=%.4f alt=%.1f\n", newLat, newLng, newAlt);
        }
      } else {
        Serial.printf("[ISS] n2yo blad parsowania: %s\n", err.c_str() ? err.c_str() : "unknown");
      }
    } else {
      Serial.printf("[ISS] n2yo HTTP kod: %d\n", httpCode);
    }
    http.end();
  }

  // Fallback na wheretheiss.at jesli n2yo nie dal pozycji
  if (!gotPosition) {
    HTTPClient http;
    http.setTimeout(2500);
    http.begin(issSecureClient, "https://api.wheretheiss.at/v1/satellites/25544");
    int httpCode = http.GET();
    if (httpCode == 200) {
      DynamicJsonDocument doc(1024);
      DeserializationError err = deserializeJson(doc, http.getString());
      if (!err) {
        newLat = doc["latitude"];
        newLng = doc["longitude"];
        newAlt = doc["altitude"];
        newSpeed = doc["velocity"];
        gotPosition = true;
        Serial.printf("[ISS] wheretheiss pozycja: lat=%.4f lng=%.4f\n", newLat, newLng);
      }
    }
    http.end();
  }

  if (gotPosition) {
    // Namiar (kierunek lotu) z dwoch kolejnych PRAWDZIWYCH odczytow
    static bool haveIssPrevFix = false;
    if (haveIssPrevFix && (issLat != newLat || issLng != newLng)) {
      double lat1r = issLat * DEG_TO_RAD;
      double lat2r = newLat * DEG_TO_RAD;
      double dLonR = (newLng - issLng) * DEG_TO_RAD;
      if (dLonR > PI) dLonR -= 2 * PI;
      if (dLonR < -PI) dLonR += 2 * PI;
      double by = sin(dLonR) * cos(lat2r);
      double bx = cos(lat1r) * sin(lat2r) - sin(lat1r) * cos(lat2r) * cos(dLonR);
      double bearing = atan2(by, bx) * RAD_TO_DEG;
      if (bearing < 0) bearing += 360;
      issGroundBearingDeg = bearing;
      issGroundBearingValid = true;
    }
    haveIssPrevFix = true;

    issLat = newLat;
    issLng = newLng;
    issAltitude = newAlt;
    if (newSpeed > 0) issSpeed = newSpeed;

    // Trajektoria: preferuj SGP4, fallback na bearing+speed
    if (issTleValid) {
      calculateIssTrajectorySGP4();
    } else {
      calculateIssTrajectory();
    }

    // Haversine - oblicz obserwacje
    double home_lat = userLatLocal * DEG_TO_RAD;
    double iss_lat = issLat * DEG_TO_RAD;
    double home_lon = userLonLocal * DEG_TO_RAD;
    double iss_lon = issLng * DEG_TO_RAD;
    double R = 6371.0;

    double dLat = iss_lat - home_lat;
    double dLon = iss_lon - home_lon;
    double a = sin(dLat/2) * sin(dLat/2) + cos(home_lat) * cos(iss_lat) * sin(dLon/2) * sin(dLon/2);
    double c = 2 * atan2(sqrt(a), sqrt(1-a));
    issDistance = R * c;

    double y = sin(dLon) * cos(iss_lat);
    double x = cos(home_lat) * sin(iss_lat) - sin(home_lat) * cos(iss_lat) * cos(dLon);
    issAzimuth = atan2(y, x) * RAD_TO_DEG;
    if (issAzimuth < 0) issAzimuth += 360;

    double cos_g = sin(home_lat) * sin(iss_lat) + cos(home_lat) * cos(iss_lat) * cos(dLon);
    if (cos_g > 1.0) cos_g = 1.0;
    if (cos_g < -1.0) cos_g = -1.0;
    double r_iss = R + issAltitude;
    double num = r_iss * cos_g - R;
    issElevation = atan2(num, sqrt(r_iss*r_iss * (1 - cos_g*cos_g))) * RAD_TO_DEG;
  }
}

String lookupIssCountryFromCoords(double lat, double lng) {
  // Zwracamy PUSTY string dla PRZEJĹšCIOWYCH niepowodzeĹ„ (brak WiFi, bĹ‚Ä…d
  // sieci, timeout, bĹ‚Ä…d parsowania) - to sygnaĹ‚ dla wywoĹ‚ujÄ…cego "nic siÄ™
  // nie zmieniĹ‚o, zachowaj poprzedniÄ… wartoĹ›Ä‡", zamiast nadpisywaÄ‡ dobrze
  // znanÄ… nazwÄ™ kraju napisem "BRAK DANYCH" przy kaĹĽdej przejĹ›ciowej
  // usterce sieciowej (a przy tylu jednoczesnych poĹ‚Ä…czeniach na tym
  // urzÄ…dzeniu pojedyncze niepowodzenia HTTPS sÄ… caĹ‚kiem normalne - to
  // wĹ‚aĹ›nie powodowaĹ‚o "miganie" miÄ™dzy nazwÄ… kraju a "brak danych").
  // "OCEAN" i realna nazwa kraju to jedyne wyniki, ktĂłre POWINNY nadpisaÄ‡
  // poprzedniÄ… wartoĹ›Ä‡, bo sÄ… wynikiem faktycznie udanego zapytania.
  if (WiFi.status() != WL_CONNECTED) {
    return "";
  }

  // BigDataCloud's free "reverse-geocode-client" endpoint is restricted by
  // their Fair Use Policy to genuine browser/GPS clients only - calls from
  // a server or embedded device (like here, looking up an arbitrary
  // satellite ground-track position rather than the device's own location)
  // get rejected outright with HTTP 400, which is exactly what the serial
  // log showed every single time. OpenStreetMap's Nominatim reverse
  // geocoding API is free, needs no key, and is fine with this kind of use
  // as long as we stay around ~1 request/second and identify ourselves via
  // User-Agent - both easily satisfied here (one lookup every 15s).
  static WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String langParam = (tftLanguage == TFT_LANG_EN) ? "en" : "pl";
  String url = "https://nominatim.openstreetmap.org/reverse?format=json&lat=" +
               String(lat, 4) + "&lon=" + String(lng, 4) +
               "&zoom=3&accept-language=" + langParam;

  http.begin(client, url);
  // 3s timeout, single attempt, no retry - a longer timeout/retry here
  // ended up starving OTHER concurrent connections (weather, callook) of
  // the ESP32's shared, limited pool of sockets/TLS memory, since only one
  // WiFi radio and socket pool is shared across every task on the device.
  // Keeping this call short and single-shot leaves the rest of the system
  // room to breathe; a missed lookup here just tries again in 15s anyway.
  http.setTimeout(3000);
  http.addHeader("User-Agent", "ESP32-HAM-CLOCK/1.2 (ISS pass tracker)");
  int httpCode = http.GET();

  // Trzy rĂłĹĽne, rozrĂłĹĽnialne wyniki:
  // - ""            = przejĹ›ciowe niepowodzenie - zachowaj poprzedniÄ… wartoĹ›Ä‡
  // - "OCEAN"       = zapytanie siÄ™ udaĹ‚o, ale nie ma kraju pod tym punktem
  // - nazwa kraju   = zapytanie siÄ™ udaĹ‚o i znaleziono kraj
  String result;
  if (httpCode == 200) {
    WiFiClient *stream = http.getStreamPtr();

    // Filtr - interesuje nas wyĹ‚Ä…cznie nazwa kraju; reszta odpowiedzi
    // Nominatim (adres, granice, id OSM itd.) i tak jest pomijana, wiÄ™c
    // rozmiar caĹ‚ej odpowiedzi nie ma znaczenia dla maĹ‚ego bufora poniĹĽej.
    DynamicJsonDocument filter(128);
    filter["address"]["country"] = true;
    filter["address"]["country_code"] = true;

    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, *stream, DeserializationOption::Filter(filter));

    if (!error) {
      // Nad otwartym oceanem Nominatim odpowiada poprawnie (HTTP 200), ale
      // bez pola "country" w adresie - to jest faktyczny, potwierdzony ocean.
      String country = doc["address"]["country"] | "";
      String countryCode = doc["address"]["country_code"] | "";
      countryCode.toUpperCase();
      if (country.length() > 0) {
        country.toUpperCase();
        result = country;
        issCountryCode = countryCode; // np. "PL", "DE", "US"
      } else {
        result = "OCEAN";
        issCountryCode = "";
      }
    } else {
      result = ""; // przejĹ›ciowy bĹ‚Ä…d parsowania - zachowaj poprzedniÄ… wartoĹ›Ä‡
      Serial.printf("[ISS COUNTRY] blad parsowania JSON: %s\n", error.c_str());
    }
  } else {
    result = ""; // przejĹ›ciowy bĹ‚Ä…d HTTP - zachowaj poprzedniÄ… wartoĹ›Ä‡
    Serial.printf("[ISS COUNTRY] HTTP kod: %d (oczekiwano 200)\n", httpCode);
  }
  http.end();
  return result;
}

// Kept for compatibility with the forward declaration used elsewhere -
// snapshots the current position under the mutex, performs the (slow,
// network-bound) lookup WITHOUT holding the mutex, then takes it again only
// briefly to store the result. See issTask() for why this split matters.
void getIssCountryFromCoords(double lat, double lng) {
  String country = lookupIssCountryFromCoords(lat, lng);
  if (issDataMutex != NULL && xSemaphoreTake(issDataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    issCountry = country;
    xSemaphoreGive(issDataMutex);
  }
}

// ============================================================================
// EKRAN ISS - uklad wzorowany na stronie WWW "Satelity" (data/satellite.js):
//   - mapa swiata z trajektoria naziemna (zamiast pustego tla),
//   - karty metryk z akcentem (odpowiednik .sat-metric),
//   - karta "najblizszy przelot" + lista "nastepne przeloty" (.sat-pass*),
//   - radar azymut/elewacja (.sat-radar-card).
// Rysowanie odbywa sie do bufora w PSRAM (TFT_eSprite) i jest w calosci
// wypychane na panel jednym pushSprite() - brak migotania, brak resztek
// poprzedniej klatki, brak ręcznego wymazywania linii/ikony.
// ============================================================================

// Paleta zgodna z motywem WWW (CSS var --accent itd.).
static const uint16_t ISS_COL_BG      = 0x0000; // #000000 czarne tlo
static const uint16_t ISS_COL_CARD    = 0x0884; // #0b1220 (wypelnienie karty)
static const uint16_t ISS_COL_BORDER  = 0x298A; // #2a3357 (obramowanie)
static const uint16_t ISS_COL_ACCENT  = 0x3697; // #35d0ba (akcent - teal)
static const uint16_t ISS_COL_ACCENT2 = 0x4D5F; // #4ea8ff (akcent - blue)
static const uint16_t ISS_COL_WARN    = 0xFE4A; // #ffc857 (zloty)
static const uint16_t ISS_COL_TEXT    = 0xEF7E; // #e8eef7
static const uint16_t ISS_COL_DIM     = 0x9D98; // #9fb0c6
static const uint16_t ISS_COL_FAINT   = 0x6BF2; // #6c7d94
static const uint16_t ISS_COL_LAND    = 0x2A47; // #2a4a3c (kontynenty)
static const uint16_t ISS_COL_GRID    = 0x198A; // #1c3350 (siatka mapy)

static const uint16_t ISS_MAP_WATER = 0x00C6; // #021836 wody
static const uint16_t ISS_MAP_RING  = 0x18C3; // pierscienie radaru

// TFT_eSprite dla ekranu ISS - alokowany w PSRAM (16bpp = 307 200 B).
TFT_eSprite issSprite = TFT_eSprite(&tft);
static bool issSpriteReady = false;
static bool issSpriteInitTried = false;

static bool ensureIssSprite() {
  if (issSpriteReady) return true;
  if (issSpriteInitTried) return false;
  issSpriteInitTried = true;
  issSprite.setColorDepth(16);
  issSpriteReady = (issSprite.createSprite(480, 320) != nullptr);
  if (!issSpriteReady) {
    Serial.println("[ISS] Nie udalo sie zaalokowac sprite 480x320 (PSRAM?) - brak ekranu ISS");
  }
  return issSpriteReady;
}

// Karta: ciemne wypelnienie + obramowanie + akcentowy pasek po lewej.
static void issCard(int x, int y, int w, int h, uint16_t accent) {
  issSprite.fillRect(x, y, w, h, ISS_COL_CARD);
  issSprite.drawRect(x, y, w, h, ISS_COL_BORDER);
  issSprite.fillRect(x, y, 3, h, accent);
}

static void issLabel(int x, int y, const char *txt, uint16_t col) {
  issSprite.setTextDatum(TL_DATUM);
  issSprite.setTextSize(1);
  issSprite.setTextColor(col, ISS_COL_CARD);
  issSprite.drawString(txt, x, y, 1);
}

static void issValue(int x, int y, const char *txt, uint16_t col) {
  issSprite.setTextDatum(TL_DATUM);
  issSprite.setTextSize(1);
  issSprite.setTextColor(col, ISS_COL_CARD);
  issSprite.drawString(txt, x, y, 2);
}

// Projekcja rownoodleglosciowa (lon/lat -> piksel) w prostokacie mapy.
static void issMapProject(double lon, double lat, int mx, int my, int mw, int mh,
                          int &px, int &py) {
  px = mx + (int)((lon + 180.0) / 360.0 * mw);
  py = my + (int)((90.0 - lat) / 180.0 * mh);
}

// Rysuje flage kraju (32x21 BMP 24-bit z LittleFS /tft_flags/<CC>.bmp) wprost
// do sprite'a ekranu ISS. Wlasna implementacja zamiast drawBmpFromFS(), bo ta
// ostatnia pisze do globalnego tft (a nie do sprite'a) i zaklada pelnoekranowe
// wspolrzedne. Ten sam uklad bajtow co drawBmpFromFS: wiersze BMP sa od dolu,
// piksele BGR. Zwraca false gdy brak flagi/pliku (UI pokaze sam kod kraju).
static bool issDrawFlagToSprite(const String &cc, int x, int y) {
  if (!littleFsReady || cc.length() != 2) return false;
  String path = "/tft_flags/" + cc + ".bmp";
  if (!LittleFS.exists(path)) return false;
  fs::File f = LittleFS.open(path, "r");
  if (!f) return false;

  if (readBmp16(f) != 0x4D42) { f.close(); return false; }
  readBmp32(f); readBmp32(f);
  uint32_t seekOffset = readBmp32(f);
  readBmp32(f);
  uint16_t w = readBmp32(f);
  uint16_t h = readBmp32(f);
  if (w == 0 || h == 0 || w > 64 || h > 64) { f.close(); return false; }
  if (readBmp16(f) != 1 || readBmp16(f) != 24 || readBmp32(f) != 0) { f.close(); return false; }

  uint16_t padding = (4 - ((w * 3) & 3)) & 3;
  uint16_t lineBuf[64];
  f.seek(seekOffset);
  int drawY = y + h - 1;
  for (uint16_t row = 0; row < h; row++) {
    uint8_t raw[64 * 3 + 3];
    f.read(raw, w * 3 + padding);
    uint8_t *bptr = raw;
    for (uint16_t col = 0; col < w; col++) {
      uint8_t b = *bptr++, g = *bptr++, r = *bptr++;
      lineBuf[col] = tft.color565(r, g, b);
    }
    issSprite.pushImage(x, drawY--, w, 1, lineBuf);
  }
  f.close();
  return true;
}

// Mapa swiata: kontynenty rysowane z wbudowanej bitmapy 1-bit (ISS_WORLDMASK),
// wygenerowanej z data/Mapa swiata.bmp. To odpowiednik mapy z WWW
// (data/satellite.js rysuje ja z /mapa_swiat.bmp), ale bez zaleznosci od
// LittleFS i bez dekodowania obrazu na urzadzeniu.
static void issDrawWorldMap(int mx, int my, int mw, int mh) {
  const int bw = ISS_WORLDMASK_W, bh = ISS_WORLDMASK_H;
  for (int y = 0; y < mh; y++) {
    uint16_t sy = (uint16_t)((uint32_t)y * bh / mh);
    for (int x = 0; x < mw; x++) {
      uint16_t sx = (uint16_t)((uint32_t)x * bw / mw);
      uint32_t idx = (uint32_t)sy * bw + sx;
      bool land = (ISS_WORLDMASK[idx >> 3] >> (7 - (idx & 7))) & 1;
      issSprite.drawPixel(mx + x, my + y, land ? ISS_COL_LAND : ISS_MAP_WATER);
    }
  }
  issSprite.drawRect(mx, my, mw, mh, ISS_COL_BORDER);
}

// Trajektoria naziemna (lat/lng z SGP4) rysowana na mapie. Rysujemy tylko
// odcinki, ktore nie przeskakuja przez antypoludnik (|dlon| > 180) - inaczej
// linia "przeskakuje" przez cala mape przy kazdym okrazeniu Ziemi.
static void issDrawGroundTrackOnMap(int mx, int my, int mw, int mh,
                                    const double *lats, const double *lngs, int n) {
  int px0, py0, px1, py1;
  for (int i = 0; i < n - 1; i++) {
    if (lats[i] == 0 && lngs[i] == 0) continue;
    if (lats[i + 1] == 0 && lngs[i + 1] == 0) continue;
    if (fabs(lngs[i] - lngs[i + 1]) > 180.0) continue; // przeskok przez antypoludnik
    issMapProject(lngs[i], lats[i], mx, my, mw, mh, px0, py0);
    issMapProject(lngs[i + 1], lats[i + 1], mx, my, mw, mh, px1, py1);
    issSprite.drawLine(px0, py0, px1, py1, ISS_COL_ACCENT2);
  }
}

// Znacznik ISS (aktualna pozycja) na mapie - punkt + pierscien + etykieta.
static void issDrawSatOnMap(int mx, int my, int mw, int mh, double lat, double lng) {
  if (lat == 0 && lng == 0) return;
  int px, py;
  issMapProject(lng, lat, mx, my, mw, mh, px, py);
  issSprite.fillCircle(px, py, 3, ISS_COL_ACCENT);
  issSprite.drawCircle(px, py, 6, ISS_COL_ACCENT2);
  issSprite.setTextDatum(TL_DATUM);
  issSprite.setTextSize(1);
  issSprite.setTextColor(ISS_COL_TEXT, ISS_MAP_WATER);
  issSprite.drawString("ISS", px + 8, py - 10, 1);
}

// Znacznik QTH (pozycja obserwatora) - zloty celownik.
static void issDrawQthOnMap(int mx, int my, int mw, int mh, double lat, double lng) {
  int px, py;
  issMapProject(lng, lat, mx, my, mw, mh, px, py);
  issSprite.drawCircle(px, py, 4, ISS_COL_WARN);
  issSprite.drawFastHLine(px - 7, py, 3, ISS_COL_WARN);
  issSprite.drawFastHLine(px + 5, py, 3, ISS_COL_WARN);
  issSprite.drawFastVLine(px, py - 7, 3, ISS_COL_WARN);
  issSprite.drawFastVLine(px, py + 5, 3, ISS_COL_WARN);
  issSprite.setTextDatum(TL_DATUM);
  issSprite.setTextSize(1);
  issSprite.setTextColor(ISS_COL_WARN, ISS_MAP_WATER);
  issSprite.drawString("QTH", px + 8, py + 2, 1);
}

// Radar azymut/elewacja: 90=zenit w srodku, 0=horyzont na brzegu (jak WWW).
static void issDrawRadar(int cx, int cy, int R) {
  issSprite.fillCircle(cx, cy, R + 6, ISS_MAP_WATER);
  issSprite.drawCircle(cx, cy, R, ISS_COL_BORDER);
  issSprite.drawCircle(cx, cy, (int)(R * 0.66), ISS_MAP_RING);
  issSprite.drawCircle(cx, cy, (int)(R * 0.33), ISS_MAP_RING);
  issSprite.drawLine(cx, cy - R, cx, cy + R, ISS_MAP_RING);
  issSprite.drawLine(cx - R, cy, cx + R, cy, ISS_MAP_RING);
  issSprite.setTextDatum(TL_DATUM);
  issSprite.setTextSize(1);
  issSprite.setTextColor(ISS_COL_TEXT, ISS_MAP_WATER);
  issSprite.drawString("N", cx - 3, cy - R - 12, 1);
  issSprite.drawString("S", cx - 3, cy + R + 3, 1);
  issSprite.drawString("E", cx + R + 3, cy - 4, 1);
  issSprite.drawString("W", cx - R - 10, cy - 4, 1);
}

// Pelne odrysowanie ekranu ISS (statyczne tlo + wszystkie wartosci).
// Wolane przy wejsciu na ekran oraz cyklicznie - sprite niweluje migotanie,
// wiec nie trzeba rozdzielac czesci statycznej i dynamicznej.
static void issRenderFrame(double lat, double lng, double alt, double speed,
                           double dist, double az, double el, const String &country,
                           const String &countryCode, bool hasData,
                           const double *trajLat, const double *trajLng, int trajN,
                           int passCount, const time_t *passAos, const time_t *passLos,
                           const double *passMaxEl, const time_t *passVisStart,
                           const time_t *passVisEnd, const int *passDur,
                           const double *passMag, bool passValid, bool wasVisible,
                           time_t curLos, int tzOffsetHours) {
  issSprite.fillScreen(ISS_COL_BG);

  // ---- Naglowek ----
  issSprite.fillRect(0, 0, 480, 30, ISS_COL_CARD);
  issSprite.drawFastHLine(0, 29, 480, ISS_COL_ACCENT);
  issSprite.setTextDatum(TL_DATUM);
  issSprite.setTextSize(1);
  issSprite.setTextDatum(TC_DATUM);
  issSprite.setTextColor(ISS_COL_FAINT, ISS_COL_CARD);
  issSprite.drawString("ESP32-HAM-CLOCK", 240, 4, 1);
  issSprite.setTextColor(ISS_COL_TEXT, ISS_COL_CARD);
  issSprite.drawString("ISS TRACKER", 240, 14, 2);
  issSprite.setTextDatum(TL_DATUM);
  bool live = hasData;
  issSprite.fillCircle(410, 15, 4, live ? ISS_COL_ACCENT : ISS_COL_FAINT);
  issSprite.setTextColor(live ? ISS_COL_ACCENT : ISS_COL_FAINT, ISS_COL_CARD);
  issSprite.drawString(live ? "NA ZYWO" : "CZEKAJ", 420, 10, 1);

  char b[40];

  // ---- Lewa kolumna: metryki ----
  const int LX = 4, LW = 118;
  struct { const char *k; char v[24]; uint16_t col; } left[] = {
    { "KRAJ POD ISS", "", ISS_COL_TEXT },
    { "SZEROKOSC",   "", ISS_COL_ACCENT },
    { "DLUGOSC",     "", ISS_COL_ACCENT },
    { "WYSOKOSC",    "", ISS_COL_TEXT },
    { "PREDKOSC",    "", ISS_COL_TEXT },
    { "W ZASIEGU",   "", ISS_COL_ACCENT },
  };
  String countryShown = country;
  // Flaga tylko dla realnego kraju (nie ocean/brak danych).
  String ccUp = countryCode; ccUp.toUpperCase();
  bool hasFlag = (ccUp.length() == 2 &&
                  !countryShown.equalsIgnoreCase("OCEAN") &&
                  !countryShown.startsWith("BRAK") &&
                  !countryShown.startsWith("CZEKAJ"));
  int nameMax = hasFlag ? 6 : 12;   // flaga zajmuje prawa czesc karty
  if ((int)countryShown.length() > nameMax) countryShown = countryShown.substring(0, nameMax);
  if (hasData) {
    snprintf(left[0].v, sizeof(left[0].v), "%s", countryShown.c_str());
    snprintf(left[1].v, sizeof(left[1].v), "%.4f", lat);
    snprintf(left[2].v, sizeof(left[2].v), "%.4f", lng);
    snprintf(left[3].v, sizeof(left[3].v), "%.1f km", alt);
    snprintf(left[4].v, sizeof(left[4].v), "%.0f km/h", speed);
    snprintf(left[5].v, sizeof(left[5].v), "%s", (dist > 0 && dist <= 5000.0) ? "TAK" : "nie");
  } else {
    snprintf(left[0].v, sizeof(left[0].v), "CZEKAJ...");
    for (int i = 1; i < 6; i++) snprintf(left[i].v, sizeof(left[i].v), "--");
  }
  for (int i = 0; i < 6; i++) {
    int y = 34 + i * 42;
    issCard(LX, y, LW, 38, ISS_COL_ACCENT);
    issLabel(LX + 8, y + 5, left[i].k, ISS_COL_FAINT);
    issValue(LX + 8, y + 17, left[i].v, left[i].col);
  }
  // Flaga kraju pod ISS - na karcie "KRAJ POD ISS" (prawa strona).
  if (hasFlag) {
    issDrawFlagToSprite(ccUp, LX + LW - 36, 34 + 8);
  }

  // ---- Prawa kolumna ----
  const int RX = 358, RW = 118;

  // Najblizszy przelot (AOS/LOS + okno widocznosci, wszystko w czasie lokalnym)
  issCard(RX, 34, RW, 72, ISS_COL_ACCENT);
  issLabel(RX + 8, 39, "NAJBLIZSZY PRZELOT", ISS_COL_FAINT);
  if (passValid) {
    time_t showEpoch = (wasVisible && curLos > 0) ? curLos : passAos[0];
    struct tm ptm;
    time_t e = showEpoch + tzOffsetHours * 3600;
    localtime_r(&e, &ptm);
    snprintf(b, sizeof(b), "%02d:%02d", ptm.tm_hour, ptm.tm_min);
    issValue(RX + 8, 50, b, ISS_COL_TEXT);

    // Linia 2: odliczanie + maks. elewacja (albo dlugosc przelotu)
    if (wasVisible) {
      snprintf(b, sizeof(b), "teraz, do %02d:%02d",
               ptm.tm_hour, ptm.tm_min);
    } else {
      time_t nowE = time(nullptr);
      long secs = (long)(passAos[0] - nowE);
      if (secs >= 3600)   snprintf(b, sizeof(b), "za %ldh%02ldm", secs / 3600, (secs % 3600) / 60);
      else if (secs >= 0) snprintf(b, sizeof(b), "za %ldm", secs / 60);
      else                snprintf(b, sizeof(b), "w toku");
    }
    issLabel(RX + 8, 70, b, ISS_COL_DIM);

    // Linia 3: okno widocznosci (start-koniec), "widoczny: --" gdy brak danych
    if (passVisStart[0] > 0 && passVisEnd[0] > 0) {
      struct tm vs, ve;
      time_t tvs = passVisStart[0] + tzOffsetHours * 3600;
      time_t tve = passVisEnd[0] + tzOffsetHours * 3600;
      localtime_r(&tvs, &vs);
      localtime_r(&tve, &ve);
      snprintf(b, sizeof(b), "wid. %02d:%02d-%02d:%02d",
               vs.tm_hour, vs.tm_min, ve.tm_hour, ve.tm_min);
      issLabel(RX + 8, 82, b, ISS_COL_WARN);
    } else {
      issLabel(RX + 8, 82, "widoczny: --", ISS_COL_FAINT);
    }
  } else {
    issValue(RX + 8, 50, "--:--", ISS_COL_FAINT);
    issLabel(RX + 8, 70, "brak danych", ISS_COL_FAINT);
    issLabel(RX + 8, 82, "widoczny: --", ISS_COL_FAINT);
  }

  // Nastepne przeloty - z kropka widocznosci (akcent = widoczny)
  issCard(RX, 110, RW, 84, ISS_COL_ACCENT2);
  issLabel(RX + 8, 115, "NASTEPNE PRZELOTY", ISS_COL_FAINT);
  if (passCount > 0) {
    for (int i = 0; i < passCount && i < 4; i++) {
      int y = 130 + i * 17;
      struct tm ptm;
      time_t e = passAos[i] + tzOffsetHours * 3600;
      localtime_r(&e, &ptm);
      bool vis = (passVisStart[i] > 0);
      // Kropka: przelot widoczny optycznie czy tylko radiowy.
      issSprite.fillCircle(RX + 10, y + 4, 3, vis ? ISS_COL_WARN : ISS_COL_FAINT);
      snprintf(b, sizeof(b), "%02d.%02d %02d:%02d", ptm.tm_mday, ptm.tm_mon + 1, ptm.tm_hour, ptm.tm_min);
      issSprite.setTextDatum(TL_DATUM);
      issSprite.setTextSize(1);
      issSprite.setTextColor(i == 0 ? ISS_COL_WARN : ISS_COL_DIM, ISS_COL_CARD);
      issSprite.drawString(b, RX + 18, y, 1);
      snprintf(b, sizeof(b), "%.0f", passMaxEl[i]);
      issSprite.setTextColor(ISS_COL_ACCENT, ISS_COL_CARD);
      issSprite.setTextDatum(TR_DATUM);
      issSprite.drawString(b, RX + RW - 8, y, 1);
      issSprite.setTextDatum(TL_DATUM);
    }
  } else {
    issLabel(RX + 8, 130, passValid ? "brak w planie" : "oczekiwanie na TLE", ISS_COL_FAINT);
  }

  // Azymut / elewacja / dystans
  struct { const char *k; char v[24]; } m2[3] = { { "AZYMUT", "" }, { "ELEWACJA", "" }, { "DYSTANS", "" } };
  snprintf(m2[0].v, sizeof(m2[0].v), "%.1f", az);
  snprintf(m2[1].v, sizeof(m2[1].v), "%.1f", el);
  snprintf(m2[2].v, sizeof(m2[2].v), "%.0f km", dist);
  for (int i = 0; i < 3; i++) {
    int y = 194 + i * 42;
    issCard(RX, y, RW, 38, ISS_COL_ACCENT2);
    issLabel(RX + 8, y + 5, m2[i].k, ISS_COL_FAINT);
    issValue(RX + 8, y + 17, m2[i].v, ISS_COL_TEXT);
  }

  // ---- Srodek: mapa swiata + trajektoria ----
  const int MX = 126, MW = 228, MY = 34, MH = 114;
  issDrawWorldMap(MX, MY, MW, MH);
  // siatka 30 stopni
  for (int gl = -150; gl <= 150; gl += 30) {
    int px, py;
    issMapProject(gl, 0, MX, MY, MW, MH, px, py);
    issSprite.drawFastVLine(px, MY, MH, ISS_MAP_RING);
  }
  for (int gl = -60; gl <= 60; gl += 30) {
    int px, py;
    issMapProject(0, gl, MX, MY, MW, MH, px, py);
    issSprite.drawFastHLine(MX, py, MW, ISS_MAP_RING);
  }
  issDrawGroundTrackOnMap(MX, MY, MW, MH, trajLat, trajLng, trajN);
  issDrawQthOnMap(MX, MY, MW, MH, userLatLonValid ? userLat : 52.40, userLatLonValid ? userLon : 16.92);
  issDrawSatOnMap(MX, MY, MW, MH, lat, lng);
  issSprite.setTextDatum(TL_DATUM);
  issSprite.setTextSize(1);
  issSprite.setTextColor(ISS_COL_DIM, ISS_MAP_WATER);
  issSprite.drawString("MAPA SWIATA - TRAJEKTORIA NAZIEMNA", MX + 4, MY + MH - 12, 1);

  // ---- Srodek dol: radar ----
  const int RCX = 240, RCY = 232, RRAD = 68;
  issDrawRadar(RCX, RCY, RRAD);
  if (hasData) {
    if (el >= 0.0) {
      // Nad horyzontem: znacznik wg azymutu, promien wg elewacji (srodek = zenit).
      double rr = RRAD * (1.0 - (el > 90 ? 90 : el) / 90.0);
      double ang = (az - 90.0) * DEG_TO_RAD;
      int sx = RCX + (int)(cos(ang) * rr);
      int sy = RCY + (int)(sin(ang) * rr);
      issSprite.fillCircle(sx, sy, 5, ISS_COL_ACCENT);
      snprintf(b, sizeof(b), "AZ %.0f  EL %.0f", az, el);
      issSprite.setTextDatum(TL_DATUM);
      issSprite.setTextSize(1);
      issSprite.setTextColor(ISS_COL_TEXT, ISS_MAP_WATER);
      issSprite.drawString(b, sx + 8, sy - 12, 1);
    } else {
      // Pod horyzontem: srodek radaru = nadir, znacznik przy krawedzi (el -90).
      issSprite.fillCircle(RCX, RCY, 5, ISS_COL_FAINT);
      snprintf(b, sizeof(b), "AZ %.0f  EL %.0f", az, el);
      issSprite.setTextDatum(TC_DATUM);
      issSprite.setTextSize(1);
      issSprite.setTextColor(ISS_COL_WARN, ISS_MAP_WATER);
      issSprite.drawString(b, RCX, RCY + RRAD - 14, 1);
      issSprite.setTextDatum(TL_DATUM);
    }
  } else {
    issSprite.setTextDatum(TL_DATUM);
    issSprite.setTextSize(1);
    issSprite.setTextColor(ISS_COL_FAINT, ISS_MAP_WATER);
    issSprite.drawString("brak danych", RCX - 30, RCY - 4, 1);
  }
  issSprite.setTextDatum(TC_DATUM);
  issSprite.setTextSize(1);
  issSprite.setTextColor(ISS_COL_FAINT, ISS_COL_BG);
  issSprite.drawString("RADAR AZYMUT / ELEWACJA", RCX, 308, 1);
  issSprite.setTextDatum(TL_DATUM);

  // Wypchnij cala klatke jednym transferem (bez migotania).
  issSprite.pushSprite(0, 0);
}

// Odczyt danych ISS pod mutexem, zbudowanie klatki i wypchniecie na panel.
static void issDrawFrameFromState() {
  if (!ensureIssSprite()) return;

  double lat = 0, lng = 0, alt = 0, speed = 0, dist = 0, az = 0, el = 0;
  String country = "CZEKAJ...", cc = "";
  bool hasData = false;
  static double trajLat[ISS_TRAJECTORY_POINTS];
  static double trajLng[ISS_TRAJECTORY_POINTS];
  int passCount = 0;
  time_t passAos[ISS_MAX_PASSES];
  time_t passLos[ISS_MAX_PASSES];
  double passMaxEl[ISS_MAX_PASSES];
  time_t passVisStart[ISS_MAX_PASSES];
  time_t passVisEnd[ISS_MAX_PASSES];
  int    passDur[ISS_MAX_PASSES];
  double passMag[ISS_MAX_PASSES];
  bool passValid = false, wasVisible = false;
  time_t curLos = 0;

  if (issDataMutex != NULL && xSemaphoreTake(issDataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    lat = issLat; lng = issLng; alt = issAltitude; speed = issSpeed;
    dist = issDistance; az = issAzimuth; el = issElevation;
    country = issCountry; cc = issCountryCode;
    hasData = (lat != 0.0 || lng != 0.0);
    for (int i = 0; i < ISS_TRAJECTORY_POINTS; i++) {
      trajLat[i] = issTrajectoryLat[i];
      trajLng[i] = issTrajectoryLng[i];
    }
    passCount = issPassCount;
    for (int i = 0; i < ISS_MAX_PASSES; i++) {
      passAos[i] = issPassAos[i];
      passLos[i] = issPassLos[i];
      passMaxEl[i] = issPassMaxEl[i];
      passVisStart[i] = issPassVisStart[i];
      passVisEnd[i] = issPassVisEnd[i];
      passDur[i] = issPassDuration[i];
      passMag[i] = issPassMag[i];
    }
    passValid = issNextPassEstimateValid;
    wasVisible = issNextPassWasVisible;
    curLos = issNextPassLosEpoch;
    xSemaphoreGive(issDataMutex);
  } else {
    return;
  }

  issRenderFrame(lat, lng, alt, speed, dist, az, el, country, cc, hasData,
                 trajLat, trajLng, ISS_TRAJECTORY_POINTS,
                 passCount, passAos, passLos, passMaxEl,
                 passVisStart, passVisEnd, passDur, passMag,
                 passValid, wasVisible, curLos,
                 getCachedTimezoneOffset());
}

void drawIssPassTracking() {
  if (!tftInitialized) return;
  issSpriteInitTried = false; // pozwol ponowic alokacje po wejsciu na ekran
  issSpriteReady = false;
  issDrawFrameFromState();
}

void drawIssStaticInterface() { issDrawFrameFromState(); }
void updateIssDynamicDisplay() { issDrawFrameFromState(); }

// ========== MQTT PSK REPORTER IMPLEMENTATION ==========

// Konfiguracja MQTT - konkretny topic zamiast szerokiego wildcard
typedef struct {
  String callsign;
  float lat;
  float lon;
  uint8_t band;
  String mode;
  unsigned long receivedAt;
} PskSpotMqtt;

#define PSK_MQTT_SPOT_BUFFER_SIZE 30
PskSpotMqtt pskMqttSpotBuffer[PSK_MQTT_SPOT_BUFFER_SIZE];
int pskMqttSpotCount = 0;
unsigned long lastPskMqttDrawMs = 0;
const unsigned long PSK_MQTT_DRAW_INTERVAL_MS = 1500; // Rysuj co 1.5 sekundy
bool pskMqttConnected = false;
unsigned long lastPskMqttReconnectAttempt = 0;
const unsigned long PSK_MQTT_RECONNECT_INTERVAL_MS = 5000; // Co 5 sekund prĂłba reconnect

// Generuje precyzyjny topic na podstawie lokatora uĹĽytkownika (tylko pierwsze 4 znaki = duĹĽy kwadrat ~100x200km)
// Temat MQTT PSKReportera to pskr/filter/v2/<pasmo>/<tryb>/<nadawca>/<odbiorca>/
// <lokator nadawcy>/<lokator odbiorcy>/<az>/<az>. Broker NIE tworzy tematow z
// prefiksem lokatora (dawny "pskr/filter/v2/+/+/+/LOC4/#" nie pasowal do niczego
// i ekran zostawal pusty), wiec subskrybujemy cala galeź i filtrujemy w
// callbacku po lokatorze.
static String getPskMqttTopicForLocator(const String& locator) {
  (void)locator;
  return "pskr/filter/v2/#";
}

// Zwraca idx-ty segment tematu MQTT (rozdzielony '/'), np. idx=8 to lokator
// odbiorcy w "pskr/filter/v2/<pasmo>/<tryb>/<nad>/<odb>/<lokN>/<lokO>/<az>/<az>".
static String pskMqttTopicSegment(const char* topic, int idx) {
  if (!topic) return String();
  const char* p = topic;
  for (int i = 0; i < idx; i++) {
    p = strchr(p, '/');
    if (!p) return String();
    p++;
  }
  const char* e = strchr(p, '/');
  return e ? String(p).substring(0, e - p) : String(p);
}

// Callback dla MQTT - parsuje JSON i dodaje do bufora
void pskMqttCallback(char* topic, byte* payload, unsigned int length) {
  // Ogranicz rozmiar payloadu dla bezpieczeĹ„stwa
  if (length > 1024) {
    Serial.println("[PSK MQTT] Payload too large, skipping");
    return;
  }

  // Broker sypie ~240 spotami/s. Zanim sparsujemy JSON (drogie), odrzucamy
  // spoty po lokatorze odbiorcy wyczytanym wprost z tematu - zostaje tylko
  // garstka odebranych w naszym kwadracie.
  if (userLocator.length() >= 4) {
    String rl4 = pskMqttTopicSegment(topic, 8);
    if (rl4.length() >= 4) {
      rl4 = rl4.substring(0, 4);
      rl4.toUpperCase();
      String loc4 = userLocator.substring(0, 4);
      loc4.toUpperCase();
      if (rl4 != loc4) return;
    }
  }

  // Parsuj JSON. Broker uzywa krotkich kluczy: sc=nadawca, sl=lokator nadawcy,
  // rc=odbiorca, rl=lokator odbiorcy, md=tryb, f=czestotliwosc [Hz], rp=SNR.
  // (Dawny kod czytal "sender"/"senderLocator"/"mode" - nigdy nie istnialy.)
  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    return; // Ciche pominiÄ™cie bĹ‚Ä™dĂłw parsowania
  }

  const char* sender = doc["sc"];
  const char* senderLocator = doc["sl"];
  const char* receiverLocator = doc["rl"];
  const char* mode = doc["md"];
  float frequency = doc["f"] | 0.0f;

  if (!sender || !senderLocator) return;

  // Na mapie rysujemy NADAWCE (DX) - tak jak tryb HTTP. Zeby mapa byla
  // lokalnie istotna, zostawiamy tylko spoty odebrane w naszym kwadracie
  // lokatora (odbiorca blisko nas), a nie nadawca.
  if (userLocator.length() >= 4 && receiverLocator && receiverLocator[0]) {
    String loc4 = userLocator.substring(0, 4);
    loc4.toUpperCase();
    String rl4 = String(receiverLocator).substring(0, 4);
    rl4.toUpperCase();
    if (rl4 != loc4) return;
  }

  // Konwersja lokatora na lat/lon
  double lat = 0, lon = 0;
  String locStr(senderLocator);
  locatorToLatLon(locStr, lat, lon);

  // OkreĹ›l pasmo
  int band = 0;
  int freq_khz = (int)(frequency / 1000);
  if (freq_khz >= 3500 && freq_khz <= 4000) band = 80;
  else if (freq_khz >= 7000 && freq_khz <= 7300) band = 40;
  else if (freq_khz >= 14000 && freq_khz <= 14350) band = 20;
  else if (freq_khz >= 21000 && freq_khz <= 21450) band = 15;
  else if (freq_khz >= 28000 && freq_khz <= 29700) band = 10;
  else if (freq_khz >= 50000 && freq_khz <= 54000) band = 6;

  // Filtruj po pasmie jeĹ›li ustawiony
  if (pskFilterBand.length() > 0) {
    int filterBand = pskFilterBand.toInt();
    if (band != filterBand) return;
  }

  // Filtruj po trybie jeĹ›li ustawiony
  String modeStr = mode ? String(mode) : String("FT8");
  if (pskFilterMode.length() > 0) {
    if (!modeStr.equalsIgnoreCase(pskFilterMode)) return;
  }

  // SprawdĹş czy monitorujemy konkretny znak
  if (pskMqttCallsign.length() > 0) {
    String monitorCall = pskMqttCallsign;
    monitorCall.toUpperCase();
    String senderCall(sender);
    senderCall.toUpperCase();
    if (senderCall != monitorCall) return;
  }

  // Dodaj do bufora
  if (pskMqttSpotCount < PSK_MQTT_SPOT_BUFFER_SIZE) {
    pskMqttSpotBuffer[pskMqttSpotCount].callsign = String(sender);
    pskMqttSpotBuffer[pskMqttSpotCount].lat = (float)lat;
    pskMqttSpotBuffer[pskMqttSpotCount].lon = (float)lon;
    pskMqttSpotBuffer[pskMqttSpotCount].band = band;
    pskMqttSpotBuffer[pskMqttSpotCount].mode = modeStr;
    pskMqttSpotBuffer[pskMqttSpotCount].receivedAt = millis();
    pskMqttSpotCount++;

    // Debug co 10 spotĂłw
    if (pskMqttSpotCount % 10 == 0) {
      Serial.printf("[PSK MQTT] Bufor: %d spotĂłw\n", pskMqttSpotCount);
    }
  }
}

// ĹÄ…czenie z MQTT z timeout i loop() - naprawia problem zawieszania
void reconnectPskMqtt() {
  if (!wifiConnected) return;

  unsigned long now = millis();
  // SprawdĹş czy minÄ…Ĺ‚ czas od ostatniej prĂłby
  if (now - lastPskMqttReconnectAttempt < PSK_MQTT_RECONNECT_INTERVAL_MS) {
    return;
  }
  lastPskMqttReconnectAttempt = now;

  // JeĹ›li juĹĽ poĹ‚Ä…czony, tylko loop()
  if (pskMqttClient.connected()) {
    pskMqttClient.loop();
    return;
  }

  Serial.print("[PSK MQTT] ĹÄ…czenie z MQTT...");

  // Generuj client ID
  String clientId = "ESP32-HAM-CLOCK-" + String(random(0xffff), HEX);

  // Timeout dla poĹ‚Ä…czenia - nie blokujÄ…cy
  unsigned long connectStart = millis();
  bool connected = false;

  // PrĂłba poĹ‚Ä…czenia z timeout 5 sekund
  while (!connected && (millis() - connectStart < 5000)) {
    connected = pskMqttClient.connect(clientId.c_str());
    if (!connected) {
      pskMqttClient.loop(); // KLUCZOWE: loop() podczas prĂłb!
      delay(100);
    }
  }

  if (connected) {
    Serial.println(" poĹ‚Ä…czono!");
    pskMqttConnected = true;

    // Subskrybuj cala galeź pskr/filter/v2/# - filtrowanie po lokatorze
    // odbywa sie w pskMqttCallback (broker nie grupuje tematow po lokatorze).
    String topic = getPskMqttTopicForLocator(userLocator);

    Serial.printf("[PSK MQTT] Subskrypcja: %s\n", topic.c_str());
    pskMqttClient.subscribe(topic.c_str());
  } else {
    Serial.println(" nieudane, ponowiÄ™ za 5s");
    pskMqttConnected = false;
  }
}

// GĹ‚Ăłwna funkcja loop dla MQTT
void loopPskMqtt() {
  if (!pskMqttEnabled) return;
  if (!wifiConnected) {
    pskMqttConnected = false;
    return;
  }

  // Konfiguracja przy pierwszym uruchomieniu
  static bool mqttInitialized = false;
  if (!mqttInitialized) {
    pskMqttClient.setServer(pskMqttServer.c_str(), pskMqttPort);
    pskMqttClient.setCallback(pskMqttCallback);
    mqttInitialized = true;
  }

  // Reconnect lub utrzymanie poĹ‚Ä…czenia (z loop() w Ĺ›rodku!)
  reconnectPskMqtt();

  // ObsĹ‚uga MQTT loop
  if (pskMqttClient.connected()) {
    pskMqttClient.loop();
  }

  // Przetwarzanie bufora i rysowanie co zadany interwaĹ‚
  processPskMqttBuffer();
}

// Przetwarza bufor i kopiuje do gĹ‚Ăłwnej tablicy spotĂłw dla wyĹ›wietlenia
void processPskMqttBuffer() {
  unsigned long now = millis();

  // SprawdĹş czy czas na rysowanie
  if (now - lastPskMqttDrawMs < PSK_MQTT_DRAW_INTERVAL_MS) {
    return;
  }
  lastPskMqttDrawMs = now;

  if (pskMqttSpotCount == 0) return;

  // Kopiuj do gĹ‚Ăłwnej tablicy spotĂłw
  pskSpotCount = 0;
  for (int i = 0; i < pskMqttSpotCount && i < PSK_MAX_SPOTS; i++) {
    pskSpots[pskSpotCount].callsign = pskMqttSpotBuffer[i].callsign;
    pskSpots[pskSpotCount].lat = pskMqttSpotBuffer[i].lat;
    pskSpots[pskSpotCount].lon = pskMqttSpotBuffer[i].lon;
    pskSpots[pskSpotCount].band = pskMqttSpotBuffer[i].band;
    pskSpots[pskSpotCount].mode = pskMqttSpotBuffer[i].mode;
    pskSpots[pskSpotCount].snr = 0; // MQTT nie daje SNR
    pskSpots[pskSpotCount].timestamp = pskMqttSpotBuffer[i].receivedAt;
    pskSpotCount++;
  }

  // CzyĹ›Ä‡ bufor
  pskMqttSpotCount = 0;

  // OdĹ›wieĹĽ wyĹ›wietlacz jeĹ›li na ekranie PSK
  if (currentScreen == SCREEN_PSK_MAP && tftInitialized && !pskMapMenuOpen) {
    Serial.printf("[PSK MQTT] Rysowanie %d spotĂłw\n", pskSpotCount);
    drawPskMap();
  }
}

// Setup MQTT - wywoĹ‚ywane przy starcie
void setupPskMqtt() {
  pskMqttClient.setServer(pskMqttServer.c_str(), pskMqttPort);
  pskMqttClient.setCallback(pskMqttCallback);
  Serial.println("[PSK MQTT] Zainicjalizowano klienta MQTT");
}

// ========== SETUP & LOOP ==========

void setup() {
  // Najpierw inicjalizacja Serial dla diagnostyki
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\n[SETUP] START");
  
  // Powod restartu - kluczowe dla diagnozy "ekran zgasl / WiFi sie rozlacza":
  // ESP_RST_BROWNOUT = za slabe zasilanie, ESP_RST_TASK_WDT = zablokowana petla,
  // ESP_RST_PANIC = crash (np. uszkodzona pamiec/stos).
  {
    esp_reset_reason_t rr = esp_reset_reason();
    const char *rrName = "UNKNOWN";
    switch (rr) {
      case ESP_RST_POWERON:   rrName = "POWERON"; break;
      case ESP_RST_EXT:       rrName = "EXT_PIN"; break;
      case ESP_RST_SW:        rrName = "SOFTWARE"; break;
      case ESP_RST_PANIC:     rrName = "PANIC/CRASH"; break;
      case ESP_RST_INT_WDT:   rrName = "INT_WDT"; break;
      case ESP_RST_TASK_WDT:  rrName = "TASK_WDT"; break;
      case ESP_RST_WDT:       rrName = "WDT"; break;
      case ESP_RST_DEEPSLEEP: rrName = "DEEPSLEEP"; break;
      case ESP_RST_BROWNOUT:  rrName = "BROWNOUT (za slabe zasilanie!)"; break;
      case ESP_RST_SDIO:      rrName = "SDIO"; break;
      default: break;
    }
    Serial.printf("[SETUP] Powod restartu: %s (%d)\n", rrName, (int)rr);
  }

  // Disable brownout detector to prevent resets due to voltage drops.
  // Uwaga: na ESP32-S3 detektor brownoutu jest domyslnie WLACZONY i resetuje
  // uklad przy kazdym chwilowym spadku napiecia (np. skok poboru pradu przez
  // WiFi radio albo podswietlenie TFT). Objawialo sie to jako ciagle restarty:
  // ekran gasl, a AP/WiFi "co chwile sie rozlaczal". Wylaczamy go takze na S3.
  #if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S3
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  #endif
  
  // Zapisz czas startu systemu
  bootTimeMs = millis();
  
  // WyczyĹ›Ä‡ cache QRZ przy starcie (aby wymusiÄ‡ pobranie nowych danych z lat/lon)
  for (int i = 0; i < QRZ_CACHE_SIZE; i++) {
    qrzCache[i].callsign = "";
    qrzCache[i].fetchedAtMs = 0;
  }
  Serial.println("[SETUP] QRZ cache cleared");

  // Ten log idzie "kanaĹ‚em" (zwykle tym samym co bootlog),
  // Na ESP32-C3 (zwĹ‚aszcza z USB CDC) warto chwilÄ™ poczekaÄ‡ na monitor portu
  unsigned long serialWaitStart = millis();
  while (!Serial && (millis() - serialWaitStart) < 2000) {
    delay(10);
  }
  delay(200);
  Serial.println("[SETUP] Serial ready");

  // Daj chwilÄ‚â€žĂ˘â€žË na ustabilizowanie WiFi/PHY po resecie (zwĂ„Ä…Ă˘â‚¬Ĺˇaszcza na "Super Mini")
  delay(300);

  if (dxSpotsMutex == nullptr) {
    dxSpotsMutex = xSemaphoreCreateMutex();
  }

  // Create mutex for ISS data protection
  if (issDataMutex == nullptr) {
    issDataMutex = xSemaphoreCreateMutex();
  }
  // Mutex chroniÄ…cy dostÄ™p do issSat (Sgp4)/issTleValid - propagator SGP4
  // trzyma stan wewnÄ™trzny, wiÄ™c nie jest bezpieczny do wywoĹ‚ywania z dwĂłch
  // miejsc naraz (np. rysowanie trajektorii i przewidywanie przelotu).
  if (issTleMutex == nullptr) {
    issTleMutex = xSemaphoreCreateMutex();
  }

  initStatusRgbLed();
  updateStatusRgbLed();

  // Inicjalizacja pomiaru napiÄ™cia baterii (TP4056 + 18650)
#ifdef BATTERY_MONITORING_ENABLED
  pinMode(BATTERY_ADC_PIN, INPUT);
  #if CONFIG_IDF_TARGET_ESP32
    analogSetAttenuation(ADC_11db);  // ESP32: pelny zakres 0-3.3V
  #else
    analogSetAttenuation(ADC_11db);  // ESP32-S3: pelny zakres 0-3.3V
  #endif
  analogReadResolution(BATTERY_ADC_RESOLUTION);
  // Pobierz pierwszy odczyt dla wstÄ™pnej kalibracji
  delay(10);
  batteryVoltage = readBatteryVoltage();
  if (batteryVoltage < 1.0f) { batteryVoltage = 0.0f; }  // brak akumulatora/dzielnika
  batteryPercentage = calculateBatteryPercentage(batteryVoltage);
  batteryCharging = (batteryVoltage > 4.25f);
  Serial.printf("[BATTERY] Init: %.2fV (%d%%) %s\n", 
                batteryVoltage, batteryPercentage, 
                batteryCharging ? "[CHARGING]" : "");
#endif
  
  // WyĂ„Ä…Ă˘â‚¬Ĺźwietlacz TFT (ESP32-S3 N16R8 + ILI9488) Ä‚ËĂ˘â€šÂ¬Ă˘â‚¬Ĺ› inicjalizacja NA POCZÄ‚â€žĂ˘â‚¬ĹľTKU (jak w projekcie referencyjnym)
  // TFT powinien byÄ‚â€žĂ˘â‚¬Ë‡ inicjalizowany zaraz po Serial.begin(), przed innymi peryferiami
#ifdef ENABLE_TFT_DISPLAY
  initTFT();
  yield();
#endif

  // Inicjalizacja LittleFS z wieloma prĂłbami i diagnostykÄ…
  Serial.println("[LittleFS] Starting initialization...");
  Serial.printf("[LittleFS] Free heap before mount: %d\n", ESP.getFreeHeap());
  int littleFsAttempts = 0;
  while (!littleFsReady && littleFsAttempts < 3) {
    littleFsAttempts++;
    Serial.printf("[LittleFS] Attempt %d/3...\n", littleFsAttempts);
    if(LittleFS.begin(false, "/littlefs", 10, "littlefs")){
      Serial.println("[LittleFS] Mounted successfully!");
      littleFsReady = true;
    } else {
      Serial.printf("[LittleFS] Mount attempt %d FAILED\n", littleFsAttempts);
      delay(200);
    }
  }
  
  if (!littleFsReady) {
    Serial.println("[LittleFS] Trying format...");
    if(LittleFS.format()){
      Serial.println("[LittleFS] Format OK, retrying mount...");
      if(LittleFS.begin(false, "/littlefs", 10, "littlefs")){
        Serial.println("[LittleFS] Mounted after format!");
        littleFsReady = true;
      }
    }
  }
  
  if (!littleFsReady) {
    Serial.println("[LittleFS] ERROR: All mount attempts failed!");
    bootLogLine("LittleFS: BRAK - sprawdz schemat partycji!");
  } else {
    Serial.printf("[LittleFS] Total: %d, Used: %d, Free: %d\n", LittleFS.totalBytes(), LittleFS.usedBytes(), LittleFS.totalBytes() - LittleFS.usedBytes());
    Serial.println("[LittleFS] Listing root files:");
    File root = LittleFS.open("/", "r");
    File file = root.openNextFile();
    int fileCount = 0;
    while (file && fileCount < 30) {
      Serial.printf("  %s (%d bytes)\n", file.path(), file.size());
      file = root.openNextFile();
      fileCount++;
    }
    root.close();
    bool splashExists = LittleFS.exists("/splash.bmp");
    bool indexExists = LittleFS.exists("/index.html");
    bool mapaExists = LittleFS.exists("/mapa.bmp");
    bool pskMapExists = LittleFS.exists("/Mapa swiata.bmp");
    Serial.printf("[LittleFS] /splash.bmp: %s\n", splashExists ? "YES" : "NO");
    Serial.printf("[LittleFS] /index.html: %s\n", indexExists ? "YES" : "NO");
    Serial.printf("[LittleFS] /mapa.bmp: %s\n", mapaExists ? "YES" : "NO");
    Serial.printf("[LittleFS] /Mapa swiata.bmp: %s\n", pskMapExists ? "YES" : "NO");
    bootLogLine("LittleFS: OK");
  }

  bootLogLine("");
  bootLogLine("ESP32 DX Cluster Receiver");
  bootLogLine("==============================");
  
  // DIAGNOSTYKA: SprawdĹş czy ENABLE_TFT_DISPLAY jest zdefiniowane
#ifdef ENABLE_TFT_DISPLAY
  bootLogLine("ENABLE_TFT_DISPLAY - init TFT");
#else
  bootLogLine("UWAGA: ENABLE_TFT_DISPLAY NIE jest zdefiniowane - TFT dont work!");
  bootLogLine("Upewnij sie, ze kompilujesz dla srodowiska 'esp32-s3-n16r8'");
#endif
  yield();

  bootLogLine("Config load...");
  bootLogLine("LittleFS ready: " + String(littleFsReady ? "YES" : "NO"));
  loadPreferences();

  // Uruchom UART2 dla modulu GPS, jesli wlaczony w konfiguracji.
  gpsApplyConfig();
  if (gpsEnabled) {
    bootLogLine("GPS: UART2 RX=" + String(gpsRxPin) + " TX=" + String(gpsTxPin) + " baud=" + String(gpsBaud));
  }
  
  // TYMCZASOWA NAPRAWA KONFIGURACJI
  if (clusterHost == "Multiplay_C984" || clusterHost.length() == 0) {
    clusterHost = "dxspots.com";
    bootLogLine("FIX: clusterHost reset to dxspots.com");
  }
  if (hamalertHost == "Multiplay_C984" || hamalertHost.length() == 0) {
    hamalertHost = "hamalert.org";
    bootLogLine("FIX: hamalertHost reset to hamalert.org");
  }
  if (hamalertLogin == "Multiplay_C984" || hamalertLogin.length() == 0) {
    hamalertLogin = "";
    bootLogLine("FIX: hamalertLogin reset to empty");
  }
  if (hamalertPassword == "TWOJE_HASLO" || hamalertPassword.length() == 0) {
    hamalertPassword = "";
    bootLogLine("FIX: hamalertPassword reset to empty");
  }
  if (aprsIsHost == "Multiplay_C984" || aprsIsHost.length() == 0) {
    aprsIsHost = "rotate.aprs2.net";
    bootLogLine("FIX: aprsIsHost reset to rotate.aprs2.net");
  }
  if (wifiSSID == "Multiplay_C984") {
    wifiSSID = "Multiplay_C984";  // To jest OK - Twoja sieÄ‡
  }
  savePreferences();
  bootLogLine("Config fixed and saved!");
  telnetBuffer.reserve(384);
  pendingTelnetLine.reserve(384);
  potaTelnetBuffer.reserve(384);
  pendingPotaLine.reserve(384);
  aprsBuffer.reserve(384);
  qrzStatus.reserve(64);
  clusterFilterCommands.reserve(128);
  weatherData.description.reserve(96);
  weatherData.cityName.reserve(48);
  weatherData.forecast3hDesc.reserve(64);
  weatherData.forecastNextDayDesc.reserve(64);
  weatherData.lastError.reserve(64);
  weatherData.iconCode.reserve(8);
  propagationData.lastError.reserve(48);
  yield();
#ifdef ENABLE_TFT_DISPLAY
  applyTftRotation();
  applyTouchRotation();
  setBacklightPercent(backlightPercent);
#endif
  
  bootLogLine("WIFI starting...");
  if (wifiSSID.length() > 0 || wifiSSID2.length() > 0) {
    bootLogLine("Connecting WiFi: " + (wifiSSID.length() > 0 ? wifiSSID : wifiSSID2));
    if (connectToWiFi()) {
      bootLogLine("WiFi connected!");
      yield();
      updateNTPTime();
      yield();
      connectToCluster();
      connectToPotaCluster();
    } else {
      bootLogLine("WiFi error, starting AP mode...");
      startAPMode();
    }
  } else {
    bootLogLine("No WiFi configuration, starting AP mode...");
    startAPMode();
  }
  yield();
  
  bootLogLine("Starting web server...");
  setupWebServer();
  yield();

  // Create ISS task on Core 0 for non-blocking satellite calculations
  //
  // Stack bumped from 4096 to 16384 bytes: this task performs HTTPS/TLS
  // requests (fetchIssData() to wheretheiss.at AND getIssCountryFromCoords()
  // to nominatim.openstreetmap.org), and TLS handshake/crypto code is very
  // stack-hungry on ESP32. 4096 bytes was already marginal with one HTTPS
  // call and overflowed once a second WiFiClientSecure object was added,
  // triggering a "Stack canary watchpoint triggered (ISS_Task)" panic and
  // reboot loop.
  xTaskCreatePinnedToCore(
    issTask,           // Task function
    "ISS_Task",       // Task name
    24576,            // Stack size (bumped again: SGP4 propagation + TLE fetch added on top of the existing HTTPS work)
    NULL,             // Parameters
    1,                // Priority
    &issTaskHandle,   // Task handle
    0                 // Core 0
  );
  Serial.println("[SETUP] ISS task created on Core 0");
  yield();
  
  bootLogLine("System ready!");
  String ipStr = wifiConnected ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  bootLogLine("IP: " + ipStr);
  
  // Aktualizuj wyĹ›wietlacz TFT (ekran startowy)
#ifdef ENABLE_TFT_DISPLAY
  // Ekran powitalny (czarny ekran, bialy tekst) - 20 s, dotyk pomija.
  drawWelcomeScreenYellow();
  unsigned long welcomeStart = millis();
  while ((millis() - welcomeStart) < 20000) {
    uint16_t tx, ty;
    if (readTouchPoint(tx, ty)) {
      break;
    }
    delay(50);
  }

  // Ekran splash SP9TNV z opcjÄ… pominiÄ™cia - wyĹ›wietla siÄ™ PO ekranie powitalnym
  drawSplashScreen();
  
  // PokaĹĽ ikonÄ™ baterii od razu na ekranie splash
#ifdef BATTERY_MONITORING_ENABLED
  drawBatteryQuickUpdate(true);
#endif
  
  // Czekaj na dotyk lub timeout 10 sekund
  unsigned long splashStart = millis();
  bool splashSkipped = false;
  unsigned long lastBatteryUpdate = 0;
  while (!splashSkipped && (millis() - splashStart) < 10000) {
    uint16_t tx, ty;
    if (readTouchPoint(tx, ty)) {
      splashSkipped = true;
    }
    // OdĹ›wieĹĽaj ikonÄ™ baterii co 1 sekundÄ™ podczas czekania
    unsigned long now = millis();
    if (now - lastBatteryUpdate >= 1000) {
      lastBatteryUpdate = now;
      updateBatteryStatus();
      drawBatteryQuickUpdate(true);
    }
    delay(50);
  }
  // OdĹ›wieĹĽaj ikonÄ™ baterii co 1 sekundÄ™ podczas czekania
  unsigned long now = millis();
  if (now - lastBatteryUpdate >= 1000) {
    lastBatteryUpdate = now;
    updateBatteryStatus();
    drawBatteryQuickUpdate(true);
  }
  delay(50);
  bootSequenceActive = false;
  drawScreen(currentScreen);
  resetTftAutoSwitchTimer();
  if (currentScreen == SCREEN_HAM_CLOCK) {
    ensureScreenOrderValid();

    // ----- ZADANIA FreeRTOS
    if (uiTaskHandle == nullptr) {
      xTaskCreatePinnedToCore(
        uiTaskLoop,
        "UI_Task",
        8192,
        nullptr,
        2,
        &uiTaskHandle,
        1
      );
    }
  }
#endif
}

void loop() {
  static unsigned long loopCounter = 0;
  static unsigned long lastLoopPrint = 0;
  loopCounter++;
  
  unsigned long now = millis();
  
  // Feed watchdog
  yield();

  // Log free heap memory every 30 seconds
  static unsigned long lastHeapLog = 0;
  if (now - lastHeapLog > 30000 || lastHeapLog == 0) {
    Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
    lastHeapLog = now;
  }

  // Odroczony restart (Ă„Ä…Ă„Ëťeby odpowiedĂ„Ä…ÄąĹş HTTP zdÄ‚â€žĂ˘â‚¬Â¦Ă„Ä…Ă„ËťyĂ„Ä…Ă˘â‚¬Ĺˇa wyjĂ„Ä…Ă˘â‚¬ĹźÄ‚â€žĂ˘â‚¬Ë‡)
  // Odroczona rekonfiguracja WiFi (zmiana SSID w WWW) - bez restartu.
  if (wifiReconfigRequested && (long)(millis() - wifiReconfigAtMs) >= 0) {
    wifiReconfigRequested = false;
    wifiReconfigure();
  }

  if (restartRequested && (long)(millis() - restartAtMs) >= 0) {
    LOGV_PRINTLN("[LOOP] Restart requested - restarting...");
    delay(50);
    ESP.restart();
  }
  
  // ObsĂ„Ä…Ă˘â‚¬Ĺˇuga serwera WWW
  if (server != nullptr) {
    server->handleClient();
  }
  
  // Aktualizuj czas NTP
  updateNTPTime();
  
  // ObsĂ„Ä…Ă˘â‚¬Ĺˇuga WiFi
  if (!wifiConnected && WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.print("STA poĹ‚Ä…czoneĂ˘â‚¬Â¦czone. IP: ");
    Serial.println(WiFi.localIP());
    // JeĹĽeli wczeĹ›niej byĹ‚ uruchomiony AP do konfiguracji, wyĹ‚Ä…cz go po udanym poĹ‚Ä…czeniu STA,
    // ĹĽeby nie robiÄ‡ wraĹĽenia "zwiechy" (klient AP traci link przy przeĹ‚Ä…czeniu kanaĹ‚u).
    if (WiFi.getMode() & WIFI_AP) {
      Serial.println("WyĹ‚Ä…czam AP (portal) po poĹ‚Ä…czeniu STA. UĹĽyj IP z sieci domowej.");
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
    }
    

    updateNTPTime();
    connectToCluster();
    connectToPotaCluster();
    connectToAPRS(); // PoĹ‚Ä…cz rĂłwnieĹĽ z APRS-IS
  } else if (wifiConnected && WiFi.status() != WL_CONNECTED) {
    LOGV_PRINTLN("[LOOP] WiFi status zmieniony: poĹ‚Ä…czony -> rozĹ‚Ä…czony");
    wifiConnected = false;
    telnetConnected = false;
    potaTelnetConnected = false;
    aprsConnected = false;
    LOGV_PRINTLN("[LOOP] STA rozĹ‚Ä…czone (wracam do trybu offline/AP jeĹ›li aktywne).");
  }

  updateStatusRgbLed();

  // Odczyt NMEA z modulu GPS + nadpisanie pozycji/lokatora (jesli wlaczone).
  gpsUpdate();

  // ObsĹ‚uga wygaszacza ekranu (Matrix)
  checkScreenSaverTimeout();

  // ObsĹ‚uga uĹ›pienia ekranu
  checkScreenSleepTimeout();

  // JeĹ›li jesteĹ›my offline, zostajemy w AP (portal) Ă˘â‚¬â€ť stabilnie.
  
  // ObsĹ‚uga Telnet
  if (wifiConnected) {
    if (!telnetConnected) {
      LOGV_PRINTLN("[LOOP] WiFi OK, prĂłba poĹ‚Ä…czenia z Cluster...");
      connectToCluster();
    } else {
      handleTelnetData();
    }
  }

  // ObsĹ‚uga POTA Telnet
  if (wifiConnected) {
    // PoĹ‚Ä…czenie z POTA Cluster (Telnet)
    if (!potaTelnetConnected) {
      connectToPotaCluster();
    } else {
      handlePotaTelnetData();
    }
    
    // Tryb HTTP API zamiast Telnetu (ĹşrĂłdĹ‚o: https://api.pota.app/v1/spots)
    if (lastPotaApiFetchMs == 0 || now - lastPotaApiFetchMs > POTA_API_FETCH_INTERVAL_MS) {
      if (fetchPotaApi()) {
        lastPotaApiFetchMs = now;
      } else {
        // nawet przy bĹ‚Ä™dzie aktualizuj czas, by nie spamowaÄ‡ API
        lastPotaApiFetchMs = now;
      }
    }

    // HAMALERT przez Telnet: set/json + sh/dx 30
    if (lastHamalertFetchMs == 0 || now - lastHamalertFetchMs > HAMALERT_FETCH_INTERVAL_MS) {
      fetchHamalertTelnet();
      lastHamalertFetchMs = now;
    }
  }

  // ObsĂ„Ä…Ă˘â‚¬Ĺˇuga APRS-IS
  if (wifiConnected) {
    if (!aprsConnected) {
      LOGV_PRINTLN("[LOOP] WiFi OK, prĂ„â€šĹ‚ba poĂ„Ä…Ă˘â‚¬ĹˇÄ‚â€žĂ˘â‚¬Â¦czenia z APRS-IS...");
      connectToAPRS();
    } else {
      handleAPRSData();
      unsigned long nowAprs = millis();

      if (aprsBeaconEnabled && aprsLoginSent && userLatLonValid) {
        if (nextAPRSPositionDueMs == 0) {
          nextAPRSPositionDueMs = nowAprs + APRS_POSITION_FIRST_DELAY_MS;
        } else if ((long)(nowAprs - nextAPRSPositionDueMs) >= 0) {
          sendAprsPosition();
          nextAPRSPositionDueMs = nowAprs + aprsPositionIntervalMs;
        }
      } else {
        nextAPRSPositionDueMs = 0; // Reset harmonogramu gdy beacon jest wyĹ‚Ä…czony lub brak loginu/koordynat
        aprsBeaconTxCount = 0;
      }
      
      // Watchdog APRS: jeĂ„Ä…Ă˘â‚¬Ĺźli brak danych przez dĂ„Ä…Ă˘â‚¬ĹˇuĂ„Ä…Ă„Ëťszy czas, zrĂ„â€šĹ‚b reconnect
      unsigned long inactivityTime = nowAprs - lastAPRSRxMs;
      if (inactivityTime > APRS_INACTIVITY_RECONNECT_MS) {
        LOGV_PRINT("[LOOP] WARNING: Brak danych z APRS-IS przez ");
        LOGV_PRINT(inactivityTime / 1000);
        LOGV_PRINTLN(" sekund -> reconnect");
        aprsClient.stop();
        aprsConnected = false;
        aprsLoginSent = false;
        // connectToAPRS() odpali siÄ‚â€žĂ˘â€žË w kolejnych iteracjach
      }
    }
  }

  // Aktualizuj dane propagacyjne (hamqsl solarxml)
  if (wifiConnected) {
    unsigned long now = millis();
    unsigned long interval = lastPropagationFetchOk ? PROPAGATION_FETCH_INTERVAL_MS
                                                   : PROPAGATION_FETCH_RETRY_MS;
    if (lastPropagationFetchMs == 0 || now - lastPropagationFetchMs > interval) {
      lastPropagationFetchOk = fetchPropagationData();
      lastPropagationFetchMs = now;

    }
  }

  // ObsĹ‚uga kolejki QRZ/Callook (asynchronicznie)
  if (wifiConnected && qrzQueueLen > 0) {
    unsigned long now = millis();
    unsigned long qrzInterval = getQrzLookupIntervalMs();
    if (now - lastQrzLookupMs >= qrzInterval) {
      for (int i = 0; i < qrzQueueLen; i++) {
        if (now < qrzQueue[i].nextTryMs) {
          continue;
        }
        String grid;
        String country;
        String name;
        String email;
        String qth;
        double lat = 0.0;
        double lon = 0.0;
        bool hasLatLon = false;
        bool ok = fetchCallookCallsignInfo(qrzQueue[i].callsign, grid, country, name, email, qth, lat, lon, hasLatLon);
        lastQrzLookupMs = now;
        if (ok) {
          updateSpotsWithQrz(qrzQueue[i].callsign, grid, country, name, lat, lon, hasLatLon);
          removeQrzQueueAt(i);
        } else {
          qrzQueue[i].attempts++;
          if (qrzQueue[i].attempts >= QRZ_RETRY_LIMIT) {
            removeQrzQueueAt(i);
          } else {
            qrzQueue[i].nextTryMs = now + QRZ_RETRY_DELAY_MS;
          }
        }
        break; // jedna prĂ„â€šĹ‚ba na iteracjÄ‚â€žĂ˘â€žË
      }
    }
  }

  // Aktualizuj pogodÄ‚â€žĂ˘â€žË (OpenWeather)
  if (wifiConnected) {
    unsigned long now = millis();
    unsigned long interval = lastWeatherFetchOk ? WEATHER_FETCH_INTERVAL_MS
                                               : WEATHER_FETCH_RETRY_MS;
    if (lastWeatherFetchMs == 0 || now - lastWeatherFetchMs > interval) {
      lastWeatherFetchOk = fetchWeatherData();
      lastWeatherFetchMs = now;

    }
  }

  // Przetwarzaj maks. 1 liniÄ‚â€žĂ˘â€žË telnet na iteracjÄ‚â€žĂ˘â€žË (Ă„Ä…Ă„Ëťeby nie zamroziÄ‚â€žĂ˘â‚¬Ë‡ WWW/UI)
  if (pendingTelnetLine.length() > 0) {
    LOGV_PRINT("[LOOP] Przetwarzanie linii telnet, len=");
    LOGV_PRINTLN(pendingTelnetLine.length());
    LOGV_PRINT("[LOOP] Linia: ");
    if (pendingTelnetLine.length() > 80) {
      LOGV_PRINTLN(pendingTelnetLine.substring(0, 80) + "...");
    } else {
      LOGV_PRINTLN(pendingTelnetLine);
    }
    
    String line = pendingTelnetLine;
    pendingTelnetLine = ""; // WyczyĂ„Ä…Ă˘â‚¬ĹźÄ‚â€žĂ˘â‚¬Ë‡ PRZED parsowaniem (Ă„Ä…Ă„Ëťeby nie gromadziÄ‚â€žĂ˘â‚¬Ë‡)
    yield(); // Feed watchdog przed dĂ„Ä…Ă˘â‚¬ĹˇugÄ‚â€žĂ˘â‚¬Â¦ operacjÄ‚â€žĂ˘â‚¬Â¦
    
    DXSpot spot;
    unsigned long parseStart = millis();
    if (parseDXSpot(line, spot)) {
      unsigned long parseTime = millis() - parseStart;
      if (parseTime > 50) {
        LOGV_PRINT("[LOOP] WARNING: parseDXSpot zajÄ‚â€žĂ˘â€žËĂ„Ä…Ă˘â‚¬Ĺˇo ");
        LOGV_PRINT(parseTime);
        LOGV_PRINTLN("ms");
      }
      addSpot(spot);
      // Aktualizuj wyĂ„Ä…Ă˘â‚¬Ĺźwietlacz TFT z nowymi spotami (tylko jeĂ„Ä…Ă˘â‚¬Ĺźli jesteĂ„Ä…Ă˘â‚¬Ĺźmy na ekranie 2)

    }
  }

  if (pendingPotaLine.length() > 0) {
    String line = pendingPotaLine;
    pendingPotaLine = "";
    
    Serial.print("[POTA DEBUG] Otrzymano linie: ");
    Serial.println(line);

    DXSpot spot;
    if (parsePotaSpot(line, spot)) {
      Serial.print("[POTA DEBUG] Parsowanie OK, mode=");
      Serial.print(spot.mode);
      Serial.print(", callsign=");
      Serial.println(spot.callsign);
      
      if (spot.mode == "SSB") {
        applyQrzCacheToSpot(spot, QRZ_CACHE_TTL_MS);
        addPotaSpot(spot);
        Serial.println("[POTA DEBUG] Spot dodany!");
        
        bool qrzConfigured = (qrzUsername.length() > 0 && qrzPassword.length() > 0);
        if (qrzConfigured && spot.country.length() == 0 && spot.callsign.length() > 0) {
          String call = spot.callsign;
          call.toUpperCase();
          enqueueQrzLookup(call);
        }
      } else {
        Serial.print("[POTA DEBUG] Odrzucono - nie SSB: ");
        Serial.println(spot.mode);
      }
    } else {
      Serial.println("[POTA DEBUG] Parsowanie nieudane");
    }
  }

  // Watchdog telnet: jeĂ„Ä…Ă˘â‚¬Ĺźli brak danych z clustra przez dĂ„Ä…Ă˘â‚¬ĹˇuĂ„Ä…Ă„Ëťszy czas, zrĂ„â€šĹ‚b reconnect
  if (telnetConnected && telnetClient.connected()) {
    unsigned long now = millis();
    unsigned long inactivityTime = now - lastTelnetRxMs;
    if (inactivityTime > TELNET_INACTIVITY_RECONNECT_MS) {
      LOGV_PRINT("[LOOP] WARNING: Brak danych z DX Cluster przez ");
      LOGV_PRINT(inactivityTime / 1000);
      LOGV_PRINTLN(" sekund -> reconnect");
      telnetClient.stop();
      telnetConnected = false;
      clusterLoginSent = false;
      clusterLoginScheduled = false;
      // connectToCluster() odpali siÄ‚â€žĂ˘â€žË w kolejnych iteracjach
    } else if (inactivityTime > 320000) { // OstrzeĂ„Ä…Ă„Ëťenie po 4 minutach
      LOGV_PRINT("[LOOP] WARNING: Brak danych z Cluster przez ");
      LOGV_PRINT(inactivityTime / 1000);
      LOGV_PRINTLN(" sekund (blisko timeout)");
    }
  }

  if (potaTelnetConnected && potaTelnetClient.connected()) {
    unsigned long now = millis();
    unsigned long inactivityTime = now - lastPotaRxMs;
    if (inactivityTime > POTA_TELNET_INACTIVITY_RECONNECT_MS) {
      LOGV_PRINT("[LOOP] WARNING: Brak danych z POTA Cluster przez ");
      LOGV_PRINT(inactivityTime / 1000);
      LOGV_PRINTLN(" sekund -> reconnect");
      potaTelnetClient.stop();
      potaTelnetConnected = false;
      potaLoginSent = false;
      potaLoginScheduled = false;
    }
  }

  // WyĂ„Ä…Ă˘â‚¬Ĺźlij znak (login) jeĂ„Ä…Ă˘â‚¬Ĺźli zaplanowany
  if (telnetConnected && telnetClient.connected() && clusterLoginScheduled) {
    if ((long)(millis() - clusterSendLoginAtMs) >= 0) {
      LOGV_PRINTLN("[LOOP] WysyĂ„Ä…Ă˘â‚¬Ĺˇanie loginu do Cluster...");
      clusterLoginScheduled = false;
      if (!clusterLoginSent) {
        String login = userCallsign;
        login.trim();
        if (login.length() == 0) {
          login = DEFAULT_CALLSIGN;
        }
        
        // JeĂ„Ä…Ă˘â‚¬Ĺźli mamy lokator, dodaj go do loginu w formacie: callsign/locator
        // Format zgodny z CC-Cluster (dxspots.com) i wiÄ‚â€žĂ˘â€žËkszoĂ„Ä…Ă˘â‚¬ĹźciÄ‚â€žĂ˘â‚¬Â¦ DX ClusterĂ„â€šĹ‚w
        if (userLocator.length() >= 4) {
          login += "/";
          login += userLocator;
          Serial.print("[CLUSTER] Login -> ");
          Serial.print(login);
          Serial.print(" (callsign/locator)");
          if (userCallsign.length() == 0) {
            Serial.print(" - domyĂ„Ä…Ă˘â‚¬Ĺźlny znak, ustaw swĂ„â€šĹ‚j w Config");
          }
          Serial.println();
        } else {
          Serial.print("[CLUSTER] Login -> ");
          Serial.print(login);
          if (userCallsign.length() == 0) {
            Serial.print(" (domyĂ„Ä…Ă˘â‚¬Ĺźlny, ustaw swĂ„â€šĹ‚j znak w Config)");
          } else {
            Serial.print(" (bez lokatora - ustaw w Config dla lepszej funkcjonalnoĂ„Ä…Ă˘â‚¬Ĺźci)");
          }
          Serial.println();
        }
        
        telnetClient.print(login);
        telnetClient.print("\r\n");
        clusterLoginSent = true;
        lastClusterKeepAliveMs = millis();
        Serial.println("[CLUSTER] Login wysĂ„Ä…Ă˘â‚¬Ĺˇany");
        
        // Po zalogowaniu wyĂ„Ä…Ă˘â‚¬Ĺźlij komendy konfiguracyjne CC-Cluster (z opĂ„â€šĹ‚Ă„Ä…ÄąĹşnieniem)
        // Daj czas clusterowi na przetworzenie loginu
        delay(500);
        sendClusterConfigCommands();
      }
    }
  }

  if (potaTelnetConnected && potaTelnetClient.connected() && potaLoginScheduled) {
    if ((long)(millis() - potaSendLoginAtMs) >= 0) {
      potaLoginScheduled = false;
      if (!potaLoginSent) {
        String login = userCallsign;
        login.trim();
        if (login.length() == 0) {
          login = DEFAULT_CALLSIGN;
        }
        if (userLocator.length() >= 4) {
          login += "/";
          login += userLocator;
        }
        potaTelnetClient.print(login);
        potaTelnetClient.print("\r\n");
        potaLoginSent = true;
        lastPotaKeepAliveMs = millis();
      }
    }
  }

  // Keepalive: niektĂ„â€šĹ‚re klastry zrywajÄ‚â€žĂ˘â‚¬Â¦ idle telnet, wiÄ‚â€žĂ˘â€žËc co ~30s wysyĂ„Ä…Ă˘â‚¬Ĺˇamy CRLF
  if (telnetConnected && telnetClient.connected()) {
    unsigned long now = millis();
    if (now - lastClusterKeepAliveMs > 30000) {
      LOGV_PRINTLN("[LOOP] WysyĂ„Ä…Ă˘â‚¬Ĺˇanie keepalive do Cluster");
      telnetClient.print("\r\n");
      lastClusterKeepAliveMs = now;
    }
  }

  if (potaTelnetConnected && potaTelnetClient.connected()) {
    unsigned long now = millis();
    if (now - lastPotaKeepAliveMs > 30000) {
      potaTelnetClient.print("\r\n");
      lastPotaKeepAliveMs = now;
    }
  }
  
  // ObsĹ‚uga MQTT PSK Reporter (w tle, nawet gdy nie na ekranie PSK)
  if (pskMqttEnabled) {
    loopPskMqtt();
  }
  
  // Dynamiczne odĹ›wieĹĽanie ekranu ISS co 1 sekundÄ™
#ifdef ENABLE_TFT_DISPLAY
  static unsigned long lastIssTftRefresh = 0;
  if (currentScreen == SCREEN_ISS_PASS_TRACKING && tftInitialized && !inMenu) {
    if (millis() - lastIssTftRefresh >= 1000) {
      lastIssTftRefresh = millis();
      updateIssDynamicDisplay();
    }
  }

  // Nocne obnizenie jasnosci - przelicz przy zmianie pory dnia (ikona OWM).
  static bool lastNightState = false;
  static bool lastNightStateInit = false;
  if (tftNightDimmingPercent > 0) {
    bool night = tftIsNightTime();
    if (!lastNightStateInit || night != lastNightState) {
      lastNightState = night;
      lastNightStateInit = true;
      applyEffectiveBacklight();
    }
  }
#endif
  
  // Aktualizacja pomiaru baterii (TP4056 + 18650)
#ifdef BATTERY_MONITORING_ENABLED
  if (updateBatteryStatus()) {
    // Szybka aktualizacja ikony baterii (bez odĹ›wieĹĽania caĹ‚ego nagĹ‚Ăłwka)
    drawBatteryQuickUpdate();
    
    // Debug co 30 sekund - stan baterii
    static unsigned long lastBatteryLogMs = 0;
    if (millis() - lastBatteryLogMs >= 30000) {
      lastBatteryLogMs = millis();
      Serial.printf("[BATTERY] %.2fV (%d%%) %s\n", 
                    batteryVoltage, batteryPercentage,
                    batteryCharging ? "[CHARGING]" : "");
    }
    
    // OstrzeĹĽenie przy niskim poziomie baterii (< 10%)
    if (batteryPercentage < 10 && !batteryLowWarningShown) {
      Serial.println("[BATTERY] UWAGA: Niski poziom baterii! PodĹ‚Ä…cz Ĺ‚adowarkÄ™.");
      batteryLowWarningShown = true;
    }
    // Reset flagi ostrzeĹĽenia gdy bateria siÄ™ naĹ‚aduje
    if (batteryPercentage > 20 && batteryLowWarningShown) {
      batteryLowWarningShown = false;
    }
  }
#endif

  // Feed watchdog przed delay
  yield();
  delay(10); // MaĹ‚e opĂłĹşnienie dla stabilnoĹ›ci
  
}
