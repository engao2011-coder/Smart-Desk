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
// Fetch status — lets the UI tell "still loading" apart from "user must fix"
// ---------------------------------------------------------------------------
enum FetchStatus : uint8_t {
    STATUS_PENDING = 0,    // never fetched successfully yet — still loading
    STATUS_OK,             // last fetch succeeded
    STATUS_TEMP_ERROR,     // transient (network / rate-limit / parse) — keep retrying
    STATUS_BAD_SYMBOL,     // not found / delisted / no quote — USER must fix the symbol
};

// Show the "check symbol" hint after this many consecutive soft failures,
// even if the error wasn't a definitive 404 (avoids flapping on a single miss).
static constexpr uint8_t ATTENTION_FAIL_THRESHOLD = 4;

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
    float  fiftyTwoWeekHigh = 0.0f;   // 52-week high
    float  changeFromPeakPct = 0.0f;  // percentage change from 52-week high
    long   volume         = 0;
    bool   valid          = false;
    bool   alertTriggered = false;    // true if |changePct| >= STOCK_ALERT_PCT
    unsigned long fetchedAt = 0;
    FetchStatus status    = STATUS_PENDING;
    uint8_t failCount     = 0;        // consecutive failed fetches (reset on success)
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
// Percentage to display for a quote, honouring the system-wide metric choice
// (Settings::stockFromPeak). Falls back to the daily change when 52-week-high
// data is unavailable so a symbol is never left blank. Used by both the
// summary page and the stocks page so they always agree.
// ---------------------------------------------------------------------------
static float metricPct(const Quote& q) {
    if (Settings::stockFromPeak && q.fiftyTwoWeekHigh != 0.0f) {
        return q.changeFromPeakPct;
    }
    return q.changePct;
}

// ---------------------------------------------------------------------------
// Fetch one quote (blocking). Returns true on success.
// Call from loop() via shouldFetchNext().
// ---------------------------------------------------------------------------
// Record a failed fetch on the quote without disturbing the last good price.
static void markFailure(int idx, FetchStatus st) {
    Quote& q = quotes[idx];
    if (q.failCount < 255) q.failCount++;
    // A definitive 404/delisted is bad immediately; soft errors escalate to
    // "bad symbol" only after several consecutive misses.
    if (st == STATUS_BAD_SYMBOL || q.failCount >= ATTENTION_FAIL_THRESHOLD) {
        q.status = STATUS_BAD_SYMBOL;
    } else {
        q.status = STATUS_TEMP_ERROR;
    }
}

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
        // 404 = symbol genuinely not found; other codes (401/429/5xx/timeout)
        // are transient and worth retrying.
        markFailure(idx, code == 404 ? STATUS_BAD_SYMBOL : STATUS_TEMP_ERROR);
        return false;
    }

    // Filter keeps only the fields we need, reducing heap usage
    StaticJsonDocument<256> filter;
    filter["chart"]["error"]                                                 = true;
    filter["chart"]["result"][0]["meta"]["regularMarketPrice"]               = true;
    filter["chart"]["result"][0]["meta"]["chartPreviousClose"]               = true;
    filter["chart"]["result"][0]["meta"]["fiftyTwoWeekHigh"]                 = true;
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
        markFailure(idx, STATUS_TEMP_ERROR);
        return false;
    }

    if (!doc["chart"]["error"].isNull()) {
        Serial.printf("[Stocks] Provider error for %s\n", sym);
        markFailure(idx, STATUS_BAD_SYMBOL);    // e.g. "Not Found, symbol may be delisted"
        return false;
    }

    JsonVariant result = doc["chart"]["result"][0];
    if (result.isNull()) {
        Serial.printf("[Stocks] No result for %s\n", sym);
        markFailure(idx, STATUS_BAD_SYMBOL);
        return false;
    }

    float price     = result["meta"]["regularMarketPrice"] | 0.0f;
    float prevClose = result["meta"]["chartPreviousClose"]  | 0.0f;
    float week52High = result["meta"]["fiftyTwoWeekHigh"]   | 0.0f;

    if (price == 0.0f) {
        Serial.printf("[Stocks] No quote data for %s\n", sym);
        // Parsed OK but no usable price — treat as soft error so a brief
        // pre-market gap doesn't immediately flag the symbol as bad.
        markFailure(idx, STATUS_TEMP_ERROR);
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
    quote.fiftyTwoWeekHigh = week52High;
    quote.changeFromPeakPct = (week52High != 0.0f)
                        ? ((price - week52High) / week52High * 100.0f)
                        : 0.0f;

    quote.valid       = true;
    quote.fetchedAt   = millis();
    quote.status      = STATUS_OK;
    quote.failCount   = 0;
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
            // Only back off for transient errors. A permanently bad symbol
            // returns quickly and shouldn't impose a 60 s cooldown on the
            // healthy symbols sharing this round-robin.
            lastFetchOk = ok || (quotes[fetchIndex].status == STATUS_BAD_SYMBOL);
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
// True when a configured symbol has failed to fetch in a way the user should
// fix (not found / delisted, or repeatedly failing). Drives the "check symbol"
// hint on-screen and in the web UI.
// ---------------------------------------------------------------------------
static bool needsAttention(int idx) {
    if (idx < 0 || idx >= MAX_STOCKS) return false;
    if (strlen(Settings::stockSymbols[idx]) == 0) return false;
    return !quotes[idx].valid && quotes[idx].status == STATUS_BAD_SYMBOL;
}

// True when any configured symbol needs the user's attention.
static bool anyNeedsAttention() {
    for (int i = 0; i < MAX_STOCKS; i++) {
        if (needsAttention(i)) return true;
    }
    return false;
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
// Return the index of the quote with the largest move by the active metric
// (daily change or from-peak, per Settings::stockFromPeak). Returns -1 if no
// valid quotes exist.
// ---------------------------------------------------------------------------
static int topMoverIndex() {
    int best = -1;
    float bestAbs = -1.0f;
    for (int i = 0; i < MAX_STOCKS; i++) {
        if (!quotes[i].valid) continue;
        float a = fabsf(metricPct(quotes[i]));
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
        // Symbols may have changed — reset per-slot fetch state so a corrected
        // ticker starts fresh as "loading" instead of keeping a stale error.
        quotes[i].status    = STATUS_PENDING;
        quotes[i].failCount = 0;
        if (strlen(Settings::stockSymbols[i]) > 0) {
            strncpy(quotes[i].symbol, Settings::stockSymbols[i], sizeof(quotes[i].symbol) - 1);
        } else {
            // Slot cleared — drop any old data so it doesn't linger on screen.
            quotes[i].valid = false;
            quotes[i].symbol[0] = '\0';
        }
    }
    // Reset rate cache so next fetchNext() cycle re-fetches if stockEuro is on
    lastRateFetchMs = 0;
}

} // namespace Stocks
