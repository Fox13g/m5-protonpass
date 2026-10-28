#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "types.h"
#include "crypto_engine.h"
#include "totp_engine.h"
#include "hid_injector.h"

using namespace sentinel;

static void testBinaryStructSizesAndAlignment() {
    assert(sizeof(CpVaultHeader) == 80);
    assert(sizeof(CpVaultIndexEntry) == 116);
    assert(sizeof(CpVaultItemPayload) == 564);
    assert(sizeof(CpVaultItemAad) == 24);
    printf("[PASS] CPVAULT v1 binary struct packing (80B header, 116B index, 564B item, 24B AAD)\n");
}

static void testBase32AndRfc6238Totp() {
    // Segredo ASCII "12345678901234567890" -> Base32 "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ"
    // Vetores oficiais da RFC 6238 (Apêndice B, SHA-1, 6 dígitos truncados):
    // T = 59s         -> 94287082 -> 6 dígitos = "287082"
    // T = 1111111109s -> 07081804 -> 6 dígitos = "081804"
    // T = 1234567890s -> 89005924 -> 6 dígitos = "005924"
    const char* rfcSecretB32 = "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ";
    uint8_t decoded[32];
    size_t decLen = TotpEngine::decodeBase32(rfcSecretB32, decoded, sizeof(decoded));
    assert(decLen == 20);
    assert(memcmp(decoded, "12345678901234567890", 20) == 0);

    char code[7];
    uint8_t rem = 0;

    assert(TotpEngine::generateTotpCode(rfcSecretB32, 59ULL, code, &rem, 30));
    assert(strcmp(code, "287082") == 0);
    assert(rem == 1);

    assert(TotpEngine::generateTotpCode(rfcSecretB32, 1111111109ULL, code, &rem, 30));
    assert(strcmp(code, "081804") == 0);

    assert(TotpEngine::generateTotpCode(rfcSecretB32, 1234567890ULL, code, &rem, 30));
    assert(strcmp(code, "005924") == 0);

    printf("[PASS] Base32 RFC 4648 decoder & TOTP RFC 6238 HMAC-SHA1 vectors verified\n");
}

static void testKeyboardLayoutsAbnt2AndUsAnsi() {
    HidKeyStroke stroke;

    // '/' no US ANSI é 0x38 sem Shift; no PT-BR ABNT2 é 0x87 (International1)
    assert(HidInjector::mapCharToHid('/', KeyboardLayout::US_ANSI, stroke));
    assert(stroke.keycode == 0x38 && stroke.modifier == 0x00);

    assert(HidInjector::mapCharToHid('/', KeyboardLayout::PT_BR_ABNT2, stroke));
    assert(stroke.keycode == 0x87 && stroke.modifier == 0x00);

    // '\' no US ANSI é 0x31; no PT-BR ABNT2 é 0x64 (Non-US Backslash)
    assert(HidInjector::mapCharToHid('\\', KeyboardLayout::US_ANSI, stroke));
    assert(stroke.keycode == 0x31 && stroke.modifier == 0x00);

    assert(HidInjector::mapCharToHid('\\', KeyboardLayout::PT_BR_ABNT2, stroke));
    assert(stroke.keycode == 0x64 && stroke.modifier == 0x00);

    // '~' no PT-BR ABNT2 emite Dead Key (0x34) seguida de SPACE (0x2C)
    assert(HidInjector::mapCharToHid('~', KeyboardLayout::PT_BR_ABNT2, stroke));
    assert(stroke.deadKeyCode == 0x34 && stroke.keycode == 0x2C);

    printf("[PASS] HID Keyboard Layout Mapper (PT-BR ABNT2 & US ANSI) verified\n");
}

static void testHexConversionAndZeroize() {
    const char* sampleHex = "0123456789abcdef0123456789abcdef";
    uint8_t bytes[16];
    assert(CryptoEngine::hexToBytes(sampleHex, bytes, 16));

    char roundTrip[33];
    CryptoEngine::bytesToHex(bytes, 16, roundTrip);
    assert(strcmp(sampleHex, roundTrip) == 0);

    CryptoEngine::secureZeroize(bytes, sizeof(bytes));
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        assert(bytes[i] == 0);
    }
    printf("[PASS] Hex codec & CryptoEngine::secureZeroize verified\n");
}

int main() {
    printf("=== Running Cardputer Pass Sentinel Firmware Native Unit Tests ===\n");
    testBinaryStructSizesAndAlignment();
    testBase32AndRfc6238Totp();
    testKeyboardLayoutsAbnt2AndUsAnsi();
    testHexConversionAndZeroize();
    printf("=== ALL FIRMWARE NATIVE TESTS PASSED ===\n");
    return 0;
}

