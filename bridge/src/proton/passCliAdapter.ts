import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import type { BridgeConfig } from '../config.js';
import type { OutboxMutation, VaultCredentialRecord } from '../crypto/vaultBuilder.js';

const execFileAsync = promisify(execFile);

export interface ProtonVaultSummary {
  vaultId: string;
  name: string;
  itemCount?: number;
}

/**
 * Extrai e normaliza o segredo Base32 puro (RFC 4648) a partir de uma URI `otpauth://totp/...`
 * ou de uma string Base32 direta.
 */
export function extractCleanBase32Secret(rawTotp?: string | null): string {
  if (!rawTotp) return '';
  const trimmed = rawTotp.trim();
  if (!trimmed) return '';

  if (trimmed.toLowerCase().startsWith('otpauth://')) {
    try {
      const parsed = new URL(trimmed);
      const secretParam = parsed.searchParams.get('secret') || '';
      return secretParam.replace(/[\s-]/g, '').toUpperCase();
    } catch {
      const match = /[?&]secret=([^&]+)/i.exec(trimmed);
      if (match?.[1]) {
        return decodeURIComponent(match[1]).replace(/[\s-]/g, '').toUpperCase();
      }
      return '';
    }
  }

  return trimmed.replace(/[\s-]/g, '').toUpperCase();
}

export class PassCliAdapter {
  private readonly cfg: BridgeConfig;
  private readonly localFallbackPath: string;

  constructor(cfg: BridgeConfig) {
    this.cfg = cfg;
    this.localFallbackPath = path.join(this.cfg.STATE_DIR, 'fallback_vault_items.json');
  }

  private buildEnv(): NodeJS.ProcessEnv {
    const env: NodeJS.ProcessEnv = { ...process.env };
    if (this.cfg.PROTON_PASS_PAT) {
      env.PROTON_PASS_PAT = this.cfg.PROTON_PASS_PAT;
    }
    return env;
  }

  private getAllowedVaultFilterSet(): Set<string> {
    if (!this.cfg.ALLOWED_VAULTS || !this.cfg.ALLOWED_VAULTS.trim()) {
      return new Set();
    }
    return new Set(
      this.cfg.ALLOWED_VAULTS.split(',')
        .map((v) => v.trim().toLowerCase())
        .filter(Boolean)
    );
  }

  /**
   * Lista os cofres (Vaults) disponíveis no Proton Pass via `pass-cli vault list --output json`.
   */
  async listVaults(): Promise<ProtonVaultSummary[]> {
    try {
      const { stdout } = await execFileAsync(
        this.cfg.PASS_CLI_BIN,
        ['vault', 'list', '--output', 'json'],
        { env: this.buildEnv(), timeout: 25_000, maxBuffer: 16 * 1024 * 1024 }
      );
      const parsed = JSON.parse(stdout);
      const rawVaults: Record<string, unknown>[] = Array.isArray(parsed)
        ? parsed
        : Array.isArray(parsed?.vaults)
          ? parsed.vaults
          : [];

      return rawVaults.map((v) => ({
        vaultId: String(v.vault_id ?? v.id ?? v.vaultId ?? 'default'),
        name: String(v.name ?? v.title ?? 'Default Vault'),
        itemCount: typeof v.item_count === 'number' ? v.item_count : undefined,
      }));
    } catch {
      return [{ vaultId: 'default-vault', name: 'Personal' }];
    }
  }

  /**
   * Extrai todas as credenciais de login do Proton Pass (`pass-cli item list --output json`),
   * filtra pelos cofres autorizados e descarta metadados pesados desnecessários ao ESP32-S3.
   */
  async fetchCredentials(): Promise<VaultCredentialRecord[]> {
    const allowedSet = this.getAllowedVaultFilterSet();

    try {
      const { stdout } = await execFileAsync(
        this.cfg.PASS_CLI_BIN,
        ['item', 'list', '--output', 'json'],
        { env: this.buildEnv(), timeout: 30_000, maxBuffer: 32 * 1024 * 1024 }
      );

      const parsed = JSON.parse(stdout);
      const rawItems: Record<string, unknown>[] = Array.isArray(parsed)
        ? parsed
        : Array.isArray(parsed?.items)
          ? parsed.items
          : [];

      const normalized: VaultCredentialRecord[] = [];

      for (const raw of rawItems) {
        const vaultId = String(raw.vault_id ?? raw.vaultId ?? 'default');
        const vaultName = String(raw.vault_name ?? raw.vaultName ?? '').toLowerCase();

        if (allowedSet.size > 0) {
          const matchesId = allowedSet.has(vaultId.toLowerCase());
          const matchesName = vaultName && allowedSet.has(vaultName);
          if (!matchesId && !matchesName) {
            continue;
          }
        }

        const content = (raw.content ?? raw.data ?? raw) as Record<string, unknown>;
        const itemUuid = String(raw.item_id ?? raw.id ?? raw.uuid ?? content.id ?? '');
        const title = String(raw.title ?? content.title ?? content.name ?? '').trim();
        if (!itemUuid || !title) continue;

        const username = String(
          content.username ?? content.email ?? content.item_email ?? raw.username ?? ''
        ).trim();
        const password = String(content.password ?? raw.password ?? '');
        const rawTotp = String(content.totp_uri ?? content.totp ?? raw.totp_uri ?? raw.totp ?? '');
        const urlList = Array.isArray(content.urls) ? content.urls : [];
        const primaryUrl = String(content.url ?? urlList[0] ?? raw.url ?? '').trim();
        const notes = String(content.note ?? content.notes ?? raw.note ?? '').trim();
        const updatedAt = Number(
          raw.modify_time ?? raw.updated_at ?? raw.updatedAt ?? Math.floor(Date.now() / 1000)
        );

        normalized.push({
          vaultId,
          itemUuid,
          title,
          username,
          password,
          totpSecret: extractCleanBase32Secret(rawTotp),
          url: primaryUrl,
          notes,
          updatedAt,
        });
      }

      // Persiste snapshot local de fallback para resiliência caso a sessão CLI oscile
      this.saveLocalFallback(normalized);
      return normalized;
    } catch (err) {
      if (fs.existsSync(this.localFallbackPath)) {
        const raw = fs.readFileSync(this.localFallbackPath, 'utf8');
        const cached = JSON.parse(raw) as VaultCredentialRecord[];
        return cached.filter((item) => {
          if (allowedSet.size === 0) return true;
          return allowedSet.has(item.vaultId.toLowerCase());
        });
      }
      throw new Error(
        `Falha ao executar pass-cli (${(err as Error).message}). Verifique 'docker compose exec -it sentinel-bridge pass-cli login'.`
      );
    }
  }

  /**
   * Aplica uma mutação (criação ou atualização de credencial feita offline no Cardputer)
   * utilizando `pass-cli item update` / `pass-cli item create`.
   */
  async applyMutation(mutation: OutboxMutation): Promise<void> {
    if (this.cfg.READ_ONLY_MODE) {
      throw new Error('Bridge Gateway está configurado em READ_ONLY_MODE; mutações recusadas.');
    }

    const itemUuid = mutation.itemUuid || crypto.randomUUID();
    const vaultId = mutation.vaultId || 'default';

    try {
      if (mutation.op === 'create') {
        await execFileAsync(
          this.cfg.PASS_CLI_BIN,
          [
            'item',
            'create',
            'login',
            '--vault-id',
            vaultId,
            '--title',
            mutation.title,
            '--username',
            mutation.username,
            '--password',
            mutation.password,
            ...(mutation.url ? ['--url', mutation.url] : []),
            ...(mutation.totpSecret ? ['--totp-uri', mutation.totpSecret] : []),
            ...(mutation.notes ? ['--note', mutation.notes] : []),
          ],
          { env: this.buildEnv(), timeout: 25_000 }
        );
      } else {
        await execFileAsync(
          this.cfg.PASS_CLI_BIN,
          [
            'item',
            'update',
            '--vault-id',
            vaultId,
            '--item-id',
            itemUuid,
            '--title',
            mutation.title,
            '--username',
            mutation.username,
            '--password',
            mutation.password,
            ...(mutation.url ? ['--url', mutation.url] : []),
            ...(mutation.totpSecret ? ['--totp-uri', mutation.totpSecret] : []),
            ...(mutation.notes ? ['--note', mutation.notes] : []),
          ],
          { env: this.buildEnv(), timeout: 25_000 }
        );
      }
    } catch {
      // Atualiza também o store local caso estejamos em modo de fallback/teste
      this.upsertLocalFallback({
        vaultId,
        itemUuid,
        title: mutation.title,
        username: mutation.username,
        password: mutation.password,
        totpSecret: extractCleanBase32Secret(mutation.totpSecret),
        url: mutation.url,
        notes: mutation.notes,
        updatedAt: mutation.updatedAt,
      });
    }
  }

  public saveLocalFallback(items: VaultCredentialRecord[]): void {
    fs.mkdirSync(path.dirname(this.localFallbackPath), { recursive: true });
    fs.writeFileSync(this.localFallbackPath, JSON.stringify(items, null, 2), 'utf8');
  }

  private upsertLocalFallback(record: VaultCredentialRecord): void {
    let existing: VaultCredentialRecord[] = [];
    if (fs.existsSync(this.localFallbackPath)) {
      existing = JSON.parse(fs.readFileSync(this.localFallbackPath, 'utf8'));
    }
    const idx = existing.findIndex(
      (it) => it.vaultId === record.vaultId && it.itemUuid === record.itemUuid
    );
    if (idx >= 0) {
      existing[idx] = record;
    } else {
      existing.push(record);
    }
    this.saveLocalFallback(existing);
  }
}

