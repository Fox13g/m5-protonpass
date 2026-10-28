#include "CryptoService.h"
#include <mbedtls/sha256.h>
#include <cstring>
#include <algorithm>
#include <esp_random.h>

CryptoService::CryptoService() {}

std::vector<uint8_t> CryptoService::generateHardwareRandom(size_t size) {
    std::vector<uint8_t> randomData(size);
    esp_fill_random(randomData.data(), randomData.size());
    return randomData;
}

std::string CryptoService::generateRandomString(size_t length) {
    const std::string PRINTABLE_CHARACTERS =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789"
        "!@#$&*-_=+";

    auto randomData = generateHardwareRandom(length);
    std::string randomString;
    randomString.reserve(length);

    for (size_t i = 0; i < length; i++) {
        randomString += PRINTABLE_CHARACTERS[randomData[i] % PRINTABLE_CHARACTERS.size()];
    }

    return randomString;
}

std::vector<uint8_t> CryptoService::deriveKeyFromPassphrase(const std::string& passphrase, const std::string& salt, size_t keySize) {
    std::vector<uint8_t> key(keySize, 0);

    mbedtls_md_context_t mdContext;
    mbedtls_md_init(&mdContext);
    const mbedtls_md_info_t* mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    if (mbedtls_md_setup(&mdContext, mdInfo, 1) != 0) {
        mbedtls_md_free(&mdContext);
        return key;
    }

    mbedtls_pkcs5_pbkdf2_hmac(
        &mdContext,
        reinterpret_cast<const unsigned char*>(passphrase.data()), passphrase.size(),
        reinterpret_cast<const unsigned char*>(salt.data()), salt.size(),
        10000, // Number of iterations (matches geo-tp/Password-Manager)
        keySize,
        key.data()
    );

    mbedtls_md_free(&mdContext);
    return key;
}

std::vector<uint8_t> CryptoService::encryptAES(const std::vector<uint8_t>& data, const std::vector<uint8_t>& key) {
    if (data.empty() || data.size() % 16 != 0 || key.size() != 16) {
        return {};
    }

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, key.data(), 128);

    std::vector<uint8_t> encrypted(data.size());
    for (size_t i = 0; i < data.size(); i += 16) {
        mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, data.data() + i, encrypted.data() + i);
    }

    mbedtls_aes_free(&aes);
    return encrypted;
}

std::vector<uint8_t> CryptoService::decryptAES(const std::vector<uint8_t>& encrypted, const std::vector<uint8_t>& key) {
    if (encrypted.empty() || encrypted.size() % 16 != 0 || key.size() != 16) {
        return {};
    }

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, key.data(), 128);

    std::vector<uint8_t> decrypted(encrypted.size());
    for (size_t i = 0; i < encrypted.size(); i += 16) {
        mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, encrypted.data() + i, decrypted.data() + i);
    }

    mbedtls_aes_free(&aes);
    return decrypted;
}

std::vector<uint8_t> CryptoService::encryptWithPassphrase(const std::string& data, const std::string& passphrase, const std::vector<uint8_t>& salt) {
    std::string saltStr(salt.begin(), salt.end());
    auto key = deriveKeyFromPassphrase(passphrase, saltStr, 16);

    std::vector<uint8_t> dataBytes(data.begin(), data.end());

    // PKCS#7 padding to multiple of 16 bytes
    size_t padding = 16 - (dataBytes.size() % 16);
    dataBytes.insert(dataBytes.end(), padding, static_cast<uint8_t>(padding));

    return encryptAES(dataBytes, key);
}

std::string CryptoService::decryptWithPassphrase(const std::vector<uint8_t>& encryptedData, const std::string& passphrase, const std::vector<uint8_t>& salt) {
    if (encryptedData.empty() || encryptedData.size() % 16 != 0) {
        return "";
    }

    std::string saltStr(salt.begin(), salt.end());
    auto key = deriveKeyFromPassphrase(passphrase, saltStr, 16);

    auto decrypted = decryptAES(encryptedData, key);
    if (decrypted.empty()) {
        return "";
    }

    uint8_t padding = decrypted.back();
    if (padding == 0 || padding > 16 || padding > decrypted.size()) {
        return "";
    }

    // Verify all PKCS#7 padding bytes
    for (size_t i = decrypted.size() - padding; i < decrypted.size(); ++i) {
        if (decrypted[i] != padding) {
            return "";
        }
    }

    decrypted.resize(decrypted.size() - padding);
    return std::string(decrypted.begin(), decrypted.end());
}

std::vector<uint8_t> CryptoService::generateSalt(size_t saltSize) {
    std::vector<uint8_t> salt(saltSize);
    esp_fill_random(salt.data(), salt.size());
    return salt;
}

std::vector<uint8_t> CryptoService::generateChecksum(const std::string& data, size_t size) {
    uint8_t hash[32];
    mbedtls_sha256(reinterpret_cast<const uint8_t*>(data.data()), data.size(), hash, 0);
    return std::vector<uint8_t>(hash, hash + std::min(size, static_cast<size_t>(32)));
}
