import AxeBuilder from "@axe-core/playwright";
import { expect, test, type Page } from "@playwright/test";

const CONTROLS = [
  "button:not([disabled])",
  "a[href]",
  "input:not([disabled])",
  "select:not([disabled])",
  "textarea:not([disabled])",
  "[role=slider]:not([aria-disabled=true])",
].join(", ");

test.beforeEach(async ({ page }) => {
  await page.emulateMedia({ reducedMotion: "reduce" });
  await page.goto("/gallery.html");
});

/** Tags every visible control with an index, so the tab walk can report what it reached. */
async function tagControls(page: Page) {
  return page.evaluate((sel) => {
    const els = [...document.querySelectorAll<HTMLElement>(sel)].filter((el) => el.checkVisibility());
    els.forEach((el, i) => (el.dataset.ctl = String(i)));
    return els.map((el) => ({
      i: el.dataset.ctl!,
      name: el.getAttribute("aria-label") ?? el.textContent?.trim() ?? el.tagName,
      radio: el.getAttribute("role") === "radio",
      group: el.closest("[role=radiogroup]")?.getAttribute("aria-label") ?? null,
    }));
  }, CONTROLS);
}

test("no axe violations on the gallery", async ({ page }) => {
  const results = await new AxeBuilder({ page }).withTags(["wcag2a", "wcag2aa", "wcag21a", "wcag21aa"]).analyze();
  expect(results.violations.map((v) => `${v.id}: ${v.nodes.map((n) => n.target.join(" ")).join(", ")}`)).toEqual([]);
});

test("every control has an accessible name", async ({ page }) => {
  const controls = await tagControls(page);
  expect(controls.length).toBeGreaterThan(80);
  for (const c of controls) await expect(page.locator(`[data-ctl="${c.i}"]`)).toHaveAccessibleName(/\S/);
});

test("Tab reaches every control; radio groups are entered once and walked with arrows", async ({ page }) => {
  const controls = await tagControls(page);
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

test("knob: keyboard steps, jumps and resets", async ({ page }) => {
  const knob = page.getByRole("slider", { name: "Density", exact: true });
  await knob.focus();
  await expect(knob).toHaveAttribute("aria-valuenow", "0.5");
  await page.keyboard.press("ArrowUp");
  await expect(knob).toHaveAttribute("aria-valuenow", "0.51");
  await page.keyboard.press("PageDown");
  await expect(knob).toHaveAttribute("aria-valuenow", "0.41");
  await page.keyboard.press("End");
  await expect(knob).toHaveAttribute("aria-valuenow", "1");
  await expect(knob).toHaveAttribute("aria-valuetext", "100%");
  await page.keyboard.press("ArrowUp");
  await expect(knob).toHaveAttribute("aria-valuenow", "1");
  await knob.dblclick();
  await expect(knob).toHaveAttribute("aria-valuenow", "0.5");
});

test("knob: dragging up raises the value", async ({ page }) => {
  const knob = page.getByRole("slider", { name: "Swing" });
  await knob.scrollIntoViewIfNeeded();
  const box = (await knob.boundingBox())!;
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await page.mouse.down();
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2 - 75, { steps: 5 });
  await page.mouse.up();
  await expect(knob).toHaveAttribute("aria-valuenow", "0.5");
});

test("disabled knob ignores keys", async ({ page }) => {
  const knob = page.getByRole("slider", { name: "Locked" });
  await expect(knob).toHaveAttribute("tabindex", "-1");
  await knob.focus();
  await page.keyboard.press("ArrowUp");
  await expect(knob).toHaveAttribute("aria-valuenow", "0.5");
});

test("segmented: arrows move the selection and focus", async ({ page }) => {
  const group = page.getByRole("radiogroup", { name: "Mode" }).first();
  const chat = group.getByRole("radio", { name: "Chat" });
  const create = group.getByRole("radio", { name: "Create" });
  await chat.focus();
  await page.keyboard.press("ArrowRight");
  await expect(create).toHaveAttribute("aria-checked", "true");
  await expect(create).toBeFocused();
  await expect(chat).toHaveAttribute("tabindex", "-1");
});

test("toggle and toggle buttons report their state", async ({ page }) => {
  const midi = page.getByRole("switch", { name: "MIDI out" });
  await expect(midi).toHaveAttribute("aria-checked", "false");
  await midi.focus();
  await page.keyboard.press("Enter");
  await expect(midi).toHaveAttribute("aria-checked", "true");

  const solo = page.getByRole("button", { name: "Solo Chords" }).first();
  await expect(solo).toHaveAttribute("aria-pressed", "false");
  await solo.click();
  await expect(solo).toHaveAttribute("aria-pressed", "true");
});

test("Space goes to the host: it never activates a control, but types in a text field", async ({ page }) => {
  const midi = page.getByRole("switch", { name: "MIDI out" });
  await midi.focus();
  await page.keyboard.press("Space");
  await expect(midi).toHaveAttribute("aria-checked", "false");
  await expect(midi).not.toBeFocused();

  const name = page.getByRole("textbox", { name: "Name" }).first();
  await name.fill("Prod");
  await name.press("Space");
  await expect(name).toHaveValue("Prod ");
  await expect(name).toBeFocused();

  await name.press("Escape");
  await expect(name).not.toBeFocused();
});

test("secret field hides and shows the key", async ({ page }) => {
  const key = page.getByLabel("API key", { exact: true }).first();
  await expect(key).toHaveAttribute("type", "password");
  await page.getByRole("button", { name: "Show API key" }).first().click();
  await expect(key).toHaveAttribute("type", "text");
});

test("field errors are tied to their input", async ({ page }) => {
  const email = page.getByRole("textbox", { name: "Email" }).first();
  await expect(email).toHaveAttribute("aria-invalid", "true");
  await expect(email).toHaveAccessibleDescription(/\S/);
});

test("lane: lock disables vary, re-roll and density; drums expand to sublanes", async ({ page }) => {
  const lanes = page.locator("#lanes");
  const drums = lanes.getByRole("article", { name: "Drums lane" });
  await expect(drums.getByRole("button", { name: "Lock Drums" })).toHaveAttribute("aria-pressed", "true");
  await expect(drums.getByRole("button", { name: "Vary Drums" })).toBeDisabled();
  await expect(drums.getByRole("slider", { name: "Drums density" })).toHaveAttribute("aria-disabled", "true");

  await drums.getByRole("button", { name: "Lock Drums" }).click();
  await expect(drums.getByRole("button", { name: "Vary Drums" })).toBeEnabled();

  const expand = drums.getByRole("button", { name: "Drums sublanes" });
  await expect(drums.getByRole("button", { name: "Mute Kick" })).toBeHidden();
  await expand.click();
  await expect(expand).toHaveAttribute("aria-expanded", "true");
  const mute = drums.getByRole("button", { name: "Mute Kick" });
  await expect(mute).toBeVisible();
  await mute.click();
  await expect(mute).toHaveAttribute("aria-pressed", "true");
});

test("result card: restore makes a node current", async ({ page }) => {
  const card = page.getByRole("article", { name: "Dusty soul loop" });
  await expect(card).not.toHaveAttribute("aria-current", "true");
  await card.getByRole("button", { name: "Restore Dusty soul loop" }).click();
  await expect(card).toHaveAttribute("aria-current", "true");
  await expect(card.getByRole("button", { name: "Restore Dusty soul loop" })).toBeDisabled();
});

test("sheet: modal, Escape closes it and focus returns to the opener", async ({ page }) => {
  const opener = page.getByRole("button", { name: "Open settings sheet" });
  await opener.focus();
  await page.keyboard.press("Enter");
  const sheet = page.getByRole("dialog", { name: "Settings" });
  await expect(sheet).toBeVisible();

  // Tab stays inside the sheet.
  for (let i = 0; i < 25; i++) {
    await page.keyboard.press("Tab");
    expect(await page.evaluate(() => document.activeElement?.closest("dialog") != null || document.activeElement === document.body)).toBe(true);
  }

  // Escape in a field closes the sheet rather than releasing focus to the host.
  await sheet.getByRole("textbox", { name: "System prompt" }).focus();
  await page.keyboard.press("Escape");
  await expect(sheet).toBeHidden();
  await expect(opener).toBeFocused();
});

test("sheet: the close button and Save close it", async ({ page }) => {
  await page.getByRole("button", { name: "Open settings sheet" }).click();
  const sheet = page.getByRole("dialog", { name: "Settings" });
  await sheet.getByRole("button", { name: "Close" }).click();
  await expect(sheet).toBeHidden();
  await page.getByRole("button", { name: "Open settings sheet" }).click();
  await sheet.getByRole("button", { name: "Save" }).click();
  await expect(sheet).toBeHidden();
  await expect(page.getByRole("status").filter({ hasText: "Settings saved" })).toBeVisible();
});

test("toasts: status for info, alert for errors, dismissable", async ({ page }) => {
  await page.getByRole("button", { name: "Info toast" }).click();
  await page.getByRole("button", { name: "Error toast" }).click();
  const region = page.getByRole("region", { name: "Notifications" });
  await expect(region.getByRole("status")).toHaveCount(1);
  const alert = region.getByRole("alert");
  await expect(alert).toHaveCount(1);
  await alert.getByRole("button", { name: "Dismiss notification" }).click();
  await expect(region.getByRole("alert")).toHaveCount(0);
});

test("prompt box: Enter submits, empty prompts can't be sent", async ({ page }) => {
  const shell = page.getByTestId("shell");
  const send = shell.getByRole("button", { name: "Send" });
  await expect(send).toBeDisabled();
  const input = shell.getByRole("textbox", { name: "Describe or ask" });
  await input.fill("dusty soul loop");
  await expect(send).toBeEnabled();
  await input.press("Enter");
  await expect(input).toHaveValue("");
  await expect(page.getByRole("status").filter({ hasText: "dusty soul loop" })).toBeVisible();
});

test("works at the plugin's minimum size (720 × 480) without page scroll", async ({ page }) => {
  await page.setViewportSize({ width: 720, height: 480 });
  const overflow = await page.evaluate(() => ({
    x: document.documentElement.scrollWidth > window.innerWidth,
    y: document.documentElement.scrollHeight > window.innerHeight,
  }));
  expect(overflow).toEqual({ x: false, y: false });
});
