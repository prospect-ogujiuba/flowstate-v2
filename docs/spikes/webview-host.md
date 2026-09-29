# Spike 2: WebView UI inside real hosts

**Question:** can a JUCE 9 `WebBrowserComponent` UI behave like a first-class plugin UI in the hosts that matter?

**Build:** `plugin/spike` (a minimal Flowstate Spike plugin: VST3 + AU + Standalone). The UI is one bundled HTML page served through the `WebBrowserComponent` resource provider. It has a text field, a button that calls a native function, a canvas piano roll of a loaded clip, and a drag handle.

## Checks (per host: Ableton Live 12, Logic Pro, one of FL Studio or Bitwig)

| # | Check | Pass when |
| --- | --- | --- |
| 1 | Load and scan | The plugin scans and opens without errors; no crash on close or reopen |
| 2 | Typing | Text entry works; typed keys don't trigger DAW shortcuts while the field has focus |
| 3 | Space bar | With no text field focused, space starts and stops the DAW transport |
| 4 | Drag-out | Dragging the handle drops a `.mid` clip on a DAW track at the drop position |
| 5 | Resize | Resizing from the plugin corner works; HiDPI is crisp |
| 6 | Instances | Three instances open at once stay responsive; memory per instance is under 150 MB |
| 7 | Bridge latency | A JS → C++ → JS round trip takes under 5 ms (the spike page shows it) |

Record results in `docs/spikes/results.md` as a host × check table, with notes on any workaround.

## If it fails

A failed drag start gets a native drag-handle component overlaid on the WebView. A failed focus or space-bar check gets JUCE key forwarding. If the WebView is unusable in a host, fall back to a native JUCE UI for that host's build. That decision is made at Gate A.
