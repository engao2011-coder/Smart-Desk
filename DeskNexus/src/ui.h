/*
 * ui.h — TFT Display UI Helpers  (v2 — themed, auto-carousel)
 *
 * Colour palette, layout constants, and drawing routines for the
 * 240×320 portrait TFT (ILI9341) on the ESP32-2432S028R (CYD).
 *
 * Screen layout (portrait, 240 wide × 320 tall):
 *
 *  ┌────────────────────────┐ y=0
 *  │  Status bar  (24 px)   │  WiFi dot · date · ● ○ page dots
 *  ├────────────────────────┤ y=24
 *  │  Clock       (64 px)   │  HH:MM + "Dhuhr in 2h 15m"
 *  ├────────────────────────┤ y=88
 *  │  Weather     (48 px)   │  Temp + condition + humidity
 *  ├────────────────────────┤ y=136
 *  │                        │
 *  │  Panel      (184 px)   │  Prayer (page 0) or Stocks (page 1)
 *  │                        │  auto-cycles every CAROUSEL_INTERVAL_MS
 *  └────────────────────────┘ y=320
 *
 * Touch: tap right half of panel → next page, left half → prev page.
 * Theme auto-switches light (Sunrise–Maghrib) / dark (night).
 *
 * Fonts used: TFT_eSPI built-in Free fonts (FreeSansBold24pt7b etc.)
 */

#pragma once

#include <TFT_eSPI.h>
#include "config.h"
#include "weather.h"
#include "prayer.h"
#include "stocks.h"

// ---------------------------------------------------------------------------
// Theme system (light / dark)
// ---------------------------------------------------------------------------
struct Theme {
    uint16_t bg;
    uint16_t panel;
    uint16_t accent;
    uint16_t textPri;
    uint16_t textSec;
    uint16_t textDim;
    uint16_t green;
    uint16_t red;
    uint16_t gold;
    uint16_t separator;
    uint16_t highlightBg;   // next-prayer row highlight
};

static const Theme THEME_DARK = {
    0x1082,  // bg         — very dark navy
    0x2124,  // panel      — slightly lighter
    0xE945,  // accent     — coral/red
    0xFFFF,  // textPri    — white
    0xC618,  // textSec    — light grey
    0x7BEF,  // textDim    — dark grey
    0x07E0,  // green
    0xF800,  // red
    0xFEA0,  // gold
    0x39E7,  // separator  — mid-grey line
    0x2940,  // highlightBg — dark gold tint
};

static const Theme THEME_LIGHT = {
    0xFFDF,  // bg         — warm off-white
    0xEF5D,  // panel      — light grey
    0xC904,  // accent     — muted coral
    0x2104,  // textPri    — near-black
    0x528A,  // textSec    — mid grey
    0x9CF3,  // textDim    — light grey
    0x0600,  // green      — darker green
    0xC000,  // red        — darker red
    0xC580,  // gold       — darker amber
    0xCE59,  // separator  — light grey line
    0xFEE0,  // highlightBg — soft gold
};

// ---------------------------------------------------------------------------
// Layout constants  (v2 — compact, no tab bar)
// ---------------------------------------------------------------------------
#define LAYOUT_STATUS_Y    0
#define LAYOUT_STATUS_H    24
#define LAYOUT_CLOCK_Y     24
#define LAYOUT_CLOCK_H     64
#define LAYOUT_WEATHER_Y   88
#define LAYOUT_WEATHER_H   48
#define LAYOUT_PANEL_Y     136
#define LAYOUT_PANEL_H     184

#define SCREEN_W  240
#define SCREEN_H  320

// Page indices (no Settings page on display)
#define PAGE_PRAYER  0
#define PAGE_STOCKS  1
#define PAGE_COUNT   2

namespace UI {

// ── State ──────────────────────────────────────────────────────────────────
static TFT_eSPI tft = TFT_eSPI();
static Theme    theme       = THEME_DARK;
static bool     isDarkTheme = true;
static int      activePage  = PAGE_PRAYER;
static bool     needsRedraw = true;
static bool     dimmed      = false;

// Touch & carousel timing
static unsigned long lastTouchMs        = 0;
static unsigned long lastPageSwitch     = 0;
static unsigned long carouselPausedUntil = 0;

// ── Touch calibration ─────────────────────────────────────────────────────
static uint16_t calData[5] = TOUCH_CAL_DATA;

// ── Small helpers ──────────────────────────────────────────────────────────
static void hline(int y, uint16_t color) {
    tft.drawFastHLine(0, y, SCREEN_W, color);
}
static void hline(int y) { hline(y, theme.separator); }

static void fillPanel(int y, int h, uint16_t color) {
    tft.fillRect(0, y, SCREEN_W, h, color);
}

// ---------------------------------------------------------------------------
// Theme auto-switch (Sunrise → light, Maghrib → dark)
// ---------------------------------------------------------------------------
static bool isLightTime() {
    struct tm t;
    if (!getLocalTime(&t)) return false;  // can't decide — keep current
    int nowMin = t.tm_hour * 60 + t.tm_min;

    int sunriseMin = 6 * 60;    // default 06:00
    int maghribMin = 18 * 60;   // default 18:00

    if (Prayer::current.valid) {
        // prayers[1] = Sunrise, prayers[4] = Maghrib
        sunriseMin = Prayer::toMinutes(Prayer::current.prayers[1].time);
        maghribMin = Prayer::toMinutes(Prayer::current.prayers[4].time);
    }
    return (nowMin >= sunriseMin && nowMin < maghribMin);
}

static void updateTheme() {
    bool wantLight = isLightTime();
    bool wantDark  = !wantLight;
    if (wantDark == isDarkTheme) return;  // no change
    isDarkTheme = wantDark;
    theme       = isDarkTheme ? THEME_DARK : THEME_LIGHT;
    needsRedraw = true;
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
static void begin() {
    ledcSetup(0, 5000, 8);
    ledcAttachPin(TFT_BL_PIN, 0);
    ledcWrite(0, BACKLIGHT_FULL_DUTY);

    tft.init();
    tft.setRotation(0);
    tft.fillScreen(theme.bg);
    tft.setTextDatum(TL_DATUM);
    tft.setSwapBytes(true);

    lastTouchMs    = millis();
    lastPageSwitch = millis();
}

// ---------------------------------------------------------------------------
// Backlight control
// ---------------------------------------------------------------------------
static void setDimmed(bool d) {
    if (d == dimmed) return;
    dimmed = d;
    ledcWrite(0, d ? BACKLIGHT_DIM_DUTY : BACKLIGHT_FULL_DUTY);
}

static void wake() {
    lastTouchMs = millis();
    setDimmed(false);
}

static void checkDim() {
    if (SCREEN_TIMEOUT_MS == 0) return;
    if (!dimmed && (millis() - lastTouchMs) >= SCREEN_TIMEOUT_MS) {
        setDimmed(true);
    }
}

// ---------------------------------------------------------------------------
// Carousel logic
// ---------------------------------------------------------------------------
static void advancePage() {
    activePage = (activePage + 1) % PAGE_COUNT;
    lastPageSwitch = millis();
    needsRedraw = true;
}

static bool shouldAutoAdvance() {
    if (millis() < carouselPausedUntil) return false;
    return (millis() - lastPageSwitch) >= CAROUSEL_INTERVAL_MS;
}

static void pauseCarousel() {
    carouselPausedUntil = millis() + CAROUSEL_PAUSE_MS;
}

// ---------------------------------------------------------------------------
// Touch handler — tap left/right half of panel area to change page
// Returns true if page changed.
// ---------------------------------------------------------------------------
static bool handleTouch(uint16_t tx, uint16_t ty) {
    // Non-blocking debounce: reject if too soon after last touch
    unsigned long now = millis();
    if ((now - lastTouchMs) < 200) return false;
    lastTouchMs = now;

    wake();
    pauseCarousel();

    // Only panel area is tappable for page switching
    if (ty >= LAYOUT_PANEL_Y) {
        if (tx >= SCREEN_W / 2) {
            // Right half → next page
            activePage = (activePage + 1) % PAGE_COUNT;
        } else {
            // Left half → previous page
            activePage = (activePage + PAGE_COUNT - 1) % PAGE_COUNT;
        }
        lastPageSwitch = millis();

        // Visual feedback — flash accent line at top of panel
        tft.drawFastHLine(0, LAYOUT_PANEL_Y, SCREEN_W, theme.accent);
        needsRedraw = true;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Status bar (y=0..23) with page dots
// ---------------------------------------------------------------------------
static void drawStatusBar(bool wifiOk, const String& dateStr, const String& ipStr) {
    tft.fillRect(0, LAYOUT_STATUS_Y, SCREEN_W, LAYOUT_STATUS_H, theme.bg);

    // WiFi indicator
    uint16_t wifiColor = wifiOk ? theme.green : theme.red;
    tft.fillCircle(10, 12, 5, wifiColor);

    tft.setTextSize(1);
    tft.setFreeFont(nullptr);

    // Page dots — right side
    const int DOT_R = 3;
    const int DOT_GAP = 12;
    int dotsW = PAGE_COUNT * DOT_GAP;
    int dotX = SCREEN_W - dotsW - 4;
    for (int i = 0; i < PAGE_COUNT; i++) {
        int cx = dotX + i * DOT_GAP + DOT_R;
        int cy = 12;
        if (i == activePage) {
            tft.fillCircle(cx, cy, DOT_R, theme.accent);
        } else {
            tft.drawCircle(cx, cy, DOT_R, theme.textDim);
        }
    }

    // Date string — centred between WiFi dot and page dots
    int available = dotX - 20;
    tft.setTextColor(theme.textSec, theme.bg);
    int dw = tft.textWidth(dateStr);
    int dx = 20 + (available - dw) / 2;
    if (dx < 20) dx = 20;
    tft.setCursor(dx, 8);
    tft.print(dateStr);

    hline(LAYOUT_STATUS_Y + LAYOUT_STATUS_H - 1);
}

// ---------------------------------------------------------------------------
// Clock section (y=24..87) — HH:MM + next-prayer countdown
// ---------------------------------------------------------------------------
static void drawClock(const struct tm& t) {
    fillPanel(LAYOUT_CLOCK_Y, LAYOUT_CLOCK_H, theme.bg);

    char hmBuf[6];
    snprintf(hmBuf, sizeof(hmBuf), "%02d:%02d", t.tm_hour, t.tm_min);

    // Large HH:MM (centred)
    tft.setTextColor(theme.textPri, theme.bg);
    tft.setFreeFont(&FreeSansBold24pt7b);
    int tw = tft.textWidth(hmBuf);
    tft.setCursor((SCREEN_W - tw) / 2, LAYOUT_CLOCK_Y + 42);
    tft.print(hmBuf);

    // Next-prayer countdown below the time
    if (Prayer::current.valid && Prayer::current.nextIndex >= 0) {
        int minsLeft = Prayer::minutesUntilNext();
        if (minsLeft >= 0) {
            const char* name = Prayer::current.prayers[Prayer::current.nextIndex].name;
            char cdBuf[32];
            snprintf(cdBuf, sizeof(cdBuf), "%s in %dh %02dm",
                     name, minsLeft / 60, minsLeft % 60);
            tft.setFreeFont(nullptr);
            tft.setTextSize(1);
            tft.setTextColor(theme.gold, theme.bg);
            int cw = tft.textWidth(cdBuf);
            tft.setCursor((SCREEN_W - cw) / 2, LAYOUT_CLOCK_Y + 54);
            tft.print(cdBuf);
        }
    }

    hline(LAYOUT_CLOCK_Y + LAYOUT_CLOCK_H - 1);
}

// ---------------------------------------------------------------------------
// Weather strip (y=88..135) — compact single-strip layout
// ---------------------------------------------------------------------------
static void drawWeather() {
    fillPanel(LAYOUT_WEATHER_Y, LAYOUT_WEATHER_H, theme.bg);

    if (!Weather::current.valid) {
        tft.setFreeFont(nullptr);
        tft.setTextSize(1);
        tft.setTextColor(theme.textDim, theme.bg);
        tft.setCursor(8, LAYOUT_WEATHER_Y + 18);
        tft.print("Weather: loading...");
        hline(LAYOUT_WEATHER_Y + LAYOUT_WEATHER_H - 1);
        return;
    }

    const Weather::Data& w = Weather::current;
    uint16_t accent = Weather::iconColor(w.iconCode);

    // Row 1: Temperature (large) on left + condition label on right
    char tempBuf[12];
    const char* unit = (strcmp(OWM_UNITS, "imperial") == 0) ? "F" : "C";
    snprintf(tempBuf, sizeof(tempBuf), "%.0f°%s", w.temp, unit);

    tft.setFreeFont(&FreeSansBold12pt7b);
    tft.setTextColor(accent, theme.bg);
    tft.setCursor(8, LAYOUT_WEATHER_Y + 22);
    tft.print(tempBuf);

    // Condition label — right of temp
    tft.setFreeFont(nullptr);
    tft.setTextSize(1);
    tft.setTextColor(theme.textSec, theme.bg);
    tft.setCursor(110, LAYOUT_WEATHER_Y + 10);
    tft.print(Weather::iconLabel(w.iconCode));

    // City name — small, right of condition
    tft.setTextColor(theme.textDim, theme.bg);
    tft.setCursor(110, LAYOUT_WEATHER_Y + 22);
    tft.print(w.cityName);

    // Row 2: Humidity, Wind, Feels-like — compact line
    char detailBuf[48];
    snprintf(detailBuf, sizeof(detailBuf), "Hum:%.0f%%  Wind:%.1fm/s  FL:%.0f°",
             w.humidity, w.windSpeedMs, w.feelsLike);
    tft.setTextColor(theme.textDim, theme.bg);
    tft.setCursor(8, LAYOUT_WEATHER_Y + 36);
    tft.print(detailBuf);

    hline(LAYOUT_WEATHER_Y + LAYOUT_WEATHER_H - 1);
}

// ---------------------------------------------------------------------------
// Prayer panel (y=136..319) — expanded single-column, 6 rows
// ---------------------------------------------------------------------------
static void drawPrayerPanel() {
    fillPanel(LAYOUT_PANEL_Y, LAYOUT_PANEL_H, theme.panel);

    tft.setFreeFont(nullptr);
    tft.setTextSize(1);

    if (!Prayer::current.valid) {
        tft.setTextColor(theme.textDim, theme.panel);
        tft.setCursor(8, LAYOUT_PANEL_Y + 80);
        tft.print("Prayer times: loading...");
        return;
    }

    // Section title
    tft.setTextColor(theme.accent, theme.panel);
    tft.setCursor(8, LAYOUT_PANEL_Y + 6);
    tft.print("PRAYER TIMES");

    // 6 rows × ~26px each, starting at y+22
    const int ROW_H  = 26;
    const int START_Y = LAYOUT_PANEL_Y + 22;

    for (int i = 0; i < Prayer::PRAYER_COUNT; i++) {
        bool isNext = (i == Prayer::current.nextIndex);
        int ry = START_Y + i * ROW_H;

        // Highlight bar for next prayer
        if (isNext) {
            tft.fillRect(0, ry, SCREEN_W, ROW_H, theme.highlightBg);
        }

        uint16_t fg    = isNext ? theme.gold : theme.textSec;
        uint16_t rowBg = isNext ? theme.highlightBg : theme.panel;

        // Prayer name (left)
        tft.setTextSize(1);
        tft.setTextColor(fg, rowBg);
        tft.setCursor(12, ry + 8);
        tft.print(Prayer::current.prayers[i].name);

        // Prayer time (right-aligned)
        const char* timeStr = Prayer::current.prayers[i].time;
        int tw = tft.textWidth(timeStr);
        tft.setCursor(SCREEN_W - tw - 12, ry + 8);
        tft.print(timeStr);

        // Separator line below each row (except last)
        if (i < Prayer::PRAYER_COUNT - 1) {
            hline(ry + ROW_H - 1, theme.separator);
        }
    }
}

// ---------------------------------------------------------------------------
// Stocks panel (y=136..319) — expanded, all 5 with detail
// ---------------------------------------------------------------------------
static void drawStocksPanel() {
    fillPanel(LAYOUT_PANEL_Y, LAYOUT_PANEL_H, theme.panel);

    tft.setFreeFont(nullptr);
    tft.setTextSize(1);

    int n = Stocks::symbolCount();
    if (n == 0) {
        tft.setTextColor(theme.textDim, theme.panel);
        tft.setCursor(8, LAYOUT_PANEL_Y + 80);
        tft.print("No stocks configured.");
        return;
    }

    // Section title
    tft.setTextColor(theme.accent, theme.panel);
    tft.setCursor(8, LAYOUT_PANEL_Y + 6);
    tft.print("STOCKS");

    const int ROW_H  = 32;
    const int START_Y = LAYOUT_PANEL_Y + 22;
    int shown = 0;

    for (int i = 0; i < MAX_STOCKS && shown < 5; i++) {
        const Stocks::Quote& q = Stocks::quotes[i];
        if (strlen(Settings::stockSymbols[i]) == 0) continue;

        int ry = START_Y + shown * ROW_H;
        uint16_t rowBg = theme.panel;

        if (!q.valid) {
            // Show symbol with "loading" state
            tft.setTextColor(theme.textDim, rowBg);
            tft.setCursor(12, ry + 6);
            tft.print(Settings::stockSymbols[i]);
            tft.setCursor(80, ry + 6);
            tft.print("...");
        } else {
            uint16_t pctColor = (q.changePct >= 0) ? theme.green : theme.red;

            // Row 1: Symbol + Price + Change%
            tft.setTextColor(theme.textPri, rowBg);
            tft.setCursor(12, ry + 4);
            tft.print(q.symbol);

            char priceBuf[12];
            snprintf(priceBuf, sizeof(priceBuf), "$%.2f", q.price);
            tft.setTextColor(theme.textSec, rowBg);
            tft.setCursor(70, ry + 4);
            tft.print(priceBuf);

            char pctBuf[10];
            snprintf(pctBuf, sizeof(pctBuf), "%+.2f%%", q.changePct);
            tft.setTextColor(pctColor, rowBg);
            int pw = tft.textWidth(pctBuf);
            tft.setCursor(SCREEN_W - pw - 8, ry + 4);
            tft.print(pctBuf);

            // Row 2: High/Low range
            char rangeBuf[28];
            snprintf(rangeBuf, sizeof(rangeBuf), "H:%.2f  L:%.2f", q.high, q.low);
            tft.setTextColor(theme.textDim, rowBg);
            tft.setCursor(70, ry + 17);
            tft.print(rangeBuf);

            // Alert dot
            if (q.alertTriggered) {
                tft.fillCircle(60, ry + 8, 3, theme.gold);
            }
        }

        // Separator
        if (shown < MAX_STOCKS - 1) {
            hline(ry + ROW_H - 1, theme.separator);
        }
        shown++;
    }

    if (shown == 0) {
        tft.setTextColor(theme.textDim, theme.panel);
        tft.setCursor(8, LAYOUT_PANEL_Y + 80);
        tft.print("Fetching quotes...");
    }
}

// ---------------------------------------------------------------------------
// Notification banner (temporary overlay at top of panel)
// ---------------------------------------------------------------------------
static unsigned long bannerExpiry = 0;
static char bannerText[64] = {};

static void showBanner(const char* text, uint32_t durationMs = 5000) {
    strncpy(bannerText, text, sizeof(bannerText) - 1);
    bannerExpiry = millis() + durationMs;
}

static void drawBannerIfActive() {
    if (millis() >= bannerExpiry) return;
    tft.fillRect(0, LAYOUT_PANEL_Y, SCREEN_W, 20, theme.accent);
    tft.setFreeFont(nullptr);
    tft.setTextSize(1);
    tft.setTextColor(theme.textPri, theme.accent);
    int tw = tft.textWidth(bannerText);
    tft.setCursor((SCREEN_W - tw) / 2, LAYOUT_PANEL_Y + 6);
    tft.print(bannerText);
}

// ---------------------------------------------------------------------------
// Full screen redraw
// ---------------------------------------------------------------------------
static void redraw(bool wifiOk, const String& ipAddr,
                   const struct tm& t, const String& dateStr) {
    drawStatusBar(wifiOk, dateStr, ipAddr);
    drawClock(t);
    drawWeather();

    switch (activePage) {
        case PAGE_PRAYER:  drawPrayerPanel();  break;
        case PAGE_STOCKS:  drawStocksPanel();  break;
    }

    drawBannerIfActive();
    needsRedraw = false;
}

// ---------------------------------------------------------------------------
// Partial updates
// ---------------------------------------------------------------------------
static void updateClock(const struct tm& t) {
    drawClock(t);
}

static void updateWeather() {
    drawWeather();
}

static void updatePanel() {
    switch (activePage) {
        case PAGE_PRAYER:  drawPrayerPanel();  break;
        case PAGE_STOCKS:  drawStocksPanel();  break;
    }
}

// ---------------------------------------------------------------------------
// Splash / boot screen
// ---------------------------------------------------------------------------
static void showSplash() {
    tft.fillScreen(theme.bg);
    tft.setFreeFont(&FreeSansBold18pt7b);
    tft.setTextColor(theme.accent, theme.bg);
    String title = "DeskNexus";
    int tw = tft.textWidth(title);
    tft.setCursor((SCREEN_W - tw) / 2, 140);
    tft.print(title);

    tft.setFreeFont(nullptr);
    tft.setTextSize(1);
    tft.setTextColor(theme.textDim, theme.bg);
    String sub = "ESP32 Desk Clock";
    tw = tft.textWidth(sub);
    tft.setCursor((SCREEN_W - tw) / 2, 166);
    tft.print(sub);
}

static void showSplashStatus(const char* msg) {
    tft.fillRect(0, 190, SCREEN_W, 20, theme.bg);
    tft.setFreeFont(nullptr);
    tft.setTextSize(1);
    tft.setTextColor(theme.textSec, theme.bg);
    int tw = tft.textWidth(msg);
    tft.setCursor((SCREEN_W - tw) / 2, 192);
    tft.print(msg);
}

} // namespace UI
