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
 *  │  Hero       (100 px)   │  HH:MM (left) │ icon+temp (right)
 *  │                        │  countdown / status below clock
 *  ├────────────────────────┤ y=124
 *  │                        │
 *  │  Panel      (196 px)   │  Home (0), Prayer (1), Forecast (2), Stocks (3)
 *  │                        │  Home is idle landing page; detail pages auto-carousel
 *  └────────────────────────┘ y=320
 *
 * Touch: tap right half of panel → next page, left half → prev page.
 * Home page: tap prayer section → prayer detail, tap stock → stocks detail.
 * After 60s idle, returns to Home page automatically.
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
    0xE71A,  // bg         — warm beige canvas (clearly distinct from white cards)
    0xFFFF,  // panel      — pure white (strong card elevation vs bg)
    0xD240,  // accent     — burnt orange
    0x18C3,  // textPri    — deep slate
    0x4A49,  // textSec    — strong mid grey
    0x738E,  // textDim    — medium grey (~4.8:1 on white, meets WCAG AA)
    0x1A86,  // green      — readable green
    0xD104,  // red        — readable red
    0xBC40,  // gold       — warm amber
    0xA534,  // separator  — neutral mid-grey (visible on both bg and panel)
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
static unsigned long azanScreenExpiry = 0;
static int      prayerScrollOffset = 0;
// Page transition animation state
static int      transitionDir     = 0;  // -1=left, 0=none, 1=right
static int      transitionStep    = 0;
static int      transitionTarget  = -1;

// Touch & carousel timing
static unsigned long lastTouchMs        = 0;
static unsigned long lastPageSwitch     = 0;
static unsigned long carouselPausedUntil = 0;
// Page dot pulse animation
static unsigned long dotPulseStart      = 0;

// Home page "Prayed" button state (set by drawHomePanel, read by handleTouch)
static bool homePrayedButtonVisible = false;
static int  homePrayedButtonY = 0;

// Break reminder state
static unsigned long breakLastNotify    = 0;     // millis() of last break notification
static bool          breakScreenActive  = false;  // full-screen break reminder showing
static unsigned long breakScreenStart   = 0;      // when break screen was shown (for duration calc)

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

// Forward declarations for functions used before their definition
static void playPanelTransition(int direction);
static void dismissBreakReminder();
static void showBanner(const char* text, uint32_t durationMs, BannerStyle style);

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

// Carousel-safe page advance — skips PAGE_HOME and PAGE_BREAK (home is the idle landing page,
// break is event-driven). Cycles through the detail pages only.
static int nextCarouselPage(int fromPage, int direction) {
    int page = fromPage;
    for (int i = 0; i < PAGE_COUNT; i++) {
        page = (page + direction + PAGE_COUNT) % PAGE_COUNT;
        if (page != PAGE_HOME && page != PAGE_BREAK && isPageAllowed(page)) {
            return page;
        }
    }
    return PAGE_PRAYER;
}

static bool switchPageBy(int direction) {
    int nextPage = nextAllowedPage(activePage, direction);
    bool changed = (nextPage != activePage);
    if (changed) {
        playPanelTransition(direction);
        dotPulseStart = millis();
    }
    activePage = nextPage;
    lastPageSwitch = millis();
    needsRedraw = true;
    return changed;
}

static bool coerceAllowedActivePage() {
    if (isPageAllowed(activePage)) return false;
    activePage = PAGE_HOME;
    needsRedraw = true;
    return true;
}

static void advancePage() {
    // Use carousel-safe advance that skips PAGE_STOCKS;
    // stocks page is only shown when a stock moves >= STOCK_INTRA_CHANGE_PCT
    int nextPage = nextCarouselPage(activePage, 1);
    if (nextPage != activePage) {
        playPanelTransition(1);
        dotPulseStart = millis();
    }
    activePage = nextPage;
    lastPageSwitch = millis();
    needsRedraw = true;
}

static bool shouldAutoAdvance() {
    if (activePage == PAGE_HOME) return false;  // Home is the idle page, don't auto-advance
    if (millis() < carouselPausedUntil) return false;
    return (millis() - lastPageSwitch) >= CAROUSEL_INTERVAL_MS;
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
    carouselPausedUntil = millis() + CAROUSEL_PAUSE_MS;
}

static void pauseCarouselFor(unsigned long ms) {
    carouselPausedUntil = millis() + ms;
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

    // Home page touch zones — drill into detail pages or quick-prayed
    if (activePage == PAGE_HOME && (int)ty >= LAYOUT_PANEL_Y) {
        const int cardY = LAYOUT_PANEL_Y + 8;
        // "Prayed" quick-action button on Home
        if (homePrayedButtonVisible) {
            int btnW = 80, btnH = 22;
            int btnX = 8 + (SCREEN_W - 16 - btnW) / 2;
            if ((int)ty >= homePrayedButtonY && (int)ty < homePrayedButtonY + btnH &&
                (int)tx >= btnX && (int)tx < btnX + btnW) {
                tft.fillRoundRect(btnX, homePrayedButtonY, btnW, btnH, 6, theme.textDim);
                delay(120);
                Prayer::markPendingPrayed();
                needsRedraw = true;
                return true;
            }
        }
        // Separator is at roughly cardY + 6 + 12 + 30 + (prayed btn ~26) + 8 = ~82px from cardY
        // Prayer section occupies top ~55% of panel, stock section bottom ~35%
        int panelMid = LAYOUT_PANEL_Y + (LAYOUT_PANEL_H * 2 / 3);
        if ((int)ty < panelMid) {
            // Tap prayer area → drill to full prayer list
            playPanelTransition(1);
            dotPulseStart = millis();
            activePage = PAGE_PRAYER;
            lastPageSwitch = millis();
            needsRedraw = true;
            return true;
        } else {
            // Tap stock area → drill to full stocks page (if allowed)
            if (isPageAllowed(PAGE_STOCKS)) {
                playPanelTransition(1);
                dotPulseStart = millis();
                activePage = PAGE_STOCKS;
                lastPageSwitch = millis();
                needsRedraw = true;
                return true;
            }
        }
    }

    // Tap a MISSED prayer row to retroactively mark it as prayed
    if (activePage == PAGE_PRAYER && Prayer::current.valid &&
        (int)ty >= LAYOUT_PANEL_Y && (int)ty < (SCREEN_H - 56)) {
        const int pendingForTouch = Prayer::pendingPrayerIndex();
        const int footerHTouch = pendingForTouch >= 0 ? 52 : 0;
        const int availHTouch = (LAYOUT_PANEL_Y + LAYOUT_PANEL_H - 14) - (LAYOUT_PANEL_Y + 14) - footerHTouch;
        const int ROW_H  = max(22, min(30, availHTouch / Prayer::PRAYER_COUNT));
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
            // Wipe transition already played in switchPageBy()
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

    // Page dots — right side (ring vs filled + accent glow for active)
    const int DOT_R = 4;
    const int DOT_GAP = 14;
    int dotsW = PAGE_COUNT * DOT_GAP;
    int dotX = SCREEN_W - dotsW - 8;
    bool pulsing = (millis() - dotPulseStart) < 300;
    for (int i = 0; i < PAGE_COUNT; i++) {
        int cx = dotX + i * DOT_GAP + DOT_R;
        int cy = 11;
        bool allowed = isPageAllowed(i);
        if (i == activePage) {
            int r = pulsing ? DOT_R + 2 : DOT_R;
            tft.fillCircle(cx, cy, r + 2, theme.panel);   // clear area
            if (i == PAGE_HOME) {
                // Home dot: small rounded square to differentiate
                tft.fillRoundRect(cx - r, cy - r, r * 2, r * 2, 2, theme.accent);
                tft.drawRoundRect(cx - r - 1, cy - r - 1, r * 2 + 2, r * 2 + 2, 2, theme.gold);
            } else {
                tft.fillCircle(cx, cy, r, theme.accent);
                tft.drawCircle(cx, cy, r + 1, theme.gold);    // accent glow ring
            }
        } else if (allowed) {
            if (i == PAGE_HOME) {
                tft.fillRoundRect(cx - DOT_R + 1, cy - DOT_R + 1, (DOT_R - 1) * 2, (DOT_R - 1) * 2, 2, theme.panel);
                tft.drawRoundRect(cx - DOT_R + 1, cy - DOT_R + 1, (DOT_R - 1) * 2, (DOT_R - 1) * 2, 2, theme.textDim);
            } else {
                tft.drawCircle(cx, cy, DOT_R, theme.textDim); // hollow ring
            }
        } else {
            tft.fillCircle(cx, cy, DOT_R, theme.panel);
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
    int minutesLeft = Prayer::minutesUntilNext();
    // Urgency color: gold when < 5 min, else dim
    uint16_t cdColor = (minutesLeft >= 0 && minutesLeft < 5) ? theme.gold : theme.textDim;
    tft.setTextColor(cdColor, theme.panel);
    char nextBuf[40];
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
    // Pulsing alive dot (toggles every second)
    bool dotOn = (millis() / 1000) % 2 == 0;
    const char* dotStr = dotOn ? " *" : "  ";
    int dotW = tft.textWidth(dotStr);
    int totalW = subW + dotW;
    int subX = 12 + (122 - totalW) / 2;
    if (subX < 12) subX = 12;
    tft.setCursor(subX, cardY + 64);
    tft.print(nextBuf);
    tft.setTextColor(dotOn ? theme.gold : theme.panel, theme.panel);
    tft.print(dotStr);

    // ── Vertical divider ──
    const int divX = 138;
    tft.drawFastVLine(divX, cardY + 8, cardH - 16, theme.separator);

    // ── Right zone: Weather (x=142..224) ──
    const int rZoneX = 146;

    if (!Weather::current.valid) {
        tft.setFreeFont(nullptr);
        tft.setTextSize(1);
        tft.setTextColor(theme.textDim, theme.panel);
        if (Weather::current.fetchState == Weather::WEATHER_NO_KEY) {
            // Friendly hint instead of error — weather is optional
            tft.setCursor(rZoneX, cardY + 22);
            tft.print("Weather");
            tft.setCursor(rZoneX, cardY + 34);
            tft.print("Setup at");
            tft.setTextColor(theme.accent, theme.panel);
            tft.setCursor(rZoneX, cardY + 46);
            tft.print("desknexus");
            tft.setCursor(rZoneX, cardY + 58);
            tft.print(".local");
            tft.setTextColor(theme.textDim, theme.panel);
            tft.setCursor(rZoneX, cardY + 72);
            tft.print("/settings");
        } else {
            const char* line2 = "loading...";
            if (Weather::current.fetchState == Weather::WEATHER_NET_ERROR) line2 = "Net error";
            tft.setCursor(rZoneX, cardY + 28);
            tft.print("Weather");
            tft.setCursor(rZoneX, cardY + 40);
            tft.print(line2);
        }
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
    const int ROW_H = max(22, min(30, availH / Prayer::PRAYER_COUNT));
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

    // Touch affordance chevrons
    tft.setFreeFont(nullptr);
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
static void drawHomePanel() {
    fillPanel(LAYOUT_PANEL_Y, LAYOUT_PANEL_H, theme.bg);
    tft.setFreeFont(nullptr);

    homePrayedButtonVisible = false;

    const int cardX = 8, cardW = SCREEN_W - 16;
    const int cardY = LAYOUT_PANEL_Y + 8, cardH = LAYOUT_PANEL_H - 16;
    tft.fillRoundRect(cardX, cardY, cardW, cardH, 12, theme.panel);

    int yPos = cardY + 6;

    // ── Section 1: Current Prayer ──────────────────────────────────────────
    tft.setTextSize(1);
    tft.setTextColor(theme.textDim, theme.panel);
    tft.setCursor(cardX + 10, yPos);
    tft.print("Current Prayer");
    yPos += 12;

    int curIdx = Prayer::currentOrLastPrayerIndex();

    if (!Prayer::current.valid || curIdx < 0) {
        tft.setTextSize(2);
        tft.setTextColor(theme.textDim, theme.panel);
        tft.setCursor(cardX + 10, yPos + 4);
        tft.print("Loading...");
        yPos += 30;
    } else {
        Prayer::RowState curState = Prayer::rowStateForIndex(curIdx);
        const char* pName = Prayer::current.prayers[curIdx].name;
        const char* pTime = Prayer::current.prayers[curIdx].time;

        // Left edge bar (color signals state)
        uint16_t edgeColor = theme.separator;
        switch (curState) {
            case Prayer::ROW_DONE:    edgeColor = theme.green;  break;
            case Prayer::ROW_PENDING: edgeColor = theme.accent; break;
            case Prayer::ROW_SNOOZED: edgeColor = theme.textSec; break;
            case Prayer::ROW_MISSED:  edgeColor = theme.red;    break;
            default:                  edgeColor = theme.gold;    break;
        }
        tft.fillRect(cardX + 6, yPos, 4, 28, edgeColor);

        // Prayer name (large)
        tft.setFreeFont(&FreeSansBold9pt7b);
        tft.setTextSize(1);
        tft.setTextColor(theme.textPri, theme.panel);
        tft.setCursor(cardX + 16, yPos + 16);
        tft.print(pName);

        // Status indicator after name
        int nameW = tft.textWidth(pName);
        tft.setFreeFont(nullptr);
        if (curState == Prayer::ROW_DONE) {
            drawCheckmark(cardX + 16 + nameW + 4, yPos + 6, theme.green);
            tft.setTextSize(1);
            tft.setTextColor(theme.green, theme.panel);
            tft.setCursor(cardX + 16 + nameW + 18, yPos + 8);
            tft.print("Prayed");
        } else if (curState == Prayer::ROW_PENDING) {
            drawPillBadge(cardX + 16 + nameW + 6, yPos + 5, "DUE", theme.red, theme.textPri);
        } else if (curState == Prayer::ROW_SNOOZED) {
            drawPillBadge(cardX + 16 + nameW + 6, yPos + 5, "SNZD", theme.textDim, theme.textPri);
        } else if (curState == Prayer::ROW_MISSED) {
            drawPillBadge(cardX + 16 + nameW + 6, yPos + 5, "MISS", theme.red, theme.textPri);
        }

        // Time (right-aligned)
        tft.setFreeFont(&FreeSansBold9pt7b);
        tft.setTextColor(theme.textSec, theme.panel);
        int tw = tft.textWidth(pTime);
        tft.setCursor(cardX + cardW - tw - 14, yPos + 16);
        tft.print(pTime);

        tft.setFreeFont(nullptr);
        yPos += 30;

        // "Prayed" quick-action button when prayer is pending
        if (curState == Prayer::ROW_PENDING) {
            int btnW = 80, btnH = 22;
            int btnX = cardX + (cardW - btnW) / 2;
            tft.fillRoundRect(btnX, yPos, btnW, btnH, 6, theme.green);
            tft.setTextSize(1);
            tft.setTextColor(theme.textPri, theme.green);
            const char* pLabel = "Prayed";
            int plw = tft.textWidth(pLabel);
            tft.setCursor(btnX + (btnW - plw) / 2, yPos + 7);
            tft.print(pLabel);
            homePrayedButtonVisible = true;
            homePrayedButtonY = yPos;
            yPos += 26;
        }
    }

    // ── Separator ──────────────────────────────────────────────────────────
    tft.drawFastHLine(cardX + 16, yPos + 2, cardW - 32, theme.separator);
    yPos += 8;

    // ── Section 2: Next Prayer + Countdown Bar ─────────────────────────────
    tft.setTextSize(1);
    tft.setTextColor(theme.textDim, theme.panel);
    tft.setCursor(cardX + 10, yPos);
    tft.print("Next Prayer");
    yPos += 12;

    int nextIdx = Prayer::current.valid ? Prayer::current.nextIndex : -1;

    if (!Prayer::current.valid || nextIdx < 0) {
        tft.setTextSize(2);
        tft.setTextColor(theme.textDim, theme.panel);
        tft.setCursor(cardX + 10, yPos + 4);
        tft.print("---");
        yPos += 30;
    } else {
        const char* nName = Prayer::current.prayers[nextIdx].name;
        const char* nTime = Prayer::current.prayers[nextIdx].time;

        // Edge bar
        tft.fillRect(cardX + 6, yPos, 4, 28, theme.gold);

        // Prayer name (large)
        tft.setFreeFont(&FreeSansBold9pt7b);
        tft.setTextColor(theme.textPri, theme.panel);
        tft.setCursor(cardX + 16, yPos + 16);
        tft.print(nName);

        // Time (right-aligned)
        tft.setTextColor(theme.gold, theme.panel);
        int tw = tft.textWidth(nTime);
        tft.setCursor(cardX + cardW - tw - 14, yPos + 16);
        tft.print(nTime);

        tft.setFreeFont(nullptr);
        yPos += 30;

        // Countdown progress bar
        int minutesLeft = Prayer::minutesUntilNext();
        if (minutesLeft >= 0) {
            const int barX = cardX + 12, barW = cardW - 24, barH = 10;
            // Track
            tft.fillRoundRect(barX, yPos, barW, barH, 4, theme.separator);
            // Fill: invert so bar shrinks as time approaches (full = far away, empty = imminent)
            // Use 120 min as "full" reference to keep it visually useful
            float pct = 1.0f - ((float)minutesLeft / 120.0f);
            if (pct < 0.0f) pct = 0.0f;
            if (pct > 1.0f) pct = 1.0f;
            int fillW = (int)(barW * pct);
            // Color: gold when < 5 min, green otherwise
            uint16_t barColor = (minutesLeft < 5) ? theme.gold : theme.green;
            if (fillW > 0) {
                tft.fillRoundRect(barX, yPos, fillW, barH, 4, barColor);
            }

            // Time remaining text overlay
            char remBuf[12];
            int h = minutesLeft / 60;
            int m = minutesLeft % 60;
            snprintf(remBuf, sizeof(remBuf), "%dh %02dm", h, m);
            tft.setTextSize(1);
            uint16_t remColor = (minutesLeft < 5) ? theme.gold : theme.textSec;
            tft.setTextColor(remColor, theme.panel);
            int rw = tft.textWidth(remBuf);
            tft.setCursor(barX + barW - rw, yPos + barH + 2);
            tft.print(remBuf);
            yPos += barH + 14;
        } else {
            yPos += 4;
        }
    }

    // ── Separator ──────────────────────────────────────────────────────────
    tft.drawFastHLine(cardX + 16, yPos + 2, cardW - 32, theme.separator);
    yPos += 8;

    // ── Section 3: Top Moving Stock ────────────────────────────────────────
    tft.setTextSize(1);
    tft.setTextColor(theme.textDim, theme.panel);
    tft.setCursor(cardX + 10, yPos);
    tft.print("Top Stock");
    yPos += 12;

    int topIdx = Stocks::topMoverIndex();

    if (topIdx < 0) {
        tft.setTextSize(1);
        tft.setTextColor(theme.textDim, theme.panel);
        tft.setCursor(cardX + 16, yPos + 4);
        tft.print("No data");
    } else {
        const Stocks::Quote& q = Stocks::quotes[topIdx];
        uint16_t pctColor = (q.changePct >= 0) ? theme.green : theme.red;
        uint16_t edgeColor = pctColor;

        // Edge bar
        tft.fillRect(cardX + 6, yPos, 4, 28, edgeColor);

        // Symbol
        tft.setFreeFont(&FreeSansBold9pt7b);
        tft.setTextColor(theme.textPri, theme.panel);
        tft.setCursor(cardX + 16, yPos + 16);
        tft.print(q.symbol);

        // % change (right, with arrow)
        char pctBuf[16];
        const char* arrow = (q.changePct >= 0) ? " +" : " ";
        snprintf(pctBuf, sizeof(pctBuf), "%s%.2f%%", arrow, q.changePct);
        tft.setTextColor(pctColor, theme.panel);
        int pw = tft.textWidth(pctBuf);
        tft.setCursor(cardX + cardW - pw - 14, yPos + 16);
        tft.print(pctBuf);

        tft.setFreeFont(nullptr);
        yPos += 20;

        // Price below symbol
        char priceBuf[16];
        snprintf(priceBuf, sizeof(priceBuf), "$%.2f", q.price);
        tft.setTextSize(1);
        tft.setTextColor(theme.textSec, theme.panel);
        tft.setCursor(cardX + 16, yPos + 2);
        tft.print(priceBuf);

        // Alert dot
        if (q.alertTriggered) {
            tft.fillCircle(cardX + cardW - 16, yPos + 4, 3, theme.gold);
        }
    }

    // Touch affordance chevrons
    tft.setFreeFont(nullptr);
    tft.setTextSize(2);
    tft.setTextColor(theme.textDim, theme.bg);
    tft.setCursor(1, LAYOUT_PANEL_Y + LAYOUT_PANEL_H / 2 - 6);
    tft.print("<");
    int rw = tft.textWidth(">");
    tft.setCursor(SCREEN_W - rw - 1, LAYOUT_PANEL_Y + LAYOUT_PANEL_H / 2 - 6);
    tft.print(">");

    // Reset text state
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
    if (azanScreenExpiry > 0 && millis() < azanScreenExpiry) {
        float pct = (float)(azanScreenExpiry - millis()) / (float)PRAYER_FULLSCREEN_MS;
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
    if (azanScreenExpiry > 0 && millis() < azanScreenExpiry) {
        float pct = (float)(azanScreenExpiry - millis()) / (float)PRAYER_FULLSCREEN_MS;
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

    const int ROW_H   = 34;
    const int START_Y  = cardY + 16;
    const char* unit   = (strcmp(Settings::owmUnits, "imperial") == 0) ? "F" : "C";
    int shown = 0;

    for (int i = 0; i < Weather::forecast.dayCount && shown < FORECAST_DAYS; i++) {
        const Weather::DayForecast& d = Weather::forecast.days[i];
        if (!d.valid) continue;

        int ry = START_Y + shown * ROW_H;
        uint16_t rowBg = theme.panel;
        uint16_t accent = Weather::iconColor(d.iconCode);

        // Left edge bar (5px) coloured by weather condition
        tft.fillRect(cardX + 6, ry, 5, ROW_H - 4, accent);

        // Weather icon (24×24) — centered vertically in the row
        int iconCx = cardX + 30;
        int iconCy = ry + ROW_H / 2 - 1;
        drawWeatherIcon(iconCx, iconCy, d.iconCode);

        // Day name (e.g. "Mon")
        tft.setFreeFont(nullptr);
        tft.setTextSize(2);
        tft.setTextColor(theme.textPri, rowBg);
        tft.setCursor(cardX + 50, ry + 2);
        tft.print(d.dayName);

        // Condition label below day name
        tft.setTextSize(1);
        tft.setTextColor(theme.textSec, rowBg);
        tft.setCursor(cardX + 50, ry + 20);
        tft.print(Weather::iconLabel(d.iconCode));

        // Hi / Lo temps — right-aligned
        char hiBuf[10], loBuf[10];
        snprintf(hiBuf, sizeof(hiBuf), "%.0f%s", d.tempHi, unit);
        snprintf(loBuf, sizeof(loBuf), "%.0f%s", d.tempLo, unit);

        // Hi temp in accent colour (matches hero style)
        tft.setTextSize(2);
        tft.setTextColor(accent, rowBg);
        int hiW = tft.textWidth(hiBuf);
        tft.setCursor(cardX + cardW - hiW - 12, ry + 2);
        tft.print(hiBuf);

        // Lo temp in dim colour, smaller
        tft.setTextSize(1);
        tft.setTextColor(theme.textDim, rowBg);
        int loW = tft.textWidth(loBuf);
        tft.setCursor(cardX + cardW - loW - 12, ry + 22);
        tft.print(loBuf);

        // Separator line between rows
        if (shown < Weather::forecast.dayCount - 1) {
            tft.drawFastHLine(cardX + 16, ry + ROW_H - 2, cardW - 32, theme.separator);
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
static unsigned long bannerExpiry = 0;
static unsigned long bannerAnimStart = 0;
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
    if (millis() >= bannerExpiry) return;

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
    bannerExpiry = millis() + durationMs;
    bannerAnimStart = millis();
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

    drawStatusBar(wifiOk, dateStr, ipAddr);
    drawHero(t);

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
    if (azanScreenActive) {
        // Only update animated elements, not the full Azan screen
        updateAzanAnimation();
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
static void showSplash() {
    tft.fillScreen(theme.bg);
    tft.fillRoundRect(20, 96, SCREEN_W - 40, 116, 18, theme.panel);
    tft.setTextSize(1);
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

static void updateConnectingProgress(int percent) {
    Theme setupTheme = THEME_DARK;
    int barX = 41, barY = 171, barW = SCREEN_W - 82, barH = 10;
    int fillW = barW * percent / 100;
    if (fillW > barW) fillW = barW;
    tft.fillRoundRect(barX, barY, fillW, barH, 3, setupTheme.accent);

    // Percentage text
    tft.fillRect(barX, barY + 16, barW, 14, setupTheme.panel);
    tft.setFreeFont(nullptr);
    tft.setTextSize(1);
    tft.setTextColor(setupTheme.textSec, setupTheme.panel);
    char msg[8];
    snprintf(msg, sizeof(msg), "%d%%", percent);
    int tw = tft.textWidth(msg);
    tft.setCursor((SCREEN_W - tw) / 2, barY + 18);
    tft.print(msg);
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
