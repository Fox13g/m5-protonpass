# Modelo de Segurança e Ameaças (Threat Model)

O **Cardputer Pass Sentinel** foi projetado sob o princípio de **Defesa em Profundidade (Defense-in-Depth)**, assumindo que tanto o meio de transporte (redes Wi-Fi públicas e proxies intermediários) quanto o armazenamento físico portátil (roubo ou perda do M5Stack Cardputer) são ambientes hostis.

---

## 1. Garantias Criptográficas Ponta-a-Ponta (Zero-Trust E2EE)

| Camada | Primitivo Criptográfico | Objetivo de Segurança |
| :--- | :--- | :--- |
| **Autenticação Proton Cloud** | SRP + OpenPGP + bcrypt (via `pass-cli` no container Docker) | Preserva o modelo *zero-knowledge* nativo do Proton Pass sem expor a senha Proton ao Cardputer. |
| **Trânsito Cloudflare Tunnel** | TLS 1.3 (Borda) + Service Token + **AES-256-GCM (Payload E2EE)** | Mesmo que a Cloudflare termine o TLS público no túnel, o corpo da resposta é um binário `CPVAULT v1` cifrado com a `DEK` local. A Cloudflare jamais tem acesso a títulos, utilizadores ou senhas. |
| **Autenticação de Requisição** | `HMAC-SHA256` + Timestamp UTC + Nonce Único | Impede ataques de repetição (*Replay Attacks*), falsificação de requisições e enumeração não autorizada do endpoint do túnel. |
| **Proteção de Chave Local (NVS)** | **Envelope Encryption:** `PBKDF2-HMAC-SHA256` ($100.000$ iterações) $\rightarrow$ `KEK` embrulha a `DEK` via `AES-256-GCM` | A chave `DEK` nunca é gravada em texto limpo na Flash. Sem o PIN/Senha Mestra do utilizador, o dump físico da Flash é inutilizável. |
| **Integridade do Cofre em Disco** | `AES-256-GCM` com AAD vinculado ao cabeçalho e ao `item_id` + `blob_offset` | Qualquer corrupção de bits ou tentativa maliciosa de substituir blocos de senhas na partição LittleFS falha imediatamente na verificação da Tag GCM de 16 bytes. |
| **Segurança em Memória Volátil (SRAM)** | Decifra Sob Demanda (*Single-Item*) + `mbedtls_platform_zeroize()` | As senhas e segredos TOTP nunca permanecem decifrados em massa na SRAM; apenas 1 item é decifrado no instante da injeção e imediatamente zerado. |

---

## 2. Mitigação de Vetores de Ataque Específicos

### 2.1. Extração Física da Flash (Roubo do M5Stack Cardputer)
- **Cenário:** Um atacante obtém acesso físico ao M5Stack Cardputer desligado e conecta os pinos QIO Flash ou USB JTAG para extrair o conteúdo das partições `nvs` e `littlefs`.
- **Mitigação:**
  1. A partição `nvs` contém apenas `wrapped_dek` (32 bytes cifrados), `wrap_iv` (12 bytes), `wrap_tag` (16 bytes) e `pbkdf2_salt` (16 bytes).
  2. A desencriptação da `DEK` exige a `KEK`, derivada com $100.000$ iterações de `PBKDF2-HMAC-SHA256`.
  3. No dispositivo físico, o contador `failed_attempts` é incrementado na NVS **antes** de retornar o erro ao utilizador; após 10 tentativas inválidas consecutivas, o firmware sobrescreve a NVS e apaga `/littlefs/vault.bin` (*Auto-Wipe*).

### 2.2. Exposição do Ficheiro de Provisionamento no Cartão microSD
- **Cenário:** O utilizador utiliza um cartão microSD para importar rapidamente a configuração (`cardputer_provision.json`) e esquece de apagar o ficheiro no computador.
- **Mitigação:** Assim que o Cardputer importa `cardputer_provision.json` e protege a `DEK` com a Senha Mestra escolhida pelo utilizador, o firmware:
  1. Abre `/sd/cardputer_provision.json` em modo de escrita;
  2. Sobrescreve todos os bytes do ficheiro com zeros (`0x00`) e faz `flush()`;
  3. Remove a entrada do diretório (`SD.remove()`).

### 2.3. Keyloggers de Software no Computador Hospedeiro
- **Cenário:** O utilizador precisa autenticar-se numa estação de trabalho potencialmente monitorizada por malware de captura de área de transferência (*Clipboard Hijackers*) ou ganchos de navegador.
- **Mitigação:** O Cardputer apresenta-se ao barramento USB OTG nativo como um teclado HID padrão (`TinyUSB`), injetando apenas os *keycodes* necessários diretamente no campo focado, sem utilizar a área de transferência do sistema operativo e sem exigir a instalação de extensões de navegador ou agentes locais na máquina alvo.

