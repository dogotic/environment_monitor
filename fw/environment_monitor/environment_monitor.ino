#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <WiFi.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_SHT4x.h>   // uncomment when sensor is wired up

// ── Pins ──────────────────────────────────────────────────────────
#define PIN_SDA  5
#define PIN_SCL  6
#define TFT_CS   10
#define TFT_DC    9
#define TFT_MOSI 11
#define TFT_SCK  13
#define TFT_RST   8
#define TFT_MISO 12

// ── NTP / timezone ────────────────────────────────────────────────
#define TZ_INFO  "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_HOST "pool.ntp.org"

// ── Comfort bar colors (RGB565) ───────────────────────────────────
#define COLOR_DRY     0xFD20   // amber  (255, 165,   0)
#define COLOR_COMFORT 0x0720   // green  (  0, 228,   0)
#define COLOR_WET     0x235F   // blue   ( 32, 104, 248)

// ── BLE UUIDs ─────────────────────────────────────────────────────
#define BLE_SVC  "12345678-1234-5678-1234-56789abcdef0"
#define BLE_CRED "12345678-1234-5678-1234-56789abcdef1"
#define BLE_STAT "12345678-1234-5678-1234-56789abcdef2"

// ── State ─────────────────────────────────────────────────────────
enum AppState { ST_PROVISION, ST_CONNECTING, ST_ONLINE };

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCK, TFT_RST, TFT_MISO);
Preferences prefs;

AppState      appState     = ST_PROVISION;
volatile bool doConnect    = false;
bool          bleConnected = false;

float tempC = NAN;
float rhPct = NAN;

NimBLECharacteristic* charStat = nullptr;
Adafruit_SHT4x sht4;
bool sensorOk = false;

// ── Helpers ───────────────────────────────────────────────────────
static String jsonField(const String& j, const char* key) {
    String needle = String('"') + key + "\":\"";
    int s = j.indexOf(needle);
    if (s < 0) return "";
    s += needle.length();
    int e = j.indexOf('"', s);
    return j.substring(s, e);
}

static void bleNotify(const char* msg) {
    if (charStat) { charStat->setValue(msg); charStat->notify(); }
}

// print centered text — bg color overwrites old text, no fillScreen needed
static void printCenter(const char* str, int16_t y, uint8_t sz,
                        uint16_t fg, uint16_t bg = ILI9341_BLACK) {
    tft.setTextSize(sz);
    tft.setTextColor(fg, bg);
    int16_t x1, y1; uint16_t w, h;
    tft.getTextBounds(str, 0, y, &x1, &y1, &w, &h);
    tft.setCursor((320 - (int16_t)w) / 2, y);
    tft.print(str);
}

// ── BLE callbacks ─────────────────────────────────────────────────
struct CredCB : NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
        String val  = c->getValue().c_str();
        String ssid = jsonField(val, "ssid");
        String pass = jsonField(val, "pass");
        if (ssid.isEmpty()) { bleNotify("ERR:no_ssid"); return; }
        prefs.begin("wifi", false);
        prefs.putString("ssid", ssid);
        prefs.putString("pass", pass);
        prefs.end();
        bleNotify("connecting");
        doConnect = true;
    }
};

struct SrvCB : NimBLEServerCallbacks {
    void onConnect(NimBLEServer*, NimBLEConnInfo&) override { bleConnected = true; }
    void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
        bleConnected = false;
        NimBLEDevice::startAdvertising();
    }
};

static void initBLE() {
    NimBLEDevice::init("EnvMonitor");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    auto* srv = NimBLEDevice::createServer();
    srv->setCallbacks(new SrvCB());
    auto* svc  = srv->createService(BLE_SVC);
    auto* cred = svc->createCharacteristic(BLE_CRED, NIMBLE_PROPERTY::WRITE);
    cred->setCallbacks(new CredCB());
    charStat = svc->createCharacteristic(
        BLE_STAT, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    charStat->setValue("ready");
    svc->start();

    // Put name + service UUID in advertisement data (not scan response) so
    // Android 12+ passive BLE scan (neverForLocation) can discover the device.
    NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
    pAdv->setName("EnvMonitor");
    pAdv->addServiceUUID(BLE_SVC);
    pAdv->start();
}

// ── WiFi ──────────────────────────────────────────────────────────
static bool connectWiFi() {
    prefs.begin("wifi", true);
    String ssid = prefs.getString("ssid", "");
    String pass = prefs.getString("pass", "");
    prefs.end();
    if (ssid.isEmpty()) return false;
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(200);
    if (WiFi.status() == WL_CONNECTED) {
        configTzTime(TZ_INFO, NTP_HOST);
        bleNotify(("ok:" + WiFi.localIP().toString()).c_str());
        return true;
    }
    WiFi.disconnect();
    bleNotify("fail");
    return false;
}

// ── Display ───────────────────────────────────────────────────────

// called when leaving ST_ONLINE so next entry redraws the frame
static bool onlineFrameDrawn = false;
static void invalidateOnline() { onlineFrameDrawn = false; }

static void drawProvision() {
    invalidateOnline();
    tft.fillScreen(ILI9341_BLACK);
    printCenter("WiFi Setup",      55,  3, ILI9341_WHITE);
    printCenter("BLE: EnvMonitor", 100, 2, ILI9341_CYAN);
    printCenter(bleConnected ? "Spojen" : "Ceka BLE...", 130, 2,
                bleConnected ? ILI9341_GREEN : ILI9341_DARKGREY);
}

static void drawConnecting(const String& ssid) {
    invalidateOnline();
    tft.fillScreen(ILI9341_BLACK);
    printCenter("Spajam na WiFi:", 90,  2, ILI9341_YELLOW);
    printCenter(ssid.c_str(),      125, 3, ILI9341_WHITE);
}

static void drawOnline() {
    struct tm t;
    bool timeOk = getLocalTime(&t, 100);

    // ── Static frame (drawn once) ─────────────────────────────────
    if (!onlineFrameDrawn) {
        tft.fillScreen(ILI9341_BLACK);
        // °C: small circle + "C" in top-left
        tft.drawCircle(5, 5, 3, ILI9341_WHITE);
        tft.setTextSize(2); tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
        tft.setCursor(11, 2); tft.print("C");
        // "%" in top-right
        tft.setCursor(305, 2); tft.print("%");
        // separator above bottom row
        tft.drawFastHLine(0, 143, 320, ILI9341_DARKGREY);
        onlineFrameDrawn = true;
    }

    // ── Temperature (textSize 5, left) ────────────────────────────
    static float lastTemp = NAN;
    bool tempChanged = (isnan(tempC) != isnan(lastTemp)) ||
                       (!isnan(tempC) && fabsf(tempC - lastTemp) >= 0.05f);
    if (tempChanged) {
        lastTemp = tempC;
        char buf[8];
        if (isnan(tempC)) strcpy(buf, " --.-");
        else              sprintf(buf, "%5.1f", tempC);
        tft.setTextSize(5); tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
        tft.setCursor(0, 22);
        tft.print(buf);
    }

    // ── Humidity (textSize 6, right-aligned) ──────────────────────
    static float lastRh = NAN;
    bool rhChanged = (isnan(rhPct) != isnan(lastRh)) ||
                     (!isnan(rhPct) && fabsf(rhPct - lastRh) >= 0.05f);
    if (rhChanged) {
        lastRh = rhPct;
        char buf[5];
        if (isnan(rhPct)) strcpy(buf, "--");
        else              sprintf(buf, "%.0f", rhPct);
        tft.fillRect(170, 22, 148, 50, ILI9341_BLACK);
        tft.setTextSize(6); tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
        int16_t x1, y1; uint16_t w, h;
        tft.getTextBounds(buf, 170, 22, &x1, &y1, &w, &h);
        tft.setCursor(316 - (int16_t)w, 22);
        tft.print(buf);
    }

    // ── Comfort bar + humidity marker ─────────────────────────────
    // Redrawn only when humidity changes by ≥0.5% or on first draw
    static float lastRhBar = NAN;
    bool barChanged = isnan(lastRhBar) ||
                      (isnan(rhPct) != isnan(lastRhBar)) ||
                      (!isnan(rhPct) && fabsf(rhPct - lastRhBar) >= 0.5f);
    if (barChanged) {
        lastRhBar = rhPct;
        // Clear marker zone between temp text and bar
        tft.fillRect(0, 73, 320, 36, ILI9341_BLACK);
        // Bar segments: DRY 0–30%, COMFORT 30–60%, WET 60–100%
        const int16_t BY = 110, BH = 26;
        const int16_t dryW = 96, compW = 96, wetW = 128;
        tft.fillRect(0,              BY, dryW,  BH, COLOR_DRY);
        tft.fillRect(dryW,           BY, compW, BH, COLOR_COMFORT);
        tft.fillRect(dryW + compW,   BY, wetW,  BH, COLOR_WET);
        tft.setTextSize(1);
        tft.setTextColor(ILI9341_WHITE, COLOR_DRY);
        tft.setCursor(dryW / 2 - 9,              BY + 9); tft.print("DRY");
        tft.setTextColor(ILI9341_WHITE, COLOR_COMFORT);
        tft.setCursor(dryW + compW / 2 - 21,     BY + 9); tft.print("COMFORT");
        tft.setTextColor(ILI9341_WHITE, COLOR_WET);
        tft.setCursor(dryW + compW + wetW / 2 - 9, BY + 9); tft.print("WET");
        // Triangle marker pointing down toward bar
        if (!isnan(rhPct)) {
            int16_t mx = constrain((int16_t)(rhPct / 100.0f * 316.0f + 2.0f), 6, 314);
            tft.fillTriangle(mx - 6, 74, mx + 6, 74, mx, 107, ILI9341_WHITE);
        }
    }

    // ── Clock (HH:MM, bottom-left) ────────────────────────────────
    static int8_t lastMin = -2;
    if (timeOk && t.tm_min != lastMin) {
        lastMin = t.tm_min;
        tft.setTextSize(2); tft.setTextColor(ILI9341_LIGHTGREY, ILI9341_BLACK);
        tft.setCursor(2, 158); tft.print(t.tm_hour < 12 ? "AM" : "PM");
        char tbuf[6];
        int h12 = t.tm_hour % 12; if (h12 == 0) h12 = 12;
        sprintf(tbuf, "%02d:%02d", h12, t.tm_min);
        tft.setTextSize(4); tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
        tft.setCursor(34, 150); tft.print(tbuf);
    } else if (!timeOk && lastMin != -2) {
        lastMin = -2;
        tft.fillRect(0, 148, 160, 36, ILI9341_BLACK);
        tft.setTextSize(4); tft.setTextColor(ILI9341_DARKGREY, ILI9341_BLACK);
        tft.setCursor(34, 150); tft.print("--:--");
    }

    // ── Date (DD/MM, bottom-right) ────────────────────────────────
    static int8_t lastDay = -1;
    if (timeOk && t.tm_mday != lastDay) {
        lastDay = t.tm_mday;
        char dbuf[6];
        sprintf(dbuf, "%02d/%02d", t.tm_mday, t.tm_mon + 1);
        tft.setTextSize(4); tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
        tft.setCursor(180, 150); tft.print(dbuf);
    }
}

// ── Setup / Loop ──────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Wire.begin(PIN_SDA, PIN_SCL);

    tft.begin();
    tft.setRotation(1);
    tft.fillScreen(ILI9341_BLACK);
    printCenter("EnvMonitor",    95,  3, ILI9341_WHITE);
    printCenter("pokretanje...", 135, 2, ILI9341_CYAN);

    sensorOk = sht4.begin(&Wire);
    if (sensorOk) sht4.setPrecision(SHT4X_HIGH_PRECISION);

    initBLE();

    prefs.begin("wifi", true);
    String savedSSID = prefs.getString("ssid", "");
    prefs.end();

    if (!savedSSID.isEmpty()) {
        appState = ST_CONNECTING;
        drawConnecting(savedSSID);
        appState = connectWiFi() ? ST_ONLINE : ST_PROVISION;
    }
}

void loop() {
    static uint32_t lastDraw   = 0;
    static uint32_t lastSensor = 0;

    if (doConnect) {
        doConnect = false;
        prefs.begin("wifi", true);
        String ssid = prefs.getString("ssid", "");
        prefs.end();
        appState = ST_CONNECTING;
        drawConnecting(ssid);
        appState = connectWiFi() ? ST_ONLINE : ST_PROVISION;
    }

    if (appState == ST_ONLINE && WiFi.status() != WL_CONNECTED) {
        prefs.begin("wifi", true);
        String ssid = prefs.getString("ssid", "");
        String pass = prefs.getString("pass", "");
        prefs.end();
        if (!ssid.isEmpty()) WiFi.begin(ssid.c_str(), pass.c_str());
    }

    if (millis() - lastDraw >= 1000) {
        lastDraw = millis();
        if      (appState == ST_PROVISION) drawProvision();
        else if (appState == ST_ONLINE)    drawOnline();
    }

    if (sensorOk && millis() - lastSensor >= 5000) {
        lastSensor = millis();
        sensors_event_t h, tmp;
        sht4.getEvent(&h, &tmp);
        tempC = tmp.temperature;
        rhPct = h.relative_humidity;
    }
}
