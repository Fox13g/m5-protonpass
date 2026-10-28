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
    std::reverse(entries.begin(), entries.end()); // last created in first
    auto labels = modelTransformer.toStrings(entries);
    auto selectedIndex = verticalSelector.select(entryService.getVaultName(), labels, true, true);

    if (selectedIndex == -1) {
        return entryService.getEmptyEntry();
    }

    return entries[selectedIndex];
}

Field EntryController::handleFieldSelection(Entry& selectedEntry) {
    auto fieldValues = modelTransformer.toStrings(selectedEntry);
    std::vector<std::string> fieldLabels = {"User", "Pass", "2FA", "Note"};
    std::vector<std::string> shortcuts = {"U", "P", "T", "N"};
    auto selectedIndex = verticalSelector.select(selectedEntry.getServiceName(), fieldValues, true, false, fieldLabels, shortcuts, true);

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
