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

// ---------------------------------------------------------------------------
// HTTP Basic auth for sensitive routes (settings pages + OTA firmware upload).
// Username is "admin"; the password is the same device password shown on the
// AP setup screen — AP_PASSWORD from config.h if set, otherwise the MAC-derived
// "deskXXXX". Reusing it keeps the web-admin and AP-join password in sync so
// there is nothing extra for the user to remember.
// ---------------------------------------------------------------------------
static const char* WEB_ADMIN_USER = "admin";

// Active web-admin password: the user-set override if present, otherwise the
// MAC-derived AP password. Kept separate from getApPassword() so changing the
// admin password does not affect the AP (hotspot) join password.
static const char* getAdminPassword() {
    if (Settings::adminPassword[0] != '\0') return Settings::adminPassword;
    return getApPassword();
}

// True when the request carries valid credentials (does not send a response).
static bool isAuthed() {
    return server.authenticate(WEB_ADMIN_USER, getAdminPassword());
}

// Gate a handler: returns true if authorised, otherwise sends a 401 challenge
// and returns false so the caller can bail out early.
static bool requireAuth() {
    if (isAuthed()) return true;
    server.requestAuthentication(BASIC_AUTH, "DeskNexus");
    return false;
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

// ---------------------------------------------------------------------------
// Shared web-page styling
// Rules common to every full page (portal, status, settings, OTA) live in
// STYLE_BASE so the dark theme stays consistent and isn't copy-pasted four
// times. Each page appends its own rules after the base.
// ---------------------------------------------------------------------------
static const char STYLE_BASE[] =
  "*{-webkit-tap-highlight-color:transparent}"
  "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Arial,sans-serif;"
  "background:#0d1015;color:#e6eaf0;"
  "display:flex;justify-content:center;align-items:center;min-height:100vh;margin:0}"
  ".card{background:#161b22;border:1px solid #232b35;border-radius:16px;padding:28px;max-width:420px;width:90%;"
  "box-shadow:0 10px 34px #0008}"
  "h2{margin:0 0 22px;color:#fff;text-align:center;font-weight:600;font-size:1.3rem;letter-spacing:.2px}"
  "a{color:#3dd9a0}";

static const char PORTAL_CSS[] =
  ".card{max-width:400px}"
  "label{display:block;margin:14px 0 5px;font-size:.82rem;color:#9ba6b4;letter-spacing:.2px}"
  "select,input{width:100%;padding:11px;border:1px solid #2a323d;border-radius:8px;"
  "background:#1c232d;color:#e6eaf0;font-size:1rem;box-sizing:border-box}"
  "select:focus,input:focus{outline:none;border-color:#ffb053}"
  "input[type=submit]{background:#ffb053;color:#3a2402;cursor:pointer;margin-top:22px;font-weight:600;border:none}"
  "input[type=submit]:hover{background:#e89a3d}"
  ".msg{text-align:center;color:#9ba6b4;margin-top:16px;font-size:.85rem}"
  ".pw-wrap{position:relative}.pw-wrap input{padding-right:44px}"
  ".pw-toggle{position:absolute;right:6px;top:50%;transform:translateY(-50%);"
  "background:none;border:none;color:#9ba6b4;cursor:pointer;font-size:1.1rem;padding:4px 6px;width:auto}"
  ".refresh{display:inline-block;margin:8px 0;color:#3dd9a0;font-size:.8rem;"
  "cursor:pointer;text-decoration:none;background:none;border:none}"
  "#overlay{display:none;position:fixed;top:0;left:0;width:100%;height:100%;"
  "background:#0d1015;z-index:999;flex-direction:column;justify-content:center;align-items:center}"
  ".spinner{width:48px;height:48px;border:4px solid #232b35;border-top-color:#ffb053;"
  "border-radius:50%;animation:spin 1s linear infinite;margin-bottom:20px}"
  "@keyframes spin{to{transform:rotate(360deg)}}";

static const char STATUS_CSS[] =
  "table{width:100%;border-collapse:collapse}"
  "td{padding:12px 4px;border-bottom:1px solid #1e2630;font-size:.92rem}"
  "td:first-child{color:#9ba6b4;width:42%}"
  "tr:last-child td{border-bottom:none}"
  ".badge{display:inline-block;background:rgba(61,217,160,.16);color:#3dd9a0;border-radius:6px;"
  "padding:3px 10px;font-size:.78rem;font-weight:600}"
  "a.btn{display:block;text-align:center;margin-top:10px;padding:12px;"
  "background:#ffb053;color:#3a2402;border-radius:9px;text-decoration:none;font-weight:600}"
  "a.btn:hover{background:#e89a3d}"
  "a.btn.alt{background:transparent;color:#cdd5df;border:1px solid #34404d;font-weight:500}"
  "a.btn.alt:hover{background:#1c232d}"
  ".btn-link{display:block;text-align:center;margin-top:16px;color:#6b7682;"
  "font-size:.84rem;text-decoration:none}.btn-link:hover{color:#3dd9a0}";

static const char SETTINGS_CSS[] =
  "body{display:block;padding:14px 0 88px}"
  ".card{max-width:460px;margin:0 auto;background:transparent;border:none;box-shadow:none;padding:0 14px}"
  "h2{display:none}"
  // device header strip
  ".shead{display:flex;align-items:center;gap:11px;background:#161b22;border:1px solid #232b35;"
  "border-radius:14px;padding:14px 15px;margin-bottom:12px}"
  ".shead .ic{width:34px;height:34px;border-radius:9px;background:rgba(255,176,83,.14);"
  "display:flex;align-items:center;justify-content:center;font-size:1.1rem}"
  ".shead .nm{font-size:1rem;font-weight:600;color:#fff}"
  ".shead .sub{font-size:.74rem;color:#6b7682}"
  ".shead .on{margin-left:auto;font-size:.68rem;font-weight:600;color:#3dd9a0;"
  "background:rgba(61,217,160,.16);padding:3px 9px;border-radius:6px}"
  // labels & inputs
  "label{display:block;margin:12px 0 4px;font-size:.78rem;color:#9ba6b4}"
  "select,input[type=text],input[type=number],input[type=password]{"
  "width:100%;padding:10px;border:1px solid #2a323d;border-radius:8px;"
  "background:#1c232d;color:#e6eaf0;font-size:.95rem;box-sizing:border-box}"
  "select:focus,input:focus{outline:none;border-color:#ffb053}"
  ".row{display:flex;gap:10px}.row>div{flex:1}"
  ".hint{font-size:.76rem;color:#6b7682;line-height:1.5;margin:6px 0 0}"
  // collapsible section cards
  "details.sec{background:#161b22;border:1px solid #232b35;border-radius:13px;margin:11px 0;overflow:hidden}"
  "details.sec>summary{display:flex;align-items:center;gap:9px;padding:13px 15px;cursor:pointer;"
  "list-style:none;font-size:.85rem;font-weight:600;color:#e6eaf0}"
  "details.sec>summary::-webkit-details-marker{display:none}"
  "details.sec>summary .ico{font-size:1.02rem;line-height:1}"
  "details.sec>summary .ttl{flex:1}"
  ".chev{color:#6b7682;font-size:.7rem;transition:transform .2s}"
  "details.sec[open]>summary .chev{transform:rotate(180deg)}"
  ".chip{font-size:.66rem;font-weight:600;padding:2px 8px;border-radius:5px}"
  ".chip.ok{color:#3dd9a0;background:rgba(61,217,160,.14)}"
  ".chip.warn{color:#ffb053;background:rgba(255,176,83,.14)}"
  ".chip.mut{color:#9ba6b4;background:#1c232d}"
  ".sbody{padding:2px 15px 15px;border-top:1px solid #1e2630}"
  // nested collapsibles (per-stock, scoring help, saved wifi)
  ".sbody details{margin:10px 0;border:1px solid #232b35;border-radius:9px;background:#12161c}"
  ".sbody details>summary{display:flex;align-items:center;gap:7px;padding:9px 12px;cursor:pointer;"
  "list-style:none;font-size:.8rem;color:#cdd5df}"
  ".sbody details>summary::-webkit-details-marker{display:none}"
  ".sbody details>summary::before{content:'\xE2\x96\xB8';color:#6b7682;font-size:.66rem}"
  ".sbody details[open]>summary::before{content:'\xE2\x96\xBE'}"
  ".ndbody{padding:0 12px 12px}"
  // segmented control (radios)
  ".seg{display:flex;background:#1c232d;border:1px solid #2a323d;border-radius:9px;overflow:hidden;margin-top:6px}"
  ".seg input{position:absolute;opacity:0;pointer-events:none}"
  ".seg label{flex:1;margin:0;text-align:center;padding:9px 4px;font-size:.82rem;color:#9ba6b4;cursor:pointer}"
  ".seg input:checked+label{background:#ffb053;color:#3a2402;font-weight:600}"
  // toggle switch
  ".tgl{display:flex;align-items:center;gap:10px;margin:14px 0 0;font-size:.82rem;color:#cdd5df;cursor:pointer}"
  ".sw{position:relative;display:inline-block;width:38px;height:21px;flex:none}"
  ".sw input{opacity:0;width:0;height:0}"
  ".sw .sl{position:absolute;inset:0;background:#2a323d;border-radius:11px;transition:.2s}"
  ".sw .sl:before{content:'';position:absolute;width:15px;height:15px;left:3px;top:3px;"
  "background:#e6eaf0;border-radius:50%;transition:.2s}"
  ".sw input:checked+.sl{background:#ffb053}"
  ".sw input:checked+.sl:before{transform:translateX(17px);background:#3a2402}"
  // sticky save bar
  ".savebar{position:fixed;left:0;right:0;bottom:0;z-index:50;display:flex;align-items:center;gap:12px;"
  "padding:11px 16px;background:rgba(17,21,27,.96);border-top:1px solid #232b35}"
  ".savebar .st{flex:1;font-size:.74rem;color:#6b7682}"
  ".savebar .st.dirty{color:#ffb053}"
  ".savebar input[type=submit]{width:auto;margin:0;padding:11px 24px;border:none;border-radius:9px;"
  "background:#ffb053;color:#3a2402;cursor:pointer;font-weight:600;font-size:.92rem}"
  ".savebar input[type=submit]:hover{background:#e89a3d}"
  // footer links
  ".link{text-align:center;margin-top:16px}.link a{color:#3dd9a0;font-size:.85rem}"
  ".rst{display:block;text-align:center;margin:12px 0 4px;color:#ff6470;font-size:.8rem;text-decoration:none}"
  // saved toast
  ".toast{position:fixed;top:14px;left:50%;transform:translateX(-50%);z-index:60;"
  "background:#13241c;border:1px solid #2e6b50;color:#3dd9a0;font-size:.84rem;font-weight:600;"
  "padding:10px 18px;border-radius:10px;box-shadow:0 8px 24px #0007;transition:opacity .4s}";

static const char OTA_CSS[] =
  "label{display:block;margin:12px 0 4px;font-size:.85rem;color:#9ba6b4}"
  "input[type=file]{width:100%;padding:11px;border:1px solid #2a323d;border-radius:8px;"
  "background:#1c232d;color:#e6eaf0;font-size:.9rem;box-sizing:border-box}"
  "input[type=submit]{width:100%;padding:13px;border:none;border-radius:9px;"
  "background:#ffb053;color:#3a2402;cursor:pointer;margin-top:18px;font-weight:600;font-size:1rem}"
  "input[type=submit]:hover{background:#e89a3d}"
  "input[type=submit]:disabled{background:#34404d;color:#7a8694;cursor:not-allowed}"
  ".warn{color:#ffb053;font-size:.82rem;margin-top:10px;text-align:center}"
  ".msg{text-align:center;color:#3dd9a0;margin-top:12px;font-size:.88rem;min-height:1.2em}"
  "a.btn{display:block;text-align:center;margin-top:16px;color:#9ba6b4;font-size:.85rem;text-decoration:none}"
  "progress{width:100%;height:16px;border-radius:8px;margin-top:16px;display:none;border:none}"
  "progress::-webkit-progress-bar{background:#1c232d;border-radius:8px}"
  "progress::-webkit-progress-value{background:#ffb053;border-radius:8px}";

// Build the shared <head> (meta + base style + page-specific style). Centralises
// the boilerplate so a page body is just pageHead(title, css) + its markup.
static String pageHead(const char* title, const char* extraCss) {
    String h = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>)rawhtml";
    h += title;
    h += R"rawhtml(</title>
<style>)rawhtml";
    h += STYLE_BASE;
    h += extraCss;
    h += R"rawhtml(</style>
</head>
)rawhtml";
    return h;
}

static String portalPage() {
    String html = pageHead("DeskNexus Setup", PORTAL_CSS);
    html += R"rawhtml(<body>
<div class="card">
  <h2>&#128338; DeskNexus Setup</h2>
  <form id="wifiForm" method="POST" action="/save">
    <input type="hidden" name="csrf" value=")rawhtml";
    html += String(csrfToken);
    html += R"rawhtml(">
    <label for="ssid">Wi-Fi Network</label>
    <select name="ssid" id="ssid">
)rawhtml";
    html += wifiScanHTML();
    html += R"rawhtml(
    </select>
    <button type="button" class="refresh" aria-label="Rescan Wi-Fi networks" onclick="location.reload()">&#8635; Rescan networks</button>
    <label for="pass">Password</label>
    <div class="pw-wrap">
      <input type="password" id="pass" name="pass" placeholder="Wi-Fi password" autocomplete="current-password">
      <button type="button" class="pw-toggle" aria-label="Show or hide password" onclick="var p=document.getElementById('pass');p.type=p.type==='password'?'text':'password'">&#128065;</button>
    </div>
    <input type="submit" value="Connect">
  </form>
  <p class="msg">DeskNexus will restart after saving.</p>
</div>
<div id="overlay">
  <div class="spinner"></div>
  <h2 style="color:#ffb053">Connecting...</h2>
  <p style="color:#9ba6b4">DeskNexus is restarting.<br>Reconnect to your home WiFi.</p>
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
<style>body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Arial,sans-serif;background:#0d1015;color:#e6eaf0;
display:flex;justify-content:center;align-items:center;min-height:100vh;text-align:center}h2{font-weight:600}</style>
</head>
<body><h2 style="color:#3dd9a0">&#10003; Credentials saved!<br>Device is restarting...</h2></body>
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

    String html = pageHead("DeskNexus Status", STATUS_CSS);
    html += R"rawhtml(<body>
<div class="card">
  <h2>&#128338; DeskNexus</h2>
  <table>
    <tr><td>Status</td><td><span class="badge">Online</span></td></tr>
    <tr><td>Network</td><td>)rawhtml";
    html += htmlEscape(ssid);
    html += R"rawhtml(</td></tr>
    <tr><td>IP Address</td><td>)rawhtml";
    html += ip;
    html += R"rawhtml(</td></tr>
    <tr><td>Hostname</td><td>desknexus.local</td></tr>
    <tr><td>Uptime</td><td>)rawhtml";
    html += String(uptime);
    html += R"rawhtml(</td></tr>
  </table>
  <a class="btn" href="/settings" style="margin-top:22px">&#9881; Settings</a>
  <a class="btn alt" href="/setup">&#128246; Wi-Fi Setup</a>
  <a class="btn-link" href="/update">&#128260; Firmware Update</a>
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
    String html = pageHead("DeskNexus Settings", SETTINGS_CSS);
    html += R"rawhtml(<body>
<div class="card">)rawhtml";

    // One-shot confirmation toast after a successful save redirect (/settings?saved=1).
    if (server.arg("saved") == "1")
        html += R"rawhtml(<div class="toast" id="toast">&#10003; Settings saved</div>)rawhtml";

    html += R"rawhtml(
  <div class="shead">
    <div class="ic">&#9881;</div>
    <div><div class="nm">DeskNexus</div><div class="sub">desknexus.local &middot; )rawhtml";
    html += WiFi.localIP().toString();
    html += R"rawhtml(</div></div>
    <span class="on">Online</span>
  </div>
  <form method="POST" action="/save-settings">
    <input type="hidden" name="csrf" value=")rawhtml";
    html += String(csrfToken);
    html += R"rawhtml(">

    <details class="sec" open>
    <summary><span class="ico">&#128205;</span><span class="ttl">Location &amp; time</span><span class="chev">&#9662;</span></summary>
    <div class="sbody">
    <div class="row">
      <div>
        <label for="city">City</label>
        <input type="text" id="city" name="city" maxlength="60" autocomplete="off" spellcheck="false" value=")rawhtml";
    html += htmlEscape(String(Settings::city));
    html += R"rawhtml(">
      </div>
      <div>
        <label for="country">Country (ISO)</label>
        <input type="text" id="country" name="country" maxlength="4" autocapitalize="characters" autocomplete="off" spellcheck="false" value=")rawhtml";
    html += htmlEscape(String(Settings::country));
    html += R"rawhtml(">
      </div>
    </div>
    <label class="tgl">
      <span class="sw"><input type="checkbox" name="autoCity" value="1")rawhtml";
    if (!Settings::cityManual) html += " checked";
    html += R"rawhtml(><span class="sl"></span></span>
      Auto-detect city from IP (off locks to manual city)
    </label>

    <label for="utc">Timezone (UTC offset)</label>
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
    <p class="hint">Auto-detect: )rawhtml";
    html += htmlEscape(String(Settings::autoDetectStatus));
    html += " (";
    html += Settings::autoDetectLastOk ? "OK" : "Failed";
    html += R"rawhtml()</p>
    </div>
    </details>

    )rawhtml";

    // Never echo the stored key back; show only whether one is set. A blank
    // submit preserves the existing key (see handleSaveSettings).
    bool hasOwmKey = strlen(Settings::owmApiKey) > 0 &&
                     strcmp(Settings::owmApiKey, "YOUR_OPENWEATHERMAP_API_KEY") != 0;

    html += R"rawhtml(<details class="sec" open>
    <summary><span class="ico">&#9925;</span><span class="ttl">Weather</span>)rawhtml";
    html += hasOwmKey ? R"rawhtml(<span class="chip ok">key set</span>)rawhtml"
                      : R"rawhtml(<span class="chip warn">no key</span>)rawhtml";
    html += R"rawhtml(<span class="chev">&#9662;</span></summary>
    <div class="sbody">
    <label for="owmkey">OpenWeatherMap API key</label>
    <input type="password" id="owmkey" name="owmkey" maxlength="46" value="" placeholder=")rawhtml";
    html += hasOwmKey ? "Saved - leave blank to keep current key"
                      : "Enter your API key";
    html += R"rawhtml(">
    <p class="hint">Free key at <a href="https://openweathermap.org/appid" target="_blank">openweathermap.org/appid</a>. Weather is optional &mdash; clock, prayers and stocks work without it.</p>
    <label>Units</label>
    <div class="seg">
      <input type="radio" id="u-m" name="units" value="metric")rawhtml";
    if (String(Settings::owmUnits) != "imperial") html += " checked";
    html += R"rawhtml(><label for="u-m">Metric &deg;C</label>
      <input type="radio" id="u-i" name="units" value="imperial")rawhtml";
    if (String(Settings::owmUnits) == "imperial") html += " checked";
    html += R"rawhtml(><label for="u-i">Imperial &deg;F</label>
    </div>
    </div>
    </details>

    )rawhtml";

    // Prayer method short name for the section chip.
    const char* pmShort = "Custom";
    switch (Settings::prayerMethod) {
        case 1:  pmShort = "MWL";     break;
        case 2:  pmShort = "ISNA";    break;
        case 3:  pmShort = "Egypt";   break;
        case 4:  pmShort = "Makkah";  break;
        case 5:  pmShort = "Karachi"; break;
        case 8:  pmShort = "Gulf";    break;
        case 16: pmShort = "Diyanet"; break;
        case 17: pmShort = "Tehran";  break;
    }
    html += R"rawhtml(<details class="sec" open>
    <summary><span class="ico">&#128338;</span><span class="ttl">Prayer times</span><span class="chip mut">)rawhtml";
    html += pmShort;
    html += R"rawhtml(</span><span class="chev">&#9662;</span></summary>
    <div class="sbody">
    <label for="method">Calculation method</label>
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
    <p class="hint">Choose the method your local mosque follows. Default: Makkah (Umm Al-Qura).</p>
    </div>
    </details>

    <details class="sec">
    <summary><span class="ico">&#127912;</span><span class="ttl">Display</span><span class="chev">&#9662;</span></summary>
    <div class="sbody">
    <label>Theme</label>
    <div class="seg">
      <input type="radio" id="th-a" name="theme" value="auto")rawhtml";
    if (Settings::themeMode == 0) html += " checked";
    html += R"rawhtml(><label for="th-a">Auto</label>
      <input type="radio" id="th-d" name="theme" value="dark")rawhtml";
    if (Settings::themeMode == 1) html += " checked";
    html += R"rawhtml(><label for="th-d">Dark</label>
      <input type="radio" id="th-l" name="theme" value="light")rawhtml";
    if (Settings::themeMode == 2) html += " checked";
    html += R"rawhtml(><label for="th-l">Light</label>
    </div>
    <p class="hint">Auto switches to dark at night based on prayer times.</p>
    </div>
    </details>

    )rawhtml";

    // Stocks status chip: count configured / ok / needs-attention slots.
    {
        int cfg = 0, okC = 0, badC = 0;
        for (int i = 0; i < MAX_STOCKS; i++) {
            if (strlen(Settings::stockSymbols[i]) == 0) continue;
            cfg++;
            if (Stocks::needsAttention(i))    badC++;
            else if (Stocks::quotes[i].valid) okC++;
        }
        html += R"rawhtml(<details class="sec">
    <summary><span class="ico">&#128200;</span><span class="ttl">Stocks</span>)rawhtml";
        if (cfg == 0)      html += R"rawhtml(<span class="chip mut">off</span>)rawhtml";
        else if (badC > 0) html += "<span class=\"chip warn\">" + String(badC) + " check</span>";
        else               html += "<span class=\"chip ok\">" + String(okC) + " ok</span>";
        html += R"rawhtml(<span class="chev">&#9662;</span></summary>
    <div class="sbody">)rawhtml";
    }
    html += R"rawhtml(
    <label for="stkRef">Refresh interval (minutes)</label>
    <input type="number" id="stkRef" name="stkRef" min="1" max="60" value=")rawhtml";
    html += String(Settings::stockRefreshMin);
    html += R"rawhtml(">
    <p class="hint">How often to refresh prices. Default 5 min. Range 1&ndash;60.</p>
    <label class="tgl">
      <span class="sw"><input type="checkbox" name="stkEur" value="1")rawhtml";
    if (Settings::stockEuro) html += " checked";
    html += R"rawhtml(><span class="sl"></span></span>
      Show prices in EUR (live USD/GBP&rarr;EUR rate)
    </label>
    <p class="hint">Yahoo Finance symbols, one per slot &mdash; leave empty to disable. London (.L) converts from GBX (pence); .DE/.PA/.AS shown as-is; others treated as USD. The device shows both the daily change (1D) and the change vs the 52-week high (52W).</p>
    )rawhtml";

    for (int i = 0; i < MAX_STOCKS; i++) {
        html += "<label>Symbol " + String(i + 1);
        // Live fetch status next to the input the user edits to fix it.
        if (strlen(Settings::stockSymbols[i]) > 0) {
            const char* txt; const char* col;
            if (Stocks::needsAttention(i))    { txt = "&#10007; not found &mdash; check symbol"; col = "#ff6470"; }
            else if (Stocks::quotes[i].valid) { txt = "&#10003; ok";                              col = "#3dd9a0"; }
            else                              { txt = "&hellip; loading";                         col = "#6b7682"; }
            html += " <span style=\"font-size:.78rem;font-weight:600;color:" + String(col) + "\">" + txt + "</span>";
            // Show the resolved company/fund name once it has been fetched.
            if (Stocks::quotes[i].name[0]) {
                html += " <span style=\"font-size:.78rem;color:#6b7682\">&middot; " +
                        htmlEscape(String(Stocks::quotes[i].name)) + "</span>";
            }
        }
        html += "</label>";
        html += "<input type=\"text\" name=\"stk" + String(i) +
                "\" maxlength=\"23\" autocapitalize=\"characters\" autocomplete=\"off\" spellcheck=\"false\" value=\"" +
                htmlEscape(String(Settings::stockSymbols[i])) +
                "\" placeholder=\"e.g. IUSE.L\" style=\"text-transform:uppercase\">";
    }

    html += R"rawhtml(
    </div>
    </details>

    <details class="sec">
    <summary><span class="ico">&#9878;</span><span class="ttl">Buy/sell signal</span><span class="chev">&#9662;</span></summary>
    <div class="sbody">
    <p class="hint">A mechanical, DCA-oriented technical rating shown on the Stocks page and Home card &mdash; <b>not financial advice</b>. It's buy-biased: it leans toward buying dips and never recommends a full sell (worst rating is TRIM). Each stock is tuned independently below.</p>

    )rawhtml";

    // Per-stock signal parameters — one collapsible group per configured symbol.
    {
        int configured = 0;
        for (int i = 0; i < MAX_STOCKS; i++) {
            if (strlen(Settings::stockSymbols[i]) == 0) continue;
            configured++;
            String si = String(i);
            String title = htmlEscape(String(Settings::stockSymbols[i]));
            if (Stocks::quotes[i].name[0])
                title += " &middot; " + htmlEscape(String(Stocks::quotes[i].name));
            html += "<details><summary>" + title + "</summary><div class=\"ndbody\">";
            html += "<label>Trend average length (weeks)</label>"
                    "<input type=\"number\" name=\"sigSma_" + si + "\" min=\"2\" max=\"52\" value=\"" + String(Settings::signalSmaWeeks[i]) + "\">";
            html += "<label>Trend strength band (%)</label>"
                    "<input type=\"number\" step=\"0.5\" name=\"sigTrn_" + si + "\" min=\"0.5\" max=\"50\" value=\"" + String(Settings::signalTrendPct[i], 1) + "\">";
            html += "<label>Momentum window (weeks)</label>"
                    "<input type=\"number\" name=\"sigMmW_" + si + "\" min=\"1\" max=\"26\" value=\"" + String(Settings::signalMomWeeks[i]) + "\">";
            html += "<label>Momentum strength band (%)</label>"
                    "<input type=\"number\" step=\"0.5\" name=\"sigMmP_" + si + "\" min=\"0.5\" max=\"50\" value=\"" + String(Settings::signalMomPct[i], 1) + "\">";
            html += "<label>52-week range edge (%, double weighted)</label>"
                    "<input type=\"number\" step=\"1\" name=\"sigRng_" + si + "\" min=\"1\" max=\"49\" value=\"" + String(Settings::signalRangeEdge[i], 0) + "\">";
            html += "<label>Minimum history (weeks)</label>"
                    "<input type=\"number\" name=\"sigMnW_" + si + "\" min=\"2\" max=\"52\" value=\"" + String(Settings::signalMinWeeks[i]) + "\">";
            html += "</div></details>";
        }
        if (configured == 0)
            html += "<p class=\"hint\">Add one or more stock symbols above first, then come back to tune each one.</p>";
    }

    html += R"rawhtml(
    <details>
    <summary>How scoring works</summary>
    <div class="ndbody">
    <p class="hint"><b>Factors</b> (per stock): <i>Trend</i> = price vs its moving average (&plusmn;1, or &plusmn;2 past the band). <i>Momentum</i> = price move over the window (&plusmn;1, or &plusmn;2 past the band). <i>Position</i> = where price sits in its 52-week range, <b>double-weighted</b> (+2 near the low, &minus;2 near the high). Per-stock defaults: 40&nbsp;wk / 5% / 10&nbsp;wk / 6% / 20% / 8&nbsp;wk.</p>
    <p class="hint"><b>Scoring:</b> total &ge; +3 &rarr; <b>STRONG BUY</b> (add aggressively); +1/+2 &rarr; <b>BUY</b> (add); &minus;2&hellip;0 &rarr; <b>HOLD</b> (stick to regular DCA); &le; &minus;3 &rarr; <b>TRIM</b> (trim slightly only, not a full sell).</p>
    </div>
    </details>
    </div>
    </details>

    <details class="sec">
    <summary><span class="ico">&#128274;</span><span class="ttl">Admin password</span><span class="chev">&#9662;</span></summary>
    <div class="sbody">
    <p class="hint">Protects the settings page and firmware updates. Username is always <b>admin</b>. Currently using )rawhtml";
    html += Settings::adminPassword[0] ? "a custom password." : "the default device password (shown on the setup screen).";
    html += R"rawhtml(</p>
    <label for="admPw">New password</label>
    <input type="password" id="admPw" name="admPw" maxlength="63" value="" placeholder="leave blank to keep current" autocomplete="new-password">
    <label for="admPw2">Confirm new password</label>
    <input type="password" id="admPw2" name="admPw2" maxlength="63" value="" placeholder="repeat new password" autocomplete="new-password">
    <p class="hint">Minimum 8 characters. Leave both blank to keep the current password. After changing it your browser will ask you to sign in again.</p>
    <label class="tgl">
      <span class="sw"><input type="checkbox" name="admPwClear" value="1"><span class="sl"></span></span>
      Reset to the default device password
    </label>
    </div>
    </details>

    <div class="savebar">
      <span class="st" id="savest">No unsaved changes</span>
      <input type="submit" value="Save settings">
    </div>
  </form>

  <details class="sec">
  <summary><span class="ico">&#128246;</span><span class="ttl">Saved Wi-Fi networks</span><span class="chev">&#9662;</span></summary>
  <div class="sbody">)rawhtml";
    if (savedNetworkCount == 0) {
        html += "<p class=\"hint\">No saved networks.</p>";
    } else {
        for (int i = 0; i < savedNetworkCount; i++) {
            String sSafe = htmlEscape(String(savedNetworks[i].ssid));
            html += "<div style=\"display:flex;justify-content:space-between;align-items:center;padding:8px 0;border-bottom:1px solid #1e2630\">";
            html += "<span style=\"font-size:.95rem\">" + sSafe + "</span>";
            html += "<form method=\"POST\" action=\"/forget-network\" style=\"margin:0\">";
            html += "<input type=\"hidden\" name=\"csrf\" value=\"" + String(csrfToken) + "\">";
            html += "<input type=\"hidden\" name=\"idx\" value=\"" + String(i) + "\">";
            html += "<button type=\"submit\" style=\"background:#ff6470;color:#3a0d11;border:none;border-radius:6px;padding:5px 12px;font-size:.8rem;font-weight:600;cursor:pointer\" onclick=\"return confirm('Forget " + sSafe + "?')\">Forget</button>";
            html += "</form></div>";
        }
    }
    html += R"rawhtml(
  </div>
  </details>
  <div class="link"><a href="/">&#8592; Back to status</a></div>
  <a class="rst" href="/reset-settings?csrf=)rawhtml";
    html += String(csrfToken);
    html += R"rawhtml(" onclick="return confirm('Reset all settings to defaults?')">Reset to defaults</a>
</div>
<script>
(function(){
  var f=document.querySelector('form'), s=document.getElementById('savest');
  if(f&&s) f.addEventListener('input',function(){s.textContent='Unsaved changes';s.className='st dirty';});
  var t=document.getElementById('toast');
  if(t){setTimeout(function(){t.style.opacity='0';},2200);setTimeout(function(){t.remove();},2700);}
})();
</script>
</body>
</html>
)rawhtml";
    return html;
}

// Error page shown when a submitted setting fails validation. Nothing was
// saved; the user goes back to fix the form.
static String settingsErrorPage(const char* msg) {
    String html = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Not saved</title>
<style>body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Arial,sans-serif;background:#0d1015;color:#e6eaf0;
display:flex;flex-direction:column;justify-content:center;align-items:center;min-height:100vh;text-align:center;margin:0;padding:0 16px}
h2{font-weight:600}a{color:#3dd9a0}</style>
</head>
<body><h2 style="color:#ff6470">&#9888; Not saved</h2><p>)rawhtml";
    html += htmlEscape(String(msg));
    html += R"rawhtml(</p><p>No changes were applied. <a href="/settings">Go back</a> and try again.</p></body>
</html>
)rawhtml";
    return html;
}


// ---------------------------------------------------------------------------
// OTA firmware update page
// ---------------------------------------------------------------------------
static String otaPage() {
    String html = pageHead("DeskNexus Firmware Update", OTA_CSS);
    html += R"rawhtml(<body>
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
        msg.style.color = '#ff6470';
        msg.textContent = '✗ Update failed: ' + xhr.responseText;
        btn.disabled = false;
      }
    };
    xhr.onerror = function() {
      msg.style.color = '#ff6470';
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

        // Require valid admin credentials before accepting any firmware bytes.
        if (!isAuthed()) {
            Serial.println("[OTA] HTTP update rejected: authentication required.");
            return;  // _otaCsrfOk stays false → no flash write, 401 sent by handleOtaComplete
        }

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
    if (!requireAuth()) return;
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
    if (!requireAuth()) return;
    server.sendHeader("Cache-Control", "no-store, no-cache");
    server.sendHeader("Pragma", "no-cache");
    server.send(200, "text/html", settingsPage());
}

static void handleSaveSettings() {
    if (!requireAuth()) return;
    if (!secureStrEqual(server.arg("csrf"), String(csrfToken))) {
        server.send(403, "text/plain", "Forbidden: invalid CSRF token.");
        return;
    }

    // ── Admin password (validate before mutating anything) ────────────────
    // admPwClear takes precedence; otherwise a non-empty admPw sets a new one.
    bool admPwClear = server.hasArg("admPwClear") && server.arg("admPwClear") == "1";
    String admPw  = server.arg("admPw");
    String admPw2 = server.arg("admPw2");
    bool   admPwApply = false;   // true => write admPw into Settings below
    if (!admPwClear && admPw.length() > 0) {
        if (admPw.length() < 8) {
            server.send(400, "text/html",
                settingsErrorPage("Admin password must be at least 8 characters."));
            return;
        }
        if (admPw != admPw2) {
            server.send(400, "text/html",
                settingsErrorPage("Admin passwords do not match."));
            return;
        }
        if (admPw.length() >= sizeof(Settings::adminPassword)) {
            server.send(400, "text/html",
                settingsErrorPage("Admin password is too long."));
            return;
        }
        admPwApply = true;
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

    // Stocks — refresh interval
    int stkRef = server.arg("stkRef").toInt();
    if (stkRef >= 1 && stkRef <= 60) Settings::stockRefreshMin = stkRef;

    // Stocks — currency display
    Settings::stockEuro = server.hasArg("stkEur") && server.arg("stkEur") == "1";

    // Buy/Sell signal thresholds — independent per stock; clamp each to the same
    // range the form enforces. Fields are suffixed with the stock slot index.
    for (int i = 0; i < MAX_STOCKS; i++) {
        String si = String(i);
        if (server.hasArg("sigSma_" + si)) {
            int x = server.arg("sigSma_" + si).toInt();
            if (x >= 2 && x <= 52) Settings::signalSmaWeeks[i] = x;
        }
        if (server.hasArg("sigTrn_" + si)) {
            float x = server.arg("sigTrn_" + si).toFloat();
            if (x >= 0.5f && x <= 50.0f) Settings::signalTrendPct[i] = x;
        }
        if (server.hasArg("sigMmW_" + si)) {
            int x = server.arg("sigMmW_" + si).toInt();
            if (x >= 1 && x <= 26) Settings::signalMomWeeks[i] = x;
        }
        if (server.hasArg("sigMmP_" + si)) {
            float x = server.arg("sigMmP_" + si).toFloat();
            if (x >= 0.5f && x <= 50.0f) Settings::signalMomPct[i] = x;
        }
        if (server.hasArg("sigRng_" + si)) {
            float x = server.arg("sigRng_" + si).toFloat();
            if (x >= 1.0f && x <= 49.0f) Settings::signalRangeEdge[i] = x;
        }
        if (server.hasArg("sigMnW_" + si)) {
            int x = server.arg("sigMnW_" + si).toInt();
            if (x >= 2 && x <= 52) Settings::signalMinWeeks[i] = x;
        }
    }

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

    // Apply admin-password change (already validated above)
    if (admPwClear) {
        Settings::adminPassword[0] = '\0';
    } else if (admPwApply) {
        strncpy(Settings::adminPassword, admPw.c_str(), sizeof(Settings::adminPassword) - 1);
        Settings::adminPassword[sizeof(Settings::adminPassword) - 1] = '\0';
    }

    Settings::save();

    // Invalidate caches so data re-fetches on next loop
    Weather::current.valid = false;
    Prayer::current.valid  = false;
    Stocks::begin();  // re-init symbol list

    // Re-apply timezone
    TimeSync::apply(Settings::utcOffset, true);

    // Redirect back to the settings page; it shows an inline "saved" toast.
    server.sendHeader("Location", "/settings?saved=1", true);
    server.send(302, "text/plain", "");
}

static void handleResetSettings() {
    if (!requireAuth()) return;
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
    if (!requireAuth()) return;
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
            if (!requireAuth()) return;
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
static unsigned long lastAPScanMs  = 0;    // 0 = never scanned → fire immediately
static bool          apScanPending = false; // async scan in progress

/*
 * checkKnownNetworkInAP() — While in AP mode, periodically scan for visible
 * Wi-Fi networks and compare them against the saved-network list.
 * The scan runs asynchronously to avoid blocking the main loop.
 * If a known SSID is visible and the STA connection succeeds the device
 * restarts so it boots cleanly in STA mode with full initialisation.
 * If the connection attempt fails the AP is restarted so the portal remains
 * accessible.  Call from loop() whenever apActive is true.
 */
static void checkKnownNetworkInAP() {
    if (!apActive) return;
    if (savedNetworkCount <= 0) return;

    unsigned long now = millis();

    // Start an async scan when the interval has elapsed (or on first call).
    if (!apScanPending &&
        (lastAPScanMs == 0 || (now - lastAPScanMs) >= AP_SCAN_INTERVAL_MS)) {
        lastAPScanMs  = now;
        apScanPending = true;
        WiFi.scanNetworks(true);   // async — returns immediately
        Serial.println("[Network] AP mode: background WiFi scan started.");
        return;
    }

    // Check if the async scan has completed.
    if (!apScanPending) return;
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;   // still in progress — check next loop
    apScanPending = false;

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
