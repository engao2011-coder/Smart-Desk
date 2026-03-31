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
#include <ArduinoJson.h>
#include "config.h"

namespace Weather {

// ---------------------------------------------------------------------------
// Data model
// ---------------------------------------------------------------------------
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
    unsigned long fetchedAt = 0;  // millis() when last fetched
};

static Data current;

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
    if (String(OWM_API_KEY) == "YOUR_OPENWEATHERMAP_API_KEY") {
        Serial.println("[Weather] API key not configured — skipping fetch.");
        return false;
    }

    String url = "http://api.openweathermap.org/data/2.5/weather?q=";
    url += String(OWM_CITY_NAME) + "," + String(OWM_COUNTRY);
    url += "&appid=" + String(OWM_API_KEY);
    url += "&units=" + String(OWM_UNITS);
    url += "&lang="  + String(OWM_LANG);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(8000);
    int code = http.GET();

    if (code != 200) {
        Serial.printf("[Weather] HTTP error %d\n", code);
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
        return false;
    }

    current.temp        = doc["main"]["temp"]       | 0.0f;
    current.feelsLike   = doc["main"]["feels_like"] | 0.0f;
    current.humidity    = doc["main"]["humidity"]   | 0.0f;
    current.pressureHpa = doc["main"]["pressure"]   | 0;
    current.windSpeedMs = doc["wind"]["speed"]      | 0.0f;
    current.visibility  = doc["visibility"]         | 0;
    current.cityName    = String((const char*)doc["name"] | OWM_CITY_NAME);

    JsonArray weather = doc["weather"].as<JsonArray>();
    if (!weather.isNull() && weather.size() > 0) {
        current.description = String((const char*)weather[0]["description"]);
        current.iconCode    = String((const char*)weather[0]["icon"]);
    }

    current.valid     = true;
    current.fetchedAt = millis();

    Serial.printf("[Weather] %.1f° %s  %s\n",
                  current.temp, current.description.c_str(), current.iconCode.c_str());
    return true;
}

// ---------------------------------------------------------------------------
// Should we refresh? Call from loop().
// ---------------------------------------------------------------------------
static bool needsRefresh() {
    if (!current.valid) return true;
    return (millis() - current.fetchedAt) >= WEATHER_REFRESH_MS;
}

} // namespace Weather
