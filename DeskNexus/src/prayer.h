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
#include <WiFiClientSecure.h>
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
    char hijriDate[32] = "";   // e.g. "12 Dhul-Hijjah 1447" (empty until fetched)
    bool hijriValid    = false;
    // Reminder de-duplication state (not persisted — reset each day / on boot).
    int  stateDayKey             = -1;
    int  lastPreAlertPrayerIndex = -1;
    int  lastDueAlertPrayerIndex = -1;
    bool dueAlertPrimed          = false;  // suppresses a catch-up azan on boot
};

static Data current;

static unsigned long lastFetchAttemptMs  = 0;
static constexpr unsigned long FETCH_RETRY_INTERVAL_MS = 60000;  // 60 s backoff after failure

// Hijri month names (ASCII, indexed by Aladhan month number 1..12) — the TFT
// fonts have no diacritics, so we avoid the API's Latin month string.
static const char* const HIJRI_MONTHS[12] = {
    "Muharram", "Safar", "Rabi al-Awwal", "Rabi al-Thani",
    "Jumada al-Awwal", "Jumada al-Thani", "Rajab", "Sha'ban",
    "Ramadan", "Shawwal", "Dhul-Qa'dah", "Dhul-Hijjah",
};

enum ReminderEventType {
    REMINDER_NONE,
    REMINDER_PRE_ALERT,
    REMINDER_DUE,
};

struct ReminderEvent {
    ReminderEventType type = REMINDER_NONE;
    int prayerIndex = -1;
    int minutesLeft = -1;
};

// ---------------------------------------------------------------------------
// Percent-encode a query value so multi-word cities ("Abu Dhabi", "New York")
// and any non-ASCII characters produce a valid URL (RFC 3986 unreserved set is
// passed through; everything else becomes %XX).
// ---------------------------------------------------------------------------
static String urlEncode(const String& s) {
    static const char* hex = "0123456789ABCDEF";
    String out;
    out.reserve(s.length() * 3);
    for (unsigned int i = 0; i < s.length(); i++) {
        char c = s[i];
        if (isalnum((unsigned char)c) || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            out += c;
        } else {
            out += '%';
            out += hex[(c >> 4) & 0xF];
            out += hex[c & 0xF];
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Parse "HH:MM" string into minutes since midnight
// ---------------------------------------------------------------------------
static int toMinutes(const char* hhmm) {
    int h = 0, m = 0;
    sscanf(hhmm, "%d:%d", &h, &m);
    return h * 60 + m;
}

static bool isActionablePrayer(int index) {
    return index >= 0 && index < PRAYER_COUNT && index != 1;
}

static int dayKey(const struct tm& t) {
    return (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday;
}

// Forward declaration — defined further down, used by syncDailyState's callers.
static void updateNextPrayer();

// Reset the per-day reminder de-duplication state. No user-facing prayer
// tracking is kept any more — this only prevents an alert firing twice.
static void resetDailyState(const struct tm& t) {
    current.stateDayKey = dayKey(t);
    current.lastPreAlertPrayerIndex = -1;
    current.lastDueAlertPrayerIndex = -1;
    current.dueAlertPrimed = false;
    Serial.printf("[Prayer] Reminder state reset for %04d-%02d-%02d\n",
                  t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
}

static void syncDailyState(const struct tm& t) {
    if (current.stateDayKey != dayKey(t)) {
        resetDailyState(t);
    }
}

// Index of the soonest actionable prayer within the pre-alert window, or -1.
static int upcomingPrayerIndexForTime(const struct tm& t) {
    if (!current.valid) return -1;  // guard: prayer data not yet loaded
    int nowMin = t.tm_hour * 60 + t.tm_min;
    for (int i = 0; i < PRAYER_COUNT; i++) {
        if (!isActionablePrayer(i)) continue;
        int delta = toMinutes(current.prayers[i].time) - nowMin;
        if (delta > 0 && delta <= PRAYER_PRE_ALERT_MINUTES) {
            return i;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Reminder polling — stateless w.r.t. the user. Fires a pre-alert before each
// prayer and an azan (DUE) once when its time arrives. No snooze / prayed /
// missed concepts: prayers are simply shown, then announced on time.
// ---------------------------------------------------------------------------
static ReminderEvent pollReminderEvent() {
    ReminderEvent event;
    struct tm t;
    if (!getLocalTime(&t)) return event;

    syncDailyState(t);
    if (!current.valid) return event;
    int nowMin = t.tm_hour * 60 + t.tm_min;

    // Most recent actionable prayer whose time has arrived today.
    int dueIdx = -1;
    for (int i = 0; i < PRAYER_COUNT; i++) {
        if (isActionablePrayer(i) && toMinutes(current.prayers[i].time) <= nowMin) {
            dueIdx = i;
        }
    }

    // On the first poll after boot / midnight reset, adopt whatever has already
    // passed without firing — avoids a stale azan for a long-gone prayer.
    if (!current.dueAlertPrimed) {
        current.dueAlertPrimed = true;
        current.lastDueAlertPrayerIndex = dueIdx;
        return event;
    }

    // DUE: a new prayer's time just arrived.
    if (dueIdx >= 0 && dueIdx != current.lastDueAlertPrayerIndex) {
        current.lastDueAlertPrayerIndex = dueIdx;
        event.type = REMINDER_DUE;
        event.prayerIndex = dueIdx;
        event.minutesLeft = 0;
        Serial.printf("[Prayer] Due event: %s\n", current.prayers[dueIdx].name);
        return event;
    }

    // PRE-ALERT: a prayer is approaching within the pre-alert window.
    int upcomingIndex = upcomingPrayerIndexForTime(t);
    if (upcomingIndex >= 0 && current.lastPreAlertPrayerIndex != upcomingIndex) {
        current.lastPreAlertPrayerIndex = upcomingIndex;
        event.type = REMINDER_PRE_ALERT;
        event.prayerIndex = upcomingIndex;
        event.minutesLeft = toMinutes(current.prayers[upcomingIndex].time) - nowMin;
        Serial.printf("[Prayer] Pre-alert: %s in %d min\n",
                      current.prayers[upcomingIndex].name, event.minutesLeft);
        return event;
    }

    return event;
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
    syncDailyState(t);
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
    syncDailyState(t);
    int nowMin  = t.tm_hour * 60 + t.tm_min;
    int nextMin = toMinutes(current.prayers[current.nextIndex].time);
    if (nextMin <= nowMin) nextMin += 1440;   // next day
    return nextMin - nowMin;
}

// ---------------------------------------------------------------------------
// Fetch prayer times from Aladhan API (blocking)
// ---------------------------------------------------------------------------
static bool fetchForTime(const struct tm& t) {
    lastFetchAttemptMs = millis();
    String cityEnc    = urlEncode(String(Settings::city));
    String countryEnc = urlEncode(String(Settings::country));
    char url[320];
    snprintf(url, sizeof(url),
             "https://api.aladhan.com/v1/timingsByCity?city=%s&country=%s&method=%d"
             "&day=%d&month=%d&year=%d",
             cityEnc.c_str(), countryEnc.c_str(), Settings::prayerMethod,
             t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, url);
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

    // Aladhan response can be several KB; parse only the fields we need
    // (timings + the Hijri date) to keep memory low.
    StaticJsonDocument<320> filter;
    JsonObject dataFilter    = filter.createNestedObject("data");
    JsonObject timingsFilter = dataFilter.createNestedObject("timings");
    timingsFilter["Fajr"]    = true;
    timingsFilter["Sunrise"] = true;
    timingsFilter["Dhuhr"]   = true;
    timingsFilter["Asr"]     = true;
    timingsFilter["Maghrib"] = true;
    timingsFilter["Isha"]    = true;
    JsonObject hijriFilter   = dataFilter["date"].createNestedObject("hijri");
    hijriFilter["day"]                = true;
    hijriFilter["year"]               = true;
    hijriFilter["month"]["number"]    = true;

    StaticJsonDocument<768> doc;
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

    // Hijri date — Aladhan returns day/year as strings and month as a number.
    // We map the number to an ASCII month name (the TFT fonts lack diacritics).
    current.hijriValid = false;
    current.hijriDate[0] = '\0';
    JsonObject hijri = doc["data"]["date"]["hijri"];
    if (!hijri.isNull()) {
        const char* hDay = hijri["day"] | "";
        const char* hYear = hijri["year"] | "";
        int hMonth = hijri["month"]["number"] | 0;
        if (hMonth >= 1 && hMonth <= 12 && hDay[0] && hYear[0]) {
            snprintf(current.hijriDate, sizeof(current.hijriDate), "%s %s %s",
                     hDay, HIJRI_MONTHS[hMonth - 1], hYear);
            current.hijriValid = true;
        }
    }

    current.valid     = true;
    current.fetchedAt = millis();
    Settings::markPrayerFetched(t);
    syncDailyState(t);
    updateNextPrayer();

    Serial.printf("[Prayer] Fajr=%s  Dhuhr=%s  Maghrib=%s\n",
                  current.prayers[0].time,
                  current.prayers[2].time,
                  current.prayers[4].time);
    return true;
}

static bool fetch() {
    lastFetchAttemptMs = millis();
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
    if (!current.valid) {
        if (lastFetchAttemptMs > 0 &&
            (millis() - lastFetchAttemptMs) < FETCH_RETRY_INTERVAL_MS) {
            return false;
        }
        return true;
    }
    return !Settings::isPrayerFetchedToday(t);
}

static bool needsRefresh() {
    if (!current.valid) {
        if (lastFetchAttemptMs > 0 &&
            (millis() - lastFetchAttemptMs) < FETCH_RETRY_INTERVAL_MS) {
            return false;
        }
        return true;
    }

    struct tm t;
    if (!getLocalTime(&t)) return false;  // no time yet — skip rather than retry immediately

    return needsRefreshForTime(t);
}

} // namespace Prayer
