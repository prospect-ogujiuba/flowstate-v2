# library

Flowstate's built-in MIDI library: packs of clips that ship with the plugin, each with its credit. The spec (manifest, analysis, catalog, search, adding a pack) is `../docs/library.md`.

From the repo root, after `npm run build:core`:

```sh
npm run -w library validate -- packs/<pack-id>   # every entry imports, or prints the reason it can't
npm run -w library build                        # regenerate catalog/ (catalog.json + normalized clips); commit it
npm run -w library check                        # what CI runs: fails if catalog/ is stale
npm run -w library test
```

`packs/` holds the sources as delivered. `catalog/` is generated, and it is what the plugin bundles.
