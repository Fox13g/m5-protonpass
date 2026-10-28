# ============================================================================
# Stage 1: Builder (Compila TypeScript para JavaScript de Produção)
# ============================================================================
FROM node:20-bookworm-slim AS builder

WORKDIR /app

COPY package.json tsconfig.json ./
COPY node_modules ./node_modules
COPY src ./src
RUN npm run build

# ============================================================================
# Stage 2: Runtime (Node.js 20 LTS + pass-cli + Utilizador Não-Root)
# ============================================================================
FROM node:20-bookworm-slim AS runtime

ARG PASS_CLI_VERSION=latest
ARG TARGETARCH=amd64

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    gnupg \
    libsecret-1-0 \
    tini \
    && rm -rf /var/lib/apt/lists/*

# Cria utilizador dedicado sem privilégios de root
RUN groupadd -g 10001 sentinel && \
    useradd -u 10001 -g sentinel -m -s /bin/bash sentinel

# Instala o binário oficial pass-cli do Proton Pass com verificação de arquitetura e provider FS para containers headless
RUN mkdir -p /usr/local/bin && \
    ARCH=$(uname -m) && \
    PASS_URL=$(curl -fsSL https://proton.me/download/pass-cli/versions.json | grep -o "https://proton.me/download/pass-cli/[^\"]*/pass-cli-linux-${ARCH}" | head -n 1) && \
    curl -fsSL "${PASS_URL:-https://proton.me/download/pass-cli/2.6.0/pass-cli-linux-${ARCH}}" -o /usr/local/bin/pass-cli.bin && \
    chmod 755 /usr/local/bin/pass-cli.bin && \
    printf '#!/usr/bin/env bash\nexport PROTON_PASS_KEY_PROVIDER="${PROTON_PASS_KEY_PROVIDER:-fs}"\nexport PROTON_PASS_NO_UPDATE_CHECK=1\nexec /usr/local/bin/pass-cli.bin "$@"\n' > /usr/local/bin/pass-cli && \
    chmod 755 /usr/local/bin/pass-cli && \
    /usr/local/bin/pass-cli --version

WORKDIR /app

COPY --from=builder /app/package.json ./package.json
COPY --from=builder /app/node_modules ./node_modules
COPY --from=builder /app/dist ./dist
COPY src ./src
COPY tsconfig.json ./tsconfig.json
COPY docker-entrypoint.sh /usr/local/bin/docker-entrypoint.sh

RUN chmod +x /usr/local/bin/docker-entrypoint.sh && \
    mkdir -p /app/data /app/provision-out /home/sentinel/.local/share/proton-pass-cli /home/sentinel/.config/proton-pass-cli && \
    chown -R sentinel:sentinel /app /home/sentinel

USER sentinel

EXPOSE 8443

ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/docker-entrypoint.sh"]
CMD ["node", "dist/index.js"]

