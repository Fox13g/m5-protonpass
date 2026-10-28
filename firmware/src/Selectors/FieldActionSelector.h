#ifndef FIELDACTIONSELECTOR_H
#define FIELDACTIONSELECTOR_H

#include <string>
#include <cctype>
#include <Inputs/IInput.h>
#include <Views/IView.h>
#include <Enums/ActionEnum.h>
#include <Arduino.h>
#include <Managers/InactivityManager.h>

class FieldActionSelector {
public:
    FieldActionSelector(IView& display, IInput& input, InactivityManager& inactivityManager);

    ActionEnum select(const std::string& label, const std::string& value);

private:
    IView& display;
    IInput& input;
    InactivityManager& inactivityManager;
};


#endif
