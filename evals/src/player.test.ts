import assert from "node:assert/strict";
import { test } from "node:test";
import vm from "node:vm";
import { packKind, playerHtml, SHEET_HEADER, type PackKind, type PlayerEntry } from "./player.ts";
import { parseCsv } from "./score-ab.ts";

const entries: PlayerEntry[] = [
  { folder: "01-a", promptId: "a", prompt: "first", fields: {}, sound: "keys" },
  { folder: "02-b", promptId: "b", prompt: "second", fields: {}, sound: "keys" },
];

/** Runs the page's script against a minimal DOM; returns a way to change fields and the CSV it saves. */
function runPage(kind: PackKind) {
  const html = playerHtml("pack", entries, kind);
  const script = /<script>([\s\S]*)<\/script>/.exec(html)![1]!;
  type El = { id: string; value: string; textContent: string; innerHTML: string; onclick?: () => void; onchange?: () => void;
    listeners: Record<string, (() => void)[]>; [k: string]: unknown };
  const els = new Map<string, El>();
  const el = (id: string): El => {
    if (!els.has(id)) {
      const e: El = {
        id, value: "", textContent: "", innerHTML: "", listeners: {}, paused: true, currentTime: 0, tagName: "DIV",
        addEventListener(type: string, fn: () => void) { (this.listeners[type] ??= []).push(fn); },
        setAttribute() {}, appendChild() {}, pause() {}, play() {}, click() { (this as El).onclick?.(); },
      };
      els.set(id, e);
    }
    return els.get(id)!;
  };
  let saved = "";
  const context = {
    document: {
      getElementById: el,
      createElement: () => ({ ...el(`new-${els.size}`), click() { /* the download link */ } }),
      addEventListener() {},
      body: {},
    },
    localStorage: { getItem: () => null, setItem() {} },
    setInterval() {},
    Blob: class { constructor(parts: string[]) { saved = parts.join(""); } },
    URL: { createObjectURL: () => "blob:" },
  };
  vm.runInNewContext(script, context);
  return {
    html,
    set(id: string, value: string) { const e = el(id); e.value = value; for (const fn of e.listeners.change ?? []) fn(); },
    save() { el("export").onclick!(); return parseCsv(saved); },
    next() { el("next").onclick!(); },
    progress: () => el("progress").textContent,
  };
}

test("a re-roll pack asks 'same idea?' and saves the re-roll sheet, ready for ab:score", () => {
  const page = runPage("reroll");
  assert.match(page.html, /id="same"/);
  assert.doesNotMatch(page.html, /id="f1"/);
  page.set("m1", "4");
  page.set("m2", "3");
  page.set("pref", "1");
  assert.equal(page.progress(), "0 of 2 scored");  // not done until "same idea?" is answered
  page.set("same", "y");
  assert.equal(page.progress(), "1 of 2 scored");
  const rows = page.save();
  assert.equal(rows[0]!.join(","), SHEET_HEADER.reroll);
  assert.deepEqual(rows[1], ["a", "1", "4", "3", "y", ""]);
  assert.deepEqual(rows[2], ["b", "", "", "", "", ""]);
});

test("a generators pack keeps fits-the-prompt and its sheet", () => {
  const page = runPage("generators");
  assert.doesNotMatch(page.html, /id="same"/);
  page.set("f1", "5");
  page.set("pref", "tie");
  const rows = page.save();
  assert.equal(rows[0]!.join(","), SHEET_HEADER.generators);
  assert.deepEqual(rows[1], ["a", "tie", "", "", "5", "", ""]);
});

test("the pack kind comes from the scoresheet header", () => {
  assert.equal(packKind(SHEET_HEADER.reroll), "reroll");
  assert.equal(packKind(SHEET_HEADER.generators), "generators");
  assert.equal(packKind(""), "generators");
});
