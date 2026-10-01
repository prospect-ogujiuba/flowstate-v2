import assert from "node:assert/strict";
import { describe, it } from "node:test";
import { readPromptTxt } from "./render-pack.ts";
import { GM, KIT, templateFor } from "./render-templates.ts";

describe("templateFor", () => {
  it("picks the family from the first style tag that names one", () => {
    assert.equal(templateFor(["trap"], "").kit, KIT.tr808);
    assert.equal(templateFor(["jazz", "swing"], "").kit, KIT.brush);
    assert.equal(templateFor(["unknown-style"], "").family, "acoustic");
  });

  it("applies an instrument word to the role named in the same clause", () => {
    const t = templateFor(["trap"], "dark trap beat with an eerie bell melody, long gliding 808 bass");
    assert.equal(t.programs.melody, GM.tubularBells);
    assert.equal(t.programs.chords, GM.piano);
    const h = templateFor(["cinematic"], "slow build with sweeping string-pad chords, a noble french-horn melody");
    assert.equal(h.programs.chords, GM.strings);
    assert.equal(h.programs.melody, GM.frenchHorn);
  });
});

describe("readPromptTxt", () => {
  it("reads the prompt and its fields", () => {
    const { prompt, fields } = readPromptTxt("lofi loop\n\nTempo: 78 BPM\nStyle: lofi, hip-hop\n");
    assert.equal(prompt, "lofi loop");
    assert.equal(fields.style, "lofi, hip-hop");
  });
});
