#pragma once

#include <stdint.h>
#include <stddef.h>
#include "config.h"
#include "types.h"

namespace sentinel {

class VaultStorage {
public:
    VaultStorage();

    /**
     * Inicializa o mutex global do barramento SPI, monta a partição LittleFS interna
     * e carrega a configuração de segurança da partição NVS.
     */
    bool begin();

    /**
     * Adquire e liberta o mutex de barramento SPI partilhado entre o TFT ST7789V2 e o leitor microSD.
     */
    void lockSpiBus();
    void unlockSpiBus();

    /**
     * Carrega ou persiste a estrutura `DeviceSecurityConfig` na NVS do ESP32-S3.
     */
    bool loadSecurityConfig(DeviceSecurityConfig& outCfg);
    bool saveSecurityConfig(const DeviceSecurityConfig& cfg);

    /**
     * Tenta desbloquear o cofre com o PIN / Senha Mestra introduzida pelo utilizador.
     * Incrementa o contador `failed_attempts` na NVS em caso de erro e executa Auto-Wipe
     * caso atinja `max_unlock_attempts`.
     */
    bool unlockWithPassphrase(const char* passphrase);

    /**
     * Bloqueia imediatamente o cofre e limpa a DEK e o índice da SRAM (`secureZeroize`).
     */
    void lockVault();

    bool isUnlocked() const { return m_unlocked; }
    const DeviceSecurityConfig& securityConfig() const { return m_secConfig; }
    DeviceSecurityConfig& mutableSecurityConfig() { return m_secConfig; }

    /**
     * Recarrega e autentica o cabeçalho e a tabela de índice do ficheiro `/vault.bin` no LittleFS.
     */
    bool reloadVaultIndex();

    /**
     * Retorna o número total de itens carregados no índice em SRAM e acesso às entradas.
     */
    uint16_t itemCount() const { return m_itemCount; }
    const CpVaultIndexEntry* indexEntries() const { return m_indexEntries; }
    const CpVaultHeader& vaultHeader() const { return m_header; }
    void getRevisionHashHex(char outHex65[65]) const;

    /**
     * Decifra SOB DEMANDA (On-Demand) apenas um único registo do LittleFS usando `fseek()`.
     * O chamador DEVE invocar `CryptoEngine::secureZeroize(&outPayload, sizeof(outPayload))` após usar!
     */
    bool decryptItemOnDemand(uint16_t indexPos, CpVaultItemPayload& outPayload);

    /**
     * Valida a integridade criptográfica de `/vault.tmp` e faz o commit atómico (`rename`) para `/vault.bin`.
     */
    bool commitTempVaultFile();

    /**
     * Cria ou atualiza uma credencial localmente quando offline, registando na fila `/outbox.bin`
     * e atualizando o cofre local `/vault.bin`.
     */
    bool saveOfflineItemMutation(
        const char* title,
        const char* username,
        const char* password,
        const char* totpSecret,
        const char* url,
        const char* notes,
        const char* existingVaultId = "default",
        const char* existingItemUuid = ""
    );

    bool hasPendingOutbox() const;
    bool readEncryptedOutbox(uint8_t* outBuf, size_t maxLen, size_t& outActualLen);
    bool clearOutbox();

    /**
     * Exporta uma cópia de backup de `/vault.bin` para o cartão microSD (`/sentinel_vault_backup.enc`)
     * e restaura automaticamente quando o firmware é recarregado via M5Launcher.
     */
    bool backupVaultToSdCard();
    bool restoreVaultFromSdCardIfNeeded();

    /**
     * Destrói imediatamente a DEK embrulhada na NVS e formata o cofre no LittleFS.
     */
    void emergencyWipe();

    const uint8_t* activeDek() const { return m_activeDek; }

private:
    bool                 m_unlocked;
    uint8_t              m_activeDek[32];
    DeviceSecurityConfig m_secConfig;
    CpVaultHeader        m_header;
    uint16_t             m_itemCount;
    CpVaultIndexEntry    m_indexEntries[MAX_INDEXED_ITEMS_IN_RAM];
    void*                m_spiMutex;
};

} // namespace sentinel

