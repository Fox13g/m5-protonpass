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
    while (true) {
        std::vector<std::string> modeOptions = {
            "Sync Wi-Fi (NTP)",
            "Manual Clock (Arrows)"
        };
        std::string menuTitle = "Clock " + TotpService::getCurrentTimeWithSecondsStr();
        int modeIndex = verticalSelector.select(
            menuTitle,
            modeOptions,
            true,
            false,
            {},
            {},
            false,
            true,
            [&](std::vector<std::string>&) -> bool {
                std::string nowTitle = "Clock " + TotpService::getCurrentTimeWithSecondsStr();
                if (nowTitle != menuTitle) {
                    menuTitle = nowTitle;
                    display.topBar(menuTitle, true, false);
                }
                return false;
            }
        );

        if (modeIndex == -1) {
            return false;
        }

        if (modeIndex == 0) {
            // Option 1: Sync Wi-Fi (NTP) with automatic 2.4GHz network scan
            display.topBar("Wi-Fi NTP Sync", true, false);
            display.subMessage("Scanning Wi-Fi...", 0);
            auto ssids = TotpService::scanWifiNetworks();
            ssids.push_back("[Type SSID manually]");

            int ssidIdx = verticalSelector.select("Select 2.4GHz Wi-Fi", ssids, true, false);
            if (ssidIdx < 0 || static_cast<size_t>(ssidIdx) >= ssids.size()) {
                continue;
            }

            std::string selectedSsid = ssids[ssidIdx];
            std::string savedSsid = nvsService.getString("wifiSsid");
            std::string savedPass = nvsService.getString("wifiPass");

            if (selectedSsid == "[Type SSID manually]") {
                selectedSsid = stringPromptSelector.select(
                    "Wi-Fi SSID (2.4GHz)",
                    "Enter network name",
                    savedSsid,
                    true,
                    true,
                    false,
                    1,
                    false
                );
                if (selectedSsid.empty()) {
                    continue;
                }
            }

            std::string initialPass = (selectedSsid == savedSsid) ? savedPass : "";
            std::string wifiPass = stringPromptSelector.select(
                selectedSsid,
                "Wi-Fi Password (OK=Connect)",
                initialPass,
                true,
                true,
                false,
                0,
                !initialPass.empty()
            );

            display.topBar("Wi-Fi NTP Sync", false, false);
            display.subMessage("Connecting & Syncing...", 0);

            uint64_t syncedEpoch = 0;
            std::string errMsg;
            if (TotpService::syncTimeViaWifiNtp(selectedSsid, wifiPass, &syncedEpoch, &errMsg)) {
                nvsService.saveString("wifiSsid", selectedSsid);
                nvsService.saveString("wifiPass", wifiPass);
                nvsService.saveString(globalState.getNvsSavedEpoch(), std::to_string(syncedEpoch));
                display.topBar("NTP Synced! (BRT)", false, false);
                display.subMessage(TotpService::getCurrentDateTimeStr(), 2200);
                return true;
            } else {
                display.topBar("Wi-Fi Sync Error", false, false);
                display.subMessage(errMsg.empty() ? "Sync failed" : errMsg, 2000);
                continue;
            }
        } else if (modeIndex == 1) {
            // Option 2: Visual Manual Clock with Arrows (, / to switch field, ; . to + / -, OK to save)
            int day = 28, month = 10, year = 2026, hour = 12, minute = 0, second = 0;
            TotpService::getLocalDateTimeComponents(day, month, year, hour, minute, second);

            // Start on Hour (field 3) so user can immediately adjust HH:MM:SS with arrows
            int selectedField = 3;
            bool redraw = true;

            while (true) {
                if (redraw) {
                    char fullBuf[32];
                    std::snprintf(
                        fullBuf,
                        sizeof(fullBuf),
                        "%02d/%02d/%04d %02d:%02d:%02d",
                        day, month, year, hour, minute, second
                    );

                    char f0[20], f1[20], f2[20], f3[20], f4[20], f5[20];
                    std::snprintf(f0, sizeof(f0), "Day: %02d", day);
                    std::snprintf(f1, sizeof(f1), "Month: %02d", month);
                    std::snprintf(f2, sizeof(f2), "Year: %04d", year);
                    std::snprintf(f3, sizeof(f3), "Hour: %02d", hour);
                    std::snprintf(f4, sizeof(f4), "Min: %02d", minute);
                    std::snprintf(f5, sizeof(f5), "Sec: %02d", second);

                    std::vector<std::string> fieldLabels = {f0, f1, f2, f3, f4, f5};
                    display.topBar("Set Clock (Arrows)", true, false);
                    display.horizontalSelection(
                        fieldLabels,
                        selectedField,
                        std::string(fullBuf),
                        ",/ Field  ;. +-  OK"
                    );
                    redraw = false;
                }

                char key = input.handler();
                if (key == KEY_NONE) {
                    continue;
                }

                if (key == KEY_ESC_CUSTOM) {
                    break;
                } else if (key == KEY_ARROW_LEFT) {
                    selectedField = (selectedField > 0) ? (selectedField - 1) : 5;
                    redraw = true;
                } else if (key == KEY_ARROW_RIGHT) {
                    selectedField = (selectedField < 5) ? (selectedField + 1) : 0;
                    redraw = true;
                } else if (key == KEY_ARROW_UP || key == KEY_ARROW_DOWN) {
                    int delta = (key == KEY_ARROW_UP) ? 1 : -1;
                    switch (selectedField) {
                        case 0: // Day 1..31
                            day += delta;
                            if (day > 31) day = 1;
                            if (day < 1) day = 31;
                            break;
                        case 1: // Month 1..12
                            month += delta;
                            if (month > 12) month = 1;
                            if (month < 1) month = 12;
                            break;
                        case 2: // Year 2025..2035
                            year += delta;
                            if (year > 2035) year = 2025;
                            if (year < 2025) year = 2035;
                            break;
                        case 3: // Hour 0..23
                            hour += delta;
                            if (hour > 23) hour = 0;
                            if (hour < 0) hour = 23;
                            break;
                        case 4: // Minute 0..59
                            minute += delta;
                            if (minute > 59) minute = 0;
                            if (minute < 0) minute = 59;
                            break;
                        case 5: // Second 0..59
                            second += delta;
                            if (second > 59) second = 0;
                            if (second < 0) second = 59;
                            break;
                    }
                    redraw = true;
                } else if (key == KEY_OK) {
                    uint64_t newEpoch = 0;
                    if (TotpService::setLocalDateTimeComponents(day, month, year, hour, minute, second, &newEpoch)) {
                        nvsService.saveString(globalState.getNvsSavedEpoch(), std::to_string(newEpoch));
                        display.topBar("Date & Time", false, false);
                        display.subMessage("Saved! " + TotpService::getCurrentTimeWithSecondsStr(), 1200);
                        return true;
                    }
                }
            }
        }
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
