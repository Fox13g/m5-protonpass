import Fastify from 'fastify';
import { Bonjour } from 'bonjour-service';
import { config, type BridgeConfig } from './config.js';
import { PassCliAdapter } from './proton/passCliAdapter.js';
import { SyncEngine } from './proton/syncEngine.js';
import { registerSyncRoutes } from './routes/syncRoutes.js';

export function buildServer(customConfig: BridgeConfig = config, customAdapter?: PassCliAdapter) {
  const app = Fastify({
    logger: {
      level: customConfig.LOG_LEVEL,
    },
    trustProxy: true, // Essencial atrás do container sentinel-cloudflared
  });

  const adapter = customAdapter ?? new PassCliAdapter(customConfig);
  const syncEngine = new SyncEngine(customConfig, adapter);

  registerSyncRoutes(app, customConfig, syncEngine);

  return { app, syncEngine, adapter };
}

async function start(): Promise<void> {
  const { app } = buildServer(config);
  let bonjourInstance: Bonjour | null = null;

  try {
    await app.listen({ host: config.BRIDGE_HOST, port: config.BRIDGE_PORT });
    app.log.info(
      `🛡️ Cardputer Pass Sentinel Bridge escutando em http://${config.BRIDGE_HOST}:${config.BRIDGE_PORT}`
    );

    if (config.ENABLE_MDNS) {
      bonjourInstance = new Bonjour();
      bonjourInstance.publish({
        name: config.MDNS_SERVICE_NAME,
        type: 'http',
        port: config.BRIDGE_PORT,
        txt: { protocol: 'cpvault-v1', device: config.SENTINEL_DEVICE_ID },
      });
      app.log.info(`📡 Serviço mDNS anunciado como ${config.MDNS_SERVICE_NAME}.local`);
    }
  } catch (err) {
    app.log.error(err);
    process.exit(1);
  }

  const shutdown = async () => {
    app.log.info('Encerrando Bridge Gateway...');
    if (bonjourInstance) {
      bonjourInstance.unpublishAll();
      bonjourInstance.destroy();
    }
    await app.close();
    process.exit(0);
  };

  process.on('SIGINT', shutdown);
  process.on('SIGTERM', shutdown);
}

const isMain =
  process.argv[1] &&
  (process.argv[1].endsWith('index.ts') || process.argv[1].endsWith('index.js'));

if (isMain) {
  start();
}

