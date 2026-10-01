// Side-by-side screenshots for P1-8's acceptance: v1's design screenshots next to the v2 shell,
// rebuilt from the new components at v1's reference size (900 x 650). Run after `vite build`:
//   npm run -w ui side-by-side            (V1_DESIGN overrides the v1 screenshot folder)
// Writes docs/design/side-by-side-{chat,settings}.png.
import { chromium } from "@playwright/test";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { preview } from "vite";

const root = resolve(import.meta.dirname, "..");
const v1 = process.env.V1_DESIGN ?? resolve(root, "../../flowstate/docs/design/screenshots");
const out = resolve(root, "../docs/design");
const shots = [
  { name: "chat", v1: "fs-chat.png", query: "only=shell" },
  { name: "settings", v1: "fs-settings.png", query: "only=shell&settings" },
];

const server = await preview({ root, preview: { port: 4177, host: "127.0.0.1", strictPort: true } });
const browser = await chromium.launch();
try {
  const page = await browser.newPage({ viewport: { width: 900, height: 650 } });
  for (const s of shots) {
    await page.goto(`http://127.0.0.1:4177/gallery.html?${s.query}`);
    await page.waitForTimeout(400);
    const v2 = await page.screenshot();

    const img = (buf: Buffer) => `data:image/png;base64,${buf.toString("base64")}`;
    const pair = await browser.newPage({ viewport: { width: 1840, height: 700 } });
    await pair.setContent(`<body style="margin:0;background:#111;color:#ddd;font:14px system-ui;display:flex;gap:40px;padding:0">
      <figure style="margin:0"><figcaption style="padding:8px 0">v1 design (${s.v1})</figcaption><img width="900" height="650" src="${img(readFileSync(`${v1}/${s.v1}`))}"></figure>
      <figure style="margin:0"><figcaption style="padding:8px 0">v2 components (gallery.html?${s.query})</figcaption><img width="900" height="650" src="${img(v2)}"></figure>
    </body>`);
    await pair.waitForFunction(() => [...document.images].every((i) => i.complete));
    await pair.screenshot({ path: `${out}/side-by-side-${s.name}.png`, fullPage: true });
    await pair.close();
    console.log(`wrote docs/design/side-by-side-${s.name}.png`);
  }
} finally {
  await browser.close();
  await server.close();
}
