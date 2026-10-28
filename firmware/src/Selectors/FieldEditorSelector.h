#ifndef FIELD_EDITOR_SELECTOR_H
#define FIELD_EDITOR_SELECTOR_H

#include <string>
#include <vector>
#include <Views/IView.h>
#include <Inputs/IInput.h>
#include <cctype>

class FieldEditorSelector {
public:
    FieldEditorSelector(IView& display, IInput& input);
    std::vector<std::string> select(const std::vector<std::string>& fieldNames, 
                                     const std::vector<std::string>& fieldValues, 
                                     const std::string& title);

private:
    IView& display;
    IInput& input;

    std::string processUserInput(const std::string& currentValue, char key);
};

#endif // FIELD_EDITOR_SELECTOR_H
