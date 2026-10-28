#include "StringPromptSelector.h"

StringPromptSelector::StringPromptSelector(IView& display, IInput& input)
    : display(display), input(input) {}

std::string StringPromptSelector::select(
    const std::string& title, 
    const std::string& label, 
    const std::string& value, 
    bool backButton, 
    bool maxInput, 
    bool isalnumOnly, 
    size_t minLength, 
    bool autoDelete
) {
    std::string output = value;
    char key = KEY_NONE;
    size_t limit = getMaxInputLimit(maxInput);

    display.topBar(title, false, false);
    display.stringPrompt(label, output, backButton, minLength);

    while (true) {
        key = input.handler();

        if (key == KEY_OK && output.length() >= minLength) {
            break; // confirm if minLength
        }
        if ((key == KEY_ARROW_LEFT || key == KEY_ESC_CUSTOM) && backButton) {
            return ""; // return
        }
        if (key == KEY_DEL && !output.empty()) {
            if (autoDelete) {
                output = "";
                autoDelete = false;
            } else {
                output.pop_back();
            }
        }
        else if ((isalnumOnly ? std::isalnum(key) : std::isprint(key)) && output.size() < limit) {
            output += key;
            autoDelete = false;
        }

        if (key != KEY_NONE) {
            display.stringPrompt(label, output, backButton, minLength);  
        }
        delay(5);
    }

    return output;
}

size_t StringPromptSelector::getMaxInputLimit(bool password) const {
    return password ? globalState.getMaxInputCharPasswordCount() : globalState.getMaxInputCharCount();
}
