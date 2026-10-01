// Dev helper: screenshots a page of the built UI. tsx scripts/shot.ts <out.png> [page] [width] [height] [selector]
import { chromium } from "@playwright/test";
import { preview } from "vite";

const [out = "shot.png", page = "gallery.html", w = "1100", h = "800", selector] = process.argv.slice(2);
const server = await preview({ preview: { port: 4175, host: "127.0.0.1" } });
const browser = await chromium.launch();
const p = await browser.newPage({ viewport: { width: Number(w), height: Number(h) } });
await p.goto(`http://127.0.0.1:4175/${page}`);
if (process.env.FULL) await p.addStyleTag({ content: "html,body{height:auto!important;overflow:visible!important}.g-scroll{height:auto!important}" });
await p.waitForTimeout(300);
if (selector) await p.locator(selector).screenshot({ path: out });
else await p.screenshot({ path: out, fullPage: Boolean(process.env.FULL) });
await browser.close();
await server.close();
