// The plugin page: the Studio (P1-9). Inside the plugin it talks to the real bridge; in a browser
// (`npm run -w ui dev`, the Playwright tests) it runs against the mock plugin in host/mock.ts.

import { render } from "preact";
import { ToastProvider } from "../components/index.ts";
import { inPlugin } from "../host/env.ts";
import type { Bridge } from "../host/types.ts";
import { App } from "./App.tsx";
import "./studio.css";

async function connect(): Promise<Bridge> {
  if (inPlugin()) return (await import("../host/bridge.ts")).pluginBridge;
  const { MockPlugin, mockOptions } = await import("../host/mock.ts");
  const mock = new MockPlugin(mockOptions(new URLSearchParams(location.search)));
  window.__flowstateMock = mock;
  return mock;
}

void connect().then((bridge) => render(<ToastProvider><App bridge={bridge} /></ToastProvider>, document.getElementById("app")!));
