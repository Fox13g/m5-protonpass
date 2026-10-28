#include "provisioning.h"
#include "crypto_engine.h"
#include "config.h"
#include <string.h>
#include <stdio.h>

#if !defined(UNIT_TEST_NATIVE)
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <ArduinoJson.h>
#endif

namespace sentinel {

bool ProvisioningManager::hasSdProvisionFile(VaultStorage& storage) {
#if !defined(UNIT_TEST_NATIVE)
    storage.lockSpiBus();
    SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    if (!SD.begin(PIN_SD_CS, SPI, 20000000)) {
        storage.unlockSpiBus();
        return false;
    }
    bool exists = SD.exists(SD_PROVISION_PATH);
    SD.end();
    storage.unlockSpiBus();
    return exists;
#else
    return false;
#endif
}

bool ProvisioningManager::importFromSdAndWipe(
    VaultStorage& storage,
    const char* newMasterPassphrase,
    char* outStatusMsg,
    size_t statusMsgLen
) {
#if !defined(UNIT_TEST_NATIVE)
    storage.lockSpiBus();
    SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    if (!SD.begin(PIN_SD_CS, SPI, 20000000)) {
        storage.unlockSpiBus();
        snprintf(outStatusMsg, statusMsgLen, "Falha ao montar microSD");
        return false;
    }

    if (!SD.exists(SD_PROVISION_PATH)) {
        SD.end();
        storage.unlockSpiBus();
        snprintf(outStatusMsg, statusMsgLen, "Arquivo SD nao encontrado");
        return false;
    }

    File f = SD.open(SD_PROVISION_PATH, FILE_READ);
    if (!f || f.size() == 0 || f.size() > 4096) {
        if (f) f.close();
        SD.end();
        storage.unlockSpiBus();
        snprintf(outStatusMsg, statusMsgLen, "Tamanho JSON invalido");
        return false;
    }

    size_t fileSize = f.size();
    char jsonBuf[4097];
    size_t readBytes = f.readBytes(jsonBuf, fileSize);
    jsonBuf[readBytes] = '\0';
    f.close();

    bool imported = importFromJsonString(storage, jsonBuf, newMasterPassphrase, outStatusMsg, statusMsgLen);
    CryptoEngine::secureZeroize(jsonBuf, sizeof(jsonBuf));

    if (imported) {
        // Sobrescreve todos os bytes do ficheiro no microSD com zeros antes de apagar (Secure Wipe)
        File wipeFile = SD.open(SD_PROVISION_PATH, FILE_WRITE);
        if (wipeFile) {
            uint8_t zeros[64];
            memset(zeros, 0, sizeof(zeros));
            size_t remaining = fileSize;
            while (remaining > 0) {
                size_t chunk = (remaining > sizeof(zeros)) ? sizeof(zeros) : remaining;
                wipeFile.write(zeros, chunk);
                remaining -= chunk;
            }
            wipeFile.flush();
            wipeFile.close();
        }
        SD.remove(SD_PROVISION_PATH);
    }

    SD.end();
    storage.unlockSpiBus();
    return imported;
#else
    return false;
#endif
}

bool ProvisioningManager::importFromJsonString(
    VaultStorage& storage,
    const char* jsonStr,
    const char* newMasterPassphrase,
    char* outStatusMsg,
    size_t statusMsgLen
) {
    if (!jsonStr || !newMasterPassphrase || strlen(newMasterPassphrase) < 4) {
        snprintf(outStatusMsg, statusMsgLen, "PIN/Senha min 4 digitos");
        return false;
    }

#if !defined(UNIT_TEST_NATIVE)
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, jsonStr);
    if (err) {
        snprintf(outStatusMsg, statusMsgLen, "JSON invalido");
        return false;
    }

    const char* dekHex   = doc["crypto"]["dekHex"] | "";
    const char* hmacHex  = doc["crypto"]["hmacSecretHex"] | "";
    const char* saltHex  = doc["crypto"]["pbkdf2SaltHex"] | "";
    uint32_t iterations  = doc["crypto"]["pbkdf2Iterations"] | PBKDF2_DEFAULT_ITERATIONS;

    uint8_t rawDek[32];
    DeviceSecurityConfig cfg;
    memset(&cfg, 0, sizeof(cfg));

    if (!CryptoEngine::hexToBytes(dekHex, rawDek, 32) ||
        !CryptoEngine::hexToBytes(hmacHex, cfg.hmac_secret, 32) ||
        !CryptoEngine::hexToBytes(saltHex, cfg.pbkdf2_salt, 16)) {
        CryptoEngine::secureZeroize(rawDek, sizeof(rawDek));
        snprintf(outStatusMsg, statusMsgLen, "Chaves hex invalidas");
        return false;
    }

    cfg.pbkdf2_iterations = iterations;
    CryptoEngine::fillRandomBytes(cfg.wrap_iv, sizeof(cfg.wrap_iv));

    uint8_t kek[32];
    if (!CryptoEngine::deriveKekFromPassphrase(
            newMasterPassphrase,
            cfg.pbkdf2_salt,
            sizeof(cfg.pbkdf2_salt),
            cfg.pbkdf2_iterations,
            kek)) {
        CryptoEngine::secureZeroize(rawDek, sizeof(rawDek));
        CryptoEngine::secureZeroize(kek, sizeof(kek));
        snprintf(outStatusMsg, statusMsgLen, "Erro derivando KEK");
        return false;
    }

    bool wrappedOk = CryptoEngine::wrapDek(rawDek, kek, cfg.wrap_iv, cfg.wrapped_dek, cfg.wrap_tag);
    CryptoEngine::secureZeroize(rawDek, sizeof(rawDek));
    CryptoEngine::secureZeroize(kek, sizeof(kek));

    if (!wrappedOk) {
        snprintf(outStatusMsg, statusMsgLen, "Erro no Envelope DEK");
        return false;
    }

    strncpy(cfg.device_id, doc["deviceId"] | "cardputer-primary-01", sizeof(cfg.device_id) - 1);
    strncpy(cfg.wifi_ssid, doc["network"]["wifiSsid"] | "", sizeof(cfg.wifi_ssid) - 1);
    strncpy(cfg.wifi_password, doc["network"]["wifiPassword"] | "", sizeof(cfg.wifi_password) - 1);
    strncpy(cfg.tunnel_url, doc["network"]["tunnelUrl"] | "", sizeof(cfg.tunnel_url) - 1);
    strncpy(cfg.cf_access_client_id, doc["network"]["cfAccessClientId"] | "", sizeof(cfg.cf_access_client_id) - 1);
    strncpy(cfg.cf_access_client_secret, doc["network"]["cfAccessClientSecret"] | "", sizeof(cfg.cf_access_client_secret) - 1);
    strncpy(cfg.mdns_service, doc["network"]["mdnsServiceName"] | "proton-bridge", sizeof(cfg.mdns_service) - 1);

    const char* layoutStr = doc["defaults"]["keyboardLayout"] | "PT_BR_ABNT2";
    cfg.kb_layout = (strcmp(layoutStr, "US_ANSI") == 0) ? KeyboardLayout::US_ANSI : KeyboardLayout::PT_BR_ABNT2;

    const char* hidStr = doc["defaults"]["hidMode"] | "USB";
    cfg.hid_mode = (strcmp(hidStr, "BLE") == 0) ? HidTransportMode::BLE_HID : HidTransportMode::USB_HID;

    cfg.max_unlock_attempts = doc["defaults"]["maxUnlockAttempts"] | DEFAULT_MAX_UNLOCK_TRIES;
    cfg.failed_attempts     = 0;
    cfg.provisioned         = true;

    if (!storage.saveSecurityConfig(cfg)) {
        snprintf(outStatusMsg, statusMsgLen, "Erro ao gravar NVS");
        return false;
    }

    storage.unlockWithPassphrase(newMasterPassphrase);
    snprintf(outStatusMsg, statusMsgLen, "Provisionado & SD Limpo!");
    return true;
#else
    return true;
#endif
}

} // namespace sentinel

