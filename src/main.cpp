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
// WiFi ON:  3x quick flash
static void powerLedConfirmOn() {
    for (int rep = 0; rep < 3; rep++) {
        for (int v = 0; v <= 255; v += 15) { ledcWrite(PWM_CH_POWER, v); delay(3); }
        for (int v = 255; v >= 0; v -= 15) { ledcWrite(PWM_CH_POWER, v); delay(3); }
        delay(40);
    }
}
// WiFi OFF: 2x slow fade out
static void powerLedConfirmOff() {
    for (int rep = 0; rep < 2; rep++) {
        for (int v = 0; v <= 180; v += 10) { ledcWrite(PWM_CH_POWER, v); delay(5); }
        for (int v = 180; v >= 0; v -= 5)  { ledcWrite(PWM_CH_POWER, v); delay(6); }
        delay(80);
    }
}

// ─── POWER LED STATE MACHINE ──────────────────────────────────────────────────
// States (priority order):
//   1. External override (confirmation animation running)
//   2. Clock not set → double-blink warning pattern
//   3. WiFi active   → slow sine fade-pulse (gentle, ~1s cycle)
//   4. Normal        → steady on
static void handlePowerLed() {
    if (powerLedOverride) {
        if (millis() >= powerLedOverrideUntil) powerLedOverride = false;
        return;
    }
    if (!timeIsSet()) {
        // Double-blink: ON-off-ON-pause
        static uint8_t step = 0; static unsigned long stepT = 0;
        const unsigned long dur[] = {400, 200, 400, 1000};
        const bool          on[]  = {true, false, true, false};
        if (millis() - stepT >= dur[step]) { stepT = millis(); step = (step+1)%4; }
        ledcWrite(PWM_CH_POWER, on[step] ? 220 : 0);
        return;
    }
    if (wifiActive) {
        // Smooth sine fade-pulse: 0 → 220 → 0 over ~1200ms, then 300ms off
        // Total cycle ~1500ms — calm and clearly different from alarm breathing
        const unsigned long CYCLE = 1500UL;
        const unsigned long ON_PHASE = 1200UL; // sine ramp portion
        unsigned long pos = millis() % CYCLE;
        uint8_t val = 0;
        if (pos < ON_PHASE) {
            float phase = (float)pos / (float)ON_PHASE; // 0.0 → 1.0
            float s = sinf(phase * M_PI);               // 0 → 1 → 0
            val = (uint8_t)(s * s * 220.0f);            // gamma-corrected
        }
        ledcWrite(PWM_CH_POWER, val);
        return;
    }
    // Time set, WiFi off → steady on
    ledcWrite(PWM_CH_POWER, 200);
}

// ─── ALARM: CHECK TOMORROW'S PICKUPS ─────────────────────────────────────────
static uint8_t checkTomorrow() {
    unsigned long ts = getSystemTime();
    if (!ts) return 0;
    DateTime dt = tsToDateTime(ts);
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

// ─── BREATHING LED UPDATE ─────────────────────────────────────────────────────
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
    DateTime dt = tsToDateTime(getSystemTime());
    uint8_t mask = (dt.hour >= NOTIFY_HOUR) ? checkTomorrow() : 0;

    if (mask && (!alarmActive || mask != alarmMask)) {
        alarmActive = true; alarmMask = mask;
        acknowledged = false; lastRepeat = millis();
        Serial.printf("[Alarm] Tomorrow: bins 0x%02X\n", mask);
        if (cfg.buzzerEnabled) { buzz(); delay(120); buzz(); }
    } else if (!mask) {
        alarmActive = false; alarmMask = 0; acknowledged = false;
    }
    if (alarmActive && !acknowledged && cfg.repeatNotify) {
        if (millis() - lastRepeat >= (unsigned long)BUZZ_REPEAT_INTERVAL_MIN * 60000UL) {
            lastRepeat = millis();
            if (cfg.buzzerEnabled) buzz();
        }
    }
    handleBreathingLeds();
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

// ─── BLINK-COUNT PREVIEW ──────────────────────────────────────────────────────
// Blink the bin LED N times with a short on/off pulse, then pause.
// e.g. day=12, month=3 → 12 blinks, long pause, 3 blinks
static void blinkCount(uint8_t binCh, uint8_t count, int onMs = 120, int offMs = 120) {
    if (count == 0) return;
    for (uint8_t i = 0; i < count; i++) {
        ledcWrite(binCh, 220);
        delay(onMs);
        ledcWrite(binCh, 0);
        if (i < count - 1) delay(offMs);
    }
}

static void showNextPickupPreview() {
    NextPickup np = findNextPickup();
    if (np.binIndex < 0) {
        // No upcoming pickups — briefly flash all bins once to signal "nothing found"
        for (int i = 0; i < NUM_BINS; i++) ledcWrite(BIN_PWM_CH[i], 180);
        delay(300);
        for (int i = 0; i < NUM_BINS; i++) ledcWrite(BIN_PWM_CH[i], 0);
        Serial.println("[Preview] No upcoming pickups found.");
        return;
    }

    uint8_t ch = BIN_PWM_CH[np.binIndex];
    Serial.printf("[Preview] Next pickup: bin %d  %02d/%02d\n",
        np.binIndex, np.day, np.month);

    // Brief intro flash so user knows which bin
    ledcWrite(ch, 220); delay(400); ledcWrite(ch, 0); delay(500);

    // Day blinks
    blinkCount(ch, np.day,  130, 130);
    delay(700);  // long pause between day and month

    // Month blinks
    blinkCount(ch, np.month, 130, 130);

    delay(200);
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
                ledcWrite(PWM_CH_POWER, 255); delay(40);
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
    handlePowerLed();
    if (wifiActive) webserver_handle();
    delay(8);
}
