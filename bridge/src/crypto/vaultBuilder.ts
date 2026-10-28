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

