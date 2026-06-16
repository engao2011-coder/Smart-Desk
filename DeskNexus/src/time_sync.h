/*
 * time_sync.h — NTP Time Synchronisation Helper
 *
 * Wraps configTime() with a reapply guard so calling code can invoke
 * apply() freely without spamming NTP servers or triggering the
 * "addApbChangeCallback" duplicate-registration warning that fires when
 * configTime() is called more than once in quick succession.
 *
 * apply(utcOffset, force)
 *   Calls configTime() only when the UTC offset has changed or force=true.
 *   A 15-second guard window prevents repeated calls within the same boot cycle.
 *
 * readLocalTimeStable(out, consecutiveReads, settleMs)
 *   Reads local time N times with a short settle delay between reads and
 *   returns the last sample. Avoids jitter from a single getLocalTime()
 *   call made immediately after an NTP sync.
 *
 * Usage in main.cpp:
 *   setup():             TimeSync::apply(Settings::utcOffset);
 *   After auto-detect:   TimeSync::apply(detectedOffset, /*force=*\/true);
 */

#pragma once

#include <Arduino.h>
#include <limits.h>
#include <time.h>
#include "config.h"

namespace TimeSync {

static long appliedUtcOffset = LONG_MIN;
static unsigned long lastApplyMs = 0;
static constexpr unsigned long REAPPLY_GUARD_MS = 15000;

static bool apply(long utcOffset, bool force = false) {
    unsigned long now = millis();
    if (!force && appliedUtcOffset == utcOffset && lastApplyMs != 0 &&
        (now - lastApplyMs) < REAPPLY_GUARD_MS) {
        return false;
    }

    configTime(utcOffset, NTP_DST_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2);
    appliedUtcOffset = utcOffset;
    lastApplyMs = now;
    return true;
}

static bool readLocalTimeStable(struct tm& out,
                                uint8_t consecutiveReads = 2,
                                unsigned long settleMs = 50) {
    if (consecutiveReads == 0) consecutiveReads = 1;

    struct tm sample = {};
    for (uint8_t i = 0; i < consecutiveReads; i++) {
        if (!getLocalTime(&sample)) return false;
        if (settleMs > 0 && i + 1 < consecutiveReads) delay(settleMs);
    }

    out = sample;
    return true;
}

static bool waitForSync(struct tm& out,
                        unsigned long timeoutMs = 10000,
                        unsigned long pollMs = 200) {
    unsigned long start = millis();
    while ((millis() - start) < timeoutMs) {
        if (readLocalTimeStable(out)) return true;
        delay(pollMs);
    }
    return readLocalTimeStable(out);
}

} // namespace TimeSync