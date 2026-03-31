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
├── DeskNexus.ino   ← Main Arduino sketch (setup / loop / state machine)
├── config.h        ← User configuration (API keys, city, stocks, timezone)
├── network.h       ← Hybrid WiFi manager (STA + AP captive portal)
├── weather.h       ← OpenWeatherMap integration
├── prayer.h        ← Aladhan prayer-times integration
├── stocks.h        ← Alpha Vantage stock-quote integration
└── ui.h            ← TFT display layout & drawing helpers
```

---

## Getting Started

### 1  Install Arduino IDE & Board Support

1. Add the ESP32 board package URL in **Preferences → Additional Board Manager URLs**:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
2. Install **esp32** via *Tools → Board → Boards Manager*.
3. Select **ESP32 Dev Module** (or the CYD-specific board if available).

### 2  Install Libraries

Install the following via *Sketch → Include Library → Manage Libraries*:

| Library | Author | Min Version |
|---------|--------|-------------|
| TFT_eSPI | Bodmer | 2.5 |
| XPT2046_Touchscreen | Paul Stoffregen | 1.4 |
| ArduinoJson | Benoit Blanchon | 6.21 |

### 3  Configure TFT_eSPI

In your Arduino libraries folder open `TFT_eSPI/User_Setup.h` and add/change:

```cpp
#define ILI9341_DRIVER
#define TFT_MOSI  13
#define TFT_SCLK  14
#define TFT_CS    15
#define TFT_DC     2
#define TFT_RST   -1
#define TOUCH_CS  33
#define SPI_FREQUENCY        40000000
#define SPI_TOUCH_FREQUENCY   2500000
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_GFXFF
#define SMOOTH_FONT
```

### 4  Edit `config.h`

Open `DeskNexus/config.h` and fill in:

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

### 5  Flash

1. Connect the CYD via USB.
2. Select the correct port.
3. Upload.

### 6  First-Time WiFi Setup

1. On first boot (or if saved credentials fail) the device broadcasts
   **DeskNexus-Setup** (open Wi-Fi AP).
2. Connect to it from your phone or laptop.
3. Open **http://192.168.4.1** in a browser.
4. Select your home network from the dropdown, enter the password, tap **Connect**.
5. The device restarts and connects automatically; credentials are saved to flash.

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

The default calibration values in `config.h` suit most CYD units. If touches
are inaccurate, run the **Touch_calibrate** example from TFT_eSPI, note the
five printed values, and paste them into:

```cpp
#define TOUCH_CAL_DATA { 365, 3570, 340, 3520, 1 }
```

---

## Customisation

| Goal | Where to change |
|------|----------------|
| Different city / country | `config.h` → `OWM_CITY_NAME`, `PRAYER_CITY` |
| Different timezone | `config.h` → `NTP_UTC_OFFSET_SEC` |
| Different prayer method | `config.h` → `PRAYER_METHOD` |
| Stock symbols | `config.h` → `STOCK_SYMBOLS[]` |
| Stock alert threshold | `config.h` → `STOCK_ALERT_PCT` |
| Colour theme | `ui.h` → colour palette (`C_*` defines) |
| Backlight timeout | `config.h` → `SCREEN_TIMEOUT_MS` |
| Temperature units | `config.h` → `OWM_UNITS` (`"metric"` or `"imperial"`) |

---

## License

MIT License — see [LICENSE](LICENSE) for details.
