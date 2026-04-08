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

static unsigned long lastFetchAttemptMs  = 0;
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

} // namespace Weather
