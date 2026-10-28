import fs from 'node:fs';
import type { SyncEngine } from '../proton/syncEngine.js';

/**
 * Canal auxiliar para sincronização ou provisionamento direto via porta USB Serial/CDC,
 * útil quando o utilizador está sem acesso Wi-Fi ou deseja transferir o cofre por cabo USB-C.
 */
export class UsbSerialSyncBridge {
  private readonly portPath: string;
  private readonly syncEngine: SyncEngine;

  constructor(portPath: string, syncEngine: SyncEngine) {
    this.portPath = portPath;
    this.syncEngine = syncEngine;
  }

  /**
   * Trata um comando de linha recebido via USB CDC (`SENTINEL_PULL` ou `SENTINEL_STATUS`)
   * e devolve a resposta codificada em Base64 com delimitador `\n`.
   */
  async handleCommandLine(line: string): Promise<string> {
    const trimmed = line.trim();
    if (trimmed === 'SENTINEL_STATUS') {
      const status = await this.syncEngine.getStatus();
      return `STATUS_OK:${JSON.stringify(status)}\n`;
    }

    if (trimmed.startsWith('SENTINEL_PULL')) {
      const parts = trimmed.split(':');
      const clientHash = parts[1]?.trim() || undefined;
      const result = await this.syncEngine.buildEncryptedVaultForPull(clientHash);
      if (result.notModified) {
        return `PULL_304:${result.revisionHashHex}\n`;
      }
      const b64 = result.vaultBuffer!.toString('base64');
      return `PULL_200:${result.revisionHashHex}:${b64}\n`;
    }

    return 'ERR:UNKNOWN_CMD\n';
  }

  isPortAvailable(): boolean {
    return Boolean(this.portPath && fs.existsSync(this.portPath));
  }
}

