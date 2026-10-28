#include "UtilityController.h"
#include "../Services/TotpService.h"

UtilityController::UtilityController(IView& display,
                                     IInput& input,
                                     HorizontalSelector& horizontalSelector,
                                     VerticalSelector& verticalSelector,
                                     FieldEditorSelector& fieldEditorSelector,
                                     StringPromptSelector& stringPromptSelector,
                                     ConfirmationSelector& confirmationSelector,
                                     UsbService& usbService,
                                     LedService& ledService,
                                     NvsService& nvsService,
                                     SdService& sdService,
                                     TimeTransformer& timeTransformer)
    : display(display),
      input(input),
      horizontalSelector(horizontalSelector),
      verticalSelector(verticalSelector),
      fieldEditorSelector(fieldEditorSelector),
      stringPromptSelector(stringPromptSelector),
      confirmationSelector(confirmationSelector),
      usbService(usbService),
      ledService(ledService),
      nvsService(nvsService),
      sdService(sdService),
      timeTransformer(timeTransformer) {}

void UtilityController::handleWelcome() {
    auto brightness = globalState.getSelectedScreenBrightness();
    display.welcome(brightness);
    input.waitPress();
    display.setBrightness(brightness);
}

bool UtilityController::handleUsbTyping(std::string sendString) {
    ledService.showLed();
    display.subMessage("Send to USB", 0);
    usbService.sendString(sendString);
    ledService.clearLed();
    return true;
}

bool UtilityController::handleKeyboardInitialization() {
    auto selectedKeyboardLayout = globalState.getSelectedKeyboardLayout();
    auto nvsKeyboardLayoutField = globalState.getNvsKeyboardLayout();

    if (selectedKeyboardLayout.empty()) {
        selectedKeyboardLayout = nvsService.getString(nvsKeyboardLayoutField);
    }

    if (selectedKeyboardLayout.empty()) {
        selectedKeyboardLayout = "Portuguese (BR)";
        nvsService.saveString(nvsKeyboardLayoutField, selectedKeyboardLayout);
        globalState.setSelectedKeyboardLayout(selectedKeyboardLayout);
    }

    const uint8_t* finalLayout = KeyboardLayoutMapper::toLayout(selectedKeyboardLayout);
    usbService.setLayout(finalLayout);
    usbService.begin();
    return true;
}

void UtilityController::handleLoadNvs() {
    // Restore Date & Time epoch from NVS for 2FA (TOTP)
    std::string savedEpochStr = nvsService.getString(globalState.getNvsSavedEpoch());
    uint64_t savedEpoch = 0;
    if (!savedEpochStr.empty()) {
        savedEpoch = std::strtoull(savedEpochStr.c_str(), nullptr, 10);
    }
    TotpService::initClock(savedEpoch);

    // Keyboard layout
    std::string savedLayout = nvsService.getString(globalState.getNvsKeyboardLayout());
    if (!savedLayout.empty()) {
        globalState.setSelectedKeyboardLayout(savedLayout);
    }

    // Brightness
    std::string savedBrightness = nvsService.getString(globalState.getNvsScreenBrightness());
    if (!savedBrightness.empty()) {
        uint8_t brightness = std::stoi(savedBrightness);
        globalState.setSelectedScreenBrightness(brightness);
        display.setBrightness(brightness);
    }

    // Screen off
    std::string savedScreenTimeout = nvsService.getString(globalState.getNvsInactivityScreenTimeout());
    if (!savedScreenTimeout.empty()) {
        uint32_t screenTimeout = timeTransformer.toMilliseconds(savedScreenTimeout);
        if (screenTimeout > 0) {
            globalState.setInactivityScreenTimeout(screenTimeout);
        }
    }

    // Vault lock
    std::string savedLockTimeout = nvsService.getString(globalState.getNvsInactivityLockTimeout());
    if (!savedLockTimeout.empty()) {
        uint32_t lockTimeout = timeTransformer.toMilliseconds(savedLockTimeout);
        if (lockTimeout > 0) {
            globalState.setInactivityLockTimeout(lockTimeout);
        }
    }
}

bool UtilityController::handleDateTimeSetting() {
    std::string currentDt = TotpService::getCurrentDateTimeStr();
    // Pre-filled with "DD/MM/YYYY HH:MM" and autoDelete=false so user can just Backspace the last 4-5 digits (HH:MM) or type 4 digits (HHMM)
    std::string newDt = stringPromptSelector.select(
        "Date & Time (2FA)",
        "DD/MM/YYYY HH:MM ou HHMM",
        currentDt,
        true,
        false,
        false,
        4,
        false
    );

    if (newDt.empty()) {
        return false;
    }

    uint64_t newEpoch = 0;
    if (TotpService::setDateTimeFromStr(newDt, &newEpoch)) {
        nvsService.saveString(globalState.getNvsSavedEpoch(), std::to_string(newEpoch));
        display.topBar("Date & Time", false, false);
        display.subMessage("Clock: " + TotpService::getCurrentDateTimeStr(), 1200);
        return true;
    } else {
        display.topBar("Date & Time", false, false);
        display.subMessage("Invalid format!", 1500);
        return false;
    }
}

bool UtilityController::handleGeneralSettings() {
    std::vector<std::string> timeLabels = timeTransformer.getAllTimeLabels();
    std::vector<uint32_t> timeValues = timeTransformer.getAllTimeValues();
    std::vector<std::string> brightnessValues = {"20", "60", "100", "140", "160", "200", "240"};
    std::vector<std::string> settingLabels = {"Date/Time ", " Keyboard ", "Brightness", "Screen off", "Vault lock"};

    auto layouts = KeyboardLayoutMapper::getAllLayoutNames();
    auto selectedLayout = globalState.getSelectedKeyboardLayout().empty() ? "Portuguese (BR)" : globalState.getSelectedKeyboardLayout();
    auto selectedScreenOffTime = timeTransformer.toLabel(globalState.getInactivityScreenTimeout());
    auto selectedLockCloseTime = timeTransformer.toLabel(globalState.getInactivityLockTimeout());
    std::vector<std::string> settings = {
        TotpService::getCurrentTimeShortStr(),
        selectedLayout,
        std::to_string(globalState.getSelectedScreenBrightness()),
        selectedScreenOffTime,
        selectedLockCloseTime + " "
    };

    while (true) {
        settings[0] = TotpService::getCurrentTimeShortStr();
        auto verticalIndex = verticalSelector.select("Settings", settings, true, false, settingLabels, {});
        int selectedIndex;
        if (verticalIndex == -1) {
            return false;
        }

        auto selectedSetting = settingLabels[verticalIndex];

        if (selectedSetting == "Date/Time ") {
            handleDateTimeSetting();
            settings[0] = TotpService::getCurrentTimeShortStr();

        } else if (selectedSetting == " Keyboard ") {
            selectedIndex = horizontalSelector.select("Choose Keyboard", layouts, "Region Layout", "Press OK to select", {}, false);
            if (selectedIndex != -1) {
                globalState.setSelectedKeyboardLayout(layouts[selectedIndex]);
                nvsService.saveString(globalState.getNvsKeyboardLayout(), layouts[selectedIndex]);
                settings[verticalIndex] = layouts[selectedIndex];
            }

        } else if (selectedSetting == "Brightness") {
            selectedIndex = horizontalSelector.select("Screen Brightness", brightnessValues, "Choose brightness", "Press OK to select", {}, false);
            if (selectedIndex != -1) {
                uint8_t brightness = std::stoi(brightnessValues[selectedIndex]);
                globalState.setSelectedScreenBrightness(brightness);
                nvsService.saveString(globalState.getNvsScreenBrightness(), brightnessValues[selectedIndex]);
                display.setBrightness(brightness);
                settings[verticalIndex] = brightnessValues[selectedIndex];
            }

        } else if (selectedSetting == "Screen off") {
            selectedIndex = horizontalSelector.select("Screen Off", timeLabels, "Turn off inactivity", "Press OK to select", {}, false);
            if (selectedIndex != -1) {
                globalState.setInactivityScreenTimeout(timeValues[selectedIndex]);
                nvsService.saveString(globalState.getNvsInactivityScreenTimeout(), timeLabels[selectedIndex]);
                settings[verticalIndex] = timeLabels[selectedIndex] + "  ";
            }

        } else if (selectedSetting == "Vault lock") {
            selectedIndex = horizontalSelector.select("Vault Lock", timeLabels, "Lock vault inactivity", "Press OK to select", {}, false);
            if (selectedIndex != -1) {
                globalState.setInactivityLockTimeout(timeValues[selectedIndex]);
                nvsService.saveString(globalState.getNvsInactivityLockTimeout(), timeLabels[selectedIndex]);
                settings[verticalIndex] = timeLabels[selectedIndex] + " ";
            }
        }
    }
}

void UtilityController::handleInactivity() {
    ledService.showLed();
    input.waitPress();
    ledService.clearLed();
    display.setBrightness(globalState.getSelectedScreenBrightness());
    if (!globalState.getLoadedVaultPath().empty()) {
        display.topBar("Inactivity", false, false);
        display.subMessage("Vault has been locked", 2000);
        globalState.setLoadedVaultPath("");
        globalState.setLoadedVaultPassword("");
        globalState.setVaultIsLocked(false);
    }
}
