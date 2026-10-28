#ifndef GLOBAL_STATE_H
#define GLOBAL_STATE_H

#include <cstdint>
#include <string>

class GlobalState {
private:
    // Device Pinout
    uint8_t ledPin = 21;
    uint8_t sdCardCSPin = 12;
    uint8_t sdCardCLKPin = 40;
    uint8_t sdCardMISOPin = 39;
    uint8_t sdCardMOSIPin = 14;
    size_t fileCountLimit = 512;
    size_t fileCacheLimit = 64;

    // Char and entry limit (expanded for Proton Pass vaults with 200+ credentials)
    size_t maxInputCharCount = 24;
    size_t maxInputCharPasswordCount = 128;
    size_t maxSavedPasswords = 1000;

    // Encryption size
    size_t saltSize = 16;
    size_t checksumSize = 32;

    // Configuration NVS key names
    std::string nvsNamespace = "vault_manager";
    std::string nvsLastUsedVaultPath = "lastUsedVault";
    std::string nvsKeyboardLayout = "keyboardLayout";
    std::string nvsScreenBrightness = "screenBright";
    std::string nvsInactivityScreenTimeout = "screenOffTime";
    std::string nvsInactivityLockTimeout = "vaultLockTime";
    std::string nvsSavedEpoch = "savedEpoch";

    // User config (Default keyboard layout: Portuguese (BR) for Brazilian users)
    std::string selectedKeyboardLayout = "Portuguese (BR)";
    uint8_t selectedScreenBrightness = 140;
    std::string defaultVaultPath = "/vaults";

    // Last Vault
    std::string loadedVaultPath = "";
    std::string loadedVaultPassword = "";
    bool vaultIsLocked = false;

    // Last Entry username
    std::string lastUsedUsername = "";

    // Inactivity Timing
    uint32_t inactivityBrightnessTimeout = 12000; // 12 sec
    uint32_t inactivityScreenTimeout = 1 * 60 * 1000; // 1 min
    uint32_t inactivityLockTimeout = 10 * 60 * 1000; // 10 mins

    // Private constructor
    GlobalState() = default;

public:
    GlobalState(const GlobalState&) = delete;
    GlobalState& operator=(const GlobalState&) = delete;

    static GlobalState& getInstance() {
        static GlobalState instance;
        return instance;
    }

    uint8_t getLedPin() const { return ledPin; }
    uint8_t getSdCardCSPin() const { return sdCardCSPin; }
    uint8_t getSdCardCLKPin() const { return sdCardCLKPin; }
    uint8_t getSdCardMISOPin() const { return sdCardMISOPin; }
    uint8_t getSdCardMOSIPin() const { return sdCardMOSIPin; }
    size_t getFileCountLimit() const { return fileCountLimit; }
    size_t getFileCacheLimit() const { return fileCacheLimit; }

    void setSdCardCSPin(uint8_t pin) { sdCardCSPin = pin; }
    void setSdCardCLKPin(uint8_t pin) { sdCardCLKPin = pin; }
    void setSdCardMISOPin(uint8_t pin) { sdCardMISOPin = pin; }
    void setSdCardMOSIPin(uint8_t pin) { sdCardMOSIPin = pin; }
    void setFileCountLimit(size_t limit) { fileCountLimit = limit; }
    void setFileCacheLimit(size_t limit) { fileCacheLimit = limit; }

    size_t getMaxInputCharCount() const { return maxInputCharCount; }
    size_t getMaxInputCharPasswordCount() const { return maxInputCharPasswordCount; }
    size_t getMaxSavedPasswordCount() const { return maxSavedPasswords; }

    void setMaxInputCharCount(size_t limit) { maxInputCharCount = limit; }
    void setMaxInputCharPasswordCount(size_t limit) { maxInputCharPasswordCount = limit; }
    void setMaxSavedPasswordCount(size_t limit) { maxSavedPasswords = limit; }

    size_t getSaltSize() const { return saltSize; }
    size_t getChecksumSize() const { return checksumSize; }

    void setSaltSize(size_t size) { saltSize = size; }
    void setChecksumSize(size_t size) { checksumSize = size; }

    const std::string& getNvsNamespace() const { return nvsNamespace; }
    const std::string& getNvsLastUsedVaultPath() const { return nvsLastUsedVaultPath; }
    const std::string& getNvsKeyboardLayout() const { return nvsKeyboardLayout; }
    const std::string& getNvsScreenBrightness() const { return nvsScreenBrightness; }
    const std::string& getNvsInactivityScreenTimeout() const { return nvsInactivityScreenTimeout; }
    const std::string& getNvsInactivityLockTimeout() const { return nvsInactivityLockTimeout; }
    const std::string& getNvsSavedEpoch() const { return nvsSavedEpoch; }

    const std::string& getSelectedKeyboardLayout() const { return selectedKeyboardLayout; }
    uint8_t getSelectedScreenBrightness() const { return selectedScreenBrightness; }
    const std::string& getDefaultVaultPath() const { return defaultVaultPath; }

    void setNvsNamespace(const std::string& ns) { nvsNamespace = ns; }
    void setNvsLastUsedVaultPath(const std::string& key) { nvsLastUsedVaultPath = key; }
    void setNvsKeyboardLayout(const std::string& key) { nvsKeyboardLayout = key; }
    void setNvsScreenBrightness(const std::string& key) { nvsScreenBrightness = key; }
    void setNvsInactivityScreenTimeout(const std::string& key) { nvsInactivityScreenTimeout = key; }
    void setNvsInactivityLockTimeout(const std::string& key) { nvsInactivityLockTimeout = key; }

    void setSelectedKeyboardLayout(const std::string& key) { selectedKeyboardLayout = key; }
    void setSelectedScreenBrightness(uint8_t br) { selectedScreenBrightness = br; }
    void setDefaultVaultPath(const std::string& p) { defaultVaultPath = p; }

    const std::string& getLoadedVaultPath() const { return loadedVaultPath; }
    const std::string& getLoadedVaultPassword() const { return loadedVaultPassword; }
    bool getVaultIsLocked() const { return vaultIsLocked; }

    void setLoadedVaultPath(const std::string& path) { loadedVaultPath = path; }
    void setLoadedVaultPassword(const std::string& password) { loadedVaultPassword = password; }
    void setVaultIsLocked(bool locked) { vaultIsLocked = locked; }

    uint32_t getInactivityBrightnessTimeout() const { return inactivityBrightnessTimeout; }
    uint32_t getInactivityScreenTimeout() const { return inactivityScreenTimeout; }
    uint32_t getInactivityLockTimeout() const { return inactivityLockTimeout; }

    void setInactivityBrightnessTimeout(size_t timeout) { inactivityBrightnessTimeout = timeout; }
    void setInactivityScreenTimeout(size_t timeout) { inactivityScreenTimeout = timeout; }
    void setInactivityLockTimeout(size_t timeout) { inactivityLockTimeout = timeout; }

    const std::string& getLastUsedUsername() const { return lastUsedUsername; }
    void setLastUsedUsername(const std::string& username) { lastUsedUsername = username; }
};

#endif // GLOBAL_STATE_H
