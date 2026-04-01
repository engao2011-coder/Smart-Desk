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
#include <WebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <time.h>
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
static Preferences prefs;
static bool        staConnected = false;
static bool        apActive     = false;
static bool        serverRoutesRegistered = false;
static bool        serverStarted = false;
static bool        mdnsActive = false;

// Persisted credentials
static char savedSSID[64]     = {};
static char savedPassword[64] = {};

// ---------------------------------------------------------------------------
// Load credentials from NVS
// ---------------------------------------------------------------------------
static void loadCredentials() {
    prefs.begin("wifi", true);   // read-only namespace
    prefs.getString("ssid",     savedSSID,     sizeof(savedSSID));
    prefs.getString("password", savedPassword, sizeof(savedPassword));
    prefs.end();
}

// ---------------------------------------------------------------------------
// Save credentials to NVS
// ---------------------------------------------------------------------------
static void saveCredentials(const char* ssid, const char* password) {
    prefs.begin("wifi", false);  // read-write namespace
    prefs.putString("ssid",     ssid);
    prefs.putString("password", password);
    prefs.end();
    strncpy(savedSSID,     ssid,     sizeof(savedSSID)     - 1);
    strncpy(savedPassword, password, sizeof(savedPassword) - 1);
}

// ---------------------------------------------------------------------------
// HTML helpers
// ---------------------------------------------------------------------------
static String wifiScanHTML() {
    // Scan for available networks
    int n = WiFi.scanNetworks();
    String options = "";
    for (int i = 0; i < n; i++) {
        options += "<option value=\"" + WiFi.SSID(i) + "\">" +
                   WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + " dBm)</option>\n";
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
</style>
</head>
<body>
<div class="card">
  <h2>&#128338; DeskNexus Setup</h2>
  <form method="POST" action="/save">
    <label for="ssid">Wi-Fi Network</label>
    <select name="ssid" id="ssid">
)rawhtml";
    html += wifiScanHTML();
    html += R"rawhtml(
    </select>
    <label for="pass">Password</label>
    <input type="password" id="pass" name="pass" placeholder="Wi-Fi password">
    <input type="submit" value="Connect">
  </form>
  <p class="msg">DeskNexus will restart after saving.</p>
</div>
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
    String ssid = String(savedSSID);
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
  h3{margin:20px 0 8px;color:#4ecca3;font-size:1rem;border-bottom:1px solid #0f3460;padding-bottom:4px}
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
</style>
</head>
<body>
<div class="card">
  <h2>&#9881; DeskNexus Settings</h2>
  <form method="POST" action="/save-settings">

    <h3>Location</h3>
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
      <div>
        <label for="method">Prayer Method</label>
        <select id="method" name="method">)rawhtml";

    struct { int id; const char* name; } methods[] = {
        {1,"Muslim World League"},{2,"ISNA"},{3,"Egypt"},
        {4,"Makkah (Umm Al-Qura)"},{5,"Karachi"},
        {8,"Gulf Region"},{16,"Turkey"},{17,"Tehran"}
    };
    for (auto& m : methods) {
        html += "<option value=\"" + String(m.id) + "\"";
        if (Settings::prayerMethod == m.id) html += " selected";
        html += ">" + String(m.name) + "</option>";
    }

    html += R"rawhtml(
        </select>
      </div>
    </div>

    <label for="utc">UTC Offset (seconds)</label>
    <input type="number" id="utc" name="utc" value=")rawhtml";
    html += String(Settings::utcOffset);
    html += R"rawhtml(">
        <p style="font-size:.8rem;color:#9bd;line-height:1.4;margin:6px 0 0">
            Auto-detect status: )rawhtml";
        html += String(Settings::autoDetectStatus);
        html += R"rawhtml(<br>
            Last result: )rawhtml";
        html += Settings::autoDetectLastOk ? "Success" : "Failure";
        html += R"rawhtml(
        </p>

    <h3>Stocks</h3>)rawhtml";

    for (int i = 0; i < MAX_STOCKS; i++) {
        html += "<label>Symbol " + String(i + 1) + "</label>";
        html += "<input type=\"text\" name=\"stk" + String(i) +
                "\" maxlength=\"23\" value=\"" + String(Settings::stockSymbols[i]) +
                "\" placeholder=\"e.g. IUSE or IUSE:LSE\" style=\"text-transform:uppercase\">";
    }

    html += R"rawhtml(

    <h3>API Keys</h3>
    <label for="owmkey">OpenWeatherMap Key</label>
    <input type="password" id="owmkey" name="owmkey" maxlength="46" value=")rawhtml";
    html += String(Settings::owmApiKey);
    html += R"rawhtml(">

    <input type="submit" value="Save Settings">
  </form>
  <div class="link"><a href="/">&#8592; Back to Status</a></div>
  <a class="rst" href="/reset-settings" onclick="return confirm('Reset all settings to defaults?')">Reset to Defaults</a>
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
    server.send(200, "text/html", settingsPage());
}

static void handleSaveSettings() {
    // City / Country
    String v = server.arg("city");
    if (v.length() > 0 && v.length() < sizeof(Settings::city))
        strncpy(Settings::city, v.c_str(), sizeof(Settings::city) - 1);

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
    Settings::resetToDefaults();
    server.sendHeader("Location", "/settings", true);
    server.send(302, "text/plain", "");
}

static void handleSetup() {
    server.send(200, "text/html", portalPage());
}

static void handleSave() {
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
        server.on("/",               HTTP_GET,  handleRoot);
        server.on("/setup",          HTTP_GET,  handleSetup);
        server.on("/save",           HTTP_POST, handleSave);
        server.on("/settings",       HTTP_GET,  handleSettings);
        server.on("/save-settings",  HTTP_POST, handleSaveSettings);
        server.on("/reset-settings", HTTP_GET,  handleResetSettings);
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

    WiFi.mode(WIFI_AP);
    // Note: passing nullptr creates an open (password-free) AP.
    // Set AP_PASSWORD in config.h to a non-empty string for a secured AP.
    WiFi.softAP(AP_SSID, strlen(AP_PASSWORD) > 0 ? AP_PASSWORD : nullptr);

    startServer();

    apActive = true;
    Serial.printf("[Network] AP mode: SSID=%s  IP=%s\n",
                  AP_SSID, WiFi.softAPIP().toString().c_str());
}

// ---------------------------------------------------------------------------
// Try STA connection with stored credentials
// Returns true on success.
// ---------------------------------------------------------------------------
static bool connectSTA() {
    if (strlen(savedSSID) == 0) return false;

    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSSID, savedPassword);
    Serial.printf("[Network] Connecting to \"%s\"", savedSSID);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start >= WIFI_CONNECT_TIMEOUT_MS) {
            Serial.println(" timeout.");
            return false;
        }
        delay(250);
        Serial.print('.');
    }
    Serial.printf(" OK  IP=%s\n", WiFi.localIP().toString().c_str());
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
    return true;
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

/*
 * reconnect() — Attempt to reconnect if WiFi was lost.
 * Call periodically from loop().
 */
static void reconnect() {
    if (apActive) return;   // managed by portal
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Network] WiFi lost — reconnecting...");
        WiFi.disconnect();
        connectSTA();
    }
}

} // namespace Network
