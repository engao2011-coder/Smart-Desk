/*
 * settings.h — Runtime Configuration (NVS-backed)
 *
 * Provides mutable settings that can be changed via the web UI and
 * persisted to NVS (ESP32 Preferences).  Compile-time #defines in
 * config.h serve as initial defaults when no NVS value exists yet.
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
static char stockSymbols[MAX_STOCKS][24] = {
    "IUSE.L", "IUSD.DE", "PPFB.DE", "", ""
};

// Timezone
static long utcOffset = NTP_UTC_OFFSET_SEC;

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

// ---------------------------------------------------------------------------
// Load from NVS (falls back to compiled defaults when key is absent)
// ---------------------------------------------------------------------------
static void load() {
    Preferences prefs;
    prefs.begin("settings", true);  // read-only

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

    prefs.end();
    Serial.printf("[Settings] Loaded — city=%s  country=%s  method=%d  utc=%ld\n",
                  city, country, prayerMethod, utcOffset);
}

// ---------------------------------------------------------------------------
// Save current values to NVS
// ---------------------------------------------------------------------------
static void save() {
    Preferences prefs;
    prefs.begin("settings", false);  // read-write

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

    const char* defaults[MAX_STOCKS] = {"IUSE.L","IUSD.DE","PPFB.DE","",""};
    for (int i = 0; i < MAX_STOCKS; i++) {
        strncpy(stockSymbols[i], defaults[i], sizeof(stockSymbols[i]) - 1);
    }
    utcOffset = NTP_UTC_OFFSET_SEC;

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
    save();
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
    save();
}

} // namespace Settings
