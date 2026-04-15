/*
 * network.h — Hybrid WiFi Manager
 *
 * Strategy:
 *  1. Try to connect to the last saved credentials (STA mode).
 *  2. If no credentials are stored, or connection times out, start in AP mode
 *     and serve a captive-portal web page so the user can pick a network.
 *  3. Once valid credentials are entered the ESP32 reconnects in STA mode
 *     and saves the credentials to NVS.
 *
 * The web portal is available at http://192.168.4.1 when in AP mode.
 */

#pragma once

#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <time.h>
#include <cstring>
#include "config.h"
#include "settings.h"
#include "weather.h"
#include "prayer.h"
#include "stocks.h"
#include "time_sync.h"

// ---------------------------------------------------------------------------
// Module-level state
// ---------------------------------------------------------------------------
namespace Network {

static WebServer   server(80);
static DNSServer   dnsServer;
static Preferences prefs;
static bool        staConnected = false;
static bool        apActive     = false;
static bool        serverRoutesRegistered = false;
static bool        serverStarted = false;
static bool        mdnsActive = false;
static bool        dnsActive = false;

// Derived AP password: prefix + last 4 hex chars of MAC
static char apDerivedPassword[16] = {};

// CSRF token — generated each time the server starts, embedded in all forms.
static char csrfToken[17] = {};

static void generateCsrfToken() {
    snprintf(csrfToken, sizeof(csrfToken), "%08lx%08lx",
             (unsigned long)(uint32_t)esp_random(),
             (unsigned long)(uint32_t)esp_random());
}

// Derive a device-unique AP password from the MAC address
// Format: AP_PASSWORD_PREFIX + last 4 hex digits of MAC (e.g. "deskA1B2")
static void deriveApPassword() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(apDerivedPassword, sizeof(apDerivedPassword), "%s%02X%02X",
             AP_PASSWORD_PREFIX, mac[4], mac[5]);
}

// Get the active AP password (derived or manual from config.h)
static const char* getApPassword() {
    if (strlen(AP_PASSWORD) > 0) return AP_PASSWORD;
    if (apDerivedPassword[0] == '\0') deriveApPassword();
    return apDerivedPassword;
}

struct SavedNetwork {
    char ssid[64];
    char password[64];
};

// Persisted credentials (most recent first)
static SavedNetwork savedNetworks[WIFI_SAVED_NETWORKS_MAX] = {};
static int          savedNetworkCount = 0;

static bool isSameSSID(const char* a, const char* b) {
    return strncmp(a, b, sizeof(savedNetworks[0].ssid)) == 0;
}

static void clearSavedNetworks() {
    memset(savedNetworks, 0, sizeof(savedNetworks));
    savedNetworkCount = 0;
}

static void persistSavedNetworks() {
    prefs.begin("wifi", false);  // read-write namespace
    prefs.putInt("count", savedNetworkCount);

    for (int i = 0; i < WIFI_SAVED_NETWORKS_MAX; i++) {
        char ssidKey[8];
        char passKey[8];
        snprintf(ssidKey, sizeof(ssidKey), "ssid%d", i);
        snprintf(passKey, sizeof(passKey), "pass%d", i);

        if (i < savedNetworkCount && savedNetworks[i].ssid[0] != '\0') {
            prefs.putString(ssidKey, savedNetworks[i].ssid);
            prefs.putString(passKey, savedNetworks[i].password);
        } else {
            if (prefs.isKey(ssidKey)) prefs.remove(ssidKey);
            if (prefs.isKey(passKey)) prefs.remove(passKey);
        }
    }

    // Keep legacy key pair in sync with top-priority network for compatibility.
    if (savedNetworkCount > 0) {
        prefs.putString("ssid", savedNetworks[0].ssid);
        prefs.putString("password", savedNetworks[0].password);
    } else {
        prefs.remove("ssid");
        prefs.remove("password");
    }

    prefs.end();
}

static void moveNetworkToFront(int index) {
    if (index <= 0 || index >= savedNetworkCount) return;
    SavedNetwork chosen = savedNetworks[index];
    for (int i = index; i > 0; i--) {
        savedNetworks[i] = savedNetworks[i - 1];
    }
    savedNetworks[0] = chosen;
}

static bool addOrUpdateNetwork(const char* ssid, const char* password) {
    if (!ssid || ssid[0] == '\0') return false;

    int foundIndex = -1;
    for (int i = 0; i < savedNetworkCount; i++) {
        if (isSameSSID(savedNetworks[i].ssid, ssid)) {
            foundIndex = i;
            break;
        }
    }

    if (foundIndex >= 0) {
        strncpy(savedNetworks[foundIndex].password, password ? password : "", sizeof(savedNetworks[foundIndex].password) - 1);
        savedNetworks[foundIndex].password[sizeof(savedNetworks[foundIndex].password) - 1] = '\0';
        moveNetworkToFront(foundIndex);
        return true;
    }

    if (savedNetworkCount < WIFI_SAVED_NETWORKS_MAX) {
        savedNetworkCount++;
    }

    for (int i = savedNetworkCount - 1; i > 0; i--) {
        savedNetworks[i] = savedNetworks[i - 1];
    }

    strncpy(savedNetworks[0].ssid, ssid, sizeof(savedNetworks[0].ssid) - 1);
    savedNetworks[0].ssid[sizeof(savedNetworks[0].ssid) - 1] = '\0';
    strncpy(savedNetworks[0].password, password ? password : "", sizeof(savedNetworks[0].password) - 1);
    savedNetworks[0].password[sizeof(savedNetworks[0].password) - 1] = '\0';

    return true;
}

// ---------------------------------------------------------------------------
// Load credentials from NVS
// ---------------------------------------------------------------------------
static void loadCredentials() {
    clearSavedNetworks();

    prefs.begin("wifi", true);   // read-only namespace

    if (prefs.isKey("count")) {
        int count = prefs.getInt("count", 0);
        if (count < 0) count = 0;
        if (count > WIFI_SAVED_NETWORKS_MAX) count = WIFI_SAVED_NETWORKS_MAX;

        for (int i = 0; i < count; i++) {
            char ssidKey[8];
            char passKey[8];
            snprintf(ssidKey, sizeof(ssidKey), "ssid%d", i);
            snprintf(passKey, sizeof(passKey), "pass%d", i);

            prefs.getString(ssidKey, savedNetworks[savedNetworkCount].ssid,
                            sizeof(savedNetworks[savedNetworkCount].ssid));
            prefs.getString(passKey, savedNetworks[savedNetworkCount].password,
                            sizeof(savedNetworks[savedNetworkCount].password));

            if (savedNetworks[savedNetworkCount].ssid[0] != '\0') {
                savedNetworkCount++;
            }
        }
    }

    // Legacy migration path (single network keys).
    if (savedNetworkCount == 0) {
        char legacySSID[64] = {};
        char legacyPassword[64] = {};
        prefs.getString("ssid", legacySSID, sizeof(legacySSID));
        prefs.getString("password", legacyPassword, sizeof(legacyPassword));
        if (legacySSID[0] != '\0') {
            addOrUpdateNetwork(legacySSID, legacyPassword);
        }
    }

    prefs.end();

    if (savedNetworkCount > 0) {
        // Persist to v2 keys after successful legacy load.
        persistSavedNetworks();
    }
}

// ---------------------------------------------------------------------------
// Save credentials to NVS
// ---------------------------------------------------------------------------
static void saveCredentials(const char* ssid, const char* password) {
    if (!addOrUpdateNetwork(ssid, password)) return;
    persistSavedNetworks();
}

// ---------------------------------------------------------------------------
// HTML helpers
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// HTML-escape a string to prevent XSS when embedding user/network data in HTML
// ---------------------------------------------------------------------------
static String htmlEscape(const String& s) {
    String out;
    out.reserve(s.length());
    for (unsigned int i = 0; i < s.length(); i++) {
        char c = s[i];
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out += c;        break;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Constant-time string comparison (prevents CSRF token timing side-channels)
// ---------------------------------------------------------------------------
static bool secureStrEqual(const String& a, const String& b) {
    // Lengths are compared first (this leaks length, but token length is fixed).
    if (a.length() != b.length()) return false;
    uint8_t diff = 0;
    for (unsigned int i = 0; i < a.length(); i++) {
        diff |= (uint8_t)a[i] ^ (uint8_t)b[i];
    }
    return diff == 0;
}

static String wifiScanHTML() {
    // Scan for available networks
    int n = WiFi.scanNetworks();
    String options = "";
    for (int i = 0; i < n; i++) {
        String ssidSafe = htmlEscape(WiFi.SSID(i));
        int rssi = WiFi.RSSI(i);
        // Signal bars: 4 > -50, 3 > -60, 2 > -70, 1 > -80, 0 else
        int bars = rssi > -50 ? 4 : rssi > -60 ? 3 : rssi > -70 ? 2 : rssi > -80 ? 1 : 0;
        String barStr = "";
        for (int b = 0; b < 4; b++) barStr += (b < bars) ? "&#9608;" : "&#9617;";
        options += "<option value=\"" + ssidSafe + "\">" +
                   ssidSafe + " " + barStr + "</option>\n";
    }
    return options;
}

static String portalPage() {
    String html = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>DeskNexus Setup</title>
<style>
  body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;
       display:flex;justify-content:center;align-items:center;min-height:100vh;margin:0}
  .card{background:#16213e;border-radius:12px;padding:32px;max-width:400px;width:90%;box-shadow:0 4px 24px #0005}
  h2{margin:0 0 24px;color:#e94560;text-align:center}
  label{display:block;margin:12px 0 4px;font-size:.9rem}
  select,input{width:100%;padding:10px;border:none;border-radius:6px;
               background:#0f3460;color:#eee;font-size:1rem;box-sizing:border-box}
  input[type=submit]{background:#e94560;cursor:pointer;margin-top:20px;font-weight:bold}
  input[type=submit]:hover{background:#c73652}
  .msg{text-align:center;color:#4ecca3;margin-top:16px;font-size:.9rem}
  .pw-wrap{position:relative}
  .pw-wrap input{padding-right:44px}
  .pw-toggle{position:absolute;right:8px;top:50%;transform:translateY(-50%);
             background:none;border:none;color:#4ecca3;cursor:pointer;font-size:1.1rem;
             padding:4px 6px;width:auto}
  .refresh{display:inline-block;margin:6px 0;color:#4ecca3;font-size:.8rem;
           cursor:pointer;text-decoration:underline;background:none;border:none}
  #overlay{display:none;position:fixed;top:0;left:0;width:100%;height:100%;
           background:#1a1a2e;z-index:999;flex-direction:column;
           justify-content:center;align-items:center}
  .spinner{width:48px;height:48px;border:4px solid #0f3460;border-top-color:#e94560;
           border-radius:50%;animation:spin 1s linear infinite;margin-bottom:20px}
  @keyframes spin{to{transform:rotate(360deg)}}
</style>
</head>
<body>
<div class="card">
  <h2>&#128338; DeskNexus Setup</h2>
  <form id="wifiForm" method="POST" action="/save">
    <input type="hidden" name="csrf" value=")rawhtml";
    html += String(csrfToken);
    html += R"rawhtml(">";
    html += R"rawhtml(
    <label for="ssid">Wi-Fi Network</label>
    <select name="ssid" id="ssid">
)rawhtml";
    html += wifiScanHTML();
    html += R"rawhtml(
    </select>
    <button type="button" class="refresh" onclick="location.reload()">&#8635; Rescan networks</button>
    <label for="pass">Password</label>
    <div class="pw-wrap">
      <input type="password" id="pass" name="pass" placeholder="Wi-Fi password">
      <button type="button" class="pw-toggle" onclick="var p=document.getElementById('pass');p.type=p.type==='password'?'text':'password'">&#128065;</button>
    </div>
    <input type="submit" value="Connect">
  </form>
  <p class="msg">DeskNexus will restart after saving.</p>
</div>
<div id="overlay">
  <div class="spinner"></div>
  <h2 style="color:#4ecca3">Connecting...</h2>
  <p style="color:#9bd">DeskNexus is restarting.<br>Reconnect to your home WiFi.</p>
</div>
<script>
document.getElementById('wifiForm').addEventListener('submit',function(){
  setTimeout(function(){document.getElementById('overlay').style.display='flex'},100);
});
</script>
</body>
</html>
)rawhtml";
    return html;
}

static String savedPage() {
    return R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head><meta charset="UTF-8"><title>Saved</title>
<style>body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;
display:flex;justify-content:center;align-items:center;min-height:100vh}</style>
</head>
<body><h2 style="color:#4ecca3">&#10003; Credentials saved!<br>Device is restarting...</h2></body>
</html>
)rawhtml";
}

static String statusPage() {
    String ip   = WiFi.localIP().toString();
    String ssid = WiFi.SSID();
    if (ssid.length() == 0 && savedNetworkCount > 0) {
        ssid = String(savedNetworks[0].ssid);
    }
    uint32_t uptimeSec = millis() / 1000;
    uint32_t h = uptimeSec / 3600;
    uint32_t m = (uptimeSec % 3600) / 60;
    uint32_t s = uptimeSec % 60;
    char uptime[32];
    snprintf(uptime, sizeof(uptime), "%02uh %02um %02us", h, m, s);

    String html = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>DeskNexus Status</title>
<style>
  body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;
       display:flex;justify-content:center;align-items:center;min-height:100vh;margin:0}
  .card{background:#16213e;border-radius:12px;padding:32px;max-width:420px;width:90%;box-shadow:0 4px 24px #0005}
  h2{margin:0 0 24px;color:#e94560;text-align:center}
  table{width:100%;border-collapse:collapse}
  td{padding:10px 8px;border-bottom:1px solid #0f3460;font-size:.95rem}
  td:first-child{color:#4ecca3;font-weight:bold;width:40%}
  .badge{display:inline-block;background:#4ecca3;color:#1a1a2e;border-radius:4px;padding:2px 8px;font-size:.8rem;font-weight:bold}
  a.btn{display:block;text-align:center;margin-top:24px;padding:10px;
        background:#e94560;color:#fff;border-radius:6px;text-decoration:none;font-weight:bold}
  a.btn:hover{background:#c73652}
</style>
</head>
<body>
<div class="card">
  <h2>&#128338; DeskNexus</h2>
  <table>
    <tr><td>Status</td><td><span class="badge">Online</span></td></tr>
    <tr><td>Network</td><td>)rawhtml";
    html += ssid;
    html += R"rawhtml(</td></tr>
    <tr><td>IP Address</td><td>)rawhtml";
    html += ip;
    html += R"rawhtml(</td></tr>
    <tr><td>Hostname</td><td>desknexus.local</td></tr>
    <tr><td>Uptime</td><td>)rawhtml";
    html += String(uptime);
    html += R"rawhtml(</td></tr>
  </table>
  <a class="btn" href="/setup">&#9881; Wi-Fi Setup</a>
  <a class="btn" href="/settings" style="background:#0f3460;margin-top:10px">&#9881; Settings</a>
  <a class="btn" href="/update" style="background:#0f3460;margin-top:10px">&#128260; Firmware Update</a>
</div>
</body>
</html>
)rawhtml";
    return html;
}

// ---------------------------------------------------------------------------
// Settings page (STA mode only)
// ---------------------------------------------------------------------------
static String settingsPage() {
    String html = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>DeskNexus Settings</title>
<style>
  body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;
       display:flex;justify-content:center;align-items:center;min-height:100vh;margin:0;padding:16px 0}
  .card{background:#16213e;border-radius:12px;padding:32px;max-width:440px;width:90%;box-shadow:0 4px 24px #0005}
  h2{margin:0 0 24px;color:#e94560;text-align:center}
  label{display:block;margin:10px 0 3px;font-size:.85rem;color:#c618}
  select,input[type=text],input[type=number],input[type=password]{
    width:100%;padding:9px;border:none;border-radius:6px;
    background:#0f3460;color:#eee;font-size:.95rem;box-sizing:border-box}
  .row{display:flex;gap:10px}
  .row>div{flex:1}
  input[type=submit]{width:100%;padding:11px;border:none;border-radius:6px;
    background:#e94560;color:#fff;cursor:pointer;margin-top:22px;font-weight:bold;font-size:1rem}
  input[type=submit]:hover{background:#c73652}
  .link{text-align:center;margin-top:14px}
  .link a{color:#4ecca3;font-size:.85rem}
  .rst{display:block;text-align:center;margin-top:10px;color:#e94560;font-size:.8rem}
  .hint{font-size:.78rem;color:#9bd;line-height:1.4;margin:4px 0 0}
  details{margin:12px 0 4px}
  summary{color:#4ecca3;font-size:1rem;border-bottom:1px solid #0f3460;padding-bottom:4px;
          cursor:pointer;font-weight:bold;list-style:none}
  summary::-webkit-details-marker{display:none}
  summary::before{content:'▸ ';font-size:.9rem}
  details[open]>summary::before{content:'▾ '}
</style>
</head>
<body>
<div class="card">
  <h2>&#9881; DeskNexus Settings</h2>
  <form method="POST" action="/save-settings">
    <input type="hidden" name="csrf" value=")rawhtml";
    html += String(csrfToken);
    html += R"rawhtml(">

    <details open>
    <summary>Location &amp; Time</summary>
    <div class="row">
      <div>
        <label for="city">City</label>
        <input type="text" id="city" name="city" maxlength="60" value=")rawhtml";
    html += String(Settings::city);
    html += R"rawhtml(">
      </div>
      <div>
        <label for="country">Country (ISO)</label>
        <input type="text" id="country" name="country" maxlength="4" value=")rawhtml";
    html += String(Settings::country);
    html += R"rawhtml(">
      </div>
    </div>
    <div class="row">
      <div>
        <label style="display:flex;align-items:center;gap:6px;cursor:pointer">
          <input type="checkbox" name="autoCity" value="1")rawhtml";
    if (!Settings::cityManual) html += " checked";
    html += R"rawhtml(>
          Auto-detect city from IP (uncheck to lock manual city)
        </label>
      </div>
    </div>

    <label for="utc">Timezone (UTC Offset)</label>
    <select id="utc" name="utc">)rawhtml";

    struct { long sec; const char* label; } tzOpts[] = {
        {-43200, "UTC-12"}, {-39600, "UTC-11"}, {-36000, "UTC-10"},
        {-34200, "UTC-9:30"}, {-32400, "UTC-9"}, {-28800, "UTC-8"},
        {-25200, "UTC-7"}, {-21600, "UTC-6"}, {-18000, "UTC-5"},
        {-16200, "UTC-4:30"}, {-14400, "UTC-4"}, {-12600, "UTC-3:30"},
        {-10800, "UTC-3"}, {-7200, "UTC-2"}, {-3600, "UTC-1"},
        {0,      "UTC+0"},
        {3600,   "UTC+1"},  {7200,   "UTC+2"},  {10800,  "UTC+3"},
        {12600,  "UTC+3:30"}, {14400, "UTC+4"}, {16200, "UTC+4:30"},
        {18000,  "UTC+5"},  {19800,  "UTC+5:30"}, {20700, "UTC+5:45"},
        {21600,  "UTC+6"},  {23400,  "UTC+6:30"}, {25200, "UTC+7"},
        {28800,  "UTC+8"},  {31500,  "UTC+8:45"}, {32400, "UTC+9"},
        {34200,  "UTC+9:30"}, {36000, "UTC+10"}, {37800, "UTC+10:30"},
        {39600,  "UTC+11"}, {43200, "UTC+12"},  {45900, "UTC+12:45"},
        {46800,  "UTC+13"}, {50400, "UTC+14"},
    };
    bool matchedTz = false;
    for (auto& tz : tzOpts) {
        html += "<option value=\"" + String(tz.sec) + "\"";
        if (Settings::utcOffset == tz.sec) { html += " selected"; matchedTz = true; }
        html += ">" + String(tz.label) + "</option>\n";
    }
    if (!matchedTz) {
        html += "<option value=\"" + String(Settings::utcOffset) + "\" selected>Custom (" + String(Settings::utcOffset) + " s)</option>\n";
    }
    html += R"rawhtml(    </select>
        <p class="hint">
            Auto-detect: )rawhtml";
        html += String(Settings::autoDetectStatus);
        html += " (";
        html += Settings::autoDetectLastOk ? "OK" : "Failed";
        html += R"rawhtml()
        </p>
    </details>

    <details open>
    <summary>Weather</summary>
    <label for="owmkey">OpenWeatherMap API Key</label>
    <input type="password" id="owmkey" name="owmkey" maxlength="46" value=")rawhtml";
    html += String(Settings::owmApiKey);
    html += R"rawhtml(">
    <p class="hint">Free key at <a href="https://openweathermap.org/appid" target="_blank" style="color:#4ecca3">openweathermap.org/appid</a>. Weather is optional — clock, prayers and stocks work without it.</p>
    <div class="row">
      <div>
        <label for="units">Units</label>
        <select id="units" name="units">
          <option value="metric")rawhtml";
    if (String(Settings::owmUnits) == "metric") html += " selected";
    html += R"rawhtml(>Metric (°C)</option>
          <option value="imperial")rawhtml";
    if (String(Settings::owmUnits) == "imperial") html += " selected";
    html += R"rawhtml(>Imperial (°F)</option>
        </select>
      </div>
    </div>
    </details>

    <details open>
    <summary>Prayer Times</summary>
    <label for="method">Calculation Method</label>
    <select id="method" name="method">)rawhtml";

    struct { int id; const char* name; } methods[] = {
        {1,"Muslim World League"},{2,"ISNA (North America)"},
        {3,"Egyptian General Authority"},{4,"Makkah (Umm Al-Qura)"},
        {5,"Univ. of Islamic Sciences, Karachi"},
        {8,"Gulf Region"},{16,"Diyanet (Turkey)"},{17,"Tehran"}
    };
    for (auto& m : methods) {
        html += "<option value=\"" + String(m.id) + "\"";
        if (Settings::prayerMethod == m.id) html += " selected";
        html += ">" + String(m.name) + "</option>";
    }

    html += R"rawhtml(
    </select>
    <p class="hint">Choose the method your local mosque follows. Default: Makkah (Umm Al-Qura) for Saudi Arabia.</p>
    </details>

    <details>
    <summary>Display</summary>
    <label for="theme">Theme</label>
    <select id="theme" name="theme">
        <option value="auto")rawhtml";
    if (Settings::themeMode == 0) html += " selected";
    html += R"rawhtml(>Auto (dark at night)</option>
        <option value="dark")rawhtml";
    if (Settings::themeMode == 1) html += " selected";
    html += R"rawhtml(>Always Dark</option>
        <option value="light")rawhtml";
    if (Settings::themeMode == 2) html += " selected";
    html += R"rawhtml(>Always Light</option>
    </select>
    </details>

    <details>
    <summary>Stocks</summary>
    <p class="hint">Yahoo Finance symbols. Leave empty to disable. Example: IUSE.L, AAPL, MSFT</p>)rawhtml";

    for (int i = 0; i < MAX_STOCKS; i++) {
        html += "<label>Symbol " + String(i + 1) + "</label>";
        html += "<input type=\"text\" name=\"stk" + String(i) +
                "\" maxlength=\"23\" value=\"" + String(Settings::stockSymbols[i]) +
                "\" placeholder=\"e.g. IUSE.L\" style=\"text-transform:uppercase\">";
    }

    html += R"rawhtml(
    </details>

    <input type="submit" value="Save Settings">
  </form>

  <details style="margin-top:20px">
  <summary>Saved Wi-Fi Networks</summary>)rawhtml";
    if (savedNetworkCount == 0) {
        html += "<p class=\"hint\">No saved networks.</p>";
    } else {
        for (int i = 0; i < savedNetworkCount; i++) {
            String sSafe = htmlEscape(String(savedNetworks[i].ssid));
            html += "<div style=\"display:flex;justify-content:space-between;align-items:center;padding:8px 0;border-bottom:1px solid #0f3460\">";
            html += "<span style=\"font-size:.95rem\">" + sSafe + "</span>";
            html += "<form method=\"POST\" action=\"/forget-network\" style=\"margin:0\">";
            html += "<input type=\"hidden\" name=\"csrf\" value=\"" + String(csrfToken) + "\">";
            html += "<input type=\"hidden\" name=\"idx\" value=\"" + String(i) + "\">";
            html += "<button type=\"submit\" style=\"background:#e94560;color:#fff;border:none;border-radius:4px;padding:4px 10px;font-size:.8rem;cursor:pointer\" onclick=\"return confirm('Forget " + sSafe + "?')\">Forget</button>";
            html += "</form></div>";
        }
    }
    html += R"rawhtml(
  </details>
  <div class="link"><a href="/">&#8592; Back to Status</a></div>
  <a class="rst" href="/reset-settings?csrf=)rawhtml";
    html += String(csrfToken);
    html += R"rawhtml(" onclick="return confirm('Reset all settings to defaults?')">Reset to Defaults</a>
</div>
</body>
</html>
)rawhtml";
    return html;
}

static String settingsSavedPage() {
    return R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head><meta charset="UTF-8"><meta http-equiv="refresh" content="2;url=/settings">
<title>Saved</title>
<style>body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;
display:flex;justify-content:center;align-items:center;min-height:100vh}</style>
</head>
<body><h2 style="color:#4ecca3">&#10003; Settings saved!<br>Refreshing data...</h2></body>
</html>
)rawhtml";
}


// ---------------------------------------------------------------------------
// OTA firmware update page
// ---------------------------------------------------------------------------
static String otaPage() {
    String html = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>DeskNexus Firmware Update</title>
<style>
  body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;
       display:flex;justify-content:center;align-items:center;min-height:100vh;margin:0}
  .card{background:#16213e;border-radius:12px;padding:32px;max-width:420px;width:90%;box-shadow:0 4px 24px #0005}
  h2{margin:0 0 24px;color:#e94560;text-align:center}
  label{display:block;margin:12px 0 4px;font-size:.9rem}
  input[type=file]{width:100%;padding:10px;border:none;border-radius:6px;
                   background:#0f3460;color:#eee;font-size:.95rem;box-sizing:border-box}
  input[type=submit]{width:100%;padding:11px;border:none;border-radius:6px;
                     background:#e94560;color:#fff;cursor:pointer;margin-top:16px;
                     font-weight:bold;font-size:1rem}
  input[type=submit]:hover{background:#c73652}
  input[type=submit]:disabled{background:#555;cursor:not-allowed}
  .warn{color:#f0a500;font-size:.85rem;margin-top:8px;text-align:center}
  .msg{text-align:center;color:#4ecca3;margin-top:12px;font-size:.9rem;min-height:1.2em}
  a.btn{display:block;text-align:center;margin-top:14px;color:#4ecca3;font-size:.85rem}
  progress{width:100%;height:16px;border-radius:8px;margin-top:14px;display:none}
  progress::-webkit-progress-bar{background:#0f3460;border-radius:8px}
  progress::-webkit-progress-value{background:#4ecca3;border-radius:8px}
</style>
</head>
<body>
<div class="card">
  <h2>&#128260; Firmware Update</h2>
  <form id="upForm" method="POST" enctype="multipart/form-data">
    <label for="fw">Select firmware binary (.bin)</label>
    <input type="file" id="fw" name="firmware" accept=".bin" required>
    <p class="warn">&#9888; The device will restart after flashing. Do not power off.</p>
    <input type="submit" id="submitBtn" value="Upload &amp; Flash">
  </form>
  <progress id="bar" value="0" max="100"></progress>
  <p class="msg" id="statusMsg"></p>
  <a class="btn" href="/">&#8592; Back to Status</a>
</div>
<script>
  var CSRF_TOKEN = ')rawhtml";
    html += String(csrfToken);
    html += R"rawhtml(';
  document.getElementById('upForm').addEventListener('submit', function(e) {
    e.preventDefault();
    var bar = document.getElementById('bar');
    var msg = document.getElementById('statusMsg');
    var btn = document.getElementById('submitBtn');
    bar.style.display = 'block';
    bar.value = 0;
    btn.disabled = true;
    msg.textContent = 'Uploading…';
    var fd = new FormData(this);
    var xhr = new XMLHttpRequest();
    xhr.upload.onprogress = function(ev) {
      if (ev.lengthComputable) bar.value = Math.round(ev.loaded / ev.total * 100);
    };
    xhr.onload = function() {
      bar.value = 100;
      if (xhr.status === 200) {
        msg.textContent = '✓ Flash complete — device restarting…';
      } else {
        msg.style.color = '#e94560';
        msg.textContent = '✗ Update failed: ' + xhr.responseText;
        btn.disabled = false;
      }
    };
    xhr.onerror = function() {
      msg.style.color = '#e94560';
      msg.textContent = '✗ Network error during upload.';
      btn.disabled = false;
    };
    xhr.open('POST', '/do-update?csrf=' + CSRF_TOKEN);
    xhr.send(fd);
  });
</script>
</body>
</html>
)rawhtml";
    return html;
}

// ---------------------------------------------------------------------------
// OTA upload handler — called for each chunk as data arrives
// ---------------------------------------------------------------------------
static bool   _otaCsrfOk   = false;
static bool   _otaUpdateOk  = false;
static String _otaErrorMsg;   // set on failure so handleOtaComplete can report it

static void handleOtaUpload() {
    HTTPUpload& upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        // Reset per-request state so a new upload always starts clean.
        _otaCsrfOk   = false;
        _otaUpdateOk = false;
        _otaErrorMsg = "";

        // CSRF token is passed as URL query parameter: /do-update?csrf=TOKEN
        // Use constant-time comparison to prevent timing side-channels.
        _otaCsrfOk = secureStrEqual(server.arg("csrf"), String(csrfToken));
        if (!_otaCsrfOk) {
            Serial.println("[OTA] HTTP update rejected: invalid CSRF token.");
            return;
        }
        Serial.printf("[OTA] HTTP update start: %s\n", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            _otaErrorMsg = Update.errorString();
            Update.printError(Serial);
            // _otaUpdateOk stays false — subsequent chunks will be skipped
        } else {
            _otaUpdateOk = true;
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (!_otaCsrfOk || !_otaUpdateOk) return;
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            _otaErrorMsg = Update.errorString();
            Update.printError(Serial);
            _otaUpdateOk = false;  // abort further writes on error
        } else {
            Serial.printf("[OTA] Written %u bytes\r", upload.totalSize);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (!_otaCsrfOk || !_otaUpdateOk) return;
        if (Update.end(true)) {
            Serial.printf("\n[OTA] HTTP update complete: %u bytes.\n", upload.totalSize);
        } else {
            _otaErrorMsg = Update.errorString();
            Update.printError(Serial);
            _otaUpdateOk = false;
        }
    }
}

// ---------------------------------------------------------------------------
// OTA completion handler — called after all upload data has been received
// ---------------------------------------------------------------------------
static void handleOtaComplete() {
    if (!_otaCsrfOk) {
        server.send(403, "text/plain", "Forbidden: invalid CSRF token.");
        return;
    }
    if (!_otaUpdateOk || Update.hasError()) {
        // Log detailed error to serial only — don't expose partition info to browser
        String err = _otaErrorMsg.length() > 0 ? _otaErrorMsg : String(Update.errorString());
        Serial.printf("[OTA] Update failed: %s\n", err.c_str());
        server.send(500, "text/plain", "Update failed. Check device serial log for details.");
    } else {
        server.send(200, "text/plain", "OK");
        delay(500);
        ESP.restart();
    }
}

// ---------------------------------------------------------------------------
// Web server route handlers
// ---------------------------------------------------------------------------
static void handleRoot() {
    if (apActive) {
        server.send(200, "text/html", portalPage());
    } else {
        server.send(200, "text/html", statusPage());
    }
}

static void handleSettings() {
    server.sendHeader("Cache-Control", "no-store, no-cache");
    server.sendHeader("Pragma", "no-cache");
    server.send(200, "text/html", settingsPage());
}

static void handleSaveSettings() {
    if (!secureStrEqual(server.arg("csrf"), String(csrfToken))) {
        server.send(403, "text/plain", "Forbidden: invalid CSRF token.");
        return;
    }
    // City / Country
    String v = server.arg("city");
    if (v.length() > 0 && v.length() < sizeof(Settings::city)) {
        strncpy(Settings::city, v.c_str(), sizeof(Settings::city) - 1);
        Settings::cityManual = true;  // user set city manually — lock auto-detect
    }

    // "Auto city" checkbox — if present and checked, clear the manual lock
    if (server.hasArg("autoCity") && server.arg("autoCity") == "1")
        Settings::cityManual = false;

    v = server.arg("country");
    if (v.length() > 0 && v.length() < sizeof(Settings::country))
        strncpy(Settings::country, v.c_str(), sizeof(Settings::country) - 1);

    // Units
    v = server.arg("units");
    if (v == "metric" || v == "imperial")
        strncpy(Settings::owmUnits, v.c_str(), sizeof(Settings::owmUnits) - 1);

    // Prayer method
    int pm = server.arg("method").toInt();
    if (pm > 0 && pm <= 99) Settings::prayerMethod = pm;

    // UTC offset
    long utc = server.arg("utc").toInt();
    if (utc >= -43200 && utc <= 50400) Settings::utcOffset = utc;

    // Theme mode: auto / dark / light
    v = server.arg("theme");
    if (v == "auto")       { Settings::themeMode = 0; }
    else if (v == "dark")  { Settings::themeMode = 1; Settings::themeDark = true; }
    else if (v == "light") { Settings::themeMode = 2; Settings::themeDark = false; }

    Settings::autoDetectLastOk = false;
    Settings::autoDetectLastEpoch = time(nullptr);
    strncpy(Settings::autoDetectStatus, "Manual settings saved", sizeof(Settings::autoDetectStatus) - 1);
    Settings::autoDetectStatus[sizeof(Settings::autoDetectStatus) - 1] = '\0';

    // Stocks
    for (int i = 0; i < MAX_STOCKS; i++) {
        String key = "stk" + String(i);
        v = server.arg(key);
        v.trim();
        v.toUpperCase();
        if (v.length() < sizeof(Settings::stockSymbols[i])) {
            strncpy(Settings::stockSymbols[i], v.c_str(), sizeof(Settings::stockSymbols[i]) - 1);
            Settings::stockSymbols[i][v.length()] = '\0';
        }
    }

    // API keys
    v = server.arg("owmkey");
    v.trim();
    if (v.length() > 0 && v.length() < sizeof(Settings::owmApiKey))
        strncpy(Settings::owmApiKey, v.c_str(), sizeof(Settings::owmApiKey) - 1);

    Settings::save();

    // Invalidate caches so data re-fetches on next loop
    Weather::current.valid = false;
    Prayer::current.valid  = false;
    Stocks::begin();  // re-init symbol list

    // Re-apply timezone
    TimeSync::apply(Settings::utcOffset, true);

    server.send(200, "text/html", settingsSavedPage());
}

static void handleResetSettings() {
    if (!secureStrEqual(server.arg("csrf"), String(csrfToken))) {
        server.send(403, "text/plain", "Forbidden: invalid CSRF token.");
        return;
    }
    Settings::resetToDefaults();
    server.sendHeader("Location", "/settings", true);
    server.send(302, "text/plain", "");
}

static void handleSetup() {
    server.sendHeader("Cache-Control", "no-store, no-cache");
    server.sendHeader("Pragma", "no-cache");
    server.send(200, "text/html", portalPage());
}

static void handleForgetNetwork() {
    if (!secureStrEqual(server.arg("csrf"), String(csrfToken))) {
        server.send(403, "text/plain", "Forbidden: invalid CSRF token.");
        return;
    }
    int idx = server.arg("idx").toInt();
    if (idx >= 0 && idx < savedNetworkCount) {
        // Shift remaining networks down
        for (int i = idx; i < savedNetworkCount - 1; i++) {
            savedNetworks[i] = savedNetworks[i + 1];
        }
        savedNetworkCount--;
        memset(&savedNetworks[savedNetworkCount], 0, sizeof(SavedNetwork));
        persistSavedNetworks();
    }
    server.sendHeader("Location", "/settings", true);
    server.send(302, "text/plain", "");
}

static void handleSave() {
    if (!secureStrEqual(server.arg("csrf"), String(csrfToken))) {
        server.send(403, "text/plain", "Forbidden: invalid CSRF token.");
        return;
    }
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");

    if (ssid.length() == 0) {
        server.send(400, "text/plain", "SSID cannot be empty.");
        return;
    }
    saveCredentials(ssid.c_str(), pass.c_str());
    server.send(200, "text/html", savedPage());
    delay(2000);
    ESP.restart();
}

static void handleCaptiveProbe() {
    if (apActive) {
        server.sendHeader("Location", "http://192.168.4.1/", true);
        server.send(302, "text/plain", "");
    } else {
        server.send(204, "text/plain", "");
    }
}

// Redirect all unknown paths to the portal (captive-portal behaviour)
static void handleNotFound() {
    if (apActive) {
        server.sendHeader("Location", "http://192.168.4.1/", true);
        server.send(302, "text/plain", "");
    } else {
        server.send(404, "text/plain", "Not found");
    }
}

// ---------------------------------------------------------------------------
// Register routes and start the web server (shared by AP and STA modes)
// ---------------------------------------------------------------------------
static void startServer() {
    if (!serverRoutesRegistered) {
        generateCsrfToken();  // new token per server (re)start
        server.on("/",               HTTP_GET,  handleRoot);
        server.on("/setup",          HTTP_GET,  handleSetup);
        server.on("/save",           HTTP_POST, handleSave);
        server.on("/settings",       HTTP_GET,  handleSettings);
        server.on("/save-settings",  HTTP_POST, handleSaveSettings);
        server.on("/reset-settings", HTTP_GET,  handleResetSettings);
        server.on("/forget-network", HTTP_POST, handleForgetNetwork);
        server.on("/update",         HTTP_GET,  []() {
            server.sendHeader("Cache-Control", "no-store, no-cache");
            server.sendHeader("Pragma", "no-cache");
            server.send(200, "text/html", otaPage());
        });
        server.on("/do-update",      HTTP_POST, handleOtaComplete, handleOtaUpload);
        server.on("/generate_204",   HTTP_GET,  handleCaptiveProbe);
        server.on("/hotspot-detect.html", HTTP_GET, handleCaptiveProbe);
        server.on("/ncsi.txt",       HTTP_GET,  handleCaptiveProbe);
        server.onNotFound(handleNotFound);
        serverRoutesRegistered = true;
    }

    if (!serverStarted) {
        server.begin();
        serverStarted = true;
        Serial.println("[Network] Web server started on port 80");
    }
}

// ---------------------------------------------------------------------------
// Start Access-Point + web portal
// ---------------------------------------------------------------------------
static void startAP() {
    if (mdnsActive) {
        MDNS.end();
        mdnsActive = false;
    }

    // Derive device-unique AP password if not manually set in config.h
    deriveApPassword();

    WiFi.mode(WIFI_AP);
    const char* apPass = getApPassword();
    WiFi.softAP(AP_SSID, apPass);

    if (dnsActive) {
        dnsServer.stop();
        dnsActive = false;
    }
    dnsServer.start(53, "*", WiFi.softAPIP());
    dnsActive = true;

    startServer();

    staConnected = false;
    apActive = true;
    Serial.printf("[Network] AP mode: SSID=%s  Pass=%s  IP=%s\n",
                  AP_SSID, getApPassword(), WiFi.softAPIP().toString().c_str());
}

// ---------------------------------------------------------------------------
// Try STA connection with stored credentials
// Returns true on success.
// ---------------------------------------------------------------------------
static bool connectSTA() {
    if (savedNetworkCount <= 0) return false;

    WiFi.mode(WIFI_STA);

    for (int i = 0; i < savedNetworkCount; i++) {
        const char* ssid = savedNetworks[i].ssid;
        const char* pass = savedNetworks[i].password;

        if (ssid[0] == '\0') continue;

        WiFi.disconnect(true);
        delay(100);
        WiFi.begin(ssid, pass);
        Serial.printf("[Network] Connecting to \"%s\"", ssid);

        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED) {
            if (millis() - start >= WIFI_CONNECT_TIMEOUT_MS) {
                Serial.println(" timeout.");
                break;
            }
            delay(250);
            Serial.print('.');
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf(" OK  IP=%s\n", WiFi.localIP().toString().c_str());
            if (i > 0) {
                moveNetworkToFront(i);
                persistSavedNetworks();
            }

            if (dnsActive) {
                dnsServer.stop();
                dnsActive = false;
            }

            if (mdnsActive) {
                MDNS.end();
                mdnsActive = false;
            }

            if (MDNS.begin("desknexus")) {
                MDNS.addService("http", "tcp", 80);
                mdnsActive = true;
                Serial.println("[Network] mDNS started: desknexus.local");
            } else {
                Serial.println("[Network] mDNS start failed.");
            }

            startServer();
            staConnected = true;
            apActive = false;
            return true;
        }
    }

    Serial.println("[Network] No saved networks connected.");
    return false;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/*
 * begin() — Call once from setup().
 * Tries STA first; falls back to AP mode.
 * Returns true if connected in STA mode.
 */
static bool begin() {
    loadCredentials();

    if (connectSTA()) {
        staConnected = true;
        apActive     = false;
        return true;
    }

    // STA failed → start AP captive portal
    startAP();
    return false;
}

/*
 * handle() — Call in every loop() iteration to process web requests
 * (works in both AP mode and STA mode).
 */
static void handle() {
    server.handleClient();
    if (apActive && dnsActive) {
        dnsServer.processNextRequest();
    }
}

/*
 * isConnected() — True when the device has internet access (STA mode).
 */
static bool isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

/*
 * ipAddress() — Current IP as a String.
 */
static String ipAddress() {
    if (apActive)  return WiFi.softAPIP().toString();
    return WiFi.localIP().toString();
}

/*
 * localAddress() — Human-friendly address for the device.
 * Returns "desknexus.local" in STA mode (mDNS) or "192.168.4.1" in AP mode.
 */
static String localAddress() {
    if (apActive) return "192.168.4.1";
    return "desknexus.local";
}

// WiFi reconnect state — exponential backoff for STA recovery
static int          reconnectFailures    = 0;
static unsigned long lastReconnectAttempt = 0;

// Backoff intervals (ms): 30s, 60s, 120s, 300s (cap)
static unsigned long reconnectBackoffMs() {
    const unsigned long intervals[] = { 30000, 60000, 120000, 300000 };
    int idx = reconnectFailures;
    if (idx < 0) idx = 0;
    if (idx >= 4) idx = 3;
    return intervals[idx];
}

/*
 * reconnect() — Attempt to reconnect if WiFi was lost.
 * Call periodically from loop().
 * Uses exponential backoff. Does NOT fall back to AP mode automatically —
 * keeps retrying STA in the background so the device recovers when the
 * router comes back.
 */
static void reconnect() {
    if (apActive) return;   // managed by portal
    if (WiFi.status() == WL_CONNECTED) {
        // Connection recovered (possibly by WiFi auto-reconnect)
        if (reconnectFailures > 0) {
            Serial.println("[Network] WiFi recovered.");
            reconnectFailures = 0;
        }
        return;
    }

    unsigned long now = millis();
    unsigned long backoff = reconnectBackoffMs();
    if (lastReconnectAttempt > 0 && (now - lastReconnectAttempt) < backoff) {
        return;  // wait for backoff period
    }
    lastReconnectAttempt = now;

    Serial.printf("[Network] WiFi lost — reconnect attempt #%d (backoff %lus)...\n",
                  reconnectFailures + 1, backoff / 1000);
    WiFi.disconnect(true);
    if (connectSTA()) {
        Serial.println("[Network] Reconnected to WiFi.");
        reconnectFailures = 0;
    } else {
        reconnectFailures++;
        Serial.printf("[Network] Reconnect failed — next retry in %lus.\n",
                      reconnectBackoffMs() / 1000);
    }
}

// ---------------------------------------------------------------------------
// AP-mode background scan — check if any saved network has come into range
// ---------------------------------------------------------------------------
static unsigned long lastAPScanMs = 0;

/*
 * checkKnownNetworkInAP() — While in AP mode, periodically scan for visible
 * Wi-Fi networks and compare them against the saved-network list.
 * If a known SSID is visible and the STA connection succeeds the device
 * restarts so it boots cleanly in STA mode with full initialisation.
 * If the connection attempt fails, the AP is restarted so the portal remains
 * accessible.  Call from loop() whenever apActive is true.
 */
static void checkKnownNetworkInAP() {
    if (!apActive) return;
    if (savedNetworkCount <= 0) return;

    unsigned long now = millis();
    if (lastAPScanMs > 0 && (now - lastAPScanMs) < AP_SCAN_INTERVAL_MS) return;
    lastAPScanMs = now;

    Serial.println("[Network] AP mode: scanning for known networks...");

    int n = WiFi.scanNetworks();   // blocking scan (~2-3 s)
    if (n <= 0) {
        WiFi.scanDelete();
        Serial.println("[Network] Scan complete — no networks found.");
        return;
    }

    bool knownFound = false;
    for (int i = 0; i < n && !knownFound; i++) {
        String vis = WiFi.SSID(i);
        for (int j = 0; j < savedNetworkCount; j++) {
            if (vis.equals(savedNetworks[j].ssid)) {
                Serial.printf("[Network] Known network visible: \"%s\"\n",
                              savedNetworks[j].ssid);
                knownFound = true;
                break;
            }
        }
    }
    WiFi.scanDelete();

    if (!knownFound) {
        Serial.println("[Network] No known networks visible.");
        return;
    }

    // Attempt STA connection — connectSTA() switches mode to WIFI_STA,
    // which terminates the soft-AP.  If it fails we restart the AP so the
    // captive portal stays accessible.
    Serial.println("[Network] Attempting STA connection from AP mode...");
    if (connectSTA()) {
        Serial.println("[Network] Connected — restarting to finish STA initialisation.");
        delay(500);
        ESP.restart();
    } else {
        Serial.println("[Network] STA connection failed — restarting AP.");
        startAP();
    }
}

} // namespace Network
