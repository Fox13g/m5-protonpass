import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import crypto from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { extractCleanBase32Secret } from './passCliAdapter.js';
import type { VaultCredentialRecord } from '../crypto/vaultBuilder.js';

/**
 * Extrai entradas de um arquivo ZIP em memória usando apenas `node:zlib` nativo.
 * Suporta métodos Store (0) e Deflate (8) via Central Directory (suporta Data Descriptors bit 3).
 */
export function extractZipEntries(zipBuffer: Buffer): Map<string, Buffer> {
  const entries = new Map<string, Buffer>();

  // Localiza End of Central Directory (EOCD) signature 0x06054b50
  let eocdOffset = -1;
  const minEocdOffset = Math.max(0, zipBuffer.length - 65557);
  for (let i = zipBuffer.length - 22; i >= minEocdOffset; i--) {
    if (zipBuffer.readUInt32LE(i) === 0x06054b50) {
      eocdOffset = i;
      break;
    }
  }

  if (eocdOffset !== -1) {
    const totalEntries = zipBuffer.readUInt16LE(eocdOffset + 10);
    const cdSize = zipBuffer.readUInt32LE(eocdOffset + 12);
    const cdOffset = zipBuffer.readUInt32LE(eocdOffset + 16);

    let ptr = cdOffset;
    const cdEnd = Math.min(zipBuffer.length, cdOffset + cdSize);
    for (let idx = 0; idx < totalEntries && ptr + 46 <= cdEnd; idx++) {
      if (zipBuffer.readUInt32LE(ptr) !== 0x02014b50) break;

      const compressionMethod = zipBuffer.readUInt16LE(ptr + 10);
      const compressedSize = zipBuffer.readUInt32LE(ptr + 20);
      const fileNameLen = zipBuffer.readUInt16LE(ptr + 28);
      const extraLen = zipBuffer.readUInt16LE(ptr + 30);
      const commentLen = zipBuffer.readUInt16LE(ptr + 32);
      const localHeaderOffset = zipBuffer.readUInt32LE(ptr + 42);

      const fileName = zipBuffer.subarray(ptr + 46, ptr + 46 + fileNameLen).toString('utf8');
      ptr += 46 + fileNameLen + extraLen + commentLen;

      if (fileName.endsWith('/')) continue;
      if (localHeaderOffset + 30 > zipBuffer.length) continue;
      if (zipBuffer.readUInt32LE(localHeaderOffset) !== 0x04034b50) continue;

      const localNameLen = zipBuffer.readUInt16LE(localHeaderOffset + 26);
      const localExtraLen = zipBuffer.readUInt16LE(localHeaderOffset + 28);
      const dataStart = localHeaderOffset + 30 + localNameLen + localExtraLen;
      const dataEnd = dataStart + compressedSize;
      if (dataEnd > zipBuffer.length) continue;

      const rawSlice = zipBuffer.subarray(dataStart, dataEnd);
      if (compressionMethod === 0) {
        entries.set(fileName, Buffer.from(rawSlice));
      } else if (compressionMethod === 8) {
        entries.set(fileName, zlib.inflateRawSync(rawSlice));
      }
    }
    return entries;
  }

  // Fallback para leitura sequencial de Local File Headers
  let offset = 0;
  while (offset + 30 <= zipBuffer.length) {
    if (zipBuffer.readUInt32LE(offset) !== 0x04034b50) break;
    const compressionMethod = zipBuffer.readUInt16LE(offset + 8);
    const compressedSize = zipBuffer.readUInt32LE(offset + 18);
    const fileNameLen = zipBuffer.readUInt16LE(offset + 26);
    const extraLen = zipBuffer.readUInt16LE(offset + 28);
    const fileName = zipBuffer.subarray(offset + 30, offset + 30 + fileNameLen).toString('utf8');
    const dataStart = offset + 30 + fileNameLen + extraLen;
    const dataEnd = dataStart + compressedSize;
    if (dataEnd > zipBuffer.length) break;

    if (!fileName.endsWith('/')) {
      const rawSlice = zipBuffer.subarray(dataStart, dataEnd);
      if (compressionMethod === 0) {
        entries.set(fileName, Buffer.from(rawSlice));
      } else if (compressionMethod === 8) {
        entries.set(fileName, zlib.inflateRawSync(rawSlice));
      }
    }
    offset = dataEnd;
  }

  return entries;
}

/**
 * Faz o parse de um documento JSON exportado pelo Proton Pass (`data.json`).
 * Suporta tanto `vaults` como objeto indexado por `vaultId` (padrão Web/Extensão Proton Pass)
 * quanto `vaults` como array ou lista direta de itens.
 */
export function parseProtonPassExportJson(jsonText: string): VaultCredentialRecord[] {
  const parsed = JSON.parse(jsonText) as Record<string, unknown>;
  const results: VaultCredentialRecord[] = [];

  const processVaultItems = (
    vaultId: string,
    vaultName: string,
    items: unknown[]
  ): void => {
    for (const rawItem of items) {
      if (!rawItem || typeof rawItem !== 'object') continue;
      const item = rawItem as Record<string, unknown>;

      // Ignora itens movidos para a lixeira (state === 2 ou state === 'trashed')
      if (item.state === 2 || item.state === 'trashed') continue;

      const dataObj = (item.data && typeof item.data === 'object' ? item.data : item) as Record<
        string,
        unknown
      >;
      const itemType = String(dataObj.type ?? item.type ?? 'login').toLowerCase();
      if (itemType !== 'login' && itemType !== 'password') continue;

      const metadataObj = (
        dataObj.metadata && typeof dataObj.metadata === 'object'
          ? dataObj.metadata
          : item.metadata && typeof item.metadata === 'object'
            ? item.metadata
            : {}
      ) as Record<string, unknown>;

      const contentObj = (
        dataObj.content && typeof dataObj.content === 'object'
          ? dataObj.content
          : item.content && typeof item.content === 'object'
            ? item.content
            : dataObj
      ) as Record<string, unknown>;

      const itemUuid = String(
        item.itemId ??
          item.item_id ??
          metadataObj.itemUuid ??
          item.id ??
          item.uuid ??
          crypto.randomUUID()
      ).trim();

      const title = String(
        metadataObj.name ?? item.title ?? dataObj.title ?? contentObj.title ?? ''
      ).trim();
      if (!title) continue;

      const itemUsername = String(contentObj.itemUsername ?? contentObj.username ?? '').trim();
      const itemEmail = String(contentObj.itemEmail ?? contentObj.email ?? '').trim();
      const username = itemUsername || itemEmail || String(metadataObj.username ?? '').trim();

      const password = String(
        contentObj.password ?? metadataObj.password ?? item.password ?? ''
      );

      let rawTotp = String(
        contentObj.totpUri ??
          contentObj.totp_uri ??
          contentObj.totp ??
          metadataObj.totp ??
          item.totp ??
          ''
      ).trim();

      // Verifica também extraFields caso o TOTP esteja num campo extra
      const extraFields = Array.isArray(dataObj.extraFields)
        ? dataObj.extraFields
        : Array.isArray(item.extra_fields)
          ? item.extra_fields
          : [];
      if (!rawTotp && extraFields.length > 0) {
        for (const ef of extraFields) {
          if (!ef || typeof ef !== 'object') continue;
          const efRec = ef as Record<string, unknown>;
          if (efRec.type === 'totp') {
            const efData = (efRec.data ?? efRec) as Record<string, unknown>;
            if (efData.totpUri || efData.content) {
              rawTotp = String(efData.totpUri ?? efData.content ?? '').trim();
              break;
            }
          }
        }
      }

      const urls = Array.isArray(contentObj.urls)
        ? contentObj.urls
        : Array.isArray(metadataObj.urls)
          ? metadataObj.urls
          : [];
      const primaryUrl = String(contentObj.url ?? urls[0] ?? item.url ?? '').trim();

      const notes = String(
        metadataObj.note ?? contentObj.note ?? item.note ?? item.notes ?? ''
      ).trim();

      const updatedAt = Number(
        item.modifyTime ??
          item.modify_time ??
          item.createTime ??
          item.create_time ??
          Math.floor(Date.now() / 1000)
      );

      results.push({
        vaultId: vaultName || vaultId || 'Personal',
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
  };

  if (Array.isArray(parsed)) {
    processVaultItems('default', 'Personal', parsed);
  } else if (parsed.vaults && typeof parsed.vaults === 'object') {
    if (Array.isArray(parsed.vaults)) {
      for (const v of parsed.vaults) {
        if (!v || typeof v !== 'object') continue;
        const vObj = v as Record<string, unknown>;
        const vId = String(vObj.vault_id ?? vObj.vaultId ?? vObj.id ?? 'default');
        const vName = String(vObj.name ?? vId);
        const vItems = Array.isArray(vObj.items) ? vObj.items : [];
        processVaultItems(vId, vName, vItems);
      }
    } else {
      for (const [vId, vVal] of Object.entries(parsed.vaults as Record<string, unknown>)) {
        if (!vVal || typeof vVal !== 'object') continue;
        const vObj = vVal as Record<string, unknown>;
        const vName = String(vObj.name ?? vId);
        const vItems = Array.isArray(vObj.items) ? vObj.items : [];
        processVaultItems(vId, vName, vItems);
      }
    }
  } else if (Array.isArray(parsed.items)) {
    processVaultItems('default', 'Personal', parsed.items);
  }

  return results;
}

/**
 * Faz o parse de um CSV exportado pelo Proton Pass (`name,url,email,username,password,note,totp,createTime,modifyTime,vault`).
 */
export function parseProtonPassExportCsv(csvText: string): VaultCredentialRecord[] {
  const rows: string[][] = [];
  let currentRow: string[] = [];
  let currentField = '';
  let inQuotes = false;

  for (let i = 0; i < csvText.length; i++) {
    const ch = csvText[i];
    const next = csvText[i + 1];

    if (inQuotes) {
      if (ch === '"' && next === '"') {
        currentField += '"';
        i++;
      } else if (ch === '"') {
        inQuotes = false;
      } else {
        currentField += ch;
      }
    } else {
      if (ch === '"') {
        inQuotes = true;
      } else if (ch === ',') {
        currentRow.push(currentField);
        currentField = '';
      } else if (ch === '\r' && next === '\n') {
        currentRow.push(currentField);
        rows.push(currentRow);
        currentRow = [];
        currentField = '';
        i++;
      } else if (ch === '\n') {
        currentRow.push(currentField);
        rows.push(currentRow);
        currentRow = [];
        currentField = '';
      } else {
        currentField += ch;
      }
    }
  }
  if (currentField.length > 0 || currentRow.length > 0) {
    currentRow.push(currentField);
    rows.push(currentRow);
  }

  if (rows.length < 2) return [];
  const headers = rows[0].map((h) => h.trim().toLowerCase());
  const idxOf = (names: string[]): number => {
    for (const n of names) {
      const i = headers.indexOf(n);
      if (i !== -1) return i;
    }
    return -1;
  };

  const nameIdx = idxOf(['name', 'title']);
  const urlIdx = idxOf(['url', 'website']);
  const emailIdx = idxOf(['email', 'itememail']);
  const userIdx = idxOf(['username', 'itemusername', 'login_username']);
  const passIdx = idxOf(['password', 'login_password']);
  const noteIdx = idxOf(['note', 'notes']);
  const totpIdx = idxOf(['totp', 'login_totp', 'totpuri']);
  const modIdx = idxOf(['modifytime', 'modify_time', 'updatedat']);
  const vaultIdx = idxOf(['vault', 'folder']);
  const typeIdx = idxOf(['type']);

  const records: VaultCredentialRecord[] = [];
  for (let r = 1; r < rows.length; r++) {
    const row = rows[r];
    if (row.length <= 1 && !row[0]?.trim()) continue;
    if (typeIdx !== -1 && row[typeIdx] && row[typeIdx].toLowerCase() !== 'login') {
      continue;
    }

    const title = (nameIdx !== -1 ? row[nameIdx] : '')?.trim() || '';
    if (!title) continue;

    const username =
      (userIdx !== -1 ? row[userIdx]?.trim() : '') ||
      (emailIdx !== -1 ? row[emailIdx]?.trim() : '') ||
      '';
    const password = (passIdx !== -1 ? row[passIdx] : '') ?? '';
    const url = (urlIdx !== -1 ? row[urlIdx]?.trim() : '') || '';
    const notes = (noteIdx !== -1 ? row[noteIdx]?.trim() : '') || '';
    const rawTotp = (totpIdx !== -1 ? row[totpIdx]?.trim() : '') || '';
    const vaultId = (vaultIdx !== -1 ? row[vaultIdx]?.trim() : '') || 'Personal';
    const updatedAt =
      modIdx !== -1 && Number(row[modIdx]) > 0
        ? Number(row[modIdx])
        : Math.floor(Date.now() / 1000);

    const deterministicHash = crypto
      .createHash('sha256')
      .update(`${vaultId}:${title}:${username}:${url}`)
      .digest('hex')
      .slice(0, 32);

    records.push({
      vaultId,
      itemUuid: deterministicHash,
      title,
      username,
      password,
      totpSecret: extractCleanBase32Secret(rawTotp),
      url,
      notes,
      updatedAt,
    });
  }

  return records;
}

/**
 * Importa um arquivo de exportação do Proton Pass (`.zip`, `.json`, `.csv` ou `.pgp`).
 */
export function importProtonPassFile(
  filePath: string,
  pgpPassphrase?: string
): VaultCredentialRecord[] {
  const ext = path.extname(filePath).toLowerCase();
  let buf = fs.readFileSync(filePath);

  // Se for arquivo criptografado OpenPGP (.pgp / .gpg)
  if (ext === '.pgp' || ext === '.gpg' || buf.toString('utf8', 0, 30).includes('BEGIN PGP MESSAGE')) {
    const args = ['--batch', '--yes', '--decrypt'];
    if (pgpPassphrase) {
      args.push('--pinentry-mode', 'loopback', '--passphrase', pgpPassphrase);
    }
    args.push(filePath);
    buf = execFileSync('gpg', args, { maxBuffer: 64 * 1024 * 1024 });
  }

  // Se for um arquivo ZIP (assinatura PK\x03\x04)
  if (buf.length >= 4 && buf.readUInt32LE(0) === 0x04034b50) {
    const entries = extractZipEntries(buf);
    for (const [entryName, entryBuf] of entries.entries()) {
      if (entryName.endsWith('.json')) {
        return parseProtonPassExportJson(entryBuf.toString('utf8'));
      }
      if (entryName.endsWith('.csv')) {
        return parseProtonPassExportCsv(entryBuf.toString('utf8'));
      }
    }
    throw new Error(`Arquivo ZIP '${filePath}' não contém data.json ou .csv do Proton Pass.`);
  }

  const text = buf.toString('utf8').trim();
  if (text.startsWith('{') || text.startsWith('[')) {
    return parseProtonPassExportJson(text);
  }

  return parseProtonPassExportCsv(text);
}

