#include "FieldEditorSelector.h"

FieldEditorSelector::FieldEditorSelector(IView& display, IInput& input)
    : display(display), input(input) {}

std::vector<std::string> FieldEditorSelector::select(const std::vector<std::string>& fieldNames, 
                                                     const std::vector<std::string>& fieldValues, 
                                                     const std::string& title) {

    int currentIndex = 0;
    std::vector<std::string> editableFields = fieldValues;

    display.topBar(title.empty() ? "Edit Fields:" : title, false, false);

    while (true) {
        std::vector<std::string> displayLines;
        for (size_t i = 0; i < editableFields.size(); ++i) {
            std::string line = (i == currentIndex ? "> " : "  ");
            line += fieldNames[i] + ": " + editableFields[i];
            displayLines.push_back(line);
        }
        display.verticalSelection(displayLines, currentIndex);

        char key = input.handler();

        switch (key) {
            case KEY_ARROW_UP:
                currentIndex = (currentIndex > 0) ? currentIndex - 1 : editableFields.size() - 1;
                break;

            case KEY_ARROW_DOWN:
                currentIndex = (currentIndex < editableFields.size() - 1) ? currentIndex + 1 : 0;
                break;

            case KEY_OK:
            case KEY_RETURN_CUSTOM:
                return editableFields;

            case KEY_DEL:
                editableFields[currentIndex] = processUserInput(editableFields[currentIndex], key);
                break;

            default: // add char input
                if (std::isalnum(key) || std::ispunct(key)) {
                    editableFields[currentIndex] = processUserInput(editableFields[currentIndex], key);
                }
                break;
        }

        // Mettre à jour la barre supérieure
        display.topBar(title, false, false);
    }
}

std::string FieldEditorSelector::processUserInput(const std::string& currentValue, char key) {
    std::string updatedValue = currentValue;
    if (key == KEY_DEL && !updatedValue.empty()) {
        updatedValue.pop_back();
    } else if (std::isalnum(key) || std::ispunct(key)) {
        updatedValue += key;
    }
    return updatedValue;
}
