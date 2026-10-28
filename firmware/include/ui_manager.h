#pragma once

#include <stdint.h>
#include "config.h"
#include "types.h"
#include "vault_storage.h"
#include "hid_injector.h"
#include "sync_client.h"

namespace sentinel {

class UiManager {
public:
    UiManager(VaultStorage& storage, HidInjector& hid, SyncClient& sync);

    /**
     * Inicializa o ecrã TFT ST7789V2 (240x135) e aloca estaticamente apenas os 2 sprites parciais:
     * - `topBarSprite` (240x20 px = 9.600 bytes)
     * - `mainViewSprite` (240x115 px = 55.200 bytes)
     * Evita uso de framebuffer único pesado e opera perfeitamente sem PSRAM.
     */
    bool begin();

    /**
     * Ciclo principal de varrimento de teclado (a cada 15ms) e atualização visual.
     */
    void update();

    UiScreenState currentScreen() const { return m_screen; }
    void transitionTo(UiScreenState nextScreen);

private:
    void handleKeyboardInput();
    void handleUnlockInput(char ch, bool enterPressed, bool backspacePressed);
    void handleVaultListInput(char ch, bool fnHeld, bool enterPressed, bool backspacePressed);
    void handleItemDetailInput(char ch, bool fnHeld, bool enterPressed, bool backspacePressed);
    void handleNewItemInput(char ch, bool fnHeld, bool enterPressed, bool backspacePressed);

    void rebuildSearchFilter();
    void triggerCredentialInjection(InjectSequenceType seqType);
    void triggerSyncFlow();

    void renderTopBar();
    void renderMainView();
    void renderUnlockScreen();
    void renderVaultListScreen();
    void renderItemDetailScreen();
    void renderInjectingScreen();
    void renderSyncScreen();
    void renderNewItemScreen();

    uint8_t readBatteryPercentage();

    VaultStorage&     m_storage;
    HidInjector&      m_hid;
    SyncClient&       m_sync;

    UiScreenState     m_screen;
    bool              m_needsRedraw;
    uint32_t          m_lastInputMs;
    uint32_t          m_lastTopBarUpdateMs;

    // Buffer de PIN / Senha Mestra no desbloqueio
    char              m_pinBuffer[64];
    size_t            m_pinLen;
    char              m_statusBanner[64];

    // Estado da Pesquisa Instantânea e Lista Deslizante
    char              m_searchQuery[MAX_SEARCH_QUERY_LEN];
    size_t            m_searchLen;
    uint16_t          m_filteredIndices[MAX_INDEXED_ITEMS_IN_RAM];
    uint16_t          m_filteredCount;
    uint16_t          m_selectedFilteredPos;
    uint16_t          m_scrollOffset;

    // Estado do Item Decifrado Sob Demanda (sempre zerado ao sair da tela!)
    CpVaultItemPayload m_activeItemPlain;
    bool               m_hasActiveItem;
    bool               m_revealPassword;
    char               m_currentTotpCode[7];
    uint8_t            m_totpRemainingSec;

    // Estado de criação de nova senha offline
    char              m_newTitle[40];
    char              m_newUsername[64];
    char              m_newPassword[64];
    uint8_t           m_newFieldStep;

    // Estado visual da sincronização
    SyncStepState     m_syncVisualStep;
    char              m_syncDetailMsg[64];
    uint8_t           m_syncProgressPct;
};

} // namespace sentinel

