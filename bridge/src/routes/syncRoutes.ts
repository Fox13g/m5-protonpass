import type { FastifyInstance, FastifyReply, FastifyRequest } from 'fastify';
import type { BridgeConfig } from '../config.js';
import { verifyRequestSignature } from '../crypto/aead.js';
import type { SyncEngine } from '../proton/syncEngine.js';

export function registerSyncRoutes(
  app: FastifyInstance,
  cfg: BridgeConfig,
  syncEngine: SyncEngine
): void {
  const usedNonces = new Map<string, number>();
  const hmacSecret = Buffer.from(cfg.SENTINEL_HMAC_SECRET_HEX, 'hex');

  // Regista parser para payloads binários (`application/octet-stream`) enviados em /push
  app.addContentTypeParser(
    'application/octet-stream',
    { parseAs: 'buffer', bodyLimit: 2 * 1024 * 1024 },
    (_req, body, done) => {
      done(null, body);
    }
  );

  // Endpoint público leve de Healthcheck para o Docker / Cloudflared
  app.get('/healthz', async () => {
    return {
      status: 'ok',
      service: 'cardputer-pass-sentinel-bridge',
      protocol: 'CPVAULT_V1',
      readOnly: cfg.READ_ONLY_MODE,
      timestamp: Math.floor(Date.now() / 1000),
    };
  });

  // Hook de Autenticação Zero-Trust HMAC-SHA256 para todas as rotas /api/v1/*
  app.addHook('preHandler', async (req: FastifyRequest, reply: FastifyReply) => {
    if (!req.url.startsWith('/api/v1/')) {
      return;
    }

    const deviceId = String(req.headers['x-sentinel-device-id'] ?? '');
    const timestamp = String(req.headers['x-sentinel-timestamp'] ?? '');
    const nonce = String(req.headers['x-sentinel-nonce'] ?? '');
    const signatureHex = String(req.headers['x-sentinel-signature'] ?? '');

    if (!deviceId || !timestamp || !nonce || !signatureHex) {
      return reply.code(401).send({
        error: 'Unauthorized',
        message: 'Missing required X-Sentinel-* authentication headers',
      });
    }

    if (deviceId !== cfg.SENTINEL_DEVICE_ID) {
      return reply.code(403).send({
        error: 'Forbidden',
        message: 'Unrecognized X-Sentinel-Device-Id',
      });
    }

    const rawBody = Buffer.isBuffer(req.body)
      ? req.body
      : req.body
        ? Buffer.from(JSON.stringify(req.body), 'utf8')
        : Buffer.alloc(0);

    const urlPath = req.url.split('?')[0];
    const verification = verifyRequestSignature({
      method: req.method,
      path: urlPath,
      timestamp,
      nonce,
      body: rawBody,
      signatureHex,
      hmacSecret,
      maxClockSkewSec: cfg.SENTINEL_MAX_CLOCK_SKEW_SEC,
      usedNonces,
    });

    if (!verification.valid) {
      req.log.warn({ reason: verification.reason, deviceId }, 'Rejected request signature');
      return reply.code(401).send({
        error: 'Unauthorized',
        message: verification.reason ?? 'Signature verification failed',
      });
    }
  });

  /**
   * GET /api/v1/sync/status
   * Retorna o carimbo temporal e o resumo SHA-256 do cofre sem movimentar os blocos cifrados.
   */
  app.get('/api/v1/sync/status', async (_req, reply) => {
    const status = await syncEngine.getStatus();
    reply.header('X-Vault-Revision', status.revisionHashHex);
    reply.header('X-Vault-Item-Count', String(status.itemCount));
    return status;
  });

  /**
   * GET /api/v1/sync/pull
   * Retorna `304 Not Modified` se `If-None-Match` ou `X-Local-Revision` coincidir com o hash atual,
   * ou retorna o stream binário `CPVAULT v1` cifrado ponta-a-ponta com AES-256-GCM.
   */
  app.get('/api/v1/sync/pull', async (req, reply) => {
    const clientRev = String(
      req.headers['x-local-revision'] ?? req.headers['if-none-match'] ?? ''
    ).replace(/"/g, '');

    const result = await syncEngine.buildEncryptedVaultForPull(clientRev || undefined);

    reply.header('X-Vault-Revision', result.revisionHashHex);
    reply.header('X-Vault-Item-Count', String(result.itemCount));

    if (result.notModified) {
      return reply.code(304).send();
    }

    return reply
      .code(200)
      .header('Content-Type', 'application/octet-stream')
      .header('Content-Length', String(result.vaultBuffer!.length))
      .send(result.vaultBuffer);
  });

  /**
   * POST /api/v1/sync/push
   * Recebe o pacote cifrado `CPOUTBOX v1` (`application/octet-stream`) do Cardputer,
   * consolida no Proton Pass via `pass-cli` (Last-Write-Wins) e devolve o novo hash.
   */
  app.post('/api/v1/sync/push', async (req, reply) => {
    if (cfg.READ_ONLY_MODE) {
      return reply.code(403).send({
        error: 'ReadOnlyMode',
        message: 'Bridge Gateway is running in read-only mode',
      });
    }

    const bodyBuffer = Buffer.isBuffer(req.body) ? req.body : Buffer.alloc(0);
    if (bodyBuffer.length < 32) {
      return reply.code(400).send({
        error: 'BadRequest',
        message: 'Invalid CPOUTBOX binary payload',
      });
    }

    const summary = await syncEngine.processEncryptedOutboxPush(bodyBuffer);
    reply.header('X-Vault-Revision', summary.newRevisionHashHex);
    return summary;
  });
}

