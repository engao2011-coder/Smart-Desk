/*
 * settings.h — Runtime Configuration (NVS-backed)
 *
 * Provides mutable settings that can be changed via the web UI and
 * persisted to NVS (ESP32 Preferences).  Compile-time #defines in
 * config.h serve as initial defaults when no NVS value exists yet.
 *
 * NOTE ON THE HEADER-ONLY PATTERN: like the other DeskNexus modules, this header
 * defines its state and functions as `static` at namespace scope. That is sound
 * only because every module is included into a SINGLE translation unit
 * (main.cpp). Including any of these headers from a second .cpp would give that
 * TU its own private copy of the state — if the project ever grows a second
 * source file, these should move to a .cpp with `extern` declarations here.
 */

#pragma once

#include <Preferences.h>
#include <time.h>
#include "config.h"

namespace Settings {

// ---------------------------------------------------------------------------
// Runtime fields — initialised from config.h defaults
// ---------------------------------------------------------------------------

// Location (shared by Weather + Prayer)
static char city[64]    = OWM_CITY_NAME;
static char country[8]  = OWM_COUNTRY;

// Weather
static char owmApiKey[48] = OWM_API_KEY;
static char owmUnits[12]  = OWM_UNITS;

// Prayer
static int  prayerMethod  = PRAYER_METHOD;

// Stocks (Yahoo Finance — no API key needed)
// Compiled defaults come from STOCK_SYMBOLS in config.h. The array is seeded at
// the top of load() and resetToDefaults() so editing config.h takes effect.
static char stockSymbols[MAX_STOCKS][24] = {};

// Copy the compiled STOCK_SYMBOLS defaults (config.h) into stockSymbols.
static void applyCompiledStockDefaults() {
    for (int i = 0; i < MAX_STOCKS; i++) {
        strncpy(stockSymbols[i], STOCK_SYMBOLS[i] ? STOCK_SYMBOLS[i] : "",
                sizeof(stockSymbols[i]) - 1);
        stockSymbols[i][sizeof(stockSymbols[i]) - 1] = '\0';
    }
}

// Timezone
static long utcOffset = NTP_UTC_OFFSET_SEC;

// UI theme preference (manual only)
// true  => dark theme
// false => light theme
static bool themeDark = true;

// Theme mode: 0 = auto (dark at night based on prayer times), 1 = always dark, 2 = always light
static int themeMode = 0;

// When true, auto-detect will NOT overwrite city/country (user set it manually)
static bool cityManual = false;

// Stocks currency display — when true prices are converted to EUR
static bool stockEuro = false;

// Stock refresh interval in minutes (maps to STOCK_REFRESH_MS default)
static int  stockRefreshMin = STOCK_REFRESH_MS / 60000;

// Web admin password — when non-empty, overrides the default device password
// for web-UI / OTA Basic Auth. Empty means "use the MAC-derived AP password".
// The AP (hotspot) join password is unaffected and stays MAC-derived.
static char adminPassword[64] = "";

// Auto-detection metadata
static bool autoDetectLastOk = false;
static long autoDetectLastEpoch = 0;
static char autoDetectStatus[64] = "Never run";

// Last successful fetch windows (persisted to NVS)
// -1 means "never fetched".
static int weatherFetchYear  = -1;
static int weatherFetchMonth = -1;
static int weatherFetchDay   = -1;
static int weatherFetchHour  = -1;

static int prayerFetchYear   = -1;
static int prayerFetchMonth  = -1;
static int prayerFetchDay    = -1;

// NVS schema version — bump when key names/formats change between firmware releases.
// On mismatch the settings namespace is wiped and re-initialised from config.h defaults.
static constexpr int NVS_VERSION = 2;

// ---------------------------------------------------------------------------
// Load from NVS (falls back to compiled defaults when key is absent)
// ---------------------------------------------------------------------------
static void load() {
    // Seed compiled defaults first so they survive an NVS version-mismatch wipe
    // (that path returns early) and are overwritten only by stored values.
    applyCompiledStockDefaults();

    Preferences prefs;
    prefs.begin("settings", false);  // read-write (needed for potential migration)

    int storedVer = prefs.getInt("nvsVer", 0);
    if (storedVer != NVS_VERSION) {
        Serial.printf("[Settings] NVS version mismatch (stored=%d, expected=%d) — resetting.\n",
                      storedVer, NVS_VERSION);
        prefs.clear();
        prefs.putInt("nvsVer", NVS_VERSION);
        prefs.end();
        return;  // fields keep their compiled defaults
    }

    prefs.getString("city",      city,      sizeof(city));
    prefs.getString("country",   country,   sizeof(country));
    prefs.getString("owmKey",    owmApiKey, sizeof(owmApiKey));
    prefs.getString("owmUnits",  owmUnits,  sizeof(owmUnits));
    prayerMethod = prefs.getInt("prayMethod", prayerMethod);

    for (int i = 0; i < MAX_STOCKS; i++) {
        char key[8];
        snprintf(key, sizeof(key), "stk%d", i);
        prefs.getString(key, stockSymbols[i], sizeof(stockSymbols[i]));
    }

    utcOffset = prefs.getLong("utcOff", utcOffset);
    themeDark = prefs.getBool("themeDark", themeDark);
    themeMode = prefs.getInt("themeM", themeMode);

    weatherFetchYear  = prefs.getInt("wFY", weatherFetchYear);
    weatherFetchMonth = prefs.getInt("wFM", weatherFetchMonth);
    weatherFetchDay   = prefs.getInt("wFD", weatherFetchDay);
    weatherFetchHour  = prefs.getInt("wFH", weatherFetchHour);

    prayerFetchYear   = prefs.getInt("pFY", prayerFetchYear);
    prayerFetchMonth  = prefs.getInt("pFM", prayerFetchMonth);
    prayerFetchDay    = prefs.getInt("pFD", prayerFetchDay);

    autoDetectLastOk    = prefs.getBool("adOK", autoDetectLastOk);
    autoDetectLastEpoch = prefs.getLong("adTS", autoDetectLastEpoch);
    if (prefs.isKey("adMsg")) {
        prefs.getString("adMsg", autoDetectStatus, sizeof(autoDetectStatus));
    }
    cityManual = prefs.getBool("cityM", cityManual);

    stockEuro = prefs.getBool("stkEur", stockEuro);
    stockRefreshMin = prefs.getInt("stkRef", stockRefreshMin);
    prefs.getString("admPw", adminPassword, sizeof(adminPassword));

    prefs.end();
    Serial.printf("[Settings] Loaded — city=%s  country=%s  method=%d  utc=%ld  cityManual=%d\n",
                  city, country, prayerMethod, utcOffset, (int)cityManual);
}

// ---------------------------------------------------------------------------
// Save current values to NVS
// ---------------------------------------------------------------------------
static void save() {
    Preferences prefs;
    prefs.begin("settings", false);  // read-write

    prefs.putInt("nvsVer", NVS_VERSION);
    prefs.putString("city",      city);
    prefs.putString("country",   country);
    prefs.putString("owmKey",    owmApiKey);
    prefs.putString("owmUnits",  owmUnits);
    prefs.putInt("prayMethod",   prayerMethod);

    for (int i = 0; i < MAX_STOCKS; i++) {
        char key[8];
        snprintf(key, sizeof(key), "stk%d", i);
        prefs.putString(key, stockSymbols[i]);
    }

    prefs.putLong("utcOff", utcOffset);
    prefs.putBool("themeDark", themeDark);
    prefs.putInt("themeM", themeMode);

    prefs.putInt("wFY", weatherFetchYear);
    prefs.putInt("wFM", weatherFetchMonth);
    prefs.putInt("wFD", weatherFetchDay);
    prefs.putInt("wFH", weatherFetchHour);

    prefs.putInt("pFY", prayerFetchYear);
    prefs.putInt("pFM", prayerFetchMonth);
    prefs.putInt("pFD", prayerFetchDay);

    prefs.putBool("adOK", autoDetectLastOk);
    prefs.putLong("adTS", autoDetectLastEpoch);
    prefs.putString("adMsg", autoDetectStatus);
    prefs.putBool("cityM", cityManual);

    prefs.putBool("stkEur", stockEuro);
    prefs.putInt("stkRef", stockRefreshMin);
    prefs.putString("admPw", adminPassword);

    prefs.end();
    Serial.println("[Settings] Saved to NVS.");
}

// ---------------------------------------------------------------------------
// Reset NVS to compiled defaults
// ---------------------------------------------------------------------------
static void resetToDefaults() {
    Preferences prefs;
    prefs.begin("settings", false);
    prefs.clear();
    prefs.end();

    // Re-init fields from #defines
    strncpy(city,      OWM_CITY_NAME, sizeof(city) - 1);
    strncpy(country,   OWM_COUNTRY,   sizeof(country) - 1);
    strncpy(owmApiKey, OWM_API_KEY,   sizeof(owmApiKey) - 1);
    strncpy(owmUnits,  OWM_UNITS,     sizeof(owmUnits) - 1);
    prayerMethod = PRAYER_METHOD;

    applyCompiledStockDefaults();
    utcOffset = NTP_UTC_OFFSET_SEC;
    themeDark = true;
    themeMode = 0;

    weatherFetchYear  = -1;
    weatherFetchMonth = -1;
    weatherFetchDay   = -1;
    weatherFetchHour  = -1;

    prayerFetchYear   = -1;
    prayerFetchMonth  = -1;
    prayerFetchDay    = -1;

    autoDetectLastOk = false;
    autoDetectLastEpoch = 0;
    strncpy(autoDetectStatus, "Never run", sizeof(autoDetectStatus) - 1);
    autoDetectStatus[sizeof(autoDetectStatus) - 1] = '\0';
    cityManual = false;

    stockEuro = false;
    stockRefreshMin = STOCK_REFRESH_MS / 60000;
    adminPassword[0] = '\0';

    Serial.println("[Settings] Reset to compiled defaults.");
}

// ---------------------------------------------------------------------------
// Weather fetch window helpers
// ---------------------------------------------------------------------------
static bool isWeatherFetchedThisHour(const tm& t) {
    return weatherFetchYear  == (t.tm_year + 1900) &&
           weatherFetchMonth == (t.tm_mon + 1) &&
           weatherFetchDay   == t.tm_mday &&
           weatherFetchHour  == t.tm_hour;
}

static void markWeatherFetched(const tm& t) {
    weatherFetchYear  = t.tm_year + 1900;
    weatherFetchMonth = t.tm_mon + 1;
    weatherFetchDay   = t.tm_mday;
    weatherFetchHour  = t.tm_hour;
    // Write only the 4 fetch-window keys to reduce NVS wear.
    Preferences prefs;
    prefs.begin("settings", false);
    prefs.putInt("wFY", weatherFetchYear);
    prefs.putInt("wFM", weatherFetchMonth);
    prefs.putInt("wFD", weatherFetchDay);
    prefs.putInt("wFH", weatherFetchHour);
    prefs.end();
}

// ---------------------------------------------------------------------------
// Prayer fetch window helpers
// ---------------------------------------------------------------------------
static bool isPrayerFetchedToday(const tm& t) {
    return prayerFetchYear  == (t.tm_year + 1900) &&
           prayerFetchMonth == (t.tm_mon + 1) &&
           prayerFetchDay   == t.tm_mday;
}

static void markPrayerFetched(const tm& t) {
    prayerFetchYear  = t.tm_year + 1900;
    prayerFetchMonth = t.tm_mon + 1;
    prayerFetchDay   = t.tm_mday;
    // Write only the 3 fetch-window keys to reduce NVS wear.
    Preferences prefs;
    prefs.begin("settings", false);
    prefs.putInt("pFY", prayerFetchYear);
    prefs.putInt("pFM", prayerFetchMonth);
    prefs.putInt("pFD", prayerFetchDay);
    prefs.end();
}

} // namespace Settings
