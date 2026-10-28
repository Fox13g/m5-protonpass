#pragma once

#include <stdint.h>
#include <stddef.h>

namespace sentinel {

// ============================================================================
// Limites Estritos de Memória (ESP32-S3FN8 — 512 KB SRAM, Sem PSRAM)
// ============================================================================
constexpr size_t MAX_INDEXED_ITEMS_IN_RAM = 512;   // ~32 KB em SRAM
constexpr size_t MAX_SEARCH_QUERY_LEN     = 32;
constexpr size_t PBKDF2_DEFAULT_ITERATIONS= 100000;
constexpr uint8_t DEFAULT_MAX_UNLOCK_TRIES= 10;

// ============================================================================
// Dimensões do Ecrã TFT ST7789V2 (240x135) — Dois Sprites Estáticos (Sem PSRAM)
// ============================================================================
constexpr int16_t DISPLAY_WIDTH           = 240;
constexpr int16_t DISPLAY_HEIGHT          = 135;
constexpr int16_t TOP_BAR_HEIGHT          = 20;    // 240x20x2B = 9.600 bytes
constexpr int16_t MAIN_VIEW_HEIGHT        = 115;   // 240x115x2B = 55.200 bytes
constexpr uint8_t VISIBLE_LIST_ROWS       = 4;

// ============================================================================
// Pinos Físicos do M5Stack Cardputer (Stamp-S3)
// ============================================================================
constexpr int PIN_BATTERY_ADC             = 10;    // Divisor resistivo da bateria
constexpr int PIN_SD_CS                   = 12;    // Chip Select microSD
constexpr int PIN_SD_MOSI                 = 14;
constexpr int PIN_SD_MISO                 = 39;
constexpr int PIN_SD_SCK                  = 40;

// ============================================================================
// Caminhos de Armazenamento Local (LittleFS & microSD)
// ============================================================================
constexpr const char* VAULT_FILE_PATH     = "/vault.bin";
constexpr const char* VAULT_TMP_PATH      = "/vault.tmp";
constexpr const char* OUTBOX_FILE_PATH    = "/outbox.bin";
constexpr const char* SD_PROVISION_PATH   = "/cardputer_provision.json";
constexpr const char* SD_BACKUP_PATH      = "/vault_backup.bin";
constexpr const char* NVS_NAMESPACE       = "sentinel_sec";

// ============================================================================
// Temporizações de Interface e HID
// ============================================================================
constexpr uint32_t KEYBOARD_SCAN_INTERVAL_MS = 15;
constexpr uint32_t HID_INTER_KEY_DELAY_MS    = 12;
constexpr uint32_t HID_TAB_STABILIZE_MS      = 100;
constexpr uint32_t TOTP_TIME_STEP_SEC        = 30;
constexpr uint32_t AUTO_LOCK_TIMEOUT_MS      = 180000; // 3 minutos de inatividade

} // namespace sentinel

