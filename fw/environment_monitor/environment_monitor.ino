#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <NimBLEDevice.h>
#include <time.h>
#include <sys/time.h>
#include <Wire.h>
#include <Adafruit_SHT4x.h>

// ── Pins ──────────────────────────────────────────────────────────
#define PIN_SDA  5
#define PIN_SCL  6
#define TFT_CS   10
#define TFT_DC    9
#define TFT_MOSI 11
#define TFT_SCK  13
#define TFT_RST   8
#define TFT_MISO 12

// ── Timezone ──────────────────────────────────────────────────────
// RTC i sistemski sat drže UTC; lokalno vrijeme se računa preko TZ.
#define TZ_INFO  "CET-1CEST,M3.5.0,M10.5.0/3"

// ── RTC (DS3231M, I2C) ────────────────────────────────────────────
#define RTC_ADDR        0x68
#define RTC_REG_TIME    0x00
#define RTC_REG_CONTROL 0x0E
#define RTC_REG_STATUS  0x0F
#define RTC_OSF         0x80     // Oscillator Stop Flag (vrijeme nevažeće)
#define RTC_EOSC        0x80     // Control: 1 = oscilator stoji na bateriji
#define RTC_RESYNC_MS   3600000UL // svaki sat sistemski sat prema RTC-u

// Važeći raspon vremena: 2024-01-01 .. 2100-01-01 (UTC)
// (gornja granica hvata npr. epoch u milisekundama)
#define TIME_VALID_MIN  1704067200LL
#define TIME_VALID_MAX  4102444800LL

// ── Comfort bar colors (RGB565) ───────────────────────────────────
#define COLOR_DRY     0xFD20   // amber  (255, 165,   0)
#define COLOR_COMFORT 0x0720   // green  (  0, 228,   0)
#define COLOR_WET     0x235F   // blue   ( 32, 104, 248)

// ── BLE UUIDs ─────────────────────────────────────────────────────
#define BLE_SVC  "12345678-1234-5678-1234-56789abcdef0"
#define BLE_TIME "12345678-1234-5678-1234-56789abcdef1"   // write: postavi vrijeme, read: trenutni epoch
#define BLE_STAT "12345678-1234-5678-1234-56789abcdef2"   // read/notify: status

// ── Globals ───────────────────────────────────────────────────────
Adafruit_ILI9341 tft(&SPI, TFT_DC, TFT_CS, TFT_RST);   // hardware SPI
Adafruit_SHT4x   sht4;

bool  sensorOk     = false;
bool  rtcOk        = false;
float tempC        = NAN;
float rhPct        = NAN;

volatile bool bleConnected = false;
// BLE callback (NimBLE task) → loop(); 64-bit vrijednost pa ide pod lock
portMUX_TYPE  timeMux      = portMUX_INITIALIZER_UNLOCKED;
bool          timePending  = false;
int64_t       pendingEpoch = 0;

NimBLECharacteristic* charStat = nullptr;

// ── Helpers ───────────────────────────────────────────────────────
static void bleNotify(const char* msg) {
    if (charStat) { charStat->setValue(msg); charStat->notify(); }
}

static bool timeValid() {
    time_t now = time(nullptr);
    return now >= TIME_VALID_MIN && now < TIME_VALID_MAX;
}

static void setSystemTime(time_t epoch) {
    struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
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

// ── DS3231M ───────────────────────────────────────────────────────
static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t bin2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

// dani od 1970-01-01 za (y, m, d) — proleptički gregorijanski (H. Hinnant)
static long daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

static bool rtcReadReg(uint8_t reg, uint8_t* buf, uint8_t len) {
    Wire.beginTransmission(RTC_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)RTC_ADDR, len) != len) return false;
    for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
    return true;
}

static bool rtcWriteReg(uint8_t reg, const uint8_t* buf, uint8_t len) {
    Wire.beginTransmission(RTC_ADDR);
    Wire.write(reg);
    Wire.write(buf, len);
    return Wire.endTransmission() == 0;
}

static bool rtcProbe() {
    Wire.beginTransmission(RTC_ADDR);
    return Wire.endTransmission() == 0;
}

// Vraća UTC epoch ili -1 ako RTC nema važeće vrijeme (OSF ili I2C greška)
static time_t rtcRead() {
    uint8_t st;
    if (!rtcReadReg(RTC_REG_STATUS, &st, 1) || (st & RTC_OSF)) return -1;
    uint8_t r[7];
    if (!rtcReadReg(RTC_REG_TIME, r, 7)) return -1;
    int sec  = bcd2bin(r[0] & 0x7F);
    int min  = bcd2bin(r[1] & 0x7F);
    int hour;
    if (r[2] & 0x40) {                       // 12h mod (ne bismo trebali, ali za svaki slučaj)
        hour = bcd2bin(r[2] & 0x1F) % 12;
        if (r[2] & 0x20) hour += 12;
    } else {
        hour = bcd2bin(r[2] & 0x3F);
    }
    int day  = bcd2bin(r[4] & 0x3F);
    int mon  = bcd2bin(r[5] & 0x1F);
    int year = 2000 + bcd2bin(r[6]) + ((r[5] & 0x80) ? 100 : 0);
    if (mon < 1 || mon > 12 || day < 1 || day > 31 || hour > 23 || min > 59 || sec > 59)
        return -1;
    return (time_t)daysFromCivil(year, mon, day) * 86400L + hour * 3600L + min * 60L + sec;
}

static bool rtcWrite(time_t epoch) {
    struct tm u;
    gmtime_r(&epoch, &u);
    int yr = u.tm_year + 1900 - 2000;
    if (yr < 0 || yr > 199) return false;
    uint8_t r[7] = {
        bin2bcd(u.tm_sec),
        bin2bcd(u.tm_min),
        bin2bcd(u.tm_hour),                          // 24h mod
        (uint8_t)(u.tm_wday == 0 ? 7 : u.tm_wday),   // 1..7
        bin2bcd(u.tm_mday),
        (uint8_t)(bin2bcd(u.tm_mon + 1) | (yr >= 100 ? 0x80 : 0)),
        bin2bcd(yr % 100),
    };
    if (!rtcWriteReg(RTC_REG_TIME, r, 7)) return false;
    // EOSC=0 → oscilator radi i na bateriji
    uint8_t ctl;
    if (rtcReadReg(RTC_REG_CONTROL, &ctl, 1) && (ctl & RTC_EOSC)) {
        ctl &= ~RTC_EOSC;
        if (!rtcWriteReg(RTC_REG_CONTROL, &ctl, 1)) return false;
    }
    // obriši OSF (i ostale flagove), ostavi 32kHz izlaz ugašen
    uint8_t st = 0x00;
    return rtcWriteReg(RTC_REG_STATUS, &st, 1);
}

// Sistemski sat ← RTC (ako RTC postoji i ima važeće vrijeme)
static bool syncFromRtc() {
    if (!rtcOk) return false;
    time_t e = rtcRead();
    if (e < TIME_VALID_MIN || e >= TIME_VALID_MAX) return false;
    setSystemTime(e);
    return true;
}

// ── BLE ───────────────────────────────────────────────────────────
// Prihvaćeni formati upisa u BLE_TIME:
//   "1763650000"            → Unix epoch (UTC, sekunde)
//   "2026-11-20 14:30:00"   → lokalno vrijeme (CET/CEST), sekunde opcionalne
// Vraća epoch (UTC) ili -1 ako format ne valja; raspon provjerava pozivatelj
static int64_t parseTime(const std::string& raw) {
    String s = String(raw.c_str());
    s.trim();
    if (s.isEmpty()) return -1;

    bool allDigits = true;
    for (size_t i = 0; i < s.length(); i++)
        if (!isDigit(s[i])) { allDigits = false; break; }
    if (allDigits) {
        if (s.length() > 12) return -1;          // ms epoch i slično — ne pokušavaj
        return strtoll(s.c_str(), nullptr, 10);
    }

    int Y, M, D, h, m, sec = 0;
    int n = sscanf(s.c_str(), "%d-%d-%d%*[ T]%d:%d:%d", &Y, &M, &D, &h, &m, &sec);
    if (n < 5) return -1;
    if (M < 1 || M > 12 || D < 1 || D > 31 || h > 23 || m > 59 || sec > 59) return -1;
    struct tm t = {};
    t.tm_year  = Y - 1900;
    t.tm_mon   = M - 1;
    t.tm_mday  = D;
    t.tm_hour  = h;
    t.tm_min   = m;
    t.tm_sec   = sec;
    t.tm_isdst = -1;                 // neka mktime sam odredi CET/CEST
    return (int64_t)mktime(&t);
}

struct TimeCB : NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
        int64_t e = parseTime(c->getValue());
        if (e < TIME_VALID_MIN || e >= TIME_VALID_MAX) { bleNotify("err:format"); return; }
        // I2C/RTC radimo u loop(), ne u BLE tasku (dijeli bus sa SHT4x)
        portENTER_CRITICAL(&timeMux);
        pendingEpoch = e;
        timePending  = true;
        portEXIT_CRITICAL(&timeMux);
    }
    void onRead(NimBLECharacteristic* c, NimBLEConnInfo&) override {
        char buf[24];
        snprintf(buf, sizeof(buf), "%lld", timeValid() ? (long long)time(nullptr) : 0LL);
        c->setValue(buf);
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
    auto* svc = srv->createService(BLE_SVC);
    auto* chTime = svc->createCharacteristic(
        BLE_TIME, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
    chTime->setCallbacks(new TimeCB());
    charStat = svc->createCharacteristic(
        BLE_STAT, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    charStat->setValue(rtcOk ? "ready:rtc" : "ready:local");

    // Put name + service UUID in advertisement data (not scan response) so
    // Android 12+ passive BLE scan (neverForLocation) can discover the device.
    NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
    pAdv->setName("EnvMonitor");
    pAdv->addServiceUUID(BLE_SVC);
    pAdv->start();
}

// Obradi vrijeme primljeno preko BLE-a (poziva se iz loop())
static void applyPendingTime() {
    portENTER_CRITICAL(&timeMux);
    bool pending = timePending;
    time_t e     = (time_t)pendingEpoch;
    timePending  = false;
    portEXIT_CRITICAL(&timeMux);
    if (!pending) return;
    setSystemTime(e);

    if (!rtcOk) rtcOk = rtcProbe();          // možda je RTC naknadno spojen
    if (rtcOk && rtcWrite(e)) {
        bleNotify("ok:rtc");
        Serial.printf("Vrijeme postavljeno (RTC): %lld\n", (long long)e);
    } else {
        rtcOk = false;
        bleNotify("ok:local");
        Serial.printf("Vrijeme postavljeno (lokalno, bez RTC): %lld\n", (long long)e);
    }
}

// ── Display ───────────────────────────────────────────────────────
static void drawMain() {
    struct tm t;
    bool timeOk = timeValid();
    if (timeOk) { time_t now = time(nullptr); localtime_r(&now, &t); }

    // ── Static frame (drawn once) ─────────────────────────────────
    static bool frameDrawn = false;
    if (!frameDrawn) {
        tft.fillScreen(ILI9341_BLACK);
        // °C: small circle + "C" in top-left
        tft.drawCircle(5, 5, 3, ILI9341_WHITE);
        tft.setTextSize(2); tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
        tft.setCursor(11, 2); tft.print("C");
        // "%" in top-right
        tft.setCursor(305, 2); tft.print("%");
        // separator above bottom row
        tft.drawFastHLine(0, 143, 320, ILI9341_DARKGREY);
        frameDrawn = true;
    }

    // ── Temperature (textSize 5, left) ────────────────────────────
    static float lastTemp = NAN;
    bool tempChanged = (isnan(tempC) != isnan(lastTemp)) ||
                       (!isnan(tempC) && fabsf(tempC - lastTemp) >= 0.05f);
    if (tempChanged) {
        lastTemp = tempC;
        char buf[8];
        if (isnan(tempC)) strcpy(buf, " --.-");
        else              snprintf(buf, sizeof(buf), "%5.1f", tempC);
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
        else              snprintf(buf, sizeof(buf), "%.0f", rhPct);
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
    static bool  barDrawn  = false;
    bool barChanged = !barDrawn ||
                      (isnan(rhPct) != isnan(lastRhBar)) ||
                      (!isnan(rhPct) && fabsf(rhPct - lastRhBar) >= 0.5f);
    if (barChanged) {
        barDrawn  = true;
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
    // -1 = još ništa nacrtano, -2 = prikazano "--:--"
    static int8_t lastMin = -1, lastHour = -1;
    if (timeOk && (t.tm_min != lastMin || t.tm_hour != lastHour)) {
        lastMin = t.tm_min; lastHour = t.tm_hour;
        tft.setTextSize(2); tft.setTextColor(ILI9341_LIGHTGREY, ILI9341_BLACK);
        tft.setCursor(2, 158); tft.print(t.tm_hour < 12 ? "AM" : "PM");
        char tbuf[6];
        int h12 = t.tm_hour % 12; if (h12 == 0) h12 = 12;
        snprintf(tbuf, sizeof(tbuf), "%02d:%02d", h12, t.tm_min);
        tft.setTextSize(4); tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
        tft.setCursor(34, 150); tft.print(tbuf);
    } else if (!timeOk && lastMin != -2) {
        lastMin = -2; lastHour = -1;
        tft.fillRect(0, 148, 160, 36, ILI9341_BLACK);
        tft.setTextSize(4); tft.setTextColor(ILI9341_DARKGREY, ILI9341_BLACK);
        tft.setCursor(34, 150); tft.print("--:--");
    }

    // ── Date (DD/MM, bottom-right) ────────────────────────────────
    static int8_t lastDay = -1, lastMon = -1;
    if (timeOk && (t.tm_mday != lastDay || t.tm_mon != lastMon)) {
        lastDay = t.tm_mday; lastMon = t.tm_mon;
        char dbuf[6];
        snprintf(dbuf, sizeof(dbuf), "%02d/%02d", t.tm_mday, t.tm_mon + 1);
        tft.setTextSize(4); tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
        tft.setCursor(180, 150); tft.print(dbuf);
    } else if (!timeOk && lastDay != -2) {
        lastDay = -2; lastMon = -1;
        tft.setTextSize(4); tft.setTextColor(ILI9341_DARKGREY, ILI9341_BLACK);
        tft.setCursor(180, 150); tft.print("--/--");
    }

    // ── Status line (bottom): izvor vremena + BLE ────────────────
    // 0 = bez vremena, 1 = lokalno, 2 = RTC; +10 ako je BLE spojen
    static int8_t lastStatus = -1;
    int8_t status = (timeOk ? (rtcOk ? 2 : 1) : 0) + (bleConnected ? 10 : 0);
    if (status != lastStatus) {
        lastStatus = status;
        tft.fillRect(0, 218, 320, 22, ILI9341_BLACK);
        tft.setTextSize(2);
        tft.setCursor(4, 222);
        if (!timeOk) {
            tft.setTextColor(ILI9341_ORANGE, ILI9341_BLACK);
            tft.print("Postavi vrijeme (BLE)");
        } else if (rtcOk) {
            tft.setTextColor(ILI9341_DARKGREY, ILI9341_BLACK);
            tft.print("RTC");
        } else {
            tft.setTextColor(ILI9341_DARKGREY, ILI9341_BLACK);
            tft.print("bez RTC");
        }
        if (bleConnected) {
            tft.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
            tft.setCursor(280, 222); tft.print("BT");
        }
    }
}

// ── Setup / Loop ──────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Wire.begin(PIN_SDA, PIN_SCL, 25000);

    setenv("TZ", TZ_INFO, 1);
    tzset();

    tft.begin();
    tft.setRotation(1);
    tft.fillScreen(ILI9341_BLACK);
    printCenter("EnvMonitor",    95,  3, ILI9341_WHITE);
    printCenter("pokretanje...", 135, 2, ILI9341_CYAN);

    sensorOk = sht4.begin(&Wire);
    if (sensorOk) sht4.setPrecision(SHT4X_HIGH_PRECISION);

    rtcOk = rtcProbe();
    if (rtcOk) {
        if (syncFromRtc()) Serial.println("RTC: vrijeme ucitano");
        else               Serial.println("RTC: pronaden, ali vrijeme nije postavljeno");
    } else {
        Serial.println("RTC: nije pronaden, vrijeme samo lokalno");
    }

    initBLE();
}

void loop() {
    static uint32_t lastDraw   = 0;
    static uint32_t lastSensor = 0;
    static uint32_t lastRtcSync = 0;

    applyPendingTime();

    // Povremeno ispravi drift ESP32 sata prema DS3231M
    if (rtcOk && millis() - lastRtcSync >= RTC_RESYNC_MS) {
        lastRtcSync = millis();
        syncFromRtc();
    }

    if (millis() - lastDraw >= 1000) {
        lastDraw = millis();
        drawMain();
    }

    if (sensorOk && millis() - lastSensor >= 5000) {
        lastSensor = millis();
        sensors_event_t h, tmp;
        if (sht4.getEvent(&h, &tmp)) {
            tempC = tmp.temperature;
            rhPct = h.relative_humidity;
        } else {
            tempC = rhPct = NAN;                 // senzor ne odgovara → "--"
        }
    }
}
