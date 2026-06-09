/*
 * stocks.h — Stock/ETF Price Monitor via Yahoo Finance
 *
 * Fetches the latest quote for each symbol using the Yahoo Finance
 * v8 chart endpoint.  No API key required.
 *
 * Symbol format: TICKER.EXCHANGE  (e.g. IUSE.L, IUSD.DE, PPFB.DE)
 * Docs: https://finance.yahoo.com
 *
 * The module staggers fetches so only one symbol is requested per
 * STOCK_REFRESH_MS cycle, cycling through the list round-robin.
 */

#pragma once

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "settings.h"

namespace Stocks {

// ---------------------------------------------------------------------------
// Data model
// ---------------------------------------------------------------------------
struct Quote {
    char   symbol[24]     = {};
    float  price          = 0.0f;
    float  change         = 0.0f;     // absolute change
    float  changePct      = 0.0f;     // percentage change
    float  prevClose      = 0.0f;
    float  prevFetchPrice = 0.0f;     // price at the previous successful fetch
    float  open           = 0.0f;
    float  high           = 0.0f;
    float  low            = 0.0f;
    long   volume         = 0;
    bool   valid          = false;
    bool   alertTriggered = false;    // true if |changePct| >= STOCK_ALERT_PCT
    unsigned long fetchedAt = 0;
};

static Quote quotes[MAX_STOCKS];
static int   fetchIndex   = 0;       // which symbol to fetch next
static int   displayIndex = 0;       // which quote to show on screen

static constexpr unsigned long FETCH_RETRY_INTERVAL_MS  = 60000;   // 60 s backoff after failure
static constexpr unsigned long RATE_REFRESH_INTERVAL_MS = 900000;  // 15 min between rate refreshes
static unsigned long lastFetchAttemptMs = 0;
static bool          lastFetchOk        = true;

// EUR exchange rates — updated by fetchExchangeRates()
static float         usdToEur           = 0.0f;
static float         gbpToEur           = 0.0f;
static unsigned long lastRateFetchMs    = 0;

// ---------------------------------------------------------------------------
// Count configured symbols
// ---------------------------------------------------------------------------
static int symbolCount() {
    int n = 0;
    for (int i = 0; i < MAX_STOCKS; i++) {
        if (strlen(Settings::stockSymbols[i]) > 0) n++;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Fetch a single EUR exchange rate from Yahoo Finance (e.g. "USDEUR=X").
// Returns true and sets `rate` on success.
// ---------------------------------------------------------------------------
static bool fetchRate(const char* pair, float& rate) {
    String url = "https://query1.finance.yahoo.com/v8/finance/chart/"
                 + String(pair) + "?range=1d&interval=1d";

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(10000);
    http.setUserAgent("Mozilla/5.0");
    int code = http.GET();

    if (code != 200) {
        Serial.printf("[Stocks] Rate HTTP %d for %s\n", code, pair);
        http.end();
        return false;
    }

    StaticJsonDocument<64> filter;
    filter["chart"]["result"][0]["meta"]["regularMarketPrice"] = true;

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, http.getStream(),
                                              DeserializationOption::Filter(filter));
    http.end();

    if (err) return false;

    float v = doc["chart"]["result"][0]["meta"]["regularMarketPrice"] | 0.0f;
    if (v == 0.0f) return false;

    rate = v;
    return true;
}

// ---------------------------------------------------------------------------
// Fetch USD→EUR and GBP→EUR rates. Call when stockEuro is enabled.
// ---------------------------------------------------------------------------
static void fetchExchangeRates() {
    float r = 0.0f;
    if (fetchRate("USDEUR=X", r)) {
        usdToEur = r;
        Serial.printf("[Stocks] USD→EUR = %.4f\n", usdToEur);
    }
    if (fetchRate("GBPEUR=X", r)) {
        gbpToEur = r;
        Serial.printf("[Stocks] GBP→EUR = %.4f\n", gbpToEur);
    }
    lastRateFetchMs = millis();
}

// ---------------------------------------------------------------------------
// Return the EUR-converted price for a quote.
// Currency is inferred from the exchange suffix:
//   .L          → GBX (pence) — divide by 100 then apply GBP→EUR
//   .DE .PA .AS .MI .F .BR .LS .MC → already EUR — no conversion
//   anything else (no dot, or .NYSE/.NASDAQ/etc.) → USD → apply USD→EUR
// Returns the raw price unchanged if rates are not yet available.
// ---------------------------------------------------------------------------
static float euroPrice(const Quote& q) {
    if (!Settings::stockEuro) return q.price;

    const char* sym = q.symbol;
    const char* dot = strrchr(sym, '.');

    if (dot) {
        const char* ext = dot + 1;
        // London Stock Exchange quotes are in GBX (pence); divide by 100 → GBP → EUR
        if (strcmp(ext, "L") == 0) {
            if (gbpToEur == 0.0f) return q.price;
            return (q.price / 100.0f) * gbpToEur;
        }
        // European exchanges already quoted in EUR
        if (strcmp(ext, "DE") == 0 || strcmp(ext, "PA") == 0 ||
            strcmp(ext, "AS") == 0 || strcmp(ext, "MI") == 0 ||
            strcmp(ext, "F")  == 0 || strcmp(ext, "BR") == 0 ||
            strcmp(ext, "LS") == 0 || strcmp(ext, "MC") == 0 ||
            strcmp(ext, "VI") == 0 || strcmp(ext, "HE") == 0) {
            return q.price;
        }
    }

    // Default: treat as USD
    if (usdToEur == 0.0f) return q.price;
    return q.price * usdToEur;
}

// ---------------------------------------------------------------------------
// Fetch one quote (blocking). Returns true on success.
// Call from loop() via shouldFetchNext().
// ---------------------------------------------------------------------------
static bool fetchOne(int idx) {
    if (idx < 0 || idx >= MAX_STOCKS) return false;
    const char* sym = Settings::stockSymbols[idx];
    if (strlen(sym) == 0) return false;

    // Yahoo Finance v8 chart endpoint — no API key needed
    String url = "https://query1.finance.yahoo.com/v8/finance/chart/"
                 + String(sym) + "?range=1d&interval=1d";

    WiFiClientSecure client;
    client.setInsecure();            // skip cert verification (matches weather/prayer)

    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(10000);
    // Yahoo Finance requires a User-Agent header; empty UA can get 401/429
    http.setUserAgent("Mozilla/5.0");
    int code = http.GET();

    if (code != 200) {
        Serial.printf("[Stocks] HTTP %d for %s\n", code, sym);
        http.end();
        return false;
    }

    // Filter keeps only the fields we need, reducing heap usage
    StaticJsonDocument<256> filter;
    filter["chart"]["error"]                                                 = true;
    filter["chart"]["result"][0]["meta"]["regularMarketPrice"]               = true;
    filter["chart"]["result"][0]["meta"]["chartPreviousClose"]               = true;
    filter["chart"]["result"][0]["indicators"]["quote"][0]["open"]           = true;
    filter["chart"]["result"][0]["indicators"]["quote"][0]["high"]           = true;
    filter["chart"]["result"][0]["indicators"]["quote"][0]["low"]            = true;
    filter["chart"]["result"][0]["indicators"]["quote"][0]["volume"]         = true;

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, http.getStream(),
                                              DeserializationOption::Filter(filter));
    http.end();

    if (err) {
        Serial.printf("[Stocks] JSON parse error: %s\n", err.c_str());
        return false;
    }

    if (!doc["chart"]["error"].isNull()) {
        Serial.printf("[Stocks] Provider error for %s\n", sym);
        return false;
    }

    JsonVariant result = doc["chart"]["result"][0];
    if (result.isNull()) {
        Serial.printf("[Stocks] No result for %s\n", sym);
        return false;
    }

    float price     = result["meta"]["regularMarketPrice"] | 0.0f;
    float prevClose = result["meta"]["chartPreviousClose"]  | 0.0f;

    if (price == 0.0f) {
        Serial.printf("[Stocks] No quote data for %s\n", sym);
        return false;
    }

    JsonVariant q = result["indicators"]["quote"][0];

    Quote& quote = quotes[idx];
    quote.prevFetchPrice = quote.valid ? quote.price : 0.0f;  // capture before overwrite
    strncpy(quote.symbol, sym, sizeof(quote.symbol) - 1);
    quote.price     = price;
    quote.prevClose = prevClose;
    quote.open      = q["open"][0]   | 0.0f;
    quote.high      = q["high"][0]   | 0.0f;
    quote.low       = q["low"][0]    | 0.0f;
    quote.volume    = (long)(q["volume"][0] | 0);
    quote.change    = price - prevClose;
    quote.changePct = (prevClose != 0.0f)
                        ? ((price - prevClose) / prevClose * 100.0f)
                        : 0.0f;

    quote.valid       = true;
    quote.fetchedAt   = millis();
    quote.alertTriggered = (fabsf(quote.changePct) >= STOCK_ALERT_PCT);

    Serial.printf("[Stocks] %s  $%.2f  %+.2f%%\n",
                  quote.symbol, quote.price, quote.changePct);
    return true;
}

// ---------------------------------------------------------------------------
// Advance round-robin and fetch the next symbol.
// Returns true when a fetch was attempted.
// Call periodically from loop() — e.g., every STOCK_REFRESH_MS / symbolCount().
// ---------------------------------------------------------------------------
static bool fetchNext() {
    int n = symbolCount();
    if (n == 0) return false;

    // Back off after a failed fetch to avoid hammering Yahoo
    if (!lastFetchOk &&
        lastFetchAttemptMs > 0 &&
        (millis() - lastFetchAttemptMs) < FETCH_RETRY_INTERVAL_MS) {
        return false;
    }

    // Refresh EUR exchange rates at the start of each new cycle (fetchIndex == 0)
    // or when rates have never been fetched.
    if (Settings::stockEuro &&
        (lastRateFetchMs == 0 ||
         (millis() - lastRateFetchMs) >= RATE_REFRESH_INTERVAL_MS) &&
        fetchIndex == 0) {
        fetchExchangeRates();
    }

    // Find the next valid slot
    for (int attempt = 0; attempt < MAX_STOCKS; attempt++) {
        fetchIndex = (fetchIndex % MAX_STOCKS);
        if (strlen(Settings::stockSymbols[fetchIndex]) > 0) {
            lastFetchAttemptMs = millis();
            bool ok = fetchOne(fetchIndex);
            lastFetchOk = ok;
            fetchIndex++;
            return ok;
        }
        fetchIndex++;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Advance the display index to the next valid quote (for screen rotation)
// ---------------------------------------------------------------------------
static void nextDisplay() {
    int n = symbolCount();
    if (n == 0) return;
    for (int i = 0; i < MAX_STOCKS; i++) {
        displayIndex = (displayIndex + 1) % MAX_STOCKS;
        if (quotes[displayIndex].valid) return;
    }
}

// ---------------------------------------------------------------------------
// Check if any stock has an active alert
// ---------------------------------------------------------------------------
static bool hasAlert() {
    for (int i = 0; i < MAX_STOCKS; i++) {
        if (quotes[i].valid && quotes[i].alertTriggered) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Return the index of the quote with the largest |changePct| (top mover).
// Returns -1 if no valid quotes exist.
// ---------------------------------------------------------------------------
static int topMoverIndex() {
    int best = -1;
    float bestAbs = -1.0f;
    for (int i = 0; i < MAX_STOCKS; i++) {
        if (!quotes[i].valid) continue;
        float a = fabsf(quotes[i].changePct);
        if (a > bestAbs) {
            bestAbs = a;
            best = i;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Initialise symbols array
// ---------------------------------------------------------------------------
static void begin() {
    for (int i = 0; i < MAX_STOCKS; i++) {
        if (strlen(Settings::stockSymbols[i]) > 0) {
            strncpy(quotes[i].symbol, Settings::stockSymbols[i], sizeof(quotes[i].symbol) - 1);
        }
    }
    // Reset rate cache so next fetchNext() cycle re-fetches if stockEuro is on
    lastRateFetchMs = 0;
}

} // namespace Stocks
