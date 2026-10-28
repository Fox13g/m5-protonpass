#include "TimeTransformer.h"

TimeTransformer::TimeTransformer() {
    timeLabels = {"30 secondes", "1 minute", "3 minutes", "5 minutes", "10 minutes", "30 minutes", "1 hour", "5 hours"};
    timeValues = {
        30 * 1000,       // 30 sec = 30 000 ms
        1 * 60 * 1000,   // 1 minute  = 60 000 ms
        3 * 60 * 1000,   // 3 minutes = 180 000 ms
        5 * 60 * 1000,   // 5 minutes = 300 000 ms
        10 * 60 * 1000,  // 10 minutes = 600 000 ms
        30 * 60 * 1000,  // 30 minutes = 1 800 000 ms
        1 * 60 * 60 * 1000, // 1 heure = 3 600 000 ms
        5 * 60 * 60 * 1000 // 5 heures
    };
}

std::string TimeTransformer::toLabel(uint32_t timeMs) const {
    for (size_t i = 0; i < timeValues.size(); ++i) {
        if (timeValues[i] == timeMs) {
            return timeLabels[i];
        }
    }
    return "Unknown";
}

uint32_t TimeTransformer::toMilliseconds(const std::string& label) const {
    for (size_t i = 0; i < timeLabels.size(); ++i) {
        if (timeLabels[i] == label) {
            return timeValues[i];
        }
    }
    return 0;
}

const std::vector<std::string>& TimeTransformer::getAllTimeLabels() const {
    return timeLabels;
}

const std::vector<uint32_t>& TimeTransformer::getAllTimeValues() const {
    return timeValues;
}
