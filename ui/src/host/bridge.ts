// Typed access to the plugin bridge (docs/bridge-spec.md): one native function, `bridge`, and one
// event, `bridge`. Only loads inside the plugin WebView, where the page is served next to
// juce_interop.js; check `inPlugin()` before importing this module.

import type { Command, PluginEvent, Reply } from "@flowstate/schema";
import { getNativeFunction } from "juce-interop";
import type { Bridge } from "./types.ts";

declare global {
  interface Window {
    __JUCE__?: { backend: { addEventListener(name: string, fn: (payload: string) => void): void } };
  }
}

export const PROTOCOL = "flowstate.bridge.v0";

const native = getNativeFunction("bridge");

export async function send(command: Command): Promise<Reply> {
  return JSON.parse(String(await native(JSON.stringify(command)))) as Reply;
}

export function onEvent(fn: (event: PluginEvent) => void): void {
  window.__JUCE__?.backend.addEventListener("bridge", (json) => fn(JSON.parse(json) as PluginEvent));
}

export const pluginBridge: Bridge = { send, onEvent };
