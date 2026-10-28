#include "sync_client.h"
#include "crypto_engine.h"
#include "config.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

#if !defined(UNIT_TEST_NATIVE)
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#endif

namespace sentinel {

SyncClient::SyncClient(VaultStorage& storage)
    : m_storage(storage)
    , m_state(SyncStepState::OFFLINE_IDLE)
{
    memset(m_lastError, 0, sizeof(m_lastError));
}

bool SyncClient::performSyncCycle(SyncProgressCallback onProgress) {
    memset(m_lastError, 0, sizeof(m_lastError));

    if (!m_storage.isUnlocked()) {
        snprintf(m_lastError, sizeof(m_lastError), "Cofre bloqueado");
        m_state = SyncStepState::SYNC_ERROR;
        if (onProgress) onProgress(m_state, m_lastError, 0);
        return false;
    }

#if !defined(UNIT_TEST_NATIVE)
    // 1. Conectar Wi-Fi
    if (!connectWifi(onProgress)) {
        disconnectWifi();
        return false;
    }

    // 2. Sincronizar relógio UTC via SNTP (essencial para TOTP RFC 6238 e janela HMAC de 60s)
    if (!syncTimeSntp(onProgress)) {
        disconnectWifi();
        return false;
    }

    // Resolve URL base do Bridge Gateway (Cloudflare Tunnel ou mDNS local)
    char baseUrl[140];
    const DeviceSecurityConfig& cfg = m_storage.securityConfig();
    if (strlen(cfg.tunnel_url) > 0) {
        strncpy(baseUrl, cfg.tunnel_url, sizeof(baseUrl) - 1);
        baseUrl[sizeof(baseUrl) - 1] = '\0';
    } else {
        snprintf(baseUrl, sizeof(baseUrl), "http://%s.local:8443", cfg.mdns_service);
    }

    // 3. Push da fila offline (`outbox.bin`) caso existam alterações feitas sem rede
    if (m_storage.hasPendingOutbox()) {
        if (!pushPendingOutbox(baseUrl, onProgress)) {
            disconnectWifi();
            return false;
        }
    }

    // 4. Pull incremental do cofre `CPVAULT v1`
    if (!pullVaultIncremental(baseUrl, onProgress)) {
        disconnectWifi();
        return false;
    }

    // 5. Desliga rádio Wi-Fi para maximizar autonomia energética da bateria
    disconnectWifi();
    m_state = SyncStepState::SYNC_COMPLETE;
    if (onProgress) onProgress(m_state, "Sincronizado com sucesso!", 100);
    return true;
#else
    return true;
#endif
}

bool SyncClient::connectWifi(SyncProgressCallback& onProgress) {
#if !defined(UNIT_TEST_NATIVE)
    const DeviceSecurityConfig& cfg = m_storage.securityConfig();
    if (strlen(cfg.wifi_ssid) == 0) {
        snprintf(m_lastError, sizeof(m_lastError), "SSID Wi-Fi nao configurado");
        m_state = SyncStepState::SYNC_ERROR;
        if (onProgress) onProgress(m_state, m_lastError, 0);
        return false;
    }

    m_state = SyncStepState::WIFI_CONNECTING;
    if (onProgress) onProgress(m_state, cfg.wifi_ssid, 10);

    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.wifi_ssid, cfg.wifi_password);

    uint32_t startMs = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - startMs > 12000) {
            snprintf(m_lastError, sizeof(m_lastError), "Timeout ligacao Wi-Fi");
            m_state = SyncStepState::SYNC_ERROR;
            if (onProgress) onProgress(m_state, m_lastError, 10);
            return false;
        }
        delay(150);
    }
    return true;
#else
    return true;
#endif
}

bool SyncClient::syncTimeSntp(SyncProgressCallback& onProgress) {
#if !defined(UNIT_TEST_NATIVE)
    m_state = SyncStepState::TIME_SYNC_SNTP;
    if (onProgress) onProgress(m_state, "Sincronizando SNTP UTC...", 25);

    configTime(0, 0, "time.cloudflare.com", "pool.ntp.org");
    time_t now = time(nullptr);
    uint32_t startMs = millis();
    while (now < 1700000000 && (millis() - startMs < 6000)) {
        delay(100);
        now = time(nullptr);
    }
    return true;
#else
    return true;
#endif
}

bool SyncClient::pushPendingOutbox(const char* baseUrl, SyncProgressCallback& onProgress) {
#if !defined(UNIT_TEST_NATIVE)
    m_state = SyncStepState::PUSHING_OUTBOX;
    if (onProgress) onProgress(m_state, "Enviando outbox.bin...", 40);

    uint8_t outboxBuf[1280];
    size_t outboxLen = 0;
    if (!m_storage.readEncryptedOutbox(outboxBuf, sizeof(outboxBuf), outboxLen)) {
        return true;
    }

    const DeviceSecurityConfig& cfg = m_storage.securityConfig();
    const char* apiPath = "/api/v1/sync/push";

    char fullUrl[192];
    snprintf(fullUrl, sizeof(fullUrl), "%s%s", baseUrl, apiPath);

    char tsStr[24];
    snprintf(tsStr, sizeof(tsStr), "%llu", static_cast<unsigned long long>(time(nullptr)));

    uint8_t rndNonce[16];
    CryptoEngine::fillRandomBytes(rndNonce, sizeof(rndNonce));
    char nonceHex[33];
    CryptoEngine::bytesToHex(rndNonce, sizeof(rndNonce), nonceHex);

    char sigHex[65];
    CryptoEngine::computeRequestHmacHex(
        "POST",
        apiPath,
        tsStr,
        nonceHex,
        outboxBuf,
        outboxLen,
        cfg.hmac_secret,
        sigHex
    );

    WiFiClientSecure secureClient;
    secureClient.setInsecure(); // Payload é autenticado com HMAC-SHA256 e cifrado em AES-256-GCM E2EE
    HTTPClient http;

    if (!http.begin(secureClient, fullUrl)) {
        snprintf(m_lastError, sizeof(m_lastError), "Erro HTTP push init");
        m_state = SyncStepState::SYNC_ERROR;
        if (onProgress) onProgress(m_state, m_lastError, 40);
        return false;
    }

    http.addHeader("Content-Type", "application/octet-stream");
    http.addHeader("X-Sentinel-Device-Id", cfg.device_id);
    http.addHeader("X-Sentinel-Timestamp", tsStr);
    http.addHeader("X-Sentinel-Nonce", nonceHex);
    http.addHeader("X-Sentinel-Signature", sigHex);

    if (strlen(cfg.cf_access_client_id) > 0) {
        http.addHeader("CF-Access-Client-Id", cfg.cf_access_client_id);
        http.addHeader("CF-Access-Client-Secret", cfg.cf_access_client_secret);
    }

    int code = http.POST(outboxBuf, outboxLen);
    http.end();

    if (code == 200) {
        m_storage.clearOutbox();
        return true;
    }

    snprintf(m_lastError, sizeof(m_lastError), "Erro Push HTTP %d", code);
    m_state = SyncStepState::SYNC_ERROR;
    if (onProgress) onProgress(m_state, m_lastError, 40);
    return false;
#else
    return true;
#endif
}

bool SyncClient::pullVaultIncremental(const char* baseUrl, SyncProgressCallback& onProgress) {
#if !defined(UNIT_TEST_NATIVE)
    m_state = SyncStepState::CHECKING_REVISION;
    if (onProgress) onProgress(m_state, "Verificando revisao SHA256...", 55);

    const DeviceSecurityConfig& cfg = m_storage.securityConfig();
    const char* apiPath = "/api/v1/sync/pull";

    char fullUrl[192];
    snprintf(fullUrl, sizeof(fullUrl), "%s%s", baseUrl, apiPath);

    char tsStr[24];
    snprintf(tsStr, sizeof(tsStr), "%llu", static_cast<unsigned long long>(time(nullptr)));

    uint8_t rndNonce[16];
    CryptoEngine::fillRandomBytes(rndNonce, sizeof(rndNonce));
    char nonceHex[33];
    CryptoEngine::bytesToHex(rndNonce, sizeof(rndNonce), nonceHex);

    char sigHex[65];
    CryptoEngine::computeRequestHmacHex(
        "GET",
        apiPath,
        tsStr,
        nonceHex,
        nullptr,
        0,
        cfg.hmac_secret,
        sigHex
    );

    char localRevHex[65];
    m_storage.getRevisionHashHex(localRevHex);

    WiFiClientSecure secureClient;
    secureClient.setInsecure();
    HTTPClient http;

    if (!http.begin(secureClient, fullUrl)) {
        snprintf(m_lastError, sizeof(m_lastError), "Erro HTTP pull init");
        m_state = SyncStepState::SYNC_ERROR;
        if (onProgress) onProgress(m_state, m_lastError, 55);
        return false;
    }

    http.addHeader("X-Sentinel-Device-Id", cfg.device_id);
    http.addHeader("X-Sentinel-Timestamp", tsStr);
    http.addHeader("X-Sentinel-Nonce", nonceHex);
    http.addHeader("X-Sentinel-Signature", sigHex);
    if (m_storage.itemCount() > 0) {
        http.addHeader("X-Local-Revision", localRevHex);
    }

    if (strlen(cfg.cf_access_client_id) > 0) {
        http.addHeader("CF-Access-Client-Id", cfg.cf_access_client_id);
        http.addHeader("CF-Access-Client-Secret", cfg.cf_access_client_secret);
    }

    int code = http.GET();
    if (code == 304) {
        http.end();
        return true; // Cofre local já está idêntico ao servidor!
    }

    if (code != 200) {
        http.end();
        snprintf(m_lastError, sizeof(m_lastError), "Falha Pull HTTP %d", code);
        m_state = SyncStepState::SYNC_ERROR;
        if (onProgress) onProgress(m_state, m_lastError, 60);
        return false;
    }

    // Streaming em blocos pequenos direto para /vault.tmp para poupar os 512 KB de SRAM
    m_state = SyncStepState::STREAMING_VAULT;
    if (onProgress) onProgress(m_state, "Recebendo CPVAULT v1...", 75);

    File tmpFile = LittleFS.open(VAULT_TMP_PATH, "w");
    if (!tmpFile) {
        http.end();
        snprintf(m_lastError, sizeof(m_lastError), "Erro abrindo vault.tmp");
        m_state = SyncStepState::SYNC_ERROR;
        return false;
    }

    int totalLen = http.getSize();
    int remaining = totalLen;
    WiFiClient* stream = http.getStreamPtr();
    uint8_t chunkBuf[512];

    while (http.connected() && (remaining > 0 || remaining == -1)) {
        size_t avail = stream->available();
        if (avail > 0) {
            size_t toRead = (avail > sizeof(chunkBuf)) ? sizeof(chunkBuf) : avail;
            int readBytes = stream->readBytes(chunkBuf, toRead);
            if (readBytes > 0) {
                tmpFile.write(chunkBuf, static_cast<size_t>(readBytes));
                if (remaining > 0) {
                    remaining -= readBytes;
                }
            }
        } else {
            delay(5);
        }
    }

    tmpFile.flush();
    tmpFile.close();
    http.end();

    m_state = SyncStepState::ATOMIC_COMMIT;
    if (onProgress) onProgress(m_state, "Validando GCM Tag & Commit...", 90);

    if (!m_storage.commitTempVaultFile()) {
        snprintf(m_lastError, sizeof(m_lastError), "Tag GCM invalida no Pull!");
        m_state = SyncStepState::SYNC_ERROR;
        if (onProgress) onProgress(m_state, m_lastError, 90);
        return false;
    }

    return true;
#else
    return true;
#endif
}

void SyncClient::disconnectWifi() {
#if !defined(UNIT_TEST_NATIVE)
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
#endif
}

} // namespace sentinel

