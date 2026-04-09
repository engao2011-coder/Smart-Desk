/*
 * ota.h — OTA (Over-The-Air) Firmware Update Support
 *
 * Two OTA mechanisms are provided:
 *
 *  1. ArduinoOTA (push via PlatformIO or Arduino IDE, UDP port 3232)
 *       pio run -e cyd-ota -t upload --upload-port desknexus.local
 *
 *  2. HTTP OTA (browser upload at http://desknexus.local/update)
 *       Handled by the web server routes in network.h.
 *
 * Usage in main.cpp:
 *   setup(): after WiFi connects → OTA::begin()
 *   loop():  every iteration     → OTA::handle()
 *
 * An optional password can be set in config.h:
 *   #define OTA_PASSWORD "your_secure_password"
 * Leave it undefined (or empty) for password-free OTA.
 */

#pragma once

#include <ArduinoOTA.h>
#include "config.h"

namespace OTA {

static bool _started = false;

/*
 * isStarted() — True once begin() has been called successfully.
 */
static bool isStarted() { return _started; }

/*
 * begin() — Initialise ArduinoOTA. Safe to call multiple times;
 * re-initialisation is skipped if already started.
 * Must be called after a successful STA WiFi connection.
 */
static void begin() {
    if (_started) return;

    ArduinoOTA.setHostname("desknexus");

#ifdef OTA_PASSWORD
    if (strlen(OTA_PASSWORD) > 0) {
        ArduinoOTA.setPassword(OTA_PASSWORD);
    }
#endif

    ArduinoOTA.onStart([]() {
        const char* type = (ArduinoOTA.getCommand() == U_FLASH)
                           ? "firmware" : "filesystem";
        Serial.printf("[OTA] Start: %s\n", type);
    });

    ArduinoOTA.onEnd([]() {
        Serial.println("\n[OTA] Complete — restarting.");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("[OTA] Progress: %u%%\r", (progress * 100) / total);
    });

    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("[OTA] Error[%u]: ", error);
        switch (error) {
            case OTA_AUTH_ERROR:    Serial.println("Auth failed");    break;
            case OTA_BEGIN_ERROR:   Serial.println("Begin failed");   break;
            case OTA_CONNECT_ERROR: Serial.println("Connect failed"); break;
            case OTA_RECEIVE_ERROR: Serial.println("Receive failed"); break;
            case OTA_END_ERROR:     Serial.println("End failed");     break;
            default:                Serial.println("Unknown error");  break;
        }
    });

    ArduinoOTA.begin();
    _started = true;
    Serial.println("[OTA] ArduinoOTA ready — hostname: desknexus (port 3232)");
}

/*
 * handle() — Service ArduinoOTA events. Call every loop() iteration.
 */
static void handle() {
    if (_started) {
        ArduinoOTA.handle();
    }
}

/*
 * stop() — Stop ArduinoOTA (e.g. when WiFi is lost).
 */
static void stop() {
    if (_started) {
        ArduinoOTA.end();
        _started = false;
    }
}

} // namespace OTA
