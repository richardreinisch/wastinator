#pragma once

#include <stdint.h>   // uint8_t, uint16_t, etc.
#include <stddef.h>   // size_t
#include <string.h>   // strncpy (used in storage_reset)

// ─── PIN DEFINITIONS (ESP32 WROOM-32) ────────────────────────────────────────
#define PIN_LED_BIN1        18   // Bin 1 LED
#define PIN_LED_BIN2        19   // Bin 2 LED
#define PIN_LED_BIN3        21   // Bin 3 LED
#define PIN_LED_BIN4        22   // Bin 4 LED
#define PIN_LED_POWER       25   // Power / Status LED
#define PIN_BUTTON          26   // Button: short=acknowledge, hold 10s=WiFi toggle
#define PIN_BUZZER          27   // Active buzzer (HIGH = on)

// ─── LEDC PWM CHANNELS (ESP32) ───────────────────────────────────────────────
#define PWM_CH_BIN1         0
#define PWM_CH_BIN2         1
#define PWM_CH_BIN3         2
#define PWM_CH_BIN4         3
#define PWM_CH_POWER        4
#define PWM_FREQ            1000   // Hz
#define PWM_RESOLUTION      8      // bits -> 0-255

// ─── WIFI ACCESS POINT ────────────────────────────────────────────────────────
#define WIFI_SSID           "Wastinator"
#define WIFI_PASSWORD       "wastinator"
#define WIFI_CHANNEL        6

// ─── TIMING ───────────────────────────────────────────────────────────────────
#define BREATHE_PERIOD_MS        2400    // Full LED breathe cycle (ms)
#define NOTIFY_HOUR              15      // Evening alert start hour (15:00)
#define BUZZ_DURATION_MS         300     // Buzzer beep length (ms)
#define BUZZ_REPEAT_INTERVAL_MIN 30      // Repeat alert interval (minutes)
#define WIFI_HOLD_MS             10000UL // Button hold time for WiFi toggle (ms)

// ─── STORAGE (NVS via Preferences) ───────────────────────────────────────────
// NVS lives in its own flash partition — survives firmware uploads.
// Bump EEPROM_MAGIC whenever AppConfig struct layout changes to force a reset.
#define EEPROM_MAGIC        0xB2

// ─── BIN COUNT ────────────────────────────────────────────────────────────────
#define NUM_BINS            4
#define MAX_PICKUPS_PER_BIN 52

// ─── COLOR PALETTE ────────────────────────────────────────────────────────────
#define NUM_COLORS          8
static const char* COLOR_NAMES[NUM_COLORS] = {
    "Black","White","Brown","Yellow","Green","Red","Blue","Orange"
};
static const char* COLOR_HEX[NUM_COLORS] = {
    "#1f2937",  // black
    "#f1f5f9",  // white
    "#92400e",  // brown
    "#eab308",  // yellow
    "#16a34a",  // green
    "#ef4444",  // red
    "#3b82f6",  // blue
    "#f97316"   // orange
};

// ─── STRUCTS ──────────────────────────────────────────────────────────────────

struct Pickup {
    uint8_t  day;
    uint8_t  month;
    uint16_t year;
    bool     active;
};

struct BinConfig {
    char     name[24];
    uint8_t  colorIndex;
    Pickup   pickups[MAX_PICKUPS_PER_BIN];
};

struct AppConfig {
    bool      buzzerEnabled;
    bool      repeatNotify;
    BinConfig bins[NUM_BINS];
};

// ─── PIN / CHANNEL LOOKUP ─────────────────────────────────────────────────────
static const uint8_t BIN_PINS[NUM_BINS] = {
    PIN_LED_BIN1, PIN_LED_BIN2, PIN_LED_BIN3, PIN_LED_BIN4
};
static const uint8_t BIN_PWM_CH[NUM_BINS] = {
    PWM_CH_BIN1, PWM_CH_BIN2, PWM_CH_BIN3, PWM_CH_BIN4
};

// ─── DEFAULTS (used on first boot) ────────────────────────────────────────────
static const char* DEFAULT_BIN_NAMES[NUM_BINS] = {
    "General Waste", "Paper", "Lightweight Packaging", "Organic Waste"
};
static const uint8_t DEFAULT_BIN_COLORS[NUM_BINS] = { 0, 5, 3, 2 };
