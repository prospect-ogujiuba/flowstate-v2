// The agent service (P1-4): the plugin's HTTP API over the planner. Contract: docs/bridge-spec.md, "Plugin ↔ agent
// service", with the shapes in schema/src/bridge.ts.
//   GET  /v1/health  Health
//   POST /v1/plan    PlanRequest -> SSE stream of ServiceEvents: header, partStarted/partDone per part, done or error
//   POST /v1/edit    EditRequest -> SSE; a skeleton in Phase 1 (validates, then answers `unavailable`)
// A BYOK key arrives in the x-flowstate-provider-key header. It never goes into a log line or an error message.
// Hosted (P1-13), plan and edit need a tester token (`Authorization: Bearer`) and keep to per-tester limits (access.ts).
import { randomUUID } from "node:crypto";
import { readFileSync } from "node:fs";
import http from "node:http";
import {
  BRIDGE_ID, EditRequest, Health, PlanRequest, ServiceEvent,
  type ErrorCode, type Part, type ProviderChoice, type Score,
} from "@flowstate/schema";
import { Access, DEFAULT_LIMITS, parseTokens, RateLimited } from "./access.ts";
import { backendFor, selectionFromEnv, type Backend, type ModelSelection } from "./backends.ts";
import { editScore, rulesFor, type EditRequest as EditorRequest } from "./editor.ts";
import { errorCode, FlowstateError } from "./errors.ts";
import type { Role } from "./part-stream.ts";
import { keptDraft, planScore, type PlanRequest as PlannerRequest } from "./planner.ts";

export interface ServiceConfig {
  /** Reported by the health endpoint: the build id, or "dev". */
  version: string;
  /** The managed default, used when a request names no provider. */
  managed: ModelSelection;
  /** Other "provider/model" pairs offered on the service's own keys. */
  managedAlso: string[];
  /** Release flags (docs/roadmap.md): `byok` turns bring-your-own-key on or off without a code change. */
  features: { byok: boolean };
  /** SSE comment interval, so proxies with an idle timeout keep the stream open (P1-13). */
  keepaliveMs: number;
  /** One line per finished request. Never receives keys, headers or prompt text. */
  log: (line: Record<string, unknown>) => void;
  backendFor: (sel: ModelSelection) => Backend;
  /** Tester tokens and limits. Without it the service is open, which `serve` allows on loopback only. */
  access?: Access | null;
}

export const KEY_HEADER = "x-flowstate-provider-key";
const MAX_BODY_BYTES = 4 * 1024 * 1024;
// A request with no roles gets the Phase 0 lanes.
const DEFAULT_ROLES: Role[] = ["chords", "bass", "melody", "drums"];

const flag = (value: string | undefined, fallback: boolean) =>
  value === undefined ? fallback : !/^(0|false|off|no)$/i.test(value.trim());

/**
 * The configuration from the environment. The managed default comes from the planner's variables
 * (selectionFromEnv: FLOWSTATE_PLANNER_BACKEND, _PROVIDER, _MODEL, _REASONING); keys from the provider's usual
 * variable. FLOWSTATE_SERVICE_MANAGED_MODELS lists more "provider/model" pairs, comma-separated.
 * FLOWSTATE_FEATURE_BYOK=0 turns BYOK off. FLOWSTATE_BUILD_ID names the version.
 * FLOWSTATE_SERVICE_TOKENS_FILE turns tester tokens on (`<tester> <sha256 hex>` per line, see access.ts), with
 * FLOWSTATE_SERVICE_CONCURRENT and FLOWSTATE_SERVICE_PER_HOUR as the per-tester limits.
 */
export function configFromEnv(): ServiceConfig {
  const managed = selectionFromEnv();
  backendFor(managed); // fail at startup, not on the first request
  return {
    version: process.env.FLOWSTATE_BUILD_ID ?? "dev",
    managed,
    managedAlso: (process.env.FLOWSTATE_SERVICE_MANAGED_MODELS ?? "").split(",").map((s) => s.trim()).filter(Boolean),
    features: { byok: flag(process.env.FLOWSTATE_FEATURE_BYOK, true) },
    keepaliveMs: 15_000,
    log: (line) => console.log(JSON.stringify(line)),
    backendFor: (sel) => backendFor(sel),
    access: accessFromEnv(),
  };
}

function accessFromEnv(): Access | null {
  const file = process.env.FLOWSTATE_SERVICE_TOKENS_FILE;
  if (!file) return null;
  const testers = parseTokens(readFileSync(file, "utf8"));
  if (testers.size === 0) throw new Error(`${file} lists no tester tokens`);
  const limit = (name: string, fallback: number) => {
    const value = Number(process.env[name] ?? fallback);
    if (!Number.isInteger(value) || value < 1) throw new Error(`${name} must be a positive whole number`);
    return value;
  };
  return new Access(testers, {
    concurrent: limit("FLOWSTATE_SERVICE_CONCURRENT", DEFAULT_LIMITS.concurrent),
    perHour: limit("FLOWSTATE_SERVICE_PER_HOUR", DEFAULT_LIMITS.perHour),
  });
}

export function createService(config: ServiceConfig): http.Server {
  return http.createServer((req, res) => {
    void route(config, req, res).catch((err) => {
      // A bug: the response may be half written.
      if (!res.headersSent) json(res, 500, { error: { code: "internal", message: "internal error" } });
      else res.end();
      config.log({ at: new Date().toISOString(), route: req.url, status: "crashed", error: String(err).slice(0, 300) });
    });
  });
}

async function route(config: ServiceConfig, req: http.IncomingMessage, res: http.ServerResponse) {
  const url = (req.url ?? "/").split("?")[0];
  if (req.method === "GET" && url === "/v1/health") {
    const health: Health = { protocol: BRIDGE_ID, version: config.version, ok: true };
    return json(res, 200, Health.parse(health));
  }
  if (req.method === "POST" && (url === "/v1/plan" || url === "/v1/edit")) return stream(config, url, req, res);
  json(res, 404, { error: { code: "bad_request", message: `no route ${req.method} ${url}` } });
}

function json(res: http.ServerResponse, status: number, body: unknown) {
  res.writeHead(status, { "content-type": "application/json" });
  res.end(JSON.stringify(body));
}

async function readBody(req: http.IncomingMessage): Promise<unknown> {
  const chunks: Buffer[] = [];
  let size = 0;
  for await (const chunk of req) {
    size += (chunk as Buffer).length;
    if (size > MAX_BODY_BYTES) throw new FlowstateError("bad_request", "request body too large");
    chunks.push(chunk as Buffer);
  }
  try {
    return JSON.parse(Buffer.concat(chunks).toString("utf8"));
  } catch {
    throw new FlowstateError("bad_request", "request body is not JSON");
  }
}

const zodIssues = (error: { issues: { path: PropertyKey[]; message: string }[] }) =>
  error.issues.slice(0, 5).map((i) => `${i.path.join(".") || "(root)"}: ${i.message}`).join("; ");

/** The model for a request: the managed default, another managed model, or the user's own key (BYOK). */
export function selectionFor(config: ServiceConfig, provider: ProviderChoice | null, key: string | undefined): ModelSelection {
  const reasoning = config.managed.reasoning ? { reasoning: config.managed.reasoning } : {};
  if (key !== undefined) {
    if (!config.features.byok) throw new FlowstateError("unavailable", "Using your own key is turned off");
    if (!provider) throw new FlowstateError("bad_request", "a key needs a provider and model");
    return { ...provider, credential: { kind: "byok", apiKey: key }, ...reasoning };
  }
  if (!provider) return config.managed;
  const name = `${provider.provider}/${provider.model}`;
  if (name !== `${config.managed.provider}/${config.managed.model}` && !config.managedAlso.includes(name))
    throw new FlowstateError("bad_request", `${name} isn't offered with managed access; add your own key for it`);
  return { ...provider, credential: { kind: "managed" }, ...reasoning };
}

// HTTP status for an error before the stream starts. Once it has started, errors arrive as `error` events.
const STATUS: Partial<Record<ErrorCode, number>> = { bad_request: 400, unauthorized: 401, rate_limited: 429, unavailable: 503, internal: 500 };

async function stream(config: ServiceConfig, url: string, req: http.IncomingMessage, res: http.ServerResponse) {
  const started = Date.now();
  const requestId = String(req.headers["x-flowstate-request-id"] ?? randomUUID()).slice(0, 100);
  const rawKey = req.headers[KEY_HEADER];
  const key = Array.isArray(rawKey) ? rawKey[0] : rawKey;
  const redact = (text: string) => (key && key.length >= 4 ? text.split(key).join("[key]") : text);
  const line: Record<string, unknown> = { at: new Date(started).toISOString(), requestId, route: url, credential: key === undefined ? "managed" : "byok" };

  // Cancelling: the client closes the connection, and the provider stream stops with it.
  const abort = new AbortController();
  res.on("close", () => {
    if (!res.writableFinished) abort.abort();
  });

  let keepalive: NodeJS.Timeout | undefined;
  let release = () => {};
  const open = (status: number, extra: Record<string, string> = {}) => {
    res.writeHead(status, {
      "content-type": "text/event-stream; charset=utf-8",
      "cache-control": "no-cache",
      "x-accel-buffering": "no",
      "x-flowstate-request-id": requestId,
      ...extra,
    });
    res.flushHeaders();
    if (status === 200) keepalive = setInterval(() => res.write(": keepalive\n\n"), config.keepaliveMs);
  };
  // Every event is checked against the contract before it goes out.
  const send = (event: ServiceEvent) => {
    if (!res.destroyed) res.write(`data: ${JSON.stringify(ServiceEvent.parse(event))}\n\n`);
  };
  const finish = (status: string, extra: Record<string, unknown> = {}) => {
    clearInterval(keepalive);
    release();
    if (!res.destroyed) res.end();
    config.log({ ...line, status, ms: Date.now() - started, ...extra });
  };
  const fail = (err: unknown) => {
    const code = abort.signal.aborted ? "cancelled" : errorCode(err);
    const message = code === "internal" ? "internal error" : redact(err instanceof Error ? err.message : String(err));
    if (!res.headersSent) open(STATUS[code] ?? 502, err instanceof RateLimited ? { "retry-after": String(err.retryAfterS) } : {});
    send({ type: "error", error: { code, message } });
    finish(code === "cancelled" ? "cancelled" : "error", { code, ...(code === "internal" ? { detail: redact(String(err)).slice(0, 300) } : {}) });
  };

  // What the request runs: a plan or an edit, both streaming the same events.
  let job: (hooks: JobHooks) => Promise<JobResult>;
  try {
    if (config.access) {
      const tester = config.access.authenticate(req.headers.authorization);
      line.tester = tester;
      release = config.access.admit(tester);
    }
    const raw = await readBody(req);
    if (url === "/v1/edit") {
      const parsed = EditRequest.safeParse(raw);
      if (!parsed.success) throw new FlowstateError("bad_request", `invalid EditRequest: ${zodIssues(parsed.error)}`);
      const body = parsed.data;
      const edit: EditorRequest = { kind: body.kind, prompt: body.prompt, score: body.score, partIds: body.partIds, role: body.role };
      rulesFor(edit); // a request that can't be served fails before the stream
      const selection = selectionFor(config, body.provider, key);
      Object.assign(line, { kind: body.kind, provider: selection.provider, model: selection.model, promptChars: body.prompt.length });
      const backend = config.backendFor(selection);
      job = async (hooks) => {
        const r = await editScore(edit, backend, { signal: abort.signal, ...hooks });
        return { ...r, changed: r.changed };
      };
    } else {
      const parsed = PlanRequest.safeParse(raw);
      if (!parsed.success) throw new FlowstateError("bad_request", `invalid PlanRequest: ${zodIssues(parsed.error)}`);
      const body = parsed.data;
      if (body.reference) throw new FlowstateError("unavailable", "Planning around a reference isn't built yet");
      const planned = plannerRequest(body);
      keptDraft(planned);
      const selection = selectionFor(config, body.provider, key);
      Object.assign(line, { provider: selection.provider, model: selection.model, promptChars: body.prompt.length });
      const backend = config.backendFor(selection);
      job = async (hooks) => {
        const r = await planScore(planned, backend, { signal: abort.signal, ...hooks });
        return { ...r, message: null, changed: null };
      };
    }
  } catch (err) {
    return fail(err);
  }

  open(200);
  // The header goes first. Part events that would come before it (a head that only passed after a repair)
  // are dropped: the parts are sent from the final score instead.
  let headerSent = false;
  let parts = 0;
  try {
    const result = await job({
      onHead: (score) => {
        if (parts > 0) return;
        headerSent = true;
        send({ type: "header", score });
      },
      onPartStarted: (partId, role) => headerSent && send({ type: "partStarted", partId, role }),
      onPart: (part) => {
        if (!headerSent) return;
        parts++;
        line.firstPartMs ??= Date.now() - started;
        send({ type: "partDone", part });
      },
    });
    if (result.validationErrors.length > 0)
      throw new FlowstateError("invalid_score", `the ${url === "/v1/edit" ? "edit" : "plan"} is still invalid after repairs: ${result.validationErrors.slice(0, 3).join("; ")}`);
    if (result.message) send({ type: "message", text: result.message });
    // An edit streams only the parts it changed; the rest are the request's. A text-only answer has no score.
    if (result.score && !headerSent) sendWhole(send, result.score, result.changed);
    send({ type: "done", score: result.score });
    finish("done", { attempts: result.attempts, firstPartMs: line.firstPartMs ?? null, tokensOut: result.usage.outputTokens });
  } catch (err) {
    fail(err);
  }
}

interface JobHooks {
  onHead: (score: Score) => void;
  onPartStarted: (partId: string, role: Role) => void;
  onPart: (part: Part) => void;
}

interface JobResult {
  score: Score | null;
  message: string | null;
  /** An edit's changed parts; null for a plan (every part is new). */
  changed: string[] | null;
  validationErrors: string[];
  attempts: number;
  usage: { outputTokens: number };
}

function sendWhole(send: (e: ServiceEvent) => void, score: Score, only: string[] | null = null) {
  send({ type: "header", score: { ...score, parts: [] } });
  for (const part of score.parts) {
    if (only && !only.includes(part.id)) continue;
    send({ type: "partStarted", partId: part.id, role: part.role });
    send({ type: "partDone", part });
  }
}

/** The planner's request from the bridge's: the session context fixes key, meter, tempo and length. */
export function plannerRequest(req: PlanRequest): PlannerRequest {
  const c = req.context;
  return {
    prompt: req.prompt,
    controls: {
      tonic: c.tonic, mode: c.mode, bars: c.bars, tempo: c.tempo,
      meterNumerator: c.meterNumerator, meterDenominator: c.meterDenominator,
      style: c.style,
      lanes: lanesFor(req),
    },
    ...(req.keep ? { keep: req.keep } : {}),
  };
}

// The lanes the model writes: the requested roles, less any role a kept part already plays.
function lanesFor(req: PlanRequest): string[] {
  const kept = new Set((req.keep?.parts ?? []).map((p) => p.role));
  const lanes = [...new Set(req.roles ?? DEFAULT_ROLES)].filter((r) => !kept.has(r));
  if (req.keep && lanes.length === 0) throw new FlowstateError("bad_request", "every requested part is locked: unlock one to generate");
  return lanes;
}
