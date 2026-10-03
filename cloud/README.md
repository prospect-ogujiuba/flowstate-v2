# cloud

The agent service: the HTTP API the plugin calls (`src/service.ts`), the planner (prompt -> score IR) and its model backends.

Run everything from the repo root. The planner realizes each score with `core`'s `fs-realize`, so build it once first: `npm run build:core`.

## Pick a backend

The backend comes from the environment (`selectionFromEnv` in `src/backends.ts`). `--provider` and `--model` on the command line override it.

| Backend | Set | Key |
| --- | --- | --- |
| `claude-code` (default): headless Claude Code on your own subscription. Dev and evals only. | nothing | your `claude` login |
| `pi`: any provider pi-ai supports | `FLOWSTATE_PLANNER_BACKEND=pi`, then `--provider <id> --model <id>` | the provider's usual variable: `OPENROUTER_API_KEY`, `DEEPSEEK_API_KEY`, `ANTHROPIC_API_KEY`, `OPENAI_API_KEY`, `GEMINI_API_KEY`, ... |

Optional: `FLOWSTATE_PLANNER_REASONING` (`minimal`, `low`, `medium`, `high`, `xhigh`, `max`; default `high`). An unknown model fails fast and lists the provider's known model ids.

Keep keys in your shell profile or a local, untracked env file, never in the repo.

## Plan the Phase 0 set

```sh
# Default backend (Claude Code)
npm run -w cloud plan -- --prompts ../evals/prompts/phase0.json --out ../evals/out/v2

# A provider through pi-ai
export FLOWSTATE_PLANNER_BACKEND=pi OPENROUTER_API_KEY=...
npm run -w cloud plan -- --prompts ../evals/prompts/phase0.json --out ../evals/out/gpt-5.5 \
  --provider openrouter --model openai/gpt-5.5
```

Paths are relative to `cloud/`, because `npm run -w cloud` runs there.

Useful flags:

| Flag | Effect |
| --- | --- |
| `--only id1,id2` | Only these prompt ids (see `evals/prompts/phase0.json`). One prompt is a cheap smoke test. |
| `--concurrency N` | Prompts in flight at once (default 4). Lower it if a provider rate-limits or runs out of credit. |
| `--reasoning <level>` | Thinking effort, overriding `FLOWSTATE_PLANNER_REASONING`. Most of the planner's output tokens are thinking, so this is the biggest latency knob. On `claude-code` it maps to `--effort` (`minimal` becomes `low`). |
| `--examples N` | Put up to N matching library clips in the request as style examples. |
| `--tools` | Offer the capabilities' tools (`library_examples`, `analyze_clip`; `pi` only). `run.json` records each prompt's tool calls. Off by default: a call costs a model round trip. |
| `--plan-only` | Write `*.score.json` only and skip realizing. |
| `--realize-only` | Realize existing `*.score.json` again (after a `core` change) without model calls. |

Each run writes, per prompt, `<id>.score.json`, `<id>.mid`, `<id>.notes.json` and `<id>.report.json`. It also writes `<id>.replies.txt` (the raw model replies, one per attempt) and `run.json`, and prints a one-line summary at the end. `run.json` holds the backend and reasoning level, plus validity rate and p50/p95 stats. Per prompt it records latency, attempts, tokens, time to first token, time to first answer text, and time to the first playable part. Parts stream out of the reply while it is still being written, so the first part can sound before the plan is done. It also records which streamed parts were not playable, and why.

A latency comparison, e.g. a fast model at low effort:

```sh
npm run -w cloud plan -- --prompts ../evals/prompts/phase0.json --out ../evals/out/sonnet-low \
  --provider claude-code --model sonnet --reasoning low
```

## Run the service

```sh
npm run serve        # from the repo root: http://127.0.0.1:8787
```

The managed default model is the planner's backend from the environment (see "Pick a backend"). The production route from P1-2 is DeepSeek Flash with thinking off:

```sh
export FLOWSTATE_PLANNER_BACKEND=pi FLOWSTATE_PLANNER_PROVIDER=deepseek \
  FLOWSTATE_PLANNER_MODEL=deepseek-flash FLOWSTATE_PLANNER_REASONING=off DEEPSEEK_API_KEY=...
npm run serve
```

| Variable | Effect |
| --- | --- |
| `FLOWSTATE_SERVICE_HOST`, `FLOWSTATE_SERVICE_PORT` | Where it listens (default `127.0.0.1:8787`) |
| `FLOWSTATE_SERVICE_MANAGED_MODELS` | More `provider/model` pairs offered on the service's keys, comma-separated (e.g. `openrouter/google/gemini-3.1-flash-lite`). A request for any other model needs the user's key. |
| `FLOWSTATE_FEATURE_BYOK` | `0` turns bring-your-own-key off (the `byok` release flag; default on) |
| `FLOWSTATE_BUILD_ID` | The version the health endpoint reports (default `dev`) |
| `FLOWSTATE_SERVICE_TOKENS_FILE` | Tester tokens (see "Hosting"). Without it the service is open, and it refuses any host but loopback. |
| `FLOWSTATE_SERVICE_CONCURRENT`, `FLOWSTATE_SERVICE_PER_HOUR` | Per-tester limits with tokens on (default 4 streams at once, 120 requests an hour) |

Routes (contract: `docs/bridge-spec.md`, "Plugin ↔ agent service"):
- `GET /v1/health`
- `POST /v1/plan` with a `PlanRequest`: an SSE stream of `header`, `partStarted`/`partDone` per part, then `done` or `error`. Close the connection to cancel; the provider call stops with it.
- `POST /v1/edit` with an `EditRequest` (`edit`, `vary` or `addPart` on the current score). It streams the edited head, the changed parts only, a one-line `message`, then `done` with the whole edited score. Locked parts never change; a question gets a text answer (`done` with `null`).

Try it with the fixture request:

```sh
node -e 'console.log(JSON.stringify(require("./schema/fixtures/bridge/PlanRequest.json").valid[0]))' > /tmp/req.json
curl -N -X POST localhost:8787/v1/plan --data @/tmp/req.json
```

Each finished request logs one JSON line to stdout: request id, route, tester (with tokens on), provider and model, managed or BYOK, status, error code, timings and token count. It never holds keys, headers or prompt text, and error messages have the user's key replaced by `[key]`.

## Hosting

The hosted service (P1-13) is the same server in a container behind Caddy. `deploy/README.md` covers the image, the server setup and the CI settings. `npm run deploy:service` deploys main.

A hosted service needs a tester token on `/v1/plan` and `/v1/edit` (`Authorization: Bearer fst_...`). `/v1/health` stays open.
- `npm run -w cloud token -- <tester>` mints a token. It prints the token once, and the line for the tokens file: `<tester> <sha256 of the token>`.
- The service stores only the hashes.
- Without a token the answer is `401` and `unauthorized`. Over a limit it is `429` with `Retry-After` and `rate_limited`.

## Edit eval

```sh
npm run -w cloud edit -- --prompts ../evals/prompts/edits-phase1.json --bases ../evals/results/p1-18/agent-deepseek-2 \
  --out ../evals/out/edits --provider deepseek --model deepseek-flash --reasoning off
```

Each case edits one of the base scores. `run.json` records validity, whether the edit changed the parts it should (`targeted`), repairs, and latency (`evals/results/edits/README.md`).

## Then measure

```sh
npm run -w evals metrics -- out/gpt-5.5
```

This writes `metrics.json` next to the run. Blind A/B packs and scoring: `evals/README.md`. Runs worth keeping go in `evals/results/<issue>/` with a README (see `evals/results/p1-1/`).

## Tests

```sh
npm run -w cloud test
```

These use pi-ai's faux provider or stub backends, so they need no keys or network. `src/service.test.ts` is the service's contract test: every event it reads is checked against the bridge schema.
