#ifndef STRING_PROMPT_SELECTOR_H
#define STRING_PROMPT_SELECTOR_H

#include <Arduino.h>
#include <cctype>
#include <string>
#include <Inputs/IInput.h>
#include <Views/IView.h>
#include <States/GlobalState.h>


class StringPromptSelector {
public:
    StringPromptSelector(IView& display, IInput& input);

    std::string select(const std::string& title, const std::string& label, const std::string& value = "", bool backButton = true, bool maxInput = false, bool isalnumOnly=false, size_t minLength=3, bool autoDelete=false);

private:
    IView& display;
    IInput& input;
    GlobalState& globalState = GlobalState::getInstance();
    size_t getMaxInputLimit(bool password) const;
};

#endif // STRING_PROMPT_SELECTOR_H
