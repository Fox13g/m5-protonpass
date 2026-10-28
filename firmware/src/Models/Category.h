#ifndef CATEGORY_H
#define CATEGORY_H

#include <string>

class Category {
private:
    size_t index;
    std::string name;
    std::string colorCode;
    std::string iconPath;

public:
    // Constructeur default
    Category()
        : index(0), name(""), colorCode(""), iconPath("") {}

    // Constructeur
    Category(size_t index, const std::string& name, const std::string& colorCode = "", const std::string& iconPath = "")
        : index(index), name(name), colorCode(colorCode), iconPath(iconPath) {}

    // Accesseurs
    size_t getIndex() const { return index; }
    const std::string& getName() const { return name; }
    const std::string& getColorCode() const { return colorCode; }
    const std::string& getIconPath() const { return iconPath; }

    // Mutateurs
    void setIndex(size_t newIndex) { index = newIndex; }
    void setName(const std::string& newName) { name = newName; }
    void setColorCode(const std::string& newColorCode) { colorCode = newColorCode; }
    void setIconPath(const std::string& newIconPath) { iconPath = newIconPath; }

    // Méthode utilitaire
    bool empty() const { return name.empty(); }
};

#endif // CATEGORY_H
