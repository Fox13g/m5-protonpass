#pragma once

#include <stdint.h>
#include <stddef.h>
#include <functional>
#include "types.h"
#include "vault_storage.h"

namespace sentinel {

using SyncProgressCallback = std::function<void(SyncStepState step, const char* detail, uint8_t progressPct)>;

class SyncClient {
public:
    explicit SyncClient(VaultStorage& storage);

    /**
     * Executa o ciclo completo da máquina de estados de sincronização:
     * 1. Liga o rádio Wi-Fi e conecta ao SSID configurado na NVS.
     * 2. Sincroniza o relógio de tempo real interno via SNTP (`pool.ntp.org` / `time.cloudflare.com`).
     * 3. Resolve o endpoint (URL HTTPS do Cloudflare Tunnel ou fallback mDNS local).
     * 4. Se existir `/outbox.bin` pendente, autentica com HMAC-SHA256 e envia via `POST /api/v1/sync/push`.
     * 5. Consulta `/api/v1/sync/pull` enviando `X-Local-Revision`.
     * 6. Se `HTTP 200`, faz streaming para `/vault.tmp`, valida a Tag GCM do índice e executa commit atómico.
     * 7. Desliga completamente o rádio Wi-Fi para maximizar a autonomia da bateria.
     */
    bool performSyncCycle(SyncProgressCallback onProgress);

    SyncStepState lastState() const { return m_state; }
    const char* lastError() const { return m_lastError; }

private:
    bool connectWifi(SyncProgressCallback& onProgress);
    bool syncTimeSntp(SyncProgressCallback& onProgress);
    bool pushPendingOutbox(const char* baseUrl, SyncProgressCallback& onProgress);
    bool pullVaultIncremental(const char* baseUrl, SyncProgressCallback& onProgress);
    void disconnectWifi();

    VaultStorage& m_storage;
    SyncStepState m_state;
    char          m_lastError[96];
};

} // namespace sentinel

