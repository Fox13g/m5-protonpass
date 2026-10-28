# Guia de Configuração e Uso (`M5Stack Cardputer` + `Proton Pass`)

Este guia descreve o passo a passo completo para importar as suas senhas do **Proton Pass** (Free, Plus ou Pro), gerar os cofres `.vault`, instalar o firmware via **M5Launcher** e utilizar os códigos **2FA (TOTP)** no **M5Stack Cardputer**.

---

## 1. Exportar do Proton Pass e Gerar os Cofres `.vault`

1. No navegador ou aplicativo **Proton Pass**, acesse **Configurações (⚙️) -> Exportar (Export)** e exporte em formato **JSON** (ou `.zip` sem criptografia PGP, ou `.pgp` com senha).
2. Copie o arquivo exportado para a pasta `./provision-out/` (ex.: `./provision-out/data.json`).
3. Execute o importador no container Docker:

```bash
docker exec -it sentinel-bridge npm run import
```

O comando irá importar todas as suas credenciais e códigos **2FA (TOTP)** e gerar automaticamente na pasta `./provision-out/vaults/`:
- `ProtonPass.vault` — Cofre unificado com todas as credenciais.
- `<NomeDoCofre>.vault` — Cofres individuais separados exatamente como no seu Proton Pass (ex.: `Personal.vault`, `Detran.vault`).

> **Alterar a Senha Mestre dos arquivos `.vault`:**
> Por padrão, a senha mestre é `9868` (ou o valor da variável `CARDPUTER_MASTER_PASSWORD`). Você também pode executar `docker exec -it sentinel-bridge npm run provision` para informar outra senha mestre interativamente.

---

## 2. Instalar o Firmware no M5Stack Cardputer

### Opção A — Via M5Launcher (Cartão microSD — Recomendado)
1. Copie o binário [`releases/CardputerPassSentinel-M5Launcher.bin`](../releases/CardputerPassSentinel-M5Launcher.bin) para o cartão microSD (FAT32).
2. Copie também a pasta `vaults/` (de `./provision-out/vaults/`) para o cartão microSD.
3. No **M5Launcher** do Cardputer, selecione `CardputerPassSentinel-M5Launcher.bin` e instale.

### Opção B — Compilar via PlatformIO (Docker ou Local)
```bash
docker run --rm \
  -v ~/.platformio:/root/.platformio \
  -v $(pwd):/workspace \
  -w /workspace/firmware \
  python:3.11-slim bash -c "pip install -q platformio && pio run -e m5stack-stamps3"
```

---

## 3. Como Configurar Data e Hora para o 2FA (TOTP)

Para que os códigos **2FA de 6 dígitos** (`RFC 6238`) sejam calculados com precisão, o relógio interno do Cardputer deve estar ajustado:

1. **Pelo Menu Principal (`Date & Time`):**
   - No carrossel inicial (ou no carrossel do cofre aberto), selecione **Date & Time** (ícone de relógio) e pressione **OK (`Enter`)**.
2. **Direto na Tela do `2FA` (Atalho `[t]`):**
   - Ao visualizar qualquer campo `2FA` (**View**), o rodapé exibe a hora atual e o atalho `[t] set date/time`. Pressione a tecla **`T`** para abrir o ajuste imediatamente.
3. **Em `Settings -> Date/Time`:**
   - Primeiro item da lista de configurações gerais.

### Formatos Aceitos no Ajuste de Relógio:
- **Apenas 4 dígitos (`HHMM`):** Ex.: digite `1545` para ajustar a hora para `15:45` mantendo a data atual.
- **Data e Hora completas (`DD/MM/YYYY HH:MM`):** Ex.: `28/10/2026 15:45`.
- O horário configurado é salvo na memória **NVS** do ESP32-S3.

---

## 4. Mapa de Teclas e Atalhos no M5Stack Cardputer

| Tecla / Atalho | Ação |
| :--- | :--- |
| **`,` (Esquerda) / `/` (Direita)** | Navega lateralmente nos menus em carrossel ou volta da lista |
| **`;` (Cima) / `.` (Baixo)** | Move o seletor para cima ou para baixo nas listas |
| **`Enter` (`OK`)** | Confirma a opção, abre a credencial ou seleciona o campo |
| **`Backspace` / `` ` `` (`ESC`)** | Apaga letra na busca ou volta para a tela anterior |
| **`A-Z`, `0-9` (na lista de senhas)** | Filtra/pesquisa instantaneamente entre as credenciais do cofre |
| **`U` (dentro de uma credencial)** | Abre diretamente o campo **User** (usuário/e-mail) |
| **`P` (dentro de uma credencial)** | Abre diretamente o campo **Pass** (senha) |
| **`T` (dentro de uma credencial)** | Abre diretamente o campo **2FA** (código TOTP de 6 dígitos) |
| **`N` (dentro de uma credencial)** | Abre diretamente o campo **Note** (notas / URL) |
| **`T` (na tela `View` do 2FA)** | Abre o ajuste rápido de **Data e Hora (`Date & Time`)** |
