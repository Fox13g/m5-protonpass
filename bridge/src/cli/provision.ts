import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import readline from 'node:readline/promises';
import { stdin as input, stdout as output } from 'node:process';
import { loadConfig } from '../config.js';
import { PassCliAdapter } from '../proton/passCliAdapter.js';
import { buildCpVaultBinary, buildGeoTpVaultFile } from '../crypto/vaultBuilder.js';

export interface CardputerProvisionFile {
  schemaVersion: 1;
  generatedAtIso: string;
  deviceId: string;
  network: {
    wifiSsid: string;
    wifiPassword: string;
    tunnelUrl: string;
    cfAccessClientId: string;
    cfAccessClientSecret: string;
    mdnsServiceName: string;
  };
  crypto: {
    dekHex: string;
    hmacSecretHex: string;
    pbkdf2SaltHex: string;
    pbkdf2Iterations: number;
  };
  defaults: {
    keyboardLayout: 'PT_BR_ABNT2' | 'US_ANSI';
    hidMode: 'USB' | 'BLE';
    maxUnlockAttempts: number;
  };
}

export function generateProvisionPayload(params: {
  deviceId: string;
  wifiSsid: string;
  wifiPassword: string;
  tunnelUrl: string;
  cfAccessClientId?: string;
  cfAccessClientSecret?: string;
  mdnsServiceName?: string;
  dekHex?: string;
  hmacSecretHex?: string;
  keyboardLayout?: 'PT_BR_ABNT2' | 'US_ANSI';
}): CardputerProvisionFile {
  const dekHex = params.dekHex || crypto.randomBytes(32).toString('hex');
  const hmacSecretHex = params.hmacSecretHex || crypto.randomBytes(32).toString('hex');
  const pbkdf2SaltHex = crypto.randomBytes(16).toString('hex');

  let cleanUrl = params.tunnelUrl.trim().replace(/\/+$/, '');
  if (cleanUrl && !/^https?:\/\//i.test(cleanUrl)) {
    cleanUrl = `https://${cleanUrl}`;
  }

  return {
    schemaVersion: 1,
    generatedAtIso: new Date().toISOString(),
    deviceId: params.deviceId,
    network: {
      wifiSsid: params.wifiSsid,
      wifiPassword: params.wifiPassword,
      tunnelUrl: cleanUrl,
      cfAccessClientId: params.cfAccessClientId ?? '',
      cfAccessClientSecret: params.cfAccessClientSecret ?? '',
      mdnsServiceName: params.mdnsServiceName ?? 'proton-bridge',
    },
    crypto: {
      dekHex,
      hmacSecretHex,
      pbkdf2SaltHex,
      pbkdf2Iterations: 100_000,
    },
    defaults: {
      keyboardLayout: params.keyboardLayout ?? 'PT_BR_ABNT2',
      hidMode: 'USB',
      maxUnlockAttempts: 10,
    },
  };
}

async function runInteractiveCli(): Promise<void> {
  const cfg = loadConfig();
  const nonInteractive = process.argv.includes('--non-interactive');

  console.log('\n====================================================================');
  console.log(' 🛡️  Cardputer Pass Sentinel — Assistente de Provisionamento Zero-Trust');
  console.log('====================================================================\n');

  const adapter = new PassCliAdapter(cfg);
  const vaults = await adapter.listVaults();
  console.log(
    `📦 Cofres detectados no Proton Pass: ${vaults.map((v) => `${v.name} (${v.vaultId})`).join(', ')}`
  );

  let wifiSsid = cfg.DEFAULT_WIFI_SSID || 'MinhaRedeWiFi';
  let wifiPassword = cfg.DEFAULT_WIFI_PASSWORD || '';
  let tunnelUrl = cfg.PUBLIC_TUNNEL_URL || 'https://sentinel.seudominio.com.br';
  let deviceId = cfg.SENTINEL_DEVICE_ID || 'cardputer-primary-01';
  let layout: 'PT_BR_ABNT2' | 'US_ANSI' = 'PT_BR_ABNT2';
  let masterPassword = process.env.CARDPUTER_MASTER_PASSWORD || '9868';

  if (!nonInteractive && input.isTTY) {
    const rl = readline.createInterface({ input, output });
    try {
      const ansMaster = await rl.question(`0. Senha Mestre dos cofres .vault [${masterPassword}]: `);
      if (ansMaster.trim()) masterPassword = ansMaster.trim();

      const ansSsid = await rl.question(`1. Nome da rede Wi-Fi 2.4GHz [${wifiSsid}]: `);
      if (ansSsid.trim()) wifiSsid = ansSsid.trim();

      const ansPass = await rl.question(`2. Senha do Wi-Fi [oculto/enter p/ manter]: `);
      if (ansPass.trim()) wifiPassword = ansPass.trim();

      const ansUrl = await rl.question(`3. URL pública do Cloudflare Tunnel [${tunnelUrl}]: `);
      if (ansUrl.trim()) tunnelUrl = ansUrl.trim();

      const ansDev = await rl.question(`4. ID do Dispositivo Cardputer [${deviceId}]: `);
      if (ansDev.trim()) deviceId = ansDev.trim();

      const ansLayout = await rl.question(`5. Layout padrão do Teclado HID (1=PT_BR_ABNT2, 2=US_ANSI) [1]: `);
      if (ansLayout.trim() === '2') layout = 'US_ANSI';
    } finally {
      rl.close();
    }
  }

  // Gera novas chaves criptográficas de 256 bits para a dupla Bridge <-> Cardputer
  const provisionData = generateProvisionPayload({
    deviceId,
    wifiSsid,
    wifiPassword,
    tunnelUrl,
    cfAccessClientId: cfg.CF_ACCESS_CLIENT_ID,
    cfAccessClientSecret: cfg.CF_ACCESS_CLIENT_SECRET,
    mdnsServiceName: cfg.MDNS_SERVICE_NAME,
    keyboardLayout: layout,
  });

  // 1. Grava as chaves geradas no volume persistente do container (`/app/data/sentinel_keys.env`)
  fs.mkdirSync(cfg.STATE_DIR, { recursive: true });
  const keysEnvPath = path.join(cfg.STATE_DIR, 'sentinel_keys.env');
  const keysEnvContent = [
    `# Chaves geradas automaticamente em ${provisionData.generatedAtIso}`,
    `SENTINEL_DEK_HEX=${provisionData.crypto.dekHex}`,
    `SENTINEL_HMAC_SECRET_HEX=${provisionData.crypto.hmacSecretHex}`,
    `SENTINEL_DEVICE_ID=${provisionData.deviceId}`,
    `PUBLIC_TUNNEL_URL=${provisionData.network.tunnelUrl}`,
    '',
  ].join('\n');
  fs.writeFileSync(keysEnvPath, keysEnvContent, { encoding: 'utf8', mode: 0o600 });

  // 2. Grava o ficheiro `cardputer_provision.json` na pasta partilhada `./provision-out/`
  fs.mkdirSync(cfg.PROVISION_OUT_DIR, { recursive: true });
  const outFilePath = path.join(cfg.PROVISION_OUT_DIR, 'cardputer_provision.json');
  fs.writeFileSync(outFilePath, JSON.stringify(provisionData, null, 2), {
    encoding: 'utf8',
    mode: 0o644,
  });

  // 3. Gera os ficheiros `.vault` (formato geo-tp/Password-Manager) e o backup `.enc`
  const encVaultPath = path.join(cfg.PROVISION_OUT_DIR, 'sentinel_vault_backup.enc');
  const vaultsDir = path.join(cfg.PROVISION_OUT_DIR, 'vaults');
  fs.mkdirSync(vaultsDir, { recursive: true });
  let exportedCount = 0;
  const generatedVaultFiles: string[] = [];

  try {
    const items = await adapter.fetchCredentials();
    if (items.length > 0) {
      const dekBuf = Buffer.from(provisionData.crypto.dekHex, 'hex');
      const { vaultBuffer } = buildCpVaultBinary({ items, dek32: dekBuf });
      fs.writeFileSync(encVaultPath, vaultBuffer, { mode: 0o644 });
      exportedCount = items.length;

      // Cofre unificado ProtonPass.vault com todas as credenciais
      const unifiedVaultBuf = buildGeoTpVaultFile(items, masterPassword);
      const unifiedVaultPath = path.join(vaultsDir, 'ProtonPass.vault');
      fs.writeFileSync(unifiedVaultPath, unifiedVaultBuf, { mode: 0o644 });
      generatedVaultFiles.push(`ProtonPass.vault (${items.length} credenciais)`);

      // Cofres separados por Vault do Proton Pass (ex: Personal.vault, Detran.vault)
      const byVault = new Map<string, typeof items>();
      for (const it of items) {
        const vName = (it.vaultId || 'Default').replace(/[^a-zA-Z0-9_-]/g, '_') || 'Default';
        const arr = byVault.get(vName) ?? [];
        arr.push(it);
        byVault.set(vName, arr);
      }

      for (const [vName, vItems] of byVault.entries()) {
        const vBuf = buildGeoTpVaultFile(vItems, masterPassword);
        const vPath = path.join(vaultsDir, `${vName}.vault`);
        fs.writeFileSync(vPath, vBuf, { mode: 0o644 });
        generatedVaultFiles.push(`${vName}.vault (${vItems.length} credenciais)`);
      }
    }
  } catch {
    // Se ainda não houver credenciais importadas, prossegue apenas com o provision.json
  }

  console.log('\n✅ Provisionamento concluído com sucesso!');
  console.log(`🔑 Chaves do servidor salvas em: ${keysEnvPath}`);
  console.log(`💾 Ficheiro de chaves do SD gerado em: ${outFilePath}`);
  if (exportedCount > 0) {
    console.log(
      `🔐 Cofres .vault gerados em ${vaultsDir} (Senha Mestre: "${masterPassword}"):`
    );
    for (const vf of generatedVaultFiles) {
      console.log(`   • ${vf}`);
    }
  }
  console.log('\n👉 Próximo passo:');
  console.log(
    '   1. Copie a pasta "vaults/" (ou os ficheiros .vault dentro dela) para o cartão microSD (FAT32).'
  );
  console.log('   2. Insira o cartão no M5Stack Cardputer, selecione "Load Vault" e digite a senha mestre.');
  console.log(
    '   3. Para códigos 2FA (TOTP), ajuste o relógio em "Date & Time" (ou pressione [t] no campo 2FA)!\n'
  );
}

const isDirectExecution =
  Boolean(process.argv[1]) &&
  (path.resolve(process.argv[1]).endsWith('provision.ts') ||
    path.resolve(process.argv[1]).endsWith('provision.js'));

if (isDirectExecution) {
  runInteractiveCli().catch((err) => {
    console.error('Erro no provisionamento:', err);
    process.exit(1);
  });
}

