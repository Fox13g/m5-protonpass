#ifndef TOTP_SERVICE_H
#define TOTP_SERVICE_H

#include <string>
#include <cstdint>
#include <vector>

class TotpService {
public:
    static void initClock(uint64_t savedEpoch = 0);
    static uint64_t getCurrentEpoch();
    static std::string getCurrentDateTimeStr();
    static std::string getCurrentTimeShortStr();
    static std::string getCurrentTimeWithSecondsStr();

    static void getLocalDateTimeComponents(int& day, int& month, int& year, int& hour, int& min, int& sec);
    static bool setLocalDateTimeComponents(int day, int month, int year, int hour, int min, int sec, uint64_t* outNewEpoch = nullptr);
    static bool setDateTimeFromStr(const std::string& input, uint64_t* outNewEpoch = nullptr);

    static std::vector<std::string> scanWifiNetworks();
    static bool syncTimeViaWifiNtp(const std::string& ssid, const std::string& password, uint64_t* outNewEpoch = nullptr, std::string* outError = nullptr);

    static std::string extractBase32Secret(const std::string& rawInput);
    static std::vector<uint8_t> decodeBase32(const std::string& base32Input);
    static std::string generateCode(const std::string& secretOrUri, uint8_t* outRemainingSec = nullptr, uint32_t timeStepSec = 30);
};

#endif // TOTP_SERVICE_H
