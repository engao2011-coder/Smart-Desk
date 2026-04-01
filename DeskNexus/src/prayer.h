/*
 * prayer.h — Prayer Times via Aladhan API
 *
 * API: https://api.aladhan.com/v1/timingsByCity
 *
 * Returns the five daily prayer times (Fajr, Dhuhr, Asr, Maghrib, Isha)
 * plus Sunrise. Times are in HH:MM (24-hour) format local to the city.
 *
 * The module also determines which prayer is "next" so the UI can
 * highlight it and show a countdown.
 */

#pragma once

#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "config.h"
#include "settings.h"

namespace Prayer {

// ---------------------------------------------------------------------------
// Data model
// ---------------------------------------------------------------------------
static const int PRAYER_COUNT = 6;

struct PrayerEntry {
    const char* name;
    char        time[6];   // "HH:MM\0"
};

struct Data {
    PrayerEntry prayers[PRAYER_COUNT] = {
        {"Fajr",    "--:--"},
        {"Sunrise", "--:--"},
        {"Dhuhr",   "--:--"},
        {"Asr",     "--:--"},
        {"Maghrib", "--:--"},
        {"Isha",    "--:--"},
    };
    int  nextIndex     = -1;   // index into prayers[] of the next prayer
    bool valid         = false;
    unsigned long fetchedAt = 0;
};

static Data current;

// ---------------------------------------------------------------------------
// Parse "HH:MM" string into minutes since midnight
// ---------------------------------------------------------------------------
static int toMinutes(const char* hhmm) {
    int h = 0, m = 0;
    sscanf(hhmm, "%d:%d", &h, &m);
    return h * 60 + m;
}

// ---------------------------------------------------------------------------
// Determine which prayer is next based on current local time
// ---------------------------------------------------------------------------
static void updateNextPrayer() {
    struct tm t;
    if (!getLocalTime(&t)) {
        current.nextIndex = -1;
        return;
    }
    int nowMin = t.tm_hour * 60 + t.tm_min;

    current.nextIndex = -1;
    for (int i = 0; i < PRAYER_COUNT; i++) {
        if (toMinutes(current.prayers[i].time) > nowMin) {
            current.nextIndex = i;
            break;
        }
    }
    // If all prayers have passed today, next = Fajr (tomorrow)
    if (current.nextIndex == -1) current.nextIndex = 0;
}

// ---------------------------------------------------------------------------
// Minutes until next prayer (from now)
// ---------------------------------------------------------------------------
static int minutesUntilNext() {
    if (current.nextIndex < 0) return -1;
    struct tm t;
    if (!getLocalTime(&t)) return -1;
    int nowMin  = t.tm_hour * 60 + t.tm_min;
    int nextMin = toMinutes(current.prayers[current.nextIndex].time);
    if (nextMin <= nowMin) nextMin += 1440;   // next day
    return nextMin - nowMin;
}

// ---------------------------------------------------------------------------
// Fetch prayer times from Aladhan API (blocking)
// ---------------------------------------------------------------------------
static bool fetchForTime(const struct tm& t) {
    char url[256];
    snprintf(url, sizeof(url),
             "http://api.aladhan.com/v1/timingsByCity?city=%s&country=%s&method=%d"
             "&day=%d&month=%d&year=%d",
             Settings::city, Settings::country, Settings::prayerMethod,
             t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);

    HTTPClient http;
    http.begin(url);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(10000);
    Serial.printf("[Prayer] Fetching city=%s country=%s date=%04d-%02d-%02d\n",
                  Settings::city, Settings::country,
                  t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    int code = http.GET();

    if (code != 200) {
        if (code < 0) {
            Serial.printf("[Prayer] Transport error %d (%s)\n",
                          code, HTTPClient::errorToString(code).c_str());
        } else {
            Serial.printf("[Prayer] HTTP error %d for city=%s country=%s\n",
                          code, Settings::city, Settings::country);
        }
        http.end();
        return false;
    }

    // Aladhan response can be several KB; parse only timings to keep memory low.
    StaticJsonDocument<160> filter;
    JsonObject timingsFilter = filter["data"].createNestedObject("timings");
    timingsFilter["Fajr"]    = true;
    timingsFilter["Sunrise"] = true;
    timingsFilter["Dhuhr"]   = true;
    timingsFilter["Asr"]     = true;
    timingsFilter["Maghrib"] = true;
    timingsFilter["Isha"]    = true;

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(
        doc,
        http.getStream(),
        DeserializationOption::Filter(filter)
    );
    http.end();

    if (err) {
        Serial.printf("[Prayer] JSON parse error: %s\n", err.c_str());
        return false;
    }

    JsonObject timings = doc["data"]["timings"];
    if (timings.isNull()) {
        Serial.println("[Prayer] Missing 'timings' in response.");
        return false;
    }

    // Copy only the relevant fields (strip " (GMT+3)" suffix if present)
    auto copyTime = [&](const char* key, char* dest) {
        const char* raw = timings[key] | "--:--";
        strncpy(dest, raw, 5);
        dest[5] = '\0';
    };

    copyTime("Fajr",    current.prayers[0].time);
    copyTime("Sunrise", current.prayers[1].time);
    copyTime("Dhuhr",   current.prayers[2].time);
    copyTime("Asr",     current.prayers[3].time);
    copyTime("Maghrib", current.prayers[4].time);
    copyTime("Isha",    current.prayers[5].time);

    current.valid     = true;
    current.fetchedAt = millis();
    Settings::markPrayerFetched(t);
    updateNextPrayer();

    Serial.printf("[Prayer] Fajr=%s  Dhuhr=%s  Maghrib=%s\n",
                  current.prayers[0].time,
                  current.prayers[2].time,
                  current.prayers[4].time);
    return true;
}

static bool fetch() {
    struct tm t;
    if (!getLocalTime(&t)) {
        Serial.println("[Prayer] Time not set — deferring fetch.");
        return false;
    }
    return fetchForTime(t);
}

// ---------------------------------------------------------------------------
// Should we refresh? Call from loop().
// ---------------------------------------------------------------------------
static bool needsRefreshForTime(const struct tm& t) {
    // Always fetch if we have no data in RAM (e.g. fresh boot after prior session).
    if (!current.valid) return true;
    return !Settings::isPrayerFetchedToday(t);
}

static bool needsRefresh() {
    if (!current.valid) return true;

    struct tm t;
    if (!getLocalTime(&t)) return true;

    return needsRefreshForTime(t);
}

} // namespace Prayer
