/*
 * stocks.h — Stock Price Monitor via Alpha Vantage API
 *
 * Fetches the latest quote for each symbol in STOCK_SYMBOLS using the
 * Alpha Vantage GLOBAL_QUOTE endpoint (free tier).
 *
 * API docs: https://www.alphavantage.co/documentation/#latestprice
 *
 * Rate limit (free tier): 5 requests/minute, 500 requests/day.
 * The module staggers fetches so only one symbol is requested per
 * STOCK_REFRESH_MS cycle, cycling through the list round-robin.
 */

#pragma once

#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"

namespace Stocks {

// ---------------------------------------------------------------------------
// Data model
// ---------------------------------------------------------------------------
struct Quote {
    char   symbol[12]     = {};
    float  price          = 0.0f;
    float  change         = 0.0f;     // absolute change
    float  changePct      = 0.0f;     // percentage change
    float  prevClose      = 0.0f;
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

// ---------------------------------------------------------------------------
// Count configured symbols
// ---------------------------------------------------------------------------
static int symbolCount() {
    int n = 0;
    for (int i = 0; i < MAX_STOCKS; i++) {
        if (STOCK_SYMBOLS[i] && strlen(STOCK_SYMBOLS[i]) > 0) n++;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Fetch one quote (blocking). Returns true on success.
// Call from loop() via shouldFetchNext().
// ---------------------------------------------------------------------------
static bool fetchOne(int idx) {
    if (idx < 0 || idx >= MAX_STOCKS) return false;
    const char* sym = STOCK_SYMBOLS[idx];
    if (!sym || strlen(sym) == 0) return false;

    if (String(AV_API_KEY) == "YOUR_ALPHAVANTAGE_API_KEY") {
        Serial.println("[Stocks] API key not configured — skipping fetch.");
        return false;
    }

    String url = "https://www.alphavantage.co/query?function=GLOBAL_QUOTE"
                 "&symbol=" + String(sym) +
                 "&apikey=" + String(AV_API_KEY);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(10000);
    int code = http.GET();

    if (code != 200) {
        Serial.printf("[Stocks] HTTP %d for %s\n", code, sym);
        http.end();
        return false;
    }

    // Response fits in 512 bytes
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();

    if (err) {
        Serial.printf("[Stocks] JSON parse error: %s\n", err.c_str());
        return false;
    }

    JsonObject q = doc["Global Quote"];
    if (q.isNull()) {
        Serial.printf("[Stocks] No quote data for %s\n", sym);
        return false;
    }

    Quote& quote = quotes[idx];
    strncpy(quote.symbol, sym, sizeof(quote.symbol) - 1);
    quote.price    = atof(q["05. price"]              | "0");
    quote.open     = atof(q["02. open"]               | "0");
    quote.high     = atof(q["03. high"]               | "0");
    quote.low      = atof(q["04. low"]                | "0");
    quote.prevClose= atof(q["08. previous close"]     | "0");
    quote.volume   = atol(q["06. volume"]             | "0");
    quote.change   = atof(q["09. change"]             | "0");

    // Change percentage comes as e.g. "1.2345%"; strip the '%'
    String pctStr  = q["10. change percent"] | "0%";
    pctStr.replace("%", "");
    quote.changePct = pctStr.toFloat();

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

    // Find the next valid slot
    for (int attempt = 0; attempt < MAX_STOCKS; attempt++) {
        fetchIndex = (fetchIndex % MAX_STOCKS);
        if (STOCK_SYMBOLS[fetchIndex] && strlen(STOCK_SYMBOLS[fetchIndex]) > 0) {
            bool ok = fetchOne(fetchIndex);
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
// Initialise symbols array
// ---------------------------------------------------------------------------
static void begin() {
    for (int i = 0; i < MAX_STOCKS; i++) {
        if (STOCK_SYMBOLS[i] && strlen(STOCK_SYMBOLS[i]) > 0) {
            strncpy(quotes[i].symbol, STOCK_SYMBOLS[i], sizeof(quotes[i].symbol) - 1);
        }
    }
}

} // namespace Stocks
