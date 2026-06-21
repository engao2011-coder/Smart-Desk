# DeskNexus — Smart Desk Clock

A full-featured desk clock firmware for the **ESP32-2432S028R** ("Cheap Yellow Display" / CYD).
Displays real-time clock, weather, prayer times, and live stock prices on a 2.8-inch ILI9341 TFT — configured entirely through a browser.

---

## Features

- **Real-time NTP clock** — auto-synced; shows Gregorian and Hijri date
- **Current weather** — temperature, feels-like, humidity, wind speed via OpenWeatherMap
- **5-day forecast** — daily high/low with dominant weather icon
- **Prayer times** — Fajr through Isha via Aladhan API; pre-alert banner + fullscreen Azan screen
- **Stock / ETF monitor** — up to 10 symbols from Yahoo Finance (no API key required); daily change, 52-week high, optional EUR conversion, plus an on-device DCA-oriented signal (STRONG BUY / BUY / HOLD / TRIM — a weekly-horizon technical indicator, not financial advice)
- **Auto-detect city & timezone** — ipwho.is detects your location on every boot, no manual timezone entry needed
- **Break reminder** — configurable interval (10–240 min); fullscreen prompt with auto-dismiss
- **Setup wizard & QR code** — TFT shows AP SSID, password, and a scannable WiFi QR on first boot
- **Captive portal** — browser-based WiFi setup at `http://192.168.4.1` when in AP mode
- **Web settings page** — change location, API key, prayer method, stocks, theme from any browser on the LAN
- **OTA firmware updates** — push over-the-air via PlatformIO (`espota`) or browser upload at `/update`
- **Dual theme** — dark (slate & amber) / light; auto-switches at sunrise / Maghrib
- **Backlight management** — dims after 90 s of inactivity, wakes instantly on touch

---

## Hardware

| Component | Details |
|-----------|---------|
| Board | ESP32-2432S028R ("Cheap Yellow Display" / CYD) |
| MCU | ESP32-WROOM-32 |
| Display | 2.8" ILI9341 TFT, 240×320 portrait |
| Touch | XPT2046 resistive (separate VSPI bus from display) |
| Flash | 4 MB; firmware sits at ~90 % (no SPIFFS partition needed) |

> No external hardware is required — everything runs on the stock CYD board.

---

## Quick Start

### 1 — Prerequisites

- [PlatformIO Core](https://platformio.org/install/cli) **or** the [VS Code PlatformIO extension](https://platformio.org/install/ide?install=vscode)
- A free API key from [OpenWeatherMap](https://openweathermap.org/api) (used for current weather and the 5-day forecast)

### 2 — Configure

Edit **`src/config.h`** before building:

```c
// ── Required ─────────────────────────────────────────────────────────────
#define OWM_API_KEY    "YOUR_OPENWEATHERMAP_API_KEY"   // ← get a free key at openweathermap.org
#define OWM_CITY_NAME  "London"                         // ← default city (overridden by auto-detect)
#define OWM_COUNTRY    "GB"                              // ISO 3166-1 alpha-2 country code
#define OWM_UNITS      "metric"                          // "metric" (°C) or "imperial" (°F)

// ── Timezone ─────────────────────────────────────────────────────────────
// If AUTO_DETECT_LOCATION_TIME is true (default) this is overridden on boot.
#define NTP_UTC_OFFSET_SEC   0    // UTC offset in seconds (e.g. UTC+3 = 10800)

// ── Prayer method (Aladhan method number) ────────────────────────────────
//  1=Muslim World League  2=ISNA  3=Egypt  4=Makkah  5=Karachi  8=Gulf  16=Turkey
#define PRAYER_METHOD   4

// ── Stocks (up to MAX_STOCKS=10 Yahoo Finance symbols) ───────────────────
static const char* STOCK_SYMBOLS[MAX_STOCKS] = {
    "AAPL", "MSFT", "",   // TICKER or TICKER.EXCHANGE (e.g. "IUSE.L", "IUSD.DE")
};
```

> **Auto-detect (default `true`):** `AUTO_DETECT_LOCATION_TIME` queries ipwho.is on every boot and automatically fills in the city, country, and UTC offset. You can lock the city manually via the web settings page.

### 3 — Build & Flash

```bash
# Initial USB flash
pio run --target upload

# Open serial monitor (115200 baud)
pio device monitor --baud 115200
```

All library dependencies declared in `platformio.ini` are installed automatically by PlatformIO on first build.

---

## First-Time WiFi Setup

1. Flash the firmware (step 3 above).
2. On first boot the device starts as Wi-Fi AP **`DeskNexus-Setup`**.
3. The TFT displays the SSID, password (e.g. `deskA1B2`), IP address, and a scannable QR code.
4. Connect to **`DeskNexus-Setup`** from your phone or laptop (use the QR or enter credentials manually).
5. Browse to **`http://192.168.4.1`** and enter your home Wi-Fi credentials.
6. The device restarts, connects to your network, and saves the credentials to non-volatile storage (NVS).
7. On all subsequent boots the device connects automatically — no portal is shown.

> **Multiple networks:** up to 5 SSIDs are remembered. The device tries each in signal-strength order. Add or remove saved networks from the web settings page.

---

## OTA Firmware Updates

### Option A — PlatformIO push (ArduinoOTA)

```bash
pio run -e cyd-ota -t upload --upload-port desknexus.local
```

The device must already be running firmware with OTA enabled (the default) and be reachable at `desknexus.local` on your network.
If you set `OTA_PASSWORD` in `config.h`, append `--upload-flags="--auth=your_password"`.

### Option B — Browser upload

1. Navigate to **`http://desknexus.local/update`** (Basic Auth required — see below).
2. Click **Choose file** and select `.pio/build/cyd/firmware.bin`.
3. Click **Update**. The device flashes the new firmware and reboots automatically.

---

## Web Interface

Once connected to your network, open **`http://desknexus.local`** in any browser:

| Path | Description | Auth required |
|------|-------------|---------------|
| `/` | Status — firmware version, WiFi signal, last fetch times, auto-detect result | Open |
| `/settings` | Configure location, OWM key, prayer method, stocks, buy/sell signal thresholds, theme | Basic Auth |
| `/update` | Browser-based firmware upload (HTTP OTA) | Basic Auth |

**Credentials:** username `admin`, password shown on the AP setup TFT screen (`deskXXXX`).
You can set a custom admin password under **Settings → Admin Password** — this overrides the MAC-derived default for web/OTA access only (the AP join password is unaffected).

---

## Touch Navigation

All pages fill the entire 240×320 screen. The **Home** page is a tile dashboard:

```
┌──────────────────────────────┐  y = 0
│  Hero            (120 px)    │  Large clock + date  │  Weather chip
│                              │                        (tap → Forecast)
├──────────────────────────────┤  y = 120
│  Prayer band     (108 px)    │  Next prayer, time, countdown
│                              │                        (tap → Prayer)
├──────────────────────────────┤  y = 228
│  Markets band     (92 px)    │  Stock ticker, price, daily change
│                              │                        (tap → Stocks)
└──────────────────────────────┘  y = 320
```

| Action | Result |
|--------|--------|
| Tap Weather chip (right of Hero) | Opens Forecast page |
| Tap Prayer band | Opens Prayer page |
| Tap Markets band | Opens Stocks page |
| Tap anywhere on a detail page | Returns to Home |
| 60 s without touch | Returns to Home automatically |
| Tap "Prayed" or "Snooze" on Azan screen | Dismisses fullscreen Azan alert |

---

## Touch Calibration

Default calibration in `config.h` is for the CYD board in portrait mode:

```c
#define TOUCH_CAL_DATA { 365, 3570, 340, 3520, 1 }
```

If touches feel misaligned, run the **Touch_calibrate** example that ships with the TFT_eSPI library, note the five output values, and paste them into `TOUCH_CAL_DATA` in `config.h`.

---

## Module Architecture

All modules use a **header-only, single-translation-unit** pattern: state and functions are `static` at namespace scope and included only by `main.cpp`. Do not include any of these headers from a second `.cpp` file.

| File | Namespace | Purpose |
|------|-----------|---------|
| `config.h` | — | Compile-time user configuration (API keys, pins, thresholds) |
| `settings.h` | `Settings` | Runtime NVS-backed settings; `load()` / `save()` on boot and web form submit |
| `network.h` | `Network` | Hybrid WiFi manager (STA ↔ AP), captive portal, all web server routes |
| `location_time.h` | `LocationTime` | Auto-detect city and UTC offset via ipwho.is on every STA boot |
| `time_sync.h` | `TimeSync` | NTP `configTime()` wrapper with a reapply guard and stable-read helper |
| `weather.h` | `Weather` | OpenWeatherMap current weather fetch + 5-day forecast |
| `prayer.h` | `Prayer` | Aladhan prayer times fetch, next-prayer tracking, Azan reminder engine |
| `stocks.h` | `Stocks` | Yahoo Finance quote fetcher, EUR conversion, 52-week high metrics, DCA-oriented buy/hold/trim signal |
| `ota.h` | `OTA` | ArduinoOTA (UDP push) initialisation and loop handler |
| `ui.h` | `UI` | TFT themes, layout constants, all drawing functions, touch dispatch |
| `main.cpp` | — | `setup()` / `loop()` orchestration — the only file that includes all headers |

---

## Security Notes

- **HTTPS with `setInsecure()`** — all outbound API calls (weather, prayer, stocks, geo-IP) use TLS encryption but **do not verify the server certificate**. A man-in-the-middle on your LAN could observe API key traffic. This is a deliberate trade-off given the ESP32's limited RAM.
- **HTTP Basic Auth** protects `/settings` and `/update`. Set a strong admin password.
- **CSRF tokens** are generated per-session and validated on every POST endpoint.
- **HTML escaping** is applied to all user-supplied and network-supplied values embedded in served pages.
- **Constant-time string comparison** is used for CSRF token validation to prevent timing side-channels.
- The read-only status page (`/`) requires no authentication.

---

## Libraries

| Library | Author | Used for |
|---------|--------|---------|
| [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) ≥ 2.5.43 | Bodmer | ILI9341 SPI display driver |
| [XPT2046_Touchscreen](https://github.com/PaulStoffregen/XPT2046_Touchscreen) | Paul Stoffregen | Resistive touch controller |
| [ArduinoJson](https://arduinojson.org/) v6 | Benoit Blanchon | JSON parsing (weather, prayer, stocks, geo) |
| [QRCode](https://github.com/ricmoo/QRCode) v0.0.1 | ricmoo | WiFi setup QR code on TFT |

All dependencies are declared in `platformio.ini` and installed automatically on first build.

---

## License

*License not yet specified. All rights reserved by the author.*
