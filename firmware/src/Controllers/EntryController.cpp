#include "EntryController.h"
#include "../Services/TotpService.h"

EntryController::EntryController(IView& display,
                                 IInput& input,
                                 HorizontalSelector& horizontalSelector,
                                 VerticalSelector& verticalSelector,
                                 FieldActionSelector& fieldActionSelector,
                                 ConfirmationSelector& confirmationSelector,
                                 StringPromptSelector& stringPromptSelector,
                                 EntryService& entryService,
                                 CryptoService& cryptoService,
                                 UsbService& usbService,
                                 LedService& ledService,
                                 NvsService& nvsService,
                                 ModelTransformer& modelTransformer)
    : display(display),
      input(input),
      horizontalSelector(horizontalSelector),
      verticalSelector(verticalSelector),
      fieldActionSelector(fieldActionSelector),
      confirmationSelector(confirmationSelector),
      stringPromptSelector(stringPromptSelector),
      entryService(entryService),
      cryptoService(cryptoService),
      usbService(usbService),
      ledService(ledService),
      nvsService(nvsService),
      modelTransformer(modelTransformer) {}

ActionEnum EntryController::actionFieldSelected(Field field) {
    auto action = fieldActionSelector.select(field.getLabel(), field.getValue());
    return action;
}

Entry EntryController::handleEntrySelection() {
    auto entries = entryService.getAllEntries();
    if (entries.empty()) {
        display.topBar(entryService.getVaultName(), true, false);
        display.subMessage("Vault is empty", 1200);
        return entryService.getEmptyEntry();
    }

    // Sort alphabetically by serviceName (case-insensitive)
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        std::string sa = a.getServiceName();
        std::string sb = b.getServiceName();
        std::transform(sa.begin(), sa.end(), sa.begin(), ::tolower);
        std::transform(sb.begin(), sb.end(), sb.begin(), ::tolower);
        if (sa != sb) return sa < sb;
        return a.getUsername() < b.getUsername();
    });

    // Separate 2FA items and group by vault (notes3) in chunks of <= 100 items
    std::vector<Entry> totpEntries;
    std::vector<std::string> vaultOrder;
    std::vector<std::vector<Entry>> vaultBuckets;

    for (const auto& e : entries) {
        if (!e.getNotes2().empty()) {
            totpEntries.push_back(e);
        }
        std::string vName = e.getNotes3().empty() ? entryService.getVaultName() : e.getNotes3();
        auto it = std::find(vaultOrder.begin(), vaultOrder.end(), vName);
        if (it == vaultOrder.end()) {
            vaultOrder.push_back(vName);
            vaultBuckets.push_back({e});
        } else {
            vaultBuckets[std::distance(vaultOrder.begin(), it)].push_back(e);
        }
    }

    // If this vault is already small (<= 100 entries) and has only 1 vault bucket (e.g. 01_2FA, 02_Detran, 03_Personal_A-L, 04_Personal_M-Z),
    // show the entries directly without an extra group selection screen
    if (entries.size() <= 100 && vaultBuckets.size() <= 1) {
        auto labels = modelTransformer.toStrings(entries);
        auto selectedIndex = verticalSelector.select(entryService.getVaultName(), labels, true, true);
        if (selectedIndex == -1) {
            return entryService.getEmptyEntry();
        }
        return entries[selectedIndex];
    }

    // Build organized groups of at most 100 items each
    std::vector<std::string> groupLabels;
    std::vector<std::vector<Entry>> groupLists;

    if (!totpEntries.empty()) {
        groupLabels.push_back("* 2FA (" + std::to_string(totpEntries.size()) + ")");
        groupLists.push_back(totpEntries);
    }

    for (size_t b = 0; b < vaultOrder.size(); ++b) {
        const auto& bucket = vaultBuckets[b];
        const std::string& vName = vaultOrder[b];
        if (bucket.size() <= 100) {
            groupLabels.push_back(vName + " (" + std::to_string(bucket.size()) + ")");
            groupLists.push_back(bucket);
        } else {
            // Split into chunks of <= 100 entries (balanced halves/chunks)
            size_t numChunks = (bucket.size() + 99) / 100;
            size_t chunkSize = (bucket.size() + numChunks - 1) / numChunks;
            for (size_t c = 0; c < numChunks; ++c) {
                size_t startIdx = c * chunkSize;
                if (startIdx >= bucket.size()) break;
                size_t endIdx = std::min(bucket.size(), startIdx + chunkSize);
                std::vector<Entry> subList(bucket.begin() + startIdx, bucket.begin() + endIdx);

                char firstChar = subList.front().getServiceName().empty() ? 'A' : std::toupper(subList.front().getServiceName()[0]);
                char lastChar = subList.back().getServiceName().empty() ? 'Z' : std::toupper(subList.back().getServiceName()[0]);
                std::string rangeStr = std::string(1, firstChar) + "-" + std::string(1, lastChar);
                groupLabels.push_back(vName + " " + rangeStr + " (" + std::to_string(subList.size()) + ")");
                groupLists.push_back(std::move(subList));
            }
        }
    }

    groupLabels.push_back("All / Search (" + std::to_string(entries.size()) + ")");
    groupLists.push_back(entries);

    while (true) {
        int groupIdx = verticalSelector.select(entryService.getVaultName(), groupLabels, true, false);
        if (groupIdx < 0 || static_cast<size_t>(groupIdx) >= groupLists.size()) {
            return entryService.getEmptyEntry();
        }

        const auto& activeEntries = groupLists[groupIdx];
        auto labels = modelTransformer.toStrings(activeEntries);
        int selectedIndex = verticalSelector.select(groupLabels[groupIdx], labels, true, true);
        if (selectedIndex >= 0 && static_cast<size_t>(selectedIndex) < activeEntries.size()) {
            return activeEntries[selectedIndex];
        }
    }
}

Field EntryController::handleFieldSelection(Entry& selectedEntry) {
    auto fieldValues = modelTransformer.toStrings(selectedEntry);
    std::vector<std::string> fieldLabels = {"User", "Pass", "2FA", "Note"};
    std::vector<std::string> shortcuts = {"U", "P", "T", "N"};

    std::string totpSecret = selectedEntry.getNotes2();
    std::function<bool(std::vector<std::string>&)> totpLiveCallback = nullptr;
    if (!totpSecret.empty()) {
        totpLiveCallback = [totpSecret](std::vector<std::string>& currentOptions) -> bool {
            if (currentOptions.size() < 3) return false;
            uint8_t remSec = 0;
            std::string code = TotpService::generateCode(totpSecret, &remSec);
            std::string formatted = (code.size() == 6)
                ? (code.substr(0, 3) + " " + code.substr(3) + " (" + std::to_string(remSec) + "s)")
                : (code + " (" + std::to_string(remSec) + "s)");
            if (currentOptions[2] != formatted) {
                currentOptions[2] = formatted;
                return true;
            }
            return false;
        };
    }

    auto selectedIndex = verticalSelector.select(
        selectedEntry.getServiceName(),
        fieldValues,
        true,
        false,
        fieldLabels,
        shortcuts,
        true,
        true,
        totpLiveCallback
    );

    if (selectedIndex == -1) {
        return Field();
    }

    // For 2FA, pass the raw Base32 secret in Field::value so FieldActionSelector can live-refresh and send only the 6 digits
    if (fieldLabels[selectedIndex] == "2FA") {
        std::string secret = selectedEntry.getNotes2().empty() ? "No 2FA" : selectedEntry.getNotes2();
        return Field("2FA", secret, "T");
    }

    return Field(fieldLabels[selectedIndex], fieldValues[selectedIndex], shortcuts[selectedIndex]);
}

bool EntryController::handleEntryCreation() {
    auto entryCount = entryService.getAllEntries().size();
    auto entryLimit = globalState.getMaxSavedPasswordCount();
    if (entryCount >= entryLimit) {
        display.topBar("Passwords Limit", false, false);
        display.subMessage("Can't save more passwords", 2000);
        return false;
    }

    auto serviceName = stringPromptSelector.select("Create a new Entry", "Enter service name", "", true, false, false, 1);
    if (serviceName.empty()) {
        return false;
    }

    auto lastUsername = globalState.getLastUsedUsername();
    if (lastUsername.empty()) {
        auto entries = entryService.getAllEntries();
        if (!entries.empty()) {
            std::reverse(entries.begin(), entries.end());
            lastUsername = entries[0].getUsername();
        }
    }

    auto randomPassword = cryptoService.generateRandomString(20);
    auto username = stringPromptSelector.select("Username or Email", "Enter username", lastUsername, false, true, false, 1, true);
    display.subMessage("Loading...", 200);
    auto password = stringPromptSelector.select("Account Password", "Enter password", randomPassword, false, true, false, 1, true);
    auto totpSecret = stringPromptSelector.select("2FA Secret (Optional)", "Base32 secret (OK to skip)", "", false, true, false, 0);
    auto notes = stringPromptSelector.select("Notes (Optional)", "Enter notes (OK to skip)", "", false, true, false, 0);
    notes = notes.empty() ? "No notes" : notes;

    auto entry = Entry(serviceName, username, password, notes);
    entry.setNotes2(TotpService::extractBase32Secret(totpSecret));
    entryService.addEntry(entry);
    globalState.setLastUsedUsername(username);
    display.topBar("Password added", false, false);
    display.subMessage("Successfully created", 1000);
    return true;
}

bool EntryController::handleEntryUpdate(Entry& entry, Field& field) {
    std::string currentVal = field.getValue();
    if (field.getLabel() == "2FA" && currentVal == "No 2FA") {
        currentVal = "";
    }
    auto value = stringPromptSelector.select(field.getLabel(), "Modify the value", currentVal, true, true, false, 0);
    if (value.empty() && currentVal.empty()) {
        return false;
    }
    if (field.getLabel() == "2FA") {
        value = TotpService::extractBase32Secret(value);
    }
    if (value == field.getValue()) {
        return false;
    }

    field.setValue(value);
    entryService.updateField(entry, field);
    display.subMessage("Field updated", 500);
    if (field.getLabel() == "User") {
        globalState.setLastUsedUsername(field.getValue());
    }

    return true;
}

bool EntryController::handleEntryDeletion() {
    bool entryUpdated = false;

    while (true) {
        auto entries = entryService.getAllEntries();
        std::reverse(entries.begin(), entries.end());
        auto labels = modelTransformer.toStrings(entries);
        std::vector<std::string> delLabels(labels.size(), "Del");
        int selectedIndex = verticalSelector.select("Select to remove", labels, true, true, delLabels);
        if (selectedIndex == -1) {
            break;
        }
        delay(200);
        auto confirmation = confirmationSelector.select("Erase Password", "Delete " + labels[selectedIndex] + " ?");
        if (confirmation) {
            entryService.deleteEntry(entries[selectedIndex]);
            entryUpdated = true;
        }
    }

    if (entryUpdated) {
        display.subMessage("Saving...", 0);
    }

    return entryUpdated;
}
