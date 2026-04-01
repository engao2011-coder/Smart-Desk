/*
 * location_time.h — Automatic city/timezone detection from internet
 *
 * Runs at boot after WiFi connect and updates Settings::city/country/utcOffset.
 */

#pragma once

#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <limits.h>
#include <time.h>
#include "config.h"
#include "settings.h"

namespace LocationTime {

struct GeoInfo {
    String city;
    String country;
    String timezoneId;
};

static bool getJson(const String& url, JsonDocument& doc) {
    HTTPClient http;
    http.begin(url);
    http.setTimeout(AUTO_DETECT_HTTP_TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    int code = http.GET();
    if (code != 200) {
        Serial.printf("[AutoDetect] HTTP %d for %s\n", code, url.c_str());
        http.end();
        return false;
    }

    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();
    if (err) {
        Serial.printf("[AutoDetect] JSON parse error: %s\n", err.c_str());
        return false;
    }
    return true;
}

static bool fetchGeo(GeoInfo& out) {
    StaticJsonDocument<1024> doc;
    if (!getJson(String(AUTO_DETECT_GEO_URL), doc)) return false;

    bool ok = doc["success"] | false;
    if (!ok) {
        Serial.println("[AutoDetect] Geo lookup did not succeed.");
        return false;
    }

    out.city = String(doc["city"] | "");
    out.country = String(doc["country_code"] | "");
    out.timezoneId = String(doc["timezone"]["id"] | "");

    if (out.city.length() == 0 || out.timezoneId.length() == 0) {
        Serial.println("[AutoDetect] Geo lookup missing city/timezone.");
        return false;
    }

    return true;
}

static bool fetchUtcOffset(const String& timezoneId, long& utcOffsetSec) {
    String url = String(AUTO_DETECT_TZ_API_BASE) + timezoneId;
    StaticJsonDocument<1024> doc;
    if (!getJson(url, doc)) return false;

    long rawOffset = doc["raw_offset"] | LONG_MIN;
    long dstOffset = doc["dst_offset"] | LONG_MIN;

    if (rawOffset == LONG_MIN || dstOffset == LONG_MIN) {
        Serial.println("[AutoDetect] Timezone response missing offsets.");
        return false;
    }

    long total = rawOffset + dstOffset;
    if (total < -43200 || total > 50400) {
        Serial.printf("[AutoDetect] Timezone offset out of range: %ld\n", total);
        return false;
    }

    utcOffsetSec = total;
    return true;
}

static void setStatus(bool ok, const String& msg) {
    Settings::autoDetectLastOk = ok;
    Settings::autoDetectLastEpoch = time(nullptr);

    String safe = msg;
    safe.trim();
    if (safe.length() == 0) safe = ok ? "Success" : "Failed";

    safe.toCharArray(Settings::autoDetectStatus, sizeof(Settings::autoDetectStatus));
    Settings::autoDetectStatus[sizeof(Settings::autoDetectStatus) - 1] = '\0';
}

static bool detectAndApply() {
#if AUTO_DETECT_LOCATION_TIME
    GeoInfo geo;
    if (!fetchGeo(geo)) {
        setStatus(false, "Geo lookup failed");
        Settings::save();
        return false;
    }

    long detectedOffset = 0;
    if (!fetchUtcOffset(geo.timezoneId, detectedOffset)) {
        setStatus(false, "Timezone lookup failed");
        Settings::save();
        return false;
    }

    if (geo.city.length() < sizeof(Settings::city)) {
        strncpy(Settings::city, geo.city.c_str(), sizeof(Settings::city) - 1);
        Settings::city[sizeof(Settings::city) - 1] = '\0';
    }

    if (geo.country.length() > 0 && geo.country.length() < sizeof(Settings::country)) {
        geo.country.toUpperCase();
        strncpy(Settings::country, geo.country.c_str(), sizeof(Settings::country) - 1);
        Settings::country[sizeof(Settings::country) - 1] = '\0';
    }

    Settings::utcOffset = detectedOffset;

    String status = "OK " + geo.city + " UTC " + String(detectedOffset);
    setStatus(true, status);
    Settings::save();

    Serial.printf("[AutoDetect] city=%s country=%s utc=%ld tz=%s\n",
                  Settings::city, Settings::country,
                  Settings::utcOffset, geo.timezoneId.c_str());
    return true;
#else
    setStatus(false, "Auto-detect disabled");
    return false;
#endif
}

} // namespace LocationTime
