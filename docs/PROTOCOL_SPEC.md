# Especificação do Protocolo e Formato Binário (`CPVAULT v1`)

Este documento define formalmente o formato binário indexado **`CPVAULT v1`** utilizado para persistência e trânsito de credenciais entre o **Bridge Gateway (Node.js)** e o **M5Stack Cardputer (ESP32-S3FN8)**, bem como o protocolo de sincronização incremental sob arquitetura **Zero-Trust End-to-End Encrypted (E2EE)**.

---

## 1. Motivação: O Limite de 512 KB de SRAM do ESP32-S3FN8

O módulo Stamp-S3 que equipa o M5Stack Cardputer dispõe de **512 KB de SRAM interna** e **não possui PSRAM externa**. Com o escalonador FreeRTOS, a pilha Wi-Fi 802.11, o contexto TLS e os dois sprites gráficos parciais (`M5GFX` de $240 \times 20$ e $240 \times 115$ píxeis), restam aproximadamente **180 KB a 220 KB de heap dinâmico contíguo**.

Se um cofre com 500 credenciais fosse armazenado como um único bloco JSON cifrado:
1. Seria necessário alocar simultaneamente o buffer cifrado, o buffer decifrado e a árvore de desserialização (`JsonDocument`), causando falha de alocação (*Out-Of-Memory*) ou fragmentação severa de heap.
2. Todas as senhas ficariam expostas em texto limpo na memória volátil durante toda a sessão de uso.

O formato **`CPVAULT v1`** resolve ambos os problemas através de:
- **Índice Compacto Separado:** Apenas os metadados de pesquisa (`title`, `username_hint`, `item_id` e `blob_offset`) são carregados em SRAM (~60 bytes por item em memória).
- **Decifra Sob Demanda (*On-Demand Single-Item Decryption*):** Cada registo de credencial completo é cifrado individualmente na Flash (LittleFS) com o seu próprio vetor de inicialização ($\text{IV}_{12\text{B}}$), etiqueta de autenticação ($\text{Tag}_{16\text{B}}$) e **Dados Autenticados Adicionais (AAD)**. O firmware faz `fseek()` diretamente para o registo selecionado, decifra apenas 564 bytes num buffer estático e executa `mbedtls_platform_zeroize()` assim que a injeção USB/BLE HID ou visualização TOTP termina.

---

## 2. Layout Binário do Arquivo `CPVAULT v1` (`vault.bin`)

Todos os campos multi-byte inteiros utilizam codificação **Little-Endian** (nativa do Xtensa LX7 do ESP32-S3 e de arquiteturas x86_64/ARM64 no servidor Node.js). Todas as strings de tamanho fixo são preenchidas com zeros (`\0` / zero-padded).

### 2.1. Cabeçalho Global (`CpVaultHeader` — 80 bytes)

| Offset | Tamanho | Tipo C++ | Campo | Descrição |
| :--- | :--- | :--- | :--- | :--- |
| `0x00` | 4 B | `char[4]` | `magic` | Assinatura fixa `"CPV1"` (`0x43, 0x50, 0x56, 0x31`) |
| `0x04` | 1 B | `uint8_t` | `version` | Versão do protocolo (`0x01`) |
| `0x05` | 1 B | `uint8_t` | `flags` | Bit 0: `read_only`; Bits 1..7: reservados (`0`) |
| `0x06` | 2 B | `uint16_t` | `item_count` | Total de credenciais no cofre ($N$) |
| `0x08` | 8 B | `uint64_t` | `synced_at_unix` | Época Unix (segundos UTC) da geração do pacote |
| `0x10` | 32 B | `uint8_t[32]` | `revision_hash` | Resumo SHA-256 canónico do estado do cofre |
| `0x30` | 12 B | `uint8_t[12]` | `index_iv` | Nonce/IV AES-256-GCM exclusivo da tabela de índice |
| `0x3C` | 16 B | `uint8_t[16]` | `index_tag` | Tag de autenticação GCM (16 B) da tabela de índice |
| `0x4C` | 4 B | `uint32_t` | `index_cipher_len`| Tamanho em bytes da tabela de índice cifrada ($N \times 116$) |

- **AAD da Tabela de Índice:** Os primeiros **48 bytes** do cabeçalho (`0x00` a `0x2F`, abrangendo `magic`, `version`, `flags`, `item_count`, `synced_at_unix` e `revision_hash`) são passados como **AAD (Additional Authenticated Data)** para o cálculo `AES-256-GCM` da tabela de índice. Qualquer adulteração em disco de qualquer byte do cabeçalho invalida imediatamente a autenticação criptográfica!

---

### 2.2. Entrada da Tabela de Índice (`CpVaultIndexEntry` — 116 bytes por item)

Imediatamente após o cabeçalho (no offset `0x50`), reside o bloco contíguo de $N \times 116$ bytes cifrado com `AES-256-GCM(DEK, index_iv, AAD=Header[0..47])`. Quando decifrada, cada entrada possui 116 bytes:

| Offset Relativo | Tamanho | Tipo C++ | Campo | Descrição |
| :--- | :--- | :--- | :--- | :--- |
| `+0x00` | 16 B | `uint8_t[16]` | `item_id` | Identificador único (primeiros 16 B do SHA-256 de `vault_id:item_uuid`) |
| `+0x10` | 40 B | `char[40]` | `title` | Título do serviço truncado em UTF-8 seguro (terminado em `\0`) |
| `+0x38` | 24 B | `char[24]` | `username_hint` | Nome de utilizador abreviado para listagem rápida |
| `+0x50` | 1 B | `uint8_t` | `item_flags` | Bit 0: `has_totp`; Bit 1: `modified_offline` |
| `+0x51` | 1 B | `uint8_t` | `reserved` | Alinhamento (`0x00`) |
| `+0x52` | 4 B | `uint32_t` | `blob_offset` | Offset absoluto no ficheiro `vault.bin` onde começa o bloco cifrado do item |
| `+0x56` | 2 B | `uint16_t` | `blob_cipher_len` | Tamanho do bloco cifrado do item (`564` bytes no v1) |
| `+0x58` | 12 B | `uint8_t[12]` | `blob_iv` | Nonce/IV AES-256-GCM exclusivo deste item |
| `+0x64` | 16 B | `uint8_t[16]` | `blob_tag` | Tag de autenticação GCM (16 B) do bloco deste item |

> **Otimização de Memória no ESP32-S3:** Ao desbloquear o cofre, o firmware lê as entradas `CpVaultIndexEntry` em streaming e retém em SRAM uma estrutura ainda mais enxuta (`RamIndexEntry` de 64 bytes) ou a própria entrada, permitindo manter 500 credenciais pesquisáveis em ~32 KB de SRAM.

---

### 2.3. Payload Individual de Credencial (`CpVaultItemPayload` — 564 bytes)

No offset `blob_offset` indicado pela entrada de índice, reside o ciphertext de 564 bytes do item, autenticado com:

$$\text{AAD}_{\text{Item}} = \text{Magic}_{4\text{B}} \parallel \text{item\_id}_{16\text{B}} \parallel \text{blob\_offset}_{4\text{B}}$$

Isto impede ataques de **troca de blocos (*block swapping*)** onde um atacante com acesso físico à Flash tentaria substituir o ciphertext da credencial A pelo ciphertext da credencial B.

| Offset Relativo | Tamanho | Tipo C++ | Campo | Descrição |
| :--- | :--- | :--- | :--- | :--- |
| `+0x00` | 8 B | `uint64_t` | `updated_at` | Carimbo Unix UTC da última modificação do registo |
| `+0x08` | 36 B | `char[36]` | `vault_id` | Identificador do cofre no Proton Pass |
| `+0x2C` | 36 B | `char[36]` | `item_uuid` | Identificador do item no Proton Pass |
| `+0x50` | 96 B | `char[96]` | `username` | Nome de utilizador ou e-mail completo |
| `+0xB0` | 128 B | `char[128]` | `password` | Palavra-passe completa |
| `+0x130` | 64 B | `char[64]` | `totp_secret` | Segredo TOTP em Base32 limpo (RFC 4648, maiúsculas, sem espaços) |
| `+0x170` | 68 B | `char[68]` | `url` | URL principal associada ao serviço |
| `+0x1B4` | 128 B | `char[128]` | `notes` | Notas seguras abreviadas |

---

## 3. Formato da Fila de Mutações Offline (`CPOUTBOX v1` — `outbox.bin`)

Quando o utilizador cria ou edita uma credencial no Cardputer em modo desconectado, a operação é acrescentada ao ficheiro `/littlefs/outbox.bin` e sincronizada no próximo ciclo de rede via `POST /api/v1/sync/push`.

Cada pacote enviado em `/api/v1/sync/push` tem o formato binário:
- `magic[4]`: `"CPO1"`
- `iv[12]`: IV aleatório de 12 bytes
- `tag[16]`: Tag GCM de 16 bytes (com `AAD = "CPO1"`)
- `ciphertext`: JSON minificado cifrado com a `DEK` contendo a lista de mutações:
  ```json
  {
    "deviceId": "cardputer-primary-01",
    "mutations": [
      {
        "op": "update",
        "vaultId": "...",
        "itemUuid": "...",
        "title": "GitHub Production",
        "username": "fox13g@proton.me",
        "password": "NovaSenhaGeradaNoCardputer!2026",
        "totpSecret": "JBSWY3DPEHPK3PXP",
        "url": "https://github.com",
        "notes": "Atualizado em campo",
        "updatedAt": 1793196000
      }
    ]
  }
  ```

---

## 4. Protocolo HTTP Zero-Trust (Autenticação HMAC-SHA256)

Todas as chamadas entre o Cardputer e o Bridge Gateway (através do Cloudflare Tunnel ou LAN) incluem os seguintes cabeçalhos:

1. **Cabeçalhos Opcionais Cloudflare Access (Borda Zero-Trust):**
   - `CF-Access-Client-Id: <client-id>`
   - `CF-Access-Client-Secret: <client-secret>`
2. **Cabeçalhos Obrigatórios de Autenticação do Dispositivo:**
   - `X-Sentinel-Device-Id: <device-id>`
   - `X-Sentinel-Timestamp: <unix-epoch-seconds>`
   - `X-Sentinel-Nonce: <32-hex-random>`
   - `X-Sentinel-Signature: <hex-hmac-sha256>`

### Cálculo Canónico da Assinatura:
$$\text{MensagemCanónica} = \text{METHOD} \parallel \text{"\n"} \parallel \text{PATH} \parallel \text{"\n"} \parallel \text{TIMESTAMP} \parallel \text{"\n"} \parallel \text{NONCE} \parallel \text{"\n"} \parallel \text{SHA256\_HEX}(\text{Body})$$

$$\text{X-Sentinel-Signature} = \text{HMAC-SHA256}_{\text{HMAC\_SECRET}}(\text{MensagemCanónica})$$

O servidor Node.js rejeita qualquer requisição cujo `Timestamp` divirja em mais de $\pm 60$ segundos do relógio UTC ou cujo `Nonce` já tenha sido utilizado dentro da janela de expiração.

