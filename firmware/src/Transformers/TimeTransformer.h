#ifndef TIME_TRANSFORMER_H
#define TIME_TRANSFORMER_H

#include <string>
#include <vector>
#include <unordered_map>

class TimeTransformer {
public:
    TimeTransformer();

    std::string toLabel(uint32_t timeMs) const;
    uint32_t toMilliseconds(const std::string& label) const;
    const std::vector<std::string>& getAllTimeLabels() const;
    const std::vector<uint32_t>& getAllTimeValues() const;

private:
    std::vector<std::string> timeLabels;
    std::vector<uint32_t> timeValues;
};

#endif // TIME_TRANSFORMER_H
