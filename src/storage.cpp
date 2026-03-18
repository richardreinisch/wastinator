#include "storage.h"
#include <Preferences.h>
#include <Arduino.h>
#include <string.h>

// NVS namespace — survives firmware uploads, only erased by full chip erase
// or explicit nvs_flash_erase() call.
static Preferences prefs;
static const char* NVS_NS    = "wastinator";  // namespace (max 15 chars)
static const char* KEY_MAGIC = "magic";
static const char* KEY_CFG   = "cfg";

void storage_init() {
    // Nothing to do — Preferences opens on demand
}

void storage_reset(AppConfig& cfg) {
    cfg.buzzerEnabled = false;
    cfg.repeatNotify  = false;
    cfg.powerSave     = false;
    for (int b = 0; b < NUM_BINS; b++) {
        strncpy(cfg.bins[b].name, DEFAULT_BIN_NAMES[b], sizeof(cfg.bins[b].name) - 1);
        cfg.bins[b].name[sizeof(cfg.bins[b].name) - 1] = '\0';
        cfg.bins[b].colorIndex = DEFAULT_BIN_COLORS[b];
        for (int i = 0; i < MAX_PICKUPS_PER_BIN; i++)
            cfg.bins[b].pickups[i] = {0, 0, 0, false};
    }
}

void storage_load(AppConfig& cfg) {
    prefs.begin(NVS_NS, /*readOnly=*/true);
    uint8_t magic = prefs.getUChar(KEY_MAGIC, 0x00);
    if (magic != EEPROM_MAGIC) {
        prefs.end();
        Serial.println("[NVS] No valid data — loading defaults.");
        storage_reset(cfg);
        return;
    }
    // Read raw bytes of AppConfig struct
    size_t len = prefs.getBytesLength(KEY_CFG);
    if (len != sizeof(AppConfig)) {
        prefs.end();
        Serial.printf("[NVS] Size mismatch (%u vs %u) — loading defaults.\n",
            len, sizeof(AppConfig));
        storage_reset(cfg);
        return;
    }
    prefs.getBytes(KEY_CFG, &cfg, sizeof(AppConfig));
    prefs.end();
    Serial.println("[NVS] Configuration loaded.");
}

void storage_save(const AppConfig& cfg) {
    prefs.begin(NVS_NS, /*readOnly=*/false);
    prefs.putUChar(KEY_MAGIC, EEPROM_MAGIC);
    prefs.putBytes(KEY_CFG, &cfg, sizeof(AppConfig));
    prefs.end();
    Serial.println("[NVS] Configuration saved.");
}
