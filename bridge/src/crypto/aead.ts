import crypto from 'node:crypto';

export interface AesGcmEncryptedBlock {
  iv: Buffer;
  tag: Buffer;
  ciphertext: Buffer;
}

/**
 * Cifra um bloco arbitrário com AES-256-GCM (IV de 12 bytes e Tag de 16 bytes),
 * compatível byte-a-byte com `mbedtls_gcm_auth_decrypt` no ESP32-S3.
 */
export function encryptAes256Gcm(
  plaintext: Buffer,
  key32: Buffer,
  aad?: Buffer,
  explicitIv12?: Buffer
): AesGcmEncryptedBlock {
  if (key32.length !== 32) {
    throw new Error(`Invalid AES-256 key length: expected 32 bytes, got ${key32.length}`);
  }
  const iv = explicitIv12 ? Buffer.from(explicitIv12) : crypto.randomBytes(12);
  if (iv.length !== 12) {
    throw new Error(`Invalid GCM IV length: expected 12 bytes, got ${iv.length}`);
  }

  const cipher = crypto.createCipheriv('aes-256-gcm', key32, iv, { authTagLength: 16 });
  if (aad && aad.length > 0) {
    cipher.setAAD(aad);
  }
  const ciphertext = Buffer.concat([cipher.update(plaintext), cipher.final()]);
  const tag = cipher.getAuthTag();
  return { iv, tag, ciphertext };
}

/**
 * Decifra e autentica um bloco AES-256-GCM. Lança exceção caso a Tag GCM ou AAD falhem.
 */
export function decryptAes256Gcm(
  ciphertext: Buffer,
  key32: Buffer,
  iv12: Buffer,
  tag16: Buffer,
  aad?: Buffer
): Buffer {
  if (key32.length !== 32) {
    throw new Error(`Invalid AES-256 key length: expected 32 bytes, got ${key32.length}`);
  }
  if (iv12.length !== 12) {
    throw new Error(`Invalid GCM IV length: expected 12 bytes, got ${iv12.length}`);
  }
  if (tag16.length !== 16) {
    throw new Error(`Invalid GCM Tag length: expected 16 bytes, got ${tag16.length}`);
  }

  const decipher = crypto.createDecipheriv('aes-256-gcm', key32, iv12, { authTagLength: 16 });
  if (aad && aad.length > 0) {
    decipher.setAAD(aad);
  }
  decipher.setAuthTag(tag16);
  return Buffer.concat([decipher.update(ciphertext), decipher.final()]);
}

/**
 * Derivação de chave local idêntica ao `mbedtls_pkcs5_pbkdf2_hmac` (SHA-256, 100.000 iterações).
 */
export function deriveKeyPbkdf2Sha256(
  passphrase: string,
  salt: Buffer,
  iterations = 100_000
): Buffer {
  return crypto.pbkdf2Sync(Buffer.from(passphrase, 'utf8'), salt, iterations, 32, 'sha256');
}

/**
 * Calcula o resumo SHA-256 de um buffer.
 */
export function sha256(data: Buffer | string): Buffer {
  return crypto.createHash('sha256').update(data).digest();
}

/**
 * Constrói e assina a mensagem canónica HMAC-SHA256 para autenticação de requisições Zero-Trust.
 */
export function computeRequestSignature(params: {
  method: string;
  path: string;
  timestamp: string;
  nonce: string;
  body: Buffer;
  hmacSecret: Buffer;
}): string {
  const bodyHashHex = sha256(params.body).toString('hex');
  const canonical = [
    params.method.toUpperCase(),
    params.path,
    params.timestamp,
    params.nonce,
    bodyHashHex,
  ].join('\n');

  return crypto.createHmac('sha256', params.hmacSecret).update(canonical, 'utf8').digest('hex');
}

/**
 * Verifica a assinatura HMAC-SHA256 em tempo constante (constant-time) e valida a janela temporal.
 */
export function verifyRequestSignature(params: {
  method: string;
  path: string;
  timestamp: string;
  nonce: string;
  body: Buffer;
  signatureHex: string;
  hmacSecret: Buffer;
  maxClockSkewSec: number;
  usedNonces: Map<string, number>;
  nowUnixSec?: number;
}): { valid: boolean; reason?: string } {
  const now = params.nowUnixSec ?? Math.floor(Date.now() / 1000);
  const reqTs = Number.parseInt(params.timestamp, 10);

  if (!Number.isFinite(reqTs)) {
    return { valid: false, reason: 'Invalid timestamp format' };
  }
  if (Math.abs(now - reqTs) > params.maxClockSkewSec) {
    return { valid: false, reason: 'Timestamp outside allowed clock skew window' };
  }
  if (!/^[0-9a-fA-F]{16,64}$/.test(params.nonce)) {
    return { valid: false, reason: 'Invalid nonce format' };
  }

  // Purga nonces expirados
  for (const [seenNonce, seenTs] of params.usedNonces.entries()) {
    if (now - seenTs > params.maxClockSkewSec * 2) {
      params.usedNonces.delete(seenNonce);
    }
  }
  if (params.usedNonces.has(params.nonce)) {
    return { valid: false, reason: 'Replay detected: nonce already used' };
  }

  const expectedHex = computeRequestSignature({
    method: params.method,
    path: params.path,
    timestamp: params.timestamp,
    nonce: params.nonce,
    body: params.body,
    hmacSecret: params.hmacSecret,
  });

  if (expectedHex.length !== params.signatureHex.length) {
    return { valid: false, reason: 'Signature length mismatch' };
  }

  const expectedBuf = Buffer.from(expectedHex, 'hex');
  const providedBuf = Buffer.from(params.signatureHex, 'hex');
  if (expectedBuf.length !== providedBuf.length || !crypto.timingSafeEqual(expectedBuf, providedBuf)) {
    return { valid: false, reason: 'HMAC signature mismatch' };
  }

  params.usedNonces.set(params.nonce, reqTs);
  return { valid: true };
}

