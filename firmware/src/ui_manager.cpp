#include "ui_manager.h"
#include "provisioning.h"
#include "crypto_engine.h"
#include "totp_engine.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>

#if !defined(UNIT_TEST_NATIVE)
#include <Arduino.h>
#include <M5Cardputer.h>
#include <M5GFX.h>

namespace {
M5Canvas* g_topBarSprite   = nullptr;
M5Canvas* g_mainViewSprite = nullptr;
}
#endif

namespace sentinel {

static bool containsIgnoreCase(const char* haystack, const char* needle) {
    if (!needle || needle[0] == '\0') return true;
    if (!haystack) return false;

    size_t nLen = strlen(needle);
    size_t hLen = strlen(haystack);
    if (nLen > hLen) return false;

    for (size_t i = 0; i <= hLen - nLen; ++i) {
        size_t j = 0;
        while (j < nLen && tolower(static_cast<unsigned char>(haystack[i + j])) ==
                           tolower(static_cast<unsigned char>(needle[j]))) {
            ++j;
        }
        if (j == nLen) return true;
    }
    return false;
}

UiManager::UiManager(VaultStorage& storage, HidInjector& hid, SyncClient& sync)
    : m_storage(storage)
    , m_hid(hid)
    , m_sync(sync)
    , m_screen(UiScreenState::SCREEN_UNLOCK)
    , m_needsRedraw(true)
    , m_lastInputMs(0)
    , m_lastTopBarUpdateMs(0)
    , m_pinLen(0)
    , m_searchLen(0)
    , m_filteredCount(0)
    , m_selectedFilteredPos(0)
    , m_scrollOffset(0)
    , m_hasActiveItem(false)
    , m_revealPassword(false)
    , m_totpRemainingSec(30)
    , m_newFieldStep(0)
    , m_syncVisualStep(SyncStepState::OFFLINE_IDLE)
    , m_syncProgressPct(0)
{
    memset(m_pinBuffer, 0, sizeof(m_pinBuffer));
    memset(m_statusBanner, 0, sizeof(m_statusBanner));
    memset(m_searchQuery, 0, sizeof(m_searchQuery));
    memset(m_filteredIndices, 0, sizeof(m_filteredIndices));
    memset(&m_activeItemPlain, 0, sizeof(m_activeItemPlain));
    memset(m_currentTotpCode, 0, sizeof(m_currentTotpCode));
    memset(m_newTitle, 0, sizeof(m_newTitle));
    memset(m_newUsername, 0, sizeof(m_newUsername));
    memset(m_newPassword, 0, sizeof(m_newPassword));
    memset(m_syncDetailMsg, 0, sizeof(m_syncDetailMsg));
}

bool UiManager::begin() {
#if !defined(UNIT_TEST_NATIVE)
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.fillScreen(TFT_BLACK);

    // Aloca os dois sprites estáticos uma única vez em SRAM (sem PSRAM)
    g_topBarSprite = new M5Canvas(&M5Cardputer.Display);
    g_topBarSprite->setColorDepth(16);
    g_topBarSprite->createSprite(DISPLAY_WIDTH, TOP_BAR_HEIGHT);

    g_mainViewSprite = new M5Canvas(&M5Cardputer.Display);
    g_mainViewSprite->setColorDepth(16);
    g_mainViewSprite->createSprite(DISPLAY_WIDTH, MAIN_VIEW_HEIGHT);

    // Verifica se há ficheiro de provisionamento no microSD
    if (ProvisioningManager::hasSdProvisionFile(m_storage)) {
        snprintf(m_statusBanner, sizeof(m_statusBanner), "SD Detectado! Crie a Senha Mestra:");
        m_screen = UiScreenState::SCREEN_PROVISION_SD;
    } else if (!m_storage.securityConfig().provisioned) {
        snprintf(m_statusBanner, sizeof(m_statusBanner), "Insira SD c/ cardputer_provision.json");
        m_screen = UiScreenState::SCREEN_UNLOCK;
    } else {
        snprintf(m_statusBanner, sizeof(m_statusBanner), "Digite PIN ou Senha Mestra:");
        m_screen = UiScreenState::SCREEN_UNLOCK;
    }

    m_hid.setLayout(m_storage.securityConfig().kb_layout);
    m_hid.setTransportMode(m_storage.securityConfig().hid_mode);
    m_lastInputMs = millis();
    renderTopBar();
    renderMainView();
#endif
    return true;
}

void UiManager::transitionTo(UiScreenState nextScreen) {
    if (m_screen == UiScreenState::SCREEN_ITEM_DETAIL && nextScreen != UiScreenState::SCREEN_ITEM_DETAIL) {
        // Garante limpeza imediata da senha e segredo TOTP na SRAM ao sair dos detalhes!
        CryptoEngine::secureZeroize(&m_activeItemPlain, sizeof(m_activeItemPlain));
        CryptoEngine::secureZeroize(m_currentTotpCode, sizeof(m_currentTotpCode));
        m_hasActiveItem = false;
        m_revealPassword = false;
    }
    m_screen = nextScreen;
    m_needsRedraw = true;
}

void UiManager::update() {
#if !defined(UNIT_TEST_NATIVE)
    uint32_t nowMs = millis();

    // Auto-lock por inatividade para proteger o dispositivo aberto
    if (m_storage.isUnlocked() && (nowMs - m_lastInputMs > AUTO_LOCK_TIMEOUT_MS)) {
        CryptoEngine::secureZeroize(&m_activeItemPlain, sizeof(m_activeItemPlain));
        m_storage.lockVault();
        snprintf(m_statusBanner, sizeof(m_statusBanner), "Bloqueado por inatividade");
        transitionTo(UiScreenState::SCREEN_UNLOCK);
    }

    handleKeyboardInput();

    // Atualiza barra de topo e contador regressivo TOTP a cada 1 segundo
    if (nowMs - m_lastTopBarUpdateMs >= 1000) {
        m_lastTopBarUpdateMs = nowMs;
        renderTopBar();

        if (m_screen == UiScreenState::SCREEN_ITEM_DETAIL && m_hasActiveItem) {
            if (strlen(m_activeItemPlain.totp_secret) > 0) {
                TotpEngine::generateTotpCode(
                    m_activeItemPlain.totp_secret,
                    static_cast<uint64_t>(time(nullptr)),
                    m_currentTotpCode,
                    &m_totpRemainingSec
                );
                m_needsRedraw = true;
            }
        }
    }

    if (m_needsRedraw) {
        m_needsRedraw = false;
        renderTopBar();
        renderMainView();
    }
#endif
}

void UiManager::handleKeyboardInput() {
#if !defined(UNIT_TEST_NATIVE)
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
        return;
    }

    m_lastInputMs = millis();
    Keyboard_Class::KeysState ks = M5Cardputer.Keyboard.keysState();

    char typedChar = '\0';
    if (!ks.word.empty()) {
        typedChar = ks.word[0];
    }

    switch (m_screen) {
        case UiScreenState::SCREEN_UNLOCK:
        case UiScreenState::SCREEN_PROVISION_SD:
            handleUnlockInput(typedChar, ks.enter, ks.del);
            break;

        case UiScreenState::SCREEN_VAULT_LIST:
            handleVaultListInput(typedChar, ks.fn, ks.enter, ks.del);
            break;

        case UiScreenState::SCREEN_ITEM_DETAIL:
            handleItemDetailInput(typedChar, ks.fn, ks.enter, ks.del);
            break;

        case UiScreenState::SCREEN_NEW_ITEM:
            handleNewItemInput(typedChar, ks.fn, ks.enter, ks.del);
            break;

        default:
            break;
    }
#endif
}

void UiManager::handleUnlockInput(char ch, bool enterPressed, bool backspacePressed) {
    if (backspacePressed) {
        if (m_pinLen > 0) {
            m_pinBuffer[--m_pinLen] = '\0';
            m_needsRedraw = true;
        }
        return;
    }

    if (enterPressed) {
        if (m_pinLen == 0) return;

        if (m_screen == UiScreenState::SCREEN_PROVISION_SD) {
            bool ok = ProvisioningManager::importFromSdAndWipe(
                m_storage,
                m_pinBuffer,
                m_statusBanner,
                sizeof(m_statusBanner)
            );
            CryptoEngine::secureZeroize(m_pinBuffer, sizeof(m_pinBuffer));
            m_pinLen = 0;

            if (ok) {
                m_hid.setLayout(m_storage.securityConfig().kb_layout);
                m_hid.setTransportMode(m_storage.securityConfig().hid_mode);
                rebuildSearchFilter();
                transitionTo(UiScreenState::SCREEN_VAULT_LIST);
            } else {
                m_needsRedraw = true;
            }
            return;
        }

        // Desbloqueio normal via PBKDF2-HMAC-SHA256 (100.000 iterações)
        snprintf(m_statusBanner, sizeof(m_statusBanner), "Derivando KEK (PBKDF2 100k)...");
        renderMainView();

        bool unlocked = m_storage.unlockWithPassphrase(m_pinBuffer);
        CryptoEngine::secureZeroize(m_pinBuffer, sizeof(m_pinBuffer));
        m_pinLen = 0;

        if (unlocked) {
            rebuildSearchFilter();
            transitionTo(UiScreenState::SCREEN_VAULT_LIST);
        } else {
            snprintf(
                m_statusBanner,
                sizeof(m_statusBanner),
                "Senha incorreta! Falhas: %u/%u",
                m_storage.securityConfig().failed_attempts,
                m_storage.securityConfig().max_unlock_attempts
            );
            m_needsRedraw = true;
        }
        return;
    }

    if (ch >= 32 && ch <= 126 && m_pinLen < sizeof(m_pinBuffer) - 1) {
        m_pinBuffer[m_pinLen++] = ch;
        m_pinBuffer[m_pinLen]   = '\0';
        m_needsRedraw = true;
    }
}

void UiManager::handleVaultListInput(char ch, bool fnHeld, bool enterPressed, bool backspacePressed) {
    if (fnHeld) {
        char lower = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
        if (lower == 's') {
            triggerSyncFlow();
            return;
        }
        if (lower == 'l') {
            m_hid.toggleLayout();
            m_storage.mutableSecurityConfig().kb_layout = m_hid.layout();
            m_storage.saveSecurityConfig(m_storage.securityConfig());
            m_needsRedraw = true;
            return;
        }
        if (lower == 'b') {
            m_hid.toggleTransportMode();
            m_storage.mutableSecurityConfig().hid_mode = m_hid.transportMode();
            m_storage.saveSecurityConfig(m_storage.securityConfig());
            m_needsRedraw = true;
            return;
        }
        if (lower == 'u') {
            triggerCredentialInjection(InjectSequenceType::USERNAME_ONLY);
            return;
        }
        if (lower == 'p') {
            triggerCredentialInjection(InjectSequenceType::PASSWORD_ONLY);
            return;
        }
        if (lower == 't') {
            triggerCredentialInjection(InjectSequenceType::TOTP_ONLY);
            return;
        }
        if (lower == 'v' && m_filteredCount > 0) {
            uint16_t rawIdx = m_filteredIndices[m_selectedFilteredPos];
            if (m_storage.decryptItemOnDemand(rawIdx, m_activeItemPlain)) {
                m_hasActiveItem = true;
                m_revealPassword = false;
                if (strlen(m_activeItemPlain.totp_secret) > 0) {
                    TotpEngine::generateTotpCode(
                        m_activeItemPlain.totp_secret,
                        static_cast<uint64_t>(time(nullptr)),
                        m_currentTotpCode,
                        &m_totpRemainingSec
                    );
                } else {
                    strcpy(m_currentTotpCode, "------");
                }
                transitionTo(UiScreenState::SCREEN_ITEM_DETAIL);
            }
            return;
        }
        if (lower == 'n') {
            memset(m_newTitle, 0, sizeof(m_newTitle));
            memset(m_newUsername, 0, sizeof(m_newUsername));
            CryptoEngine::generateStrongPassword(m_newPassword, 20);
            m_newFieldStep = 0;
            transitionTo(UiScreenState::SCREEN_NEW_ITEM);
            return;
        }
    }

    // Navegação Cima (';') e Baixo ('.')
    if (ch == ';' && m_filteredCount > 0) {
        if (m_selectedFilteredPos > 0) {
            m_selectedFilteredPos--;
            if (m_selectedFilteredPos < m_scrollOffset) {
                m_scrollOffset = m_selectedFilteredPos;
            }
            m_needsRedraw = true;
        }
        return;
    }
    if (ch == '.' && m_filteredCount > 0) {
        if (m_selectedFilteredPos + 1 < m_filteredCount) {
            m_selectedFilteredPos++;
            if (m_selectedFilteredPos >= m_scrollOffset + VISIBLE_LIST_ROWS) {
                m_scrollOffset = m_selectedFilteredPos - VISIBLE_LIST_ROWS + 1;
            }
            m_needsRedraw = true;
        }
        return;
    }

    if (enterPressed && m_filteredCount > 0) {
        triggerCredentialInjection(InjectSequenceType::FULL_AUTO_TYPE);
        return;
    }

    if (backspacePressed) {
        if (m_searchLen > 0) {
            m_searchQuery[--m_searchLen] = '\0';
            rebuildSearchFilter();
            m_needsRedraw = true;
        }
        return;
    }

    // Pesquisa instantânea (search-as-you-type)
    if (ch >= 32 && ch <= 126 && m_searchLen < MAX_SEARCH_QUERY_LEN - 1) {
        m_searchQuery[m_searchLen++] = ch;
        m_searchQuery[m_searchLen]   = '\0';
        rebuildSearchFilter();
        m_needsRedraw = true;
    }
}

void UiManager::handleItemDetailInput(char ch, bool fnHeld, bool enterPressed, bool backspacePressed) {
    if (backspacePressed) {
        transitionTo(UiScreenState::SCREEN_VAULT_LIST);
        return;
    }

    if (enterPressed) {
        triggerCredentialInjection(InjectSequenceType::FULL_AUTO_TYPE);
        return;
    }

    if (fnHeld) {
        char lower = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
        if (lower == 'r') {
            m_revealPassword = !m_revealPassword;
            m_needsRedraw = true;
        } else if (lower == 't') {
            triggerCredentialInjection(InjectSequenceType::TOTP_ONLY);
        } else if (lower == 'p') {
            triggerCredentialInjection(InjectSequenceType::PASSWORD_ONLY);
        } else if (lower == 'u') {
            triggerCredentialInjection(InjectSequenceType::USERNAME_ONLY);
        }
    }
}

void UiManager::handleNewItemInput(char ch, bool fnHeld, bool enterPressed, bool backspacePressed) {
    if (fnHeld && tolower(static_cast<unsigned char>(ch)) == 'q') {
        CryptoEngine::secureZeroize(m_newPassword, sizeof(m_newPassword));
        transitionTo(UiScreenState::SCREEN_VAULT_LIST);
        return;
    }

    char* targetBuf = (m_newFieldStep == 0) ? m_newTitle :
                      (m_newFieldStep == 1) ? m_newUsername : m_newPassword;
    size_t maxLen   = (m_newFieldStep == 0) ? sizeof(m_newTitle) - 1 :
                      (m_newFieldStep == 1) ? sizeof(m_newUsername) - 1 : sizeof(m_newPassword) - 1;
    size_t curLen   = strlen(targetBuf);

    if (backspacePressed) {
        if (curLen > 0) {
            targetBuf[curLen - 1] = '\0';
            m_needsRedraw = true;
        } else if (m_newFieldStep > 0) {
            m_newFieldStep--;
            m_needsRedraw = true;
        } else {
            CryptoEngine::secureZeroize(m_newPassword, sizeof(m_newPassword));
            transitionTo(UiScreenState::SCREEN_VAULT_LIST);
        }
        return;
    }

    if (enterPressed) {
        if (m_newFieldStep < 2) {
            m_newFieldStep++;
            m_needsRedraw = true;
        } else {
            m_storage.saveOfflineItemMutation(
                m_newTitle,
                m_newUsername,
                m_newPassword,
                "",
                "",
                "Criado no Cardputer"
            );
            CryptoEngine::secureZeroize(m_newPassword, sizeof(m_newPassword));
            transitionTo(UiScreenState::SCREEN_VAULT_LIST);
        }
        return;
    }

    if (ch >= 32 && ch <= 126 && curLen < maxLen) {
        targetBuf[curLen]     = ch;
        targetBuf[curLen + 1] = '\0';
        m_needsRedraw = true;
    }
}

void UiManager::rebuildSearchFilter() {
    m_filteredCount = 0;
    uint16_t total = m_storage.itemCount();
    const CpVaultIndexEntry* entries = m_storage.indexEntries();

    for (uint16_t i = 0; i < total; ++i) {
        if (containsIgnoreCase(entries[i].title, m_searchQuery) ||
            containsIgnoreCase(entries[i].username_hint, m_searchQuery)) {
            m_filteredIndices[m_filteredCount++] = i;
        }
    }

    m_selectedFilteredPos = 0;
    m_scrollOffset = 0;
}

void UiManager::triggerCredentialInjection(InjectSequenceType seqType) {
    if (m_filteredCount == 0) return;

    uint16_t rawIdx = m_filteredIndices[m_selectedFilteredPos];
    CpVaultItemPayload tempPlain;
    if (!m_storage.decryptItemOnDemand(rawIdx, tempPlain)) {
        return;
    }

    char totpCode[7] = {0};
    if (seqType == InjectSequenceType::TOTP_ONLY && strlen(tempPlain.totp_secret) > 0) {
        TotpEngine::generateTotpCode(
            tempPlain.totp_secret,
            static_cast<uint64_t>(time(nullptr)),
            totpCode
        );
    }

    UiScreenState prevScreen = m_screen;
    m_screen = UiScreenState::SCREEN_INJECTING;
    renderMainView();

    m_hid.executeInjectionSequence(seqType, tempPlain.username, tempPlain.password, totpCode);

    // Limpeza criptográfica imediata da SRAM!
    CryptoEngine::secureZeroize(&tempPlain, sizeof(tempPlain));
    CryptoEngine::secureZeroize(totpCode, sizeof(totpCode));

    m_screen = prevScreen;
    m_needsRedraw = true;
}

void UiManager::triggerSyncFlow() {
    transitionTo(UiScreenState::SCREEN_SYNC);

    m_sync.performSyncCycle([this](SyncStepState step, const char* detail, uint8_t pct) {
        m_syncVisualStep = step;
        strncpy(m_syncDetailMsg, detail ? detail : "", sizeof(m_syncDetailMsg) - 1);
        m_syncDetailMsg[sizeof(m_syncDetailMsg) - 1] = '\0';
        m_syncProgressPct = pct;
        renderTopBar();
        renderMainView();
    });

#if !defined(UNIT_TEST_NATIVE)
    delay(900);
#endif
    rebuildSearchFilter();
    transitionTo(UiScreenState::SCREEN_VAULT_LIST);
}

uint8_t UiManager::readBatteryPercentage() {
#if !defined(UNIT_TEST_NATIVE)
    int mv = M5Cardputer.Power.getBatteryVoltage();
    if (mv <= 3300) return 0;
    if (mv >= 4180) return 100;
    return static_cast<uint8_t>(((mv - 3300) * 100) / (4180 - 3300));
#else
    return 90;
#endif
}

void UiManager::renderTopBar() {
#if !defined(UNIT_TEST_NATIVE)
    if (!g_topBarSprite) return;

    m_storage.lockSpiBus();
    g_topBarSprite->fillSprite(0x00C3); // Fundo verde escuro profundo
    g_topBarSprite->setTextSize(1);
    g_topBarSprite->setTextColor(TFT_GREEN, 0x00C3);
    g_topBarSprite->drawString("SENTINEL", 4, 6);

    const char* modeStr = (m_hid.transportMode() == HidTransportMode::USB_HID) ? "USB" : "BLE";
    const char* layStr  = (m_hid.layout() == KeyboardLayout::PT_BR_ABNT2) ? "ABNT2" : "US";
    char badge[20];
    snprintf(badge, sizeof(badge), "[%s:%s]", modeStr, layStr);
    g_topBarSprite->setTextColor(TFT_CYAN, 0x00C3);
    g_topBarSprite->drawString(badge, 60, 6);

    const char* wifiStr = (m_screen == UiScreenState::SCREEN_SYNC) ? "WiFi:ON" : "WiFi:OFF";
    g_topBarSprite->setTextColor((m_screen == UiScreenState::SCREEN_SYNC) ? TFT_YELLOW : TFT_SILVER, 0x00C3);
    g_topBarSprite->drawString(wifiStr, 138, 6);

    char batStr[12];
    snprintf(batStr, sizeof(batStr), "%u%%", readBatteryPercentage());
    g_topBarSprite->setTextColor(TFT_GREEN, 0x00C3);
    g_topBarSprite->drawRightString(batStr, DISPLAY_WIDTH - 4, 6);

    g_topBarSprite->drawFastHLine(0, TOP_BAR_HEIGHT - 1, DISPLAY_WIDTH, TFT_DARKGREEN);
    g_topBarSprite->pushSprite(0, 0);
    m_storage.unlockSpiBus();
#endif
}

void UiManager::renderMainView() {
#if !defined(UNIT_TEST_NATIVE)
    if (!g_mainViewSprite) return;

    m_storage.lockSpiBus();
    g_mainViewSprite->fillSprite(TFT_BLACK);
    g_mainViewSprite->setTextSize(1);

    switch (m_screen) {
        case UiScreenState::SCREEN_UNLOCK:
        case UiScreenState::SCREEN_PROVISION_SD:
            renderUnlockScreen();
            break;
        case UiScreenState::SCREEN_VAULT_LIST:
            renderVaultListScreen();
            break;
        case UiScreenState::SCREEN_ITEM_DETAIL:
            renderItemDetailScreen();
            break;
        case UiScreenState::SCREEN_INJECTING:
            renderInjectingScreen();
            break;
        case UiScreenState::SCREEN_SYNC:
            renderSyncScreen();
            break;
        case UiScreenState::SCREEN_NEW_ITEM:
            renderNewItemScreen();
            break;
    }

    g_mainViewSprite->pushSprite(0, TOP_BAR_HEIGHT);
    m_storage.unlockSpiBus();
#endif
}

void UiManager::renderUnlockScreen() {
#if !defined(UNIT_TEST_NATIVE)
    g_mainViewSprite->drawRoundRect(6, 8, DISPLAY_WIDTH - 12, MAIN_VIEW_HEIGHT - 16, 4, TFT_DARKGREEN);
    g_mainViewSprite->setTextColor(TFT_GREEN, TFT_BLACK);
    g_mainViewSprite->drawString(
        (m_screen == UiScreenState::SCREEN_PROVISION_SD)
            ? "PROVISIONAMENTO MICROSD"
            : "COFRE BLOQUEADO (AES-256-GCM)",
        14,
        16
    );

    g_mainViewSprite->setTextColor(TFT_SILVER, TFT_BLACK);
    g_mainViewSprite->drawString(m_statusBanner, 14, 34);

    // Caixa do PIN mascarado
    g_mainViewSprite->drawRect(14, 50, DISPLAY_WIDTH - 28, 22, TFT_GREEN);
    char masked[36];
    size_t stars = (m_pinLen > 32) ? 32 : m_pinLen;
    memset(masked, '*', stars);
    masked[stars] = '_';
    masked[stars + 1] = '\0';
    g_mainViewSprite->setTextColor(TFT_WHITE, TFT_BLACK);
    g_mainViewSprite->drawString(masked, 20, 57);

    g_mainViewSprite->setTextColor(TFT_DARKGREY, TFT_BLACK);
    g_mainViewSprite->drawString("[ENTER] Confirmar | PBKDF2-SHA256 100k", 14, 84);
#endif
}

void UiManager::renderVaultListScreen() {
#if !defined(UNIT_TEST_NATIVE)
    // Barra de pesquisa no topo da área principal
    g_mainViewSprite->drawRect(4, 3, DISPLAY_WIDTH - 8, 16, TFT_DARKGREEN);
    char searchLine[48];
    snprintf(searchLine, sizeof(searchLine), "> %s_", m_searchQuery);
    g_mainViewSprite->setTextColor(TFT_GREEN, TFT_BLACK);
    g_mainViewSprite->drawString(searchLine, 8, 7);

    char countLine[20];
    snprintf(countLine, sizeof(countLine), "%u/%u", m_filteredCount, m_storage.itemCount());
    g_mainViewSprite->setTextColor(TFT_SILVER, TFT_BLACK);
    g_mainViewSprite->drawRightString(countLine, DISPLAY_WIDTH - 8, 7);

    if (m_filteredCount == 0) {
        g_mainViewSprite->setTextColor(TFT_SILVER, TFT_BLACK);
        g_mainViewSprite->drawString("Nenhuma credencial. Pressione [Fn+S] p/ Sync", 8, 45);
    } else {
        const CpVaultIndexEntry* entries = m_storage.indexEntries();
        for (uint8_t row = 0; row < VISIBLE_LIST_ROWS; ++row) {
            uint16_t fPos = m_scrollOffset + row;
            if (fPos >= m_filteredCount) break;

            uint16_t rawIdx = m_filteredIndices[fPos];
            const CpVaultIndexEntry& item = entries[rawIdx];
            int16_t y = 23 + row * 19;

            bool selected = (fPos == m_selectedFilteredPos);
            if (selected) {
                g_mainViewSprite->fillRect(4, y, DISPLAY_WIDTH - 8, 17, 0x0285);
                g_mainViewSprite->setTextColor(TFT_WHITE, 0x0285);
            } else {
                g_mainViewSprite->setTextColor(TFT_SILVER, TFT_BLACK);
            }

            char rowTitle[26];
            snprintf(rowTitle, sizeof(rowTitle), "%s%.21s", selected ? "> " : "  ", item.title);
            g_mainViewSprite->drawString(rowTitle, 6, y + 4);

            if (item.item_flags & 0x01) {
                g_mainViewSprite->setTextColor(TFT_CYAN, selected ? 0x0285 : TFT_BLACK);
                g_mainViewSprite->drawRightString("2FA", DISPLAY_WIDTH - 8, y + 4);
            }
        }
    }

    // Rodapé de atalhos rápidos
    g_mainViewSprite->drawFastHLine(0, MAIN_VIEW_HEIGHT - 14, DISPLAY_WIDTH, TFT_DARKGREY);
    g_mainViewSprite->setTextColor(TFT_SILVER, TFT_BLACK);
    g_mainViewSprite->drawString("[ENT]Auto [Fn+V]TOTP [Fn+S]Sync [Fn+L]Layout", 4, MAIN_VIEW_HEIGHT - 11);
#endif
}

void UiManager::renderItemDetailScreen() {
#if !defined(UNIT_TEST_NATIVE)
    if (!m_hasActiveItem) return;

    uint16_t rawIdx = m_filteredIndices[m_selectedFilteredPos];
    const CpVaultIndexEntry& idxEntry = m_storage.indexEntries()[rawIdx];

    g_mainViewSprite->setTextColor(TFT_GREEN, TFT_BLACK);
    g_mainViewSprite->drawString(idxEntry.title, 6, 4);
    g_mainViewSprite->drawFastHLine(4, 15, DISPLAY_WIDTH - 8, TFT_DARKGREEN);

    g_mainViewSprite->setTextColor(TFT_SILVER, TFT_BLACK);
    char usrLine[42];
    snprintf(usrLine, sizeof(usrLine), "User: %.30s", m_activeItemPlain.username);
    g_mainViewSprite->drawString(usrLine, 6, 20);

    char pwdLine[42];
    if (m_revealPassword) {
        snprintf(pwdLine, sizeof(pwdLine), "Pass: %.30s", m_activeItemPlain.password);
        g_mainViewSprite->setTextColor(TFT_YELLOW, TFT_BLACK);
    } else {
        snprintf(pwdLine, sizeof(pwdLine), "Pass: ************ [Fn+R Ver]");
        g_mainViewSprite->setTextColor(TFT_SILVER, TFT_BLACK);
    }
    g_mainViewSprite->drawString(pwdLine, 6, 34);

    // Card dedicado ao TOTP RFC 6238
    g_mainViewSprite->drawRoundRect(6, 48, DISPLAY_WIDTH - 12, 46, 4, TFT_CYAN);
    g_mainViewSprite->setTextColor(TFT_CYAN, TFT_BLACK);
    g_mainViewSprite->drawString("TOTP 2FA:", 12, 54);

    g_mainViewSprite->setTextSize(2);
    g_mainViewSprite->setTextColor(TFT_WHITE, TFT_BLACK);
    g_mainViewSprite->drawString(m_currentTotpCode, 12, 68);
    g_mainViewSprite->setTextSize(1);

    char remLine[16];
    snprintf(remLine, sizeof(remLine), "%us", m_totpRemainingSec);
    g_mainViewSprite->drawRightString(remLine, DISPLAY_WIDTH - 14, 54);

    int16_t barMaxW = 90;
    int16_t barW = (m_totpRemainingSec * barMaxW) / 30;
    g_mainViewSprite->drawRect(DISPLAY_WIDTH - 104, 72, barMaxW, 8, TFT_CYAN);
    g_mainViewSprite->fillRect(DISPLAY_WIDTH - 104, 72, barW, 8, TFT_CYAN);

    g_mainViewSprite->setTextColor(TFT_SILVER, TFT_BLACK);
    g_mainViewSprite->drawString("[Fn+T]Digitar TOTP [ENT]Auto [DEL]Voltar", 6, MAIN_VIEW_HEIGHT - 12);
#endif
}

void UiManager::renderInjectingScreen() {
#if !defined(UNIT_TEST_NATIVE)
    g_mainViewSprite->drawRoundRect(10, 18, DISPLAY_WIDTH - 20, MAIN_VIEW_HEIGHT - 36, 4, TFT_GREEN);
    g_mainViewSprite->setTextColor(TFT_GREEN, TFT_BLACK);
    g_mainViewSprite->drawString("INJETANDO CREDENCIAL VIA HID...", 20, 32);
    g_mainViewSprite->setTextColor(TFT_WHITE, TFT_BLACK);
    g_mainViewSprite->drawString("Username -> TAB -> Password -> ENTER", 20, 52);
    g_mainViewSprite->setTextColor(TFT_CYAN, TFT_BLACK);
    g_mainViewSprite->drawString("Zeroize de SRAM automatico pos-envio", 20, 72);
#endif
}

void UiManager::renderSyncScreen() {
#if !defined(UNIT_TEST_NATIVE)
    g_mainViewSprite->drawRoundRect(6, 6, DISPLAY_WIDTH - 12, MAIN_VIEW_HEIGHT - 12, 4, TFT_CYAN);
    g_mainViewSprite->setTextColor(TFT_CYAN, TFT_BLACK);
    g_mainViewSprite->drawString("SINCRONIZACAO ZERO-TRUST (CLOUDFLARED)", 12, 14);

    g_mainViewSprite->setTextColor(TFT_WHITE, TFT_BLACK);
    g_mainViewSprite->drawString(m_syncDetailMsg, 12, 42);

    // Barra de progresso
    int16_t maxW = DISPLAY_WIDTH - 28;
    int16_t fillW = (m_syncProgressPct * maxW) / 100;
    g_mainViewSprite->drawRect(14, 66, maxW, 12, TFT_CYAN);
    g_mainViewSprite->fillRect(14, 66, fillW, 12, TFT_GREEN);

    g_mainViewSprite->setTextColor(TFT_SILVER, TFT_BLACK);
    g_mainViewSprite->drawString("Payload E2EE AES-256-GCM + HMAC-SHA256", 12, 88);
#endif
}

void UiManager::renderNewItemScreen() {
#if !defined(UNIT_TEST_NATIVE)
    g_mainViewSprite->setTextColor(TFT_GREEN, TFT_BLACK);
    g_mainViewSprite->drawString("NOVA CREDENCIAL OFFLINE (OUTBOX)", 6, 4);
    g_mainViewSprite->drawFastHLine(4, 15, DISPLAY_WIDTH - 8, TFT_DARKGREEN);

    g_mainViewSprite->setTextColor((m_newFieldStep == 0) ? TFT_YELLOW : TFT_SILVER, TFT_BLACK);
    char l0[48];
    snprintf(l0, sizeof(l0), "1. Servico: %s%s", m_newTitle, (m_newFieldStep == 0) ? "_" : "");
    g_mainViewSprite->drawString(l0, 6, 24);

    g_mainViewSprite->setTextColor((m_newFieldStep == 1) ? TFT_YELLOW : TFT_SILVER, TFT_BLACK);
    char l1[48];
    snprintf(l1, sizeof(l1), "2. Usuario: %s%s", m_newUsername, (m_newFieldStep == 1) ? "_" : "");
    g_mainViewSprite->drawString(l1, 6, 44);

    g_mainViewSprite->setTextColor((m_newFieldStep == 2) ? TFT_YELLOW : TFT_SILVER, TFT_BLACK);
    char l2[48];
    snprintf(l2, sizeof(l2), "3. Senha:   %.24s%s", m_newPassword, (m_newFieldStep == 2) ? "_" : "");
    g_mainViewSprite->drawString(l2, 6, 64);

    g_mainViewSprite->setTextColor(TFT_SILVER, TFT_BLACK);
    g_mainViewSprite->drawString("[ENTER] Proximo/Salvar  |  [Fn+Q] Cancelar", 6, MAIN_VIEW_HEIGHT - 12);
#endif
}

} // namespace sentinel

