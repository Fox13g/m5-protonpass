import fs from 'node:fs';
import path from 'node:path';
import readline from 'node:readline/promises';
import { stdin as input, stdout as output } from 'node:process';
import { loadConfig } from '../config.js';
import { PassCliAdapter } from '../proton/passCliAdapter.js';
import { importProtonPassFile } from '../proton/protonExportImporter.js';
import { buildGeoTpVaultFile } from '../crypto/vaultBuilder.js';

function findCandidateExportFiles(dirs: string[]): string[] {
  const candidates: string[] = [];
  const ignoredNames = new Set([
    'cardputer_provision.json',
    'fallback_vault_items.json',
    'bridge_state.json',
    'package.json',
    'tsconfig.json',
  ]);

  for (const dir of dirs) {
    if (!fs.existsSync(dir)) continue;
    for (const entry of fs.readdirSync(dir)) {
      if (ignoredNames.has(entry)) continue;
      const lower = entry.toLowerCase();
      if (
        lower.endsWith('.zip') ||
        lower.endsWith('.json') ||
        lower.endsWith('.csv') ||
        lower.endsWith('.pgp') ||
        lower.endsWith('.gpg')
      ) {
        candidates.push(path.join(dir, entry));
      }
    }
  }
  return candidates;
}

async function main(): Promise<void> {
  const cfg = loadConfig();
  const adapter = new PassCliAdapter(cfg);
  const args = process.argv.slice(2);

  console.log('================================================================');
  console.log('  Cardputer Pass Sentinel — Importador Proton Pass (Free/Plus)');
  console.log('================================================================');

  let targetFile = args.find((a) => !a.startsWith('--'));
  let passphraseFlagIdx = args.indexOf('--passphrase');
  let passphrase =
    passphraseFlagIdx !== -1 && args[passphraseFlagIdx + 1]
      ? args[passphraseFlagIdx + 1]
      : undefined;

  if (!targetFile) {
    const candidates = findCandidateExportFiles([cfg.PROVISION_OUT_DIR, cfg.STATE_DIR]);
    if (candidates.length === 1) {
      targetFile = candidates[0];
      console.log(`[*] Arquivo de exportação detectado automaticamente: ${targetFile}`);
    } else if (candidates.length > 1) {
      console.log('\nArquivos encontrados em ./provision-out / ./data:');
      candidates.forEach((c, idx) => console.log(`  [${idx + 1}] ${c}`));
      const rl = readline.createInterface({ input, output });
      const choice = await rl.question('\nEscolha o número do arquivo para importar [1]: ');
      rl.close();
      const idx = (parseInt(choice.trim() || '1', 10) || 1) - 1;
      targetFile = candidates[Math.max(0, Math.min(idx, candidates.length - 1))];
    }
  }

  if (!targetFile) {
    console.log('\nNenhum arquivo de exportação (.zip, .json, .csv, .pgp) foi encontrado em ./provision-out/.');
    console.log('\nComo exportar da sua conta Proton Pass Free:');
    console.log('  1. Abra https://pass.proton.me ou a extensão do navegador Proton Pass.');
    console.log('  2. Vá em Configurações (⚙️) -> Exportar (Export).');
    console.log('  3. Escolha o formato JSON (ou CSV) — desmarque "Criptografar" para .zip direto,');
    console.log('     ou mantenha criptografado (.pgp) e informe a senha aqui.');
    console.log('  4. Copie o arquivo baixado para a pasta:');
    console.log('     ~/protonpass/provision-out/');
    console.log('  5. Execute novamente: docker exec -it sentinel-bridge npm run import\n');
    process.exit(1);
  }

  if (!fs.existsSync(targetFile)) {
    console.error(`[!] Arquivo não encontrado: ${targetFile}`);
    process.exit(1);
  }

  const lower = targetFile.toLowerCase();
  if ((lower.endsWith('.pgp') || lower.endsWith('.gpg')) && !passphrase) {
    const rl = readline.createInterface({ input, output });
    passphrase = await rl.question('Digite a frase-senha (passphrase) definida na exportação PGP: ');
    rl.close();
  }

  const imported = importProtonPassFile(targetFile, passphrase);
  adapter.saveLocalFallback(imported);

  const masterPassword = process.env.CARDPUTER_MASTER_PASSWORD || '9868';
  const vaultsDir = path.join(cfg.PROVISION_OUT_DIR, 'vaults');
  fs.mkdirSync(vaultsDir, { recursive: true });

  // Gera o cofre unificado ProtonPass.vault e cofres separados por Vault
  const unifiedBuf = buildGeoTpVaultFile(imported, masterPassword);
  fs.writeFileSync(path.join(vaultsDir, 'ProtonPass.vault'), unifiedBuf, { mode: 0o644 });

  const byVault = new Map<string, typeof imported>();
  for (const it of imported) {
    const vName = (it.vaultId || 'Default').replace(/[^a-zA-Z0-9_-]/g, '_') || 'Default';
    const arr = byVault.get(vName) ?? [];
    arr.push(it);
    byVault.set(vName, arr);
  }
  for (const [vName, vItems] of byVault.entries()) {
    const vBuf = buildGeoTpVaultFile(vItems, masterPassword);
    fs.writeFileSync(path.join(vaultsDir, `${vName}.vault`), vBuf, { mode: 0o644 });
  }

  const totpCount = imported.filter((i) => Boolean(i.totpSecret)).length;
  const vaults = [...new Set(imported.map((i) => i.vaultId))];

  console.log(`\n[✔] Importação concluída com sucesso!`);
  console.log(`    • Credenciais importadas : ${imported.length}`);
  console.log(`    • Códigos 2FA (TOTP)     : ${totpCount}`);
  console.log(`    • Cofres (Vaults)        : ${vaults.join(', ') || 'Personal'}`);
  console.log(`    • Armazenado em          : ${cfg.STATE_DIR}/fallback_vault_items.json`);
  console.log(`    • Cofres .vault gerados  : ${vaultsDir}/ (Senha Mestre: ${masterPassword})`);
  console.log('================================================================\n');
}

main().catch((err) => {
  console.error('[ERRO]', (err as Error).message);
  process.exit(1);
});

