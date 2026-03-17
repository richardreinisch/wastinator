# 🗑 Wastinator – ESP32 WROOM-32

A self-contained waste collection reminder for the **ESP32 WROOM-32**.
No internet required. All data stored persistently in NVS flash (survives firmware uploads).
LEDs breathe softly via LEDC PWM. Bin names and colors are fully configurable via the web UI.

---

## Inspiration

Wastinator is based on the general idea of several DIY home automation and reminder projects
shared by the maker and Arduino community — from simple LED notification circuits to
ESP8266/ESP32-based web-server projects. The specific combination of features (breathing LEDs,
self-hosted web UI, NVS storage, button-hold WiFi toggle, blink-code date display, and
boot melody) was designed and implemented from scratch for this project.
Thanks to everyone in the open-source and maker community who shares their work and ideas freely.

---

## Hardware

| Part | Qty |
|---|---|
| ESP32 WROOM-32 Dev Board | 1× |
| LED × 4 (one per bin, any color) | 4× |
| LED Power/Status (green recommended) | 1× |
| 220Ω resistors | 5× |
| Buzzer — passive preferred, active works too | 1× |
| Tactile push button | 1× |

> **Passive vs. active buzzer:** A passive buzzer allows true tone generation (boot melody
> plays correctly). An active buzzer will produce approximate pitches via fast switching —
> the melody is still audible. Alarm beeps work identically with both types.

---

## Wiring

```
ESP32 GPIO   →  Component
──────────────────────────────────────────────────────
GPIO 18      →  220Ω → Bin 1 LED  (cathode → GND)
GPIO 19      →  220Ω → Bin 2 LED  (cathode → GND)
GPIO 21      →  220Ω → Bin 3 LED  (cathode → GND)
GPIO 22      →  220Ω → Bin 4 LED  (cathode → GND)
GPIO 25      →  220Ω → Power LED  (cathode → GND)
GPIO 26      →  Push button       (other side → GND)
GPIO 27      →  Buzzer (+)        (– → GND)
3V3 / 5V     →  VCC
GND          →  GND (common)
```

> Button uses internal pull-up — no external resistor needed. LOW = pressed.

---

## Installation

1. Install **PlatformIO** extension in VS Code
2. Open folder: `File → Open Folder → wastinator`
3. Connect ESP32 via USB
4. Click **→ Upload** in the status bar

No additional libraries needed — `Preferences.h` and `WebServer.h` are part of the
ESP32 Arduino core.

---

## Boot Sequence

On every power-on the device runs a short startup routine:

1. **LED test** — each bin LED fades in and out individually (confirms wiring), then all
   four together as a group sweep, then the power LED fades in and holds
2. **Boot melody** — cheerful ascending fanfare: C5 → E5 → G5 → C6
3. **Auto WiFi** — if the clock has never been set (e.g. after first flash or power outage),
   the WiFi AP starts automatically so you can set the time immediately

---

## Button

One button handles everything:

| Action | Result |
|---|---|
| Short press — alarm active | Acknowledge alarm, LEDs off |
| Short press — no alarm | Show next upcoming pickup via blink-code |
| Hold 10 seconds | Toggle WiFi AP on / off |

### Next Pickup Blink-Code (short press, no alarm)

The LED of the bin with the soonest upcoming pickup blinks out the date:

1. Brief intro flash (identifies the bin)
2. **Day** blinks — e.g. 12× for the 12th
3. Long pause (separator)
4. **Month** blinks — e.g. 3× for March

If no pickups are scheduled, all four bin LEDs flash once briefly.

### WiFi Toggle (10s hold)

- Power LED ramps up from 0 → 255 as a progress bar
- At 5s: brief flash as half-way confirmation
- At 10s WiFi **ON**: 3× quick flash
- At 10s WiFi **OFF**: 2× slow fade

---

## Web Interface

1. Hold button 10s → WiFi hotspot starts  
   *(or boots automatically if clock is not set)*
   - **SSID:** `Wastinator` · **Password:** `wastinator`
2. Connect your phone or laptop to that network
3. Open **http://192.168.4.1** in your browser

### What you can configure

- **Bin names** — click the name field and type directly (up to 23 characters)
- **Bin colors** — click any color swatch below the bin name (8 colors)
- **Pickup dates** — select a date and click "+ Add"; dates auto-save to NVS
- **Buzzer** — enable/disable buzzer alerts
- **Repeat notifications** — repeats alert every 30 min until acknowledged
- **Controller clock** — set current date & time (required after every power outage)

### Upcoming Pickups panel

The web UI shows a summary panel above the bin cards with the next scheduled pickup
per bin, sorted by date. Entries are color-coded by urgency:

| Urgency | Display |
|---|---|
| Today / Tomorrow | 🔴 Red |
| Within 7 days | 🟡 Yellow |
| Further away | Normal — full date |

The soonest upcoming pickup across all bins is marked with a **NEXT** badge.

---

## Power LED

| State | Power LED |
|---|---|
| Clock not set (after power outage) | Double-blink warning pattern |
| WiFi active, clock set | Slow sine fade-pulse (~1.5s cycle) |
| Normal — clock set, WiFi off | Steady on |
| Button being held (WiFi toggle) | Ramps up 0 → 255 (progress bar) |
| WiFi just turned ON | 3× quick flash confirmation |
| WiFi just turned OFF | 2× slow fade confirmation |

---

## Bin LEDs

| State | Bin LEDs |
|---|---|
| No pickup tomorrow | Off |
| Pickup tomorrow (alert starts at 15:00 the day before) | Soft sine breathing |
| Alarm acknowledged | Off until next alarm |

The four bin LEDs breathe with a ¼-cycle offset from each other — Bin 1 first,
then Bin 2 (¼ cycle later), Bin 3 (½), Bin 4 (¾) — creating a gentle wave effect.

---

## Available Colors

Select per bin in the web UI. Color index is stored in NVS.

| # | Color | | # | Color |
|---|---|---|---|---|
| 0 | Black | | 4 | Green |
| 1 | White | | 5 | Red |
| 2 | Brown | | 6 | Blue |
| 3 | Yellow | | 7 | Orange |

---

## Storage (NVS)

Data is stored via the ESP32 **Preferences / NVS** (Non-Volatile Storage) API.
NVS lives in its own dedicated flash partition and **survives firmware uploads** —
unlike the EEPROM emulation library which could be wiped during upload.

To force a full reset of all saved data, bump `EEPROM_MAGIC` in `config.h`
(e.g. `0xB2` → `0xB3`). On the next boot, defaults are loaded and written fresh.

---

## Customization

| Setting | File | Constant |
|---|---|---|
| WiFi SSID / password | `config.h` | `WIFI_SSID`, `WIFI_PASSWORD` |
| Alert start hour | `config.h` | `NOTIFY_HOUR` |
| Breathe cycle speed | `config.h` | `BREATHE_PERIOD_MS` |
| Repeat alert interval | `config.h` | `BUZZ_REPEAT_INTERVAL_MIN` |
| WiFi hold duration | `config.h` | `WIFI_HOLD_MS` |
| Max pickups per bin | `config.h` | `MAX_PICKUPS_PER_BIN` |
| Storage reset trigger | `config.h` | `EEPROM_MAGIC` |

---

## Default Bin Setup (first boot)

| Bin | Default Name | Default Color |
|---|---|---|
| Bin 1 | General Waste | Black |
| Bin 2 | Paper | Red |
| Bin 3 | Lightweight Packaging | Yellow |
| Bin 4 | Organic Waste | Brown |

All names and colors can be changed freely in the web UI.

---

## Project Structure

```
wastinator/
├── platformio.ini          ← ESP32 build config (no extra libs needed)
├── include/
│   ├── config.h            ← Pins, PWM channels, color palette, structs, defaults
│   ├── storage.h           ← NVS storage interface
│   └── webserver.h         ← Web server interface
├── src/
│   ├── main.cpp            ← Boot sequence, melody, LED test, alarm logic,
│   │                          breathing LEDs, button handler, blink-code preview
│   ├── storage.cpp         ← NVS read/write via Preferences
│   └── webserver.cpp       ← HTTP server + embedded HTML/CSS/JS UI
└── README.md
```
