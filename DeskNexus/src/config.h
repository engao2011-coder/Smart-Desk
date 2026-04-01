/*
 * config.h — User Configuration
 *
 * Edit this file to set your API keys, city, stock symbols,
 * and prayer-time preferences before flashing to the ESP32.
 *
 * Hardware target: ESP32-WROOM-32 with 2.8" ILI9341 display
 * (ESP32-2432S028R / "Cheap Yellow Display" – CYD)
 */

#pragma once

// ---------------------------------------------------------------------------
// Display (TFT_eSPI) — pinout for CYD ESP32-2432S028R
// These pins are defined here for application use (backlight, touch IRQ).
// The SPI/driver settings for TFT_eSPI are configured via build_flags in
// platformio.ini — no manual User_Setup.h editing is required.
// ---------------------------------------------------------------------------
#define TFT_BL_PIN     21   // Backlight PWM pin
#define TOUCH_CS_PIN   33   // XPT2046 chip-select
#define TOUCH_IRQ_PIN  36   // XPT2046 interrupt (optional)

// ---------------------------------------------------------------------------
// WiFi — Hybrid mode
// Leave WIFI_SSID empty ("") to always start in AP/setup mode first.
// The user can configure credentials through the built-in web portal,
// which are then persisted to NVS (Preferences).
// ---------------------------------------------------------------------------
#define WIFI_CONNECT_TIMEOUT_MS  15000   // ms to wait for STA connection

// Access-Point fallback settings (captive portal)
#define AP_SSID      "DeskNexus-Setup"
#define AP_PASSWORD  ""                   // Open network; set a password if desired

// ---------------------------------------------------------------------------
// NTP (time sync)
// ---------------------------------------------------------------------------
#define NTP_SERVER_1  "pool.ntp.org"
#define NTP_SERVER_2  "time.nist.gov"
// UTC offset in seconds — adjust for your timezone
// Examples: UTC+3 = 10800, UTC-5 = -18000, UTC+5:30 = 19800
#define NTP_UTC_OFFSET_SEC   10800   // ← Default: UTC+3 (change as needed)
#define NTP_DST_OFFSET_SEC   0       // Daylight-saving offset (0 if not used)

// ---------------------------------------------------------------------------
// Automatic city/timezone detection (runs on boot when WiFi is available)
// ---------------------------------------------------------------------------
#define AUTO_DETECT_LOCATION_TIME  true
#define AUTO_DETECT_HTTP_TIMEOUT_MS  8000
#define AUTO_DETECT_GEO_URL          "http://ipwho.is/"
#define AUTO_DETECT_TZ_API_BASE      "http://worldtimeapi.org/api/timezone/"

// ---------------------------------------------------------------------------
// OpenWeatherMap
// Register at https://openweathermap.org/api to get a free API key.
// ---------------------------------------------------------------------------
#define OWM_API_KEY    "YOUR_OPENWEATHERMAP_API_KEY"   // ← replace
#define OWM_CITY_NAME  "Riyadh"                         // ← your city
#define OWM_COUNTRY    "SA"                              // ISO country code
#define OWM_UNITS      "metric"   // "metric" (°C) or "imperial" (°F)
#define OWM_LANG       "en"       // Language for weather description

// Weather refresh policy:
// - fetched at most once per wall-clock hour
// - successful fetch window is persisted in NVS

// ---------------------------------------------------------------------------
// Prayer Times (Aladhan API — https://aladhan.com/prayer-times-api)
// ---------------------------------------------------------------------------
#define PRAYER_CITY     "Riyadh"    // ← your city
#define PRAYER_COUNTRY  "SA"        // ISO country code
// Calculation method (Aladhan method number):
//  1 = Muslim World League, 2 = ISNA, 3 = Egypt, 4 = Makkah, 5 = Karachi
//  8 = Gulf Region, 16 = Turkey, 17 = Tehran …
#define PRAYER_METHOD   4           // ← Makkah / Umm Al-Qura

// Prayer refresh policy:
// - fetched at most once per wall-clock day
// - successful fetch window is persisted in NVS

// ---------------------------------------------------------------------------
// Stock Monitor
// Up to MAX_STOCKS symbols will be displayed in rotation.
// Uses Yahoo Finance (no API key required).
// Symbol format: TICKER.EXCHANGE  e.g. "IUSE.L", "IUSD.DE", "PPFB.DE"
// ---------------------------------------------------------------------------
#define MAX_STOCKS      5

// Stock symbols to monitor (fill up to MAX_STOCKS, leave extras as "")
static const char* STOCK_SYMBOLS[MAX_STOCKS] = {
    "IUSE.L",
    "IUSD.DE",
    "PPFB.DE",
    "",
    "",
};

// How often to refresh stock data (milliseconds)
#define STOCK_REFRESH_MS   (5UL * 60UL * 1000UL)   // 5 minutes

// Stock alert threshold — notify if price changes by this % since last refresh
#define STOCK_ALERT_PCT   2.0f   // 2 %

// ---------------------------------------------------------------------------
// UI / Display behaviour
// ---------------------------------------------------------------------------
#define SCREEN_TIMEOUT_MS   60000   // Dim screen after 60 s of inactivity (0 = never)
#define BACKLIGHT_DIM_DUTY  40      // Backlight PWM duty when dimmed (0-255)
#define BACKLIGHT_FULL_DUTY 220     // Backlight PWM duty when active  (0-255)

// ---------------------------------------------------------------------------
// Auto-carousel (page rotation)
// ---------------------------------------------------------------------------
#define CAROUSEL_INTERVAL_MS  10000   // Auto-switch pages every 10 s
#define CAROUSEL_PAUSE_MS     30000   // Pause carousel 30 s after manual touch

// Touch calibration values for the CYD XPT2046 in portrait mode.
// Run the TFT_eSPI Touch_calibrate example and paste results here.
#define TOUCH_CAL_DATA { 365, 3570, 340, 3520, 1 }
