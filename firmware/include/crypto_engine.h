#pragma once

#include <stdint.h>
#include <stddef.h>
#include "types.h"

namespace sentinel {

class CryptoEngine {
public:
    /**
     * Deriva uma Key Encryption Key (KEK) de 256 bits a partir do PIN ou Senha Mestra local
     * invocando `mbedtls_pkcs5_pbkdf2_hmac` (SHA-256, 100.000 iterações).
     */
    static bool deriveKekFromPassphrase(
        const char* passphrase,
        const uint8_t* salt,
        size_t saltLen,
        uint32_t iterations,
        uint8_t outKek32[32]
    );

    /**
     * Cifra autenticada AES-256-GCM com IV de 12 bytes, Tag de 16 bytes e AAD opcional.
     */
    static bool encryptAes256Gcm(
        const uint8_t* plainData,
        size_t length,
        const uint8_t key32[32],
        const uint8_t iv12[12],
        const uint8_t* aad,
        size_t aadLen,
        uint8_t* cipherOut,
        uint8_t tagOut16[16]
    );

    /**
     * Decifra autenticada por hardware AES-256-GCM via `mbedtls_gcm_auth_decrypt`.
     * Caso qualquer bit do ciphertext ou do AAD tenha sido alterado, retorna `false` e zera `plainOut`.
     */
    static bool decryptAes256Gcm(
        const uint8_t* cipherData,
        size_t length,
        const uint8_t key32[32],
        const uint8_t iv12[12],
        const uint8_t tag16[16],
        const uint8_t* aad,
        size_t aadLen,
        uint8_t* plainOut
    );

    /**
     * Embrulha (Key Wrap) a DEK de 256 bits usando a KEK derivada da Senha Mestra local.
     */
    static bool wrapDek(
        const uint8_t dek32[32],
        const uint8_t kek32[32],
        const uint8_t iv12[12],
        uint8_t wrappedDekOut32[32],
        uint8_t tagOut16[16]
    );

    /**
     * Desembrulha (Key Unwrap) a DEK guardada na NVS validando a Tag GCM.
     */
    static bool unwrapDek(
        const uint8_t wrappedDek32[32],
        const uint8_t kek32[32],
        const uint8_t iv12[12],
        const uint8_t tag16[16],
        uint8_t dekOut32[32]
    );

    /**
     * Calcula SHA-256 de um buffer arbitrário.
     */
    static bool computeSha256(const uint8_t* data, size_t len, uint8_t outHash32[32]);

    /**
     * Calcula assinatura HMAC-SHA256 canónica para autenticação Zero-Trust junto ao Bridge Gateway.
     */
    static bool computeRequestHmacHex(
        const char* method,
        const char* path,
        const char* timestampStr,
        const char* nonceHex,
        const uint8_t* body,
        size_t bodyLen,
        const uint8_t hmacSecret32[32],
        char outSignatureHex[65]
    );

    /**
     * Preenche um buffer com bytes aleatórios do gerador de hardware (TRNG do ESP32-S3).
     */
    static void fillRandomBytes(uint8_t* buf, size_t len);

    /**
     * Gera uma senha aleatória criptograficamente segura no próprio Cardputer (modo offline).
     */
    static void generateStrongPassword(char* outBuf, size_t length = 24);

    /**
     * Limpa de forma segura um bloco de memória volátil (`mbedtls_platform_zeroize`),
     * impedindo que o compilador otimize e remova a limpeza da senha na SRAM.
     */
    static void secureZeroize(void* buf, size_t len);

    /**
     * Converte string hexadecimal para bytes e vice-versa.
     */
    static bool hexToBytes(const char* hex, uint8_t* outBytes, size_t expectedBytes);
    static void bytesToHex(const uint8_t* bytes, size_t len, char* outHex);
};

} // namespace sentinel

