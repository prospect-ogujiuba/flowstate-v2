# Design system

v1's visual identity rebuilt as CSS tokens and Preact components (roadmap P1-8). The style is dark, compact and textured, with v1's yellow accent. Everything lives in `ui/src`; the gallery (`ui/gallery.html`) shows every component and state.

| What | Where |
| --- | --- |
| Tokens | `ui/src/styles/tokens.css` |
| Base styles (page, focus ring, `.sr-only`) | `ui/src/styles/base.css` |
| Components and their styles | `ui/src/components/*.tsx`, `components.css`, exported from `components/index.ts` |
| Icons | `ui/src/assets/icons`, `ui/src/assets/status` |
| Gallery | `ui/src/gallery/` |
| Host keys (Space and Escape) | `ui/src/host/keys.ts` |

Run the gallery in a browser with `npm run -w ui dev`, then open `/gallery.html`. To see it in the plugin, start the DAW with `FLOWSTATE_UI_PAGE=gallery.html` (`testing-plugin.md`, check 12).

## Sources

v1 wrote its colours inline as `juce::Colour::fromRGB` calls, about 60 distinct values across `../flowstate/Source/ui`. Its sizes are constants in `Source/state/FlowstateState.h` (`Typography`, `Layout`). The design tree and screenshots are in `../flowstate/docs/design`.

Where v1's design screenshots and its shipped code disagree, the tokens follow the code. For example, the screenshots draw the settings modal about 290 px wide, but v1 shipped `Layout::modalWidth = 560`, so the sheet is 560 px.

## Tokens

Components use tokens only, never raw colours. Canvas drawing reads tokens with `cssVar()` (`components/css.ts`).

| Group | Tokens | v1 source |
| --- | --- | --- |
| Surfaces | `--bg` #070708 with `--bg-texture` (a 9 px diamond dot grid); `--bar-bg` (wood grain); `--surface` #1f1f22; `--surface-2` #21262c; `--panel`; `--card`; `--overlay` | `main-bg.png` (average #090909, repeating about every 9 px), `header-bg.png`, popup menu, assistant bubble, Home view panel (10,12,20,95), Home quick-start cards (32,36,54,206), modal scrim |
| Controls | `--control` #222, `--control-hover`, `--control-press` #484f66, `--glass` | Nav buttons (34,34,34), pressed header button (72,82,118), header icon buttons (white at alpha 22) |
| Inputs | `--input-bg` #f6f8fc, `--input-text`, `--input-border` | v1's settings fields are light (246,248,252) |
| Lines | `--line` (white at 15%), `--line-soft`, `--line-strong` #5c5c5c | Field outline (alpha 38), nav outline (92,92,92) |
| Text | `--text`, `--text-2`, `--text-3` #bec4d2, `--placeholder` #878787 | (246,248,255,238), (232,236,255,…), (190,196,210), (135,135,135) |
| Brand | `--accent` #ffe800, `--accent-soft`, `--on-accent` | v1's yellow (255,232,0), the most used colour, on active nav, track chip and submit |
| Interaction | `--focus` #96e1ff, `--user` #1088ff, `--idea` #ffcc33 | Focus outline, user bubble, lightning and lightbulb icons |
| Status | `--ok`, `--warn`, `--danger`, `--danger-solid`, `--toast-*` | Status states (86,255,100), (255,193,92), (255,116,128), toast backgrounds |
| Meter | `--meter-low/mid/high/clip`, `--meter-gradient` | Footer meter stops: 0, 78%, 95%, 100% |
| Parts | `--part-chords/bass/melody/drums/extra`, `--roll-bar` | New in v2; each reuses a palette colour. Bar lines are v1's sequencer preview yellow (255,232,0,84) |
| Type | `--font` (system sans), `--text-2xs` 10.5 px to `--text-2xl` 21 px, weights 400 and 700, `.fs-num` for tabular numbers | v1 used JUCE's default sans; sizes from `Typography` and the inline `FontOptions` |
| Spacing | `--space-0` 2 px to `--space-8` 28 px, on a 4 px base | `Layout`: gap 8, inner 10, outer 12, header padding 28 |
| Radii | `--radius-sm` 4, `-md` 6, `-lg` 10, `-xl` 14, `-2xl` 24, `-pill` | `modalCornerRadius` 14, prompt box |
| Motion | `--dur-fast` 120 ms, `--dur-base` 200 ms; both 0 under `prefers-reduced-motion` | — |

The UI is dark only, like v1.

## Components

| Component | Role and keyboard | Notes |
| --- | --- | --- |
| `Button` | `button`; Enter | Variants: primary, secondary, ghost, ok, danger, nav. `pressed` makes it a toggle (`aria-pressed`). Icon-only buttons must have a `label`; so should short text like "S". `unavailable` (a reason): see below. |
| `Segmented` | `radiogroup`; Tab enters, arrows move | v1's Chat/Create switch |
| `Toggle` | `switch`; Enter | Its visible label is its name |
| `Knob` | `slider`; arrows, PageUp/PageDown (10×), Home/End | Drag up or down (Shift for fine), double-click resets. A range that spans zero fills from the centre. Takes `unavailable`. |
| `TextField`, `TextArea`, `Select`, `SecretField` | Native controls with a `<label>` | Hint and error text via `aria-describedby`, `aria-invalid` on errors; SecretField has a show/hide toggle |
| `ContextChip`, `SuggestionChip`, `Badge` | `button`, `button`, text | A context chip's name says where the value comes from ("from host" when locked, "from the idea", "overridden"). `SuggestionChip` takes `unavailable`. |
| `UsageMeter`, `LevelMeter` | `meter`, `img` | The level meter is a canvas, fed at no more than 30 Hz |
| `PianoRoll` | `img` with a note-count name | A canvas; notes arrive in beats, and the roll does no theory |
| `Lane` | `article` named "<part> lane" | Grip (drag to DAW; Enter saves a file instead), mute, solo, lock, vary, re-roll, density knob, and optionally edit notes and remove; drums add a sublanes disclosure (`aria-expanded`). Lock disables vary, re-roll and density. `gaps` marks controls this build can't run. |
| `Card`, `ResultCard` | `section`, `article` with `aria-current` | Thread result: play, restore, branch, drag |
| `Sheet`, `SheetSection` | Native `<dialog>` (modal) | Inert background, Tab stays inside, Escape and Close close it, focus returns to the opener |
| `ToastProvider`, `useToast` | A `region` holding `status`, or `alert` for errors | Errors stay until dismissed; the rest leave after 5 s, paused while hovered or focused |
| `Logo`, `ConnectionStatus` | `img` | Connecting steps through v1's four signal frames |
| `PromptBox`, `PromptSetting` | Form with a labelled textarea | Enter sends, Shift+Enter adds a line, and empty prompts can't be sent |

## Not in this build

The plugin lists what it can't run yet in `Session.unavailable`, each with a reason (`docs/bridge-spec.md`, "Gaps"). Such a control stays visible, so the product's shape shows, but it never sends a command:
- It looks disabled (`.is-unavailable`, dimmed, a help cursor) and has `aria-disabled="true"`.
- It stays focusable. Its reason is its accessible description and its tooltip.
- Enter or a click shows the reason as a toast instead of running.

`Button`, `Knob` and `SuggestionChip` take the reason as `unavailable`; the Studio's starter cards and prompt mode do the same by hand. Don't use `disabled` for this: a disabled control can't be focused or explain itself.

## Keyboard and the host

- Space belongs to the DAW. Unless focus is in a text entry (input, textarea, select), Space releases focus to the host (`releaseFocus`). So controls activate with Enter, never Space.
- Escape in a text entry releases focus too, except inside an open sheet, where Escape closes the sheet.
- A text entry that loses focus to nothing (a click on empty space) releases focus with `blur`. Tabbing to another control keeps focus in the plugin.
- The Studio's only shortcuts are undo (Ctrl/Cmd+Z) and redo (Shift+Ctrl/Cmd+Z, Ctrl+Y), active only while the plugin has focus and no text entry or sheet is.
- The page itself never scrolls; scrolling containers sit inside it. `#app` is positioned so `.sr-only` labels can't extend the document.
- On macOS, WKWebView follows the system's Keyboard navigation setting. With it off, Tab reaches only text fields and Option-Tab reaches the rest.

## Icons

- **From v1:** `ui/src/assets/icons` holds v1's single-colour SVGs, recoloured to `currentColor`. `ui/src/assets/status` holds the connection and power states, coloured by status tokens.
- **Left out:** `plugin-settings.svg` has fixed greys (`sliders` replaces it); `message-tail.svg` belonged to the chat bubbles; `logo.svg` is a 140 KB PNG in an SVG wrapper, so `logo.png` (24 KB) is used instead.
- **Drawn for v2** in the same 16 px style: `lock`, `lock-open`, `play`, `stop`, `grip`, `branch`, `restore`, `sparkle`, `plus`, `eye`, `menu`, `undo` and `redo`.

## v1 reference screenshots

`docs/design/v1/` holds v1's screens, captured from the v1 Standalone on 2026-10-02 (build `806d8a4`, Linux): `home`, `chat`, `chat-sessions` (the Chats drawer), `chat-prompt-library`, `context`, `compose`, `create`, `settings` and `ai-connection`. The Studio (P1-9) sits inside this shell; the roadmap's P1-9 says which v1 piece maps onto which Studio part. v1 couldn't generate without its old account login, so there is no screenshot of a filled lane; its preview is `MidiSequencerPreview` in `../flowstate/Source/ui/`.

## The Studio in v1's shell

The Studio (`ui/src/studio/`, roadmap P1-9) maps v1's screens onto one:

| v1 | Studio |
| --- | --- |
| `HeaderBar`: nav pills, logo, Track 1 chip, connection, power, settings, account | Studio and Library pills, the logo (back to the Studio), a yellow pill that says what this instance sends (opens MIDI out), the connection meter, settings and account. No power button: v1 used it for a development login. |
| `FooterBar`: AI warning, level meter, copyright | The same, with the meter showing where the host is in the idea; the warning hides below 760 px. |
| Home: "What do you want to make today?", status banner, action cards | The empty Studio: the same heading, the banner from the AI service's last state, and the first-run starters. |
| Context tab | The context strip; the chips open one Context sheet. |
| Compose lanes | The part lanes, all visible at once, with v1's yellow bar lines in each roll. |
| Chat box, prompt library | The prompt bar; its suggestion chips change with state. |
| Chats drawer | The thread drawer: docked at the side from 960 px wide, over the lanes below that. |
| Settings and AI Connection modals | One settings sheet. |

At heights under 600 px the header, footer, lanes and prompt box shrink (`data-compact`), so 720×480 shows the strip, two lanes and the prompt bar without page scroll; the stage scrolls inside.

## Side by side with v1

`npm run -w ui side-by-side` writes `side-by-side-chat.png` and `side-by-side-settings.png` here. Each pairs v1's design screenshot with the v2 shell, which rebuilds v1's chat screen from the new components at v1's reference size (`gallery.html?only=shell`, 900×650).

Intended differences:
- The main area shows v2's part lanes instead of v1's chat transcript.
- Type and the settings sheet follow v1's shipped sizes, not the smaller design screenshots.
- The sheet has a close button.
- The level meter colours by absolute level, so the fill turns red only near full scale.

## Tests

`npm run -w ui test` builds the UI and runs Playwright (`ui/tests`): for the gallery and the Studio, axe (WCAG 2.1 AA), an accessible-name check and a Tab walk over every control; behaviour tests for each component; and the Studio's flows against the mock plugin at 720×480 and larger, plus one test through the real interop path. CI runs Chromium and WebKit (WebKit stands in for WKWebView). Locally, Chromium runs by default; WebKit runs with `PW_WEBKIT=1` once its system libraries are installed (`sudo npx playwright install-deps webkit`).
