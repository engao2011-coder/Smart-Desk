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
    0xEF5C,  // bg         — clean warm off-white canvas (de-muddied vs old beige)
    0xFFFF,  // panel      — pure white (strong card elevation vs bg)
    0xDB42,  // accent     — refined amber-orange (cleaner than brick)
    0x1905,  // textPri    — ink slate (16:1 on white)
    0x42AD,  // textSec    — slate-600 (~7.6:1 on white)
    0x6BD1,  // textDim    — slate-500 (~4.4:1 on white, meets WCAG AA)
    0x1408,  // green      — lively emerald (readable up/prayed)
    0xC965,  // red        — clean red
    0xB3E1,  // gold       — deep warm amber
    0xC617,  // separator  — warm hairline (visible on both bg and panel)
    0xFF9A,  // highlightBg — soft pale amber (calm next-prayer tint)
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

// Home page "Prayed" button state (set by drawHomePanel, read by handleTouch)
static bool homePrayedButtonVisible = false;
static int  homePrayedButtonY = 0;

// Home card Stocks-section geometry (set by drawHomeStockSection, read by
// handleTouch + updateHomeStockSection). Declared early so the touch handler
// can use the section top as the prayer/stocks tap boundary.
static int homeStockSecX = 0, homeStockSecW = 0, homeStockSecY = -1;

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
    // Rollover-safe pause check (subtraction wraps correctly at the millis() overflow).
    if (carouselPauseDuration > 0 &&
        (millis() - carouselPauseStart) < carouselPauseDuration) return false;
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
        // Prayer timeline occupies the top of the card, the stocks section the
        // bottom. Use the recorded stocks-section top as the split so the zones
        // always match what was drawn (falls back to ~2/3 before first draw).
        int stockTop = (homeStockSecY > 0) ? homeStockSecY - 4
                                           : LAYOUT_PANEL_Y + (LAYOUT_PANEL_H * 2 / 3);
        if ((int)ty < stockTop) {
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
    // Vertically-centred time with a gold accent rule and the weekday name
    // beneath it. The prayer countdown that used to sit here was removed —
    // it lives in the prayer widget below.
    const int clockZoneW = 122;
    char hmBuf[6];
    snprintf(hmBuf, sizeof(hmBuf), "%02d:%02d", t.tm_hour, t.tm_min);

    tft.setTextColor(theme.textPri, theme.panel);
    // Ensure clock width/layout uses a deterministic text scale.
    tft.setTextSize(1);
    tft.setFreeFont(&FreeSansBold24pt7b);
    int tw = tft.textWidth(hmBuf);
    int clockX = 12 + (clockZoneW - tw) / 2;
    if (clockX < 12) clockX = 12;
    tft.setCursor(clockX, cardY + 48);
    tft.print(hmBuf);

    // Thin gold accent rule, as wide as the time, just below it.
    int ruleW = tw;
    int ruleX = 12 + (clockZoneW - ruleW) / 2;
    if (ruleX < 12) ruleX = 12;
    tft.drawFastHLine(ruleX, cardY + 60, ruleW, theme.gold);

    // Weekday name beneath the rule.
    static const char* const WEEKDAY[7] = {
        "Sunday", "Monday", "Tuesday", "Wednesday",
        "Thursday", "Friday", "Saturday"
    };
    const char* wd = WEEKDAY[(t.tm_wday >= 0 && t.tm_wday < 7) ? t.tm_wday : 0];
    tft.setFreeFont(nullptr);
    tft.setTextSize(1);
    tft.setTextColor(theme.textSec, theme.panel);
    int wdw = tft.textWidth(wd);
    int wdx = 12 + (clockZoneW - wdw) / 2;
    if (wdx < 12) wdx = 12;
    tft.setCursor(wdx, cardY + 76);
    tft.print(wd);

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
            // Friendly hint instead of error — weather is optional.
            // The full setup URL lives on the dedicated setup screens; here
            // we just point the user at Settings without cramming five
            // tight lines into the ~76px hero corner.
            tft.setTextColor(theme.textSec, theme.panel);
            tft.setCursor(rZoneX, cardY + 26);
            tft.print("Weather");
            tft.setTextColor(theme.textDim, theme.panel);
            tft.setCursor(rZoneX, cardY + 44);
            tft.print("Add API");
            tft.setCursor(rZoneX, cardY + 56);
            tft.print("key in");
            tft.setTextColor(theme.accent, theme.panel);
            tft.setCursor(rZoneX, cardY + 70);
            tft.print("Settings");
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
        const int wZoneW = 80;  // usable width of the right zone

        // Weather icon (centered in right zone, near top)
        int iconCx = rZoneX + wZoneW / 2;
        int iconCy = cardY + 20;
        drawWeatherIcon(iconCx, iconCy, w.iconCode);

        // Temperature below icon
        char tempBuf[12];
        const char* unit = (strcmp(Settings::owmUnits, "imperial") == 0) ? "F" : "C";
        snprintf(tempBuf, sizeof(tempBuf), "%.0f°%s", w.temp, unit);

        tft.setFreeFont(&FreeSansBold12pt7b);
        tft.setTextColor(wAccent, theme.panel);
        tw = tft.textWidth(tempBuf);
        int tempX = rZoneX + (wZoneW - tw) / 2;
        tft.setCursor(tempX, cardY + 50);
        tft.print(tempBuf);

        // Condition label
        tft.setFreeFont(nullptr);
        tft.setTextSize(1);
        tft.setTextColor(theme.textSec, theme.panel);
        const char* label = Weather::iconLabel(w.iconCode);
        int lw = tft.textWidth(label);
        int lx = rZoneX + (wZoneW - lw) / 2;
        tft.setCursor(lx, cardY + 62);
        tft.print(label);

        // Humidity line
        char humBuf[12];
        snprintf(humBuf, sizeof(humBuf), "Hum %.0f%%", w.humidity);
        tft.setTextColor(theme.textDim, theme.panel);
        int hw = tft.textWidth(humBuf);
        int hx = rZoneX + (wZoneW - hw) / 2;
        tft.setCursor(hx, cardY + 76);
        tft.print(humBuf);

        // City name
        tft.setTextColor(theme.textDim, theme.panel);
        int cn = tft.textWidth(w.cityName);
        int cnx = rZoneX + (wZoneW - cn) / 2;
        tft.setCursor(cnx, cardY + 88);
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

// Right-aligned "tap >" disclosure hint, drawn level with a Home section
// header to signal that tapping the section drills into its detail page.
// Distinguished from the dim carousel chevrons by the accent colour.
static void drawTapHint(int cardX, int cardW, int y) {
    tft.setFreeFont(nullptr);
    tft.setTextSize(1);
    tft.setTextColor(theme.accent, theme.panel);
    const char* hint = "tap >";
    int hw = tft.textWidth(hint);
    tft.setCursor(cardX + cardW - hw - 10, y);
    tft.print(hint);
    tft.setTextColor(theme.textDim, theme.panel);
}

// Geometry of the Home card's rotating Stocks section is recorded on each full
// draw (see homeStockSec* above) so updateHomeStockSection() can repaint just
// this region without flicker.

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

// Draw Section 3 of the Home card: the rotating stock quote (Stocks::displayIndex).
static void drawHomeStockSection(int cardX, int cardW, int yPos) {
    homeStockSecX = cardX; homeStockSecW = cardW; homeStockSecY = yPos;

    tft.setTextSize(1);
    tft.setTextColor(theme.textDim, theme.panel);
    tft.setCursor(cardX + 10, yPos);
    tft.print("Stocks");
    drawTapHint(cardX, cardW, yPos);
    yPos += 12;

    int topIdx = Stocks::displayQuoteIndex();

    if (topIdx < 0) {
        tft.setTextSize(1);
        tft.setTextColor(theme.textDim, theme.panel);
        tft.setCursor(cardX + 16, yPos + 4);
        tft.print("No data");
    } else {
        const Stocks::Quote& q = Stocks::quotes[topIdx];
        float mPct = Stocks::metricPct(q);
        uint16_t pctColor = (mPct >= 0) ? theme.green : theme.red;
        uint16_t edgeColor = pctColor;

        // Edge bar
        tft.fillRect(cardX + 6, yPos, 4, 28, edgeColor);

        // Line 1, right: daily change ("1D") — primary, big bold figure with a
        // small dim label. Measured first so the name knows its budget.
        tft.setFreeFont(&FreeSansBold9pt7b);
        char dBuf[16];
        const char* arrow = (mPct >= 0) ? " +" : " ";
        snprintf(dBuf, sizeof(dBuf), "%s%.2f%%", arrow, mPct);
        int dW    = tft.textWidth(dBuf);
        int dNumX = cardX + cardW - 14 - dW;
        tft.setFreeFont(nullptr);
        tft.setTextSize(1);
        int d1W = tft.textWidth("1D");
        int d1X = dNumX - d1W - 5;

        // Name (left) — shrink to the small built-in font, then truncate, when
        // the bold name would collide with the 1D figure.
        const char* nm       = Stocks::displayName(q);
        int         nameX    = cardX + 16;
        int         nameMaxW = d1X - nameX - 8;
        char        nameBuf[44];
        tft.setFreeFont(&FreeSansBold9pt7b);
        tft.setTextColor(theme.textPri, theme.panel);
        if (tft.textWidth(nm) <= nameMaxW) {
            fitTextToWidth(nm, nameMaxW, nameBuf, sizeof(nameBuf));
            tft.setCursor(nameX, yPos + 16);
            tft.print(nameBuf);
        } else {
            tft.setFreeFont(nullptr);
            tft.setTextSize(1);
            fitTextToWidth(nm, nameMaxW, nameBuf, sizeof(nameBuf));
            tft.setCursor(nameX, yPos + 10);
            tft.print(nameBuf);
        }

        // "1D" label + daily figure
        tft.setFreeFont(nullptr);
        tft.setTextSize(1);
        tft.setTextColor(theme.textDim, theme.panel);
        tft.setCursor(d1X, yPos + 6);
        tft.print("1D");
        tft.setFreeFont(&FreeSansBold9pt7b);
        tft.setTextColor(pctColor, theme.panel);
        tft.setCursor(dNumX, yPos + 16);
        tft.print(dBuf);

        tft.setFreeFont(nullptr);
        yPos += 20;

        // Line 2, left: price.
        char priceBuf[16];
        if (Settings::stockEuro) {
            snprintf(priceBuf, sizeof(priceBuf), "EUR %.2f", Stocks::euroPrice(q));
        } else {
            snprintf(priceBuf, sizeof(priceBuf), "$%.2f", q.price);
        }
        tft.setTextSize(1);
        tft.setTextColor(theme.textSec, theme.panel);
        tft.setCursor(cardX + 16, yPos + 2);
        tft.print(priceBuf);

        // Line 2, right: 52-week change ("52W") — secondary context, small.
        // "--" when no 52-week high was reported. Reserve room for the alert dot.
        bool  hp   = Stocks::hasPeak(q);
        float pPct = Stocks::peakPct(q);
        char  pBuf[12];
        if (hp) snprintf(pBuf, sizeof(pBuf), "%+.2f%%", pPct);
        else    snprintf(pBuf, sizeof(pBuf), "--");
        uint16_t pCol = !hp ? theme.textDim : ((pPct >= 0) ? theme.green : theme.red);
        tft.setTextSize(1);
        int pW    = tft.textWidth(pBuf);
        int pRight = cardX + cardW - (q.alertTriggered ? 22 : 12);
        int pNumX = pRight - pW;
        int p1X   = pNumX - tft.textWidth("52W") - 4;
        tft.setTextColor(theme.textDim, theme.panel);
        tft.setCursor(p1X, yPos + 2);
        tft.print("52W");
        tft.setTextColor(pCol, theme.panel);
        tft.setCursor(pNumX, yPos + 2);
        tft.print(pBuf);

        // Alert dot (daily move beyond the alert threshold)
        if (q.alertTriggered) {
            tft.fillCircle(cardX + cardW - 16, yPos + 4, 3, theme.gold);
        }
    }
}

static void drawHomePanel() {
    fillPanel(LAYOUT_PANEL_Y, LAYOUT_PANEL_H, theme.bg);
    tft.setFreeFont(nullptr);

    homePrayedButtonVisible = false;

    const int cardX = 8, cardW = SCREEN_W - 16;
    const int cardY = LAYOUT_PANEL_Y + 8, cardH = LAYOUT_PANEL_H - 16;
    tft.fillRoundRect(cardX, cardY, cardW, cardH, 12, theme.panel);

    int yPos = cardY + 6;

    // 3-letter prayer abbreviations for the compact day strip.
    auto prayerAbbrev = [](int idx) -> const char* {
        switch (idx) {
            case 0: return "Fjr";
            case 1: return "Sun";
            case 2: return "Dhr";
            case 3: return "Asr";
            case 4: return "Mgb";
            case 5: return "Ish";
            default: return "---";
        }
    };

    // ── Section 1: Today's prayer timeline ─────────────────────────────────
    // A horizontal strip of all six daily prayers (state-coloured dots + times),
    // a Fajr→Isha day-progress bar, and a prayed/missed summary. Tapping this
    // region drills into the full prayer list. The next-prayer countdown lives
    // only in the hero, so it isn't repeated here.
    tft.setTextSize(1);
    tft.setTextColor(theme.textDim, theme.panel);
    tft.setCursor(cardX + 10, yPos);
    tft.print("Today's prayers");
    drawTapHint(cardX, cardW, yPos);
    yPos += 14;

    const int nextIdx = Prayer::current.valid ? Prayer::current.nextIndex : -1;

    if (!Prayer::current.valid) {
        tft.setTextSize(2);
        tft.setTextColor(theme.textDim, theme.panel);
        tft.setCursor(cardX + 10, yPos + 6);
        tft.print("Loading...");
        yPos += 34;
    } else {
        // Show the five daily prayers; Sunrise (index 1) is omitted so each
        // remaining prayer gets a wider column.
        static const int dispIdx[5] = {0, 2, 3, 4, 5};
        const int DISP_COUNT = 5;
        // If the next event is Sunrise, emphasise the following prayer (Dhuhr)
        // instead so the strip/countdown always points at a real prayer.
        const int highlightIdx = (nextIdx == 1) ? 2 : nextIdx;

        const int stripX = cardX + 6;
        const int stripW = cardW - 12;
        const int colW   = stripW / DISP_COUNT;
        const int stripY = yPos;

        for (int d = 0; d < DISP_COUNT; d++) {
            int i = dispIdx[d];
            Prayer::RowState st = Prayer::rowStateForIndex(i);
            int colX = stripX + d * colW;
            int cx   = colX + colW / 2;
            bool isNext = (i == highlightIdx);

            // Highlight the next prayer's column so it pops out of the row.
            uint16_t colBg = theme.panel;
            if (isNext) {
                tft.fillRoundRect(colX + 1, stripY - 2, colW - 2, 32, 4, theme.highlightBg);
                colBg = theme.highlightBg;
            }

            uint16_t labelCol = isNext ? theme.gold : theme.textDim;
            uint16_t timeCol  = isNext ? theme.gold : theme.textSec;

            // Abbreviated name (top of column)
            tft.setTextSize(1);
            tft.setTextColor(labelCol, colBg);
            const char* ab = prayerAbbrev(i);
            int aw = tft.textWidth(ab);
            tft.setCursor(cx - aw / 2, stripY);
            tft.print(ab);

            // State marker dot (middle of column)
            int dotY = stripY + 14;
            switch (st) {
                case Prayer::ROW_DONE:
                    tft.fillCircle(cx, dotY, 4, theme.green);
                    break;
                case Prayer::ROW_MISSED:
                    tft.fillCircle(cx, dotY, 4, theme.red);
                    break;
                case Prayer::ROW_PENDING:
                    tft.fillCircle(cx, dotY, 4, theme.accent);
                    tft.drawCircle(cx, dotY, 5, theme.gold);
                    break;
                case Prayer::ROW_SNOOZED:
                    tft.drawCircle(cx, dotY, 4, theme.textSec);
                    tft.fillCircle(cx, dotY, 2, theme.textSec);
                    break;
                default:
                    if (isNext) {
                        tft.fillCircle(cx, dotY, 4, theme.gold);
                        tft.drawCircle(cx, dotY, 6, theme.gold);
                    } else {
                        tft.drawCircle(cx, dotY, 3, theme.textDim);
                    }
                    break;
            }

            // Time (bottom of column)
            tft.setTextColor(timeCol, colBg);
            const char* tm = Prayer::current.prayers[i].time;
            int twv = tft.textWidth(tm);
            tft.setCursor(cx - twv / 2, stripY + 22);
            tft.print(tm);
        }
        yPos = stripY + 32;

        // Current local time in minutes (for the countdown + progress fill).
        int nowMin = -1;
        struct tm nt;
        if (getLocalTime(&nt)) nowMin = nt.tm_hour * 60 + nt.tm_min;
        int ishaMin = Prayer::toMinutes(Prayer::current.prayers[5].time);

        // ── Summary line: prayed count (left) · missed count (right) ──
        int doneCount = 0;
        for (int i = 0; i < Prayer::PRAYER_COUNT; i++) {
            if (i == 1) continue;  // Sunrise is not a prayer
            if (Prayer::rowStateForIndex(i) == Prayer::ROW_DONE) doneCount++;
        }
        tft.setTextSize(1);
        char leftBuf[16];
        snprintf(leftBuf, sizeof(leftBuf), "%d prayed", doneCount);
        tft.setTextColor(theme.green, theme.panel);
        tft.setCursor(cardX + 12, yPos);
        tft.print(leftBuf);

        int missed = Prayer::missedCount();
        if (missed > 0) {
            char missBuf[16];
            snprintf(missBuf, sizeof(missBuf), "%d missed", missed);
            int rw = tft.textWidth(missBuf);
            tft.setTextColor(theme.red, theme.panel);
            tft.setCursor(cardX + cardW - rw - 12, yPos);
            tft.print(missBuf);
        }
        yPos += 13;

        // ── Countdown chip ─────────────────────────────────────────────────
        // Design A fused with the progress bar: the chip body IS the bar — it
        // fills from the previous prayer to the next one — with a gold edge,
        // clock glyph, prayer name (left) and remaining time (right) overlaid.
        int cdMin = Prayer::minutesUntilNext();
        if (nextIdx == 1 && nowMin >= 0) {
            // Fajr→Sunrise window: count to Dhuhr rather than Sunrise.
            cdMin = Prayer::toMinutes(Prayer::current.prayers[2].time) - nowMin;
        }
        if (highlightIdx >= 0 && cdMin >= 0 && nowMin >= 0) {
            // Fraction elapsed from the previous prayer to the next one.
            int nextMin = Prayer::toMinutes(Prayer::current.prayers[highlightIdx].time);
            int prevMin = -100000;
            for (int j = 0; j < Prayer::PRAYER_COUNT; j++) {
                int pm = Prayer::toMinutes(Prayer::current.prayers[j].time);
                if (pm <= nowMin && pm > prevMin) prevMin = pm;
            }
            if (prevMin == -100000) prevMin = ishaMin - 1440;   // before Fajr today
            int adjNext = nextMin;
            if (adjNext <= prevMin) adjNext += 1440;             // wraps past midnight
            float frac = 0.0f;
            if (adjNext > prevMin) frac = (float)(nowMin - prevMin) / (float)(adjNext - prevMin);
            if (frac < 0.0f) frac = 0.0f;
            if (frac > 1.0f) frac = 1.0f;

            bool urgent = (cdMin < 5);
            uint16_t fillCol = urgent ? theme.accent : theme.green;
            uint16_t timeCol = urgent ? theme.textPri : theme.gold;

            const int chipX = cardX + 10, chipW = cardW - 20, chipH = 24;
            const int chipY = yPos;

            // Track, then progress fill, then the gold left edge bar.
            tft.fillRoundRect(chipX, chipY, chipW, chipH, 5, theme.separator);
            int fillW = (int)((chipW - 4) * frac);
            if (fillW > 0) tft.fillRect(chipX + 4, chipY + 1, fillW, chipH - 2, fillCol);
            tft.fillRect(chipX, chipY, 4, chipH, theme.gold);

            // Clock glyph (gold) near the left.
            int gx = chipX + 16, gy = chipY + chipH / 2;
            tft.drawCircle(gx, gy, 6, theme.gold);
            tft.drawCircle(gx, gy, 5, theme.gold);
            tft.drawLine(gx, gy, gx, gy - 4, theme.gold);
            tft.drawLine(gx, gy, gx + 3, gy + 1, theme.gold);

            // Prayer name (left) — transparent bg so it sits over the fill.
            tft.setFreeFont(&FreeSansBold9pt7b);
            tft.setTextColor(theme.textPri);
            tft.setCursor(gx + 12, chipY + chipH - 7);
            tft.print(Prayer::current.prayers[highlightIdx].name);

            // Remaining time (right) — bold.
            char cdBuf[16];
            int ch = cdMin / 60, cm = cdMin % 60;
            if (ch > 0) snprintf(cdBuf, sizeof(cdBuf), "%dh %02dm", ch, cm);
            else        snprintf(cdBuf, sizeof(cdBuf), "%dm", cm);
            tft.setTextColor(timeCol);
            int cw = tft.textWidth(cdBuf);
            tft.setCursor(chipX + chipW - cw - 8, chipY + chipH - 7);
            tft.print(cdBuf);

            tft.setFreeFont(nullptr);
            tft.setTextSize(1);
            yPos += chipH + 4;
        } else {
            yPos += 2;
        }

        // ── "Prayed" quick-action button when the current prayer is due ──
        int curIdx = Prayer::currentOrLastPrayerIndex();
        if (curIdx >= 0 && Prayer::rowStateForIndex(curIdx) == Prayer::ROW_PENDING) {
            yPos += 2;
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
            yPos += btnH + 4;
        }
    }

    // ── Separator ──────────────────────────────────────────────────────────
    tft.drawFastHLine(cardX + 16, yPos + 2, cardW - 32, theme.separator);
    yPos += 10;

    // ── Section 2: Stocks (rotates through all symbols) ────────────────────
    drawHomeStockSection(cardX, cardW, yPos);

    drawPanelChevrons();

    // Reset text state
    tft.setFreeFont(nullptr);
    tft.setTextSize(1);
}

// Repaint just the rotating Stocks section of the Home card. Called on the
// rotation tick so the symbol cycles without redrawing (and flickering) the
// whole panel. Falls back to nothing if Home isn't the active page.
static void updateHomeStockSection() {
    if (activePage != PAGE_HOME || azanScreenActive || homeStockSecY < 0) return;
    // Clear the section's region (mid-card, clear of the rounded corners) and
    // redraw it, then re-stamp the chevrons in case the clear overlapped them.
    tft.fillRect(homeStockSecX + 2, homeStockSecY - 2, homeStockSecW - 4, 58, theme.panel);
    drawHomeStockSection(homeStockSecX, homeStockSecW, homeStockSecY);
    drawPanelChevrons();
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

    const int ROW_H  = 32;
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

    int shown = 0;

    for (int oi = 0; oi < orderCount && shown < 5; oi++) {
        int i = order[oi];
        const Stocks::Quote& q = Stocks::quotes[i];

        int ry = START_Y + shown * ROW_H;
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
            tft.setCursor(26, ry + 8);
            tft.print(Settings::stockSymbols[i]);

            if (badSym) {
                // Tell the user this symbol is the problem, not the network.
                tft.setTextSize(1);
                tft.setTextColor(theme.red, rowBg);
                const char* msg = "CHECK SYMBOL";
                int mw = tft.textWidth(msg);
                tft.setCursor(SCREEN_W - mw - 24, ry + 12);
                tft.print(msg);
            } else {
                tft.setTextColor(theme.textDim, rowBg);
                tft.setCursor(154, ry + 8);
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
            int         nameY    = ry + 2;
            tft.setTextSize(2);
            if (tft.textWidth(nm) > nameMaxW) {
                tft.setTextSize(1);   // shrink before resorting to truncation
                nameY = ry + 6;
            }
            fitTextToWidth(nm, nameMaxW, nameBuf, sizeof(nameBuf));
            tft.setTextColor(theme.textPri, rowBg);
            tft.setCursor(nameX, nameY);
            tft.print(nameBuf);

            // "1D" label + daily figure
            tft.setTextSize(1);
            tft.setTextColor(theme.textDim, rowBg);
            tft.setCursor(d1X, ry + 7);
            tft.print("1D");
            tft.setTextSize(2);
            tft.setTextColor(dCol, rowBg);
            tft.setCursor(dNumX, ry + 2);
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
            tft.setCursor(26, ry + 21);
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
            tft.setCursor(p1X, ry + 21);
            tft.print("52W");
            tft.setTextColor(pCol, rowBg);
            tft.setCursor(pNumX, ry + 21);
            tft.print(pBuf);

            // Alert dot — in the right margin, clear of both figures.
            if (q.alertTriggered) {
                tft.fillCircle(SCREEN_W - 12, ry + 16, 3, theme.gold);
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
