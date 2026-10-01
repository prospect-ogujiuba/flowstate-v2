# ui

The plugin's WebView UI: TypeScript, Preact and Vite. `npm run build:ui` writes `ui/dist`, which
`plugin/cmake/juce-targets.cmake` bundles into the binary; the plugin serves it through JUCE's
resource provider, with JUCE's interop library next to it as `juce_interop.js`. The output is flat
(no directories) because the resource provider looks files up by name.

| Page | What |
| --- | --- |
| `index.html` → `src/placeholder/` | The P1-6 placeholder that proves the bridge; the Studio screen (P1-9) replaces it |
| `gallery.html` → `src/gallery/` | The component gallery (P1-8). In the plugin: start the DAW with `FLOWSTATE_UI_PAGE=gallery.html` |

- `src/components/`, `src/styles/`: the design system. The spec is `docs/design/README.md`.
- `src/host/`: the bridge (`docs/bridge-spec.md`) and the keys that belong to the host.

```sh
npm run -w ui dev            # dev server; open /gallery.html
npm run -w ui test           # build, then Playwright (Chromium; PW_WEBKIT=1 adds WebKit)
npm run -w ui side-by-side   # docs/design/side-by-side-*.png against v1's screenshots
```
