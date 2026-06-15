# DeskNexus — ESP32 Desk Clock

An ESP32-based desk clock with a 2.8" colour touchscreen displaying
real-time clock, weather, daily prayer times, and live stock quotes.

---

## Features

| Feature | Detail |
|---------|--------|
| **Clock** | Large HH:MM display; NTP-synced |
| **Weather** | Current temperature, condition, humidity & wind via OpenWeatherMap; 5-day forecast with temperature-swing alerts |
| **Prayer Times** | All five prayers + Sunrise via Aladhan API, with Hijri date; full-screen page highlights the next prayer with a live countdown; an azan screen announces each prayer when due |
| **Stocks** | Live quotes for up to 5 symbols via Yahoo Finance (no API key required); shows a concise company/fund name (generic ETF/issuer boilerplate such as "iShares", "MSCI", "UCITS", "ETF", "USD" is stripped) with both the daily (1D) and 52-week (52W) change. The Home markets band shows the top mover; the Stocks page lists all symbols |
| **Notifications** | On-screen banner when a prayer is approaching/due or a break reminder fires. Stocks signal moves visually: a gold dot when a symbol moves ≥ 2 % vs the previous close, and the display auto-switches to the Stocks page when a symbol moves ≥ 1 % since the last fetch |
| **Break Reminder** | Configurable periodic reminder to take a break (default every 60 min) |
| **WiFi** | Hybrid: connects to saved network; falls back to AP captive-portal if unavailable |
| **Auto Location** | Automatically detects city and timezone from IP on boot |
| **Setup Wizard** | First-boot QR-code setup flow for easy Wi-Fi configuration |
| **OTA Updates** | Over-the-air firmware updates via ArduinoOTA push or browser HTTP upload |
| **Auto-return** | Any detail page returns to the Home dashboard after an idle period |
| **Auto-dim** | Backlight dims after configurable idle period |
| **Touch nav** | Location-based: on Home, tap the weather chip → Forecast, the prayer band → Prayer, the markets band → Stocks. Tap a detail page to return Home. Settings are configured from the web portal, not the display |
| **Theme** | "Slate & Amber" — near-black slate with a single warm amber accent; auto light/dark by time of day, or forced via settings. Shared by the display and web portal |
| **Persistent Settings** | User configuration saved to NVS (ESP32 flash) and survives reboots |

---

## Hardware

| Component | Part |
|-----------|------|
| Microcontroller | ESP32-WROOM-32 |
| Display | 2.8" ILI9341 TFT (240 × 320) |
| Touch | XPT2046 resistive touchscreen |
| Board | ESP32-2432S028R ("Cheap Yellow Display" / CYD) |

### CYD Pin Assignments

| Signal | GPIO |
|--------|------|
| TFT MOSI | 13 |
| TFT SCLK | 14 |
| TFT CS | 15 |
| TFT DC | 2 |
| TFT RST | — (tied to EN) |
| Backlight (PWM) | 21 |
| Touch CS | 33 |
| Touch IRQ | 36 |

---

## Project Structure

```
DeskNexus/
├── platformio.ini      ← PlatformIO build configuration (board, libs, TFT pins)
└── src/
    ├── main.cpp        ← Main sketch (setup / loop / state machine)
    ├── config.h        ← User configuration (API keys, city, stocks, timezone)
    ├── settings.h      ← Runtime configuration persisted to NVS
    ├── network.h       ← Hybrid WiFi manager (STA + AP captive portal)
    ├── location_time.h ← Automatic city/timezone detection from IP
    ├── time_sync.h     ← NTP time synchronisation helpers
    ├── weather.h       ← OpenWeatherMap integration (current + forecast)
    ├── prayer.h        ← Aladhan prayer-times integration
    ├── stocks.h        ← Yahoo Finance stock-quote integration
    ├── ota.h           ← Over-the-air firmware update (ArduinoOTA + HTTP)
    └── ui.h            ← TFT display layout & drawing helpers
```

---

## Getting Started

### 1  Install PlatformIO

Install the [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode) for VS Code,
or use the PlatformIO Core CLI:

```bash
pip install platformio
```

### 2  Install dependencies

All libraries are declared in `platformio.ini` and downloaded automatically on first build.
No manual library installation is needed.

| Library | Author | Version |
|---------|--------|---------|
| TFT_eSPI | Bodmer | ^2.5 |
| XPT2046_Touchscreen | Paul Stoffregen | git (latest) |
| ArduinoJson | Benoit Blanchon | ^6.21 |
| QRCode | Richard Moore | ^0.0.1 |

### 3  TFT_eSPI is pre-configured

The CYD pin assignments and driver settings for TFT_eSPI are declared directly in
`platformio.ini` via `build_flags`. No manual editing of `TFT_eSPI/User_Setup.h` is required.

### 4  Edit `src/config.h`

Open `DeskNexus/src/config.h` and fill in:

```cpp
// Timezone (auto-detected on boot if AUTO_DETECT_LOCATION_TIME is true)
#define NTP_UTC_OFFSET_SEC   10800   // e.g. 10800 = UTC+3

// OpenWeatherMap. City/country here are shared by both weather AND prayer times.
#define OWM_API_KEY    "your_key_here"
#define OWM_CITY_NAME  "Riyadh"
#define OWM_COUNTRY    "SA"

// Stocks to monitor (Yahoo Finance — no API key required)
// Symbol format: TICKER.EXCHANGE  e.g. "IUSE.L", "IUSD.DE"
static const char* STOCK_SYMBOLS[MAX_STOCKS] = {
    "IUSE.L", "IUSD.DE", "PPFB.DE", "", "",
};

// Prayer times (uses OWM_CITY_NAME / OWM_COUNTRY above for the location)
#define PRAYER_METHOD  4          // 4 = Umm Al-Qura / Makkah
```

> All of these compiled values are just *initial* defaults — once the device is
> running you can change city, stocks, prayer method and more from the web portal
> (`http://desknexus.local/settings`), and the changes persist to flash.

**Free API keys:**
- OpenWeatherMap: <https://openweathermap.org/api>
- Aladhan: no key required
- Yahoo Finance (stocks): no key required

### 5  Build & Flash

```bash
# Build
pio run

# Flash (auto-detects the connected ESP32)
pio run --target upload

# Open serial monitor
pio device monitor
```

Or use the PlatformIO IDE toolbar buttons (✓ Build / → Upload / 🔌 Monitor).

### 6  First-Time WiFi Setup

1. On first boot (or if saved credentials fail) the device broadcasts
   **DeskNexus-Setup** (open Wi-Fi AP).
2. Connect to it from your phone or laptop.
3. Open **http://192.168.4.1** in a browser.
4. Select your home network from the dropdown, enter the password, tap **Connect**.
5. The device restarts and connects automatically; credentials are saved to flash.

---

## OTA (Over-The-Air) Updates

Once the device is running on your local network you can update the firmware
without a USB cable using either method below.

### Method 1 — ArduinoOTA (PlatformIO / Arduino IDE push)

```bash
# Push new firmware wirelessly — device must be on the same network
pio run -e cyd-ota -t upload --upload-port desknexus.local
```

The `cyd-ota` environment is pre-configured in `platformio.ini` with
`upload_protocol = espota` and `upload_port = desknexus.local`.

> **Optional password protection:** Uncomment `#define OTA_PASSWORD` in
> `src/config.h` and set your password. Then add `--upload-flags --auth=<pass>`
> to the PlatformIO upload command.

### Method 2 — HTTP browser upload

1. Navigate to **http://desknexus.local** (or the device IP) on your
   browser.
2. Click **Firmware Update** and sign in (see *Web admin login* below).
3. Choose the compiled `.bin` file
   (`DeskNexus/.pio/build/cyd/firmware.bin` after `pio run`).
4. Click **Upload & Flash** — a progress bar shows transfer progress.
5. The device restarts automatically when the flash completes.

The HTTP upload page is protected with a per-session CSRF token so it
cannot be triggered by a cross-site request.

### Web admin login

The **Settings** and **Firmware Update** pages require HTTP Basic auth so
that no one else on your network can read your API key or flash arbitrary
firmware:

- **Username:** `admin`
- **Password:** the device password shown on the AP setup screen — your
  `AP_PASSWORD` from `src/config.h` if set, otherwise the auto-derived
  `deskXXXX` (last 4 hex digits of the device MAC).

The read-only status page (`http://desknexus.local`) stays open and needs
no login.

---

## Screen Layout (Portrait 240 × 320)

Every page fills the whole screen. The **Home** dashboard is the hub — you reach
a detail page by tapping its region on Home, and a tap (or an idle timeout)
returns you there. The look is the "Slate & Amber" theme: a near-black slate
canvas with a single warm amber accent, mint for positive/up, red for
negative/down.

### Home dashboard

```
┌────────────────────────────┐ y=0
│ Mon 14 Jun           W24 ●  │ ← date · week number · WiFi dot
│                            │
│  14:32           ☀ 24°      │ ← dominant clock (left) · weather chip (right)
│                    Clear    │
├────────────────────────────┤ ~y=120
│ NEXT PRAYER                │
│ Asr                  15:48  │ ← next prayer name · time
│ in 1h 16m                  │
│ ▰▰▰▰▰▰▱▱▱▱                  │ ← elapsed-progress bar to next prayer
├────────────────────────────┤ ~y=228
│ MARKETS                    │
│ ▏ S&P 500          +1.24%   │ ← top-moving stock · daily change (1D)
│ ▏ $5,431.60      52W +18.0% │   price · 52-week change (52W)
└────────────────────────────┘ y=320
```

Tap the **weather chip** → Forecast · the **prayer band** → Prayer · the
**markets band** → Stocks. The clock itself is not a link.

### Detail pages

- **Prayer** — a "next prayer" hero (name, time, countdown, progress bar, Hijri
  date) over a read-only timetable of all six entries, with the next prayer
  highlighted and past prayers dimmed. An azan screen appears when a prayer is
  due; tap **Dismiss** to clear it.

  ```
  ┌────────────────────────────┐ y=0
  │ TUE 15 JUN          ● Cairo │ ← Gregorian date · WiFi dot · city
  │ NEXT PRAYER                │
  │ Asr                  17:02  │ ← next prayer name (left) · time (right)
  │ in 1h 20m                  │ ← live countdown
  │ ▰▰▰▰▰▰▱▱▱▱                  │ ← elapsed-progress bar to next prayer
  │      20 Dhul-Hijjah 1447    │ ← Hijri date
  ├────────────────────────────┤
  │ Fajr                 03:58  │ ← past prayers dimmed
  │ · Sunrise            05:41  │ ← Sunrise marked, never highlighted
  │ Dhuhr                13:08  │
  │ ▎Asr                 17:02  │ ← next prayer highlighted (amber)
  │ Maghrib              20:35  │ ← upcoming prayers bright
  │ Isha                 22:18  │
  └────────────────────────────┘ y=320
  ```
- **Forecast** — 5-day weather outlook.
- **Stocks** — per-symbol detail with name, daily (1D) and 52-week (52W) change.

Tap anywhere on a detail page to return to **Home**; it also returns
automatically after an idle period. **Settings** are not shown on the display —
they are configured from the web portal (see *Web admin login* above).

---

## Touch Calibration

The default calibration values in `src/config.h` suit most CYD units. If touches
are inaccurate, run the **Touch_calibrate** example from TFT_eSPI, note the
five printed values, and paste them into:

```cpp
#define TOUCH_CAL_DATA { 365, 3570, 340, 3520, 1 }
```

---

## Customisation

| Goal | Where to change |
|------|----------------|
| Different city / country | `src/config.h` → `OWM_CITY_NAME`, `OWM_COUNTRY` (shared by weather + prayer; or enable auto-detection / set from the web portal) |
| Different timezone | `src/config.h` → `NTP_UTC_OFFSET_SEC` (or enable auto-detection) |
| Auto-detect location & timezone | `src/config.h` → `AUTO_DETECT_LOCATION_TIME` |
| Different prayer method | `src/config.h` → `PRAYER_METHOD` |
| Prayer alert timing | `src/config.h` → `PRAYER_PRE_ALERT_MINUTES` |
| Stock symbols | `src/config.h` → `STOCK_SYMBOLS[]` (Yahoo Finance format: TICKER.EXCHANGE) |
| Stock alert threshold | `src/config.h` → `STOCK_ALERT_PCT` |
| Stock refresh interval | `src/config.h` → `STOCK_REFRESH_MS` |
| Break reminder interval | `src/config.h` → `BREAK_REMINDER_INTERVAL_M` |
| Colour theme | `src/ui.h` → `THEME_DARK` / `THEME_LIGHT` structs |
| Backlight timeout | `src/config.h` → `SCREEN_TIMEOUT_MS` |
| Backlight brightness | `src/config.h` → `BACKLIGHT_FULL_DUTY` / `BACKLIGHT_DIM_DUTY` |
| Auto-carousel timing | `src/config.h` → `CAROUSEL_INTERVAL_MS` |
| Temperature units | `src/config.h` → `OWM_UNITS` (`"metric"` or `"imperial"`) |
| OTA password | `src/config.h` → uncomment & set `OTA_PASSWORD` |

---

## License

MIT License — see [LICENSE](LICENSE) for details.
