# ui

The plugin's WebView UI, bundled into the binary by `plugin/cmake/juce-targets.cmake` and served
through JUCE's resource provider. It speaks the bridge (`docs/bridge-spec.md`) through one native
function, `bridge`, and one event, `bridge`. JUCE's interop library is served as
`juce_interop.js` from the JUCE checkout.

This is the P1-6 placeholder: plain HTML/JS, no build step, just enough to prove the bridge in
real hosts. The design system (P1-8) and the Studio screen (P1-9) replace it with the TypeScript
UI; its build output then becomes what the plugin bundles.
