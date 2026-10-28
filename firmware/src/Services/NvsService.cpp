#include "NvsService.h"

NvsService::NvsService()
    : globalState(GlobalState::getInstance()) {
    // Open nvs namespace
    preferences.begin(globalState.getNvsNamespace().c_str(), false);
}

NvsService::~NvsService() {
    preferences.end(); // Close namespace
}

void NvsService::saveString(const std::string& key, const std::string& value) {
    preferences.putString(key.c_str(), value.c_str());
}

std::string NvsService::getString(const std::string& key, const std::string& defaultValue) {
    return preferences.getString(key.c_str(), defaultValue.c_str()).c_str();
}

void NvsService::saveInt(const std::string& key, int value) {
    preferences.putInt(key.c_str(), value);
}

int NvsService::getInt(const std::string& key, int defaultValue) {
    return preferences.getInt(key.c_str(), defaultValue);
}

void NvsService::remove(const std::string& key) {
    preferences.remove(key.c_str());
}

void NvsService::clearNamespace() {
    preferences.clear();
}
