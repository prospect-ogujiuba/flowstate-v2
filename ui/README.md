# ui

The plugin's WebView UI: TypeScript, Preact and Vite. `npm run build:ui` writes `ui/dist`, which
`plugin/cmake/juce-targets.cmake` bundles into the binary; the plugin serves it through JUCE's
resource provider, with JUCE's interop library next to it as `juce_interop.js`. The output is flat
(no directories) because the resource provider looks files up by name.

| Page | What |
| --- | --- |
| `index.html` → `src/studio/` | The Studio (P1-9): the plugin's one screen, inside v1's shell |
| `gallery.html` → `src/gallery/` | The component gallery (P1-8). In the plugin: start the DAW with `FLOWSTATE_UI_PAGE=gallery.html` |

- `src/components/`, `src/styles/`: the design system. The spec is `docs/design/README.md`.
- `src/studio/`: the Studio. `App.tsx` lays out v1's shell (`Shell.tsx`) around the context strip and toolbar, the part lanes, the prompt bar and the thread drawer; the library, settings, context, capture and tweak sheets sit beside them. `useStudio.ts` holds the session the plugin last sent.
- `src/host/`: the bridge (`docs/bridge-spec.md`), the keys that belong to the host, and `mock.ts`, a stand-in plugin for the browser.

Outside the plugin, `index.html` runs against the mock: it answers every command as the plugin does and streams canned parts. Query parameters set it up: `?mock=idea` starts with an idea, `gaps=none` turns on what this build can't do yet (`Session.unavailable`), `byok=0` hides the key field, `capture=N` sets the captured bars, `delay=ms` paces the stream, `playing=1` runs the host transport. The tests drive it through `window.__flowstateMock`.

Controls listed in `Session.unavailable` stay visible but dimmed (`aria-disabled`), with the plugin's reason as their description and tooltip; pressing one shows the reason instead of sending the command. When the plugin drops a gap, the control turns on with no UI change.

```sh
npm run -w ui dev            # dev server: / is the Studio (mock plugin), /gallery.html the components
npm run -w ui test           # build, then Playwright (Chromium; PW_WEBKIT=1 adds WebKit)
npm run -w ui side-by-side   # docs/design/side-by-side-*.png against v1's screenshots
```
