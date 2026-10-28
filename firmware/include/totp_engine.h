#pragma once

#include <stdint.h>
#include <stddef.h>

namespace sentinel {

class TotpEngine {
public:
    /**
     * Descodifica uma string Base32 (RFC 4648, ignorando espaços, hífens e padding '=')
     * para um vetor de bytes em memória estática.
     * Retorna o número de bytes descodificados ou 0 em caso de erro.
     */
    static size_t decodeBase32(const char* base32Input, uint8_t* outBytes, size_t maxOutLen);

    /**
     * Calcula o código TOTP de 6 dígitos (RFC 6238 / RFC 4226 HMAC-SHA1) para o carimbo Unix fornecido.
     * Formata o resultado sempre com 6 caracteres e zeros à esquerda (ex: "049281").
     */
    static bool generateTotpCode(
        const char* base32Secret,
        uint64_t unixTimeSec,
        char outCode7[7],
        uint8_t* outRemainingSec = nullptr,
        uint32_t timeStepSec = 30
    );

    /**
     * Calcula HMAC-SHA1 (20 bytes) para os contadores de 8 bytes Big-Endian da RFC 4226.
     */
    static bool computeHmacSha1(
        const uint8_t* key,
        size_t keyLen,
        const uint8_t* message,
        size_t messageLen,
        uint8_t outDigest20[20]
    );
};

} // namespace sentinel

