import { describe, expect, it } from 'vitest';
import crypto from 'node:crypto';
import {
  computeRequestSignature,
  decryptAes256Gcm,
  deriveKeyPbkdf2Sha256,
  encryptAes256Gcm,
  verifyRequestSignature,
} from '../src/crypto/aead.js';

describe('Crypto Primitive Layer (AES-256-GCM, PBKDF2-SHA256 & HMAC Zero-Trust)', () => {
  it('encrypts and decrypts AES-256-GCM with AAD and rejects tampered ciphertext or AAD', () => {
    const key = crypto.randomBytes(32);
    const aad = Buffer.from('CPV1-HEADER-AAD', 'utf8');
    const plaintext = Buffer.from('SuperSecretPassword!2026#ABNT2', 'utf8');

    const { iv, tag, ciphertext } = encryptAes256Gcm(plaintext, key, aad);
    expect(iv.length).toBe(12);
    expect(tag.length).toBe(16);

    const decrypted = decryptAes256Gcm(ciphertext, key, iv, tag, aad);
    expect(decrypted.toString('utf8')).toBe('SuperSecretPassword!2026#ABNT2');

    // Adulteração de 1 bit no ciphertext deve falhar na verificação da Tag GCM
    const tamperedCipher = Buffer.from(ciphertext);
    tamperedCipher[0] ^= 0x01;
    expect(() => decryptAes256Gcm(tamperedCipher, key, iv, tag, aad)).toThrow();

    // Adulteração no AAD deve falhar na verificação da Tag GCM
    const tamperedAad = Buffer.from('CPV1-TAMPER-AAD', 'utf8');
    expect(() => decryptAes256Gcm(ciphertext, key, iv, tag, tamperedAad)).toThrow();
  });

  it('derives deterministic 256-bit keys via PBKDF2-HMAC-SHA256 (100,000 iterations)', () => {
    const salt = Buffer.from('000102030405060708090a0b0c0d0e0f', 'hex');
    const key1 = deriveKeyPbkdf2Sha256('MasterPass#Cardputer', salt, 100_000);
    const key2 = deriveKeyPbkdf2Sha256('MasterPass#Cardputer', salt, 100_000);
    const keyDiff = deriveKeyPbkdf2Sha256('WrongPass#Cardputer', salt, 100_000);

    expect(key1.length).toBe(32);
    expect(key1.equals(key2)).toBe(true);
    expect(key1.equals(keyDiff)).toBe(false);
  });

  it('validates canonical HMAC-SHA256 request signatures and blocks replay attacks', () => {
    const hmacSecret = crypto.randomBytes(32);
    const usedNonces = new Map<string, number>();
    const nowUnix = 1793196000;
    const timestamp = String(nowUnix);
    const nonce = crypto.randomBytes(16).toString('hex');
    const body = Buffer.from('{"test":true}', 'utf8');

    const signatureHex = computeRequestSignature({
      method: 'POST',
      path: '/api/v1/sync/push',
      timestamp,
      nonce,
      body,
      hmacSecret,
    });

    // 1ª chamada deve ser aceite
    const res1 = verifyRequestSignature({
      method: 'POST',
      path: '/api/v1/sync/push',
      timestamp,
      nonce,
      body,
      signatureHex,
      hmacSecret,
      maxClockSkewSec: 60,
      usedNonces,
      nowUnixSec: nowUnix + 5,
    });
    expect(res1.valid).toBe(true);

    // 2ª chamada com o mesmo nonce deve ser bloqueada (Replay Attack)
    const resReplay = verifyRequestSignature({
      method: 'POST',
      path: '/api/v1/sync/push',
      timestamp,
      nonce,
      body,
      signatureHex,
      hmacSecret,
      maxClockSkewSec: 60,
      usedNonces,
      nowUnixSec: nowUnix + 6,
    });
    expect(resReplay.valid).toBe(false);
    expect(resReplay.reason).toContain('Replay detected');
  });
});

