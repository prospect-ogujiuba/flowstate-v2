# cloud

The agent service. Today this is the planner (prompt -> score IR) and its model backends; the HTTP service arrives in P1-4.

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
| `--plan-only` | Write `*.score.json` only and skip realizing. |
| `--realize-only` | Realize existing `*.score.json` again (after a `core` change) without model calls. |

Each run writes, per prompt, `<id>.score.json`, `<id>.mid`, `<id>.notes.json` and `<id>.report.json`. It also writes `<id>.replies.txt` (the raw model replies, one per attempt) and `run.json`, and prints a one-line summary at the end. `run.json` holds the backend and reasoning level, plus validity rate and p50/p95 stats. Per prompt it records latency, attempts, tokens, time to first token, time to first answer text, and time to the first playable part. Parts stream out of the reply while it is still being written, so the first part can sound before the plan is done. It also records which streamed parts were not playable, and why.

A latency comparison, e.g. a fast model at low effort:

```sh
npm run -w cloud plan -- --prompts ../evals/prompts/phase0.json --out ../evals/out/sonnet-low \
  --provider claude-code --model sonnet --reasoning low
```

## Then measure

```sh
npm run -w evals metrics -- out/gpt-5.5
```

This writes `metrics.json` next to the run. Blind A/B packs and scoring: `evals/README.md`. Runs worth keeping go in `evals/results/<issue>/` with a README (see `evals/results/p1-1/`).

## Tests

```sh
npm run -w cloud test
```

These use pi-ai's faux provider, so they need no keys or network.
