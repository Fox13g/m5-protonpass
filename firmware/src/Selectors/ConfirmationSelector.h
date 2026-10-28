#ifndef CONFIRMATION_SELECTOR_H
#define CONFIRMATION_SELECTOR_H

#include <string>
#include <Inputs/IInput.h>
#include <Views/IView.h>
#include <Arduino.h>

class ConfirmationSelector {
public:
    ConfirmationSelector(IView& display, IInput& input);
    bool select(const std::string& title, const std::string& description);

private:
    IView& display;
    IInput& input;
};

#endif // CONFIRMATION_SELECTOR_H
