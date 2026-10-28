#include "vault_storage.h"
#include "crypto_engine.h"
#include <string.h>
#include <stdio.h>

#if !defined(UNIT_TEST_NATIVE)
#include <Arduino.h>
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

namespace sentinel {

VaultStorage::VaultStorage()
    : m_unlocked(false)
    , m_itemCount(0)
    , m_spiMutex(nullptr)
{
    memset(m_activeDek, 0, sizeof(m_activeDek));
    memset(&m_secConfig, 0, sizeof(m_secConfig));
    memset(&m_header, 0, sizeof(m_header));
    memset(m_indexEntries, 0, sizeof(m_indexEntries));
}

bool VaultStorage::begin() {
#if !defined(UNIT_TEST_NATIVE)
    if (!m_spiMutex) {
        m_spiMutex = xSemaphoreCreateMutex();
    }

    if (!LittleFS.begin(true)) {
        return false;
    }

    loadSecurityConfig(m_secConfig);
    return true;
#else
    return true;
#endif
}

void VaultStorage::lockSpiBus() {
#if !defined(UNIT_TEST_NATIVE)
    if (m_spiMutex) {
        xSemaphoreTake(static_cast<SemaphoreHandle_t>(m_spiMutex), portMAX_DELAY);
    }
#endif
}

void VaultStorage::unlockSpiBus() {
#if !defined(UNIT_TEST_NATIVE)
    if (m_spiMutex) {
        xSemaphoreGive(static_cast<SemaphoreHandle_t>(m_spiMutex));
    }
#endif
}

bool VaultStorage::loadSecurityConfig(DeviceSecurityConfig& outCfg) {
#if !defined(UNIT_TEST_NATIVE)
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) {
        return false;
    }

    size_t readLen = prefs.getBytes("sec_cfg", &outCfg, sizeof(DeviceSecurityConfig));
    prefs.end();

    if (readLen != sizeof(DeviceSecurityConfig)) {
        memset(&outCfg, 0, sizeof(DeviceSecurityConfig));
        outCfg.provisioned         = false;
        outCfg.pbkdf2_iterations   = PBKDF2_DEFAULT_ITERATIONS;
        outCfg.max_unlock_attempts = DEFAULT_MAX_UNLOCK_TRIES;
        outCfg.kb_layout           = KeyboardLayout::PT_BR_ABNT2;
        outCfg.hid_mode            = HidTransportMode::USB_HID;
        strncpy(outCfg.device_id, "cardputer-primary-01", sizeof(outCfg.device_id) - 1);
        strncpy(outCfg.mdns_service, "proton-bridge", sizeof(outCfg.mdns_service) - 1);
        return false;
    }
    return true;
#else
    memset(&outCfg, 0, sizeof(DeviceSecurityConfig));
    return true;
#endif
}

bool VaultStorage::saveSecurityConfig(const DeviceSecurityConfig& cfg) {
    m_secConfig = cfg;
#if !defined(UNIT_TEST_NATIVE)
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        return false;
    }
    size_t written = prefs.putBytes("sec_cfg", &cfg, sizeof(DeviceSecurityConfig));
    prefs.end();
    return (written == sizeof(DeviceSecurityConfig));
#else
    return true;
#endif
}

bool VaultStorage::unlockWithPassphrase(const char* passphrase) {
    if (!passphrase || !m_secConfig.provisioned) {
        return false;
    }

    uint8_t derivedKek[32];
    bool okKek = CryptoEngine::deriveKekFromPassphrase(
        passphrase,
        m_secConfig.pbkdf2_salt,
        sizeof(m_secConfig.pbkdf2_salt),
        m_secConfig.pbkdf2_iterations,
        derivedKek
    );
    if (!okKek) {
        CryptoEngine::secureZeroize(derivedKek, sizeof(derivedKek));
        return false;
    }

    uint8_t candidateDek[32];
    bool okUnwrap = CryptoEngine::unwrapDek(
        m_secConfig.wrapped_dek,
        derivedKek,
        m_secConfig.wrap_iv,
        m_secConfig.wrap_tag,
        candidateDek
    );
    CryptoEngine::secureZeroize(derivedKek, sizeof(derivedKek));

    if (!okUnwrap) {
        CryptoEngine::secureZeroize(candidateDek, sizeof(candidateDek));
        m_secConfig.failed_attempts++;
        saveSecurityConfig(m_secConfig);

        if (m_secConfig.failed_attempts >= m_secConfig.max_unlock_attempts) {
            emergencyWipe();
        }
        return false;
    }

    // Sucesso! Zera contador de falhas na NVS se necessário
    if (m_secConfig.failed_attempts > 0) {
        m_secConfig.failed_attempts = 0;
        saveSecurityConfig(m_secConfig);
    }

    memcpy(m_activeDek, candidateDek, 32);
    CryptoEngine::secureZeroize(candidateDek, sizeof(candidateDek));
    m_unlocked = true;

    reloadVaultIndex();
    return true;
}

void VaultStorage::lockVault() {
    m_unlocked = false;
    m_itemCount = 0;
    CryptoEngine::secureZeroize(m_activeDek, sizeof(m_activeDek));
    CryptoEngine::secureZeroize(m_indexEntries, sizeof(m_indexEntries));
}

bool VaultStorage::reloadVaultIndex() {
    if (!m_unlocked) return false;

#if !defined(UNIT_TEST_NATIVE)
    if (!LittleFS.exists(VAULT_FILE_PATH)) {
        m_itemCount = 0;
        memset(&m_header, 0, sizeof(m_header));
        return true;
    }

    File f = LittleFS.open(VAULT_FILE_PATH, "r");
    if (!f || f.size() < CPVAULT_HEADER_SIZE) {
        if (f) f.close();
        return false;
    }

    CpVaultHeader hdr;
    if (f.read(reinterpret_cast<uint8_t*>(&hdr), CPVAULT_HEADER_SIZE) != CPVAULT_HEADER_SIZE) {
        f.close();
        return false;
    }

    if (memcmp(hdr.magic, CPVAULT_MAGIC, 4) != 0 || hdr.version != CPVAULT_VERSION) {
        f.close();
        return false;
    }

    uint16_t count = hdr.item_count;
    if (count > MAX_INDEXED_ITEMS_IN_RAM) {
        count = MAX_INDEXED_ITEMS_IN_RAM;
    }

    uint32_t expectedCipherLen = static_cast<uint32_t>(hdr.item_count) * CPVAULT_INDEX_ENTRY_SIZE;
    if (hdr.index_cipher_len != expectedCipherLen) {
        f.close();
        return false;
    }

    if (count == 0) {
        m_header = hdr;
        m_itemCount = 0;
        f.close();
        return true;
    }

    // Lê a tabela de índice cifrada e valida a Tag GCM + AAD do cabeçalho
    size_t readLen = static_cast<size_t>(count) * CPVAULT_INDEX_ENTRY_SIZE;
    uint8_t* cipherBuf = static_cast<uint8_t*>(malloc( expectedCipherLen ));
    if (!cipherBuf) {
        f.close();
        return false;
    }

    if (f.read(cipherBuf, expectedCipherLen) != expectedCipherLen) {
        free(cipherBuf);
        f.close();
        return false;
    }
    f.close();

    uint8_t* plainBuf = static_cast<uint8_t*>(malloc(expectedCipherLen));
    if (!plainBuf) {
        free(cipherBuf);
        return false;
    }

    bool authOk = CryptoEngine::decryptAes256Gcm(
        cipherBuf,
        expectedCipherLen,
        m_activeDek,
        hdr.index_iv,
        hdr.index_tag,
        reinterpret_cast<const uint8_t*>(&hdr),
        CPVAULT_HEADER_AAD_SIZE,
        plainBuf
    );
    free(cipherBuf);

    if (!authOk) {
        CryptoEngine::secureZeroize(plainBuf, expectedCipherLen);
        free(plainBuf);
        return false;
    }

    memcpy(m_indexEntries, plainBuf, readLen);
    CryptoEngine::secureZeroize(plainBuf, expectedCipherLen);
    free(plainBuf);

    m_header = hdr;
    m_itemCount = count;
    return true;
#else
    return true;
#endif
}

void VaultStorage::getRevisionHashHex(char outHex65[65]) const {
    CryptoEngine::bytesToHex(m_header.revision_hash, 32, outHex65);
}

bool VaultStorage::decryptItemOnDemand(uint16_t indexPos, CpVaultItemPayload& outPayload) {
    memset(&outPayload, 0, sizeof(outPayload));
    if (!m_unlocked || indexPos >= m_itemCount) {
        return false;
    }

#if !defined(UNIT_TEST_NATIVE)
    const CpVaultIndexEntry& entry = m_indexEntries[indexPos];
    if (entry.blob_cipher_len != CPVAULT_ITEM_PAYLOAD_SIZE) {
        return false;
    }

    File f = LittleFS.open(VAULT_FILE_PATH, "r");
    if (!f) return false;

    if (!f.seek(entry.blob_offset, SeekSet)) {
        f.close();
        return false;
    }

    uint8_t cipherBlob[CPVAULT_ITEM_PAYLOAD_SIZE];
    if (f.read(cipherBlob, CPVAULT_ITEM_PAYLOAD_SIZE) != CPVAULT_ITEM_PAYLOAD_SIZE) {
        f.close();
        return false;
    }
    f.close();

    // Constrói AAD de 24 bytes: "CPV1" (4B) || item_id (16B) || blob_offset (4B)
    CpVaultItemAad aad;
    memcpy(aad.magic, CPVAULT_MAGIC, 4);
    memcpy(aad.item_id, entry.item_id, 16);
    aad.blob_offset = entry.blob_offset;

    bool ok = CryptoEngine::decryptAes256Gcm(
        cipherBlob,
        CPVAULT_ITEM_PAYLOAD_SIZE,
        m_activeDek,
        entry.blob_iv,
        entry.blob_tag,
        reinterpret_cast<const uint8_t*>(&aad),
        sizeof(aad),
        reinterpret_cast<uint8_t*>(&outPayload)
    );
    CryptoEngine::secureZeroize(cipherBlob, sizeof(cipherBlob));
    return ok;
#else
    return false;
#endif
}

bool VaultStorage::commitTempVaultFile() {
    if (!m_unlocked) return false;
#if !defined(UNIT_TEST_NATIVE)
    if (!LittleFS.exists(VAULT_TMP_PATH)) {
        return false;
    }

    // Valida cabeçalho e Tag GCM da tabela de índice em /vault.tmp antes de renomear
    File f = LittleFS.open(VAULT_TMP_PATH, "r");
    if (!f || f.size() < CPVAULT_HEADER_SIZE) {
        if (f) f.close();
        LittleFS.remove(VAULT_TMP_PATH);
        return false;
    }

    CpVaultHeader tmpHdr;
    if (f.read(reinterpret_cast<uint8_t*>(&tmpHdr), CPVAULT_HEADER_SIZE) != CPVAULT_HEADER_SIZE) {
        f.close();
        LittleFS.remove(VAULT_TMP_PATH);
        return false;
    }

    if (memcmp(tmpHdr.magic, CPVAULT_MAGIC, 4) != 0 || tmpHdr.version != CPVAULT_VERSION) {
        f.close();
        LittleFS.remove(VAULT_TMP_PATH);
        return false;
    }

    uint32_t idxLen = tmpHdr.index_cipher_len;
    if (idxLen != static_cast<uint32_t>(tmpHdr.item_count) * CPVAULT_INDEX_ENTRY_SIZE) {
        f.close();
        LittleFS.remove(VAULT_TMP_PATH);
        return false;
    }

    if (idxLen > 0) {
        uint8_t* cipherIdx = static_cast<uint8_t*>(malloc(idxLen));
        uint8_t* plainIdx  = static_cast<uint8_t*>(malloc(idxLen));
        if (!cipherIdx || !plainIdx) {
            if (cipherIdx) free(cipherIdx);
            if (plainIdx) free(plainIdx);
            f.close();
            return false;
        }

        bool readOk = (f.read(cipherIdx, idxLen) == idxLen);
        f.close();

        bool authOk = readOk && CryptoEngine::decryptAes256Gcm(
            cipherIdx,
            idxLen,
            m_activeDek,
            tmpHdr.index_iv,
            tmpHdr.index_tag,
            reinterpret_cast<const uint8_t*>(&tmpHdr),
            CPVAULT_HEADER_AAD_SIZE,
            plainIdx
        );

        CryptoEngine::secureZeroize(plainIdx, idxLen);
        free(cipherIdx);
        free(plainIdx);

        if (!authOk) {
            LittleFS.remove(VAULT_TMP_PATH);
            return false;
        }
    } else {
        f.close();
    }

    // Troca atómica
    if (LittleFS.exists(VAULT_FILE_PATH)) {
        LittleFS.remove(VAULT_FILE_PATH);
    }
    if (!LittleFS.rename(VAULT_TMP_PATH, VAULT_FILE_PATH)) {
        return false;
    }

    return reloadVaultIndex();
#else
    return true;
#endif
}

bool VaultStorage::saveOfflineItemMutation(
    const char* title,
    const char* username,
    const char* password,
    const char* totpSecret,
    const char* url,
    const char* notes,
    const char* existingVaultId,
    const char* existingItemUuid
) {
    if (!m_unlocked || !title || !password) return false;

#if !defined(UNIT_TEST_NATIVE)
    JsonDocument doc;
    doc["deviceId"] = m_secConfig.device_id;
    JsonArray mutations = doc["mutations"].to<JsonArray>();

    JsonObject m = mutations.add<JsonObject>();
    bool isUpdate = (existingItemUuid && strlen(existingItemUuid) > 0);
    m["op"]         = isUpdate ? "update" : "create";
    m["vaultId"]    = (existingVaultId && strlen(existingVaultId) > 0) ? existingVaultId : "default";

    char uuidBuf[37];
    if (isUpdate) {
        strncpy(uuidBuf, existingItemUuid, sizeof(uuidBuf) - 1);
        uuidBuf[sizeof(uuidBuf) - 1] = '\0';
    } else {
        uint8_t rnd[16];
        CryptoEngine::fillRandomBytes(rnd, sizeof(rnd));
        CryptoEngine::bytesToHex(rnd, 16, uuidBuf);
    }

    m["itemUuid"]   = uuidBuf;
    m["title"]      = title;
    m["username"]   = username ? username : "";
    m["password"]   = password;
    m["totpSecret"] = totpSecret ? totpSecret : "";
    m["url"]        = url ? url : "";
    m["notes"]      = notes ? notes : "";
    m["updatedAt"]  = static_cast<uint64_t>(time(nullptr));

    char jsonBuf[1024];
    size_t jsonLen = serializeJson(doc, jsonBuf, sizeof(jsonBuf));
    if (jsonLen == 0) return false;

    uint8_t iv[12];
    uint8_t tag[16];
    CryptoEngine::fillRandomBytes(iv, sizeof(iv));

    uint8_t cipherBuf[1024];
    bool ok = CryptoEngine::encryptAes256Gcm(
        reinterpret_cast<const uint8_t*>(jsonBuf),
        jsonLen,
        m_activeDek,
        iv,
        reinterpret_cast<const uint8_t*>(CPOUTBOX_MAGIC),
        4,
        cipherBuf,
        tag
    );
    CryptoEngine::secureZeroize(jsonBuf, sizeof(jsonBuf));
    if (!ok) return false;

    File f = LittleFS.open(OUTBOX_FILE_PATH, "w");
    if (!f) return false;
    f.write(reinterpret_cast<const uint8_t*>(CPOUTBOX_MAGIC), 4);
    f.write(iv, 12);
    f.write(tag, 16);
    f.write(cipherBuf, jsonLen);
    f.close();
    return true;
#else
    return true;
#endif
}

bool VaultStorage::hasPendingOutbox() const {
#if !defined(UNIT_TEST_NATIVE)
    return LittleFS.exists(OUTBOX_FILE_PATH);
#else
    return false;
#endif
}

bool VaultStorage::readEncryptedOutbox(uint8_t* outBuf, size_t maxLen, size_t& outActualLen) {
    outActualLen = 0;
#if !defined(UNIT_TEST_NATIVE)
    if (!LittleFS.exists(OUTBOX_FILE_PATH)) return false;
    File f = LittleFS.open(OUTBOX_FILE_PATH, "r");
    if (!f) return false;
    size_t sz = f.size();
    if (sz > maxLen || sz < 32) {
        f.close();
        return false;
    }
    outActualLen = f.read(outBuf, sz);
    f.close();
    return (outActualLen == sz);
#else
    return false;
#endif
}

bool VaultStorage::clearOutbox() {
#if !defined(UNIT_TEST_NATIVE)
    if (LittleFS.exists(OUTBOX_FILE_PATH)) {
        return LittleFS.remove(OUTBOX_FILE_PATH);
    }
#endif
    return true;
}

bool VaultStorage::backupVaultToSdCard() {
#if !defined(UNIT_TEST_NATIVE)
    if (!LittleFS.exists(VAULT_FILE_PATH)) return false;

    lockSpiBus();
    SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    if (!SD.begin(PIN_SD_CS, SPI, 20000000)) {
        unlockSpiBus();
        return false;
    }

    File src = LittleFS.open(VAULT_FILE_PATH, "r");
    File dst = SD.open(SD_BACKUP_PATH, FILE_WRITE);
    if (!src || !dst) {
        if (src) src.close();
        if (dst) dst.close();
        SD.end();
        unlockSpiBus();
        return false;
    }

    uint8_t buf[512];
    while (src.available()) {
        size_t r = src.read(buf, sizeof(buf));
        dst.write(buf, r);
    }
    src.close();
    dst.flush();
    dst.close();
    SD.end();
    unlockSpiBus();
    return true;
#else
    return true;
#endif
}

void VaultStorage::emergencyWipe() {
    lockVault();
    memset(&m_secConfig, 0, sizeof(m_secConfig));
    saveSecurityConfig(m_secConfig);

#if !defined(UNIT_TEST_NATIVE)
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, false)) {
        prefs.clear();
        prefs.end();
    }
    LittleFS.remove(VAULT_FILE_PATH);
    LittleFS.remove(VAULT_TMP_PATH);
    LittleFS.remove(OUTBOX_FILE_PATH);
    LittleFS.format();
#endif
}

} // namespace sentinel

