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
    int  stateDayKey   = -1;
    int  prayedMask    = 0;
    int  snoozedPrayerIndex = -1;
    long snoozeUntilEpoch = 0;
    int  snoozeCount = 0;            // how many times current prayer was snoozed today
    bool snoozeJustExpired = false;  // set by syncDailyState; consumed by pollReminderEvent
    int  lastPreAlertPrayerIndex = -1;
    int  lastDueAlertPrayerIndex = -1;
    int  lastReminderPrayerIndex = -1;
    long lastReminderEpoch = 0;
};

static Data current;

static unsigned long lastFetchAttemptMs  = 0;
static constexpr unsigned long FETCH_RETRY_INTERVAL_MS = 60000;  // 60 s backoff after failure

enum ReminderEventType {
    REMINDER_NONE,
    REMINDER_PRE_ALERT,
    REMINDER_DUE,
    REMINDER_REPEAT,
    REMINDER_SNOOZE_EXPIRED,  // snooze window elapsed; shows banner, not full Azan screen
};

enum RowState {
    ROW_NORMAL,
    ROW_UPCOMING,
    ROW_PENDING,
    ROW_SNOOZED,
    ROW_DONE,
    ROW_MISSED,
};

struct ReminderEvent {
    ReminderEventType type = REMINDER_NONE;
    int prayerIndex = -1;
    int minutesLeft = -1;
};

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

static long epochForLocalTime(const struct tm& t) {
    struct tm copy = t;
    return (long)mktime(&copy);
}

// Forward declarations — these are defined further down in this file but are
// needed by syncDailyState which must appear before them (loadPersistentState
// calls syncDailyState, and loadPersistentState is called from setup()).
static bool isPrayed(int index);
static int  activePendingIndexForTime(const struct tm& t);
static void updateNextPrayer();

static void persistState() {
    Settings::savePrayerState(
        current.stateDayKey,
        current.prayedMask,
        current.snoozedPrayerIndex,
        current.snoozeUntilEpoch,
        current.snoozeCount
    );
}

static void resetDailyState(const struct tm& t, bool persist = true) {
    current.stateDayKey = dayKey(t);
    current.prayedMask = 0;
    current.snoozedPrayerIndex = -1;
    current.snoozeUntilEpoch = 0;
    current.snoozeCount = 0;
    current.snoozeJustExpired = false;
    current.lastPreAlertPrayerIndex = -1;
    current.lastDueAlertPrayerIndex = -1;
    current.lastReminderPrayerIndex = -1;
    current.lastReminderEpoch = 0;
    if (persist) {
        persistState();
    }
    Serial.printf("[Prayer] Daily state reset for %04d-%02d-%02d\n",
                  t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
}

static void syncDailyState(const struct tm& t) {
    int todayKey = dayKey(t);
    if (current.stateDayKey != todayKey) {
        resetDailyState(t, true);
    }
    if (current.snoozedPrayerIndex >= 0 && current.snoozeUntilEpoch > 0) {
        long nowEpoch = epochForLocalTime(t);
        if (nowEpoch >= current.snoozeUntilEpoch) {
            // Flag for a soft SNOOZE_EXPIRED event so the next pollReminderEvent()
            // emits a banner only; the Azan screen re-fires on the subsequent pass.
            // NOTE: snoozedPrayerIndex is intentionally kept so the orphaned-snooze
            // block below can detect when a *different* prayer becomes pending and
            // reset snoozeCount — preventing "No More" from bleeding into un-snoozed
            // later prayers. isSnoozed() validates via snoozeUntilEpoch > 0, so it
            // correctly returns false after expiry regardless.
            current.snoozeUntilEpoch = 0;
            current.snoozeJustExpired = true;
            persistState();
            Serial.println("[Prayer] Snooze expired.");
        }
    }
    // Clear orphaned snooze: if a later prayer is now the active pending one,
    // the previously-snoozed prayer's snooze record is no longer meaningful.
    if (current.snoozedPrayerIndex >= 0) {
        int pendingNow = activePendingIndexForTime(t);
        if (pendingNow >= 0 && pendingNow != current.snoozedPrayerIndex) {
            Serial.printf("[Prayer] Orphaned snooze cleared (was idx %d, now pending idx %d).\n",
                          current.snoozedPrayerIndex, pendingNow);
            current.snoozedPrayerIndex = -1;
            current.snoozeUntilEpoch = 0;
            current.snoozeCount = 0;
            persistState();
        }
    }
}

static void loadPersistentState() {
    current.stateDayKey        = Settings::prayerStateDayKey;
    current.prayedMask         = Settings::prayerPrayedMask;
    current.snoozedPrayerIndex = Settings::prayerSnoozeIndex;
    current.snoozeUntilEpoch   = Settings::prayerSnoozeUntil;
    current.snoozeCount        = Settings::prayerSnoozeCount;

    struct tm t;
    if (getLocalTime(&t)) {
        syncDailyState(t);
    }
}

static bool isPrayed(int index) {
    if (!isActionablePrayer(index)) return false;
    return (current.prayedMask & (1 << index)) != 0;
}

static bool isSnoozed(int index) {
    if (index < 0 || index != current.snoozedPrayerIndex || current.snoozeUntilEpoch <= 0)
        return false;
    long now = (long)time(nullptr);
    // Range guard: if snooze epoch is more than 24h in the future, treat as corrupt
    if (current.snoozeUntilEpoch > now + 86400L) {
        current.snoozeUntilEpoch = 0;
        current.snoozedPrayerIndex = -1;
        current.snoozeCount = 0;
        return false;
    }
    return now < current.snoozeUntilEpoch;
}

static int activePendingIndexForTime(const struct tm& t) {
    if (!current.valid) return -1;  // guard: pray data not yet loaded
    int nowMin = t.tm_hour * 60 + t.tm_min;
    int latestDue = -1;
    for (int i = 0; i < PRAYER_COUNT; i++) {
        if (!isActionablePrayer(i) || isPrayed(i)) continue;
        if (toMinutes(current.prayers[i].time) <= nowMin) {
            latestDue = i;
        }
    }
    return latestDue;
}

static int pendingPrayerIndex() {
    struct tm t;
    if (!getLocalTime(&t)) return -1;
    syncDailyState(t);
    return activePendingIndexForTime(t);
}

static int upcomingPrayerIndexForTime(const struct tm& t) {
    if (!current.valid) return -1;  // guard: prayer data not yet loaded
    if (activePendingIndexForTime(t) >= 0) return -1;

    int nowMin = t.tm_hour * 60 + t.tm_min;
    for (int i = 0; i < PRAYER_COUNT; i++) {
        if (!isActionablePrayer(i) || isPrayed(i)) continue;
        int delta = toMinutes(current.prayers[i].time) - nowMin;
        if (delta > 0 && delta <= PRAYER_PRE_ALERT_MINUTES) {
            return i;
        }
    }
    return -1;
}

static int upcomingPrayerIndex() {
    struct tm t;
    if (!getLocalTime(&t)) return -1;
    syncDailyState(t);
    return upcomingPrayerIndexForTime(t);
}

static bool markPrayed(int prayerIndex) {
    if (!isActionablePrayer(prayerIndex)) return false;

    struct tm t;
    if (getLocalTime(&t)) {
        syncDailyState(t);
    }

    current.prayedMask |= (1 << prayerIndex);
    if (current.snoozedPrayerIndex == prayerIndex) {
        current.snoozedPrayerIndex = -1;
        current.snoozeUntilEpoch = 0;
        current.snoozeCount = 0;
    }
    persistState();
    Serial.printf("[Prayer] Marked prayed: %s\n", current.prayers[prayerIndex].name);
    updateNextPrayer();  // recalculate immediately so next-prayer display is correct
    return true;
}

static bool markPendingPrayed() {
    int prayerIndex = pendingPrayerIndex();
    if (prayerIndex < 0) return false;
    return markPrayed(prayerIndex);
}

// forPrayerIndex: explicit prayer to snooze (-1 = auto-resolve from active pending).
// minutes: snooze duration; defaults to PRAYER_SNOOZE_MINUTES.
static bool snoozePendingPrayer(int forPrayerIndex = -1, int minutes = PRAYER_SNOOZE_MINUTES) {
    struct tm t;
    if (!getLocalTime(&t)) return false;
    syncDailyState(t);

    // Resolve which prayer to snooze: explicit caller wins, otherwise use latest due.
    int prayerIndex = (forPrayerIndex >= 0) ? forPrayerIndex : activePendingIndexForTime(t);
    if (prayerIndex < 0 || !isActionablePrayer(prayerIndex) || isPrayed(prayerIndex)) return false;

    // Enforce daily snooze cap per prayer.
    if (current.snoozeCount >= PRAYER_MAX_SNOOZE_COUNT) {
        Serial.printf("[Prayer] Snooze cap reached (%d/%d) for %s — ignored.\n",
                      current.snoozeCount, PRAYER_MAX_SNOOZE_COUNT,
                      current.prayers[prayerIndex].name);
        return false;
    }

    // Defense-in-depth: do not overwrite an active snooze for this prayer.
    if (isSnoozed(prayerIndex)) return false;

    long nowEpoch = epochForLocalTime(t);
    current.snoozedPrayerIndex = prayerIndex;
    current.snoozeUntilEpoch = nowEpoch + (minutes * 60L);
    ++current.snoozeCount;
    persistState();
    Serial.printf("[Prayer] Snoozed %s until epoch %ld (snooze %d/%d)\n",
                  current.prayers[prayerIndex].name,
                  current.snoozeUntilEpoch,
                  current.snoozeCount,
                  PRAYER_MAX_SNOOZE_COUNT);
    return true;
}

static void formatEpochTime(long epochValue, char* buffer, size_t bufferLen) {
    if (epochValue <= 0 || bufferLen < 6) {
        snprintf(buffer, bufferLen, "--:--");
        return;
    }

    time_t raw = (time_t)epochValue;
    struct tm* local = localtime(&raw);
    if (!local) {
        snprintf(buffer, bufferLen, "--:--");
        return;
    }

    snprintf(buffer, bufferLen, "%02d:%02d", local->tm_hour, local->tm_min);
}

static bool snoozedUntilText(char* buffer, size_t bufferLen) {
    if (current.snoozedPrayerIndex < 0 || current.snoozeUntilEpoch <= 0) {
        return false;
    }
    formatEpochTime(current.snoozeUntilEpoch, buffer, bufferLen);
    return true;
}

static RowState rowStateForIndex(int prayerIndex) {
    if (!current.valid || prayerIndex < 0 || prayerIndex >= PRAYER_COUNT) {
        return ROW_NORMAL;
    }

    struct tm t;
    if (!getLocalTime(&t)) return ROW_NORMAL;
    syncDailyState(t);

    if (!isActionablePrayer(prayerIndex)) {
        return (prayerIndex == current.nextIndex) ? ROW_UPCOMING : ROW_NORMAL;
    }

    int pendingIndex = activePendingIndexForTime(t);
    int upcomingIndex = upcomingPrayerIndexForTime(t);
    int prayerMin = toMinutes(current.prayers[prayerIndex].time);
    int nowMin = t.tm_hour * 60 + t.tm_min;

    if (isPrayed(prayerIndex) && prayerMin <= nowMin) {
        return ROW_DONE;
    }

    if (prayerIndex == pendingIndex) {
        return isSnoozed(prayerIndex) ? ROW_SNOOZED : ROW_PENDING;
    }

    // Keep the computed next prayer highlighted, including post-Isha rollover to next-day Fajr.
    if (prayerIndex == current.nextIndex) {
        return ROW_UPCOMING;
    }

    if (!isPrayed(prayerIndex) && prayerMin <= nowMin) {
        return ROW_MISSED;
    }

    if (prayerIndex == upcomingIndex) {
        return ROW_UPCOMING;
    }

    return ROW_NORMAL;
}

static ReminderEvent pollReminderEvent() {
    ReminderEvent event;
    struct tm t;
    if (!getLocalTime(&t)) return event;

    syncDailyState(t);
    long nowEpoch = epochForLocalTime(t);

    // When a snooze has just expired, emit a soft banner event.
    // Clearing lastDueAlertPrayerIndex here lets the next poll pass re-fire REMINDER_DUE
    // (which shows the full Azan screen), giving the user a moment to respond first.
    if (current.snoozeJustExpired) {
        current.snoozeJustExpired = false;
        int expiredIndex = current.lastDueAlertPrayerIndex;
        current.lastDueAlertPrayerIndex = -1;  // allow REMINDER_DUE to re-fire next pass
        if (expiredIndex >= 0) {
            event.type = REMINDER_SNOOZE_EXPIRED;
            event.prayerIndex = expiredIndex;
            event.minutesLeft = 0;
            Serial.printf("[Prayer] Snooze-expired event: %s\n", current.prayers[expiredIndex].name);
            return event;
        }
        // expiredIndex < 0 (boot edge case): fall through to normal DUE logic below.
    }

    int pendingIndex = activePendingIndexForTime(t);

    if (pendingIndex >= 0) {
        if (current.lastDueAlertPrayerIndex != pendingIndex) {
            current.lastDueAlertPrayerIndex = pendingIndex;
            current.lastReminderPrayerIndex = pendingIndex;
            current.lastReminderEpoch = nowEpoch;
            event.type = REMINDER_DUE;
            event.prayerIndex = pendingIndex;
            event.minutesLeft = 0;
            Serial.printf("[Prayer] Due event: %s\n", current.prayers[pendingIndex].name);
            return event;
        }

        if (!isSnoozed(pendingIndex) &&
            current.lastReminderPrayerIndex == pendingIndex &&
            (nowEpoch - current.lastReminderEpoch) >= (PRAYER_REMINDER_MINUTES * 60L)) {
            current.lastReminderEpoch = nowEpoch;
            event.type = REMINDER_REPEAT;
            event.prayerIndex = pendingIndex;
            event.minutesLeft = 0;
            Serial.printf("[Prayer] Repeat reminder: %s\n", current.prayers[pendingIndex].name);
            return event;
        }

        return event;
    }

    int upcomingIndex = upcomingPrayerIndexForTime(t);
    if (upcomingIndex >= 0 && current.lastPreAlertPrayerIndex != upcomingIndex) {
        current.lastPreAlertPrayerIndex = upcomingIndex;
        event.type = REMINDER_PRE_ALERT;
        event.prayerIndex = upcomingIndex;
        event.minutesLeft = toMinutes(current.prayers[upcomingIndex].time) - (t.tm_hour * 60 + t.tm_min);
        Serial.printf("[Prayer] Pre-alert: %s in %d min\n",
                      current.prayers[upcomingIndex].name,
                      event.minutesLeft);
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
// Index of the current/most-recent prayer (pending, due, or last past prayer).
// Used by the Home page to show "current prayer" status.
// ---------------------------------------------------------------------------
static int currentOrLastPrayerIndex() {
    if (!current.valid) return -1;
    struct tm t;
    if (!getLocalTime(&t)) return -1;
    syncDailyState(t);

    // If a prayer is pending (due but not prayed), show that one
    int pending = activePendingIndexForTime(t);
    if (pending >= 0) return pending;

    // Otherwise find the most recent prayer that has passed (prayed or not)
    int nowMin = t.tm_hour * 60 + t.tm_min;
    int lastPast = -1;
    for (int i = 0; i < PRAYER_COUNT; i++) {
        if (toMinutes(current.prayers[i].time) <= nowMin) {
            lastPast = i;
        }
    }
    // Pre-Fajr: no prayer has passed today yet — show Isha (last of previous day)
    if (lastPast < 0) lastPast = PRAYER_COUNT - 1;
    return lastPast;
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
    char url[256];
    snprintf(url, sizeof(url),
             "https://api.aladhan.com/v1/timingsByCity?city=%s&country=%s&method=%d"
             "&day=%d&month=%d&year=%d",
             Settings::city, Settings::country, Settings::prayerMethod,
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
