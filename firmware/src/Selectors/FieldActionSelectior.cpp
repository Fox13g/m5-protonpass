#include "FieldActionSelector.h"
#include "../Services/TotpService.h"

FieldActionSelector::FieldActionSelector(IView& display, IInput& input, InactivityManager& inactivityManager)
    : display(display), input(input), inactivityManager(inactivityManager) {}

ActionEnum FieldActionSelector::select(const std::string& label, const std::string& value) {
    char key = KEY_NONE;
    inactivityManager.reset();

    bool isTotp = (label == "2FA");
    uint32_t lastRefreshMs = 0;
    std::string lastRenderedState = "";

    auto renderScreen = [&](bool force) {
        if (isTotp) {
            std::string timeStr = TotpService::getCurrentTimeWithSecondsStr();
            std::string topTitle = "2FA  " + timeStr;
            std::string shown = "No 2FA (press M)";
            if (!value.empty() && value != "No 2FA") {
                uint8_t remSec = 0;
                std::string code = TotpService::generateCode(value, &remSec);
                if (code.size() == 6) {
                    shown = code.substr(0, 3) + " " + code.substr(3) + " (" + std::to_string(remSec) + "s)";
                } else {
                    shown = code.empty() ? "Invalid 2FA" : (code + " (" + std::to_string(remSec) + "s)");
                }
            }
            std::string currentState = topTitle + "|" + shown;
            if (force || currentState != lastRenderedState) {
                lastRenderedState = currentState;
                display.topBar(topTitle, true, false);
                display.value(label, shown);
            }
        } else if (force) {
            display.topBar(label, true, false);
            display.value(label, value);
        }
    };

    renderScreen(true);
    lastRefreshMs = millis();

    while (true) {
        inactivityManager.update();

        if (inactivityManager.getVaultIsLocked()) {
            return ActionEnum::None;
        }

        if (isTotp && (millis() - lastRefreshMs >= 250)) {
            lastRefreshMs = millis();
            renderScreen(false);
        }

        key = input.handler();

        if (key != KEY_NONE) {
            inactivityManager.reset();
        }

        switch (key) {
            case KEY_ESC_CUSTOM:
            case KEY_ARROW_LEFT:
                return ActionEnum::None;
            case KEY_OK:
                return ActionEnum::SendToUsb;
            case 't':
            case 'T':
                if (isTotp) {
                    return ActionEnum::SetDateTime;
                }
                break;
            case 'm':
            case 'M':
                return ActionEnum::UpdateField;
        }
    }
}
