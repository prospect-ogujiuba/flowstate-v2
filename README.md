# Flowstate v2

**Describe it, hear it in your session, drag it in.**

Flowstate is an AI co-writer that lives on a MIDI track. The model writes a musical plan (the score IR); a deterministic C++ engine performs it in time with your DAW.

This repo is in **Phase 1** (core loop). Phase 0 closed with Gate A passed. See `docs/roadmap.md`.

- `docs/proposal.md`: the v2 analysis and redesign proposal (living document)
- `docs/product.md`: what we're building and what we're not
- `docs/architecture.md`: runtime shape and technology decisions
- `docs/ir-spec.md`: the score IR, the product's central contract
- `docs/bridge-spec.md`: messages between the WebView, the plugin and the agent service
- `docs/dev-setup.md`: tools and libraries to install on a new machine
- `docs/testing-plugin.md`: building, testing and validating the plugin, and manual DAW checks
- `CLAUDE.md`: working rules and commands

```sh
npm install
npm run build:core && npm run build:bridge
npm test
npm run typecheck
```
