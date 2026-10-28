#include "ModelTransformer.h"
#include "../Services/TotpService.h"

template <typename T>
std::vector<std::string> ModelTransformer::toStrings(const std::vector<T>& models, std::function<std::string(const T&)> extractor) {
    std::vector<std::string> result;
    for (const auto& model : models) {
        result.push_back(extractor(model));
    }
    return result;
}

std::vector<std::string> ModelTransformer::toStrings(const std::vector<Entry>& models) {
    return toStrings<Entry>(models, [](const Entry& entry) { return entry.getServiceName(); });
}

std::vector<std::string> ModelTransformer::toStrings(const std::vector<Category>& models) {
    return toStrings<Category>(models, [](const Category& category) { return category.getName(); });
}

std::vector<std::string> ModelTransformer::toStrings(const Entry& entry) {
    std::string totpDisplay = "No 2FA";
    if (!entry.getNotes2().empty()) {
        uint8_t remSec = 0;
        std::string code = TotpService::generateCode(entry.getNotes2(), &remSec);
        if (code.size() == 6) {
            totpDisplay = code.substr(0, 3) + " " + code.substr(3) + " (" + std::to_string(remSec) + "s)";
        } else if (!code.empty()) {
            totpDisplay = code + " (" + std::to_string(remSec) + "s)";
        } else {
            totpDisplay = entry.getNotes2();
        }
    }

    return {
        entry.getUsername(),
        entry.getPassword(),
        totpDisplay,
        entry.getNotes().empty() ? "No notes" : entry.getNotes()
    };
}
