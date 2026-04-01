/*
 * main.cpp — DeskNexus Main Sketch (PlatformIO)
 *
 * ESP32-WROOM-32 desk clock with:
 *  • Real-time clock (NTP synced)
 *  • Current weather (OpenWeatherMap)
 *  • Daily prayer times (Aladhan API)
 *  • Stock price monitor (Alpha Vantage)
 *  • Hybrid WiFi (STA first → AP captive-portal fallback)
 *  • Touch-based tab navigation
 *  • Auto-dim backlight
 *
 * Hardware: 2.8" ILI9341 TFT (240×320) + XPT2046 touch
 *           on an ESP32-2432S028R ("Cheap Yellow Display" / CYD)
 *
 * ── Required libraries (declared in platformio.ini — installed automatically) ──
 *  • TFT_eSPI            by Bodmer
 *  • XPT2046_Touchscreen by Paul Stoffregen
 *  • ArduinoJson         by Benoit Blanchon  (v6.x)
 *
 * ── TFT_eSPI Configuration ──────────────────────────────────────────────────
 *  TFT_eSPI pin and driver settings are configured via build_flags in
 *  platformio.ini. No manual editing of User_Setup.h is required.
 *
 * ── Configuration ────────────────────────────────────────────────────────────
 *  Edit src/config.h to set your API keys, city, timezone offset, and stock list.
 *
 * ── First-Time WiFi Setup ────────────────────────────────────────────────────
 *  1. Flash the sketch (pio run --target upload).
 *  2. If no credentials are stored, the device starts as Wi-Fi AP "DeskNexus-Setup".
 *  3. Connect to that network from your phone/laptop.
 *  4. Browse to http://192.168.4.1 and enter your home Wi-Fi credentials.
 *  5. The device restarts and connects automatically; credentials are saved.
 */

// ── Includes ──────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <time.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>

#include "config.h"
#include "settings.h"
#include "network.h"
#include "location_time.h"
#include "weather.h"
#include "prayer.h"
#include "stocks.h"
#include "time_sync.h"
#include "ui.h"

// ── Touchscreen ───────────────────────────────────────────────────────────
// The CYD board wires the XPT2046 to a separate SPI bus (not the display SPI).
// Touch SPI pins: SCK=25, MISO=39, MOSI=32, CS=33.
static SPIClass     touchSPI(VSPI);
static XPT2046_Touchscreen touch(TOUCH_CS_PIN, TOUCH_IRQ_PIN);

// ── Timing state ──────────────────────────────────────────────────────────
static unsigned long lastMinuteTick  = 0;   // 60 s — clock & status redraw
static unsigned long lastLightTick   = 0;   // 10 s — carousel, prayer countdown, banner
static unsigned long lastStockFetch  = 0;   // stock round-robin
static unsigned long lastNetCheck    = 0;   // WiFi reconnect check
static unsigned long lastThemeCheck  = 0;   // theme auto-switch (60 s)

static bool ntpSynced   = false;
static bool firstDraw   = true;

static bool refreshPrayerOnStartup(const struct tm& localTime) {
    if (!Prayer::needsRefreshForTime(localTime)) return true;
    return Prayer::fetchForTime(localTime);
}

// ── Helper: build date string ─────────────────────────────────────────────
static const char* DAY_NAMES[]   = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
static const char* MONTH_NAMES[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                     "Jul","Aug","Sep","Oct","Nov","Dec"};

static String buildDateString(const struct tm& t) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%s %d %s %04d",
             DAY_NAMES[t.tm_wday],
             t.tm_mday,
             MONTH_NAMES[t.tm_mon],
             t.tm_year + 1900);
    return String(buf);
}

// ── Helper: check stock alerts and show a banner ──────────────────────────
static void checkStockAlerts() {
    for (int i = 0; i < MAX_STOCKS; i++) {
        Stocks::Quote& q = Stocks::quotes[i];
        if (q.valid && q.alertTriggered) {
            char msg[48];
            snprintf(msg, sizeof(msg), "%s %+.1f%%  $%.2f",
                     q.symbol, q.changePct, q.price);
            UI::showBanner(msg, 8000);
            q.alertTriggered = false;   // reset so it only fires once
            // Event-driven: switch to Stocks page
            UI::activePage = PAGE_STOCKS;
            UI::needsRedraw = true;
            break;                      // one banner at a time
        }
    }
}

// ── Helper: check upcoming prayer alert ───────────────────────────────────
static void checkPrayerAlert() {
    if (!Prayer::current.valid) return;
    int minsLeft = Prayer::minutesUntilNext();
    if (minsLeft >= 0 && minsLeft <= 5) {
        // Show banner 5 minutes before the next prayer
        const char* name =
            Prayer::current.prayers[Prayer::current.nextIndex].name;
        const char* time =
            Prayer::current.prayers[Prayer::current.nextIndex].time;
        char msg[40];
        snprintf(msg, sizeof(msg), "%s at %s (%d min)", name, time, minsLeft);
        UI::showBanner(msg, 10000);
        // Event-driven: switch to Prayer page
        UI::activePage = PAGE_PRAYER;
        UI::needsRedraw = true;
    }
}

// ── setup() ───────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("\n\n=== DeskNexus v1.0 ===");

    // Init display + splash
    UI::begin();
    UI::showSplash();
    UI::showSplashStatus("Initialising...");

    // Load runtime settings from NVS
    Settings::load();

    // Init touchscreen on its own SPI bus (SCK=25, MISO=39, MOSI=32, SS=33)
    touchSPI.begin(25, 39, 32, 33);
    touch.begin(touchSPI);
    touch.setRotation(0);

    // Init stocks helper
    Stocks::begin();

    // Network — hybrid STA/AP
    UI::showSplashStatus("Connecting to WiFi...");
    bool wifiOk = Network::begin();

    if (wifiOk) {
        UI::showSplashStatus("Syncing time (NTP)...");
        TimeSync::apply(Settings::utcOffset);

        struct tm t = {};
        ntpSynced = TimeSync::waitForSync(t);

        if (ntpSynced) {
            Serial.printf("[NTP] Synced: %04d-%02d-%02d %02d:%02d:%02d\n",
                          t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                          t.tm_hour, t.tm_min, t.tm_sec);
        } else {
            Serial.println("[NTP] Sync failed — will retry.");
        }

        UI::showSplashStatus("Detecting location/timezone...");
        if (!LocationTime::detectAndApply()) {
            Serial.println("[AutoDetect] Using saved city/timezone values.");
        } else {
            // Re-apply detected UTC offset so local-time based modules use updated zone.
            TimeSync::apply(Settings::utcOffset, true);
            ntpSynced = TimeSync::readLocalTimeStable(t);
        }

        // Initial data fetches (honor persisted fetch windows)
        UI::showSplashStatus("Loading weather...");
        if (Weather::needsRefresh()) {
            Weather::fetch();
        }

        UI::showSplashStatus("Loading prayer times...");
        if (ntpSynced) {
            if (!refreshPrayerOnStartup(t)) {
                Serial.println("[Prayer] Startup fetch failed — will retry in loop.");
            }
        } else if (Prayer::needsRefresh()) {
            Prayer::fetch();
        }

        UI::showSplashStatus("Loading stocks...");
        Stocks::fetchNext();
    } else {
        UI::showSplashStatus("AP mode  192.168.4.1");
        delay(2000);
    }

    // Short splash delay then go to main screen
    delay(500);
    UI::needsRedraw = true;
    firstDraw       = true;
}

// ── loop() ────────────────────────────────────────────────────────────────
void loop() {
    // ── Web portal handler (AP mode) ──────────────────────────────────────
    Network::handle();

    // ── Touch input (non-blocking debounce inside handleTouch) ────────────
    if (touch.tirqTouched() && touch.touched()) {
        TS_Point p = touch.getPoint();

        // Map raw touch coords to screen pixels (portrait 240×320)
        uint16_t tx = map(p.x, UI::calData[0], UI::calData[1], 0, SCREEN_W);
        uint16_t ty = map(p.y, UI::calData[2], UI::calData[3], 0, SCREEN_H);

        UI::handleTouch(tx, ty);   // debounce + page switch + carousel pause
    }

    unsigned long now = millis();

    // ── Lightweight tick (~10 s) — carousel + prayer countdown + banner ───
    if (now - lastLightTick >= 10000) {
        lastLightTick = now;

        // Auto-advance carousel
        if (UI::shouldAutoAdvance()) {
            UI::advancePage();
        }

        // Refresh prayer countdown & panel
        Prayer::updateNextPrayer();
        UI::updatePanel();
        UI::drawBannerIfActive();
    }

    // ── Minute tick (60 s) — clock, status bar, theme ─────────────────────
    if (now - lastMinuteTick >= 60000 || UI::needsRedraw || firstDraw) {
        if (now - lastMinuteTick >= 60000) lastMinuteTick = now;

        struct tm t;
        bool timeOk = getLocalTime(&t);

        bool wifiOk   = Network::isConnected();
        String ipAddr = Network::ipAddress();
        String date   = timeOk ? buildDateString(t) : "--";

        // Re-NTP sync if we haven't synced yet
        if (!ntpSynced && wifiOk) {
            TimeSync::apply(Settings::utcOffset);
            ntpSynced = TimeSync::readLocalTimeStable(t);
            if (ntpSynced && Prayer::needsRefreshForTime(t) && Prayer::fetchForTime(t)) {
                UI::needsRedraw = true;
            }
        }

        if (UI::needsRedraw || firstDraw) {
            if (timeOk) {
                UI::redraw(wifiOk, ipAddr, t, date);
                firstDraw = false;
            }
        } else if (timeOk) {
            UI::updateClock(t);
            UI::drawStatusBar(wifiOk, date, ipAddr);
        }
    }

    // ── Theme auto-switch (every 60 s) ────────────────────────────────────
    if (now - lastThemeCheck >= 60000) {
        lastThemeCheck = now;
        UI::updateTheme();
    }

    // ── Backlight auto-dim ────────────────────────────────────────────────
    UI::checkDim();

    // ── WiFi reconnect check (every 30 s) ─────────────────────────────────
    if ((now - lastNetCheck) >= 30000) {
        lastNetCheck = now;
        Network::reconnect();
    }

    // ── Remote data refresh ───────────────────────────────────────────────
    if (Network::isConnected()) {
        // Weather
        if (Weather::needsRefresh()) {
            if (Weather::fetch()) {
                UI::updateWeather();
            }
        }

        // Prayer times
        if (Prayer::needsRefresh()) {
            if (Prayer::fetch()) {
                UI::needsRedraw = true;
            }
        }

        // Stocks — one symbol per interval to respect rate limit
        int nStocks = Stocks::symbolCount();
        if (nStocks > 0 &&
            (now - lastStockFetch) >= (STOCK_REFRESH_MS / (unsigned long)nStocks)) {
            lastStockFetch = now;
            if (Stocks::fetchNext()) {
                checkStockAlerts();
                if (UI::activePage == PAGE_STOCKS) {
                    UI::updatePanel();
                }
            }
        }
    }

    // ── Prayer alert (check every 30 s) ───────────────────────────────────
    static unsigned long lastPrayerAlert = 0;
    if ((now - lastPrayerAlert) >= 30000) {
        lastPrayerAlert = now;
        checkPrayerAlert();
    }

    delay(10);  // yield to FreeRTOS scheduler
}
