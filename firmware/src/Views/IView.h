#ifndef IVIEW_H
#define IVIEW_H

#include <vector>
#include <string>
#include <cstdint>

class IView {
public:
    virtual ~IView() = default;

    virtual void initialize() = 0;
    virtual void welcome(uint8_t defaultBrightness=100) = 0;
    virtual void setBrightness(uint16_t brightness) = 0;
    virtual uint8_t getBrightness() = 0;
    virtual void topBar(const std::string& title, bool submenu, bool searchBar) = 0;
    virtual void horizontalSelection(const std::vector<std::string>& options, uint16_t selectedIndex, const std::string& description1="", const std::string& description2="", const std::vector<std::string>& icons={}) = 0;
    virtual void verticalSelection(const std::vector<std::string>& options, uint16_t selectedIndex, size_t visibleRows = 4, const std::vector<std::string>& optionLabels = {}, const std::vector<std::string>& shortcuts = {}, bool visibleMention=false) = 0;
    virtual void value(std::string label, std::string val) = 0; 
    virtual void subMessage(std::string message, int delayMs) = 0;
    virtual void stringPrompt(std::string label, std::string value, bool backButton, size_t minLength) = 0;
    virtual void confirmationPrompt(std::string label) = 0;
    virtual void debug(const std::string& message) = 0;
};

#endif
