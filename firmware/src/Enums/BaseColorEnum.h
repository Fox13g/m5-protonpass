#ifndef BASE_COLOR_ENUM_H
#define BASE_COLOR_ENUM_H
#include <unordered_map>
#include <string>
#include <vector>
#include <cstdint>
#include <M5Cardputer.h>

enum class BaseColorEnum {
    Black,
    Navy,
    DarkGreen,
    DarkCyan,
    Maroon,
    Purple,
    Olive,
    LightGrey,
    LightGray,
    DarkGrey,
    DarkGray,
    Blue,
    Green,
    Cyan,
    Red,
    Magenta,
    Yellow,
    White,
    Orange,
    GreenYellow,
    Pink,
    Brown,
    Gold,
    Silver,
    SkyBlue,
    Violet,
    Transparent
};

class BaseColorEnumMapper {
public:
    static std::string toString(BaseColorEnum color) {
        static const std::unordered_map<BaseColorEnum, std::string> colorToStringMap = {
            {BaseColorEnum::Black, "Black"},
            {BaseColorEnum::Navy, "Navy"},
            {BaseColorEnum::DarkGreen, "Dark Green"},
            {BaseColorEnum::DarkCyan, "Dark Cyan"},
            {BaseColorEnum::Maroon, "Maroon"},
            {BaseColorEnum::Purple, "Purple"},
            {BaseColorEnum::Olive, "Olive"},
            {BaseColorEnum::LightGrey, "Light Grey"},
            {BaseColorEnum::LightGray, "Light Gray"},
            {BaseColorEnum::DarkGrey, "Dark Grey"},
            {BaseColorEnum::DarkGray, "Dark Gray"},
            {BaseColorEnum::Blue, "Blue"},
            {BaseColorEnum::Green, "Green"},
            {BaseColorEnum::Cyan, "Cyan"},
            {BaseColorEnum::Red, "Red"},
            {BaseColorEnum::Magenta, "Magenta"},
            {BaseColorEnum::Yellow, "Yellow"},
            {BaseColorEnum::White, "White"},
            {BaseColorEnum::Orange, "Orange"},
            {BaseColorEnum::GreenYellow, "Green Yellow"},
            {BaseColorEnum::Pink, "Pink"},
            {BaseColorEnum::Brown, "Brown"},
            {BaseColorEnum::Gold, "Gold"},
            {BaseColorEnum::Silver, "Silver"},
            {BaseColorEnum::SkyBlue, "Sky Blue"},
            {BaseColorEnum::Violet, "Violet"},
            {BaseColorEnum::Transparent, "Transparent"}
        };

        auto it = colorToStringMap.find(color);
        return it != colorToStringMap.end() ? it->second : "Unknown Color";
    }

    static uint32_t toColorValue(BaseColorEnum color) {
        switch (color) {
            case BaseColorEnum::Black:        return TFT_BLACK;
            case BaseColorEnum::Navy:         return TFT_NAVY;
            case BaseColorEnum::DarkGreen:    return TFT_DARKGREEN;
            case BaseColorEnum::DarkCyan:     return TFT_DARKCYAN;
            case BaseColorEnum::Maroon:       return TFT_MAROON;
            case BaseColorEnum::Purple:       return TFT_PURPLE;
            case BaseColorEnum::Olive:        return TFT_OLIVE;
            case BaseColorEnum::LightGrey:    return TFT_LIGHTGREY;
            case BaseColorEnum::LightGray:    return TFT_LIGHTGRAY;
            case BaseColorEnum::DarkGrey:     return TFT_DARKGREY;
            case BaseColorEnum::DarkGray:     return TFT_DARKGRAY;
            case BaseColorEnum::Blue:         return TFT_BLUE;
            case BaseColorEnum::Green:        return TFT_GREEN;
            case BaseColorEnum::Cyan:         return TFT_CYAN;
            case BaseColorEnum::Red:          return TFT_RED;
            case BaseColorEnum::Magenta:      return TFT_MAGENTA;
            case BaseColorEnum::Yellow:       return TFT_YELLOW;
            case BaseColorEnum::White:        return TFT_WHITE;
            case BaseColorEnum::Orange:       return TFT_ORANGE;
            case BaseColorEnum::GreenYellow:  return TFT_GREENYELLOW;
            case BaseColorEnum::Pink:         return TFT_PINK;
            case BaseColorEnum::Brown:        return TFT_BROWN;
            case BaseColorEnum::Gold:         return TFT_GOLD;
            case BaseColorEnum::Silver:       return TFT_SILVER;
            case BaseColorEnum::SkyBlue:      return TFT_SKYBLUE;
            case BaseColorEnum::Violet:       return TFT_VIOLET;
            case BaseColorEnum::Transparent:  return TFT_TRANSPARENT;
            default:                          return TFT_BLACK; // Fallback
        }
    }

    static std::vector<std::string> getAllColorNames() {
        static const std::vector<BaseColorEnum> allColors = {
            BaseColorEnum::Black, BaseColorEnum::Navy, BaseColorEnum::DarkGreen,
            BaseColorEnum::DarkCyan, BaseColorEnum::Maroon, BaseColorEnum::Purple,
            BaseColorEnum::Olive, BaseColorEnum::LightGrey, BaseColorEnum::LightGray,
            BaseColorEnum::DarkGrey, BaseColorEnum::DarkGray, BaseColorEnum::Blue,
            BaseColorEnum::Green, BaseColorEnum::Cyan, BaseColorEnum::Red,
            BaseColorEnum::Magenta, BaseColorEnum::Yellow, BaseColorEnum::White,
            BaseColorEnum::Orange, BaseColorEnum::GreenYellow, BaseColorEnum::Pink,
            BaseColorEnum::Brown, BaseColorEnum::Gold, BaseColorEnum::Silver,
            BaseColorEnum::SkyBlue, BaseColorEnum::Violet, BaseColorEnum::Transparent
        };

        std::vector<std::string> colorNames;
        for (const auto& color : allColors) {
            colorNames.push_back(toString(color));
        }
        return colorNames;
    }
};

#endif // BASE_COLOR_ENUM_H
