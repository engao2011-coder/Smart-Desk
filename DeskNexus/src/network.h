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
#include "config.h"

// ---------------------------------------------------------------------------
// Module-level state
// ---------------------------------------------------------------------------
namespace Network {

static WebServer   server(80);
static Preferences prefs;
static bool        staConnected = false;
static bool        apActive     = false;

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

// ---------------------------------------------------------------------------
// Web server route handlers
// ---------------------------------------------------------------------------
static void handleRoot() {
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
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
}

// ---------------------------------------------------------------------------
// Start Access-Point + web portal
// ---------------------------------------------------------------------------
static void startAP() {
    WiFi.mode(WIFI_AP);
    // Note: passing nullptr creates an open (password-free) AP.
    // Set AP_PASSWORD in config.h to a non-empty string for a secured AP.
    WiFi.softAP(AP_SSID, strlen(AP_PASSWORD) > 0 ? AP_PASSWORD : nullptr);

    server.on("/",     HTTP_GET,  handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.onNotFound(handleNotFound);
    server.begin();

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
    MDNS.begin("desknexus");
    Serial.println("[Network] mDNS started: desknexus.local");
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
 * handle() — Call in every loop() iteration while in AP mode to
 * process web-portal requests.
 */
static void handle() {
    if (apActive) {
        server.handleClient();
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
