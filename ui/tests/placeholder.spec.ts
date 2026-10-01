import { expect, test } from "@playwright/test";
import { readFileSync } from "node:fs";

// The plugin page against a mocked bridge: JUCE's interop module is stubbed, and each command
// gets the first valid Reply fixture that carries a session.
const replies = JSON.parse(readFileSync(new URL("../../schema/fixtures/bridge/Reply.json", import.meta.url), "utf8")).valid as { session: unknown }[];
const reply = replies.find((r) => r.session !== null)!;

const interop = `
  window.__sent = [];
  export function getNativeFunction(name) {
    return async (json) => { window.__sent.push(JSON.parse(json)); return JSON.stringify(${JSON.stringify(reply)}); };
  }`;

test("the plugin page loads the interop module, says hello and renders the session", async ({ page }) => {
  await page.addInitScript(() => {
    (window as unknown as { __JUCE__: unknown }).__JUCE__ = { backend: { addEventListener: () => {} } };
  });
  // Only at the root, next to the page: that's where the plugin serves it.
  await page.route((url) => url.pathname === "/juce_interop.js", (r) => r.fulfill({ contentType: "text/javascript", body: interop }));
  await page.goto("/index.html");

  await expect(page.locator("#ctx-key")).not.toHaveText("–");
  const sent = await page.evaluate(() => (window as unknown as { __sent: { type: string }[] }).__sent);
  expect(sent[0]).toEqual({ type: "hello", protocol: "flowstate.bridge.v0" });

  // Space with no text field focused goes back to the host.
  await page.getByRole("button", { name: "Undo" }).focus();
  await page.keyboard.press("Space");
  const last = await page.evaluate(() => (window as unknown as { __sent: { type: string }[] }).__sent.at(-1));
  expect(last).toEqual({ type: "releaseFocus", reason: "space" });
});
