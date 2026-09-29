# Flowstate v2

**Describe it, hear it in your session, drag it in.**

Flowstate is an AI co-writer that lives on a MIDI track. The model writes a musical plan (the score IR); a deterministic C++ engine performs it in time with your DAW.

This repo is in **Phase 0**: salvage from v1, plus spikes that decide the plan. See `docs/roadmap.md`.

- `docs/product.md`: what we're building and what we're not
- `docs/architecture.md`: runtime shape and technology decisions
- `docs/ir-spec.md`: the score IR, the product's central contract
- `AGENTS.md`: working rules and commands

```sh
npm install
npm run build:core && npm run test:core
npm run typecheck
```
