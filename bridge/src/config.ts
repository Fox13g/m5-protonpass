import dotenv from 'dotenv';
import { z } from 'zod';
import fs from 'node:fs';
import path from 'node:path';

dotenv.config();

// Carrega também sentinel_keys.env caso tenha sido gerado pelo `npm run provision`
const stateDir = process.env.STATE_DIR || path.resolve(process.cwd(), 'data');
const generatedKeysPath = path.join(stateDir, 'sentinel_keys.env');
if (fs.existsSync(generatedKeysPath)) {
  dotenv.config({ path: generatedKeysPath, override: true });
}

const envSchema = z.object({
  BRIDGE_HOST: z.string().default('0.0.0.0'),
  BRIDGE_PORT: z.coerce.number().int().min(1).max(65535).default(8443),
  NODE_ENV: z.enum(['development', 'production', 'test']).default('production'),
  LOG_LEVEL: z.enum(['fatal', 'error', 'warn', 'info', 'debug', 'trace']).default('info'),

  SENTINEL_DEK_HEX: z
    .string()
    .regex(/^[0-9a-fA-F]{64}$/, 'SENTINEL_DEK_HEX deve conter 64 caracteres hexadecimais (256 bits)')
    .default('0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef'),
  SENTINEL_HMAC_SECRET_HEX: z
    .string()
    .regex(/^[0-9a-fA-F]{64}$/, 'SENTINEL_HMAC_SECRET_HEX deve conter 64 caracteres hexadecimais (256 bits)')
    .default('fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210'),
  SENTINEL_DEVICE_ID: z.string().min(3).default('cardputer-primary-01'),
  SENTINEL_MAX_CLOCK_SKEW_SEC: z.coerce.number().int().min(10).max(600).default(60),

  PASS_CLI_BIN: z.string().default('pass-cli'),
  PROTON_PASS_PAT: z.string().optional().default(''),
  ALLOWED_VAULTS: z.string().optional().default(''),
  READ_ONLY_MODE: z
    .string()
    .transform((v) => v.toLowerCase() === 'true' || v === '1')
    .default('false'),

  PUBLIC_TUNNEL_URL: z.string().default('https://sentinel.example.com'),
  CF_ACCESS_CLIENT_ID: z.string().optional().default(''),
  CF_ACCESS_CLIENT_SECRET: z.string().optional().default(''),

  DEFAULT_WIFI_SSID: z.string().optional().default(''),
  DEFAULT_WIFI_PASSWORD: z.string().optional().default(''),

  ENABLE_MDNS: z
    .string()
    .transform((v) => v.toLowerCase() === 'true' || v === '1')
    .default('false'),
  MDNS_SERVICE_NAME: z.string().default('proton-bridge'),

  STATE_DIR: z.string().default(stateDir),
  PROVISION_OUT_DIR: z.string().default(path.resolve(process.cwd(), '../provision-out')),
});

export type BridgeConfig = z.infer<typeof envSchema>;

export function loadConfig(overrides: Partial<Record<string, string>> = {}): BridgeConfig {
  const merged = { ...process.env, ...overrides };
  return envSchema.parse(merged);
}

export const config = loadConfig();

