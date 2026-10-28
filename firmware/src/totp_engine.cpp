#include "totp_engine.h"
#include "crypto_engine.h"
#include <string.h>
#include <stdio.h>

#if !defined(UNIT_TEST_NATIVE)
#include <mbedtls/md.h>
#endif

namespace sentinel {

// ============================================================================
// Implementação Portável SHA-1 + HMAC-SHA1 para TOTP RFC 6238 / RFC 4226
// (Usa mbedtls por hardware no ESP32-S3 e SHA-1 padrão RFC 3174 nos testes nativos)
// ============================================================================
#if defined(UNIT_TEST_NATIVE)
namespace {

inline uint32_t rol32(uint32_t value, unsigned int bits) {
    return (value << bits) | (value >> (32 - bits));
}

void sha1Compress(uint32_t state[5], const uint8_t block[64]) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(block[i * 4 + 0]) << 24) |
               (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 8)  |
               (static_cast<uint32_t>(block[i * 4 + 3]));
    }
    for (int i = 16; i < 80; ++i) {
        w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    for (int i = 0; i < 80; ++i) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        uint32_t temp = rol32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol32(b, 30);
        b = a;
        a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

void sha1Digest(const uint8_t* data, size_t len, uint8_t out20[20]) {
    uint32_t state[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint8_t block[64];
    size_t offset = 0;

    while (len - offset >= 64) {
        sha1Compress(state, data + offset);
        offset += 64;
    }

    size_t rem = len - offset;
    memset(block, 0, sizeof(block));
    if (rem > 0) {
        memcpy(block, data + offset, rem);
    }
    block[rem] = 0x80;

    if (rem >= 56) {
        sha1Compress(state, block);
        memset(block, 0, sizeof(block));
    }

    uint64_t bitLen = static_cast<uint64_t>(len) * 8ULL;
    for (int i = 0; i < 8; ++i) {
        block[63 - i] = static_cast<uint8_t>((bitLen >> (i * 8)) & 0xFF);
    }
    sha1Compress(state, block);

    for (int i = 0; i < 5; ++i) {
        out20[i * 4 + 0] = static_cast<uint8_t>((state[i] >> 24) & 0xFF);
        out20[i * 4 + 1] = static_cast<uint8_t>((state[i] >> 16) & 0xFF);
        out20[i * 4 + 2] = static_cast<uint8_t>((state[i] >> 8) & 0xFF);
        out20[i * 4 + 3] = static_cast<uint8_t>(state[i] & 0xFF);
    }
}

} // namespace
#endif

size_t TotpEngine::decodeBase32(const char* base32Input, uint8_t* outBytes, size_t maxOutLen) {
    if (!base32Input || !outBytes || maxOutLen == 0) {
        return 0;
    }

    uint32_t buffer = 0;
    int bitsLeft = 0;
    size_t outCount = 0;

    for (const char* p = base32Input; *p != '\0'; ++p) {
        char c = *p;
        if (c == '=' || c == ' ' || c == '-' || c == '\t' || c == '\r' || c == '\n') {
            continue;
        }

        int val = -1;
        if (c >= 'A' && c <= 'Z') {
            val = c - 'A';
        } else if (c >= 'a' && c <= 'z') {
            val = c - 'a';
        } else if (c >= '2' && c <= '7') {
            val = c - '2' + 26;
        } else {
            return 0; // Caractere inválido na cadeia Base32
        }

        buffer = (buffer << 5) | static_cast<uint32_t>(val);
        bitsLeft += 5;

        if (bitsLeft >= 8) {
            bitsLeft -= 8;
            if (outCount >= maxOutLen) {
                return 0;
            }
            outBytes[outCount++] = static_cast<uint8_t>((buffer >> bitsLeft) & 0xFF);
        }
    }

    return outCount;
}

bool TotpEngine::computeHmacSha1(
    const uint8_t* key,
    size_t keyLen,
    const uint8_t* message,
    size_t messageLen,
    uint8_t outDigest20[20]
) {
    if (!key || keyLen == 0 || !message || !outDigest20) {
        return false;
    }

#if !defined(UNIT_TEST_NATIVE)
    const mbedtls_md_info_t* mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    if (!mdInfo) return false;
    return (mbedtls_md_hmac(mdInfo, key, keyLen, message, messageLen, outDigest20) == 0);
#else
    uint8_t kPad[64];
    memset(kPad, 0, sizeof(kPad));
    if (keyLen > 64) {
        sha1Digest(key, keyLen, kPad);
    } else {
        memcpy(kPad, key, keyLen);
    }

    uint8_t iPad[64];
    uint8_t oPad[64];
    for (int i = 0; i < 64; ++i) {
        iPad[i] = kPad[i] ^ 0x36;
        oPad[i] = kPad[i] ^ 0x5C;
    }

    uint8_t innerData[128];
    if (messageLen > 64) return false;
    memcpy(innerData, iPad, 64);
    memcpy(innerData + 64, message, messageLen);

    uint8_t innerHash[20];
    sha1Digest(innerData, 64 + messageLen, innerHash);

    uint8_t outerData[84];
    memcpy(outerData, oPad, 64);
    memcpy(outerData + 64, innerHash, 20);
    sha1Digest(outerData, 84, outDigest20);
    return true;
#endif
}

bool TotpEngine::generateTotpCode(
    const char* base32Secret,
    uint64_t unixTimeSec,
    char outCode7[7],
    uint8_t* outRemainingSec,
    uint32_t timeStepSec
) {
    if (!base32Secret || !outCode7 || timeStepSec == 0) {
        return false;
    }

    uint8_t decodedKey[64];
    size_t keyLen = decodeBase32(base32Secret, decodedKey, sizeof(decodedKey));
    if (keyLen == 0) {
        CryptoEngine::secureZeroize(decodedKey, sizeof(decodedKey));
        return false;
    }

    uint64_t timeCounter = unixTimeSec / timeStepSec;
    if (outRemainingSec) {
        *outRemainingSec = static_cast<uint8_t>(timeStepSec - (unixTimeSec % timeStepSec));
    }

    // Converte o contador de 64 bits para Big-Endian conforme RFC 4226 Secção 5.2
    uint8_t counterBytes[8];
    for (int i = 7; i >= 0; --i) {
        counterBytes[i] = static_cast<uint8_t>(timeCounter & 0xFF);
        timeCounter >>= 8;
    }

    uint8_t hmacResult[20];
    bool ok = computeHmacSha1(decodedKey, keyLen, counterBytes, sizeof(counterBytes), hmacResult);
    CryptoEngine::secureZeroize(decodedKey, sizeof(decodedKey));

    if (!ok) {
        CryptoEngine::secureZeroize(hmacResult, sizeof(hmacResult));
        return false;
    }

    // Truncamento Dinâmico (RFC 4226 Secção 5.3)
    uint8_t offset = hmacResult[19] & 0x0F;
    uint32_t binaryCode =
        ((static_cast<uint32_t>(hmacResult[offset] & 0x7F)) << 24) |
        ((static_cast<uint32_t>(hmacResult[offset + 1] & 0xFF)) << 16) |
        ((static_cast<uint32_t>(hmacResult[offset + 2] & 0xFF)) << 8) |
        (static_cast<uint32_t>(hmacResult[offset + 3] & 0xFF));

    CryptoEngine::secureZeroize(hmacResult, sizeof(hmacResult));

    uint32_t otp = binaryCode % 1000000U;
    snprintf(outCode7, 7, "%06u", static_cast<unsigned int>(otp));
    return true;
}

} // namespace sentinel

