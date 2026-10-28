#ifndef ICON_ENUM_H
#define ICON_ENUM_H

#include <unordered_map>
#include <vector>
#include <string>

enum class IconEnum {
    None,
    CreateVault,
    LoadVault,
    SdCard,
    Settings,
    Clock,
    AddEntry,
    SelectEntry,
    DeleteEntry,
};

class IconEnumMapper {
public:
    static std::string toString(IconEnum icon) {
        static const std::unordered_map<IconEnum, std::string> iconToStringMap = {
            {IconEnum::None, "None"},
            {IconEnum::CreateVault, "Create Vault"},
            {IconEnum::LoadVault, "Load Vault"},
            {IconEnum::SdCard, "Load File"},
            {IconEnum::Settings, "Settings"},
            {IconEnum::Clock, "Date & Time"},
            {IconEnum::AddEntry, "New Password"},
            {IconEnum::SelectEntry, "My Passwords"},
            {IconEnum::DeleteEntry, "Del Password"}
        };

        auto it = iconToStringMap.find(icon);
        return it != iconToStringMap.end() ? it->second : "Unknown Icon";
    }

    static std::vector<std::string> getIconNames(const std::vector<IconEnum>& icons) {
        std::vector<std::string> iconNames;
        for (const auto& icon : icons) {
            iconNames.push_back(toString(icon));
        }
        return iconNames;
    }
};

#endif // ICON_ENUM_H
