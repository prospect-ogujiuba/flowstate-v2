import AxeBuilder from "@axe-core/playwright";
import { expect, test, type Page } from "@playwright/test";
import { readFileSync } from "node:fs";

// The Studio (P1-9) against the mocked bridge (ui/src/host/mock.ts): every flow in the proposal's
// UX section, the controls this build can't run yet, the host's keys, and sizes from 720 × 480 up.
// One test at the end drives the real interop path with JUCE's module stubbed.

type Sent = { type: string; [k: string]: unknown };
type Mock = {
  sent: Sent[];
  delay: number;
  fail(type: string, code: string, message: string): void;
  emit(event: unknown): void;
  setHost(patch: Record<string, unknown>): void;
  setGaps(gaps: { feature: string; reason: string }[]): void;
};
type MockWindow = { __flowstateMock: Mock };

async function open(page: Page, query = "") {
  await page.emulateMedia({ reducedMotion: "reduce" });
  await page.goto(`/index.html?delay=40${query ? `&${query}` : ""}`);
  await expect(page.getByRole("group", { name: "Context" })).toBeVisible();
}
const sent = (page: Page) => page.evaluate(() => (window as unknown as MockWindow).__flowstateMock.sent);
const sentOf = async (page: Page, type: string) => (await sent(page)).filter((c) => c.type === type);
const lastOf = async (page: Page, type: string) => (await sentOf(page, type)).at(-1);
const lane = (page: Page, name: string) => page.getByRole("article", { name: `${name} lane` });
const toast = (page: Page, text: string | RegExp) => page.getByRole("region", { name: "Notifications" }).getByText(text);

const CONTROLS = ["button:not([disabled])", "a[href]", "input:not([disabled])", "select:not([disabled])", "textarea:not([disabled])", "[role=slider]:not([tabindex='-1'])"].join(", ");

async function tagControls(page: Page) {
  return page.evaluate((sel) => {
    const els = [...document.querySelectorAll<HTMLElement>(sel)].filter((el) => el.checkVisibility() && !el.closest("dialog:not([open])"));
    els.forEach((el, i) => (el.dataset.ctl = String(i)));
    return els.map((el) => ({
      i: el.dataset.ctl!,
      name: el.getAttribute("aria-label") ?? el.textContent?.trim() ?? el.tagName,
      radio: el.getAttribute("role") === "radio",
      group: el.closest("[role=radiogroup]")?.getAttribute("aria-label") ?? null,
    }));
  }, CONTROLS);
}

// ---- Layout, accessibility and the host's keys, at the plugin's sizes ---------------------------

for (const [w, h] of [[720, 480], [900, 650], [1440, 900]] as const) {
  test.describe(`at ${w} × ${h}`, () => {
    test.use({ viewport: { width: w, height: h } });

    test("the shell fits without page scroll; header, lanes and prompt bar are on screen", async ({ page }) => {
      await open(page, "mock=idea");
      const overflow = await page.evaluate(() => ({
        x: document.documentElement.scrollWidth > innerWidth,
        y: document.documentElement.scrollHeight > innerHeight,
      }));
      expect(overflow).toEqual({ x: false, y: false });
      for (const el of [
        page.getByRole("navigation", { name: "Views" }),
        page.getByRole("button", { name: "Settings" }),
        page.getByRole("group", { name: "Context" }),
        lane(page, "Chords"),
        page.getByRole("textbox", { name: "Describe or ask" }),
        page.getByRole("button", { name: "Send" }),
        page.getByText(/Flowstate ©/),
      ]) await expect(el).toBeInViewport();
      // A lane further down scrolls into view inside the stage, not the page.
      await lane(page, "Melody").scrollIntoViewIfNeeded();
      await expect(lane(page, "Melody")).toBeInViewport();
      expect(await page.evaluate(() => document.scrollingElement!.scrollTop)).toBe(0);
    });

    test("no axe violations, empty and with an idea", async ({ page }) => {
      test.setTimeout(90_000); // two full axe runs; slow on a loaded machine
      for (const q of ["", "mock=idea"]) {
        await open(page, q);
        const results = await new AxeBuilder({ page }).withTags(["wcag2a", "wcag2aa", "wcag21a", "wcag21aa"]).analyze();
        expect(results.violations.map((v) => `${v.id}: ${v.nodes.map((n) => n.target.join(" ")).join(", ")}`)).toEqual([]);
      }
    });

    test("every control is named and Tab reaches it", async ({ page }) => {
      await open(page, "mock=idea");
      const controls = await tagControls(page);
      expect(controls.length).toBeGreaterThan(40);
      for (const c of controls) await expect(page.locator(`[data-ctl="${c.i}"]`)).toHaveAccessibleName(/\S/);
      const reached = new Set<string>();
      const groups = new Set<string>();
      await page.locator("body").focus();
      for (let n = 0; n < controls.length + 20; n++) {
        await page.keyboard.press("Tab");
        const tag = await page.evaluate(() => {
          const el = document.activeElement as HTMLElement | null;
          return { i: el?.dataset.ctl ?? null, group: el?.closest("[role=radiogroup]")?.getAttribute("aria-label") ?? null };
        });
        if (tag.i) reached.add(tag.i);
        if (tag.group) groups.add(tag.group);
      }
      const missed = controls.filter((c) => !reached.has(c.i) && !(c.radio && c.group && groups.has(c.group)));
      expect(missed.map((c) => c.name)).toEqual([]);
    });
  });
}

test("Space reaches the DAW: it never presses a control, but types in the prompt", async ({ page }) => {
  await open(page, "mock=idea");
  const mute = lane(page, "Chords").getByRole("button", { name: "Mute Chords" });
  await mute.focus();
  const before = (await sent(page)).length;
  await page.keyboard.press("Space");
  await expect(mute).not.toBeFocused();
  await expect(mute).toHaveAttribute("aria-pressed", "false");
  expect((await sent(page)).slice(before)).toEqual([{ type: "releaseFocus", reason: "space" }]);

  const input = page.getByRole("textbox", { name: "Describe or ask" });
  await input.fill("dusty");
  await input.press("Space");
  await expect(input).toHaveValue("dusty ");
  await expect(input).toBeFocused();
  await input.press("Escape");
  await expect(input).not.toBeFocused();
  expect(await lastOf(page, "releaseFocus")).toEqual({ type: "releaseFocus", reason: "escape" });
});

test("Tabbing out of the prompt keeps focus in the plugin; clicking away hands it to the host", async ({ page }) => {
  await open(page, "mock=idea");
  const input = page.getByRole("textbox", { name: "Describe or ask" });
  await input.focus();
  await page.keyboard.press("Tab");
  expect(await sentOf(page, "releaseFocus")).toEqual([]);
  await input.focus();
  await page.evaluate(() => (document.activeElement as HTMLElement).blur());
  expect(await lastOf(page, "releaseFocus")).toEqual({ type: "releaseFocus", reason: "blur" });
});

// ---- Flow 1: first run ------------------------------------------------------------------------

test("first run: the empty Studio offers starters, and Surprise me plays a full idea", async ({ page }) => {
  await open(page);
  expect((await sent(page))[0]).toEqual({ type: "hello", protocol: "flowstate.bridge.v0" });
  await expect(page.getByRole("heading", { name: "What do you want to make today?" })).toBeVisible();
  const starters = page.getByRole("list", { name: "Starters" });
  await starters.getByRole("button", { name: /Surprise me/ }).click();
  expect(await lastOf(page, "generate")).toEqual({ type: "generate", prompt: "", roles: null, count: 1, capture: null });

  // Parts stream in one by one, then the idea is done and lands in the thread.
  await expect(lane(page, "Chords")).toBeVisible();
  for (const name of ["Drums", "Bass", "Melody"]) await expect(lane(page, name)).toBeVisible();
  await expect(page.locator("#st-thread").getByRole("article", { name: "Surprise idea" })).toHaveAttribute("aria-current", "true");
  await expect(page.getByRole("img", { name: "AI service connected" })).toBeVisible();
});

test("first run: Start from a vibe focuses the prompt; vibe chips send at once", async ({ page }) => {
  await open(page);
  await page.getByRole("button", { name: /Start from a vibe/ }).click();
  await expect(page.getByRole("textbox", { name: "Describe or ask" })).toBeFocused();
  await page.getByRole("list", { name: "Suggestions" }).getByRole("button", { name: "neo-soul, lazy swing" }).click();
  expect(await lastOf(page, "generate")).toMatchObject({ prompt: "neo-soul, lazy swing", count: 1 });
});

test("first run: Start from the library opens it, and Settings opens the sheet", async ({ page }) => {
  await open(page);
  await page.getByRole("button", { name: /Start from the library/ }).click();
  await expect(page.getByRole("heading", { name: "Library" })).toBeVisible();
  await page.getByRole("navigation", { name: "Views" }).getByRole("button", { name: "Studio" }).click();
  await page.getByRole("button", { name: /Settings and connection/ }).click();
  await expect(page.getByRole("dialog", { name: "Settings" })).toBeVisible();
});

// ---- Flow 2: describe -> hear ------------------------------------------------------------------

test("describe -> hear: Return sends, Shift+Return adds a line, progress shows, parts play as they land", async ({ page }) => {
  await open(page);
  await page.evaluate(() => ((window as unknown as MockWindow).__flowstateMock.delay = 600));
  const input = page.getByRole("textbox", { name: "Describe or ask" });
  await input.fill("lofi");
  await input.press("Shift+Enter");
  await input.pressSequentially("rainy study");
  await expect(input).toHaveValue("lofi\nrainy study");
  await input.press("Enter");
  expect(await lastOf(page, "generate")).toEqual({ type: "generate", prompt: "lofi\nrainy study", roles: null, count: 1, capture: null });
  await expect(input).toHaveValue("");
  await expect(page.getByRole("status").filter({ hasText: /Planning|Writing parts/ }).first()).toBeVisible();
  await expect(page.getByRole("button", { name: "Working" })).toBeDisabled();
  await expect(lane(page, "Chords")).toBeVisible();
  await expect(page.getByRole("status").filter({ hasText: /Writing parts · \d in/ })).toBeVisible();
  await expect(lane(page, "Melody")).toBeVisible();
  await expect(page.getByRole("button", { name: "Send" })).toBeVisible();
});

test("describe -> hear: Cancel stops a running generation", async ({ page }) => {
  await open(page);
  await page.evaluate(() => ((window as unknown as MockWindow).__flowstateMock.delay = 2000));
  const input = page.getByRole("textbox", { name: "Describe or ask" });
  await input.fill("afrobeats");
  await input.press("Enter");
  await page.getByRole("button", { name: "Cancel" }).click();
  expect(await lastOf(page, "cancel")).toMatchObject({ type: "cancel", requestId: expect.any(String) });
  await expect(toast(page, "Generation cancelled")).toBeVisible();
  // The instant sketch stays, so there is still an idea to play; it says what it is.
  await expect(page.getByRole("status").filter({ hasText: /Sketch: the generation didn't finish/ })).toBeVisible();
  await expect(page.locator("#st-thread").getByRole("article", { name: "Sketch" })).toHaveAttribute("aria-current", "true");
});

test("describe -> hear: the instant sketch plays at once, and the AI's parts replace it", async ({ page }) => {
  await open(page);
  await page.evaluate(() => ((window as unknown as MockWindow).__flowstateMock.delay = 2000));
  const input = page.getByRole("textbox", { name: "Describe or ask" });
  await input.fill("dusty soul");
  await input.press("Enter");
  // Before any part has streamed: four sketch lanes, labelled as the sketch.
  for (const name of ["Chords", "Drums", "Bass", "Melody"]) await expect(lane(page, name)).toBeVisible();
  await expect(page.getByRole("status").filter({ hasText: "Sketch: playing now" })).toBeVisible();
  // When it's done, the AI's idea is current and the sketch is gone from the thread.
  const card = page.locator("#st-thread").getByRole("article", { name: "Dusty soul" });
  await expect(card).toHaveAttribute("aria-current", "true", { timeout: 20_000 });
  await expect(page.getByText(/^Sketch:/)).toHaveCount(0);
  await expect(page.locator("#st-thread").getByRole("article", { name: "Sketch" })).toHaveCount(0);
});

test("describe -> hear: variations go out as one request; the playhead follows the host", async ({ page }) => {
  await open(page, "mock=idea");
  await page.getByRole("button", { name: "Variations: 1 variation" }).click();
  await page.getByRole("button", { name: "Variations: 2 variations" }).click();
  const input = page.getByRole("textbox", { name: "Describe or ask" });
  await input.fill("gospel 6/8");
  await input.press("Enter");
  expect(await lastOf(page, "generate")).toMatchObject({ prompt: "gospel 6/8", count: 3 });
  await expect(page.locator("#st-thread").getByRole("article", { name: "Gospel 6/8" })).toHaveCount(3);

  await page.evaluate(() => (window as unknown as MockWindow).__flowstateMock.setHost({ playing: true, positionPpq: 6 }));
  await expect(page.getByText(/Bar \d+ · beat \d+ · 92 bpm/)).toBeVisible();
  await expect(page.getByRole("img", { name: /Loop position \d+%/ })).toBeVisible();
});

test("a failed generation says why and marks the service", async ({ page }) => {
  await open(page);
  await page.evaluate(() => (window as unknown as MockWindow).__flowstateMock.emit({ type: "generationFailed", requestId: "r9", error: { code: "network", message: "connection refused" } }));
  await expect(page.getByRole("alert").filter({ hasText: "connection refused" })).toBeVisible();
  await expect(page.getByRole("img", { name: /Can't reach the AI service/ })).toBeVisible();
  await expect(page.getByRole("status").filter({ hasText: /^Offline/ })).toBeVisible();
  await page.evaluate(() => (window as unknown as MockWindow).__flowstateMock.emit({ type: "notice", level: "warning", message: "One variation failed" }));
  await expect(toast(page, "One variation failed")).toBeVisible();
});

// ---- Flow 3: iterate --------------------------------------------------------------------------

test("iterate: each result is a card; restore, undo and redo walk the lineage", async ({ page }) => {
  await open(page, "mock=idea");
  const input = page.getByRole("textbox", { name: "Describe or ask" });
  await input.fill("darker take");
  await input.press("Enter");
  const thread = page.locator("#st-thread");
  const first = thread.getByRole("article", { name: "Dusty soul loop" });
  const second = thread.getByRole("article", { name: "Darker take" });
  await expect(second).toHaveAttribute("aria-current", "true");

  await first.getByRole("button", { name: "Restore Dusty soul loop" }).click();
  await expect(first).toHaveAttribute("aria-current", "true");
  await page.getByRole("button", { name: "Undo" }).click();
  await expect(second).toHaveAttribute("aria-current", "true");
  await page.getByRole("button", { name: "Redo" }).click();
  await expect(first).toHaveAttribute("aria-current", "true");

  // The shortcuts work while the plugin has focus, but not while typing.
  await page.getByRole("button", { name: "Redo" }).focus();
  await page.keyboard.press("Control+z");
  await expect(second).toHaveAttribute("aria-current", "true");
  await input.focus();
  await page.keyboard.press("Control+z");
  expect(await sentOf(page, "undo")).toHaveLength(2);
});

test("iterate: play, branch and drag from a card", async ({ page }) => {
  await open(page, "mock=idea");
  const card = page.locator("#st-thread").getByRole("article", { name: "Dusty soul loop" });
  await card.getByRole("button", { name: "Play Dusty soul loop" }).click();
  expect(await lastOf(page, "setAudition")).toEqual({ type: "setAudition", audition: { nodeId: "n1", loop: null, freeRun: false } });
  await expect(card.getByRole("button", { name: "Play Dusty soul loop" })).toHaveAttribute("aria-pressed", "true");
  await card.getByRole("button", { name: "Play Dusty soul loop" }).click();
  expect(await lastOf(page, "setAudition")).toMatchObject({ audition: { nodeId: null } });

  await card.getByRole("button", { name: "Branch from Dusty soul loop" }).click();
  expect(await lastOf(page, "selectNode")).toEqual({ type: "selectNode", nodeId: "n1" });
  await expect(page.getByRole("textbox", { name: "Describe or ask" })).toBeFocused();

  await card.getByRole("button", { name: "Drag Dusty soul loop into your DAW" }).dispatchEvent("pointerdown");
  expect(await lastOf(page, "startDrag")).toEqual({ type: "startDrag", nodeId: "n1", partIds: null, splitDrums: false });
});

test("iterate: mute, solo and remove a part", async ({ page }) => {
  await open(page, "mock=idea");
  const bass = lane(page, "Bass");
  await bass.getByRole("button", { name: "Mute Bass" }).click();
  await expect(bass.getByRole("button", { name: "Mute Bass" })).toHaveAttribute("aria-pressed", "true");
  expect(await lastOf(page, "setPartState")).toEqual({ type: "setPartState", state: { partId: "p-bass", muted: true, solo: false, locked: false, density: 0.5 } });
  await bass.getByRole("button", { name: "Solo Bass" }).click();
  expect(await lastOf(page, "setPartState")).toMatchObject({ state: { partId: "p-bass", muted: true, solo: true } });
  await lane(page, "Melody").getByRole("button", { name: "Remove Melody" }).click();
  expect(await lastOf(page, "removePart")).toEqual({ type: "removePart", partId: "p-melody" });
  await expect(lane(page, "Melody")).toHaveCount(0);
  await page.getByRole("button", { name: "Undo" }).click();
  await expect(lane(page, "Melody")).toBeVisible();
});

test("iterate: keep the chords, new everything else: one lock click, then generate", async ({ page }) => {
  await open(page, "mock=idea");
  const chords = lane(page, "Chords");
  const before = await chords.locator(".fs-lane__role").textContent();
  await chords.getByRole("button", { name: "Lock Chords" }).click();
  await expect(chords.getByRole("button", { name: "Lock Chords" })).toHaveAttribute("aria-pressed", "true");
  expect(await lastOf(page, "setPartState")).toMatchObject({ state: { partId: "p-chords", locked: true } });
  const input = page.getByRole("textbox", { name: "Describe or ask" });
  await input.fill("new groove, same chords");
  await input.press("Enter");
  const card = page.locator("#st-thread").getByRole("article", { name: "New groove, same chords" });
  await expect(card).toHaveAttribute("aria-current", "true");
  await expect(lane(page, "Chords").locator(".fs-lane__role")).toHaveText(before!);
  await expect(lane(page, "Chords").getByRole("button", { name: "Lock Chords" })).toHaveAttribute("aria-pressed", "true");
  for (const name of ["Drums", "Bass", "Melody"]) await expect(lane(page, name)).toBeVisible();
});

test("the thread drawer opens and closes", async ({ page }) => {
  await open(page, "mock=idea");
  const thread = page.getByRole("complementary", { name: "Thread" });
  await expect(thread).toBeVisible();
  await thread.getByRole("button", { name: "Close thread" }).click();
  await expect(thread).toBeHidden();
  const toggle = page.getByRole("toolbar", { name: "Idea" }).getByRole("button", { name: "Thread" });
  await expect(toggle).toHaveAttribute("aria-pressed", "false");
  await toggle.click();
  await expect(thread).toBeVisible();
});

test.describe("at 720 × 480, the thread is an overlay", () => {
  test.use({ viewport: { width: 720, height: 480 } });
  test("closed by default, it opens over the lanes", async ({ page }) => {
    await open(page, "mock=idea");
    const thread = page.getByRole("complementary", { name: "Thread" });
    await expect(thread).toBeHidden();
    await page.getByRole("button", { name: "Thread" }).click();
    await expect(thread).toBeInViewport();
    await expect(thread.getByRole("article", { name: "Dusty soul loop" })).toBeVisible();
  });
});

test("talk: change this idea, vary a part and add one; each lands as a card (P1-19)", async ({ page }) => {
  await open(page, "mock=idea");
  const thread = page.locator("#st-thread");
  await page.getByRole("button", { name: "Prompt makes: New idea" }).click();
  await expect(page.getByRole("button", { name: "Prompt makes: Change this idea" })).toBeVisible();
  const input = page.getByRole("textbox", { name: "Describe or ask" });
  await input.fill("busier drums in bar 4");
  await input.press("Enter");
  expect(await lastOf(page, "edit")).toEqual({ type: "edit", prompt: "busier drums in bar 4", partIds: null });
  await expect(thread.getByRole("article", { name: "Busier drums in bar 4" })).toHaveAttribute("aria-current", "true");

  await lane(page, "Bass").getByRole("button", { name: "Vary Bass" }).click();
  expect(await lastOf(page, "vary")).toEqual({ type: "vary", partId: "p-bass" });
  await expect(page.getByRole("button", { name: "Send" })).toBeVisible();

  await page.getByRole("button", { name: "Add part" }).click();
  await page.getByRole("group", { name: "Part to add" }).getByRole("button", { name: "Arp" }).click();
  expect(await lastOf(page, "addPart")).toEqual({ type: "addPart", role: "arp", prompt: null });
});

// ---- What this build can't do: looks disabled and says why (Session.unavailable) ----------------

test("unavailable commands look disabled, say why, and send nothing", async ({ page }) => {
  await open(page, "mock=idea");
  const chords = lane(page, "Chords");
  const checks = [
    { el: chords.getByRole("button", { name: "Edit Chords notes" }), why: /Note edits aren't in core yet/ },
  ];
  const before = (await sent(page)).length;
  for (const { el, why } of checks) {
    await expect(el).toHaveAttribute("aria-disabled", "true");
    await expect(el).toHaveAccessibleDescription(why);
    // Still reachable from the keyboard: Enter explains instead of running.
    await el.focus();
    await page.keyboard.press("Enter");
    await expect(toast(page, why).first()).toBeVisible();
  }
  expect((await sent(page)).slice(before)).toEqual([]);
});

test("re-roll is live in this build: a new card per take (P1-22)", async ({ page }) => {
  await open(page, "mock=idea");
  const melody = lane(page, "Melody");
  const reroll = melody.getByRole("button", { name: "Re-roll Melody" });
  await expect(reroll).not.toHaveAttribute("aria-disabled", "true");
  await reroll.click();
  expect(await lastOf(page, "reroll")).toEqual({ type: "reroll", partId: "p-melody" });
  await expect(page.getByRole("button", { name: "Undo" })).toBeEnabled();
  // A locked part can't be re-rolled: the button is disabled with the lock.
  await melody.getByRole("button", { name: "Lock Melody" }).click();
  await expect(reroll).toBeDisabled();
});

test("tweak and the density knob are live in this build (P1-21)", async ({ page }) => {
  await open(page, "mock=idea");
  const bass = lane(page, "Bass");
  // The knob is a live re-render: it sends the part state and makes no card.
  const density = bass.getByRole("slider", { name: "Bass density" });
  await expect(density).not.toHaveAttribute("aria-disabled", "true");
  const cards = (await sent(page)).length;
  await density.focus();
  await page.keyboard.press("ArrowDown");
  expect(await lastOf(page, "setPartState")).toMatchObject({ state: { partId: "p-bass", density: 0.49 } });
  await expect(density).toHaveAttribute("aria-valuenow", "0.49");
  expect((await sent(page)).slice(cards).map((c) => c.type)).toEqual(["setPartState"]);

  // Each tweak is a new card; a locked part is left out, and named on its own it is refused with the reason.
  await bass.getByRole("button", { name: "Lock Bass" }).click();
  await page.getByRole("button", { name: "Tweak" }).click();
  const sheet = page.getByRole("dialog", { name: "Tweak" });
  await sheet.getByRole("button", { name: "Semitone up" }).click();
  expect(await lastOf(page, "tweak")).toEqual({ type: "tweak", partId: null, op: "transpose", amount: 1 });
  await sheet.getByRole("combobox", { name: "Apply to" }).selectOption("p-bass");
  await sheet.getByRole("button", { name: "Revoice" }).click();
  expect(await lastOf(page, "tweak")).toEqual({ type: "tweak", partId: "p-bass", op: "revoice", amount: null });
  await expect(toast(page, /Bass is locked/).first()).toBeVisible();
  await sheet.getByRole("button", { name: "Done" }).click();
  await page.getByRole("button", { name: "Undo" }).click();
  expect(await lastOf(page, "undo")).toEqual({ type: "undo" });
});

test("when the plugin can do it all: talk, tweak and touch send their commands", async ({ page }) => {
  await open(page, "mock=idea&gaps=none");
  const chords = lane(page, "Chords");

  // Talk: change this idea by prompt, and vary one part.
  await page.getByRole("button", { name: "Prompt makes: New idea" }).click();
  const input = page.getByRole("textbox", { name: "Describe or ask" });
  await input.fill("busier drums in bar 4");
  await input.press("Enter");
  expect(await lastOf(page, "edit")).toEqual({ type: "edit", prompt: "busier drums in bar 4", partIds: null });
  await expect(page.getByRole("button", { name: "Send" })).toBeVisible();
  await page.getByRole("button", { name: "darker" }).click();
  expect(await lastOf(page, "edit")).toMatchObject({ prompt: "darker" });
  await expect(page.getByRole("button", { name: "Send" })).toBeVisible();
  await chords.getByRole("button", { name: "Vary Chords" }).click();
  expect(await lastOf(page, "vary")).toEqual({ type: "vary", partId: "p-chords" });
  await expect(page.getByRole("button", { name: "Send" })).toBeVisible();

  // "Keep the chords, new melody" is one lock click plus re-roll.
  await chords.getByRole("button", { name: "Lock Chords" }).click();
  expect(await lastOf(page, "setPartState")).toMatchObject({ state: { partId: "p-chords", locked: true } });
  await expect(chords.getByRole("button", { name: "Vary Chords" })).toBeDisabled();
  await lane(page, "Melody").getByRole("button", { name: "Re-roll Melody" }).click();
  expect(await lastOf(page, "reroll")).toEqual({ type: "reroll", partId: "p-melody" });

  // Tweak: density is a live re-render, transforms are new nodes.
  const density = lane(page, "Bass").getByRole("slider", { name: "Bass density" });
  await density.focus();
  await page.keyboard.press("ArrowUp");
  expect(await lastOf(page, "setPartState")).toMatchObject({ state: { partId: "p-bass", density: 0.51 } });
  await page.getByRole("button", { name: "Tweak" }).click();
  const sheet = page.getByRole("dialog", { name: "Tweak" });
  await sheet.getByRole("combobox", { name: "Apply to" }).selectOption("p-bass");
  await sheet.getByRole("button", { name: "Octave down" }).click();
  expect(await lastOf(page, "tweak")).toEqual({ type: "tweak", partId: "p-bass", op: "register", amount: -1 });
  await sheet.getByRole("combobox", { name: "Apply to" }).selectOption("");
  await sheet.getByRole("button", { name: "Simplify" }).click();
  expect(await lastOf(page, "tweak")).toEqual({ type: "tweak", partId: null, op: "simplify", amount: null });
  await sheet.getByRole("button", { name: "Done" }).click();

  // Add part goes to the model.
  await page.getByRole("button", { name: "Add part" }).click();
  await page.getByRole("group", { name: "Part to add" }).getByRole("button", { name: "Pad" }).click();
  expect(await lastOf(page, "addPart")).toEqual({ type: "addPart", role: "pad", prompt: null });

  // Touch: the note editor isn't built in the Studio yet, so it says so.
  await expect(chords.getByRole("button", { name: "Edit Chords notes" })).toHaveAccessibleDescription(/note editor isn't built yet/);
});

// ---- Flow 4: capture -> continue --------------------------------------------------------------

test("capture: choose bars and what to do, then generate from what was played", async ({ page }) => {
  await open(page, "capture=8");
  await page.getByRole("list", { name: "Starters" }).getByRole("button", { name: /Use what I just played/ }).click();
  const sheet = page.getByRole("dialog", { name: "Use what I just played" });
  await expect(sheet.getByText("8 bars of what you played")).toBeVisible();
  await sheet.getByRole("radio", { name: "Harmonize" }).click();
  await sheet.getByRole("combobox", { name: "Bars" }).selectOption("8");
  await sheet.getByRole("button", { name: "Generate" }).click();
  expect(await lastOf(page, "generate")).toEqual({ type: "generate", prompt: "", roles: null, count: 1, capture: { bars: 8, intent: "harmonize" } });
});

test("capture: the plugin's reason shows when what was played can't be used that way", async ({ page }) => {
  await open(page, "capture=4");
  await page.evaluate(() => (window as unknown as MockWindow).__flowstateMock.fail("generate", "bad_request", "What you played is already a bass line. Choose another way to use it."));
  await page.getByRole("list", { name: "Starters" }).getByRole("button", { name: /Use what I just played/ }).click();
  const sheet = page.getByRole("dialog", { name: "Use what I just played" });
  await sheet.getByRole("radio", { name: "Add bass" }).click();
  await sheet.getByRole("button", { name: "Generate" }).click();
  expect(await lastOf(page, "generate")).toMatchObject({ capture: { bars: 4, intent: "add_bass" } });
  await expect(toast(page, /already a bass line/)).toBeVisible();
});

test("capture: with nothing played yet, it says so", async ({ page }) => {
  await open(page, "capture=0");
  const starter = page.getByRole("list", { name: "Starters" }).getByRole("button", { name: /Use what I just played/ });
  await expect(starter).toHaveAttribute("aria-disabled", "true");
  await starter.focus();
  await page.keyboard.press("Enter");
  await expect(toast(page, /nothing has been captured yet/)).toBeVisible();
  await expect(page.getByRole("dialog", { name: "Use what I just played" })).toBeHidden();
});

// ---- Flow 5: commit ---------------------------------------------------------------------------

test("commit: drag one part, all parts, drums split by sublane, or save a file", async ({ page }) => {
  await open(page, "mock=idea");
  await lane(page, "Bass").getByRole("button", { name: "Drag Bass into your DAW" }).dispatchEvent("pointerdown");
  expect(await lastOf(page, "startDrag")).toEqual({ type: "startDrag", nodeId: null, partIds: ["p-bass"], splitDrums: false });

  const bar = page.getByRole("toolbar", { name: "Idea" });
  await bar.getByRole("button", { name: "Drag all parts into your DAW" }).dispatchEvent("pointerdown");
  expect(await lastOf(page, "startDrag")).toEqual({ type: "startDrag", nodeId: null, partIds: null, splitDrums: false });

  await bar.getByRole("button", { name: "Split drums by sublane when dragging" }).click();
  await lane(page, "Drums").getByRole("button", { name: "Drag Drums into your DAW" }).dispatchEvent("pointerdown");
  expect(await lastOf(page, "startDrag")).toEqual({ type: "startDrag", nodeId: null, partIds: ["p-drums"], splitDrums: true });

  // The keyboard can't start an OS drag; Enter on a handle saves the same content instead.
  await lane(page, "Bass").getByRole("button", { name: "Drag Bass into your DAW" }).focus();
  await page.keyboard.press("Enter");
  expect(await lastOf(page, "exportMidi")).toEqual({ type: "exportMidi", nodeId: null, partIds: ["p-bass"], splitDrums: false });
  await bar.getByRole("button", { name: "Save as MIDI file" }).click();
  expect(await lastOf(page, "exportMidi")).toEqual({ type: "exportMidi", nodeId: null, partIds: null, splitDrums: true });
});

test("commit: play while the host is stopped, and the drums lane shows its sublanes", async ({ page }) => {
  await open(page, "mock=idea");
  const play = page.getByRole("button", { name: "Play while the host is stopped" });
  await play.click();
  expect(await lastOf(page, "setAudition")).toEqual({ type: "setAudition", audition: { nodeId: null, loop: null, freeRun: true } });
  await expect(play).toHaveAttribute("aria-pressed", "true");
  const drums = lane(page, "Drums");
  await drums.getByRole("button", { name: "Drums sublanes" }).click();
  for (const name of ["Kick notes", "Snare notes", "Hats notes"]) await expect(drums.getByRole("img", { name: new RegExp(name) })).toBeVisible();
});

test("commit: MIDI out per instance, from the track pill", async ({ page }) => {
  await open(page, "mock=idea");
  await page.getByRole("button", { name: "MIDI out: All parts" }).click();
  const sheet = page.getByRole("dialog", { name: "Settings" });
  await sheet.getByRole("combobox", { name: "This instance sends" }).selectOption("bass");
  expect(await lastOf(page, "setMidiOut")).toEqual({ type: "setMidiOut", midiOut: { role: "bass", channel: null } });
  await sheet.getByRole("combobox", { name: "MIDI channel" }).selectOption("2");
  expect(await lastOf(page, "setMidiOut")).toEqual({ type: "setMidiOut", midiOut: { role: "bass", channel: 2 } });
  await sheet.getByRole("button", { name: "Done" }).click();
  await expect(page.getByRole("button", { name: "MIDI out: Bass only · ch 2" })).toBeVisible();
});

// ---- Context strip ----------------------------------------------------------------------------

test("context: host values show as locked; an override applies and can follow the host again", async ({ page }) => {
  await open(page, "mock=idea");
  const strip = page.getByRole("group", { name: "Context" });
  await expect(strip.getByRole("button", { name: "Tempo: 92 bpm, from host" })).toBeVisible();
  await expect(strip.getByRole("button", { name: "Key: A minor, from the idea" })).toBeVisible();

  await strip.getByRole("button", { name: /^Tempo/ }).click();
  const sheet = page.getByRole("dialog", { name: "Context" });
  const tempo = sheet.getByRole("textbox", { name: "Tempo (bpm)" });
  await expect(tempo).toBeFocused();
  await tempo.fill("500");
  await expect(tempo).toHaveAttribute("aria-invalid", "true");
  await expect(sheet.getByRole("button", { name: "Apply" })).toBeDisabled();
  await tempo.fill("128");
  await sheet.getByRole("combobox", { name: "Mode" }).selectOption("dorian");
  await sheet.getByRole("button", { name: "Apply" }).click();
  expect(await lastOf(page, "setContextOverride")).toEqual({
    type: "setContextOverride",
    override: { tonic: null, mode: "dorian", tempo: 128, meterNumerator: null, meterDenominator: null, bars: null, swing: null },
  });
  await expect(strip.getByRole("button", { name: "Tempo: 128 bpm, overridden" })).toBeVisible();
  await expect(strip.getByRole("button", { name: "Key: A dorian, overridden" })).toBeVisible();

  await strip.getByRole("button", { name: /^Tempo/ }).click();
  await sheet.getByRole("textbox", { name: "Tempo (bpm)" }).fill("");
  await sheet.getByRole("button", { name: "Apply" }).click();
  expect(await lastOf(page, "setContextOverride")).toMatchObject({ override: { tempo: null, mode: "dorian" } });
  await expect(strip.getByRole("button", { name: "Tempo: 92 bpm, from host" })).toBeVisible();
});

test("context: Escape closes the sheet without applying, and focus returns to the chip", async ({ page }) => {
  await open(page, "mock=idea");
  const chip = page.getByRole("group", { name: "Context" }).getByRole("button", { name: /^Bars/ });
  await chip.click();
  const sheet = page.getByRole("dialog", { name: "Context" });
  await expect(sheet.getByRole("combobox", { name: "Bars" })).toBeFocused();
  await page.keyboard.press("Escape");
  await expect(sheet).toBeHidden();
  await expect(chip).toBeFocused();
  expect(await sentOf(page, "setContextOverride")).toEqual([]);
});

// ---- Settings ---------------------------------------------------------------------------------

test("settings: provider and model, BYOK key, preview synth, build ID", async ({ page }) => {
  await open(page);
  await page.getByRole("button", { name: "Settings", exact: true }).click();
  const sheet = page.getByRole("dialog", { name: "Settings" });
  await sheet.getByRole("combobox", { name: "Provider" }).selectOption("openrouter");
  await sheet.getByRole("textbox", { name: "Model" }).fill("openai/gpt-5.5");
  await sheet.getByRole("button", { name: "Use this model" }).click();
  expect(await lastOf(page, "setProvider")).toEqual({ type: "setProvider", provider: { provider: "openrouter", model: "openai/gpt-5.5" } });
  await expect(sheet.getByText("Now: openrouter • openai/gpt-5.5")).toBeVisible();

  const save = sheet.getByRole("button", { name: "Save key" });
  await expect(save).toBeDisabled();
  const key = sheet.getByLabel("Your API key", { exact: true });
  await key.fill("sk-or-secret");
  await save.focus();
  await page.keyboard.press("Enter");
  expect(await lastOf(page, "setApiKey")).toEqual({ type: "setApiKey", provider: "openrouter", key: "sk-or-secret" });
  await expect(key).toHaveValue("");
  await expect(key).toHaveAttribute("placeholder", /A key is stored/);
  await expect(sheet.getByRole("button", { name: "Remove key" })).toBeVisible();

  await sheet.getByRole("switch", { name: /Preview synth/ }).click();
  expect(await lastOf(page, "setPreviewSynth")).toEqual({ type: "setPreviewSynth", enabled: false });
  await expect(sheet.getByText("Build mock-dev")).toBeVisible();

  await sheet.getByRole("combobox", { name: "Provider" }).selectOption("");
  await sheet.getByRole("button", { name: "Use this model" }).click();
  expect(await lastOf(page, "setProvider")).toEqual({ type: "setProvider", provider: null });
  await sheet.getByRole("button", { name: "Done" }).click();
  await expect(page.getByRole("button", { name: "Model: Managed default" })).toBeVisible();
});

test("settings: the key is sent once and never shown again; it is removed for its own provider", async ({ page }) => {
  await open(page);
  await page.getByRole("button", { name: "Account and usage" }).click();
  const sheet = page.getByRole("dialog", { name: "Settings" });
  await sheet.getByRole("combobox", { name: "Provider" }).selectOption("openai");
  await sheet.getByRole("textbox", { name: "Model" }).fill("gpt-5.5");
  await sheet.getByRole("button", { name: "Use this model" }).click();
  const key = sheet.getByLabel("Your API key", { exact: true });
  await key.fill("sk-test-123");
  await sheet.getByRole("button", { name: "Save key" }).click();
  expect(await lastOf(page, "setApiKey")).toEqual({ type: "setApiKey", provider: "openai", key: "sk-test-123" });
  await expect(key).toHaveValue("");

  // Another provider in the picker isn't the one with the key: nothing to remove there.
  await sheet.getByRole("combobox", { name: "Provider" }).selectOption("google");
  await expect(sheet.getByRole("button", { name: "Remove key" })).toHaveCount(0);
  await sheet.getByRole("combobox", { name: "Provider" }).selectOption("openai");
  await sheet.getByRole("button", { name: "Remove key" }).click();
  expect(await lastOf(page, "setApiKey")).toEqual({ type: "setApiKey", provider: "openai", key: null });
  await expect(sheet.getByRole("button", { name: "Remove key" })).toHaveCount(0);
});

test("settings: a build without a keychain says why the key can't be saved", async ({ page }) => {
  await open(page);
  await page.evaluate(() => (window as unknown as MockWindow).__flowstateMock.setGaps([{ feature: "apiKey", reason: "This build has no OS keychain to keep your key in." }]));
  await page.getByRole("button", { name: "Settings", exact: true }).click();
  const sheet = page.getByRole("dialog", { name: "Settings" });
  await sheet.getByRole("combobox", { name: "Provider" }).selectOption("openai");
  const save = sheet.getByRole("button", { name: "Save key" });
  await expect(save).toHaveAttribute("aria-disabled", "true");
  await expect(save).toHaveAccessibleDescription(/no OS keychain/);
  await sheet.getByLabel("Your API key", { exact: true }).fill("sk-secret");
  await save.focus();
  await page.keyboard.press("Enter");
  await expect(toast(page, /no OS keychain/)).toBeVisible();
  expect(await sentOf(page, "setApiKey")).toEqual([]);
});

test("settings: the BYOK release flag hides the key field", async ({ page }) => {
  await open(page, "byok=0");
  await page.getByRole("button", { name: "Settings", exact: true }).click();
  await expect(page.getByRole("dialog", { name: "Settings" }).getByLabel("Your API key")).toHaveCount(0);
});

// ---- Library ----------------------------------------------------------------------------------

test("library: search, preview in time, use (with its credit) and drag", async ({ page }) => {
  await open(page, "mock=idea");
  await page.getByRole("navigation", { name: "Views" }).getByRole("button", { name: "Library" }).click();
  await expect(page.getByRole("status").filter({ hasText: /\d+ clips/ })).toBeVisible();
  expect(await lastOf(page, "searchCatalog")).toMatchObject({ query: { text: "", origins: null, roles: null, fitContext: false, limit: 24, offset: 0 } });
  await expect(page.getByRole("article", { name: "Dusty soul loop" })).toBeVisible();

  await page.getByRole("radiogroup", { name: "Source" }).getByRole("radio", { name: "Library" }).click();
  await page.getByRole("searchbox", { name: "Search" }).fill("velvet");
  await expect(page.getByRole("status").filter({ hasText: "1 clip" })).toBeVisible();
  expect(await lastOf(page, "searchCatalog")).toMatchObject({ query: { text: "velvet", origins: ["library"] } });
  const card = page.getByRole("article", { name: "Velvet sevenths" });
  await expect(card.getByText("MIDI by GodFlow (flowknows) for Flowstate.")).toBeVisible();

  await card.getByRole("button", { name: "Preview Velvet sevenths" }).click();
  expect(await lastOf(page, "previewEntry")).toEqual({ type: "previewEntry", entryId: "lib:godflow/rnb-07" });
  await expect(card.getByRole("button", { name: "Preview Velvet sevenths" })).toHaveAttribute("aria-pressed", "true");
  await card.getByRole("button", { name: "Drag Velvet sevenths into your DAW" }).dispatchEvent("pointerdown");
  expect(await lastOf(page, "dragEntry")).toEqual({ type: "dragEntry", entryId: "lib:godflow/rnb-07" });

  await page.getByRole("switch", { name: "Fit to this session" }).click();
  await expect.poll(async () => (await lastOf(page, "searchCatalog"))?.query).toMatchObject({ fitContext: true });

  await card.getByRole("button", { name: "Use Velvet sevenths" }).click();
  expect(await lastOf(page, "useEntry")).toEqual({ type: "useEntry", entryId: "lib:godflow/rnb-07" });
  expect(await lastOf(page, "previewEntry")).toEqual({ type: "previewEntry", entryId: null });
  // Back in the Studio, the clip is the current idea and its credit shows.
  await expect(lane(page, "Chords")).toBeVisible();
  await expect(page.locator(".st-stage").getByText("MIDI by GodFlow (flowknows) for Flowstate.")).toBeVisible();
  await expect(page.locator("#st-thread").getByRole("article", { name: "Velvet sevenths" }).getByText(/MIDI by GodFlow/)).toBeVisible();
});

// ---- The real bridge path ---------------------------------------------------------------------

test("in the plugin: the page loads JUCE's interop module, says hello, renders the session and follows events", async ({ page }) => {
  const replies = JSON.parse(readFileSync(new URL("../../schema/fixtures/bridge/Reply.json", import.meta.url), "utf8")).valid as { session: { context: { tonic: string } } | null }[];
  const reply = replies.find((r) => r.session !== null)!;
  const interop = `
    window.__sent = [];
    export function getNativeFunction(name) {
      return async (json) => { window.__sent.push(JSON.parse(json)); return JSON.stringify(${JSON.stringify(reply)}); };
    }`;
  await page.addInitScript(() => {
    const w = window as unknown as { __JUCE__: unknown; __listeners: ((s: string) => void)[] };
    w.__listeners = [];
    w.__JUCE__ = { backend: { addEventListener: (_: string, fn: (s: string) => void) => w.__listeners.push(fn) } };
  });
  // Only at the root, next to the page: that's where the plugin serves it.
  await page.route((url) => url.pathname === "/juce_interop.js", (r) => r.fulfill({ contentType: "text/javascript", body: interop }));
  await page.goto("/index.html");

  const strip = page.getByRole("group", { name: "Context" });
  await expect(strip.getByRole("button", { name: new RegExp(`^Key: ${reply.session!.context.tonic} `) })).toBeVisible();
  const sentNow = () => page.evaluate(() => (window as unknown as { __sent: Sent[] }).__sent);
  expect((await sentNow())[0]).toEqual({ type: "hello", protocol: "flowstate.bridge.v0" });
  expect(await page.evaluate(() => "__flowstateMock" in window)).toBe(false);

  // An event from the plugin updates the Studio.
  const changed = structuredClone(reply.session!) as unknown as { context: { tempo: number } };
  changed.context.tempo = 140;
  await page.evaluate((s) => (window as unknown as { __listeners: ((s: string) => void)[] }).__listeners.forEach((fn) => fn(JSON.stringify({ type: "session", session: s }))), changed);
  await expect(strip.getByRole("button", { name: /^Tempo: 140 bpm/ })).toBeVisible();

  // Space with no text field focused goes back to the host.
  await strip.getByRole("button", { name: /^Tempo/ }).focus();
  await page.keyboard.press("Space");
  expect((await sentNow()).at(-1)).toEqual({ type: "releaseFocus", reason: "space" });
});
