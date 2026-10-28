import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { decryptAes256Gcm, encryptAes256Gcm, sha256 } from './aead.js';

export const CPVAULT_MAGIC = 'CPV1';
export const CPVAULT_VERSION = 1;
export const CPVAULT_HEADER_SIZE = 80;
export const CPVAULT_HEADER_AAD_SIZE = 48;
export const CPVAULT_INDEX_ENTRY_SIZE = 116;
export const CPVAULT_ITEM_PAYLOAD_SIZE = 564;

export const CPOUTBOX_MAGIC = 'CPO1';

export interface VaultCredentialRecord {
  vaultId: string;
  itemUuid: string;
  title: string;
  username: string;
  password: string;
  totpSecret: string; // Base32 limpo (ex: JBSWY3DPEHPK3PXP)
  url: string;
  notes: string;
  updatedAt: number; // Época Unix em segundos
  modifiedOffline?: boolean;
}

export interface ParsedVaultHeader {
  magic: string;
  version: number;
  readOnly: boolean;
  itemCount: number;
  syncedAtUnix: number;
  revisionHashHex: string;
  indexIv: Buffer;
  indexTag: Buffer;
  indexCipherLen: number;
}

export interface ParsedVaultIndexEntry {
  itemIdHex: string;
  itemIdBytes: Buffer;
  title: string;
  usernameHint: string;
  hasTotp: boolean;
  modifiedOffline: boolean;
  blobOffset: number;
  blobCipherLen: number;
  blobIv: Buffer;
  blobTag: Buffer;
}

/**
 * Escreve uma string UTF-8 truncada com segurança num slice de tamanho fixo,
 * garantindo sempre terminador nulo (`\0`) no final.
 */
function writeFixedCString(target: Buffer, offset: number, maxLength: number, value: string): void {
  target.fill(0, offset, offset + maxLength);
  if (!value) return;
  const utf8 = Buffer.from(value, 'utf8');
  const copyLen = Math.min(utf8.length, maxLength - 1);
  utf8.copy(target, offset, 0, copyLen);
}

/**
 * Lê uma string UTF-8 terminada em `\0` de um slice de tamanho fixo.
 */
function readFixedCString(source: Buffer, offset: number, maxLength: number): string {
  const slice = source.subarray(offset, offset + maxLength);
  const nullIdx = slice.indexOf(0);
  const end = nullIdx === -1 ? maxLength : nullIdx;
  return slice.subarray(0, end).toString('utf8');
}

/**
 * Calcula o identificador binário determinístico de 16 bytes do item.
 */
export function computeItemId16(vaultId: string, itemUuid: string): Buffer {
  return sha256(`${vaultId}:${itemUuid}`).subarray(0, 16);
}

/**
 * Calcula o hash canónico SHA-256 do estado do cofre (independente de IVs aleatórios),
 * permitindo ao Cardputer comparar se houve alterações reais (`HTTP 304 Not Modified`).
 */
export function computeCanonicalRevisionHash(items: VaultCredentialRecord[]): Buffer {
  const sorted = [...items].sort((a, b) =>
    `${a.vaultId}:${a.itemUuid}`.localeCompare(`${b.vaultId}:${b.itemUuid}`)
  );
  const canonicalJson = JSON.stringify(
    sorted.map((it) => ({
      v: it.vaultId,
      u: it.itemUuid,
      t: it.title,
      usr: it.username,
      pwd: it.password,
      otp: it.totpSecret,
      url: it.url,
      n: it.notes,
      ts: it.updatedAt,
    }))
  );
  return sha256(canonicalJson);
}

/**
 * Constrói a imagem binária completa `CPVAULT v1` cifrada com AES-256-GCM por registo e no índice.
 */
export function buildCpVaultBinary(params: {
  items: VaultCredentialRecord[];
  dek32: Buffer;
  syncedAtUnix?: number;
  readOnly?: boolean;
}): {
  vaultBuffer: Buffer;
  revisionHashHex: string;
  itemCount: number;
} {
  if (params.dek32.length !== 32) {
    throw new Error('DEK must be 32 bytes (256 bits)');
  }

  // Ordena alfabeticamente por título para navegação imediata no Cardputer
  const sortedItems = [...params.items].sort((a, b) =>
    a.title.localeCompare(b.title, undefined, { sensitivity: 'base' })
  );

  const itemCount = sortedItems.length;
  const syncedAt = params.syncedAtUnix ?? Math.floor(Date.now() / 1000);
  const revisionHash = computeCanonicalRevisionHash(sortedItems);
  const indexPlainLen = itemCount * CPVAULT_INDEX_ENTRY_SIZE;
  const blobsStartOffset = CPVAULT_HEADER_SIZE + indexPlainLen;

  const indexPlainBuffer = Buffer.alloc(indexPlainLen, 0);
  const encryptedBlobs: Buffer[] = [];

  for (let i = 0; i < itemCount; i++) {
    const item = sortedItems[i];
    const itemId16 = computeItemId16(item.vaultId, item.itemUuid);
    const blobOffset = blobsStartOffset + i * CPVAULT_ITEM_PAYLOAD_SIZE;

    // 1. Serializa o payload limpo de 564 bytes do item
    const payloadPlain = Buffer.alloc(CPVAULT_ITEM_PAYLOAD_SIZE, 0);
    payloadPlain.writeBigUInt64LE(BigInt(item.updatedAt), 0x00);
    writeFixedCString(payloadPlain, 0x08, 36, item.vaultId);
    writeFixedCString(payloadPlain, 0x2c, 36, item.itemUuid);
    writeFixedCString(payloadPlain, 0x50, 96, item.username);
    writeFixedCString(payloadPlain, 0xb0, 128, item.password);
    writeFixedCString(payloadPlain, 0x130, 64, item.totpSecret);
    writeFixedCString(payloadPlain, 0x170, 68, item.url);
    writeFixedCString(payloadPlain, 0x1b4, 128, item.notes);

    // 2. Constrói AAD de 24 bytes exclusivo do item: "CPV1" (4B) || item_id (16B) || blob_offset (4B)
    const itemAad = Buffer.alloc(24, 0);
    itemAad.write(CPVAULT_MAGIC, 0, 4, 'ascii');
    itemId16.copy(itemAad, 4);
    itemAad.writeUInt32LE(blobOffset, 20);

    // 3. Cifra o payload do item com IV único de 12 bytes
    const encryptedItem = encryptAes256Gcm(payloadPlain, params.dek32, itemAad);
    encryptedBlobs.push(encryptedItem.ciphertext);

    // 4. Preenche a entrada correspondente (116 bytes) na tabela de índice
    const idxOffset = i * CPVAULT_INDEX_ENTRY_SIZE;
    itemId16.copy(indexPlainBuffer, idxOffset + 0x00);
    writeFixedCString(indexPlainBuffer, idxOffset + 0x10, 40, item.title);
    writeFixedCString(indexPlainBuffer, idxOffset + 0x38, 24, item.username);

    let itemFlags = 0;
    if (item.totpSecret && item.totpSecret.trim().length > 0) itemFlags |= 0x01;
    if (item.modifiedOffline) itemFlags |= 0x02;

    indexPlainBuffer.writeUInt8(itemFlags, idxOffset + 0x50);
    indexPlainBuffer.writeUInt8(0, idxOffset + 0x51); // reserved
    indexPlainBuffer.writeUInt32LE(blobOffset, idxOffset + 0x52);
    indexPlainBuffer.writeUInt16LE(encryptedItem.ciphertext.length, idxOffset + 0x56);
    encryptedItem.iv.copy(indexPlainBuffer, idxOffset + 0x58);
    encryptedItem.tag.copy(indexPlainBuffer, idxOffset + 0x64);
  }

  // 5. Constrói os primeiros 48 bytes do Header (usados como AAD do bloco de índice)
  const headerBuffer = Buffer.alloc(CPVAULT_HEADER_SIZE, 0);
  headerBuffer.write(CPVAULT_MAGIC, 0x00, 4, 'ascii');
  headerBuffer.writeUInt8(CPVAULT_VERSION, 0x04);
  headerBuffer.writeUInt8(params.readOnly ? 0x01 : 0x00, 0x05);
  headerBuffer.writeUInt16LE(itemCount, 0x06);
  headerBuffer.writeBigUInt64LE(BigInt(syncedAt), 0x08);
  revisionHash.copy(headerBuffer, 0x10);

  const headerAad = headerBuffer.subarray(0, CPVAULT_HEADER_AAD_SIZE);
  const encryptedIndex = encryptAes256Gcm(indexPlainBuffer, params.dek32, headerAad);

  encryptedIndex.iv.copy(headerBuffer, 0x30);
  encryptedIndex.tag.copy(headerBuffer, 0x3c);
  headerBuffer.writeUInt32LE(encryptedIndex.ciphertext.length, 0x4c);

  const vaultBuffer = Buffer.concat([headerBuffer, encryptedIndex.ciphertext, ...encryptedBlobs]);
  return {
    vaultBuffer,
    revisionHashHex: revisionHash.toString('hex'),
    itemCount,
  };
}

/**
 * Faz o parse e autentica o cabeçalho e a tabela de índice de um arquivo `CPVAULT v1`.
 */
export function parseAndVerifyCpVaultIndex(
  vaultBuffer: Buffer,
  dek32: Buffer
): {
  header: ParsedVaultHeader;
  entries: ParsedVaultIndexEntry[];
} {
  if (vaultBuffer.length < CPVAULT_HEADER_SIZE) {
    throw new Error('Buffer too small to contain CPVAULT v1 header');
  }

  const magic = vaultBuffer.subarray(0x00, 0x04).toString('ascii');
  if (magic !== CPVAULT_MAGIC) {
    throw new Error(`Invalid CPVAULT magic: expected ${CPVAULT_MAGIC}, got ${magic}`);
  }

  const version = vaultBuffer.readUInt8(0x04);
  if (version !== CPVAULT_VERSION) {
    throw new Error(`Unsupported CPVAULT version: ${version}`);
  }

  const flags = vaultBuffer.readUInt8(0x05);
  const itemCount = vaultBuffer.readUInt16LE(0x06);
  const syncedAtUnix = Number(vaultBuffer.readBigUInt64LE(0x08));
  const revisionHashHex = vaultBuffer.subarray(0x10, 0x30).toString('hex');
  const indexIv = Buffer.from(vaultBuffer.subarray(0x30, 0x3c));
  const indexTag = Buffer.from(vaultBuffer.subarray(0x3c, 0x4c));
  const indexCipherLen = vaultBuffer.readUInt32LE(0x4c);

  if (indexCipherLen !== itemCount * CPVAULT_INDEX_ENTRY_SIZE) {
    throw new Error('Corrupted index length in header');
  }

  const headerAad = vaultBuffer.subarray(0, CPVAULT_HEADER_AAD_SIZE);
  const indexCiphertext = vaultBuffer.subarray(
    CPVAULT_HEADER_SIZE,
    CPVAULT_HEADER_SIZE + indexCipherLen
  );

  const indexPlain = decryptAes256Gcm(indexCiphertext, dek32, indexIv, indexTag, headerAad);
  const entries: ParsedVaultIndexEntry[] = [];

  for (let i = 0; i < itemCount; i++) {
    const off = i * CPVAULT_INDEX_ENTRY_SIZE;
    const itemIdBytes = Buffer.from(indexPlain.subarray(off + 0x00, off + 0x10));
    const title = readFixedCString(indexPlain, off + 0x10, 40);
    const usernameHint = readFixedCString(indexPlain, off + 0x38, 24);
    const itemFlags = indexPlain.readUInt8(off + 0x50);
    const blobOffset = indexPlain.readUInt32LE(off + 0x52);
    const blobCipherLen = indexPlain.readUInt16LE(off + 0x56);
    const blobIv = Buffer.from(indexPlain.subarray(off + 0x58, off + 0x64));
    const blobTag = Buffer.from(indexPlain.subarray(off + 0x64, off + 0x74));

    entries.push({
      itemIdHex: itemIdBytes.toString('hex'),
      itemIdBytes,
      title,
      usernameHint,
      hasTotp: (itemFlags & 0x01) !== 0,
      modifiedOffline: (itemFlags & 0x02) !== 0,
      blobOffset,
      blobCipherLen,
      blobIv,
      blobTag,
    });
  }

  return {
    header: {
      magic,
      version,
      readOnly: (flags & 0x01) !== 0,
      itemCount,
      syncedAtUnix,
      revisionHashHex,
      indexIv,
      indexTag,
      indexCipherLen,
    },
    entries,
  };
}

/**
 * Decifra sob demanda um único registo de credencial a partir da sua entrada de índice.
 */
export function decryptSingleVaultItem(
  vaultBuffer: Buffer,
  entry: ParsedVaultIndexEntry,
  dek32: Buffer
): VaultCredentialRecord {
  const blobEnd = entry.blobOffset + entry.blobCipherLen;
  if (blobEnd > vaultBuffer.length) {
    throw new Error('Item blob offset exceeds vault file size');
  }

  const itemAad = Buffer.alloc(24, 0);
  itemAad.write(CPVAULT_MAGIC, 0, 4, 'ascii');
  entry.itemIdBytes.copy(itemAad, 4);
  itemAad.writeUInt32LE(entry.blobOffset, 20);

  const cipherSlice = vaultBuffer.subarray(entry.blobOffset, blobEnd);
  const plain = decryptAes256Gcm(cipherSlice, dek32, entry.blobIv, entry.blobTag, itemAad);

  return {
    updatedAt: Number(plain.readBigUInt64LE(0x00)),
    vaultId: readFixedCString(plain, 0x08, 36),
    itemUuid: readFixedCString(plain, 0x2c, 36),
    title: entry.title,
    username: readFixedCString(plain, 0x50, 96),
    password: readFixedCString(plain, 0xb0, 128),
    totpSecret: readFixedCString(plain, 0x130, 64),
    url: readFixedCString(plain, 0x170, 68),
    notes: readFixedCString(plain, 0x1b4, 128),
    modifiedOffline: entry.modifiedOffline,
  };
}

/**
 * Empacota ou desempacota a fila offline `CPOUTBOX v1` (`outbox.bin`).
 */
export interface OutboxMutation {
  op: 'create' | 'update';
  vaultId: string;
  itemUuid: string;
  title: string;
  username: string;
  password: string;
  totpSecret: string;
  url: string;
  notes: string;
  updatedAt: number;
}

export interface OutboxEnvelope {
  deviceId: string;
  mutations: OutboxMutation[];
}

export function encodeOutboxPacket(envelope: OutboxEnvelope, dek32: Buffer): Buffer {
  const aad = Buffer.from(CPOUTBOX_MAGIC, 'ascii');
  const plain = Buffer.from(JSON.stringify(envelope), 'utf8');
  const { iv, tag, ciphertext } = encryptAes256Gcm(plain, dek32, aad);
  return Buffer.concat([aad, iv, tag, ciphertext]);
}

export function decodeOutboxPacket(packet: Buffer, dek32: Buffer): OutboxEnvelope {
  if (packet.length < 32) {
    throw new Error('Outbox packet too short');
  }
  const magic = packet.subarray(0, 4).toString('ascii');
  if (magic !== CPOUTBOX_MAGIC) {
    throw new Error(`Invalid Outbox magic: ${magic}`);
  }
  const aad = packet.subarray(0, 4);
  const iv = packet.subarray(4, 16);
  const tag = packet.subarray(16, 32);
  const ciphertext = packet.subarray(32);

  const plain = decryptAes256Gcm(ciphertext, dek32, iv, tag, aad);
  return JSON.parse(plain.toString('utf8')) as OutboxEnvelope;
}

/**
 * Gera um ficheiro `.vault` compatível com a arquitetura `geo-tp/Password-Manager`:
 * - Layout binário: `[16 bytes Salt] + [32 bytes SHA-256 Checksum do JSON claro] + [AES-128-ECB PKCS#7 do JSON]`
 * - Derivação de chave: `PBKDF2-HMAC-SHA256(masterPassword, salt, 10000, 16)`
 * - Campos de cada entrada: `serviceName`, `username`, `password`, `notes2` (segredo TOTP 2FA), `notes`, `link`.
 */
export function buildGeoTpVaultFile(
  items: VaultCredentialRecord[],
  masterPassword: string
): Buffer {
  // Conta títulos repetidos para desambiguar no visor do Cardputer quando necessário
  const titleCounts = new Map<string, number>();
  for (const it of items) {
    const key = (it.title || 'Sem Titulo').trim().toLowerCase();
    titleCounts.set(key, (titleCounts.get(key) ?? 0) + 1);
  }

  const mapped = items.map((it) => {
    const baseTitle = (it.title || 'Sem Titulo').trim();
    const count = titleCounts.get(baseTitle.toLowerCase()) ?? 1;
    const serviceName =
      count > 1 && it.username ? `${baseTitle} (${it.username.trim()})` : baseTitle;
    const rawNote = it.notes?.trim()
      ? it.notes.trim()
      : it.url?.trim()
        ? it.url.trim()
        : '';
    const noteVal = rawNote.length > 96 ? rawNote.slice(0, 96) : rawNote;

    // Formato JSON enxuto para minimizar uso de RAM no ESP32-S3 (mantendo 100% de compatibilidade)
    const entryObj: Record<string, unknown> = {
      serviceName,
      username: it.username ?? '',
      password: it.password ?? '',
    };
    if (noteVal) entryObj.notes = noteVal;
    if (it.totpSecret?.trim()) entryObj.notes2 = it.totpSecret.trim();
    if (it.vaultId?.trim()) entryObj.notes3 = it.vaultId.trim();
    if (it.url?.trim()) entryObj.link = it.url.trim().slice(0, 96);
    return entryObj;
  });

  // Ordena A -> Z por serviceName
  mapped.sort((a, b) =>
    String(a.serviceName).localeCompare(String(b.serviceName), undefined, {
      sensitivity: 'base',
    })
  );

  const entries = mapped.map((entry, idx) => ({
    id: idx,
    ...entry,
  }));

  const jsonPayload = JSON.stringify({
    categories: [],
    entries,
  });

  const plainBuf = Buffer.from(jsonPayload, 'utf8');
  const salt = crypto.randomBytes(16);
  const checksum = sha256(plainBuf); // 32 bytes SHA-256 do plaintext JSON
  const key = crypto.pbkdf2Sync(Buffer.from(masterPassword, 'utf8'), salt, 10000, 16, 'sha256');

  const cipher = crypto.createCipheriv('aes-128-ecb', key, null);
  cipher.setAutoPadding(true); // PKCS#7 padding padrão
  const encrypted = Buffer.concat([cipher.update(plainBuf), cipher.final()]);

  return Buffer.concat([salt, checksum, encrypted]);
}

/**
 * Gera na pasta `vaultsDir` os cofres organizados em listas de no máximo 100 senhas,
 * além de separar os itens com 2FA (TOTP) e gerar o `ProtonPass.vault` unificado com grupos internos.
 */
export function writeOrganizedGeoTpVaults(
  items: VaultCredentialRecord[],
  vaultsDir: string,
  masterPassword: string
): string[] {
  fs.mkdirSync(vaultsDir, { recursive: true });

  // Remove arquivos .vault antigos para manter o diretório limpo
  for (const existing of fs.readdirSync(vaultsDir)) {
    if (existing.endsWith('.vault')) {
      fs.rmSync(path.join(vaultsDir, existing), { force: true });
    }
  }

  const sortedAll = [...items].sort((a, b) =>
    (a.title || '').localeCompare(b.title || '', undefined, { sensitivity: 'base' })
  );

  const summary: string[] = [];

  // 1. Cofre unificado ProtonPass.vault (possui menu interno de grupos <= 100 + 2FA)
  const unifiedBuf = buildGeoTpVaultFile(sortedAll, masterPassword);
  fs.writeFileSync(path.join(vaultsDir, 'ProtonPass.vault'), unifiedBuf, { mode: 0o644 });
  summary.push(`ProtonPass.vault (${sortedAll.length} credenciais c/ menu de grupos)`);

  // 2. Cofre dedicado apenas para contas com 2FA (TOTP)
  const totpItems = sortedAll.filter((it) => Boolean(it.totpSecret && it.totpSecret.trim()));
  let prefixIdx = 1;
  if (totpItems.length > 0) {
    const fileName = `${String(prefixIdx).padStart(2, '0')}_2FA.vault`;
    prefixIdx++;
    const buf = buildGeoTpVaultFile(totpItems, masterPassword);
    fs.writeFileSync(path.join(vaultsDir, fileName), buf, { mode: 0o644 });
    summary.push(`${fileName} (${totpItems.length} códigos 2FA)`);
  }

  // 3. Agrupa por Vault do Proton Pass e divide listas > 100 em blocos de até 100 senhas
  const byVault = new Map<string, VaultCredentialRecord[]>();
  for (const it of sortedAll) {
    const vName = (it.vaultId || 'Default').replace(/[^a-zA-Z0-9_-]/g, '_') || 'Default';
    const arr = byVault.get(vName) ?? [];
    arr.push(it);
    byVault.set(vName, arr);
  }

  // Ordena cofres menores primeiro (ex: Detran antes de Personal)
  const vaultEntries = [...byVault.entries()].sort((a, b) => a[1].length - b[1].length);

  for (const [vName, vItems] of vaultEntries) {
    if (vItems.length <= 100) {
      const fileName = `${String(prefixIdx).padStart(2, '0')}_${vName}.vault`;
      prefixIdx++;
      const buf = buildGeoTpVaultFile(vItems, masterPassword);
      fs.writeFileSync(path.join(vaultsDir, fileName), buf, { mode: 0o644 });
      summary.push(`${fileName} (${vItems.length} credenciais)`);
    } else {
      const numChunks = Math.ceil(vItems.length / 100);
      const chunkSize = Math.ceil(vItems.length / numChunks);
      for (let c = 0; c < numChunks; c++) {
        const slice = vItems.slice(c * chunkSize, (c + 1) * chunkSize);
        if (slice.length === 0) continue;
        const firstChar = (slice[0].title || 'A').trim().charAt(0).toUpperCase() || 'A';
        const lastChar =
          (slice[slice.length - 1].title || 'Z').trim().charAt(0).toUpperCase() || 'Z';
        const safeRange = `${firstChar.replace(/[^A-Z0-9]/g, 'A')}-${lastChar.replace(/[^A-Z0-9]/g, 'Z')}`;
        const fileName = `${String(prefixIdx).padStart(2, '0')}_${vName}_${safeRange}.vault`;
        prefixIdx++;
        const buf = buildGeoTpVaultFile(slice, masterPassword);
        fs.writeFileSync(path.join(vaultsDir, fileName), buf, { mode: 0o644 });
        summary.push(`${fileName} (${slice.length} credenciais, <= 100)`);
      }
    }
  }

  return summary;
}

/**
 * Decifra e valida o checksum SHA-256 de um ficheiro `.vault` (`geo-tp/Password-Manager`).
 */
export function decryptGeoTpVaultFile(vaultBuffer: Buffer, masterPassword: string): {
  categories: unknown[];
  entries: Array<{
    id: number;
    serviceName: string;
    username: string;
    password: string;
    notes: string;
    notes2: string;
    notes3?: string;
    link: string;
  }>;
} {
  if (vaultBuffer.length < 48) {
    throw new Error('Vault file too small (< 48 bytes)');
  }
  const salt = vaultBuffer.subarray(0, 16);
  const expectedChecksum = vaultBuffer.subarray(16, 48);
  const encrypted = vaultBuffer.subarray(48);

  const key = crypto.pbkdf2Sync(Buffer.from(masterPassword, 'utf8'), salt, 10000, 16, 'sha256');
  const decipher = crypto.createDecipheriv('aes-128-ecb', key, null);
  decipher.setAutoPadding(true);
  const plainBuf = Buffer.concat([decipher.update(encrypted), decipher.final()]);

  const actualChecksum = sha256(plainBuf);
  if (!actualChecksum.equals(expectedChecksum)) {
    throw new Error('Checksum mismatch on decrypted .vault file');
  }

  const parsed = JSON.parse(plainBuf.toString('utf8')) as {
    categories?: unknown[];
    entries?: Array<Record<string, unknown>>;
  };

  return {
    categories: parsed.categories ?? [],
    entries: (parsed.entries ?? []).map((e, idx) => ({
      id: typeof e.id === 'number' ? e.id : idx,
      serviceName: String(e.serviceName ?? ''),
      username: String(e.username ?? ''),
      password: String(e.password ?? ''),
      notes: String(e.notes ?? ''),
      notes2: String(e.notes2 ?? ''),
      notes3: String(e.notes3 ?? ''),
      link: String(e.link ?? ''),
    })),
  };
}


