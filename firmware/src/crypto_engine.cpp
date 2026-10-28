#include "crypto_engine.h"
#include <string.h>
#include <stdio.h>

#if !defined(UNIT_TEST_NATIVE)
#include <Arduino.h>
#include <esp_system.h>
#include <mbedtls/gcm.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>
#include <mbedtls/platform_util.h>
#else
#include <stdlib.h>
#endif

namespace sentinel {

static constexpr const char* KEY_WRAP_AAD = "CPVAULT_DEK_WRAP_V1";

bool CryptoEngine::deriveKekFromPassphrase(
    const char* passphrase,
    const uint8_t* salt,
    size_t saltLen,
    uint32_t iterations,
    uint8_t outKek32[32]
) {
    if (!passphrase || !salt || saltLen == 0 || !outKek32) {
        return false;
    }

#if !defined(UNIT_TEST_NATIVE)
    mbedtls_md_context_t mdCtx;
    mbedtls_md_init(&mdCtx);

    const mbedtls_md_info_t* mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!mdInfo || mbedtls_md_setup(&mdCtx, mdInfo, 1) != 0) {
        mbedtls_md_free(&mdCtx);
        return false;
    }

    int rc = mbedtls_pkcs5_pbkdf2_hmac(
        &mdCtx,
        reinterpret_cast<const unsigned char*>(passphrase),
        strlen(passphrase),
        salt,
        saltLen,
        iterations,
        32,
        outKek32
    );
    mbedtls_md_free(&mdCtx);
    return (rc == 0);
#else
    memset(outKek32, 0xAA, 32);
    return true;
#endif
}

bool CryptoEngine::encryptAes256Gcm(
    const uint8_t* plainData,
    size_t length,
    const uint8_t key32[32],
    const uint8_t iv12[12],
    const uint8_t* aad,
    size_t aadLen,
    uint8_t* cipherOut,
    uint8_t tagOut16[16]
) {
    if (!key32 || !iv12 || !tagOut16 || (length > 0 && (!plainData || !cipherOut))) {
        return false;
    }

#if !defined(UNIT_TEST_NATIVE)
    mbedtls_gcm_context gcmCtx;
    mbedtls_gcm_init(&gcmCtx);

    if (mbedtls_gcm_setkey(&gcmCtx, MBEDTLS_CIPHER_ID_AES, key32, 256) != 0) {
        mbedtls_gcm_free(&gcmCtx);
        return false;
    }

    int status = mbedtls_gcm_crypt_and_tag(
        &gcmCtx,
        MBEDTLS_GCM_ENCRYPT,
        length,
        iv12,
        12,
        aad,
        aadLen,
        plainData,
        cipherOut,
        16,
        tagOut16
    );
    mbedtls_gcm_free(&gcmCtx);
    return (status == 0);
#else
    if (length > 0) memcpy(cipherOut, plainData, length);
    memset(tagOut16, 0x55, 16);
    return true;
#endif
}

bool CryptoEngine::decryptAes256Gcm(
    const uint8_t* cipherData,
    size_t length,
    const uint8_t key32[32],
    const uint8_t iv12[12],
    const uint8_t tag16[16],
    const uint8_t* aad,
    size_t aadLen,
    uint8_t* plainOut
) {
    if (!key32 || !iv12 || !tag16 || (length > 0 && (!cipherData || !plainOut))) {
        return false;
    }

#if !defined(UNIT_TEST_NATIVE)
    mbedtls_gcm_context gcmCtx;
    mbedtls_gcm_init(&gcmCtx);

    if (mbedtls_gcm_setkey(&gcmCtx, MBEDTLS_CIPHER_ID_AES, key32, 256) != 0) {
        mbedtls_gcm_free(&gcmCtx);
        return false;
    }

    int status = mbedtls_gcm_auth_decrypt(
        &gcmCtx,
        length,
        iv12,
        12,
        aad,
        aadLen,
        tag16,
        16,
        cipherData,
        plainOut
    );
    mbedtls_gcm_free(&gcmCtx);

    if (status != 0) {
        if (plainOut && length > 0) {
            secureZeroize(plainOut, length);
        }
        return false;
    }
    return true;
#else
    if (length > 0) memcpy(plainOut, cipherData, length);
    return true;
#endif
}

bool CryptoEngine::wrapDek(
    const uint8_t dek32[32],
    const uint8_t kek32[32],
    const uint8_t iv12[12],
    uint8_t wrappedDekOut32[32],
    uint8_t tagOut16[16]
) {
    return encryptAes256Gcm(
        dek32,
        32,
        kek32,
        iv12,
        reinterpret_cast<const uint8_t*>(KEY_WRAP_AAD),
        strlen(KEY_WRAP_AAD),
        wrappedDekOut32,
        tagOut16
    );
}

bool CryptoEngine::unwrapDek(
    const uint8_t wrappedDek32[32],
    const uint8_t kek32[32],
    const uint8_t iv12[12],
    const uint8_t tag16[16],
    uint8_t dekOut32[32]
) {
    return decryptAes256Gcm(
        wrappedDek32,
        32,
        kek32,
        iv12,
        tag16,
        reinterpret_cast<const uint8_t*>(KEY_WRAP_AAD),
        strlen(KEY_WRAP_AAD),
        dekOut32
    );
}

bool CryptoEngine::computeSha256(const uint8_t* data, size_t len, uint8_t outHash32[32]) {
    if (!outHash32) return false;
#if !defined(UNIT_TEST_NATIVE)
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    if (mbedtls_sha256_starts_ret(&ctx, 0) != 0) {
        mbedtls_sha256_free(&ctx);
        return false;
    }
    if (len > 0 && data) {
        if (mbedtls_sha256_update_ret(&ctx, data, len) != 0) {
            mbedtls_sha256_free(&ctx);
            return false;
        }
    }
    int rc = mbedtls_sha256_finish_ret(&ctx, outHash32);
    mbedtls_sha256_free(&ctx);
    return (rc == 0);
#else
    memset(outHash32, 0, 32);
    return true;
#endif
}

bool CryptoEngine::computeRequestHmacHex(
    const char* method,
    const char* path,
    const char* timestampStr,
    const char* nonceHex,
    const uint8_t* body,
    size_t bodyLen,
    const uint8_t hmacSecret32[32],
    char outSignatureHex[65]
) {
    if (!method || !path || !timestampStr || !nonceHex || !hmacSecret32 || !outSignatureHex) {
        return false;
    }

    uint8_t bodyHash[32];
    if (!computeSha256(body, bodyLen, bodyHash)) {
        return false;
    }
    char bodyHashHex[65];
    bytesToHex(bodyHash, 32, bodyHashHex);

    char canonical[384];
    int written = snprintf(
        canonical,
        sizeof(canonical),
        "%s\n%s\n%s\n%s\n%s",
        method,
        path,
        timestampStr,
        nonceHex,
        bodyHashHex
    );
    if (written < 0 || static_cast<size_t>(written) >= sizeof(canonical)) {
        return false;
    }

#if !defined(UNIT_TEST_NATIVE)
    uint8_t hmacDigest[32];
    const mbedtls_md_info_t* mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!mdInfo) return false;

    int rc = mbedtls_md_hmac(
        mdInfo,
        hmacSecret32,
        32,
        reinterpret_cast<const unsigned char*>(canonical),
        static_cast<size_t>(written),
        hmacDigest
    );
    if (rc != 0) return false;

    bytesToHex(hmacDigest, 32, outSignatureHex);
    secureZeroize(hmacDigest, sizeof(hmacDigest));
    return true;
#else
    memset(outSignatureHex, 'a', 64);
    outSignatureHex[64] = '\0';
    return true;
#endif
}

void CryptoEngine::fillRandomBytes(uint8_t* buf, size_t len) {
    if (!buf || len == 0) return;
#if !defined(UNIT_TEST_NATIVE)
    esp_fill_random(buf, len);
#else
    for (size_t i = 0; i < len; ++i) {
        buf[i] = static_cast<uint8_t>(rand() & 0xFF);
    }
#endif
}

void CryptoEngine::generateStrongPassword(char* outBuf, size_t length) {
    if (!outBuf || length == 0) return;
    static constexpr const char CHARSET[] =
        "abcdefghijkmnopqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789!@#$%&*_+-=";
    constexpr size_t charsetSize = sizeof(CHARSET) - 1;

    uint8_t rnd[64];
    size_t clampedLen = (length > 63) ? 63 : length;
    fillRandomBytes(rnd, clampedLen);

    for (size_t i = 0; i < clampedLen; ++i) {
        outBuf[i] = CHARSET[rnd[i] % charsetSize];
    }
    outBuf[clampedLen] = '\0';
    secureZeroize(rnd, sizeof(rnd));
}

void CryptoEngine::secureZeroize(void* buf, size_t len) {
    if (!buf || len == 0) return;
#if !defined(UNIT_TEST_NATIVE)
    mbedtls_platform_zeroize(buf, len);
#else
    volatile uint8_t* p = reinterpret_cast<volatile uint8_t*>(buf);
    while (len--) {
        *p++ = 0;
    }
#endif
}

static int hexCharVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool CryptoEngine::hexToBytes(const char* hex, uint8_t* outBytes, size_t expectedBytes) {
    if (!hex || !outBytes) return false;
    size_t hexLen = strlen(hex);
    if (hexLen != expectedBytes * 2) return false;

    for (size_t i = 0; i < expectedBytes; ++i) {
        int hi = hexCharVal(hex[i * 2]);
        int lo = hexCharVal(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        outBytes[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

void CryptoEngine::bytesToHex(const uint8_t* bytes, size_t len, char* outHex) {
    static constexpr const char HEX_LUT[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        outHex[i * 2]     = HEX_LUT[(bytes[i] >> 4) & 0x0F];
        outHex[i * 2 + 1] = HEX_LUT[bytes[i] & 0x0F];
    }
    outHex[len * 2] = '\0';
}

} // namespace sentinel

