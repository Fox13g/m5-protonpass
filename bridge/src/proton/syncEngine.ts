import type { BridgeConfig } from '../config.js';
import {
  buildCpVaultBinary,
  computeCanonicalRevisionHash,
  decodeOutboxPacket,
  type OutboxMutation,
  type VaultCredentialRecord,
} from '../crypto/vaultBuilder.js';
import { PassCliAdapter } from './passCliAdapter.js';

export interface SyncStatusResult {
  revisionHashHex: string;
  itemCount: number;
  syncedAtUnix: number;
  readOnly: boolean;
}

export interface PushSyncSummary {
  appliedCount: number;
  skippedStaleCount: number;
  newRevisionHashHex: string;
}

export class SyncEngine {
  private readonly cfg: BridgeConfig;
  private readonly adapter: PassCliAdapter;

  constructor(cfg: BridgeConfig, adapter?: PassCliAdapter) {
    this.cfg = cfg;
    this.adapter = adapter ?? new PassCliAdapter(cfg);
  }

  private getDekBuffer(): Buffer {
    return Buffer.from(this.cfg.SENTINEL_DEK_HEX, 'hex');
  }

  /**
   * Obtém os metadados leves da revisão atual do cofre sem transferir o binário completo.
   */
  async getStatus(): Promise<SyncStatusResult> {
    const items = await this.adapter.fetchCredentials();
    const revisionHash = computeCanonicalRevisionHash(items);
    return {
      revisionHashHex: revisionHash.toString('hex'),
      itemCount: items.length,
      syncedAtUnix: Math.floor(Date.now() / 1000),
      readOnly: this.cfg.READ_ONLY_MODE,
    };
  }

  /**
   * Compila a imagem binária `CPVAULT v1` cifrada com AES-256-GCM.
   * Caso `clientRevisionHashHex` seja idêntico ao hash atual, retorna `notModified: true`.
   */
  async buildEncryptedVaultForPull(clientRevisionHashHex?: string): Promise<{
    notModified: boolean;
    revisionHashHex: string;
    itemCount: number;
    vaultBuffer?: Buffer;
  }> {
    const items = await this.adapter.fetchCredentials();
    const currentHashHex = computeCanonicalRevisionHash(items).toString('hex');

    if (
      clientRevisionHashHex &&
      clientRevisionHashHex.toLowerCase() === currentHashHex.toLowerCase()
    ) {
      return {
        notModified: true,
        revisionHashHex: currentHashHex,
        itemCount: items.length,
      };
    }

    const built = buildCpVaultBinary({
      items,
      dek32: this.getDekBuffer(),
      readOnly: this.cfg.READ_ONLY_MODE,
    });

    return {
      notModified: false,
      revisionHashHex: built.revisionHashHex,
      itemCount: built.itemCount,
      vaultBuffer: built.vaultBuffer,
    };
  }

  /**
   * Processa um pacote `CPOUTBOX v1` enviado pelo Cardputer, resolvendo conflitos via
   * Last-Write-Wins (`mutation.updatedAt >= existing.updatedAt`).
   */
  async processEncryptedOutboxPush(encryptedPacket: Buffer): Promise<PushSyncSummary> {
    if (this.cfg.READ_ONLY_MODE) {
      throw new Error('Bridge Gateway configurado em READ_ONLY_MODE');
    }

    const envelope = decodeOutboxPacket(encryptedPacket, this.getDekBuffer());
    const currentItems = await this.adapter.fetchCredentials();
    const itemMap = new Map<string, VaultCredentialRecord>();
    for (const item of currentItems) {
      itemMap.set(`${item.vaultId}:${item.itemUuid}`, item);
    }

    let appliedCount = 0;
    let skippedStaleCount = 0;

    for (const mutation of envelope.mutations) {
      const key = `${mutation.vaultId}:${mutation.itemUuid}`;
      const existing = itemMap.get(key);

      if (existing && mutation.op === 'update' && mutation.updatedAt < existing.updatedAt) {
        // Arbitragem Last-Write-Wins: a versão na nuvem Proton é mais recente
        skippedStaleCount++;
        continue;
      }

      await this.adapter.applyMutation(mutation as OutboxMutation);
      appliedCount++;
    }

    const updatedItems = await this.adapter.fetchCredentials();
    const newRevisionHashHex = computeCanonicalRevisionHash(updatedItems).toString('hex');

    return {
      appliedCount,
      skippedStaleCount,
      newRevisionHashHex,
    };
  }
}

