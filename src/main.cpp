#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "storage.h"
#include "webserver.h"

// ─── SYSTEM TIME ──────────────────────────────────────────────────────────────
static unsigned long timeBase  = 0;  // Unix timestamp when last set (0 = not set)
static unsigned long milliBase = 0;

void setSystemTime(unsigned long ts) {
    timeBase  = ts;
    milliBase = millis();
    Serial.printf("[Time] Clock set to: %lu\n", ts);
}

unsigned long getSystemTime() {
    if (timeBase == 0) return 0;
    return timeBase + (millis() - milliBase) / 1000UL;
}

bool timeIsSet() { return timeBase != 0; }

// Timezone offset in seconds east of UTC (auto-set from browser)
// Default 3600 = UTC+1 (Vienna winter). Browser sends actual offset on time-set.
static int tzOffsetSec = 3600;

void setTimezoneOffset(int offsetSec) {
    tzOffsetSec = offsetSec;
    Serial.printf("[Time] Timezone offset: %+d s (%+.1f h)\n",
        offsetSec, offsetSec / 3600.0f);
}

struct DateTime { uint16_t year; uint8_t month, day, hour, minute; };

static DateTime tsToDateTime(unsigned long ts) {
    uint32_t days = ts / 86400UL;
    uint32_t rem  = ts % 86400UL;
    DateTime dt;
    dt.hour   = rem / 3600;
    dt.minute = (rem % 3600) / 60;
    uint32_t z   = days + 719468;
    uint32_t era = z / 146097;
    uint32_t doe = z - era * 146097;
    uint32_t yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
    uint32_t y   = yoe + era * 400;
    uint32_t doy = doe - (365*yoe + yoe/4 - yoe/100);
    uint32_t mp  = (5*doy + 2) / 153;
    dt.day   = doy - (153*mp + 2)/5 + 1;
    dt.month = mp + (mp < 10 ? 3 : -9);
    dt.year  = y + (dt.month <= 2 ? 1 : 0);
    return dt;
}

// ─── PWM SETUP ────────────────────────────────────────────────────────────────
static void pwm_setup() {
    for (int i = 0; i < NUM_BINS; i++) {
        ledcSetup(BIN_PWM_CH[i], PWM_FREQ, PWM_RESOLUTION);
        ledcAttachPin(BIN_PINS[i], BIN_PWM_CH[i]);
        ledcWrite(BIN_PWM_CH[i], 0);
    }
    ledcSetup(PWM_CH_POWER, PWM_FREQ, PWM_RESOLUTION);
    ledcAttachPin(PIN_LED_POWER, PWM_CH_POWER);
    ledcWrite(PWM_CH_POWER, 0);
}

// Smooth breathe: sinusoidal, gamma-corrected
static uint8_t breatheValue(float phase) {
    float s = sinf(phase * M_PI);
    return (uint8_t)(s * s * 230.0f + 2.0f);
}

// ─── BUZZER / MELODY ──────────────────────────────────────────────────────────
// Uses LEDC channel 5 for tone generation (passive buzzer gives best results;
// active buzzers will approximate pitch via fast switching).
#define PWM_CH_BUZZER  5
#define PWM_BUZZ_RES   8   // 8-bit, 50% duty = 128

// Play a single tone: freq in Hz, duration in ms. freq=0 = rest.
static void tone_play(uint32_t freq, int ms) {
    if (freq == 0) {
        ledcDetachPin(PIN_BUZZER);
        digitalWrite(PIN_BUZZER, LOW);
        delay(ms);
        return;
    }
    ledcSetup(PWM_CH_BUZZER, freq, PWM_BUZZ_RES);
    ledcAttachPin(PIN_BUZZER, PWM_CH_BUZZER);
    ledcWrite(PWM_CH_BUZZER, 128);
    delay(ms);
    ledcWrite(PWM_CH_BUZZER, 0);
    ledcDetachPin(PIN_BUZZER);
    digitalWrite(PIN_BUZZER, LOW);
}

static void note_gap(int ms = 40) { delay(ms); }

// Simple blocking buzz for alarms (active buzzer compatible)
static void buzz(int ms = BUZZ_DURATION_MS) {
    digitalWrite(PIN_BUZZER, HIGH); delay(ms); digitalWrite(PIN_BUZZER, LOW);
}

// ── Boot Melody ───────────────────────────────────────────────────────────────
// Cheerful ascending fanfare: C5 E5 G5 C6 — short and punchy
// Frequency reference: C5=523 E5=659 G5=784 C6=1047
static void playBootMelody() {
    tone_play(523,  110); note_gap(30);  // C5
    tone_play(659,  110); note_gap(30);  // E5
    tone_play(784,  110); note_gap(30);  // G5
    tone_play(1047, 260); note_gap(20);  // C6 (held)
    tone_play(784,   70); note_gap(20);  // G5 quick
    tone_play(1047, 170);                // C6 finish
}

// ── LED Boot Test ─────────────────────────────────────────────────────────────
// Phase 1: each bin LED fades in/out individually (confirms each LED works)
// Phase 2: all bin LEDs together — grand sweep
// Phase 3: power LED fades in and stays on
static void runLedBootTest() {
    // Phase 1: sequential
    for (int i = 0; i < NUM_BINS; i++) {
        for (int v = 0; v <= 220; v += 5) { ledcWrite(BIN_PWM_CH[i], v); delay(5); }
        delay(60);
        for (int v = 220; v >= 0; v -= 5) { ledcWrite(BIN_PWM_CH[i], v); delay(4); }
        delay(30);
    }

    delay(80);

    // Phase 2: all together
    for (int v = 0; v <= 220; v += 4) {
        for (int i = 0; i < NUM_BINS; i++) ledcWrite(BIN_PWM_CH[i], v);
        delay(4);
    }
    delay(120);
    for (int v = 220; v >= 0; v -= 4) {
        for (int i = 0; i < NUM_BINS; i++) ledcWrite(BIN_PWM_CH[i], v);
        delay(3);
    }

    delay(60);

    // Phase 3: power LED fade in
    for (int v = 0; v <= 200; v += 4) { ledcWrite(PWM_CH_POWER, v); delay(5); }
}

// ─── STATE ────────────────────────────────────────────────────────────────────
static AppConfig cfg;
static bool     wifiActive    = false;
static bool     alarmActive   = false;
static uint8_t  alarmMask     = 0;
static bool     acknowledged  = false;
static unsigned long lastRepeat = 0;

static float breathePhase[NUM_BINS] = {0, 0, 0, 0};
static const float BREATHE_OFFSETS[NUM_BINS] = {0.0f, 0.25f, 0.5f, 0.75f};
static unsigned long lastBreathUpdate = 0;

// Power LED override (active during confirmation animations)
static bool          powerLedOverride = false;
static unsigned long powerLedOverrideUntil = 0;

// ─── SETUP PINS ───────────────────────────────────────────────────────────────
static void setupPins() {
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
    pwm_setup();
}

static bool buttonDown() { return digitalRead(PIN_BUTTON) == LOW; }

// ─── WIFI AP ──────────────────────────────────────────────────────────────────
static void startAP() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);
    Serial.printf("[WiFi] AP started: SSID=%s  IP=%s\n",
        WIFI_SSID, WiFi.softAPIP().toString().c_str());
    webserver_init(cfg);
    wifiActive = true;
}

static void stopAP() {
    webserver_stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    wifiActive = false;
    Serial.println("[WiFi] AP stopped.");
}

// ─── POWER LED CONFIRMATION ANIMATIONS ───────────────────────────────────────
// Non-blocking: set override flag + duration, handlePowerLed does the animation.
// Pattern is encoded in handlePowerLed via powerLedConfirmMode.
enum ConfirmMode { CONFIRM_NONE=0, CONFIRM_ON, CONFIRM_OFF };
static ConfirmMode   powerLedConfirmMode  = CONFIRM_NONE;
static unsigned long powerLedConfirmStart = 0;

static void powerLedConfirmOn() {
    powerLedConfirmMode  = CONFIRM_ON;
    powerLedConfirmStart = millis();
    powerLedOverride      = true;
    powerLedOverrideUntil = millis() + 900; // handled inside handlePowerLed
}
static void powerLedConfirmOff() {
    powerLedConfirmMode  = CONFIRM_OFF;
    powerLedConfirmStart = millis();
    powerLedOverride      = true;
    powerLedOverrideUntil = millis() + 900;
}

// ─── POWER LED STATE MACHINE ──────────────────────────────────────────────────
// Priority order:
//   1. Confirmation animation (WiFi toggle feedback) — fully non-blocking
//   2. Button hold progress  — brightness written by handleButton
//   3. Clock not set         → double-blink warning
//   4. WiFi active           → slow sine fade-pulse
//   5. Normal                → steady on
static void handlePowerLed() {
    unsigned long now = millis();

    // 1. Confirmation animation
    if (powerLedConfirmMode != CONFIRM_NONE) {
        unsigned long t = now - powerLedConfirmStart;
        uint8_t val = 0;
        if (powerLedConfirmMode == CONFIRM_ON) {
            // 3x quick flash: 160ms rise + 140ms fall + 60ms gap = 360ms/rep
            unsigned long cycle = t % 360UL;
            if ((int)(t / 360) < 3) {
                val = (cycle < 160) ? (uint8_t)(cycle * 255 / 160)
                                    : (uint8_t)((360 - cycle) * 255 / 200);
            } else {
                powerLedConfirmMode = CONFIRM_NONE;
                powerLedOverride    = false;
            }
        } else {
            // 2x slow fade: 280ms rise + 320ms fall + 80ms gap = 680ms/rep
            unsigned long cycle = t % 680UL;
            if ((int)(t / 680) < 2) {
                val = (cycle < 280) ? (uint8_t)(cycle * 180 / 280)
                                    : (uint8_t)((680 - cycle) * 180 / 400);
            } else {
                powerLedConfirmMode = CONFIRM_NONE;
                powerLedOverride    = false;
            }
        }
        ledcWrite(PWM_CH_POWER, val);
        return;
    }

    // 2. Button hold progress (brightness already written by handleButton)
    if (powerLedOverride) {
        if (now >= powerLedOverrideUntil) powerLedOverride = false;
        return;
    }

    // 3. Clock not set: double-blink warning
    if (!timeIsSet()) {
        static uint8_t step = 0; static unsigned long stepT = 0;
        const unsigned long dur[] = {400, 200, 400, 1000};
        const bool          on[]  = {true, false, true, false};
        if (now - stepT >= dur[step]) { stepT = now; step = (step+1)%4; }
        ledcWrite(PWM_CH_POWER, on[step] ? 220 : 0);
        return;
    }

    // 4. WiFi active: smooth sine fade-pulse (~1.5s cycle)
    if (wifiActive) {
        const unsigned long CYCLE    = 1500UL;
        const unsigned long ON_PHASE = 1200UL;
        unsigned long pos = now % CYCLE;
        uint8_t val = 0;
        if (pos < ON_PHASE) {
            float s = sinf((float)pos / (float)ON_PHASE * M_PI);
            val = (uint8_t)(s * s * 220.0f);
        }
        ledcWrite(PWM_CH_POWER, val);
        return;
    }

    // 5. Power-save: brief gentle pulse every 10 seconds, otherwise off
    if (cfg.powerSave) {
        // 10s cycle: LED off for 9700ms, then a soft 300ms sine blip
        const unsigned long PS_CYCLE  = 10000UL;
        const unsigned long PS_PULSE  =   300UL;
        unsigned long pos = now % PS_CYCLE;
        uint8_t val = 0;
        if (pos < PS_PULSE) {
            float phase = (float)pos / (float)PS_PULSE;
            float s = sinf(phase * M_PI);
            val = (uint8_t)(s * s * 80.0f);   // max brightness 80/255 — very subtle
        }
        ledcWrite(PWM_CH_POWER, val);
        return;
    }

    // 6. Normal: steady on
    ledcWrite(PWM_CH_POWER, 200);
}

// ─── ALARM: CHECK TOMORROW'S PICKUPS ─────────────────────────────────────────
// Timezone offset in seconds — adjust TZ_OFFSET_SEC to your local UTC offset.
// Vienna: UTC+1 in winter = 3600, UTC+2 in summer (CEST) = 7200
// The browser sends UTC timestamps; we add the offset so day/hour is local time.
static uint8_t checkTomorrow() {
    unsigned long ts = getSystemTime();
    if (!ts) return 0;

    unsigned long localTs = ts + (unsigned long)tzOffsetSec;
    DateTime dt = tsToDateTime(localTs);

    uint8_t nd = dt.day, nm = dt.month; uint16_t ny = dt.year;
    static const uint8_t dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    uint8_t maxd = dim[nm-1];
    if (nm==2 && ((ny%4==0 && ny%100!=0) || ny%400==0)) maxd=29;
    if (++nd > maxd) { nd=1; if (++nm>12) { nm=1; ny++; } }

    uint8_t mask = 0;
    for (int b = 0; b < NUM_BINS; b++)
        for (int i = 0; i < MAX_PICKUPS_PER_BIN; i++) {
            const Pickup& p = cfg.bins[b].pickups[i];
            if (p.active && p.day==nd && p.month==nm && p.year==ny)
                mask |= (1 << b);
        }
    return mask;
}

// ─── BREATHING LED UPDATE — always called from loop ──────────────────────────
// Separated from handleAlarm() so LEDs keep running regardless of WiFi state,
// button holds, or blocking preview sequences.
static void handleBreathingLeds() {
    unsigned long now = millis();
    unsigned long dt  = now - lastBreathUpdate;
    if (dt < 8) return;
    lastBreathUpdate = now;
    float step = (float)dt / (float)BREATHE_PERIOD_MS;
    for (int i = 0; i < NUM_BINS; i++) {
        bool active = alarmActive && !acknowledged && (alarmMask & (1<<i));
        if (active) {
            breathePhase[i] += step;
            if (breathePhase[i] >= 1.0f) breathePhase[i] -= 1.0f;
            ledcWrite(BIN_PWM_CH[i], breatheValue(breathePhase[i]));
        } else {
            breathePhase[i] = BREATHE_OFFSETS[i];
            ledcWrite(BIN_PWM_CH[i], 0);
        }
    }
}

// ─── ALARM HANDLER ────────────────────────────────────────────────────────────
static void handleAlarm() {
    if (!timeIsSet()) return;

    DateTime dt = tsToDateTime(getSystemTime() + (unsigned long)tzOffsetSec);
    uint8_t mask = (dt.hour >= NOTIFY_HOUR) ? checkTomorrow() : 0;

    if (mask && (!alarmActive || mask != alarmMask)) {
        alarmActive = true; alarmMask = mask;
        acknowledged = false; lastRepeat = millis();
        Serial.printf("[Alarm] Active — tomorrow bins 0x%02X  local: %02d:%02d\n",
            mask, dt.hour, dt.minute);
        if (cfg.buzzerEnabled) { buzz(); delay(120); buzz(); }
        else Serial.println("[Alarm] (buzzer disabled — enable in web UI)");
    } else if (!mask && alarmActive) {
        Serial.println("[Alarm] Cleared.");
        alarmActive = false; alarmMask = 0; acknowledged = false;
    } else if (!mask) {
        alarmActive = false; alarmMask = 0; acknowledged = false;
    }
    if (alarmActive && !acknowledged && cfg.repeatNotify) {
        if (millis() - lastRepeat >= (unsigned long)BUZZ_REPEAT_INTERVAL_MIN * 60000UL) {
            lastRepeat = millis();
            if (cfg.buzzerEnabled) buzz();
        }
    }
}

// ─── NEXT PICKUP FINDER ───────────────────────────────────────────────────────
struct NextPickup {
    int     binIndex;   // -1 = none found
    uint8_t day;
    uint8_t month;
};

// Find the soonest upcoming pickup across all bins (from tomorrow onwards)
static NextPickup findNextPickup() {
    NextPickup result = {-1, 0, 0};
    if (!timeIsSet()) return result;

    unsigned long today = getSystemTime();
    // Normalise to start of today (strip hours/minutes/seconds)
    today = (today / 86400UL) * 86400UL;

    unsigned long bestTs = 0xFFFFFFFFUL;

    for (int b = 0; b < NUM_BINS; b++) {
        for (int i = 0; i < MAX_PICKUPS_PER_BIN; i++) {
            const Pickup& p = cfg.bins[b].pickups[i];
            if (!p.active) continue;

            // Convert pickup date to approximate unix timestamp (good enough for comparison)
            // Using simple day-count: days since epoch for that date
            // Year offset from 1970, rough calculation sufficient for sorting
            int y = p.year, m = p.month, d = p.day;
            // Days from epoch to 1 Jan of year y
            long dy = (y - 1970) * 365L + (y - 1969) / 4 - (y - 1901) / 100 + (y - 1601) / 400;
            static const int mdays[] = {0,31,59,90,120,151,181,212,243,273,304,334};
            dy += mdays[m - 1];
            if (m > 2 && ((y%4==0 && y%100!=0) || y%400==0)) dy++;
            dy += d - 1;
            unsigned long pts = (unsigned long)dy * 86400UL;

            // Must be strictly in the future (from start of today onwards, skip today)
            if (pts > today && pts < bestTs) {
                bestTs = pts;
                result = {b, p.day, p.month};
            }
        }
    }
    return result;
}

// ─── BLINK-COUNT PREVIEW — non-blocking state machine ────────────────────────
// States: idle → intro → day blinks → pause → month blinks → done
// Driven by handlePreview() called every loop iteration.
enum PreviewState {
    PV_IDLE=0, PV_INTRO_ON, PV_INTRO_OFF,
    PV_DAY_ON, PV_DAY_OFF, PV_PAUSE,
    PV_MONTH_ON, PV_MONTH_OFF, PV_DONE
};

static PreviewState  pvState    = PV_IDLE;
static unsigned long pvStateAt  = 0;   // millis() when current state started
static uint8_t       pvBinCh    = 0;
static uint8_t       pvDay      = 0;
static uint8_t       pvMonth    = 0;
static uint8_t       pvCount    = 0;   // blinks remaining in current group
static bool          pvAllBins  = false; // true = flash all bins (no pickup found)

// Timing constants (ms)
#define PV_INTRO_ON_MS   400
#define PV_INTRO_OFF_MS  500
#define PV_BLINK_ON_MS   130
#define PV_BLINK_OFF_MS  130
#define PV_PAUSE_MS      700

static void startNextPickupPreview() {
    NextPickup np = findNextPickup();
    if (np.binIndex < 0) {
        Serial.println("[Preview] No upcoming pickups found.");
        // Flash all bins briefly
        for (int i = 0; i < NUM_BINS; i++) ledcWrite(BIN_PWM_CH[i], 180);
        pvAllBins  = true;
        pvState    = PV_DONE;
        pvStateAt  = millis();
        return;
    }
    Serial.printf("[Preview] Next pickup: bin %d  %02d/%02d\n",
        np.binIndex, np.day, np.month);
    pvBinCh   = BIN_PWM_CH[np.binIndex];
    pvDay     = np.day;
    pvMonth   = np.month;
    pvCount   = pvDay;
    pvAllBins = false;
    pvState   = PV_INTRO_ON;
    pvStateAt = millis();
    ledcWrite(pvBinCh, 220);
}

// Called every loop() — advances the state machine, never blocks
static void handlePreview() {
    if (pvState == PV_IDLE) return;
    unsigned long elapsed = millis() - pvStateAt;

    switch (pvState) {
        case PV_INTRO_ON:
            if (elapsed >= PV_INTRO_ON_MS) {
                ledcWrite(pvBinCh, 0);
                pvState = PV_INTRO_OFF; pvStateAt = millis();
            }
            break;
        case PV_INTRO_OFF:
            if (elapsed >= PV_INTRO_OFF_MS) {
                ledcWrite(pvBinCh, 220);
                pvState = PV_DAY_ON; pvStateAt = millis();
            }
            break;
        case PV_DAY_ON:
            if (elapsed >= PV_BLINK_ON_MS) {
                ledcWrite(pvBinCh, 0);
                pvCount--;
                pvState = PV_DAY_OFF; pvStateAt = millis();
            }
            break;
        case PV_DAY_OFF:
            if (elapsed >= PV_BLINK_OFF_MS) {
                if (pvCount > 0) {
                    ledcWrite(pvBinCh, 220);
                    pvState = PV_DAY_ON;
                } else {
                    pvState = PV_PAUSE;  // day done, wait before month
                    pvCount = pvMonth;
                }
                pvStateAt = millis();
            }
            break;
        case PV_PAUSE:
            if (elapsed >= PV_PAUSE_MS) {
                ledcWrite(pvBinCh, 220);
                pvState = PV_MONTH_ON; pvStateAt = millis();
            }
            break;
        case PV_MONTH_ON:
            if (elapsed >= PV_BLINK_ON_MS) {
                ledcWrite(pvBinCh, 0);
                pvCount--;
                pvState = PV_MONTH_OFF; pvStateAt = millis();
            }
            break;
        case PV_MONTH_OFF:
            if (elapsed >= PV_BLINK_OFF_MS) {
                if (pvCount > 0) {
                    ledcWrite(pvBinCh, 220);
                    pvState = PV_MONTH_ON;
                } else {
                    pvState = PV_DONE;
                }
                pvStateAt = millis();
            }
            break;
        case PV_DONE:
            if (elapsed >= 300) {
                if (pvAllBins)
                    for (int i = 0; i < NUM_BINS; i++) ledcWrite(BIN_PWM_CH[i], 0);
                pvState   = PV_IDLE;
                pvAllBins = false;
            }
            break;
        default: break;
    }
}

// Trigger the preview (called on short button press)
static void showNextPickupPreview() {
    if (pvState != PV_IDLE) return; // already running
    startNextPickupPreview();
}

// ─── UNIFIED BUTTON HANDLER ───────────────────────────────────────────────────
// Short press  → acknowledge alarm  OR  show next pickup preview
// Hold 10s     → toggle WiFi AP
// During hold  → power LED ramps up 0→255 as progress indicator
//   at 5s (half-way) → brief flash confirmation
//   at 10s           → confirmation animation + action
static bool     btnPrev    = false;
static bool     btnHolding = false;
static unsigned long btnDownAt = 0;

static void handleButton() {
    bool down = buttonDown();

    if (down && !btnPrev) {           // press down
        btnDownAt  = millis();
        btnHolding = true;
    }

    if (down && btnHolding) {         // being held
        unsigned long held = millis() - btnDownAt;

        if (held >= WIFI_HOLD_MS) {   // ── 10s reached: fire action ──────────
            btnHolding = false;
            if (!wifiActive) {
                startAP();
                Serial.println("[BTN] WiFi enabled (10s hold)");
                powerLedConfirmOn();
            } else {
                stopAP();
                Serial.println("[BTN] WiFi disabled (10s hold)");
                powerLedConfirmOff();
            }
            powerLedOverride      = true;
            powerLedOverrideUntil = millis() + 800;

        } else {                      // ── in progress: ramp LED ─────────────
            static bool midFlash = false;
            if (held >= WIFI_HOLD_MS/2 && !midFlash) {
                midFlash = true;
                ledcWrite(PWM_CH_POWER, 255); // brief peak — no blocking delay
            }
            if (held < WIFI_HOLD_MS/2) midFlash = false;
            uint8_t brightness = (uint8_t)((held * 240UL) / WIFI_HOLD_MS);
            ledcWrite(PWM_CH_POWER, brightness);
            powerLedOverride      = true;
            powerLedOverrideUntil = millis() + 50;
        }
    }

    if (!down && btnPrev) {           // released
        unsigned long held = millis() - btnDownAt;
        btnHolding = false;
        if (held < WIFI_HOLD_MS) {
            if (alarmActive && !acknowledged) {
                // Acknowledge active alarm
                acknowledged = true;
                for (int i = 0; i < NUM_BINS; i++) ledcWrite(BIN_PWM_CH[i], 0);
                Serial.println("[ACK] Alarm acknowledged.");
                if (cfg.buzzerEnabled) { buzz(80); delay(80); buzz(80); }
            } else {
                // No alarm → show next upcoming pickup as blink-count
                showNextPickupPreview();
            }
        }
    }

    btnPrev = down;
}

// ─── SETUP / LOOP ─────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("\n[Boot] Wastinator starting...");
    setupPins();
    storage_init();
    storage_load(cfg);

    // Boot sequence: LED test + melody play simultaneously
    // Melody runs while LEDs cycle — they are both blocking but interleaved
    // by running LED test first, then melody (total ~2.5s, feels snappy)
    runLedBootTest();
    playBootMelody();

    // Auto-start WiFi if clock has never been set (e.g. after power outage)
    // so the user can immediately open the web UI and set the time.
    if (!timeIsSet()) {
        Serial.println("[Boot] Clock not set - starting WiFi automatically.");
        startAP();
    }

    Serial.println("[Boot] Ready. Hold button 10s to toggle WiFi.");
}

void loop() {
    handleButton();
    handleAlarm();
    handlePreview();         // non-blocking blink-code state machine
    // Only run breathing LEDs when preview is idle (they share the bin LED pins)
    if (pvState == PV_IDLE) handleBreathingLeds();
    handlePowerLed();
    if (wifiActive) webserver_handle();
    delay(8);
}
