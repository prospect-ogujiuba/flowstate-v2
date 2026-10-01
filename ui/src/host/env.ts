/** True inside the plugin WebView, where JUCE injects its backend. */
export const inPlugin = (): boolean => typeof window !== "undefined" && window.__JUCE__ !== undefined;
