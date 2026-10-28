#pragma once

#include "vault_storage.h"

namespace sentinel {

class ProvisioningManager {
public:
    /**
     * Verifica no arranque se existe o ficheiro `/cardputer_provision.json` no cartão microSD.
     */
    static bool hasSdProvisionFile(VaultStorage& storage);

    /**
     * Importa `/cardputer_provision.json` do cartão microSD, deriva a KEK a partir da
     * `newMasterPassphrase` escolhida pelo utilizador no teclado físico, embrulha a DEK
     * na partição NVS e sobrescreve com zeros antes de apagar o ficheiro do cartão microSD!
     */
    static bool importFromSdAndWipe(
        VaultStorage& storage,
        const char* newMasterPassphrase,
        char* outStatusMsg,
        size_t statusMsgLen
    );

    /**
     * Permite provisionar também a partir de uma string JSON recebida via porta USB Serial/CDC.
     */
    static bool importFromJsonString(
        VaultStorage& storage,
        const char* jsonStr,
        const char* newMasterPassphrase,
        char* outStatusMsg,
        size_t statusMsgLen
    );
};

} // namespace sentinel

