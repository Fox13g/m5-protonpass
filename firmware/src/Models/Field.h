#ifndef FIELD_H
#define FIELD_H

#include <string>

class Field {
private:
    std::string label;
    std::string value;
    std::string shortcut;

public:
    // Constructeurs
    Field() : label(""), value(""), shortcut("") {}
    Field(const std::string& label, const std::string& value, const std::string& shortcut = "")
        : label(label), value(value), shortcut(shortcut) {}

    // Accesseurs
    const std::string& getLabel() const { return label; }
    const std::string& getValue() const { return value; }
    const std::string& getShortcut() const { return shortcut; }

    // Mutateurs
    void setLabel(const std::string& newLabel) { label = newLabel; }
    void setValue(const std::string& newValue) { value = newValue; }
    void setShortcut(const std::string& newShortcut) { shortcut = newShortcut; }

    // Méthodes utils
    bool empty() const { return label.empty() && value.empty() && shortcut.empty(); }
};

#endif // FIELD_H
