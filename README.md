# DeskNexus — ESP32 Desk Clock

An ESP32-based desk clock with a 2.8" colour touchscreen displaying
real-time clock, weather, daily prayer times, and live stock quotes.

---

## Features

| Feature | Detail |
|---------|--------|
| **Clock** | Large HH:MM display with seconds; NTP-synced |
| **Weather** | Current temperature, condition, humidity & wind via OpenWeatherMap |
| **Prayer Times** | All five prayers + Sunrise via Aladhan API; highlights next prayer with countdown |
| **Stocks** | Live quotes (price + % change) for up to 5 symbols via Alpha Vantage |
| **Notifications** | On-screen banner when a stock moves ≥ 2 % or a prayer is ≤ 5 min away |
| **WiFi** | Hybrid: connects to saved network; falls back to AP captive-portal if unavailable |
| **Auto-dim** | Backlight dims after configurable idle period |
| **Touch nav** | Tap tabs at the bottom to switch between Prayer, Stocks, and Settings panels |

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
├── platformio.ini  ← PlatformIO build configuration (board, libs, TFT pins)
└── src/
    ├── main.cpp    ← Main sketch (setup / loop / state machine)
    ├── config.h    ← User configuration (API keys, city, stocks, timezone)
    ├── network.h   ← Hybrid WiFi manager (STA + AP captive portal)
    ├── weather.h   ← OpenWeatherMap integration
    ├── prayer.h    ← Aladhan prayer-times integration
    ├── stocks.h    ← Alpha Vantage stock-quote integration
    └── ui.h        ← TFT display layout & drawing helpers
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
| XPT2046_Touchscreen | Paul Stoffregen | ^1.4 |
| ArduinoJson | Benoit Blanchon | ^6.21 |

### 3  TFT_eSPI is pre-configured

The CYD pin assignments and driver settings for TFT_eSPI are declared directly in
`platformio.ini` via `build_flags`. No manual editing of `TFT_eSPI/User_Setup.h` is required.

### 4  Edit `src/config.h`

Open `DeskNexus/src/config.h` and fill in:

```cpp
// Timezone
#define NTP_UTC_OFFSET_SEC   10800   // e.g. 10800 = UTC+3

// OpenWeatherMap
#define OWM_API_KEY    "your_key_here"
#define OWM_CITY_NAME  "Riyadh"
#define OWM_COUNTRY    "SA"

// Alpha Vantage (stocks)
#define AV_API_KEY     "your_key_here"

// Stocks to monitor
static const char* STOCK_SYMBOLS[MAX_STOCKS] = {
    "AAPL", "GOOGL", "MSFT", "", "",
};

// Prayer times
#define PRAYER_CITY    "Riyadh"
#define PRAYER_COUNTRY "SA"
#define PRAYER_METHOD  4          // 4 = Umm Al-Qura / Makkah
```

**Free API keys:**
- OpenWeatherMap: <https://openweathermap.org/api>
- Alpha Vantage: <https://www.alphavantage.co/support/#api-key>
- Aladhan: no key required

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
2. Click **Firmware Update**.
3. Choose the compiled `.bin` file
   (`DeskNexus/.pio/build/cyd/firmware.bin` after `pio run`).
4. Click **Upload & Flash** — a progress bar shows transfer progress.
5. The device restarts automatically when the flash completes.

The HTTP upload page is protected with a per-session CSRF token so it
cannot be triggered by a cross-site request.

---

## Screen Layout (Portrait 240 × 320)

```
┌──────────────────────────┐  ← Status bar: WiFi indicator + date
│  ● Mon 10 Mar 2025        │
├──────────────────────────┤
│                          │
│       14:35              │  ← Clock (large) + seconds
│                       47 │
│                          │
├──────────────────────────┤
│ Riyadh        Hum: 28%  │  ← Weather
│ 31°C          Wind: 4.2  │
│ Sunny         Feels: 33° │
├──────────────────────────┤
│  Prayer  │ Stocks │ Sett │  ← Tab bar
├──────────────────────────┤
│ Fajr   05:10  Asr  15:42 │  ← Panel (Prayer / Stocks / Settings)
│ Sunrise 06:30  Maghrib 18:15│
│ Dhuhr  12:10  Isha  19:45│
│         Next in 0h 22m   │
└──────────────────────────┘
```

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
| Different city / country | `src/config.h` → `OWM_CITY_NAME`, `PRAYER_CITY` |
| Different timezone | `src/config.h` → `NTP_UTC_OFFSET_SEC` |
| Different prayer method | `src/config.h` → `PRAYER_METHOD` |
| Stock symbols | `src/config.h` → `STOCK_SYMBOLS[]` |
| Stock alert threshold | `src/config.h` → `STOCK_ALERT_PCT` |
| Colour theme | `src/ui.h` → colour palette (`C_*` defines) |
| Backlight timeout | `src/config.h` → `SCREEN_TIMEOUT_MS` |
| Temperature units | `src/config.h` → `OWM_UNITS` (`"metric"` or `"imperial"`) |

---

## License

MIT License — see [LICENSE](LICENSE) for details.
