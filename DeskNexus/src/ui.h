/*
 * ui.h — TFT Display UI Helpers  (v3 — modern merged hero, improved readability)
 *
 * Colour palette, layout constants, and drawing routines for the
 * 240×320 portrait TFT (ILI9341) on the ESP32-2432S028R (CYD).
 *
 * Screen layout (portrait, 240 wide × 320 tall):
 *
 *  ┌────────────────────────┐ y=0
 *  │  Status bar  (24 px)   │  WiFi dot · date · ■ ● ○ ○ page dots
 *  ├────────────────────────┤ y=24
 *  │  Hero       (100 px)   │  HH:MM + weekday (left) │ weather (right)
 *  │                        │  icon · temp · condition · humidity · city
 *  ├────────────────────────┤ y=124
 *  │                        │
 *  │  Panel      (196 px)   │  Home (0), Prayer (1), Forecast (2), Stocks (3)
 *  │                        │  Home is the dashboard / hub; the rest are detail pages
 *  └────────────────────────┘ y=320
 *
 * Navigation is location-based — you reach a page by tapping its widget on the
 * Home dashboard: tap the weather (hero, right) → weather page, tap the prayer
 * section → prayer detail, tap the stocks section → stocks detail. On any detail
 * page, a tap on the panel returns to Home. After 60s idle it also returns to
 * Home automatically. (Pages no longer auto-rotate on a timer.)
 *
 * Fonts used: TFT_eSPI built-in Free fonts (FreeSansBold24pt7b etc.)
 */

#pragma once

#include <TFT_eSPI.h>
#include <qrcode.h>
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

// Slate & Amber — a calm near-black slate with a single warm amber accent.
// Mint is reserved for positive/up/done, red for negative/down, both used
// sparingly so the amber stays the one thing that draws the eye.
static const Theme THEME_DARK = {
    0x0882,  // bg          — #0D1015 slate-black canvas
    0x10C4,  // panel       — #161B22 elevated card surface
    0xFD8A,  // accent      — #FFB053 warm amber (the one brand accent)
    0xFFFF,  // textPri     — #FFFFFF white
    0x9D36,  // textSec     — #9BA6B4 cool slate-grey
    0x6BB0,  // textDim     — #6B7682 muted slate (meets AA on panel)
    0x3ED4,  // green       — #3DD9A0 mint (positive / done)
    0xFB2E,  // red         — #FF6470 soft red (negative)
    0xFE4D,  // gold        — #FFCB6B warm gold (highlights)
    0x2987,  // separator   — #2A323D hairline
    0x3143,  // highlightBg — #33291A warm amber tint (next-prayer row)
};

static const Theme THEME_LIGHT = {
    0xEF7E,  // bg          — #ECEEF2 cool light-slate canvas
    0xFFFF,  // panel       — #FFFFFF pure white card
    0xE483,  // accent      — #E0901E amber (fill with dark ink text)
    0x1105,  // textPri     — #16202B ink slate (high contrast)
    0x4AAC,  // textSec     — #4A5564 slate-600
    0x6BD1,  // textDim     — #6E7A88 slate-500 (meets AA)
    0x144B,  // green       — #138A5E emerald (positive / done)
    0xD1C7,  // red         — #D33A3A clean red
    0xAB62,  // gold        — #A86E10 deep amber
    0xD6FC,  // separator   — #D7DCE3 cool hairline
    0xFF5A,  // highlightBg — #FCEBD0 soft amber tint (next-prayer row)
};

// ---------------------------------------------------------------------------
// Layout constants  (v3 — merged hero widget, larger panel)
// ---------------------------------------------------------------------------
#define SCREEN_W  240
#define SCREEN_H  320

// There is no status bar or hero any more — every page fills the whole screen.
// Home draws its own tile grid (see HOME_* below); the detail pages draw a
// single full-screen panel via the shared LAYOUT_PANEL_* rect.
#define PANEL_Y_FULL       0
#define PANEL_H_FULL       SCREEN_H

// Active panel rect — updated per page by updatePanelLayout().
static int LAYOUT_PANEL_Y = PANEL_Y_FULL;
static int LAYOUT_PANEL_H = PANEL_H_FULL;

// ── Home dashboard layout ────────────────────────────────────────────────
// A card-less dashboard with strong hierarchy, divided by hairlines:
//   Hero    — dominant clock + date (left), weather chip (right)
//   Prayer  — next prayer name, time, countdown + progress bar
//   Markets — top-moving stock: name/price + daily change
// Tapping the weather chip opens Forecast; the prayer band opens Prayer; the
// markets band opens Stocks. The clock itself is not a link.
#define HOME_PAD     14
#define HOME_HERO_Y  0
#define HOME_HERO_H  120
#define HOME_PRA_Y   120                 // next-prayer band
#define HOME_PRA_H   108
#define HOME_STK_Y   228                 // markets band
#define HOME_STK_H   (SCREEN_H - HOME_STK_Y)
// Weather chip occupies the right portion of the hero; taps here open Forecast.
#define HOME_WEATHER_X  128

// Page indices (no Settings page on display)
#define PAGE_HOME      0
#define PAGE_PRAYER    1
#define PAGE_FORECAST  2
#define PAGE_STOCKS    3
#define PAGE_BREAK     4
#define PAGE_COUNT     5

namespace UI {

// ── Azan screen layout constants (shared by drawAzanScreen + handleTouch) ──
static constexpr int AZAN_BUTTON_MARGIN = 68;   // height reserved for bottom buttons
static constexpr int AZAN_CONTENT_H     = 200;  // expanded text-content block height
static constexpr int AZAN_START_Y       = 10;    // start near top
// X-dismiss hit zone: top-right corner of the card (y < 50, x > SCREEN_W-40)
static constexpr int AZAN_DISMISS_X     = SCREEN_W - 40;
static constexpr int AZAN_DISMISS_Y_MAX = 50;

// ── Banner constants ───────────────────────────────────────────────────────
static constexpr int BANNER_H = 36;   // taller banner for readability

// Banner color types — controls banner background per event urgency
enum BannerStyle { BANNER_ACCENT, BANNER_GOLD, BANNER_RED };

// ── State ──────────────────────────────────────────────────────────────────
static TFT_eSPI tft = TFT_eSPI();
static Theme    theme       = THEME_DARK;
static bool     isDarkTheme = true;
static int      activePage  = PAGE_HOME;
static bool     needsRedraw = true;
static bool     dimmed      = false;
static bool     azanScreenActive = false;
static int      azanPrayerIndex = -1;
static unsigned long azanScreenStart = 0;   // millis() when the azan screen was shown
static int      prayerScrollOffset = 0;
// Page transition animation state
static int      transitionDir     = 0;  // -1=left, 0=none, 1=right
static int      transitionStep    = 0;
static int      transitionTarget  = -1;

// Touch & carousel timing
static unsigned long lastTouchMs        = 0;
static unsigned long lastPageSwitch     = 0;
static unsigned long carouselPauseStart    = 0;  // millis() when the carousel was paused
static unsigned long carouselPauseDuration = 0;  // 0 = not paused
// Page dot pulse animation
static unsigned long dotPulseStart      = 0;

// Break reminder state
static unsigned long breakLastNotify    = 0;     // millis() of last break notification
static bool          breakScreenActive  = false;  // full-screen break reminder showing
static unsigned long breakScreenStart   = 0;      // when break screen was shown (for duration calc)

// ── Touch calibration ─────────────────────────────────────────────────────
static uint16_t calData[5] = TOUCH_CAL_DATA;

// ── Small helpers ──────────────────────────────────────────────────────────
static void fillPanel(int y, int h, uint16_t color) {
    tft.fillRect(0, y, SCREEN_W, h, color);
}

// Forward declarations for functions used before their definition
static void playPanelTransition(int direction);
static void dismissBreakReminder();
static void showBanner(const char* text, uint32_t durationMs, BannerStyle style);
static void drawPanelChevrons();

static void updateTheme() {
    bool wantDark;

    switch (Settings::themeMode) {
        case 1:  wantDark = true;  break;  // always dark
        case 2:  wantDark = false; break;  // always light
        default: {
            // Auto: dark between Maghrib and Sunrise, light otherwise
            if (Prayer::current.valid) {
                struct tm t;
                if (getLocalTime(&t)) {
                    int nowMin = t.tm_hour * 60 + t.tm_min;
                    int sunrise  = Prayer::toMinutes(Prayer::current.prayers[1].time);
                    int maghrib  = Prayer::toMinutes(Prayer::current.prayers[4].time);
                    wantDark = (nowMin >= maghrib || nowMin < sunrise);
                } else {
                    wantDark = Settings::themeDark;
                }
            } else {
                wantDark = Settings::themeDark;
            }
            Settings::themeDark = wantDark;  // keep in sync for NVS
            break;
        }
    }

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

    lastTouchMs      = millis();
    lastPageSwitch   = millis();
    breakLastNotify  = millis();  // start counting from boot
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
    if (page == PAGE_FORECAST) {
        return Network::isConnected() && Weather::forecast.valid;
    }
    if (page == PAGE_BREAK) {
        return Settings::breakReminderEnabled;
    }
    return true;
}

static bool coerceAllowedActivePage() {
    if (isPageAllowed(activePage)) return false;
    activePage = PAGE_HOME;
    needsRedraw = true;
    return true;
}

// Every page now fills the whole screen (no status bar / hero). Kept as a hook
// in case pages need different rects again, and so the banner overlay and
// detail panels share a single source of truth.
static void updatePanelLayout() {
    LAYOUT_PANEL_Y = PANEL_Y_FULL;   // 0
    LAYOUT_PANEL_H = PANEL_H_FULL;   // full height
}

// Return to Home page after idle timeout on any detail page
static bool shouldReturnToHome() {
    if (activePage == PAGE_HOME) return false;
    return (millis() - lastTouchMs) >= CAROUSEL_IDLE_RETURN_MS;
}

static void returnToHome() {
    if (activePage == PAGE_HOME) return;
    playPanelTransition(-1);
    dotPulseStart = millis();
    activePage = PAGE_HOME;
    lastPageSwitch = millis();
    needsRedraw = true;
}

static void pauseCarousel() {
    carouselPauseStart    = millis();
    carouselPauseDuration = CAROUSEL_PAUSE_MS;
}

static void pauseCarouselFor(unsigned long ms) {
    carouselPauseStart    = millis();
    carouselPauseDuration = ms;
}

static bool hasPrayerFooterActions() {
    return activePage == PAGE_PRAYER && Prayer::pendingPrayerIndex() >= 0;
}

static void dismissAzanScreen() {
    if (!azanScreenActive) return;
    azanScreenActive = false;
    azanPrayerIndex = -1;
    azanScreenStart = 0;
    needsRedraw = true;
}

static void showAzanScreen(int prayerIndex) {
    azanScreenActive = true;
    azanPrayerIndex = prayerIndex;
    azanScreenStart = millis();
    activePage = PAGE_PRAYER;
    wake();
    pauseCarousel();
    needsRedraw = true;
}

static void updatePrayerUiState() {
    if (azanScreenActive && (millis() - azanScreenStart) >= PRAYER_FULLSCREEN_MS) {
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
                // Flash Prayed button (120ms visual feedback)
                tft.fillRoundRect(12, buttonTop, bW, 48, 10, theme.textDim);
                delay(120);
                if (Prayer::markPrayed(azanPrayerIndex)) {
                    dismissAzanScreen();
                    activePage = PAGE_HOME;  // return to Home after marking prayed
                }
            } else {
                // Flash Snooze button (120ms visual feedback)
                tft.fillRoundRect(SCREEN_W / 2 + 6, buttonTop, bW, 48, 10, theme.textDim);
                delay(120);
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
        const int footerTop = SCREEN_H - 56;
        if (ty >= footerTop) {
            // Visual flash feedback before action (120ms)
            const int bW = SCREEN_W / 2 - 18;
            const int fy = SCREEN_H - 52 - 4;  // matches drawPrayerPanel footer geometry
            const int bH = 40;
            if (tx < SCREEN_W / 2) {
                tft.fillRoundRect(14, fy, bW, bH, 8, theme.textDim);
                delay(120);
                Prayer::markPendingPrayed();
            } else {
                tft.fillRoundRect(SCREEN_W / 2 + 4, fy, bW, bH, 8, theme.textDim);
                delay(120);
                int fp = Prayer::pendingPrayerIndex();
                if (fp >= 0 && !Prayer::isSnoozed(fp)) {
                    Prayer::snoozePendingPrayer(fp);
                }
            }
            needsRedraw = true;
            return true;
        }
    }

    // Break reminder page — dismiss button
    if (activePage == PAGE_BREAK && (int)ty >= LAYOUT_PANEL_Y) {
        const int cardX = 8, cardW = SCREEN_W - 16;
        const int cardY = LAYOUT_PANEL_Y + 8, cardH = LAYOUT_PANEL_H - 16;
        const int btnW = 120, btnH = 32;
        const int btnX = cardX + (cardW - btnW) / 2;
        const int btnY = cardY + cardH - btnH - 12;
        if ((int)tx >= btnX && (int)tx < btnX + btnW &&
            (int)ty >= btnY && (int)ty < btnY + btnH) {
            // Flash button feedback
            tft.fillRoundRect(btnX, btnY, btnW, btnH, 8, theme.textDim);
            delay(120);
            dismissBreakReminder();
            return true;
        }
        // Any tap on break page dismisses it
        dismissBreakReminder();
        return true;
    }

    // Home dashboard — the clock itself isn't a link. Tapping the weather chip
    // (right side of the hero) opens Forecast; the prayer band opens Prayer; the
    // markets band opens Stocks.
    if (activePage == PAGE_HOME) {
        int targetPage;
        if ((int)ty < HOME_PRA_Y) {
            // Hero band: only the weather chip on the right is a link.
            if ((int)tx >= HOME_WEATHER_X) targetPage = PAGE_FORECAST;
            else return true;   // clock area — consume, no navigation
        } else if ((int)ty < HOME_STK_Y) {
            targetPage = PAGE_PRAYER;
        } else {
            targetPage = PAGE_STOCKS;
        }

        if (isPageAllowed(targetPage)) {
            playPanelTransition(1);
            dotPulseStart = millis();
            activePage = targetPage;
            lastPageSwitch = millis();
            needsRedraw = true;
        }
        return true;
    }

    // Tap a MISSED prayer row to retroactively mark it as prayed
    if (activePage == PAGE_PRAYER && Prayer::current.valid &&
        (int)ty >= LAYOUT_PANEL_Y && (int)ty < (SCREEN_H - 56)) {
        const int pendingForTouch = Prayer::pendingPrayerIndex();
        const int footerHTouch = pendingForTouch >= 0 ? 52 : 0;
        const int availHTouch = (LAYOUT_PANEL_Y + LAYOUT_PANEL_H - 14) - (LAYOUT_PANEL_Y + 14) - footerHTouch;
        const int ROW_H  = max(22, min(46, availHTouch / Prayer::PRAYER_COUNT));
        const int START_Y = LAYOUT_PANEL_Y + 14;
        int tappedIdx = ((int)ty - START_Y) / ROW_H + prayerScrollOffset;
        if (tappedIdx >= 0 && tappedIdx < Prayer::PRAYER_COUNT) {
            if (Prayer::rowStateForIndex(tappedIdx) == Prayer::ROW_MISSED) {
                Prayer::markPrayed(tappedIdx);
                needsRedraw = true;
                return true;  // consume — don't fall through to page switch
            }
        }
    }

    // Navigation is location-based: pages are chosen by tapping their widget on
    // the Home dashboard (weather/prayer/stocks). On any detail page, a tap on
    // the panel simply returns to Home so the next page can be picked.
    if (ty >= LAYOUT_PANEL_Y && activePage != PAGE_HOME) {
        returnToHome();
        return true;
    }
    return false;
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
// Helper: draw a small checkmark glyph at (x,y), size ~10px
// ---------------------------------------------------------------------------
static void drawCheckmark(int x, int y, uint16_t color) {
    tft.drawLine(x, y + 5, x + 3, y + 8, color);
    tft.drawLine(x + 1, y + 5, x + 4, y + 8, color);
    tft.drawLine(x + 3, y + 8, x + 9, y + 2, color);
    tft.drawLine(x + 4, y + 8, x + 10, y + 2, color);
}

// ---------------------------------------------------------------------------
// Helper: draw a small hourglass glyph at (x,y), size ~10px
// ---------------------------------------------------------------------------
static void drawHourglass(int x, int y, uint16_t color) {
    tft.fillTriangle(x, y, x + 8, y, x + 4, y + 5, color);        // top triangle
    tft.fillTriangle(x, y + 10, x + 8, y + 10, x + 4, y + 5, color); // bottom triangle
}

// ---------------------------------------------------------------------------
// Helper: draw a status pill badge (rounded rect with text)
// ---------------------------------------------------------------------------
static void drawPillBadge(int x, int y, const char* text, uint16_t bg, uint16_t fg) {
    tft.setTextSize(1);
    int tw = tft.textWidth(text);
    int pw = tw + 8;
    int ph = 12;
    tft.fillRoundRect(x, y, pw, ph, 4, bg);
    tft.setTextColor(fg, bg);
    tft.setCursor(x + 4, y + 2);
    tft.print(text);
}

// ---------------------------------------------------------------------------
// Prayer panel (y=124..319) — redesigned with adaptive row height,
// distinct state visuals, pill badges, checkmarks, and glyphs
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

    // Missed prayers count badge (top-right of card)
    int mc = Prayer::missedCount();
    if (mc > 1) {
        char mcBuf[8];
        snprintf(mcBuf, sizeof(mcBuf), "%d missed", mc);
        tft.setFreeFont(nullptr);
        tft.setTextSize(1);
        int mcW = tft.textWidth(mcBuf) + 8;
        int mcX = SCREEN_W - 16 - mcW;
        int mcY = LAYOUT_PANEL_Y + 10;
        tft.fillRoundRect(mcX, mcY, mcW, 12, 4, theme.red);
        tft.setTextColor(theme.textPri, theme.red);
        tft.setCursor(mcX + 4, mcY + 2);
        tft.print(mcBuf);
    }

    const int pendingForFooter = Prayer::pendingPrayerIndex();
    const int footerH = pendingForFooter >= 0 ? 52 : 0;
    const int CARD_TOP = LAYOUT_PANEL_Y + 14;
    const int CARD_BOTTOM = LAYOUT_PANEL_Y + LAYOUT_PANEL_H - 14;
    const int availH = CARD_BOTTOM - CARD_TOP - footerH;

    // Adaptive row height: ensure all 6 prayers fit
    const int ROW_H = max(22, min(46, availH / Prayer::PRAYER_COUNT));
    const int maxVisible = availH / ROW_H;
    const int START_Y = CARD_TOP;

    // Smart scroll: when footer is active, shift visible window so pending prayer is visible
    if (footerH > 0 && maxVisible < Prayer::PRAYER_COUNT) {
        prayerScrollOffset = max(0, min(pendingForFooter - 1,
                                        Prayer::PRAYER_COUNT - maxVisible));
    } else {
        prayerScrollOffset = 0;
    }

    // Hint that earlier prayers are scrolled off the top
    if (prayerScrollOffset > 0) {
        tft.setTextSize(1);
        tft.setTextColor(theme.textDim, theme.panel);
        int dotW = tft.textWidth("...");
        tft.setCursor((SCREEN_W - dotW) / 2, START_Y - 1);
        tft.print("...");
    }

    for (int i = prayerScrollOffset; i < Prayer::PRAYER_COUNT; i++) {
        int visIdx = i - prayerScrollOffset;
        if (visIdx >= maxVisible) break;

        Prayer::RowState rowState = Prayer::rowStateForIndex(i);
        int ry = START_Y + visIdx * ROW_H;

        uint16_t rowBg = theme.panel;
        uint16_t fg = theme.textSec;
        uint16_t edgeColor = theme.separator;
        const char* pillText = nullptr;
        uint16_t pillBg = 0, pillFg = 0;
        bool showCheckmark = false;
        bool showHourglass = false;
        bool showStrikethrough = false;
        bool pulseBg = false;

        switch (rowState) {
            case Prayer::ROW_UPCOMING:
                rowBg = theme.highlightBg;
                fg = theme.textPri;
                edgeColor = theme.gold;
                break;
            case Prayer::ROW_PENDING: {
                // Pulsing amber background
                pulseBg = true;
                bool pulseHi = (millis() / 500) % 2 == 0;
                rowBg = pulseHi ? theme.accent : theme.gold;
                fg = theme.textPri;
                edgeColor = theme.accent;
                pillText = "DUE";
                pillBg = theme.red;
                pillFg = theme.textPri;
                break;
            }
            case Prayer::ROW_SNOOZED: {
                rowBg = theme.highlightBg;
                fg = theme.textPri;
                edgeColor = theme.textSec;
                showHourglass = true;
                pillText = "SNZD";
                pillBg = theme.textDim;
                pillFg = theme.textPri;
                break;
            }
            case Prayer::ROW_DONE:
                fg = theme.green;
                edgeColor = theme.green;
                showCheckmark = true;
                break;
            case Prayer::ROW_MISSED:
                fg = theme.textDim;
                edgeColor = theme.red;
                showStrikethrough = true;
                pillText = "MISS";
                pillBg = theme.red;
                pillFg = theme.textPri;
                break;
            case Prayer::ROW_NORMAL:
            default:
                if (i == Prayer::current.nextIndex) {
                    fg = theme.gold;
                    edgeColor = theme.gold;
                }
                break;
        }

        // Row background
        tft.fillRect(14, ry, SCREEN_W - 28, ROW_H - 2, rowBg);

        // Left edge bar (5px wide — color signals state)
        tft.fillRect(14, ry, 5, ROW_H - 2, edgeColor);

        // Prayer name (left) — size 2 for readability
        tft.setFreeFont(&FreeSansBold9pt7b);
        tft.setTextSize(1);
        tft.setTextColor(fg, rowBg);
        tft.setCursor(26, ry + ROW_H - 7);
        tft.print(Prayer::current.prayers[i].name);

        // Checkmark glyph for DONE state
        if (showCheckmark) {
            int nameW = tft.textWidth(Prayer::current.prayers[i].name);
            drawCheckmark(26 + nameW + 4, ry + (ROW_H - 12) / 2, theme.green);
        }

        // Hourglass glyph for SNOOZED state
        if (showHourglass) {
            int nameW = tft.textWidth(Prayer::current.prayers[i].name);
            drawHourglass(26 + nameW + 4, ry + (ROW_H - 12) / 2, theme.textSec);
        }

        // Strikethrough line for MISSED state
        if (showStrikethrough) {
            int nameW = tft.textWidth(Prayer::current.prayers[i].name);
            int lineY = ry + ROW_H / 2;
            tft.drawFastHLine(26, lineY, nameW, theme.red);
            tft.drawFastHLine(26, lineY + 1, nameW, theme.red);
        }

        // Status pill badge (right of name)
        if (pillText) {
            drawPillBadge(100, ry + (ROW_H - 14) / 2, pillText, pillBg, pillFg);
        }

        // Tap hint for MISSED rows — lets user know the row is tappable
        if (rowState == Prayer::ROW_MISSED) {
            tft.setFreeFont(nullptr);
            tft.setTextSize(1);
            int pillW = tft.textWidth("MISS") + 8;
            tft.setTextColor(theme.textDim, rowBg);
            tft.setCursor(100 + pillW + 2, ry + (ROW_H - 14) / 2 + 2);
            tft.print("tap");
        }

        // Snooze until-time shown as a small secondary label after the pill
        if (rowState == Prayer::ROW_SNOOZED) {
            char untilBuf[6] = {};
            if (Prayer::snoozedUntilText(i, untilBuf, sizeof(untilBuf))) {
                tft.setFreeFont(nullptr);
                tft.setTextSize(1);
                tft.setTextColor(theme.textDim, rowBg);
                int pillW = tft.textWidth("SNZD") + 8;
                tft.setCursor(100 + pillW + 2, ry + (ROW_H - 14) / 2 + 2);
                tft.print(untilBuf);
            }
        }

        // Prayer time (right-aligned) — FreeSansBold9pt for crispness
        const char* timeStr = Prayer::current.prayers[i].time;
        tft.setFreeFont(&FreeSansBold9pt7b);
        tft.setTextSize(1);
        tft.setTextColor(fg, rowBg);
        int tw = tft.textWidth(timeStr);
        tft.setCursor(SCREEN_W - tw - 24, ry + ROW_H - 7);
        tft.print(timeStr);

        // Reset font for separator
        tft.setFreeFont(nullptr);

        // Separator line below each row (except last visible)
        if (visIdx < maxVisible - 1 && i < Prayer::PRAYER_COUNT - 1) {
            tft.drawFastHLine(20, ry + ROW_H - 2, SCREEN_W - 40, theme.separator);
        }
    }

    drawPanelChevrons();

    if (footerH > 0) {
        int bW = SCREEN_W / 2 - 18;
        int fy = SCREEN_H - footerH - 4;
        int bH = footerH - 12;

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
        bool footerCapHit  = (Prayer::current.snoozeCount[pendingForFooter] >= PRAYER_MAX_SNOOZE_COUNT);
        uint16_t sBg = (footerSnoozed || footerCapHit) ? theme.textDim : theme.gold;
        tft.fillRoundRect(bx2, fy, bW, bH, 8, sBg);
        tft.setTextColor(theme.textPri, sBg);
        char sLabelBuf[12];
        if (footerSnoozed)     strncpy(sLabelBuf, "Snoozed",  sizeof(sLabelBuf));
        else if (footerCapHit) strncpy(sLabelBuf, "No More",  sizeof(sLabelBuf));
        else                   strncpy(sLabelBuf, "Snooze",   sizeof(sLabelBuf));
        int slw = tft.textWidth(sLabelBuf);
        tft.setCursor(bx2 + (bW - slw) / 2, fy + (bH - 16) / 2);
        tft.print(sLabelBuf);
    }
}

// ---------------------------------------------------------------------------
// Home / Dashboard panel (y=124..319) — summary: current prayer + next prayer
// + top-moving stock. Tap sections to drill into detail pages.
// ---------------------------------------------------------------------------
// Left/right touch-affordance chevrons drawn on every carousel panel
// (Home, Prayer, Forecast, Stocks). Single source of truth so the four
// panels stay visually identical.
static void drawPanelChevrons() {
    tft.setFreeFont(nullptr);
    tft.setTextSize(2);
    tft.setTextColor(theme.textDim, theme.bg);
    tft.setCursor(1, LAYOUT_PANEL_Y + LAYOUT_PANEL_H / 2 - 6);
    tft.print("<");
    int rw = tft.textWidth(">");
    tft.setCursor(SCREEN_W - rw - 1, LAYOUT_PANEL_Y + LAYOUT_PANEL_H / 2 - 6);
    tft.print(">");
}

// ---------------------------------------------------------------------------
// Copy `src` into `out`, dropping trailing characters and appending an
// ellipsis until the text fits within `maxW` pixels at the CURRENT font/size.
// Leaves the text untouched when it already fits. Used to keep long stock
// names on a single line.
// ---------------------------------------------------------------------------
static void fitTextToWidth(const char* src, int maxW, char* out, size_t outSz) {
    if (outSz == 0) return;
    if (tft.textWidth(src) <= maxW) {
        strncpy(out, src, outSz - 1);
        out[outSz - 1] = '\0';
        return;
    }
    static const char ell[] = "..";
    int ellW = tft.textWidth(ell);
    size_t n = strlen(src);
    while (n > 0) {
        char tmp[48];
        size_t cn = (n < sizeof(tmp)) ? n : sizeof(tmp) - 1;
        memcpy(tmp, src, cn);
        tmp[cn] = '\0';
        if (tft.textWidth(tmp) + ellW <= maxW) {
            snprintf(out, outSz, "%s%s", tmp, ell);
            return;
        }
        n = cn - 1;
    }
    strncpy(out, ell, outSz - 1);
    out[outSz - 1] = '\0';
}

// ===========================================================================
// Home dashboard — full-width time/date banner + Weather | Prayer | Stocks tiles.
// Tapping a tile opens its detail page (see handleTouch). There is no status
// bar or hero any more, so Home owns the entire screen.
// ===========================================================================
static const char* const HOME_WD[7]   = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
static const char* const HOME_MON[12]  = {"Jan","Feb","Mar","Apr","May","Jun",
                                          "Jul","Aug","Sep","Oct","Nov","Dec"};
static String g_homeDateStr;   // full date string incl. "W##"; set by redraw()

// Small uppercase tracked section label (e.g. "NEXT PRAYER", "MARKETS").
static void drawHomeLabel(int x, int y, const char* text, uint16_t color) {
    tft.setFreeFont(nullptr);
    tft.setTextSize(1);
    tft.setTextColor(color, theme.bg);
    // Manual letter-spacing for a refined logotype feel.
    int cx = x;
    for (const char* p = text; *p; ++p) {
        char ch[2] = {*p, 0};
        tft.setCursor(cx, y);
        tft.print(ch);
        cx += tft.textWidth(ch) + 2;
    }
}

// Centre a short message vertically in a band (loading / no-data states).
static void drawHomeBandMsg(int bandY, int bandH, const char* msg) {
    tft.setFreeFont(nullptr);
    tft.setTextSize(1);
    tft.setTextColor(theme.textDim, theme.bg);
    tft.setCursor(HOME_PAD, bandY + bandH / 2 + 3);
    tft.print(msg);
}

// Hero — dominant clock + date (left), weather chip (right). Drawn directly on
// the background (no card) with a hairline divider below it.
static void drawHomeBanner() {
    struct tm t; bool ok = getLocalTime(&t);
    bool wifi = Network::isConnected();

    tft.fillRect(0, HOME_HERO_Y, SCREEN_W, HOME_HERO_H, theme.bg);

    // Status line — date (left) + week number, with a small WiFi dot.
    if (ok) {
        char d1[24];
        snprintf(d1, sizeof(d1), "%s %d %s",
                 HOME_WD[(t.tm_wday >= 0 && t.tm_wday < 7) ? t.tm_wday : 0],
                 t.tm_mday,
                 HOME_MON[(t.tm_mon >= 0 && t.tm_mon < 12) ? t.tm_mon : 0]);
        drawHomeLabel(HOME_PAD, HOME_HERO_Y + 16, d1, theme.textSec);
    }
    const int rightX = SCREEN_W - HOME_PAD;
    int wi = g_homeDateStr.indexOf('W');
    int dotX = rightX;
    if (wi >= 0) {
        String wk = g_homeDateStr.substring(wi);
        tft.setFreeFont(nullptr); tft.setTextSize(1);
        tft.setTextColor(theme.textDim, theme.bg);
        int wkW = tft.textWidth(wk.c_str());
        tft.setCursor(rightX - wkW, HOME_HERO_Y + 16);
        tft.print(wk);
        dotX = rightX - wkW - 10;
    }
    tft.fillCircle(dotX - 3, HOME_HERO_Y + 19, 3, wifi ? theme.green : theme.red);

    // Time — the dominant element, large and left-aligned.
    char hm[6];
    if (ok) snprintf(hm, sizeof(hm), "%02d:%02d", t.tm_hour, t.tm_min);
    else    strncpy(hm, "--:--", sizeof(hm));
    tft.setTextSize(1);
    tft.setFreeFont(&FreeSansBold24pt7b);
    tft.setTextColor(theme.textPri, theme.bg);
    tft.setCursor(HOME_PAD - 2, HOME_HERO_Y + 92);
    tft.print(hm);

    // Hairline divider below the hero.
    tft.drawFastHLine(HOME_PAD, HOME_HERO_Y + HOME_HERO_H - 1, SCREEN_W - 2 * HOME_PAD, theme.separator);

    tft.setFreeFont(nullptr);
    tft.setTextSize(1);
}

// Weather chip — top-right of the hero: icon, temperature, condition. Clears
// only its own sub-region so a partial refresh never erases the clock.
static void drawHomeWeatherTile() {
    const int chipX = HOME_WEATHER_X;
    const int chipY = HOME_HERO_Y + 26;
    const int chipW = SCREEN_W - chipX - HOME_PAD;
    tft.fillRect(chipX, chipY, chipW + HOME_PAD, 64, theme.bg);

    if (!Weather::current.valid) {
        tft.setFreeFont(nullptr); tft.setTextSize(1);
        tft.setTextColor(theme.textDim, theme.bg);
        const char* m = (Weather::current.fetchState == Weather::WEATHER_NO_KEY)
                        ? "add API key" : "weather ...";
        tft.setCursor(SCREEN_W - HOME_PAD - tft.textWidth(m), chipY + 30);
        tft.print(m);
        return;
    }
    const Weather::Data& wd = Weather::current;
    uint16_t accent = Weather::iconColor(wd.iconCode);
    const int rightX = SCREEN_W - HOME_PAD;

    // Line 1 — temperature (right-aligned) with the icon to its left.
    const char* unit = (strcmp(Settings::owmUnits, "imperial") == 0) ? "F" : "C";
    char nb[8]; snprintf(nb, sizeof(nb), "%.0f", wd.temp);
    tft.setTextSize(1);
    tft.setFreeFont(&FreeSansBold18pt7b);
    int numW = tft.textWidth(nb);
    const int degW = 14;            // degree ring + unit glyph
    const int tBase = chipY + 34;
    const int tX = rightX - numW - degW;
    tft.setTextColor(theme.textPri, theme.bg);
    tft.setCursor(tX, tBase);
    tft.print(nb);
    int dX = tX + numW;
    tft.drawCircle(dX + 4, tBase - 17, 2, theme.textSec);   // degree ring
    tft.setFreeFont(&FreeSansBold9pt7b);
    tft.setTextColor(theme.textSec, theme.bg);
    tft.setCursor(dX + 8, tBase - 3);
    tft.print(unit);

    // Weather icon to the left of the temperature, centred on the same line.
    drawWeatherIcon(tX - 18, tBase - 9, wd.iconCode);

    // Line 2 — condition label (right-aligned, accent colour).
    tft.setFreeFont(nullptr); tft.setTextSize(1);
    tft.setTextColor(accent, theme.bg);
    const char* lbl = Weather::iconLabel(wd.iconCode);
    tft.setCursor(rightX - tft.textWidth(lbl), chipY + 50);
    tft.print(lbl);
}

// Next-prayer band — label, name (amber), time, countdown + progress bar.
static void drawHomePrayerTile() {
    tft.fillRect(0, HOME_PRA_Y, SCREEN_W, HOME_PRA_H, theme.bg);
    const int top = HOME_PRA_Y + 16;

    if (!Prayer::current.valid) { drawHomeBandMsg(HOME_PRA_Y, HOME_PRA_H, "Prayers ...");
        tft.drawFastHLine(HOME_PAD, HOME_PRA_Y + HOME_PRA_H - 1, SCREEN_W - 2 * HOME_PAD, theme.separator);
        return; }

    int nextIdx = Prayer::current.nextIndex;
    int hi = (nextIdx == 1) ? 2 : nextIdx;      // skip Sunrise
    if (hi < 0 || hi >= Prayer::PRAYER_COUNT) hi = 0;
    const char* name = Prayer::current.prayers[hi].name;
    const char* tm   = Prayer::current.prayers[hi].time;

    int cd = Prayer::minutesUntilNext();
    int nowMin = -1; struct tm nt;
    if (getLocalTime(&nt)) nowMin = nt.tm_hour * 60 + nt.tm_min;
    if (nextIdx == 1 && nowMin >= 0)
        cd = Prayer::toMinutes(Prayer::current.prayers[2].time) - nowMin;

    const int rightX = SCREEN_W - HOME_PAD;

    // Label
    drawHomeLabel(HOME_PAD, top, "NEXT PRAYER", theme.accent);

    // Name (left) + time (right), on a shared baseline well below the label.
    tft.setFreeFont(&FreeSansBold12pt7b);
    tft.setTextSize(1);
    tft.setTextColor(theme.textPri, theme.bg);
    tft.setCursor(HOME_PAD - 1, top + 34);
    tft.print(name);

    tft.setTextColor(theme.textSec, theme.bg);
    tft.setCursor(rightX - tft.textWidth(tm), top + 34);
    tft.print(tm);

    // Countdown (left, below name).
    tft.setFreeFont(nullptr); tft.setTextSize(1);
    char cdb[20];
    if (cd >= 0) {
        int h = cd / 60, m = cd % 60;
        if (h > 0) snprintf(cdb, sizeof(cdb), "in %dh %02dm", h, m);
        else       snprintf(cdb, sizeof(cdb), "in %dm", m);
    } else strncpy(cdb, "--", sizeof(cdb));
    tft.setTextColor(theme.textDim, theme.bg);
    tft.setCursor(HOME_PAD, top + 52);
    tft.print(cdb);

    // Progress bar — fraction of the gap to the next prayer already elapsed.
    const int barX = HOME_PAD, barW = SCREEN_W - 2 * HOME_PAD;
    const int barY = top + 66, barH = 4;
    tft.fillRoundRect(barX, barY, barW, barH, 2, theme.separator);
    int prevIdx = (hi - 1 + Prayer::PRAYER_COUNT) % Prayer::PRAYER_COUNT;
    int prevMin = Prayer::toMinutes(Prayer::current.prayers[prevIdx].time);
    int nextMin = Prayer::toMinutes(Prayer::current.prayers[hi].time);
    float frac = 0.5f;
    if (cd >= 0 && nextMin > prevMin) {
        int span = nextMin - prevMin;
        if (span > 0) frac = 1.0f - (float)cd / (float)span;
    }
    if (frac < 0.04f) frac = 0.04f; if (frac > 1.0f) frac = 1.0f;
    tft.fillRoundRect(barX, barY, (int)(barW * frac), barH, 2, theme.accent);

    tft.drawFastHLine(HOME_PAD, HOME_PRA_Y + HOME_PRA_H - 1, SCREEN_W - 2 * HOME_PAD, theme.separator);
}

// Markets band — top-moving stock: name + price (left), daily change (right),
// with a coloured rail on the left signalling up/down.
static void drawHomeStocksTile() {
    tft.fillRect(0, HOME_STK_Y, SCREEN_W, HOME_STK_H, theme.bg);
    const int top = HOME_STK_Y + 14;

    drawHomeLabel(HOME_PAD, top, "MARKETS", theme.textDim);

    int idx = Stocks::displayQuoteIndex();
    if (idx < 0) { drawHomeBandMsg(HOME_STK_Y + 18, HOME_STK_H - 18, "Stocks ..."); return; }

    const Stocks::Quote& q = Stocks::quotes[idx];
    float mPct = Stocks::metricPct(q);
    uint16_t pc = (mPct >= 0) ? theme.green : theme.red;

    const int rowY = top + 20;

    // Coloured rail at the left signals up/down.
    tft.fillRect(HOME_PAD, rowY, 4, 34, pc);

    // Left block: name (top) + price (below).
    const int nameX = HOME_PAD + 12;
    char nb[24];
    tft.setFreeFont(&FreeSansBold9pt7b);
    tft.setTextSize(1);
    fitTextToWidth(Stocks::displayName(q), 120, nb, sizeof(nb));
    tft.setTextColor(theme.textPri, theme.bg);
    tft.setCursor(nameX, rowY + 14);
    tft.print(nb);

    char pb[16];
    if (Settings::stockEuro) snprintf(pb, sizeof(pb), "EUR %.2f", Stocks::euroPrice(q));
    else                     snprintf(pb, sizeof(pb), "$%.2f", q.price);
    tft.setFreeFont(nullptr); tft.setTextSize(1);
    tft.setTextColor(theme.textSec, theme.bg);
    tft.setCursor(nameX, rowY + 28);
    tft.print(pb);

    // Right block, line 1: daily change (1D).
    char cb[16]; snprintf(cb, sizeof(cb), "%+.2f%%", mPct);
    tft.setFreeFont(&FreeSansBold9pt7b);
    tft.setTextColor(pc, theme.bg);
    tft.setCursor(SCREEN_W - HOME_PAD - tft.textWidth(cb), rowY + 14);
    tft.print(cb);

    // Right block, line 2: 52-week change ("52W"), small secondary figure.
    bool  hp   = Stocks::hasPeak(q);
    float pPct = Stocks::peakPct(q);
    char  p52[20];
    if (hp) snprintf(p52, sizeof(p52), "52W %+.2f%%", pPct);
    else    snprintf(p52, sizeof(p52), "52W --");
    uint16_t p52col = !hp ? theme.textDim : ((pPct >= 0) ? theme.green : theme.red);
    tft.setFreeFont(nullptr); tft.setTextSize(1);
    tft.setTextColor(p52col, theme.bg);
    tft.setCursor(SCREEN_W - HOME_PAD - tft.textWidth(p52), rowY + 30);
    tft.print(p52);
}

static void drawHomePanel() {
    fillPanel(0, SCREEN_H, theme.bg);
    drawHomeBanner();
    drawHomeWeatherTile();
    drawHomePrayerTile();
    drawHomeStocksTile();
    tft.setFreeFont(nullptr);
    tft.setTextSize(1);
}

// Repaint just the rotating Stocks tile on the rotation tick (no full redraw).
static void updateHomeStockSection() {
    if (activePage != PAGE_HOME || azanScreenActive) return;
    drawHomeStocksTile();
    tft.setFreeFont(nullptr);
    tft.setTextSize(1);
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

    // ── Mosque silhouette icon (TFT primitives only, ~80×60px) ──
    // Drawn using fillCircle, fillRect, fillTriangle
    auto drawMosqueIcon = [](int cx, int cy, uint16_t col) {
        // Central dome (upper half — draw circle then mask bottom)
        tft.fillCircle(cx, cy + 10, 22, col);
        tft.fillRect(cx - 22, cy + 10, 44, 22, theme.panel);  // mask bottom half

        // Dome base
        tft.fillRect(cx - 24, cy + 8, 48, 6, col);

        // Left minaret
        tft.fillRect(cx - 32, cy - 10, 8, 24, col);
        tft.fillTriangle(cx - 36, cy - 10, cx - 24, cy - 10, cx - 28, cy - 22, col);

        // Right minaret
        tft.fillRect(cx + 24, cy - 10, 8, 24, col);
        tft.fillTriangle(cx + 20, cy - 10, cx + 36, cy - 10, cx + 28, cy - 22, col);

        // Crescent on top of dome (two overlapping circles)
        tft.fillCircle(cx + 2, cy - 12, 6, col);
        tft.fillCircle(cx - 1, cy - 12, 5, theme.panel);  // cut-out for crescent shape

        // Base platform
        tft.fillRect(cx - 36, cy + 13, 72, 4, col);
    };

    // Card background (full height)
    const int cardX = 8, cardW = SCREEN_W - 16;
    const int cardY = AZAN_START_Y;
    const int cardH = SCREEN_H - AZAN_START_Y - AZAN_BUTTON_MARGIN - 6;
    tft.fillRoundRect(cardX, cardY, cardW, cardH, 16, theme.panel);

    // X dismiss button — top-right corner
    tft.setFreeFont(nullptr);
    tft.setTextSize(1);
    tft.setTextColor(theme.textDim, theme.panel);
    tft.setCursor(SCREEN_W - 30, cardY + 6);
    tft.print("[X]");

    // Small subtitle above mosque
    tft.setTextSize(1);
    tft.setTextColor(theme.textSec, theme.panel);
    const char* subtitle = "Prayer Time";
    int stw = tft.textWidth(subtitle);
    tft.setCursor((SCREEN_W - stw) / 2, cardY + 14);
    tft.print(subtitle);

    // Mosque icon — animated glow (alternates gold/accent every 1s)
    bool glowPhase = (millis() / 1000) % 2 == 0;
    uint16_t mosqueColor = glowPhase ? theme.gold : theme.accent;
    drawMosqueIcon(SCREEN_W / 2, cardY + 60, mosqueColor);

    // Prayer name (large, centered)
    tft.setFreeFont(&FreeSansBold24pt7b);
    tft.setTextColor(theme.textPri, theme.panel);
    const char* name = Prayer::current.prayers[prayerIndex].name;
    int tw = tft.textWidth(name);
    tft.setCursor((SCREEN_W - tw) / 2, cardY + 122);
    tft.print(name);

    // Prayer time in gold
    tft.setFreeFont(&FreeSansBold18pt7b);
    tft.setTextColor(theme.gold, theme.panel);
    const char* timeStr = Prayer::current.prayers[prayerIndex].time;
    tw = tft.textWidth(timeStr);
    tft.setCursor((SCREEN_W - tw) / 2, cardY + 158);
    tft.print(timeStr);

    // Instruction text
    tft.setFreeFont(nullptr);
    tft.setTextSize(1);
    tft.setTextColor(theme.textSec, theme.panel);
    const char* instr = "Tap to mark prayed or snooze";
    tw = tft.textWidth(instr);
    tft.setCursor((SCREEN_W - tw) / 2, cardY + 172);
    tft.print(instr);

    // ── Progress bar (time remaining) ──
    const int barX = 20, barW = SCREEN_W - 40, barH = 8;
    const int barY = cardY + cardH - 20;
    // Track (background)
    tft.fillRoundRect(barX, barY, barW, barH, 4, theme.separator);
    // Fill (proportional to time remaining)
    unsigned long azanElapsed = millis() - azanScreenStart;
    if (azanScreenActive && azanElapsed < PRAYER_FULLSCREEN_MS) {
        float pct = (float)(PRAYER_FULLSCREEN_MS - azanElapsed) / (float)PRAYER_FULLSCREEN_MS;
        if (pct > 1.0f) pct = 1.0f;
        int fillW = (int)(barW * pct);
        if (fillW > 0) {
            tft.fillRoundRect(barX, barY, fillW, barH, 4, theme.gold);
        }
    }

    // ── Buttons ──
    int buttonTop = SCREEN_H - AZAN_BUTTON_MARGIN;
    int bW = SCREEN_W / 2 - 18;
    int buttonH = 48;

    tft.fillRoundRect(12, buttonTop, bW, buttonH, 10, theme.green);
    tft.setFreeFont(nullptr);
    tft.setTextSize(2);
    tft.setTextColor(theme.textPri, theme.green);
    const char* pLabel = "Prayed";
    int plw = tft.textWidth(pLabel);
    tft.setCursor(12 + (bW - plw) / 2, buttonTop + (buttonH - 16) / 2);
    tft.print(pLabel);

    int bx2 = SCREEN_W / 2 + 6;
    bool azanSnoozed = Prayer::isSnoozed(prayerIndex);
    bool azanCapHit  = (Prayer::current.snoozeCount[prayerIndex] >= PRAYER_MAX_SNOOZE_COUNT);
    uint16_t azanSnBg = (azanSnoozed || azanCapHit) ? theme.textDim : theme.gold;
    tft.fillRoundRect(bx2, buttonTop, bW, buttonH, 10, azanSnBg);
    tft.setTextColor(theme.textPri, azanSnBg);
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
// Partial Azan screen update — only redraws animated elements (mosque glow + progress bar)
// Called from updateClock() to avoid full-screen redraw flicker
// ---------------------------------------------------------------------------
static void updateAzanAnimation() {
    if (!azanScreenActive || azanPrayerIndex < 0) return;

    // Re-draw mosque icon region with current glow phase
    const int cardY = AZAN_START_Y;
    const int cardH = SCREEN_H - AZAN_START_Y - AZAN_BUTTON_MARGIN - 6;
    int cx = SCREEN_W / 2, cy = cardY + 60;

    // Clear mosque area and redraw
    tft.fillRect(cx - 40, cy - 26, 80, 46, theme.panel);
    bool glowPhase = (millis() / 1000) % 2 == 0;
    uint16_t mosqueColor = glowPhase ? theme.gold : theme.accent;
    // Inline mosque redraw (same as lambda above)
    tft.fillCircle(cx, cy + 10, 22, mosqueColor);
    tft.fillRect(cx - 22, cy + 10, 44, 22, theme.panel);
    tft.fillRect(cx - 24, cy + 8, 48, 6, mosqueColor);
    tft.fillRect(cx - 32, cy - 10, 8, 24, mosqueColor);
    tft.fillTriangle(cx - 36, cy - 10, cx - 24, cy - 10, cx - 28, cy - 22, mosqueColor);
    tft.fillRect(cx + 24, cy - 10, 8, 24, mosqueColor);
    tft.fillTriangle(cx + 20, cy - 10, cx + 36, cy - 10, cx + 28, cy - 22, mosqueColor);
    tft.fillCircle(cx + 2, cy - 12, 6, mosqueColor);
    tft.fillCircle(cx - 1, cy - 12, 5, theme.panel);
    tft.fillRect(cx - 36, cy + 13, 72, 4, mosqueColor);

    // Update progress bar
    const int barX = 20, barW = SCREEN_W - 40, barH = 8;
    const int barY = cardY + cardH - 20;
    tft.fillRoundRect(barX, barY, barW, barH, 4, theme.separator);
    unsigned long azanElapsed = millis() - azanScreenStart;
    if (azanScreenActive && azanElapsed < PRAYER_FULLSCREEN_MS) {
        float pct = (float)(PRAYER_FULLSCREEN_MS - azanElapsed) / (float)PRAYER_FULLSCREEN_MS;
        if (pct > 1.0f) pct = 1.0f;
        int fillW = (int)(barW * pct);
        if (fillW > 0) {
            tft.fillRoundRect(barX, barY, fillW, barH, 4, theme.gold);
        }
    }
}

// ---------------------------------------------------------------------------
// Forecast panel (y=124..319) — 5-day weather forecast matching hero style
// ---------------------------------------------------------------------------
static void drawForecastPanel() {
    fillPanel(LAYOUT_PANEL_Y, LAYOUT_PANEL_H, theme.bg);
    tft.setFreeFont(nullptr);

    const int cardX = 8, cardW = SCREEN_W - 16;
    const int cardY = LAYOUT_PANEL_Y + 8, cardH = LAYOUT_PANEL_H - 16;
    tft.fillRoundRect(cardX, cardY, cardW, cardH, 12, theme.panel);

    if (!Weather::forecast.valid || Weather::forecast.dayCount == 0) {
        tft.setTextSize(2);
        tft.setTextColor(theme.textDim, theme.panel);
        tft.setCursor(20, LAYOUT_PANEL_Y + 88);
        tft.print("Forecast N/A");
        return;
    }

    // Header
    tft.setTextSize(1);
    tft.setTextColor(theme.textDim, theme.panel);
    const char* hdr = "5-Day Forecast";
    int hw = tft.textWidth(hdr);
    tft.setCursor(cardX + (cardW - hw) / 2, cardY + 4);
    tft.print(hdr);

    const int START_Y  = cardY + 16;
    const char* unit   = (strcmp(Settings::owmUnits, "imperial") == 0) ? "F" : "C";

    // Count the days we'll actually draw so the rows can be stretched to fill
    // the full-height panel (the hero no longer sits above us).
    int dayRows = 0;
    for (int i = 0; i < Weather::forecast.dayCount && dayRows < FORECAST_DAYS; i++) {
        if (Weather::forecast.days[i].valid) dayRows++;
    }
    if (dayRows < 1) dayRows = 1;

    // Adaptive row height: divide the available card height among the rows,
    // clamped so rows never get cramped (32) or absurdly tall (60).
    const int availH = (cardY + cardH - 6) - START_Y;
    const int ROW_H  = max(32, min(60, availH / dayRows));
    const int vpad   = (ROW_H - 34) / 2;   // centre the 34px content block
    int shown = 0;

    for (int i = 0; i < Weather::forecast.dayCount && shown < FORECAST_DAYS; i++) {
        const Weather::DayForecast& d = Weather::forecast.days[i];
        if (!d.valid) continue;

        int ry = START_Y + shown * ROW_H;   // slot top
        int cy = ry + vpad;                  // centred content top
        uint16_t rowBg = theme.panel;
        uint16_t accent = Weather::iconColor(d.iconCode);

        // Left edge bar (5px) coloured by weather condition — fills the slot
        tft.fillRect(cardX + 6, ry + 2, 5, ROW_H - 6, accent);

        // Weather icon (24×24) — centered vertically in the row
        int iconCx = cardX + 30;
        int iconCy = ry + ROW_H / 2 - 1;
        drawWeatherIcon(iconCx, iconCy, d.iconCode);

        // Day name (e.g. "Mon")
        tft.setFreeFont(nullptr);
        tft.setTextSize(2);
        tft.setTextColor(theme.textPri, rowBg);
        tft.setCursor(cardX + 50, cy + 2);
        tft.print(d.dayName);

        // Condition label below day name
        tft.setTextSize(1);
        tft.setTextColor(theme.textSec, rowBg);
        tft.setCursor(cardX + 50, cy + 20);
        tft.print(Weather::iconLabel(d.iconCode));

        // Hi / Lo temps — right-aligned
        char hiBuf[10], loBuf[10];
        snprintf(hiBuf, sizeof(hiBuf), "%.0f%s", d.tempHi, unit);
        snprintf(loBuf, sizeof(loBuf), "%.0f%s", d.tempLo, unit);

        // Hi temp in accent colour (matches hero style)
        tft.setTextSize(2);
        tft.setTextColor(accent, rowBg);
        int hiW = tft.textWidth(hiBuf);
        tft.setCursor(cardX + cardW - hiW - 12, cy + 2);
        tft.print(hiBuf);

        // Lo temp in dim colour, smaller
        tft.setTextSize(1);
        tft.setTextColor(theme.textDim, rowBg);
        int loW = tft.textWidth(loBuf);
        tft.setCursor(cardX + cardW - loW - 12, cy + 22);
        tft.print(loBuf);

        // Separator line between rows
        if (shown < dayRows - 1) {
            tft.drawFastHLine(cardX + 16, ry + ROW_H - 2, cardW - 32, theme.separator);
        }
        shown++;
    }

    drawPanelChevrons();

    // Reset text state
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

    const int START_Y = LAYOUT_PANEL_Y + 14;
    int order[MAX_STOCKS];
    int orderCount = 0;

    for (int i = 0; i < MAX_STOCKS; i++) {
        if (strlen(Settings::stockSymbols[i]) == 0) continue;
        order[orderCount++] = i;
    }

    // Stable insertion sort: valid quotes first, then by |active metric| descending.
    for (int i = 1; i < orderCount; i++) {
        int key = order[i];
        const Stocks::Quote& kq = Stocks::quotes[key];
        float keyAbs = fabsf(Stocks::metricPct(kq));
        float keyRank = kq.valid ? keyAbs : -1.0f;
        int j = i - 1;

        while (j >= 0) {
            const Stocks::Quote& jq = Stocks::quotes[order[j]];
            float jAbs = fabsf(Stocks::metricPct(jq));
            float jRank = jq.valid ? jAbs : -1.0f;
            if (jRank >= keyRank) break;
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }

    // Adaptive row height: stretch up to 5 rows across the full-height panel
    // (the hero no longer sits above us), clamped so rows stay readable.
    const int nRows      = max(1, min(orderCount, 5));
    const int cardBottom = LAYOUT_PANEL_Y + LAYOUT_PANEL_H - 10;
    const int ROW_H      = max(32, min(64, (cardBottom - START_Y) / nRows));
    const int vpad       = (ROW_H - 32) / 2;   // centre the 32px content block

    int shown = 0;

    for (int oi = 0; oi < orderCount && shown < 5; oi++) {
        int i = order[oi];
        const Stocks::Quote& q = Stocks::quotes[i];

        int ry = START_Y + shown * ROW_H;   // slot top
        int cy = ry + vpad;                 // centred content top
        uint16_t rowBg = theme.panel;   // uniform bg — no zebra
        uint16_t edgeColor = theme.separator;

        if (q.valid) {
            edgeColor = (Stocks::metricPct(q) >= 0) ? theme.green : theme.red;
        }

        tft.fillRect(14, ry, SCREEN_W - 28, ROW_H - 2, rowBg);
        tft.fillRect(14, ry, 5, ROW_H - 2, edgeColor);

        if (!q.valid) {
            bool badSym = Stocks::needsAttention(i);
            tft.setTextSize(2);
            // Symbol in the alert colour when it needs fixing, dim otherwise.
            tft.setTextColor(badSym ? theme.gold : theme.textDim, rowBg);
            tft.setCursor(26, cy + 8);
            tft.print(Settings::stockSymbols[i]);

            if (badSym) {
                // Tell the user this symbol is the problem, not the network.
                tft.setTextSize(1);
                tft.setTextColor(theme.red, rowBg);
                const char* msg = "CHECK SYMBOL";
                int mw = tft.textWidth(msg);
                tft.setCursor(SCREEN_W - mw - 24, cy + 12);
                tft.print(msg);
            } else {
                tft.setTextColor(theme.textDim, rowBg);
                tft.setCursor(154, cy + 8);
                tft.print("...");
            }
        } else {
            // Row 1, right: daily change ("1D") — primary, size-2 figure with a
            // small dim label. Measured first so the name gets the rest of the
            // row width. Numbers right-align at SCREEN_W-24 on both rows; the
            // alert dot lives in the margin to the right of them.
            float dPct = Stocks::metricPct(q);
            uint16_t dCol = (dPct >= 0) ? theme.green : theme.red;
            char dBuf[12];
            snprintf(dBuf, sizeof(dBuf), "%+.2f%%", dPct);
            tft.setTextSize(2);
            int dW    = tft.textWidth(dBuf);
            int dNumX = SCREEN_W - 24 - dW;
            tft.setTextSize(1);
            int d1X = dNumX - tft.textWidth("1D") - 5;

            // Name (left): prefer size 2; shrink to size 1, then truncate.
            const char* nm       = Stocks::displayName(q);
            int         nameX    = 26;
            int         nameMaxW = d1X - nameX - 8;
            char        nameBuf[44];
            int         nameY    = cy + 2;
            tft.setTextSize(2);
            if (tft.textWidth(nm) > nameMaxW) {
                tft.setTextSize(1);   // shrink before resorting to truncation
                nameY = cy + 6;
            }
            fitTextToWidth(nm, nameMaxW, nameBuf, sizeof(nameBuf));
            tft.setTextColor(theme.textPri, rowBg);
            tft.setCursor(nameX, nameY);
            tft.print(nameBuf);

            // "1D" label + daily figure
            tft.setTextSize(1);
            tft.setTextColor(theme.textDim, rowBg);
            tft.setCursor(d1X, cy + 7);
            tft.print("1D");
            tft.setTextSize(2);
            tft.setTextColor(dCol, rowBg);
            tft.setCursor(dNumX, cy + 2);
            tft.print(dBuf);

            // Row 2, left: price — secondary context.
            char priceBuf[14];
            if (Settings::stockEuro) {
                snprintf(priceBuf, sizeof(priceBuf), "EUR %.2f", Stocks::euroPrice(q));
            } else {
                snprintf(priceBuf, sizeof(priceBuf), "$%.2f", q.price);
            }
            tft.setTextSize(1);
            tft.setTextColor(theme.textSec, rowBg);
            tft.setCursor(26, cy + 21);
            tft.print(priceBuf);

            // Row 2, right: 52-week change ("52W"), small. "--" when no
            // 52-week high was reported for this symbol.
            bool  hp   = Stocks::hasPeak(q);
            float pPct = Stocks::peakPct(q);
            char  pBuf[12];
            if (hp) snprintf(pBuf, sizeof(pBuf), "%+.2f%%", pPct);
            else    snprintf(pBuf, sizeof(pBuf), "--");
            uint16_t pCol = !hp ? theme.textDim : ((pPct >= 0) ? theme.green : theme.red);
            tft.setTextSize(1);
            int pW    = tft.textWidth(pBuf);
            int pNumX = SCREEN_W - 24 - pW;
            int p1X   = pNumX - tft.textWidth("52W") - 5;
            tft.setTextColor(theme.textDim, rowBg);
            tft.setCursor(p1X, cy + 21);
            tft.print("52W");
            tft.setTextColor(pCol, rowBg);
            tft.setCursor(pNumX, cy + 21);
            tft.print(pBuf);

            // Alert dot — in the right margin, clear of both figures.
            if (q.alertTriggered) {
                tft.fillCircle(SCREEN_W - 12, cy + 16, 3, theme.gold);
            }
        }

        // Separator
        if (shown < orderCount - 1) {
            tft.drawFastHLine(20, ry + ROW_H - 1, SCREEN_W - 40, theme.separator);
        }
        shown++;
    }

    drawPanelChevrons();

    if (shown == 0) {
        tft.setTextSize(2);
        tft.setTextColor(theme.textDim, theme.panel);
        tft.setCursor(20, LAYOUT_PANEL_Y + 88);
        tft.print("Fetching...");
    }
}

// ---------------------------------------------------------------------------
// Break Reminder panel (PAGE_BREAK)
// Shows a friendly reminder to take a break, with elapsed time and dismiss.
// ---------------------------------------------------------------------------
static void drawBreakPanel() {
    fillPanel(LAYOUT_PANEL_Y, LAYOUT_PANEL_H, theme.bg);
    tft.setFreeFont(nullptr);

    const int cardX = 8, cardW = SCREEN_W - 16;
    const int cardY = LAYOUT_PANEL_Y + 8, cardH = LAYOUT_PANEL_H - 16;
    tft.fillRoundRect(cardX, cardY, cardW, cardH, 12, theme.panel);

    int yPos = cardY + 12;

    // Title
    tft.setFreeFont(&FreeSansBold9pt7b);
    tft.setTextSize(1);
    tft.setTextColor(theme.accent, theme.panel);
    const char* title = "Time for a Break!";
    int tw = tft.textWidth(title);
    tft.setCursor(cardX + (cardW - tw) / 2, yPos + 14);
    tft.print(title);
    yPos += 28;

    // Horizontal accent line
    tft.drawFastHLine(cardX + 20, yPos, cardW - 40, theme.accent);
    yPos += 10;

    // Body text
    tft.setFreeFont(nullptr);
    tft.setTextSize(1);
    tft.setTextColor(theme.textSec, theme.panel);
    const char* line1 = "You've been at your desk for";
    tw = tft.textWidth(line1);
    tft.setCursor(cardX + (cardW - tw) / 2, yPos);
    tft.print(line1);
    yPos += 14;

    // Show how long since last break
    unsigned long elapsedMs = millis() - breakLastNotify;
    int elapsedMin = (int)(elapsedMs / 60000UL);
    if (elapsedMin < 1) elapsedMin = Settings::breakReminderInterval;

    char timeBuf[24];
    if (elapsedMin >= 60) {
        int hrs = elapsedMin / 60;
        int mins = elapsedMin % 60;
        if (mins > 0)
            snprintf(timeBuf, sizeof(timeBuf), "%dh %dm", hrs, mins);
        else
            snprintf(timeBuf, sizeof(timeBuf), "%d hour%s", hrs, hrs > 1 ? "s" : "");
    } else {
        snprintf(timeBuf, sizeof(timeBuf), "%d min", elapsedMin);
    }

    tft.setTextSize(2);
    tft.setTextColor(theme.gold, theme.panel);
    tw = tft.textWidth(timeBuf);
    tft.setCursor(cardX + (cardW - tw) / 2, yPos + 4);
    tft.print(timeBuf);
    yPos += 30;

    tft.setTextSize(1);
    tft.setTextColor(theme.textSec, theme.panel);
    const char* line2 = "Stand up, stretch, and rest";
    tw = tft.textWidth(line2);
    tft.setCursor(cardX + (cardW - tw) / 2, yPos);
    tft.print(line2);
    yPos += 12;
    const char* line3 = "your eyes for a moment.";
    tw = tft.textWidth(line3);
    tft.setCursor(cardX + (cardW - tw) / 2, yPos);
    tft.print(line3);
    yPos += 20;

    // "Dismiss" button
    const int btnW = 120, btnH = 32;
    const int btnX = cardX + (cardW - btnW) / 2;
    const int btnY = cardY + cardH - btnH - 12;
    tft.fillRoundRect(btnX, btnY, btnW, btnH, 8, theme.green);
    tft.setTextSize(2);
    tft.setTextColor(theme.textPri, theme.green);
    const char* btnLabel = "OK";
    tw = tft.textWidth(btnLabel);
    tft.setCursor(btnX + (btnW - tw) / 2, btnY + 8);
    tft.print(btnLabel);

    // Interval info at bottom
    tft.setTextSize(1);
    tft.setTextColor(theme.textDim, theme.panel);
    char intBuf[32];
    snprintf(intBuf, sizeof(intBuf), "Reminder every %d min", Settings::breakReminderInterval);
    tw = tft.textWidth(intBuf);
    tft.setCursor(cardX + (cardW - tw) / 2, btnY - 14);
    tft.print(intBuf);
}

// Helper: dismiss break reminder and reset timer
static void dismissBreakReminder() {
    breakScreenActive = false;
    breakLastNotify = millis();
    if (activePage == PAGE_BREAK) {
        activePage = PAGE_HOME;
    }
    needsRedraw = true;
}

// Check if a break reminder should fire
static bool shouldFireBreakReminder() {
    if (!Settings::breakReminderEnabled) return false;
    if (breakScreenActive) return false;
    unsigned long intervalMs = (unsigned long)Settings::breakReminderInterval * 60UL * 1000UL;
    return (millis() - breakLastNotify) >= intervalMs;
}

// Fire break reminder: show banner and switch to break page
static void fireBreakReminder() {
    breakScreenActive = true;
    breakScreenStart  = millis();
    wake();
    showBanner("Take a break!", BREAK_REMINDER_BANNER_MS, BANNER_GOLD);
    pauseCarousel();
    activePage = PAGE_BREAK;
    needsRedraw = true;
    Serial.println("[Break] Reminder fired.");
}

// Auto-dismiss break screen after expiry (rollover-safe)
static void updateBreakState() {
    if (breakScreenActive && (millis() - breakScreenStart) >= BREAK_REMINDER_SCREEN_MS) {
        dismissBreakReminder();
    }
}

// ---------------------------------------------------------------------------
// Notification banner (temporary overlay at top of panel)
// Redesigned: taller (36px), slide-in animation, color-coded by event type
// ---------------------------------------------------------------------------
static unsigned long bannerAnimStart = 0;   // also serves as the banner-start reference
static unsigned long bannerDuration  = 0;    // 0 = no banner active
static char bannerText[64] = {};
static BannerStyle bannerStyle = BANNER_ACCENT;

static uint16_t bannerBgColor() {
    switch (bannerStyle) {
        case BANNER_GOLD: return theme.gold;
        case BANNER_RED:  return theme.red;
        default:          return theme.accent;
    }
}

static void drawBannerIfActive() {
    if (azanScreenActive) return;
    // Rollover-safe expiry check (subtraction wraps correctly at the millis() overflow).
    if (bannerDuration == 0 || (millis() - bannerAnimStart) >= bannerDuration) return;

    uint16_t bg = bannerBgColor();

    // Slide-in animation: over ~180ms (6 frames × 30ms), banner grows from 0 → BANNER_H
    int h = BANNER_H;
    unsigned long elapsed = millis() - bannerAnimStart;
    if (elapsed < 180) {
        h = (int)(BANNER_H * elapsed / 180);
        if (h < 4) h = 4;
    }

    tft.fillRect(0, LAYOUT_PANEL_Y, SCREEN_W, h, bg);

    // 2px accent border at top edge (toast feel)
    uint16_t borderColor = (bannerStyle == BANNER_ACCENT) ? theme.gold : theme.textPri;
    tft.drawFastHLine(0, LAYOUT_PANEL_Y, SCREEN_W, borderColor);
    tft.drawFastHLine(0, LAYOUT_PANEL_Y + 1, SCREEN_W, borderColor);

    // Only draw text once animation is mostly done
    if (h >= BANNER_H - 4) {
        tft.setFreeFont(&FreeSansBold9pt7b);
        tft.setTextSize(1);
        tft.setTextColor(theme.textPri, bg);

        int tw = tft.textWidth(bannerText);
        int cursorY = LAYOUT_PANEL_Y + BANNER_H - 8;
        if (tw > SCREEN_W - 16) {
            // Fall back to default font for very long text
            tft.setFreeFont(nullptr);
            tft.setTextSize(1);
            tw = tft.textWidth(bannerText);
            cursorY = LAYOUT_PANEL_Y + BANNER_H / 2 + 2;
        }

        tft.setTextWrap(false);
        tft.setCursor((SCREEN_W - tw) / 2, cursorY);
        tft.print(bannerText);
        tft.setTextWrap(true);
    }

    // Reset text state
    tft.setFreeFont(nullptr);
    tft.setTextSize(1);
}

static void showBanner(const char* text, uint32_t durationMs = BANNER_DEFAULT_MS,
                       BannerStyle style = BANNER_ACCENT) {
    strncpy(bannerText, text, sizeof(bannerText) - 1);
    bannerText[sizeof(bannerText) - 1] = '\0';
    bannerAnimStart = millis();
    bannerDuration  = durationMs;
    bannerStyle = style;
    drawBannerIfActive();  // paint immediately, no tick delay
}

// ---------------------------------------------------------------------------
// Page transition animation — column-wipe effect
// ---------------------------------------------------------------------------
static void playPanelTransition(int direction) {
    const int STEPS = 4;
    const int STEP_W = SCREEN_W / STEPS;
    const int DELAY_MS = 25;

    for (int s = 0; s < STEPS; s++) {
        int x;
        if (direction > 0) {
            // Wipe left-to-right
            x = s * STEP_W;
        } else {
            // Wipe right-to-left
            x = SCREEN_W - (s + 1) * STEP_W;
        }
        tft.fillRect(x, LAYOUT_PANEL_Y, STEP_W, LAYOUT_PANEL_H, theme.bg);
        delay(DELAY_MS);
    }
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
    updatePanelLayout();

    // Stash the full date string so the Home banner can show the week number.
    g_homeDateStr = dateStr;

    switch (activePage) {
        case PAGE_HOME:     drawHomePanel();     break;
        case PAGE_PRAYER:   drawPrayerPanel();   break;
        case PAGE_FORECAST: drawForecastPanel(); break;
        case PAGE_STOCKS:   drawStocksPanel();   break;
        case PAGE_BREAK:    drawBreakPanel();    break;
    }

    drawBannerIfActive();
    needsRedraw = false;
}

// ---------------------------------------------------------------------------
// Partial updates
// ---------------------------------------------------------------------------
static void updateClock(const struct tm& t) {
    (void)t;
    if (azanScreenActive) {
        // Only update animated elements, not the full Azan screen
        updateAzanAnimation();
        return;
    }
    // The clock lives in the Home banner. Refresh it plus the prayer tile so the
    // countdown stays current — both cheap, no full-screen flicker.
    if (activePage == PAGE_HOME) {
        drawHomeBanner();
        drawHomePrayerTile();
        tft.setFreeFont(nullptr);
        tft.setTextSize(1);
    }
}

static void updateWeather() {
    if (azanScreenActive) return;
    if (activePage == PAGE_HOME) {
        drawHomeWeatherTile();
        tft.setFreeFont(nullptr);
        tft.setTextSize(1);
    } else if (activePage == PAGE_FORECAST) {
        // Refresh the full-screen forecast panel.
        updatePanelLayout();
        drawForecastPanel();
    }
}

static void updatePanel() {
    if (azanScreenActive) {
        drawAzanScreen();
        return;
    }
    coerceAllowedActivePage();
    updatePanelLayout();
    switch (activePage) {
        case PAGE_HOME:     drawHomePanel();     break;
        case PAGE_PRAYER:   drawPrayerPanel();   break;
        case PAGE_FORECAST: drawForecastPanel(); break;
        case PAGE_STOCKS:   drawStocksPanel();   break;
        case PAGE_BREAK:    drawBreakPanel();    break;
    }
}

// ---------------------------------------------------------------------------
// Splash / boot screen
// ---------------------------------------------------------------------------
// Boot-progress vertical anchors (kept in one place so showSplash reserves the
// exact band that showSplashStatus repaints).
#define SPLASH_DOTS_Y    238
#define SPLASH_STATUS_Y  258

static void showSplash() {
    tft.fillScreen(theme.bg);
    const int cx = SCREEN_W / 2;

    // ── Logo badge: a clock face inside a rounded tile, echoing the home tiles ──
    const int badge = 78;
    const int bx = cx - badge / 2, by = 48;
    tft.fillRoundRect(bx, by, badge, badge, 16, theme.panel);
    tft.drawRoundRect(bx, by, badge, badge, 16, theme.accent);
    tft.drawRoundRect(bx + 1, by + 1, badge - 2, badge - 2, 15, theme.accent);

    const int fcx = cx, fcy = by + badge / 2, fr = 27;
    tft.fillCircle(fcx, fcy, fr, theme.bg);
    tft.drawCircle(fcx, fcy, fr, theme.gold);
    tft.drawCircle(fcx, fcy, fr - 1, theme.gold);

    // Hour ticks at 12 / 3 / 6 / 9
    tft.fillCircle(fcx, fcy - (fr - 6), 2, theme.textDim);
    tft.fillCircle(fcx, fcy + (fr - 6), 2, theme.textDim);
    tft.fillCircle(fcx - (fr - 6), fcy, 2, theme.textDim);
    tft.fillCircle(fcx + (fr - 6), fcy, 2, theme.textDim);

    // Hands set to a pleasant ~10:08, drawn doubled for weight
    tft.drawLine(fcx, fcy, fcx + 14, fcy - 8, theme.textPri);   // minute
    tft.drawLine(fcx, fcy, fcx + 15, fcy - 9, theme.textPri);
    tft.drawLine(fcx, fcy, fcx - 11, fcy - 7, theme.textPri);   // hour
    tft.drawLine(fcx, fcy, fcx - 11, fcy - 6, theme.textPri);
    tft.fillCircle(fcx, fcy, 3, theme.gold);

    // ── Wordmark ──
    tft.setTextSize(1);
    tft.setFreeFont(&FreeSansBold18pt7b);
    tft.setTextColor(theme.accent, theme.bg);
    String title = "DeskNexus";
    int tw = tft.textWidth(title);
    tft.setCursor(cx - tw / 2, 172);
    tft.print(title);

    // ── Subtitle (letter-spaced for a logotype feel) ──
    tft.setFreeFont(nullptr);
    tft.setTextSize(1);
    tft.setTextColor(theme.textSec, theme.bg);
    const char* sub = "S M A R T   D E S K   C L O C K";
    tft.setCursor(cx - tft.textWidth(sub) / 2, 188);
    tft.print(sub);

    // Gold accent rule
    tft.drawFastHLine(cx - 58, 204, 116, theme.gold);

    // Version, bottom
    tft.setTextColor(theme.textDim, theme.bg);
    const char* ver = "v1.0";
    tft.setCursor(cx - tft.textWidth(ver) / 2, 302);
    tft.print(ver);
}

static void showSplashStatus(const char* msg) {
    const int cx = SCREEN_W / 2;

    // Progress dots — the active one advances on every boot step.
    static int step = 0;
    const int dots = 4, gap = 16, r = 3;
    int dx = cx - ((dots - 1) * gap) / 2;
    tft.fillRect(0, SPLASH_DOTS_Y - r - 1, SCREEN_W, (r + 1) * 2 + 2, theme.bg);
    for (int i = 0; i < dots; i++) {
        int ccx = dx + i * gap;
        if (i == (step % dots)) {
            tft.fillCircle(ccx, SPLASH_DOTS_Y, r, theme.accent);
            tft.drawCircle(ccx, SPLASH_DOTS_Y, r + 1, theme.gold);
        } else {
            tft.fillCircle(ccx, SPLASH_DOTS_Y, r - 1, theme.textDim);
        }
    }
    step++;

    // Status message
    tft.fillRect(0, SPLASH_STATUS_Y, SCREEN_W, 22, theme.bg);
    tft.setFreeFont(nullptr);
    tft.setTextSize(2);
    tft.setTextColor(theme.textSec, theme.bg);
    tft.setCursor(cx - tft.textWidth(msg) / 2, SPLASH_STATUS_Y + 4);
    tft.print(msg);
}

// ---------------------------------------------------------------------------
// QR Code renderer (uses ricmoo/QRCode library)
// ---------------------------------------------------------------------------
static void drawQRCode(int cx, int cy, const char* data, int moduleSize = SETUP_QR_MODULE_PX) {
    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(SETUP_QR_VERSION)];
    qrcode_initText(&qrcode, qrcodeData, SETUP_QR_VERSION, ECC_LOW, data);

    int qrSize = qrcode.size * moduleSize;
    int startX = cx - qrSize / 2;
    int startY = cy - qrSize / 2;

    // White background with 4px quiet zone
    tft.fillRect(startX - 4, startY - 4, qrSize + 8, qrSize + 8, TFT_WHITE);

    // Draw modules
    for (uint8_t y = 0; y < qrcode.size; y++) {
        for (uint8_t x = 0; x < qrcode.size; x++) {
            if (qrcode_getModule(&qrcode, x, y)) {
                tft.fillRect(startX + x * moduleSize, startY + y * moduleSize,
                             moduleSize, moduleSize, TFT_BLACK);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// AP Setup Screen — shown on TFT when in AP/captive-portal mode
// ---------------------------------------------------------------------------
static void showAPSetupScreen() {
    // Always use dark theme for setup (consistent, no prayer data yet)
    Theme setupTheme = THEME_DARK;
    tft.fillScreen(setupTheme.bg);

    // Title
    tft.setFreeFont(&FreeSansBold9pt7b);
    tft.setTextSize(1);
    tft.setTextColor(setupTheme.accent, setupTheme.bg);
    const char* title = "DeskNexus Setup";
    int tw = tft.textWidth(title);
    tft.setCursor((SCREEN_W - tw) / 2, 22);
    tft.print(title);

    // Separator
    tft.drawFastHLine(30, 30, SCREEN_W - 60, setupTheme.separator);

    // WiFi QR code — centered (Version 4 = 33 modules, 3px each = 99px)
    char qrStr[96];
    snprintf(qrStr, sizeof(qrStr), "WIFI:T:WPA;S:%s;P:%s;;",
             AP_SSID, Network::getApPassword());
    drawQRCode(SCREEN_W / 2, 96, qrStr, SETUP_QR_MODULE_PX);

    // Instructions below QR
    tft.setFreeFont(nullptr);
    tft.setTextSize(1);
    int infoY = 156;

    tft.setTextColor(setupTheme.textSec, setupTheme.bg);
    const char* line1 = "Scan QR or join WiFi:";
    tw = tft.textWidth(line1);
    tft.setCursor((SCREEN_W - tw) / 2, infoY);
    tft.print(line1);
    infoY += 14;

    // SSID (bold/accent)
    tft.setTextSize(2);
    tft.setTextColor(setupTheme.textPri, setupTheme.bg);
    tw = tft.textWidth(AP_SSID);
    tft.setCursor((SCREEN_W - tw) / 2, infoY);
    tft.print(AP_SSID);
    infoY += 22;

    // Password
    tft.setTextSize(1);
    tft.setTextColor(setupTheme.textSec, setupTheme.bg);
    char passLine[48];
    snprintf(passLine, sizeof(passLine), "Password: %s", Network::getApPassword());
    tw = tft.textWidth(passLine);
    tft.setCursor((SCREEN_W - tw) / 2, infoY);
    tft.print(passLine);
    infoY += 16;

    tft.setTextColor(setupTheme.gold, setupTheme.bg);
    const char* autoOpen = "Portal opens automatically";
    tw = tft.textWidth(autoOpen);
    tft.setCursor((SCREEN_W - tw) / 2, infoY);
    tft.print(autoOpen);

    // Bottom: IP address
    tft.drawFastHLine(30, 280, SCREEN_W - 60, setupTheme.separator);
    tft.setTextSize(2);
    tft.setTextColor(setupTheme.accent, setupTheme.bg);
    const char* ip = "192.168.4.1";
    tw = tft.textWidth(ip);
    tft.setCursor((SCREEN_W - tw) / 2, 290);
    tft.print(ip);

    // Waiting indicator
    tft.setTextSize(1);
    tft.setTextColor(setupTheme.textDim, setupTheme.bg);
    const char* wait = "Waiting for connection...";
    tw = tft.textWidth(wait);
    tft.setCursor((SCREEN_W - tw) / 2, 310);
    tft.print(wait);
}

// Update the "waiting" indicator with client count (call periodically in AP loop)
static void updateAPSetupWaiting() {
    Theme setupTheme = THEME_DARK;
    int clients = WiFi.softAPgetStationNum();
    tft.fillRect(0, 306, SCREEN_W, 14, setupTheme.bg);
    tft.setFreeFont(nullptr);
    tft.setTextSize(1);
    tft.setTextColor(clients > 0 ? setupTheme.gold : setupTheme.textDim, setupTheme.bg);

    char msg[48];
    if (clients > 0) {
        snprintf(msg, sizeof(msg), "%d device(s) connected", clients);
    } else {
        snprintf(msg, sizeof(msg), "Waiting for connection...");
    }
    int tw = tft.textWidth(msg);
    tft.setCursor((SCREEN_W - tw) / 2, 310);
    tft.print(msg);
}

// ---------------------------------------------------------------------------
// Connecting Screen — shown during WiFi STA connection attempts
// ---------------------------------------------------------------------------
static void showConnectingScreen(const char* ssid) {
    Theme setupTheme = THEME_DARK;
    tft.fillScreen(setupTheme.bg);

    // Card background
    tft.fillRoundRect(20, 80, SCREEN_W - 40, 160, 14, setupTheme.panel);

    // Title
    tft.setFreeFont(&FreeSansBold9pt7b);
    tft.setTextSize(1);
    tft.setTextColor(setupTheme.textPri, setupTheme.panel);
    const char* title = "Connecting...";
    int tw = tft.textWidth(title);
    tft.setCursor((SCREEN_W - tw) / 2, 114);
    tft.print(title);

    // SSID
    tft.setFreeFont(nullptr);
    tft.setTextSize(2);
    tft.setTextColor(setupTheme.accent, setupTheme.panel);
    // Truncate long SSIDs
    char truncSSID[20];
    strncpy(truncSSID, ssid, sizeof(truncSSID) - 1);
    truncSSID[sizeof(truncSSID) - 1] = '\0';
    tw = tft.textWidth(truncSSID);
    tft.setCursor((SCREEN_W - tw) / 2, 135);
    tft.print(truncSSID);

    // Progress bar outline
    int barX = 40, barY = 170, barW = SCREEN_W - 80, barH = 12;
    tft.drawRoundRect(barX, barY, barW, barH, 4, setupTheme.separator);
}

static void showConnectionResult(bool success, const char* detail) {
    Theme setupTheme = THEME_DARK;
    tft.fillScreen(setupTheme.bg);

    // Card
    tft.fillRoundRect(20, 70, SCREEN_W - 40, 180, 14, setupTheme.panel);

    if (success) {
        // Green checkmark circle
        tft.fillCircle(SCREEN_W / 2, 120, 24, setupTheme.green);
        // Checkmark lines (white)
        tft.drawLine(SCREEN_W/2 - 10, 120, SCREEN_W/2 - 2, 130, TFT_WHITE);
        tft.drawLine(SCREEN_W/2 - 2, 130, SCREEN_W/2 + 12, 112, TFT_WHITE);
        tft.drawLine(SCREEN_W/2 - 9, 120, SCREEN_W/2 - 1, 130, TFT_WHITE);
        tft.drawLine(SCREEN_W/2 - 1, 130, SCREEN_W/2 + 11, 112, TFT_WHITE);

        tft.setFreeFont(&FreeSansBold9pt7b);
        tft.setTextSize(1);
        tft.setTextColor(setupTheme.green, setupTheme.panel);
        const char* ok = "Connected!";
        int tw = tft.textWidth(ok);
        tft.setCursor((SCREEN_W - tw) / 2, 170);
        tft.print(ok);

        // IP address
        tft.setFreeFont(nullptr);
        tft.setTextSize(2);
        tft.setTextColor(setupTheme.textPri, setupTheme.panel);
        int tw2 = tft.textWidth(detail);
        tft.setCursor((SCREEN_W - tw2) / 2, 190);
        tft.print(detail);
    } else {
        // Red X circle
        tft.fillCircle(SCREEN_W / 2, 120, 24, setupTheme.red);
        tft.drawLine(SCREEN_W/2 - 10, 110, SCREEN_W/2 + 10, 130, TFT_WHITE);
        tft.drawLine(SCREEN_W/2 + 10, 110, SCREEN_W/2 - 10, 130, TFT_WHITE);
        tft.drawLine(SCREEN_W/2 - 9, 110, SCREEN_W/2 + 11, 130, TFT_WHITE);
        tft.drawLine(SCREEN_W/2 + 11, 110, SCREEN_W/2 - 9, 130, TFT_WHITE);

        tft.setFreeFont(&FreeSansBold9pt7b);
        tft.setTextSize(1);
        tft.setTextColor(setupTheme.red, setupTheme.panel);
        const char* fail = "Connection Failed";
        int tw = tft.textWidth(fail);
        tft.setCursor((SCREEN_W - tw) / 2, 170);
        tft.print(fail);

        tft.setFreeFont(nullptr);
        tft.setTextSize(1);
        tft.setTextColor(setupTheme.textSec, setupTheme.panel);
        const char* retry = "Starting setup mode...";
        int tw2 = tft.textWidth(retry);
        tft.setCursor((SCREEN_W - tw2) / 2, 195);
        tft.print(retry);
    }
}

// ---------------------------------------------------------------------------
// Setup wizard state machine
// ---------------------------------------------------------------------------
enum WizardState {
    WIZ_WELCOME,
    WIZ_WIFI_QR,
    WIZ_WAITING,
    WIZ_CONNECTING,
    WIZ_SUCCESS,
    WIZ_CONFIG_HINT,
    WIZ_DONE
};

static WizardState wizardState = WIZ_WELCOME;

static void drawWizardWelcome() {
    Theme setupTheme = THEME_DARK;
    tft.fillScreen(setupTheme.bg);

    // Geometric "desk" icon — simple monitor shape
    int cx = SCREEN_W / 2;
    int iy = 80;
    tft.drawRoundRect(cx - 36, iy, 72, 48, 6, setupTheme.accent);    // screen outline
    tft.drawFastHLine(cx - 8, iy + 48, 16, setupTheme.accent);       // stand neck
    tft.drawFastVLine(cx, iy + 48, 12, setupTheme.accent);            // stand pole
    tft.drawFastHLine(cx - 16, iy + 60, 32, setupTheme.accent);       // stand base
    // Small dot "power" light
    tft.fillCircle(cx, iy + 42, 2, setupTheme.green);

    // Title
    tft.setFreeFont(&FreeSansBold18pt7b);
    tft.setTextSize(1);
    tft.setTextColor(setupTheme.textPri, setupTheme.bg);
    const char* title = "Welcome";
    int tw = tft.textWidth(title);
    tft.setCursor((SCREEN_W - tw) / 2, 175);
    tft.print(title);

    tft.setFreeFont(&FreeSansBold9pt7b);
    tft.setTextColor(setupTheme.accent, setupTheme.bg);
    const char* sub = "DeskNexus";
    tw = tft.textWidth(sub);
    tft.setCursor((SCREEN_W - tw) / 2, 200);
    tft.print(sub);

    // Instruction
    tft.setFreeFont(nullptr);
    tft.setTextSize(1);
    tft.setTextColor(setupTheme.textDim, setupTheme.bg);
    const char* hint = "Tap screen to begin setup";
    tw = tft.textWidth(hint);
    tft.setCursor((SCREEN_W - tw) / 2, 240);
    tft.print(hint);

    // Version
    tft.setTextColor(setupTheme.textDim, setupTheme.bg);
    const char* ver = "v1.0";
    tw = tft.textWidth(ver);
    tft.setCursor((SCREEN_W - tw) / 2, 310);
    tft.print(ver);
}

static void drawWizardConfigHint() {
    Theme setupTheme = THEME_DARK;
    tft.fillScreen(setupTheme.bg);

    // Card
    tft.fillRoundRect(16, 40, SCREEN_W - 32, 240, 14, setupTheme.panel);

    tft.setFreeFont(&FreeSansBold9pt7b);
    tft.setTextSize(1);
    tft.setTextColor(setupTheme.textPri, setupTheme.panel);
    const char* title = "Customize Your Desk";
    int tw = tft.textWidth(title);
    tft.setCursor((SCREEN_W - tw) / 2, 74);
    tft.print(title);

    tft.setFreeFont(nullptr);
    tft.setTextSize(1);
    tft.setTextColor(setupTheme.textSec, setupTheme.panel);
    const char* line1 = "Open a browser and visit:";
    tw = tft.textWidth(line1);
    tft.setCursor((SCREEN_W - tw) / 2, 100);
    tft.print(line1);

    // Settings URL
    tft.setTextSize(2);
    tft.setTextColor(setupTheme.accent, setupTheme.panel);
    String addr = Network::localAddress();
    tw = tft.textWidth(addr);
    tft.setCursor((SCREEN_W - tw) / 2, 120);
    tft.print(addr);

    // QR for settings URL
    String settingsUrl = "http://" + addr + "/settings";
    drawQRCode(SCREEN_W / 2, 190, settingsUrl.c_str(), 2);

    tft.setFreeFont(nullptr);
    tft.setTextSize(1);
    tft.setTextColor(setupTheme.textDim, setupTheme.panel);
    const char* hint = "Set city, stocks, prayer method";
    tw = tft.textWidth(hint);
    tft.setCursor((SCREEN_W - tw) / 2, 248);
    tft.print(hint);

    // Auto-advance hint at bottom
    tft.setTextColor(setupTheme.textDim, setupTheme.bg);
    const char* auto_msg = "Starting in 8 seconds...";
    tw = tft.textWidth(auto_msg);
    tft.setCursor((SCREEN_W - tw) / 2, 310);
    tft.print(auto_msg);
}

// Persist wizard state to NVS (survives restart after WiFi save)
static void saveWizardState(WizardState state) {
    Preferences prefs;
    prefs.begin("settings", false);
    prefs.putInt(WIZARD_STATE_NVS_KEY, (int)state);
    prefs.end();
}

static WizardState loadWizardState() {
    Preferences prefs;
    prefs.begin("settings", true);
    int s = prefs.getInt(WIZARD_STATE_NVS_KEY, (int)WIZ_WELCOME);
    prefs.end();
    if (s < WIZ_WELCOME || s > WIZ_DONE) return WIZ_WELCOME;
    return (WizardState)s;
}

static bool isFirstBoot() {
    Preferences prefs;
    prefs.begin("settings", true);
    bool first = prefs.getBool(FIRST_BOOT_NVS_KEY, true);
    prefs.end();
    return first;
}

static void clearFirstBoot() {
    Preferences prefs;
    prefs.begin("settings", false);
    prefs.putBool(FIRST_BOOT_NVS_KEY, false);
    prefs.putInt(WIZARD_STATE_NVS_KEY, (int)WIZ_DONE);
    prefs.end();
}

} // namespace UI
