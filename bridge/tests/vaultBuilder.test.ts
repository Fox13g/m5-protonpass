import { describe, expect, it } from 'vitest';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {
  buildCpVaultBinary,
  CPVAULT_HEADER_SIZE,
  CPVAULT_INDEX_ENTRY_SIZE,
  CPVAULT_ITEM_PAYLOAD_SIZE,
  decryptSingleVaultItem,
   encodeOutboxPacket,
  parseAndVerifyCpVaultIndex,
  type VaultCredentialRecord,
} from '../src/crypto/vaultBuilder.js';
import { extractCleanBase32Secret, PassCliAdapter } from '../src/proton/passCliAdapter.js';
import { loadConfig } from '../src/config.js';
import { buildServer } from '../src/index.js';
import { computeRequestSignature } from '../src/crypto/aead.js';

describe('CPVAULT v1 Indexed Binary Format & End-to-End Sync Server', () => {
  const sampleItems: VaultCredentialRecord[] = [
    {
      vaultId: 'vault-personal-01',
      itemUuid: 'uuid-github-001',
      title: 'GitHub Production',
      username: 'fox13g@proton.me',
      password: 'Gh!Prod#2026_SecurePassword',
      totpSecret: extractCleanBase32Secret(
        'otpauth://totp/GitHub:fox13g?secret=JBSWY3DPEHPK3PXP&issuer=GitHub'
      ),
      url: 'https://github.com/login',
      notes: 'Chave principal SSH & deploy',
      updatedAt: 1793190000,
    },
    {
      vaultId: 'vault-work-02',
      itemUuid: 'uuid-aws-002',
      title: 'AWS Root Console',
      username: 'secops@empresa.com.br',
      password: 'Aws$Root!998877_Complex',
      totpSecret: 'KRSXG5CTMVRXEZLU',
      url: 'https://console.aws.amazon.com',
      notes: 'Conta produção sa-east-1',
      updatedAt: 1793195000,
    },
  ];

  it('builds CPVAULT v1 binary, verifies header/index AAD, and decrypts single items on demand', () => {
    const dek = crypto.randomBytes(32);
    const { vaultBuffer, revisionHashHex, itemCount } = buildCpVaultBinary({
      items: sampleItems,
      dek32: dek,
      syncedAtUnix: 1793196000,
      readOnly: false,
    });

    expect(itemCount).toBe(2);
    expect(vaultBuffer.length).toBe(
      CPVAULT_HEADER_SIZE + 2 * CPVAULT_INDEX_ENTRY_SIZE + 2 * CPVAULT_ITEM_PAYLOAD_SIZE
    );

    const parsed = parseAndVerifyCpVaultIndex(vaultBuffer, dek);
    expect(parsed.header.magic).toBe('CPV1');
    expect(parsed.header.itemCount).toBe(2);
    expect(parsed.header.revisionHashHex).toBe(revisionHashHex);
    expect(parsed.entries.length).toBe(2);

    // Ordenado alfabeticamente por título: "AWS Root Console" vem antes de "GitHub Production"
    expect(parsed.entries[0].title).toBe('AWS Root Console');
    expect(parsed.entries[1].title).toBe('GitHub Production');
    expect(parsed.entries[1].hasTotp).toBe(true);

    // Decifra sob demanda apenas o item #1 ("GitHub Production")
    const githubItem = decryptSingleVaultItem(vaultBuffer, parsed.entries[1], dek);
    expect(githubItem.username).toBe('fox13g@proton.me');
    expect(githubItem.password).toBe('Gh!Prod#2026_SecurePassword');
    expect(githubItem.totpSecret).toBe('JBSWY3DPEHPK3PXP');

    // Verifica que trocar os offsets entre blocos falha devido ao AAD vinculado ao item_id + offset
    const swappedEntry = {
      ...parsed.entries[1],
      blobOffset: parsed.entries[0].blobOffset,
      blobIv: parsed.entries[0].blobIv,
      blobTag: parsed.entries[0].blobTag,
    };
    expect(() => decryptSingleVaultItem(vaultBuffer, swappedEntry, dek)).toThrow();
  });

  it('handles Zero-Trust E2EE HTTP status, pull (200 & 304), and bidirectional outbox push (LWW)', async () => {
    const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'sentinel-bridge-test-'));
    const dekHex = crypto.randomBytes(32).toString('hex');
    const hmacSecretHex = crypto.randomBytes(32).toString('hex');
    const hmacSecret = Buffer.from(hmacSecretHex, 'hex');
    const dek = Buffer.from(dekHex, 'hex');

    const cfg = loadConfig({
      STATE_DIR: tmpDir,
      SENTINEL_DEK_HEX: dekHex,
      SENTINEL_HMAC_SECRET_HEX: hmacSecretHex,
      SENTINEL_DEVICE_ID: 'cardputer-test-01',
      READ_ONLY_MODE: 'false',
      PASS_CLI_BIN: '/nonexistent/pass-cli-mock',
    });

    const adapter = new PassCliAdapter(cfg);
    adapter.saveLocalFallback(sampleItems);

    const { app } = buildServer(cfg, adapter);

    const signHeaders = (method: string, urlPath: string, body: Buffer = Buffer.alloc(0)) => {
      const timestamp = String(Math.floor(Date.now() / 1000));
      const nonce = crypto.randomBytes(16).toString('hex');
      const sig = computeRequestSignature({
        method,
        path: urlPath,
        timestamp,
        nonce,
        body,
        hmacSecret,
      });
      return {
        'x-sentinel-device-id': 'cardputer-test-01',
        'x-sentinel-timestamp': timestamp,
        'x-sentinel-nonce': nonce,
        'x-sentinel-signature': sig,
      };
    };

    // 1. GET /api/v1/sync/status
    const statusRes = await app.inject({
      method: 'GET',
      url: '/api/v1/sync/status',
      headers: signHeaders('GET', '/api/v1/sync/status'),
    });
    expect(statusRes.statusCode).toBe(200);
    const statusBody = statusRes.json();
    expect(statusBody.itemCount).toBe(2);
    const initialRev = statusBody.revisionHashHex;

    // 2. GET /api/v1/sync/pull com mesmo hash -> 304 Not Modified
    const pull304 = await app.inject({
      method: 'GET',
      url: '/api/v1/sync/pull',
      headers: {
        ...signHeaders('GET', '/api/v1/sync/pull'),
        'x-local-revision': initialRev,
      },
    });
    expect(pull304.statusCode).toBe(304);

    // 3. GET /api/v1/sync/pull sem hash local -> 200 OK com binário CPVAULT v1
    const pull200 = await app.inject({
      method: 'GET',
      url: '/api/v1/sync/pull',
      headers: signHeaders('GET', '/api/v1/sync/pull'),
    });
    expect(pull200.statusCode).toBe(200);
    const vaultBin = pull200.rawPayload;
    const parsedVault = parseAndVerifyCpVaultIndex(vaultBin, dek);
    expect(parsedVault.header.itemCount).toBe(2);

    // 4. POST /api/v1/sync/push com mutação offline mais recente (Last-Write-Wins)
    const outboxPacket = encodeOutboxPacket(
      {
        deviceId: 'cardputer-test-01',
        mutations: [
          {
            op: 'update',
            vaultId: 'vault-personal-01',
            itemUuid: 'uuid-github-001',
            title: 'GitHub Production',
            username: 'fox13g@proton.me',
            password: 'UpdatedOfflineOnCardputer!2026',
            totpSecret: 'JBSWY3DPEHPK3PXP',
            url: 'https://github.com/login',
            notes: 'Editado no Cardputer',
            updatedAt: 1793200000, // Mais recente que 1793190000
          },
        ],
      },
      dek
    );

    const pushRes = await app.inject({
      method: 'POST',
      url: '/api/v1/sync/push',
      headers: {
        ...signHeaders('POST', '/api/v1/sync/push', outboxPacket),
        'content-type': 'application/octet-stream',
      },
      payload: outboxPacket,
    });
    expect(pushRes.statusCode).toBe(200);
    const pushSummary = pushRes.json();
    expect(pushSummary.appliedCount).toBe(1);
    expect(pushSummary.newRevisionHashHex).not.toBe(initialRev);

    await app.close();
    fs.rmSync(tmpDir, { recursive: true, force: true });
  });
});

