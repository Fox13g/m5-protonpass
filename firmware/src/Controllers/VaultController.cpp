#include "VaultController.h"

VaultController::VaultController(IView& display, 
                                 IInput& input, 
                                 HorizontalSelector& horizontalSelector, 
                                 VerticalSelector& verticalSelector,
                                 ConfirmationSelector& confirmationSelector,
                                 StringPromptSelector& stringPromptSelector,
                                 SdService& sdService, 
                                 NvsService& nvsService, 
                                 CategoryService& categoryService, 
                                 EntryService& entryService,
                                 CryptoService& cryptoService,
                                 JsonTransformer& jsonTransformer,
                                 ModelTransformer& modelTransformer)
    : display(display), 
      input(input), 
      horizontalSelector(horizontalSelector), 
      verticalSelector(verticalSelector),
      confirmationSelector(confirmationSelector),
      stringPromptSelector(stringPromptSelector),
      sdService(sdService), 
      nvsService(nvsService), 
      categoryService(categoryService), 
      entryService(entryService),
      cryptoService(cryptoService),
      jsonTransformer(jsonTransformer),
      modelTransformer(modelTransformer) {}

ActionEnum VaultController::actionNoVault() {
    std::vector<ActionEnum> availableActions = {
        ActionEnum::OpenVault,
        ActionEnum::CreateVault,
        ActionEnum::SetDateTime,
        ActionEnum::UpdateSettings
    };
    std::vector<IconEnum> actionIcons = {
        IconEnum::LoadVault,
        IconEnum::CreateVault,
        IconEnum::Clock,
        IconEnum::Settings
    };
    auto labels = ActionEnumMapper::getActionNames(availableActions);
    auto iconNames = IconEnumMapper::getIconNames(actionIcons);
    auto selectedIndex = horizontalSelector.select("", labels, "", "", iconNames);

    if (selectedIndex != -1) {
        return availableActions[selectedIndex];
    }

    return ActionEnum::None;
}

ActionEnum VaultController::actionVaultSelected() {
    std::vector<ActionEnum> availableActions = {
        ActionEnum::SelectEntry,
        ActionEnum::CreateEntry,
        ActionEnum::DeleteEntry,
        ActionEnum::SetDateTime
    };
    std::vector<IconEnum> actionIcons = {
        IconEnum::SelectEntry,
        IconEnum::AddEntry,
        IconEnum::DeleteEntry,
        IconEnum::Clock
    };
    auto labels = ActionEnumMapper::getActionNames(availableActions);
    auto iconNames = IconEnumMapper::getIconNames(actionIcons);

    auto confirmation = false;
    while (!confirmation) {
        auto selectedIndex = horizontalSelector.select("", labels, "", "", iconNames);
        if (selectedIndex != -1) {
            return availableActions[selectedIndex];
        }

        if (globalState.getVaultIsLocked()) {
            confirmation = true;
        } else {
            confirmation = confirmationSelector.select("Back to the Menu", "Close the vault ?");
        }
    }

    return ActionEnum::None;
}

bool VaultController::handleVaultCreation() {
    display.topBar("Create a new vault", false, false);
    display.subMessage("Loading...", 300);
    if (!sdService.begin()) {
        display.subMessage("SD card not found", 2000);
        return false;
    }

    auto vaultName = stringPromptSelector.select("Create a new vault", "Enter the vault name", "", true, false, true, 1);
    if (vaultName.empty()) {
        return false;
    }

    auto vaultPath = globalState.getDefaultVaultPath() + "/" + vaultName + ".vault";
    if (sdService.isFile(vaultPath)) {
        auto confirmation = confirmationSelector.select("Vault already exists", "Erase the vault ?");
        if (!confirmation) {
            return false;
        }
    }

    // Allow 4+ character passwords/PINs (e.g. "9868") and back button
    std::string pass1 = "";
    std::string pass2 = "";
    do {
        pass1 = stringPromptSelector.select("Vault Password", "Enter master password", "", true, true, false, 4);
        if (pass1.empty()) {
            return false;
        }
        pass2 = stringPromptSelector.select("Repeat Password", "Repeat master password", "", true, true, false, 4);
        if (pass2.empty()) {
            return false;
        }
        if (pass1 != pass2) {
            display.subMessage("Do not match", 1500);
        }
    } while (pass1 != pass2);

    display.subMessage("Creating vault...", 0);
    auto salt = cryptoService.generateSalt(globalState.getSaltSize());
    auto jsonEmpty = jsonTransformer.emptyJsonStructure();
    auto checksum = cryptoService.generateChecksum(jsonEmpty, globalState.getChecksumSize());
    auto jsonEncrypted = cryptoService.encryptWithPassphrase(jsonEmpty, pass1, salt);

    VaultFile vault = VaultFile(vaultPath, {});
    vault.setSalt(salt);
    vault.setChecksum(checksum);
    vault.setEncryptedData(jsonEncrypted);
    std::vector<Entry> emptyEntries;
    entryService.setEntries(emptyEntries);
    entryService.setContainerName(vaultName);

    sdService.begin();
    sdService.ensureDirectory(globalState.getDefaultVaultPath());
    auto confirmation = sdService.writeBinaryFile(vaultPath, vault.getData());
    sdService.close();
    if (!confirmation) {
        display.subMessage("Failed to write SD", 2000);
        return false;
    }
    sdService.removeCachedPath(globalState.getDefaultVaultPath());

    globalState.setLoadedVaultPassword(pass1);
    globalState.setLoadedVaultPath(vaultPath);
    return true;
}

bool VaultController::handleVaultSave() {
    auto loadedVaultPath = globalState.getLoadedVaultPath();
    auto loadedVaultPassword = globalState.getLoadedVaultPassword();
    if (loadedVaultPath.empty() || loadedVaultPassword.empty()) {
        display.subMessage("No vault loaded", 2000);
        return false;
    }

    sdService.begin();
    auto vaultData = sdService.readBinaryFile(loadedVaultPath);
    sdService.close();
    if (vaultData.empty()) {
        display.subMessage("Failed to load vault", 2000);
        return false;
    }

    VaultFile vault(loadedVaultPath, vaultData);

    auto salt = vault.getSalt();
    if (salt.empty()) {
        display.subMessage("Invalid vault data", 2000);
        return false;
    }

    auto entries = entryService.getAllEntries();
    auto categories = categoryService.getAllCategories();

    auto jsonData = jsonTransformer.mergeEntriesAndCategoriesToJson(entries, categories);
    auto checksum = cryptoService.generateChecksum(jsonData, globalState.getChecksumSize());
    auto encryptedData = cryptoService.encryptWithPassphrase(jsonData, loadedVaultPassword, salt);

    vault.setChecksum(checksum);
    vault.setEncryptedData(encryptedData);

    sdService.begin();
    auto confirmation = sdService.writeBinaryFile(loadedVaultPath, vault.getData());
    sdService.close();

    if (!confirmation) {
        display.subMessage("Failed to save vault", 2000);
        return false;
    }

    return true;
}

bool VaultController::handleVaultLoading() {
    // Open SD card vault list directly without extra unnecessary click
    return loadSdVault();
}

bool VaultController::loadSdVault() {
    display.topBar("Load Vault (SD)", false, false);
    display.subMessage("Loading...", 200);
    if (!sdService.begin()) {
        display.subMessage("SD card not found", 2000);
        return false;
    }

    std::string currentPath = nvsService.getString(globalState.getNvsLastUsedVaultPath());
    currentPath = sdService.isDirectory(currentPath) ? currentPath : "";
    if (currentPath.empty()) {
        auto defaultPath = globalState.getDefaultVaultPath();
        currentPath = sdService.ensureDirectory(defaultPath) ? defaultPath : "/";
    }

    std::vector<std::string> elementNames;
    while (true) {
        if (sdService.isFile(currentPath)) {
            if (sdService.validateVaultFile(currentPath)) {
                if (loadDataFromEncryptedFile(currentPath)) {
                    display.subMessage("Loaded successfully", 800);
                    return true;
                } else {
                    display.subMessage("Invalid Password", 1500);
                }
            } else {
                display.subMessage("Not a .vault file", 1500);
            }

            currentPath = sdService.getParentDirectory(currentPath);
            currentPath = currentPath.empty() ? "/" : currentPath;
        }

        display.subMessage("Loading...", 0);
        elementNames = sdService.getCachedDirectoryElements(currentPath);
        if (elementNames.empty()) {
            display.subMessage("No vaults found", 1500);
            if (currentPath == "/") {
                sdService.close();
                return false;
            }
            currentPath = sdService.getParentDirectory(currentPath);
            currentPath = currentPath.empty() ? "/" : currentPath;
            continue;
        }

        int selectedIndex = verticalSelector.select(currentPath, elementNames, true, true, {}, {}, false, false);
        if (selectedIndex < 0 || static_cast<size_t>(selectedIndex) >= elementNames.size()) {
            if (currentPath == "/" || currentPath == globalState.getDefaultVaultPath()) {
                sdService.close();
                return false;
            }
            currentPath = sdService.getParentDirectory(currentPath);
            currentPath = currentPath.empty() ? "/" : currentPath;
            continue;
        }

        if (!currentPath.empty() && currentPath.back() != '/') {
            currentPath += "/";
        }
        currentPath += elementNames[selectedIndex];
    }
}

bool VaultController::loadDataFromEncryptedFile(std::string path) {
    auto vaultBinary = sdService.readBinaryFile(path);
    if (vaultBinary.size() <= globalState.getSaltSize() + globalState.getChecksumSize()) {
        return false;
    }
    VaultFile vaultFile = VaultFile(path, vaultBinary);
    // Allow 1+ char password/PIN (e.g. "9868") and Back button
    auto password = stringPromptSelector.select("Open encrypted vault", "Enter master password", "", true, true, false, 1);
    if (password.empty()) {
        return false;
    }

    display.subMessage("Unlocking...", 0);
    auto salt = vaultFile.getSalt();
    auto savedChecksum = vaultFile.getChecksum();
    auto encryptedData = vaultFile.getEncryptedData();

    auto decryptedData = cryptoService.decryptWithPassphrase(encryptedData, password, salt);
    if (decryptedData.empty()) {
        return false;
    }

    auto dataChecksum = cryptoService.generateChecksum(decryptedData, globalState.getChecksumSize());
    if (savedChecksum != dataChecksum) {
        return false;
    }

    auto vaultName = sdService.getFileName(path);
    auto entries = jsonTransformer.fromJsonToEntries(decryptedData);
    auto categories = jsonTransformer.fromJsonToCategories(decryptedData);
    entryService.setEntries(entries);
    entryService.setContainerName(vaultName);
    categoryService.setCategories(categories);
    globalState.setLoadedVaultPassword(password);
    globalState.setLoadedVaultPath(path);
    auto parentDir = sdService.getParentDirectory(path);
    nvsService.saveString(globalState.getNvsLastUsedVaultPath(), parentDir);

    return true;
}
