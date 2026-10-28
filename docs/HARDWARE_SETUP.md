# Guia de Configuração de Hardware e Docker (`M5Stack Cardputer` + `Cloudflare Tunnel`)

Este guia descreve o passo a passo completo para colocar o **Bridge Gateway** a rodar em container Docker atrás de um **Cloudflare Tunnel (`cloudflared`)** e gravar/provisionar o firmware no **M5Stack Cardputer**.

---

## 1. Configurar o Cloudflare Tunnel (Para Redes Bloqueadas por CGNAT / Operadora)

1. Acesse o painel **Cloudflare Zero Trust** $\rightarrow$ **Networks** $\rightarrow$ **Tunnels**.
2. Clique em **Create a tunnel** $\rightarrow$ escolha **Cloudflared** e dê o nome `cardputer-sentinel`.
3. Copie o **Tunnel Token** gerado (`eyJhIjoi...`) e cole no ficheiro `.env` na variável `CLOUDFLARE_TUNNEL_TOKEN`.
4. Na aba **Public Hostname** do túnel, adicione uma rota:
   - **Subdomain:** `sentinel` (ex.: `sentinel.seudominio.com.br`)
   - **Service Type:** `HTTP`
   - **URL:** `sentinel-bridge:8443` *(nome do container Docker na rede interna `sentinel_net`)*
5. *(Recomendado)* Em **Access** $\rightarrow$ **Applications**, crie uma política de **Service Auth** para `sentinel.seudominio.com.br` com um **Service Token** (`CF-Access-Client-Id` e `CF-Access-Client-Secret`) e adicione ambos ao `.env`.

---

## 2. Subir o Servidor em Container Docker e Gerar o Provisionamento

Na raiz do projeto (`/home/fox13g/protonpass`):

```bash
# 1. Copie o template de variáveis de ambiente
cp .env.example .env

# 2. Construa e inicie os containers (Bridge Node.js + Cloudflared)
docker compose up -d --build

# 3. Autentique a sessão do Proton Pass (pass-cli) dentro do volume persistente do container
docker compose exec -it sentinel-bridge pass-cli login

# 4. Execute o assistente interativo de provisionamento
# Ele irá gerar novas chaves de 256 bits, atualizar o estado e exportar ./provision-out/cardputer_provision.json
docker compose exec -it sentinel-bridge npm run provision
```

Após concluir o passo 4, o ficheiro `./provision-out/cardputer_provision.json` estará disponível na sua máquina hospedeira!

---

## 3. Gravar o Firmware no M5Stack Cardputer

### Pré-requisitos
- **PlatformIO Core (CLI)** ou extensão **PlatformIO IDE** no VS Code.
- Cabo USB-C de dados conectado ao M5Stack Cardputer.

### Compilação e Upload
```bash
cd firmware

# Compilar o firmware
pio run -e m5stack-cardputer

# Colocar o Cardputer em modo de download (Segurar botão G0 ao ligar, se necessário) e gravar
pio run -e m5stack-cardputer -t upload
```

---

## 4. Primeiro Arranque e Provisionamento Automático via microSD

1. Formate um cartão microSD em **FAT32** e copie o ficheiro `./provision-out/cardputer_provision.json` para a raiz do cartão (`/cardputer_provision.json`).
2. Insira o cartão microSD no M5Stack Cardputer e ligue o aparelho.
3. O Cardputer exibirá:
   `[SD PROVISION DETECTADO]`
   `Defina sua Senha Mestra / PIN Local:`
4. Digite a sua Senha Mestra ou PIN (mínimo 4 caracteres) e pressione **`Enter`**.
5. O firmware irá:
   - Derivar a `KEK` via `PBKDF2-HMAC-SHA256` ($100.000$ iterações);
   - Cifrar a `DEK` com `AES-256-GCM` e salvá-la na partição NVS;
   - Salvar o SSID/Senha do Wi-Fi e a URL do Cloudflare Tunnel na NVS;
   - **Sobrescrever com zeros e apagar `cardputer_provision.json` do cartão microSD.**
6. Pressione **`Fn + S`** para realizar a primeira sincronização com o seu Proton Pass!

---

## 5. Mapa de Teclas e Atalhos no M5Stack Cardputer

| Atalho Físico | Ação |
| :--- | :--- |
| **`A-Z`, `0-9`** | Filtra instantaneamente a lista de credenciais pelo título ou utilizador |
| **`Backspace`** | Apaga caractere da pesquisa ou volta da tela de detalhes (`Esc`) |
| **`;` (Seta Cima) / `.` (Seta Baixo)** | Move o seletor para cima ou para baixo na lista de credenciais |
| **`Enter`** | **Auto-Type Completo:** Digita `Username` $\rightarrow$ `TAB` $\rightarrow$ `100ms` $\rightarrow$ `Password` $\rightarrow$ `ENTER` |
| **`Fn + U`** | Injeta apenas o **Nome de Utilizador** via USB/BLE HID |
| **`Fn + P`** | Injeta apenas a **Palavra-Passe** via USB/BLE HID |
| **`Fn + T`** | Calcula e injeta apenas o código **TOTP de 6 dígitos** (RFC 6238) |
| **`Fn + V`** | Abre o **Modal de Detalhes & TOTP ao vivo** (barra regressiva de 30s) |
| **`Fn + R`** | Na tela de detalhes, revela/oculta a senha em texto limpo no ecrã |
| **`Fn + S`** | Liga o Wi-Fi, sincroniza via Cloudflare Tunnel (`Push Outbox` + `Pull CPVAULT`) e desliga o Wi-Fi |
| **`Fn + L`** | Alterna o layout do teclado injetor entre **`PT-BR ABNT2`** e **`US ANSI`** |
| **`Fn + B`** | Alterna o modo de injeção entre **`USB HID` (Cabo)** e **`BLE HID` (Bluetooth)** |
| **`Fn + N`** | Cria uma nova credencial ou gera uma senha forte de 24 caracteres offline (adicionada ao `outbox.bin`) |

