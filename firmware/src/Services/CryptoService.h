#ifndef CRYPTO_SERVICE_H
#define CRYPTO_SERVICE_H

#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <mbedtls/aes.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/pkcs5.h>
#include <States/GlobalState.h>

class CryptoService {
public:
    CryptoService();

    // Key derivation and passphrase handling
    std::vector<uint8_t> deriveKeyFromPassphrase(const std::string& passphrase, const std::string& salt, size_t keySize);
    
    // AES encryption and decryption
    std::vector<uint8_t> encryptAES(const std::vector<uint8_t>& data, const std::vector<uint8_t>& key);
    std::vector<uint8_t> decryptAES(const std::vector<uint8_t>& encrypted, const std::vector<uint8_t>& key);

    // Passphrase-based encryption/decryption of private data
    std::vector<uint8_t> encryptWithPassphrase(const std::string& data, const std::string& passphrase, const std::vector<uint8_t>& salt);
    std::string decryptWithPassphrase(const std::vector<uint8_t>& encryptedData, const std::string& passphrase, const std::vector<uint8_t>& salt);

    // Utility
    std::vector<uint8_t> generateChecksum(const std::string& data, size_t size);
    std::vector<uint8_t> generateSalt(size_t saltSize);
    std::vector<uint8_t> generateHardwareRandom(size_t size);
    std::string generateRandomString(size_t length);
};

#endif // CRYPTO_SERVICE_H
