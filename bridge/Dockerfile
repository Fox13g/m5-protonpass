# ============================================================================
# Stage 1: Builder (Compila TypeScript para JavaScript de Produção)
# ============================================================================
FROM node:20-bookworm-slim AS builder

WORKDIR /app

COPY package.json tsconfig.json ./
RUN npm install --no-audit --no-fund

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

# Instala o binário oficial pass-cli caso disponível para a arquitetura ou cria wrapper configurável
RUN mkdir -p /usr/local/bin && \
    (curl -fsSL "https://proton.me/download/pass-cli/linux-${TARGETARCH}/pass-cli" -o /usr/local/bin/pass-cli && chmod +x /usr/local/bin/pass-cli) || \
    echo '#!/usr/bin/env bash\necho "Aviso: Substitua /usr/local/bin/pass-cli pelo binário oficial do Proton Pass CLI ou monte via volume." >&2\nexit 1' > /usr/local/bin/pass-cli && chmod +x /usr/local/bin/pass-cli

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

