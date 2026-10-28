#include "TotpService.h"
#include <mbedtls/md.h>
#include <sys/time.h>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>

#ifndef BUILD_UNIX_EPOCH
#define BUILD_UNIX_EPOCH 1793210400UL
#endif

void TotpService::initClock(uint64_t savedEpoch) {
    // Define fuso horário padrão para Brasília (UTC-3: POSIX "BRT3")
    setenv("TZ", "BRT3", 1);
    tzset();

    time_t now = time(nullptr);
    uint64_t floorEpoch = BUILD_UNIX_EPOCH;
    if (savedEpoch > floorEpoch) {
        floorEpoch = savedEpoch;
    }

    if (static_cast<uint64_t>(now) < floorEpoch) {
        struct timeval tv;
        tv.tv_sec = static_cast<time_t>(floorEpoch);
        tv.tv_usec = 0;
        settimeofday(&tv, nullptr);
    }
}

uint64_t TotpService::getCurrentEpoch() {
    time_t now = time(nullptr);
    if (now < static_cast<time_t>(BUILD_UNIX_EPOCH)) {
        initClock(0);
        now = time(nullptr);
    }
    return static_cast<uint64_t>(now);
}

std::string TotpService::getCurrentDateTimeStr() {
    setenv("TZ", "BRT3", 1);
    tzset();
    time_t now = static_cast<time_t>(getCurrentEpoch());
    struct tm tmLocal;
    localtime_r(&now, &tmLocal);

    char buf[32];
    snprintf(buf, sizeof(buf), "%02d/%02d/%04d %02d:%02d",
             tmLocal.tm_mday,
             tmLocal.tm_mon + 1,
             tmLocal.tm_year + 1900,
             tmLocal.tm_hour,
             tmLocal.tm_min);
    return std::string(buf);
}

std::string TotpService::getCurrentTimeShortStr() {
    setenv("TZ", "BRT3", 1);
    tzset();
    time_t now = static_cast<time_t>(getCurrentEpoch());
    struct tm tmLocal;
    localtime_r(&now, &tmLocal);

    char buf[24];
    snprintf(buf, sizeof(buf), "%02d/%02d %02d:%02d",
             tmLocal.tm_mday,
             tmLocal.tm_mon + 1,
             tmLocal.tm_hour,
             tmLocal.tm_min);
    return std::string(buf);
}

bool TotpService::setDateTimeFromStr(const std::string& input, uint64_t* outNewEpoch) {
    setenv("TZ", "BRT3", 1);
    tzset();

    time_t now = static_cast<time_t>(getCurrentEpoch());
    struct tm tmLocal;
    localtime_r(&now, &tmLocal);

    int day = tmLocal.tm_mday;
    int month = tmLocal.tm_mon + 1;
    int year = tmLocal.tm_year + 1900;
    int hour = tmLocal.tm_hour;
    int min = tmLocal.tm_min;
    int sec = 0;

    // Extrai apenas os dígitos numéricos para aceitar qualquer formato:
    // - "28/10/2026 15:30" ou "28/10/2026 1530" ou "28102026 1530" -> 12 dígitos (DDMMYYYYHHMM)
    // - "28/10/2026 15:30:45" -> 14 dígitos (DDMMYYYYHHMMSS)
    // - "15:30" ou "1530" -> 4 dígitos (HHMM, mantém a data atual)
    std::string digits;
    for (char c : input) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            digits += c;
        }
    }

    if (digits.size() == 4) {
        // Apenas HHMM
        hour = std::stoi(digits.substr(0, 2));
        min = std::stoi(digits.substr(2, 2));
    } else if (digits.size() == 6) {
        // HHMMSS
        hour = std::stoi(digits.substr(0, 2));
        min = std::stoi(digits.substr(2, 2));
        sec = std::stoi(digits.substr(4, 2));
    } else if (digits.size() == 12 || digits.size() == 14) {
        // DDMMYYYYHHMM[SS]
        day = std::stoi(digits.substr(0, 2));
        month = std::stoi(digits.substr(2, 2));
        year = std::stoi(digits.substr(4, 4));
        hour = std::stoi(digits.substr(8, 2));
        min = std::stoi(digits.substr(10, 2));
        if (digits.size() == 14) {
            sec = std::stoi(digits.substr(12, 2));
        }
    } else {
        // Tenta sscanf flexível
        if (sscanf(input.c_str(), "%d/%d/%d %d:%d:%d", &day, &month, &year, &hour, &min, &sec) < 5) {
            if (sscanf(input.c_str(), "%d/%d/%d %d:%d", &day, &month, &year, &hour, &min) != 5) {
                if (sscanf(input.c_str(), "%d:%d", &hour, &min) != 2) {
                    return false;
                }
            }
        }
    }

    if (year < 2024 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 59) {
        return false;
    }

    struct tm newTm = {};
    newTm.tm_year = year - 1900;
    newTm.tm_mon = month - 1;
    newTm.tm_mday = day;
    newTm.tm_hour = hour;
    newTm.tm_min = min;
    newTm.tm_sec = sec;
    newTm.tm_isdst = -1;

    time_t newEpoch = mktime(&newTm);
    if (newEpoch <= 0) {
        return false;
    }

    struct timeval tv;
    tv.tv_sec = newEpoch;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);

    if (outNewEpoch) {
        *outNewEpoch = static_cast<uint64_t>(newEpoch);
    }
    return true;
}

std::string TotpService::extractBase32Secret(const std::string& rawInput) {
    if (rawInput.empty()) return "";

    std::string input = rawInput;
    // Se for uma URI otpauth://totp/...?secret=XXXX&...
    auto pos = input.find("secret=");
    if (pos == std::string::npos) {
        pos = input.find("SECRET=");
    }
    if (pos != std::string::npos) {
        input = input.substr(pos + 7);
        auto amp = input.find('&');
        if (amp != std::string::npos) {
            input = input.substr(0, amp);
        }
    }

    std::string cleaned;
    for (char c : input) {
        if ((c >= 'A' && c <= 'Z') || (c >= '2' && c <= '7')) {
            cleaned += c;
        } else if (c >= 'a' && c <= 'z') {
            cleaned += static_cast<char>(c - 'a' + 'A');
        }
    }
    return cleaned;
}

std::vector<uint8_t> TotpService::decodeBase32(const std::string& base32Input) {
    std::string clean = extractBase32Secret(base32Input);
    std::vector<uint8_t> out;
    if (clean.empty()) return out;

    uint32_t buffer = 0;
    int bitsLeft = 0;

    for (char c : clean) {
        int val = -1;
        if (c >= 'A' && c <= 'Z') {
            val = c - 'A';
        } else if (c >= '2' && c <= '7') {
            val = c - '2' + 26;
        } else {
            continue;
        }

        buffer = (buffer << 5) | static_cast<uint32_t>(val);
        bitsLeft += 5;

        if (bitsLeft >= 8) {
            bitsLeft -= 8;
            out.push_back(static_cast<uint8_t>((buffer >> bitsLeft) & 0xFF));
        }
    }
    return out;
}

std::string TotpService::generateCode(const std::string& secretOrUri, uint8_t* outRemainingSec, uint32_t timeStepSec) {
    auto key = decodeBase32(secretOrUri);
    if (key.empty() || timeStepSec == 0) {
        return "";
    }

    uint64_t unixTimeSec = getCurrentEpoch();
    uint64_t timeCounter = unixTimeSec / timeStepSec;
    if (outRemainingSec) {
        *outRemainingSec = static_cast<uint8_t>(timeStepSec - (unixTimeSec % timeStepSec));
    }

    uint8_t counterBytes[8];
    for (int i = 7; i >= 0; --i) {
        counterBytes[i] = static_cast<uint8_t>(timeCounter & 0xFF);
        timeCounter >>= 8;
    }

    uint8_t hmacResult[20];
    const mbedtls_md_info_t* mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    if (!mdInfo) {
        return "";
    }

    if (mbedtls_md_hmac(mdInfo, key.data(), key.size(), counterBytes, sizeof(counterBytes), hmacResult) != 0) {
        return "";
    }

    uint8_t offset = hmacResult[19] & 0x0F;
    uint32_t binaryCode =
        ((static_cast<uint32_t>(hmacResult[offset] & 0x7F)) << 24) |
        ((static_cast<uint32_t>(hmacResult[offset + 1] & 0xFF)) << 16) |
        ((static_cast<uint32_t>(hmacResult[offset + 2] & 0xFF)) << 8) |
        (static_cast<uint32_t>(hmacResult[offset + 3] & 0xFF));

    uint32_t otp = binaryCode % 1000000U;
    char outCode[8];
    snprintf(outCode, sizeof(outCode), "%06u", static_cast<unsigned int>(otp));
    return std::string(outCode);
}

