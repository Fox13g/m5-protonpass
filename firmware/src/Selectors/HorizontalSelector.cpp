#include "HorizontalSelector.h"

HorizontalSelector::HorizontalSelector(IView& display, IInput& input , InactivityManager& inactivityManager)
    : display(display), input(input), inactivityManager(inactivityManager) {}

int HorizontalSelector::select(
    const std::string& title, 
    const std::vector<std::string>& options, 
    const std::string& description1, 
    const std::string& description2,
    const std::vector<std::string>& icons,
    bool handleInactivity) {

    int currentIndex = 0;
    int lastIndex = -1;
    inactivityManager.reset();

    display.topBar(title, false, false);

    while (true) {
        if (handleInactivity) {
            inactivityManager.update();
            if (inactivityManager.getVaultIsLocked()) {
                return -1;
            }
        }

        // Display the current options horizontally with the selection
        if (lastIndex != currentIndex) {
            display.horizontalSelection(options, currentIndex, description1, description2, icons);
            lastIndex = currentIndex;
        }

        // Capture user input
        char key = input.handler();

        if (handleInactivity && key != KEY_NONE) {
            inactivityManager.reset();
        }

        switch (key) {
            case KEY_ARROW_LEFT: // Move left
                currentIndex = (currentIndex > 0) ? currentIndex - 1 : options.size() - 1;
                break;
            case KEY_ARROW_RIGHT: // Move right
                currentIndex = (currentIndex < options.size() - 1) ? currentIndex + 1 : 0;
                break;
            case KEY_OK: // Select
                return currentIndex;
            case KEY_ESC_CUSTOM:
                if (handleInactivity) {
                    return -1;
                }
            default:
                break; // Ignore other inputs
        }
    }
}
