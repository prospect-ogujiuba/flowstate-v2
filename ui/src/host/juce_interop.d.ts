// JUCE's WebView interop library. The plugin serves it next to the page as juce_interop.js; the
// build maps this module name to that file (vite.config.ts).
declare module "juce-interop" {
  export function getNativeFunction(name: string): (...args: unknown[]) => Promise<unknown>;
}
