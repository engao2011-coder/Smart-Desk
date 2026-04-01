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
#include <XPT2046_Touchscreen.h>

#include "config.h"
#include "network.h"
#include "weather.h"
#include "prayer.h"
#include "stocks.h"
#include "ui.h"

// ── Touchscreen ───────────────────────────────────────────────────────────
// Pass TOUCH_IRQ_PIN so tirqTouched() gives efficient polling.
static XPT2046_Touchscreen touch(TOUCH_CS_PIN, TOUCH_IRQ_PIN);

// ── Timing state ──────────────────────────────────────────────────────────
static unsigned long lastSecond     = 0;   // for 1-second clock tick
static unsigned long lastStockFetch = 0;   // for stock round-robin
static unsigned long lastNetCheck   = 0;   // WiFi reconnect check
static unsigned long lastPrayerUpd  = 0;   // prayer countdown refresh

static bool ntpSynced   = false;
static bool firstDraw   = true;

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

    // Init touchscreen
    touch.begin();
    touch.setRotation(0);

    // Init stocks helper
    Stocks::begin();

    // Network — hybrid STA/AP
    UI::showSplashStatus("Connecting to WiFi...");
    bool wifiOk = Network::begin();

    if (wifiOk) {
        UI::showSplashStatus("Syncing time (NTP)...");
        // Configure NTP
        configTime(NTP_UTC_OFFSET_SEC, NTP_DST_OFFSET_SEC,
                   NTP_SERVER_1, NTP_SERVER_2);

        // Wait up to 10 s for NTP sync
        struct tm t;
        unsigned long ntpStart = millis();
        while (!getLocalTime(&t) && (millis() - ntpStart < 10000)) {
            delay(200);
        }
        ntpSynced = getLocalTime(&t);

        if (ntpSynced) {
            Serial.printf("[NTP] Synced: %04d-%02d-%02d %02d:%02d:%02d\n",
                          t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                          t.tm_hour, t.tm_min, t.tm_sec);
        } else {
            Serial.println("[NTP] Sync failed — will retry.");
        }

        // Initial data fetches
        UI::showSplashStatus("Loading weather...");
        Weather::fetch();

        UI::showSplashStatus("Loading prayer times...");
        Prayer::fetch();

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

    // ── Touch input ───────────────────────────────────────────────────────
    if (touch.tirqTouched() && touch.touched()) {
        TS_Point p = touch.getPoint();

        // Map raw touch coords to screen pixels (portrait 240×320)
        // TFT_eSPI calibration: mapToScreen helper
        uint16_t tx = map(p.x, UI::calData[0], UI::calData[1], 0, SCREEN_W);
        uint16_t ty = map(p.y, UI::calData[2], UI::calData[3], 0, SCREEN_H);

        UI::wake();   // reset dim timer

        // Tab bar touch?
        if (UI::handleTabTouch(tx, ty)) {
            // Tab changed; panel will redraw below
        }

        delay(120);  // simple debounce
    }

    // ── Time update (every second) ────────────────────────────────────────
    unsigned long now = millis();
    if (now - lastSecond >= 1000) {
        lastSecond = now;

        struct tm t;
        bool timeOk = getLocalTime(&t);

        bool wifiOk  = Network::isConnected();
        String ip    = Network::localAddress();
        String date  = timeOk ? buildDateString(t) : "--";

        // Re-NTP sync if we haven't synced yet
        if (!ntpSynced && wifiOk) {
            configTime(NTP_UTC_OFFSET_SEC, NTP_DST_OFFSET_SEC,
                       NTP_SERVER_1, NTP_SERVER_2);
            ntpSynced = getLocalTime(&t);
        }

        if (UI::needsRedraw || firstDraw) {
            if (timeOk) {
                UI::redraw(wifiOk, ip, t, date);
                firstDraw = false;
            }
        } else {
            // Partial updates — only repaint what changed
            if (timeOk) {
                UI::updateClock(t);
            }
            UI::drawStatusBar(wifiOk, date);

            // Update panel countdown (prayer) every 10 s
            if ((now - lastPrayerUpd) >= 10000) {
                lastPrayerUpd = now;
                Prayer::updateNextPrayer();
                UI::updatePanel();
            }

            // Show any pending banner
            UI::drawBannerIfActive();
        }
    }

    // ── Backlight auto-dim ────────────────────────────────────────────────
    UI::checkDim();

    // ── WiFi reconnect check (every 30 s) ─────────────────────────────────
    if ((millis() - lastNetCheck) >= 30000) {
        lastNetCheck = millis();
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
            (millis() - lastStockFetch) >= (STOCK_REFRESH_MS / (unsigned long)nStocks)) {
            lastStockFetch = millis();
            if (Stocks::fetchNext()) {
                checkStockAlerts();
                if (UI::activeTab == TAB_STOCKS) {
                    UI::updatePanel();
                }
            }
        }
    }

    // ── Prayer alert (check every 30 s) ───────────────────────────────────
    static unsigned long lastPrayerAlert = 0;
    if ((millis() - lastPrayerAlert) >= 30000) {
        lastPrayerAlert = millis();
        checkPrayerAlert();
    }

    delay(10);  // yield to FreeRTOS scheduler
}
