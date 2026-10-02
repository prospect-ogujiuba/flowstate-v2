// Runs the agent service locally: npm run serve (from the repo root). Configuration: configFromEnv in service.ts.
// FLOWSTATE_SERVICE_HOST (default 127.0.0.1) and FLOWSTATE_SERVICE_PORT (default 8787) choose where it listens.
import { configFromEnv, createService } from "./service.ts";

const config = configFromEnv();
const host = process.env.FLOWSTATE_SERVICE_HOST ?? "127.0.0.1";
const port = Number(process.env.FLOWSTATE_SERVICE_PORT ?? 8787);
const server = createService(config);

server.listen(port, host, () => {
  const m = config.managed;
  const also = config.managedAlso.length ? `, also ${config.managedAlso.join(", ")}` : "";
  console.error(`Flowstate agent service on http://${host}:${port} (managed: ${m.provider}/${m.model}, ` +
    `reasoning ${m.reasoning ?? "high"}${also}; byok ${config.features.byok ? "on" : "off"}; version ${config.version})`);
});

// Open streams are cut, which aborts their provider calls.
const stop = () => {
  server.close(() => process.exit(0));
  server.closeAllConnections();
};
process.on("SIGINT", stop);
process.on("SIGTERM", stop);
