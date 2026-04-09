/*
 * weather.h — OpenWeatherMap Current-Weather Integration
 *
 * Fetches current weather data for the configured city and exposes a
 * simple struct so the UI can display temperature, condition, and icon.
 *
 * API used: https://api.openweathermap.org/data/2.5/weather
 */

#pragma once

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "config.h"
#include "settings.h"

namespace Weather {

// ---------------------------------------------------------------------------
// Data model
// ---------------------------------------------------------------------------
enum FetchState {
    WEATHER_NEVER,      // never attempted
    WEATHER_NO_KEY,     // API key missing or is the placeholder default
    WEATHER_NET_ERROR,  // HTTP error or JSON parse failure
    WEATHER_OK,         // last fetch succeeded
};

struct Data {
    float  temp         = 0.0f;   // Temperature in configured units (°C or °F)
    float  feelsLike    = 0.0f;   // Feels-like temperature in same units
    float  humidity     = 0.0f;   // %
    float  windSpeedMs  = 0.0f;   // m/s
    int    pressureHpa  = 0;
    int    visibility   = 0;      // metres
    String description  = "--";
    String iconCode     = "01d";  // OWM icon code (e.g. "01d" = clear sky day)
    String cityName     = OWM_CITY_NAME;
    bool   valid        = false;
    FetchState fetchState = WEATHER_NEVER;
    unsigned long fetchedAt = 0;  // millis() when last fetched
};

static Data current;

// ---------------------------------------------------------------------------
// 5-day forecast data
// ---------------------------------------------------------------------------
struct DayForecast {
    char   dayName[4]  = "";      // "Mon", "Tue", etc.
    float  tempHi      = -999.0f;
    float  tempLo      =  999.0f;
    String iconCode    = "01d";   // dominant weather icon for the day
    bool   valid       = false;
};

struct ForecastData {
    DayForecast days[FORECAST_DAYS];
    int   dayCount       = 0;
    bool  valid          = false;
    bool  alertTriggered = false;  // significant-change alert flag
    unsigned long fetchedAt = 0;
};

static ForecastData forecast;

static unsigned long lastFetchAttemptMs  = 0;
static unsigned long lastForecastFetchMs = 0;
static constexpr unsigned long FETCH_RETRY_INTERVAL_MS = 60000;  // 60 s backoff after failure

// ---------------------------------------------------------------------------
// Map OWM icon code → simple emoji / text label for display
// ---------------------------------------------------------------------------
static const char* iconLabel(const String& code) {
    if (code.startsWith("01")) return "Sunny";
    if (code.startsWith("02")) return "Partly Cloudy";
    if (code.startsWith("03")) return "Cloudy";
    if (code.startsWith("04")) return "Overcast";
    if (code.startsWith("09")) return "Showers";
    if (code.startsWith("10")) return "Rainy";
    if (code.startsWith("11")) return "Thunderstorm";
    if (code.startsWith("13")) return "Snowy";
    if (code.startsWith("50")) return "Foggy";
    return "N/A";
}

// ---------------------------------------------------------------------------
// Map OWM icon code → colour accent for UI (RGB565)
// ---------------------------------------------------------------------------
static uint16_t iconColor(const String& code) {
    if (code.startsWith("01")) return 0xFEE0;  // golden yellow — sunny
    if (code.startsWith("02") ||
        code.startsWith("03") ||
        code.startsWith("04")) return 0xC618;  // light grey — cloudy
    if (code.startsWith("09") ||
        code.startsWith("10")) return 0x5D1F;  // steel blue — rain
    if (code.startsWith("11")) return 0xF800;  // red — storm
    if (code.startsWith("13")) return 0xFFFF;  // white — snow
    if (code.startsWith("50")) return 0xC618;  // grey — fog
    return 0x7BEF;                             // silver fallback
}

// ---------------------------------------------------------------------------
// Fetch (blocking, call from a task or between frames)
// ---------------------------------------------------------------------------
static bool fetch() {
    lastFetchAttemptMs = millis();
    String apiKey = String(Settings::owmApiKey);
    apiKey.trim();

    if (apiKey.length() == 0 || apiKey == "YOUR_OPENWEATHERMAP_API_KEY") {
        Serial.println("[Weather] API key not configured — skipping fetch.");
        current.fetchState = WEATHER_NO_KEY;
        return false;
    }

    String url = "https://api.openweathermap.org/data/2.5/weather?q=";
    url += String(Settings::city) + "," + String(Settings::country);
    url += "&appid=" + apiKey;
    url += "&units=" + String(Settings::owmUnits);
    url += "&lang="  + String(OWM_LANG);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(8000);
    Serial.printf("[Weather] Fetching city=%s country=%s\n", Settings::city, Settings::country);
    int code = http.GET();

    if (code != 200) {
        if (code < 0) {
            Serial.printf("[Weather] Transport error %d (%s)\n",
                          code, HTTPClient::errorToString(code).c_str());
        } else {
            Serial.printf("[Weather] HTTP error %d for city=%s country=%s\n",
                          code, Settings::city, Settings::country);
        }
        if (code == 401) {
            String body = http.getString();
            Serial.printf("[Weather] Auth failure: %s\n", body.c_str());
            current.fetchState = WEATHER_NO_KEY;  // 401 = bad key
        } else {
            current.fetchState = WEATHER_NET_ERROR;
        }
        http.end();
        return false;
    }

    // Parse JSON response
    // OWM current-weather response is ~500 bytes; 1024 bytes is sufficient.
    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();

    if (err) {
        Serial.printf("[Weather] JSON parse error: %s\n", err.c_str());
        current.fetchState = WEATHER_NET_ERROR;
        return false;
    }

    current.temp        = doc["main"]["temp"]       | 0.0f;
    current.feelsLike   = doc["main"]["feels_like"] | 0.0f;
    current.humidity    = doc["main"]["humidity"]   | 0.0f;
    current.pressureHpa = doc["main"]["pressure"]   | 0;
    current.windSpeedMs = doc["wind"]["speed"]      | 0.0f;
    current.visibility  = doc["visibility"]         | 0;
    current.cityName    = String(doc["name"] | OWM_CITY_NAME);

    JsonArray weather = doc["weather"].as<JsonArray>();
    if (!weather.isNull() && weather.size() > 0) {
        current.description = String((const char*)weather[0]["description"]);
        current.iconCode    = String((const char*)weather[0]["icon"]);
    }

    current.valid     = true;
    current.fetchedAt = millis();
    current.fetchState = WEATHER_OK;

    struct tm t;
    if (getLocalTime(&t)) {
        Settings::markWeatherFetched(t);
    }

    Serial.printf("[Weather] %.1f° %s  %s\n",
                  current.temp, current.description.c_str(), current.iconCode.c_str());
    return true;
}

// ---------------------------------------------------------------------------
// Should we refresh? Call from loop().
// ---------------------------------------------------------------------------
static bool needsRefresh() {
    if (!current.valid) {
        // Backoff: avoid hammering the API after a failed fetch.
        if (lastFetchAttemptMs > 0 &&
            (millis() - lastFetchAttemptMs) < FETCH_RETRY_INTERVAL_MS) {
            return false;
        }
        return true;
    }

    struct tm t;
    if (!getLocalTime(&t)) return false;  // no time yet — skip rather than retry immediately

    return !Settings::isWeatherFetchedThisHour(t);
}

// ---------------------------------------------------------------------------
// 5-day forecast fetch (OWM free /data/2.5/forecast — 3-hour intervals)
// ---------------------------------------------------------------------------
static bool fetchForecast() {
    lastForecastFetchMs = millis();
    String apiKey = String(Settings::owmApiKey);
    apiKey.trim();

    if (apiKey.length() == 0 || apiKey == "YOUR_OPENWEATHERMAP_API_KEY") {
        return false;
    }

    String url = "https://api.openweathermap.org/data/2.5/forecast?q=";
    url += String(Settings::city) + "," + String(Settings::country);
    url += "&appid=" + apiKey;
    url += "&units=" + String(Settings::owmUnits);
    url += "&cnt=40";

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(10000);
    Serial.println("[Forecast] Fetching 5-day forecast...");
    int code = http.GET();

    if (code != 200) {
        Serial.printf("[Forecast] HTTP error %d\n", code);
        http.end();
        return false;
    }

    // Use a filter to keep only the fields we need — saves ~10KB of heap
    StaticJsonDocument<256> filter;
    filter["list"][0]["dt"] = true;
    filter["list"][0]["main"]["temp_min"] = true;
    filter["list"][0]["main"]["temp_max"] = true;
    filter["list"][0]["weather"][0]["icon"] = true;

    // The filtered response needs ~6KB for up to 40 entries
    DynamicJsonDocument doc(6144);
    DeserializationError err = deserializeJson(doc, http.getStream(),
                                               DeserializationOption::Filter(filter));
    http.end();

    if (err) {
        Serial.printf("[Forecast] JSON parse error: %s\n", err.c_str());
        return false;
    }

    JsonArray list = doc["list"].as<JsonArray>();
    if (list.isNull() || list.size() == 0) {
        Serial.println("[Forecast] Empty list");
        return false;
    }
    Serial.printf("[Forecast] Parsed %d entries\n", list.size());

    // ── Group 3-hour intervals by calendar day ──
    // We track up to FORECAST_DAYS days. Day boundaries use the dt timestamp.
    struct DayBucket {
        int dayOfYear = -1;
        float hi = -999.0f;
        float lo =  999.0f;
        char dayName[4] = "";
        // Track dominant icon: count occurrences per icon prefix (2 chars)
        char icons[8][4] = {};    // up to 8 distinct prefixes
        int  iconCounts[8] = {};
        int  iconN = 0;
        char bestIcon[8] = "01d"; // fallback
    };
    DayBucket buckets[FORECAST_DAYS];
    int bucketCount = 0;

    // Get today's day-of-year to skip "today" entries
    struct tm now;
    if (!getLocalTime(&now)) {
        Serial.println("[Forecast] No local time");
        return false;
    }
    int todayDOY = now.tm_yday;

    for (JsonObject entry : list) {
        time_t dt = entry["dt"] | 0L;
        if (dt == 0) continue;
        struct tm entryTm;
        localtime_r(&dt, &entryTm);
        int doy = entryTm.tm_yday;

        // Skip entries for "today"
        if (doy == todayDOY) continue;

        // Find or create bucket for this day
        int bIdx = -1;
        for (int b = 0; b < bucketCount; b++) {
            if (buckets[b].dayOfYear == doy) { bIdx = b; break; }
        }
        if (bIdx < 0) {
            if (bucketCount >= FORECAST_DAYS) continue;
            bIdx = bucketCount++;
            buckets[bIdx].dayOfYear = doy;
            // Day name
            strftime(buckets[bIdx].dayName, sizeof(buckets[bIdx].dayName), "%a", &entryTm);
        }

        float tMin = entry["main"]["temp_min"] | -999.0f;
        float tMax = entry["main"]["temp_max"] | -999.0f;
        if (tMax > buckets[bIdx].hi) buckets[bIdx].hi = tMax;
        if (tMin < buckets[bIdx].lo) buckets[bIdx].lo = tMin;

        // Icon tracking
        const char* icon = entry["weather"][0]["icon"] | "01d";
        char prefix[3] = { icon[0], icon[1], '\0' };
        bool found = false;
        for (int k = 0; k < buckets[bIdx].iconN; k++) {
            if (strcmp(buckets[bIdx].icons[k], prefix) == 0) {
                buckets[bIdx].iconCounts[k]++;
                found = true;
                break;
            }
        }
        if (!found && buckets[bIdx].iconN < 8) {
            int n = buckets[bIdx].iconN++;
            strncpy(buckets[bIdx].icons[n], prefix, 3);
            buckets[bIdx].iconCounts[n] = 1;
        }
    }

    // ── Build forecast result ──
    forecast.dayCount = bucketCount;
    forecast.alertTriggered = false;

    for (int b = 0; b < bucketCount; b++) {
        // Pick dominant icon (most frequent prefix) + append "d" for day variant
        int bestIdx = 0;
        for (int k = 1; k < buckets[b].iconN; k++) {
            if (buckets[b].iconCounts[k] > buckets[b].iconCounts[bestIdx]) bestIdx = k;
        }
        if (buckets[b].iconN > 0) {
            snprintf(buckets[b].bestIcon, sizeof(buckets[b].bestIcon),
                     "%sd", buckets[b].icons[bestIdx]);
        }

        forecast.days[b].valid   = true;
        forecast.days[b].tempHi  = buckets[b].hi;
        forecast.days[b].tempLo  = buckets[b].lo;
        forecast.days[b].iconCode = String(buckets[b].bestIcon);
        strncpy(forecast.days[b].dayName, buckets[b].dayName, 3);
        forecast.days[b].dayName[3] = '\0';

        Serial.printf("[Forecast] %s: %.0f/%.0f %s\n",
                      forecast.days[b].dayName,
                      forecast.days[b].tempHi, forecast.days[b].tempLo,
                      forecast.days[b].iconCode.c_str());

        // ── Alert detection ──
        // 1) Significant temp swing vs current weather
        if (current.valid) {
            if (fabsf(buckets[b].hi - current.temp) >= FORECAST_TEMP_SWING_ALERT ||
                fabsf(buckets[b].lo - current.temp) >= FORECAST_TEMP_SWING_ALERT) {
                forecast.alertTriggered = true;
            }
        }
        // 2) Condition shift: rain/storm/snow incoming when current is clear/cloudy
        if (current.valid) {
            bool currentClear = current.iconCode.startsWith("01") ||
                                current.iconCode.startsWith("02") ||
                                current.iconCode.startsWith("03") ||
                                current.iconCode.startsWith("04");
            String fIcon = forecast.days[b].iconCode;
            bool forecastBad = fIcon.startsWith("09") || fIcon.startsWith("10") ||
                               fIcon.startsWith("11") || fIcon.startsWith("13");
            if (currentClear && forecastBad) {
                forecast.alertTriggered = true;
            }
        }
    }

    // Clear unused days
    for (int b = bucketCount; b < FORECAST_DAYS; b++) {
        forecast.days[b].valid = false;
    }

    forecast.valid     = (bucketCount > 0);
    forecast.fetchedAt = millis();

    Serial.printf("[Forecast] %d days fetched, alert=%d\n",
                  bucketCount, forecast.alertTriggered);
    return forecast.valid;
}

// ---------------------------------------------------------------------------
// Forecast alert check
// ---------------------------------------------------------------------------
static bool hasForecastAlert() {
    return forecast.valid && forecast.alertTriggered;
}

} // namespace Weather
