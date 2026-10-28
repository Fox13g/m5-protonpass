#include <Arduino.h>
#include <M5Cardputer.h>
#include "config.h"
#include "vault_storage.h"
#include "hid_injector.h"
#include "sync_client.h"
#include "ui_manager.h"

namespace {
sentinel::VaultStorage g_vaultStorage;
sentinel::HidInjector  g_hidInjector;
sentinel::SyncClient   g_syncClient(g_vaultStorage);
sentinel::UiManager    g_uiManager(g_vaultStorage, g_hidInjector, g_syncClient);
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);

    g_vaultStorage.begin();
    g_hidInjector.begin();
    g_uiManager.begin();
}

void loop() {
    M5Cardputer.update();
    g_uiManager.update();
    delay(sentinel::KEYBOARD_SCAN_INTERVAL_MS);
}

