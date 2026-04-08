/*
 * ui.h — TFT Display UI Helpers  (v3 — modern merged hero, improved readability)
 *
 * Colour palette, layout constants, and drawing routines for the
 * 240×320 portrait TFT (ILI9341) on the ESP32-2432S028R (CYD).
 *
 * Screen layout (portrait, 240 wide × 320 tall):
 *
 *  ┌────────────────────────┐ y=0
 *  │  Status bar  (24 px)   │  WiFi dot · date · ● ○ page dots
 *  ├────────────────────────┤ y=24
 *  │  Hero       (100 px)   │  HH:MM (left) │ icon+temp (right)
 *  │                        │  countdown / status below clock
 *  ├────────────────────────┤ y=124
 *  │                        │
 *  │  Panel      (196 px)   │  Prayer (page 0) or Stocks (page 1)
 *  │                        │  auto-cycles every CAROUSEL_INTERVAL_MS
 *  └────────────────────────┘ y=320
 *
 * Touch: tap right half of panel → next page, left half → prev page.
 * Theme is manually selected (dark/light) and persisted in settings.
 *
 * Fonts used: TFT_eSPI built-in Free fonts (FreeSansBold24pt7b etc.)
 */

#pragma once

#include <TFT_eSPI.h>
#include "config.h"
#include "settings.h"
#include "network.h"
#include "weather.h"
#include "prayer.h"
#include "stocks.h"

// ---------------------------------------------------------------------------
// Theme system (light / dark) — contrast-improved v3
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
    0x18C4,  // bg         — deep slate blue
    0x2126,  // panel      — softened navy panel
    0xFC00,  // accent     — warm amber accent
    0xFFFF,  // textPri    — white
    0xDEFB,  // textSec    — cool light grey
    0xB596,  // textDim    — boosted slate (~5:1 contrast)
    0x3E0A,  // green
    0xFB2C,  // red
    0xFD20,  // gold
    0x4A69,  // separator  — visible blue-grey line
    0x39A6,  // highlightBg — muted steel blue
};

static const Theme THEME_LIGHT = {
    0xFF9A,  // bg         — warm paper
    0xF758,  // panel      — pale sand
    0xD240,  // accent     — burnt orange
    0x18C3,  // textPri    — deep slate
    0x4A49,  // textSec    — strong mid grey
    0x6B4D,  // textDim    — darker grey (~5:1 contrast)
    0x1A86,  // green      — readable green
    0xD104,  // red        — readable red
    0xBC40,  // gold       — warm amber
    0xD66F,  // separator  — soft sand line
    0xFE48,  // highlightBg — warm cream
};

// ---------------------------------------------------------------------------
// Layout constants  (v3 — merged hero widget, larger panel)
// ---------------------------------------------------------------------------
#define LAYOUT_STATUS_Y    0
#define LAYOUT_STATUS_H    24
#define LAYOUT_HERO_Y      24
#define LAYOUT_HERO_H      100
#define LAYOUT_PANEL_Y     124
#define LAYOUT_PANEL_H     196

#define SCREEN_W  240
#define SCREEN_H  320

// Page indices (no Settings page on display)
#define PAGE_PRAYER  0
#define PAGE_STOCKS  1
#define PAGE_COUNT   2

namespace UI {

// ── Azan screen layout constants (shared by drawAzanScreen + handleTouch) ──
static constexpr int AZAN_BUTTON_MARGIN = 68;   // height reserved for bottom buttons
static constexpr int AZAN_CONTENT_H     = 132;  // text-content block height
static constexpr int AZAN_START_Y       = (SCREEN_H - AZAN_BUTTON_MARGIN - AZAN_CONTENT_H) / 2;
// X-dismiss hit zone: top-right corner of the card (y < AZAN_START_Y, x > SCREEN_W-40)
static constexpr int AZAN_DISMISS_X     = SCREEN_W - 40;
static constexpr int AZAN_DISMISS_Y_MAX = AZAN_START_Y;  // tap must be above content start

// ── State ──────────────────────────────────────────────────────────────────
static TFT_eSPI tft = TFT_eSPI();
static Theme    theme       = THEME_DARK;
static bool     isDarkTheme = true;
static int      activePage  = PAGE_PRAYER;
static bool     needsRedraw = true;
static bool     dimmed      = false;
static bool     azanScreenActive = false;
static int      azanPrayerIndex = -1;
static unsigned long azanScreenExpiry = 0;

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

static void updateTheme() {
    bool wantDark = Settings::themeDark;
    if (wantDark == isDarkTheme) return;  // no change

    // Smooth transition: fade backlight down, swap palette, redraw, fade up
    int currentDuty = dimmed ? BACKLIGHT_DIM_DUTY : BACKLIGHT_FULL_DUTY;
    for (int d = currentDuty; d >= 0; d -= 20) {
        ledcWrite(0, d > 0 ? d : 0);
        delay(20);
    }
    ledcWrite(0, 0);

    isDarkTheme = wantDark;
    theme       = isDarkTheme ? THEME_DARK : THEME_LIGHT;
    needsRedraw = true;

    // After next redraw, fade back up (handled by caller via needsRedraw)
    // We do a brief pause then ramp up — the full redraw will happen in loop()
    // so we just ramp up here and let the next frame paint with new theme
    for (int d = 0; d <= currentDuty; d += 20) {
        ledcWrite(0, d);
        delay(20);
    }
    ledcWrite(0, currentDuty);
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

    isDarkTheme = Settings::themeDark;
    theme = isDarkTheme ? THEME_DARK : THEME_LIGHT;
    tft.fillScreen(theme.bg);

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
    // Keep screen on while a prayer is pending — user needs to respond
    if (Prayer::pendingPrayerIndex() >= 0) return;
    if (!dimmed && (millis() - lastTouchMs) >= SCREEN_TIMEOUT_MS) {
        setDimmed(true);
    }
}

// ---------------------------------------------------------------------------
// Carousel logic
// ---------------------------------------------------------------------------
static bool isPageAllowed(int page) {
    if (page == PAGE_STOCKS) {
        return Network::isConnected();
    }
    return true;
}

static int nextAllowedPage(int fromPage, int direction) {
    int page = fromPage;
    for (int i = 0; i < PAGE_COUNT; i++) {
        page = (page + direction + PAGE_COUNT) % PAGE_COUNT;
        if (isPageAllowed(page)) {
            return page;
        }
    }
    return PAGE_PRAYER;
}

// Like nextAllowedPage but also skips PAGE_STOCKS — stocks only appears via event trigger
static int nextCarouselPage(int fromPage, int direction) {
    int page = fromPage;
    for (int i = 0; i < PAGE_COUNT; i++) {
        page = (page + direction + PAGE_COUNT) % PAGE_COUNT;
        if (isPageAllowed(page) && page != PAGE_STOCKS) {
            return page;
        }
    }
    return PAGE_PRAYER;
}

static bool switchPageBy(int direction) {
    int nextPage = nextAllowedPage(activePage, direction);
    bool changed = (nextPage != activePage);
    activePage = nextPage;
    lastPageSwitch = millis();
    needsRedraw = true;
    return changed;
}

static bool coerceAllowedActivePage() {
    if (isPageAllowed(activePage)) return false;
    activePage = PAGE_PRAYER;
    needsRedraw = true;
    return true;
}

static void advancePage() {
    // Use carousel-safe advance that skips PAGE_STOCKS;
    // stocks page is only shown when a stock moves >= STOCK_INTRA_CHANGE_PCT
    int nextPage = nextCarouselPage(activePage, 1);
    activePage = nextPage;
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

static bool hasPrayerFooterActions() {
    return activePage == PAGE_PRAYER && Prayer::pendingPrayerIndex() >= 0;
}

static void dismissAzanScreen() {
    if (!azanScreenActive) return;
    azanScreenActive = false;
    azanPrayerIndex = -1;
    azanScreenExpiry = 0;
    needsRedraw = true;
}

static void showAzanScreen(int prayerIndex) {
    azanScreenActive = true;
    azanPrayerIndex = prayerIndex;
    azanScreenExpiry = millis() + PRAYER_FULLSCREEN_MS;
    activePage = PAGE_PRAYER;
    wake();
    pauseCarousel();
    needsRedraw = true;
}

static void updatePrayerUiState() {
    if (azanScreenActive && millis() >= azanScreenExpiry) {
        dismissAzanScreen();
    }

    if (Prayer::pendingPrayerIndex() >= 0) {
        activePage = PAGE_PRAYER;
        pauseCarousel();
    }
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

    if (azanScreenActive) {
        int buttonTop = SCREEN_H - AZAN_BUTTON_MARGIN;
        if (ty >= (uint16_t)buttonTop) {
            // Button row — Prayed (left) or Snooze (right)
            int bW = SCREEN_W / 2 - 18;
            if (tx < SCREEN_W / 2) {
                // Flash Prayed button
                tft.fillRoundRect(12, buttonTop, bW, 48, 10, theme.textDim);
                delay(60);
                if (Prayer::markPrayed(azanPrayerIndex)) {
                    dismissAzanScreen();
                }
            } else {
                // Flash Snooze button
                tft.fillRoundRect(SCREEN_W / 2 + 6, buttonTop, bW, 48, 10, theme.textDim);
                delay(60);
                if (!Prayer::isSnoozed(azanPrayerIndex) && Prayer::snoozePendingPrayer(azanPrayerIndex)) {
                    dismissAzanScreen();
                }
            }
            needsRedraw = true;
            return true;
        }
        // X dismiss zone — top-right corner of card, above content
        if ((int)ty < AZAN_DISMISS_Y_MAX && (int)tx >= AZAN_DISMISS_X) {
            dismissAzanScreen();
            return true;
        }
        // Any other tap on azan screen is intentionally ignored
        return true;
    }

    if (hasPrayerFooterActions()) {
        const int footerTop = SCREEN_H - 50;
        if (ty >= footerTop) {
            // Visual flash feedback before action
            const int bW = SCREEN_W / 2 - 18;
            const int fy = SCREEN_H - 48 - 4;  // matches drawPrayerPanel footer geometry
            const int bH = 40;
            if (tx < SCREEN_W / 2) {
                tft.fillRoundRect(14, fy, bW, bH, 8, theme.textDim);
                delay(60);
                Prayer::markPendingPrayed();
            } else {
                tft.fillRoundRect(SCREEN_W / 2 + 4, fy, bW, bH, 8, theme.textDim);
                delay(60);
                int fp = Prayer::pendingPrayerIndex();
                if (fp >= 0 && !Prayer::isSnoozed(fp)) {
                    Prayer::snoozePendingPrayer(fp);
                }
            }
            needsRedraw = true;
            return true;
        }
    }

    // Tap a MISSED prayer row to retroactively mark it as prayed
    if (activePage == PAGE_PRAYER && Prayer::current.valid &&
        (int)ty >= LAYOUT_PANEL_Y && (int)ty < (SCREEN_H - 50)) {
        const int ROW_H  = 26;
        const int START_Y = LAYOUT_PANEL_Y + 14;
        int tappedIdx = ((int)ty - START_Y) / ROW_H;
        if (tappedIdx >= 0 && tappedIdx < Prayer::PRAYER_COUNT) {
            if (Prayer::rowStateForIndex(tappedIdx) == Prayer::ROW_MISSED) {
                Prayer::markPrayed(tappedIdx);
                needsRedraw = true;
                return true;  // consume — don't fall through to page switch
            }
        }
    }

    // Only panel area is tappable for page switching
    if (ty >= LAYOUT_PANEL_Y) {
        bool changed = false;
        if (tx >= SCREEN_W / 2) {
            // Right half → next page
            changed = switchPageBy(1);
        } else {
            // Left half → previous page
            changed = switchPageBy(-1);
        }

        if (changed) {
            // Visual feedback — flash accent line at top of panel
            tft.drawFastHLine(0, LAYOUT_PANEL_Y, SCREEN_W, theme.accent);
            return true;
        }
        return false;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Status bar (y=0..23) with page dots
// ---------------------------------------------------------------------------
static void drawStatusBar(bool wifiOk, const String& dateStr, const String& ipStr) {
    tft.fillRect(0, LAYOUT_STATUS_Y, SCREEN_W, LAYOUT_STATUS_H, theme.panel);

    // WiFi indicator
    uint16_t wifiColor = wifiOk ? theme.green : theme.red;
    tft.fillCircle(11, 11, 5, wifiColor);
    tft.drawCircle(11, 11, 7, theme.panel);

    tft.setTextSize(1);
    tft.setFreeFont(nullptr);

    // Page dots — right side
    const int DOT_R = 4;
    const int DOT_GAP = 14;
    int dotsW = PAGE_COUNT * DOT_GAP;
    int dotX = SCREEN_W - dotsW - 8;
    for (int i = 0; i < PAGE_COUNT; i++) {
        int cx = dotX + i * DOT_GAP + DOT_R;
        int cy = 11;
        bool allowed = isPageAllowed(i);
        if (i == activePage) {
            tft.fillCircle(cx, cy, DOT_R, theme.accent);
            tft.drawCircle(cx, cy, DOT_R + 2, theme.panel);
        } else if (allowed) {
            tft.drawCircle(cx, cy, DOT_R, theme.textDim);
        } else {
            // Page unavailable (e.g. Stocks while offline) — faint hollow dot
            tft.fillCircle(cx, cy, DOT_R, theme.panel);   // erase interior
            tft.drawCircle(cx, cy, DOT_R - 1, theme.separator);
        }
    }

    // Date string — centred between WiFi dot and page dots
    int available = dotX - 28;
    tft.setTextColor(theme.textPri, theme.panel);
    int dw = tft.textWidth(dateStr);
    int dx = 28 + (available - dw) / 2;
    if (dx < 28) dx = 28;
    tft.setCursor(dx, 7);
    tft.print(dateStr);

    hline(LAYOUT_STATUS_Y + LAYOUT_STATUS_H - 1);
}

// ---------------------------------------------------------------------------
// Geometric weather icon drawing (24×24 around cx,cy)
// ---------------------------------------------------------------------------
static void drawWeatherIcon(int cx, int cy, const String& code) {
    if (code.startsWith("01")) {
        // Sunny — filled circle + 8 rays
        uint16_t col = 0xFEE0;
        tft.fillCircle(cx, cy, 7, col);
        for (int a = 0; a < 360; a += 45) {
            float r1 = 10, r2 = 13;
            float rad = a * 3.14159f / 180.0f;
            int x1 = cx + (int)(r1 * cosf(rad));
            int y1 = cy + (int)(r1 * sinf(rad));
            int x2 = cx + (int)(r2 * cosf(rad));
            int y2 = cy + (int)(r2 * sinf(rad));
            tft.drawLine(x1, y1, x2, y2, col);
        }
    } else if (code.startsWith("02")) {
        // Partly cloudy — small sun + overlapping cloud
        tft.fillCircle(cx + 5, cy - 4, 5, 0xFEE0);
        tft.drawLine(cx + 5, cy - 12, cx + 5, cy - 10, 0xFEE0);
        tft.drawLine(cx + 12, cy - 4, cx + 10, cy - 4, 0xFEE0);
        tft.fillCircle(cx - 4, cy + 2, 6, theme.textDim);
        tft.fillCircle(cx + 4, cy, 7, theme.textDim);
        tft.fillRect(cx - 10, cy + 4, 20, 6, theme.textDim);
    } else if (code.startsWith("03") || code.startsWith("04")) {
        // Cloudy / overcast — two overlapping circles
        tft.fillCircle(cx - 4, cy - 1, 7, theme.textDim);
        tft.fillCircle(cx + 5, cy - 3, 8, theme.textSec);
        tft.fillRect(cx - 11, cy + 4, 22, 6, theme.textDim);
    } else if (code.startsWith("09") || code.startsWith("10")) {
        // Rain — cloud + droplet lines
        tft.fillCircle(cx - 3, cy - 4, 6, 0x5D1F);
        tft.fillCircle(cx + 5, cy - 5, 7, 0x5D1F);
        tft.fillRect(cx - 9, cy, 20, 5, 0x5D1F);
        tft.drawLine(cx - 5, cy + 7, cx - 7, cy + 12, theme.textPri);
        tft.drawLine(cx + 1, cy + 7, cx - 1, cy + 12, theme.textPri);
        tft.drawLine(cx + 7, cy + 7, cx + 5, cy + 12, theme.textPri);
    } else if (code.startsWith("11")) {
        // Thunderstorm — cloud + yellow zigzag bolt
        tft.fillCircle(cx - 3, cy - 5, 6, 0x7BEF);
        tft.fillCircle(cx + 5, cy - 6, 7, 0x7BEF);
        tft.fillRect(cx - 9, cy - 1, 20, 5, 0x7BEF);
        tft.fillTriangle(cx, cy + 3, cx - 4, cy + 8, cx + 2, cy + 8, 0xFEE0);
        tft.fillTriangle(cx - 2, cy + 7, cx - 6, cy + 13, cx + 1, cy + 13, 0xFEE0);
    } else if (code.startsWith("13")) {
        // Snow — cloud + 3 dots
        tft.fillCircle(cx - 3, cy - 4, 6, theme.textSec);
        tft.fillCircle(cx + 5, cy - 5, 7, theme.textSec);
        tft.fillRect(cx - 9, cy, 20, 5, theme.textSec);
        tft.fillCircle(cx - 5, cy + 9, 2, theme.textPri);
        tft.fillCircle(cx + 1, cy + 11, 2, theme.textPri);
        tft.fillCircle(cx + 7, cy + 9, 2, theme.textPri);
    } else if (code.startsWith("50")) {
        // Fog — 3 horizontal lines
        for (int i = 0; i < 3; i++) {
            int ly = cy - 4 + i * 6;
            tft.drawFastHLine(cx - 10, ly, 20, theme.textDim);
            tft.drawFastHLine(cx - 10, ly + 1, 20, theme.textDim);
        }
    } else {
        // Unknown — small question mark circle
        tft.drawCircle(cx, cy, 8, theme.textDim);
    }
}

// ---------------------------------------------------------------------------
// Hero widget (y=24..123) — merged clock + weather, side-by-side
// ---------------------------------------------------------------------------
static void drawHero(const struct tm& t) {
    fillPanel(LAYOUT_HERO_Y, LAYOUT_HERO_H, theme.bg);

    // Main card
    const int cardX = 8, cardW = SCREEN_W - 16;
    const int cardY = LAYOUT_HERO_Y + 4, cardH = LAYOUT_HERO_H - 8;
    tft.fillRoundRect(cardX, cardY, cardW, cardH, 12, theme.panel);

    // ── Left zone: Clock (x=12..134) ──
    char hmBuf[6];
    snprintf(hmBuf, sizeof(hmBuf), "%02d:%02d", t.tm_hour, t.tm_min);

    tft.setTextColor(theme.textPri, theme.panel);
    // Ensure clock width/layout uses a deterministic text scale.
    tft.setTextSize(1);
    tft.setFreeFont(&FreeSansBold24pt7b);
    int tw = tft.textWidth(hmBuf);
    int clockX = 12 + (122 - tw) / 2;
    if (clockX < 12) clockX = 12;
    tft.setCursor(clockX, cardY + 42);
    tft.print(hmBuf);

    // Prayer countdown subtitle under the clock.
    auto prayerShortLabel = [](int idx) -> const char* {
        switch (idx) {
            case 0: return "FJR";
            case 1: return "SUN";
            case 2: return "DHR";
            case 3: return "ASR";
            case 4: return "MGB";
            case 5: return "ISH";
            default: return "---";
        }
    };

    tft.setFreeFont(nullptr);
    tft.setTextSize(2);
    tft.setTextColor(theme.textDim, theme.panel);
    char nextBuf[40];
    int minutesLeft = Prayer::minutesUntilNext();
    if (Prayer::current.valid && Prayer::current.nextIndex >= 0 && minutesLeft >= 0) {
        int h = minutesLeft / 60;
        int m = minutesLeft % 60;
        snprintf(nextBuf, sizeof(nextBuf), "%s %02d:%02d",
                 prayerShortLabel(Prayer::current.nextIndex),
                 h, m);
    } else {
        snprintf(nextBuf, sizeof(nextBuf), "--- --:--");
    }
    int subW = tft.textWidth(nextBuf);
    int subX = 12 + (122 - subW) / 2;
    if (subX < 12) subX = 12;
    tft.setCursor(subX, cardY + 64);
    tft.print(nextBuf);

    // ── Vertical divider ──
    const int divX = 138;
    tft.drawFastVLine(divX, cardY + 8, cardH - 16, theme.separator);

    // ── Right zone: Weather (x=142..224) ──
    const int rZoneX = 146;

    if (!Weather::current.valid) {
        const char* line2 = "loading...";
        if (Weather::current.fetchState == Weather::WEATHER_NO_KEY)    line2 = "No API key";
        if (Weather::current.fetchState == Weather::WEATHER_NET_ERROR) line2 = "Net error";
        tft.setFreeFont(nullptr);
        tft.setTextSize(1);
        tft.setTextColor(theme.textDim, theme.panel);
        tft.setCursor(rZoneX, cardY + 28);
        tft.print("Weather");
        tft.setCursor(rZoneX, cardY + 40);
        tft.print(line2);
    } else {
        const Weather::Data& w = Weather::current;
        uint16_t wAccent = Weather::iconColor(w.iconCode);

        // Weather icon (centered in right zone, near top)
        int iconCx = rZoneX + 38;
        int iconCy = cardY + 22;
        drawWeatherIcon(iconCx, iconCy, w.iconCode);

        // Temperature below icon
        char tempBuf[12];
        const char* unit = (strcmp(Settings::owmUnits, "imperial") == 0) ? "F" : "C";
        snprintf(tempBuf, sizeof(tempBuf), "%.0f°%s", w.temp, unit);

        tft.setFreeFont(&FreeSansBold12pt7b);
        tft.setTextColor(wAccent, theme.panel);
        tw = tft.textWidth(tempBuf);
        int tempX = rZoneX + (76 - tw) / 2;
        tft.setCursor(tempX, cardY + 56);
        tft.print(tempBuf);

        // Condition label
        tft.setFreeFont(nullptr);
        tft.setTextSize(1);
        tft.setTextColor(theme.textSec, theme.panel);
        const char* label = Weather::iconLabel(w.iconCode);
        int lw = tft.textWidth(label);
        int lx = rZoneX + (76 - lw) / 2;
        tft.setCursor(lx, cardY + 66);
        tft.print(label);

        // City name
        tft.setTextColor(theme.textDim, theme.panel);
        int cn = tft.textWidth(w.cityName);
        int cnx = rZoneX + (76 - cn) / 2;
        tft.setCursor(cnx, cardY + 78);
        tft.print(w.cityName);
    }

    hline(LAYOUT_HERO_Y + LAYOUT_HERO_H - 1);
}

// ---------------------------------------------------------------------------
// Prayer panel (y=124..319) — expanded single-column, 6 rows, readable fonts
// ---------------------------------------------------------------------------
static void drawPrayerPanel() {
    fillPanel(LAYOUT_PANEL_Y, LAYOUT_PANEL_H, theme.bg);

    tft.setFreeFont(nullptr);

    if (!Prayer::current.valid) {
        tft.fillRoundRect(8, LAYOUT_PANEL_Y + 8, SCREEN_W - 16, LAYOUT_PANEL_H - 16, 12, theme.panel);
        tft.setTextSize(2);
        tft.setTextColor(theme.textDim, theme.panel);
        tft.setCursor(20, LAYOUT_PANEL_Y + 88);
        tft.print("Loading...");
        return;
    }

    tft.fillRoundRect(8, LAYOUT_PANEL_Y + 8, SCREEN_W - 16, LAYOUT_PANEL_H - 16, 12, theme.panel);

    const int pendingForFooter = Prayer::pendingPrayerIndex();
    const int footerH = pendingForFooter >= 0 ? 48 : 0;
    const int ROW_H  = 26;
    const int START_Y = LAYOUT_PANEL_Y + 14;

    for (int i = 0; i < Prayer::PRAYER_COUNT; i++) {
        Prayer::RowState rowState = Prayer::rowStateForIndex(i);
        int ry = START_Y + i * ROW_H;
        if (ry + ROW_H > (LAYOUT_PANEL_Y + LAYOUT_PANEL_H - footerH - 10)) break;

        uint16_t rowBg = theme.panel;
        uint16_t fg = theme.textSec;
        char snoozeLabelBuf[14] = "";
        const char* statusText = "";
        uint16_t edgeColor = theme.separator;

        switch (rowState) {
            case Prayer::ROW_UPCOMING:
                rowBg = theme.highlightBg;
                fg = theme.textPri;
                edgeColor = theme.gold;
                break;
            case Prayer::ROW_PENDING:
                rowBg = theme.accent;
                fg = theme.textPri;
                statusText = "DUE";
                edgeColor = theme.accent;
                break;
            case Prayer::ROW_SNOOZED: {
                rowBg = theme.highlightBg;
                fg = theme.textPri;
                edgeColor = theme.textSec;
                char untilBuf[6] = {};
                if (Prayer::snoozedUntilText(untilBuf, sizeof(untilBuf))) {
                    snprintf(snoozeLabelBuf, sizeof(snoozeLabelBuf), "SNZD%s", untilBuf);
                } else {
                    strncpy(snoozeLabelBuf, "SNZD", sizeof(snoozeLabelBuf));
                }
                statusText = snoozeLabelBuf;
                break;
            }
            case Prayer::ROW_DONE:
                fg = theme.green;
                edgeColor = theme.green;
                break;
            case Prayer::ROW_MISSED:
                fg = theme.red;
                statusText = "MISS";
                edgeColor = theme.red;
                break;
            case Prayer::ROW_NORMAL:
            default:
                if (i == Prayer::current.nextIndex) {
                    fg = theme.gold;
                    edgeColor = theme.gold;
                }
                break;
        }

        tft.fillRect(14, ry, SCREEN_W - 28, ROW_H - 2, rowBg);
        tft.fillRect(14, ry, 5, ROW_H - 2, edgeColor);

        // Prayer name (left) — size 2 for readability
        tft.setTextSize(2);
        tft.setTextColor(fg, rowBg);
        tft.setCursor(26, ry + 5);
        tft.print(Prayer::current.prayers[i].name);

        // Status tag (right of name)
        if (statusText[0] != '\0') {
            tft.setTextSize(1);
            tft.setTextColor((rowState == Prayer::ROW_PENDING) ? theme.textPri : theme.textDim, rowBg);
            tft.setCursor(100, ry + 8);
            tft.print(statusText);
        }

        // Prayer time (right-aligned) — size 2
        const char* timeStr = Prayer::current.prayers[i].time;
        tft.setTextSize(2);
        tft.setTextColor(fg, rowBg);
        int tw = tft.textWidth(timeStr);
        tft.setCursor(SCREEN_W - tw - 24, ry + 5);
        tft.print(timeStr);

        // Separator line below each row (except last)
        if (i < Prayer::PRAYER_COUNT - 1) {
            tft.drawFastHLine(20, ry + ROW_H - 2, SCREEN_W - 40, theme.separator);
        }
    }

    // Touch affordance chevrons
    tft.setTextSize(2);
    tft.setTextColor(theme.textDim, theme.bg);
    tft.setCursor(1, LAYOUT_PANEL_Y + LAYOUT_PANEL_H / 2 - 6);
    tft.print("<");
    int rw = tft.textWidth(">");
    tft.setCursor(SCREEN_W - rw - 1, LAYOUT_PANEL_Y + LAYOUT_PANEL_H / 2 - 6);
    tft.print(">");

    if (footerH > 0) {
        int bW = SCREEN_W / 2 - 18;
        int fy = SCREEN_H - footerH - 4;
        int bH = footerH - 8;

        // "Prayed" button (green)
        tft.fillRoundRect(14, fy, bW, bH, 8, theme.green);
        tft.setTextSize(2);
        tft.setTextColor(theme.textPri, theme.green);
        const char* pLabel = "Prayed";
        int plw = tft.textWidth(pLabel);
        tft.setCursor(14 + (bW - plw) / 2, fy + (bH - 16) / 2);
        tft.print(pLabel);

        // "Snooze" button (gold, dimmed if already snoozed or cap reached)
        int bx2 = SCREEN_W / 2 + 4;
        bool footerSnoozed = Prayer::isSnoozed(pendingForFooter);
        bool footerCapHit  = (Prayer::current.snoozeCount >= PRAYER_MAX_SNOOZE_COUNT);
        uint16_t sBg = (footerSnoozed || footerCapHit) ? theme.textDim : theme.gold;
        tft.fillRoundRect(bx2, fy, bW, bH, 8, sBg);
        tft.setTextColor(theme.bg, sBg);
        char sLabelBuf[12];
        if (footerSnoozed)     strncpy(sLabelBuf, "Snoozed",  sizeof(sLabelBuf));
        else if (footerCapHit) strncpy(sLabelBuf, "No More",  sizeof(sLabelBuf));
        else                   strncpy(sLabelBuf, "Snooze",   sizeof(sLabelBuf));
        int slw = tft.textWidth(sLabelBuf);
        tft.setCursor(bx2 + (bW - slw) / 2, fy + (bH - 16) / 2);
        tft.print(sLabelBuf);
    }
}

static void drawAzanScreen() {
    fillPanel(0, SCREEN_H, theme.bg);

    int prayerIndex = azanPrayerIndex;
    if (prayerIndex < 0) {
        prayerIndex = Prayer::pendingPrayerIndex();
    }
    if (prayerIndex < 0) {
        azanScreenActive = false;
        needsRedraw = true;
        return;
    }

    // Vertically centered content group: title(28) + gap(12) + name(38) + gap(8) + time(20) + gap(10) + instruction(16) = ~132
    const int buttonH = 48;
    const int buttonMargin = AZAN_BUTTON_MARGIN;
    const int contentH = AZAN_CONTENT_H;
    const int startY = AZAN_START_Y;

    // Card behind content
    tft.fillRoundRect(12, startY - 16, SCREEN_W - 24, contentH + 32, 16, theme.panel);

    // X dismiss button — top-right corner of card
    tft.setFreeFont(nullptr);
    tft.setTextSize(1);
    tft.setTextColor(theme.textDim, theme.panel);
    tft.setCursor(SCREEN_W - 30, startY - 11);
    tft.print("[X]");

    // "Prayer Time" title
    tft.setFreeFont(&FreeSansBold18pt7b);
    tft.setTextColor(theme.accent, theme.panel);
    String title = "Prayer Time";
    int tw = tft.textWidth(title);
    tft.setCursor((SCREEN_W - tw) / 2, startY + 24);
    tft.print(title);

    // Prayer name (large)
    tft.setFreeFont(&FreeSansBold24pt7b);
    tft.setTextColor(theme.textPri, theme.panel);
    const char* name = Prayer::current.prayers[prayerIndex].name;
    tw = tft.textWidth(name);
    tft.setCursor((SCREEN_W - tw) / 2, startY + 74);
    tft.print(name);

    // Prayer time
    tft.setFreeFont(nullptr);
    tft.setTextSize(2);
    tft.setTextColor(theme.gold, theme.panel);
    const char* timeStr = Prayer::current.prayers[prayerIndex].time;
    tw = tft.textWidth(timeStr);
    tft.setCursor((SCREEN_W - tw) / 2, startY + 90);
    tft.print(timeStr);

    // Instruction text — size 2 for readability
    tft.setTextSize(1);
    tft.setTextColor(theme.textSec, theme.panel);
    const char* instr = "Tap to mark prayed or snooze";
    tw = tft.textWidth(instr);
    tft.setCursor((SCREEN_W - tw) / 2, startY + 116);
    tft.print(instr);

    // Buttons — centered labels
    int buttonTop = SCREEN_H - buttonMargin;
    int bW = SCREEN_W / 2 - 18;

    tft.fillRoundRect(12, buttonTop, bW, buttonH, 10, theme.green);
    tft.setTextSize(2);
    tft.setTextColor(theme.textPri, theme.green);
    const char* pLabel = "Prayed";
    int plw = tft.textWidth(pLabel);
    tft.setCursor(12 + (bW - plw) / 2, buttonTop + (buttonH - 16) / 2);
    tft.print(pLabel);

    int bx2 = SCREEN_W / 2 + 6;
    bool azanSnoozed = Prayer::isSnoozed(prayerIndex);
    bool azanCapHit  = (Prayer::current.snoozeCount >= PRAYER_MAX_SNOOZE_COUNT);
    uint16_t azanSnBg = (azanSnoozed || azanCapHit) ? theme.textDim : theme.gold;
    tft.fillRoundRect(bx2, buttonTop, bW, buttonH, 10, azanSnBg);
    tft.setTextColor(theme.bg, azanSnBg);
    char sLabelBuf[12];
    if (azanSnoozed)     strncpy(sLabelBuf, "Snoozed",  sizeof(sLabelBuf));
    else if (azanCapHit) strncpy(sLabelBuf, "No More",  sizeof(sLabelBuf));
    else                 strncpy(sLabelBuf, "Snooze",   sizeof(sLabelBuf));
    int slw = tft.textWidth(sLabelBuf);
    tft.setCursor(bx2 + (bW - slw) / 2, buttonTop + (buttonH - 16) / 2);
    tft.print(sLabelBuf);

    // Reset text state so subsequent partial draws start from a known baseline.
    tft.setFreeFont(nullptr);
    tft.setTextSize(1);
}

// ---------------------------------------------------------------------------
// Stocks panel (y=124..319) — expanded, readable fonts, no zebra striping
// ---------------------------------------------------------------------------
static void drawStocksPanel() {
    fillPanel(LAYOUT_PANEL_Y, LAYOUT_PANEL_H, theme.bg);

    tft.setFreeFont(nullptr);

    int n = Stocks::symbolCount();
    if (n == 0) {
        tft.fillRoundRect(8, LAYOUT_PANEL_Y + 8, SCREEN_W - 16, LAYOUT_PANEL_H - 16, 12, theme.panel);
        tft.setTextSize(2);
        tft.setTextColor(theme.textDim, theme.panel);
        tft.setCursor(20, LAYOUT_PANEL_Y + 88);
        tft.print("No stocks set.");
        return;
    }

    tft.fillRoundRect(8, LAYOUT_PANEL_Y + 8, SCREEN_W - 16, LAYOUT_PANEL_H - 16, 12, theme.panel);

    const int ROW_H  = 32;
    const int START_Y = LAYOUT_PANEL_Y + 14;
    int order[MAX_STOCKS];
    int orderCount = 0;

    for (int i = 0; i < MAX_STOCKS; i++) {
        if (strlen(Settings::stockSymbols[i]) == 0) continue;
        order[orderCount++] = i;
    }

    // Stable insertion sort: valid quotes first, then by |changePct| descending.
    for (int i = 1; i < orderCount; i++) {
        int key = order[i];
        const Stocks::Quote& kq = Stocks::quotes[key];
        float keyAbs = kq.changePct >= 0 ? kq.changePct : -kq.changePct;
        float keyRank = kq.valid ? keyAbs : -1.0f;
        int j = i - 1;

        while (j >= 0) {
            const Stocks::Quote& jq = Stocks::quotes[order[j]];
            float jAbs = jq.changePct >= 0 ? jq.changePct : -jq.changePct;
            float jRank = jq.valid ? jAbs : -1.0f;
            if (jRank >= keyRank) break;
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }

    int shown = 0;

    for (int oi = 0; oi < orderCount && shown < 5; oi++) {
        int i = order[oi];
        const Stocks::Quote& q = Stocks::quotes[i];

        int ry = START_Y + shown * ROW_H;
        uint16_t rowBg = theme.panel;   // uniform bg — no zebra
        uint16_t edgeColor = theme.separator;

        if (q.valid) {
            edgeColor = (q.changePct >= 0) ? theme.green : theme.red;
        }

        tft.fillRect(14, ry, SCREEN_W - 28, ROW_H - 2, rowBg);
        tft.fillRect(14, ry, 5, ROW_H - 2, edgeColor);

        if (!q.valid) {
            tft.setTextSize(2);
            tft.setTextColor(theme.textDim, rowBg);
            tft.setCursor(26, ry + 8);
            tft.print(Settings::stockSymbols[i]);
            tft.setCursor(154, ry + 8);
            tft.print("...");
        } else {
            uint16_t pctColor = (q.changePct >= 0) ? theme.green : theme.red;

            // Row 1: Symbol + Change % (size 2) — primary signal
            tft.setTextSize(2);
            tft.setTextColor(theme.textPri, rowBg);
            tft.setCursor(26, ry + 2);
            if (strlen(q.symbol) > 0) {
                tft.print(q.symbol);
            } else {
                tft.print(Settings::stockSymbols[i]);
            }

            char pctBuf[12];
            snprintf(pctBuf, sizeof(pctBuf), "%+.2f%%", q.changePct);
            tft.setTextColor(pctColor, rowBg);
            int pctW = tft.textWidth(pctBuf);
            tft.setCursor(SCREEN_W - pctW - 24, ry + 2);
            tft.print(pctBuf);

            // Row 2: Price (size 1) — secondary context
            tft.setTextSize(1);
            tft.setTextColor(theme.textDim, rowBg);
            tft.setCursor(26, ry + 21);
            tft.print("Price");

            char priceBuf[12];
            snprintf(priceBuf, sizeof(priceBuf), "$%.2f", q.price);
            tft.setTextColor(theme.textSec, rowBg);
            int priceW = tft.textWidth(priceBuf);
            tft.setCursor(SCREEN_W - priceW - 24, ry + 21);
            tft.print(priceBuf);

            // Alert dot
            if (q.alertTriggered) {
                tft.fillCircle(SCREEN_W - 36, ry + 8, 3, theme.gold);
            }
        }

        // Separator
        if (shown < orderCount - 1) {
            tft.drawFastHLine(20, ry + ROW_H - 1, SCREEN_W - 40, theme.separator);
        }
        shown++;
    }

    // Touch affordance chevrons
    tft.setTextSize(2);
    tft.setTextColor(theme.textDim, theme.bg);
    tft.setCursor(1, LAYOUT_PANEL_Y + LAYOUT_PANEL_H / 2 - 6);
    tft.print("<");
    int rw = tft.textWidth(">");
    tft.setCursor(SCREEN_W - rw - 1, LAYOUT_PANEL_Y + LAYOUT_PANEL_H / 2 - 6);
    tft.print(">");

    if (shown == 0) {
        tft.setTextSize(2);
        tft.setTextColor(theme.textDim, theme.panel);
        tft.setCursor(20, LAYOUT_PANEL_Y + 88);
        tft.print("Fetching...");
    }
}

// ---------------------------------------------------------------------------
// Notification banner (temporary overlay at top of panel)
// ---------------------------------------------------------------------------
static unsigned long bannerExpiry = 0;
static char bannerText[64] = {};

static void drawBannerIfActive() {
    if (azanScreenActive) return;
    if (millis() >= bannerExpiry) return;
    tft.fillRect(0, LAYOUT_PANEL_Y, SCREEN_W, 28, theme.accent);
    tft.setFreeFont(nullptr);
    tft.setTextSize(2);
    tft.setTextColor(theme.textPri, theme.accent);
    int tw = tft.textWidth(bannerText);
    tft.setCursor((SCREEN_W - tw) / 2, LAYOUT_PANEL_Y + 6);
    tft.print(bannerText);

    // Keep global text state predictable for other widgets.
    tft.setTextSize(1);
}

static void showBanner(const char* text, uint32_t durationMs = 5000) {
    strncpy(bannerText, text, sizeof(bannerText) - 1);
    bannerExpiry = millis() + durationMs;
    drawBannerIfActive();  // paint immediately, no tick delay
}

// ---------------------------------------------------------------------------
// Full screen redraw
// ---------------------------------------------------------------------------
static void redraw(bool wifiOk, const String& ipAddr,
                   const struct tm& t, const String& dateStr) {
    if (azanScreenActive) {
        drawAzanScreen();
        needsRedraw = false;
        return;
    }

    // Safety net: keep panel page state valid across AP/STA transitions.
    coerceAllowedActivePage();

    drawStatusBar(wifiOk, dateStr, ipAddr);
    drawHero(t);

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
    if (azanScreenActive) {
        drawAzanScreen();
        return;
    }
    drawHero(t);
}

static void updateWeather() {
    if (azanScreenActive) return;
    // Weather is part of the hero widget now; needs time for clock portion
    struct tm t;
    if (getLocalTime(&t)) {
        drawHero(t);
    }
}

static void updatePanel() {
    if (azanScreenActive) {
        drawAzanScreen();
        return;
    }
    coerceAllowedActivePage();
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
    tft.fillRoundRect(20, 96, SCREEN_W - 40, 116, 18, theme.panel);
    tft.setFreeFont(&FreeSansBold18pt7b);
    tft.setTextColor(theme.accent, theme.panel);
    String title = "DeskNexus";
    int tw = tft.textWidth(title);
    tft.setCursor((SCREEN_W - tw) / 2, 140);
    tft.print(title);

    tft.setFreeFont(nullptr);
    tft.setTextSize(2);
    tft.setTextColor(theme.textSec, theme.panel);
    String sub = "Smart Desk Clock";
    tw = tft.textWidth(sub);
    tft.setCursor((SCREEN_W - tw) / 2, 160);
    tft.print(sub);
}

static void showSplashStatus(const char* msg) {
    tft.fillRect(24, 186, SCREEN_W - 48, 24, theme.panel);
    tft.setFreeFont(nullptr);
    tft.setTextSize(2);
    tft.setTextColor(theme.textSec, theme.panel);
    int tw = tft.textWidth(msg);
    tft.setCursor((SCREEN_W - tw) / 2, 190);
    tft.print(msg);
}

} // namespace UI
