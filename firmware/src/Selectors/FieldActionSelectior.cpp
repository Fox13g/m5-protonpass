#include "FieldActionSelector.h"
#include "../Services/TotpService.h"

FieldActionSelector::FieldActionSelector(IView& display, IInput& input, InactivityManager& inactivityManager)
    : display(display), input(input), inactivityManager(inactivityManager) {}

ActionEnum FieldActionSelector::select(const std::string& label, const std::string& value) {
    char key = KEY_NONE;
    inactivityManager.reset();

    bool isTotp = (label == "2FA");
    uint32_t lastRefreshMs = 0;

    auto renderScreen = [&]() {
        if (isTotp) {
            std::string topTitle = "2FA  " + TotpService::getCurrentTimeShortStr();
            display.topBar(topTitle, true, false);
            if (!value.empty() && value != "No 2FA") {
                uint8_t remSec = 0;
                std::string code = TotpService::generateCode(value, &remSec);
                std::string shown = code.empty() ? "Invalid 2FA" : (code + " (" + std::to_string(remSec) + "s)");
                display.value(label, shown);
            } else {
                display.value(label, "No 2FA (press M)");
            }
        } else {
            display.topBar(label, true, false);
            display.value(label, value);
        }
    };

    renderScreen();
    lastRefreshMs = millis();

    while (true) {
        inactivityManager.update();

        if (inactivityManager.getVaultIsLocked()) {
            return ActionEnum::None;
        }

        if (isTotp && (millis() - lastRefreshMs >= 1000)) {
            lastRefreshMs = millis();
            renderScreen();
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
