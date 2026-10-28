#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace sentinel {

// ============================================================================
// Constantes do Formato Binário CPVAULT v1 (Alinhadas com bridge/vaultBuilder.ts)
// ============================================================================
constexpr char     CPVAULT_MAGIC[4]          = {'C', 'P', 'V', '1'};
constexpr uint8_t  CPVAULT_VERSION           = 1;
constexpr size_t   CPVAULT_HEADER_SIZE       = 80;
constexpr size_t   CPVAULT_HEADER_AAD_SIZE   = 48;
constexpr size_t   CPVAULT_INDEX_ENTRY_SIZE  = 116;
constexpr size_t   CPVAULT_ITEM_PAYLOAD_SIZE = 564;

constexpr char     CPOUTBOX_MAGIC[4]         = {'C', 'P', 'O', '1'};

#pragma pack(push, 1)

/**
 * Cabeçalho de 80 bytes do ficheiro `/vault.bin` (`CPVAULT v1`).
 * Os primeiros 48 bytes (`magic` até `revision_hash`) constituem o AAD da tabela de índice.
 */
struct CpVaultHeader {
    char     magic[4];            // 0x00: "CPV1"
    uint8_t  version;             // 0x04: 1
    uint8_t  flags;               // 0x05: bit 0 = read_only
    uint16_t item_count;          // 0x06: Número de itens (LE)
    uint64_t synced_at_unix;      // 0x08: Timestamp UTC da sincronização (LE)
    uint8_t  revision_hash[32];   // 0x10: Resumo SHA-256 canónico do cofre
    uint8_t  index_iv[12];        // 0x30: Nonce AES-256-GCM da tabela de índice
    uint8_t  index_tag[16];       // 0x3C: Tag GCM (16 B) da tabela de índice
    uint32_t index_cipher_len;    // 0x4C: Tamanho em bytes da tabela de índice cifrada
};
static_assert(sizeof(CpVaultHeader) == CPVAULT_HEADER_SIZE, "CpVaultHeader must be exactly 80 bytes");

/**
 * Entrada de 116 bytes na tabela de índice cifrada do `CPVAULT v1`.
 */
struct CpVaultIndexEntry {
    uint8_t  item_id[16];         // +0x00: Primeiros 16B do SHA-256(vault_id:item_uuid)
    char     title[40];           // +0x10: Título UTF-8 terminado em '\0'
    char     username_hint[24];   // +0x38: Resumo do utilizador terminado em '\0'
    uint8_t  item_flags;          // +0x50: bit 0 = has_totp, bit 1 = modified_offline
    uint8_t  reserved;            // +0x51: 0x00
    uint32_t blob_offset;         // +0x52: Offset absoluto no ficheiro vault.bin
    uint16_t blob_cipher_len;     // +0x56: Tamanho do payload cifrado (564 bytes)
    uint8_t  blob_iv[12];         // +0x58: Nonce AES-256-GCM exclusivo do item
    uint8_t  blob_tag[16];        // +0x64: Tag GCM (16 B) exclusiva do item
};
static_assert(sizeof(CpVaultIndexEntry) == CPVAULT_INDEX_ENTRY_SIZE, "CpVaultIndexEntry must be 116 bytes");

/**
 * Bloco de 564 bytes de credencial individual decifrado sob demanda em buffer estático.
 */
struct CpVaultItemPayload {
    uint64_t updated_at;          // +0x00: Época Unix UTC da última edição
    char     vault_id[36];        // +0x08: ID do cofre Proton Pass
    char     item_uuid[36];       // +0x2C: UUID do registo Proton Pass
    char     username[96];        // +0x50: Utilizador ou e-mail completo
    char     password[128];       // +0xB0: Palavra-passe
    char     totp_secret[64];     // +0x130: Segredo Base32 puro (RFC 4648)
    char     url[68];             // +0x170: URL principal
    char     notes[128];          // +0x1B4: Notas abreviadas
};
static_assert(sizeof(CpVaultItemPayload) == CPVAULT_ITEM_PAYLOAD_SIZE, "CpVaultItemPayload must be 564 bytes");

/**
 * AAD de 24 bytes vinculado a cada bloco individual de credencial contra ataques de Block Swapping.
 */
struct CpVaultItemAad {
    char     magic[4];            // "CPV1"
    uint8_t  item_id[16];         // Identificador único do item
    uint32_t blob_offset;         // Offset esperado no ficheiro
};
static_assert(sizeof(CpVaultItemAad) == 24, "CpVaultItemAad must be 24 bytes");

#pragma pack(pop)

// ============================================================================
// Enumerações de Estado de Operação, Layout HID e Interface Gráfica
// ============================================================================
enum class KeyboardLayout : uint8_t {
    PT_BR_ABNT2 = 0,
    US_ANSI     = 1
};

enum class HidTransportMode : uint8_t {
    USB_HID = 0,
    BLE_HID = 1
};

enum class InjectSequenceType : uint8_t {
    FULL_AUTO_TYPE = 0, // Username -> TAB -> 100ms -> Password -> ENTER
    USERNAME_ONLY  = 1,
    PASSWORD_ONLY  = 2,
    TOTP_ONLY      = 3
};

enum class UiScreenState : uint8_t {
    SCREEN_UNLOCK       = 0,
    SCREEN_PROVISION_SD = 1,
    SCREEN_VAULT_LIST   = 2,
    SCREEN_ITEM_DETAIL  = 3,
    SCREEN_INJECTING    = 4,
    SCREEN_SYNC         = 5,
    SCREEN_NEW_ITEM     = 6
};

enum class SyncStepState : uint8_t {
    OFFLINE_IDLE      = 0,
    WIFI_CONNECTING   = 1,
    TIME_SYNC_SNTP    = 2,
    PUSHING_OUTBOX    = 3,
    CHECKING_REVISION = 4,
    STREAMING_VAULT   = 5,
    ATOMIC_COMMIT     = 6,
    SYNC_COMPLETE     = 7,
    SYNC_ERROR        = 8
};

/**
 * Configuração persistida na NVS do ESP32-S3 (a DEK é armazenada apenas em formato wrapped AES-GCM).
 */
struct DeviceSecurityConfig {
    bool           provisioned;
    char           device_id[40];
    char           wifi_ssid[36];
    char           wifi_password[64];
    char           tunnel_url[128];
    char           cf_access_client_id[64];
    char           cf_access_client_secret[96];
    char           mdns_service[32];
    uint8_t        hmac_secret[32];
    uint8_t        pbkdf2_salt[16];
    uint32_t       pbkdf2_iterations;
    uint8_t        wrapped_dek[32];
    uint8_t        wrap_iv[12];
    uint8_t        wrap_tag[16];
    uint8_t        failed_attempts;
    uint8_t        max_unlock_attempts;
    KeyboardLayout kb_layout;
    HidTransportMode hid_mode;
};

} // namespace sentinel

