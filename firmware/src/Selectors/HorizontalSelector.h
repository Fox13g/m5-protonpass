#ifndef HORIZONTAL_SELECTOR_H
#define HORIZONTAL_SELECTOR_H

#include <string>
#include <vector>
#include <Views/IView.h>
#include <Inputs/IInput.h>
#include <Enums/IconEnum.h>
#include <Managers/InactivityManager.h>

class HorizontalSelector {
public:
    HorizontalSelector(IView& display, IInput& input, InactivityManager& inactivityManager);
    int select(const std::string& title, const std::vector<std::string>& options, const std::string& description1="", const std::string& description2="", const std::vector<std::string>& icons={}, bool handleInactivity=true);

private:
    IView& display;
    IInput& input;
    InactivityManager& inactivityManager;
};

#endif // HORIZONTAL_SELECTOR_H
