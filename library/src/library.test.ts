// Import, classification and path safety for library packs. Needs core's fs-analyze (npm run build:core).
import { test } from "node:test";
import assert from "node:assert/strict";
import { copyFileSync, mkdirSync, mkdtempSync, readFileSync, rmSync, symlinkSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { LibraryCatalog, Score, type LibraryPackManifest } from "@flowstate/schema";
import { buildCatalog, catalogRoot, catalogText, clipFileName, packsRoot } from "./catalog.ts";
import { leakReason, loadPack, unsafePathReason } from "./pack.ts";

const godflow = path.join(packsRoot, "godflow");
const manifest = JSON.parse(readFileSync(path.join(godflow, "manifest.json"), "utf8")) as LibraryPackManifest;
const build = buildCatalog();

test("every GodFlow entry imports, and the build is clean", () => {
  const r = build.results.find((x) => x.packId === "godflow")!;
  assert.deepEqual(r.problems, []);
  assert.deepEqual(r.entries.filter((e) => !e.ok), []);
  assert.equal(r.entries.length, manifest.entries.length);
  assert.ok(build.ok);
});

test("the committed catalog is what a fresh build writes", () => {
  assert.equal(readFileSync(path.join(catalogRoot, "catalog.json"), "utf8"), catalogText(build.catalog));
  for (const [name, data] of build.files) assert.ok(readFileSync(path.join(catalogRoot, name)).equals(data), `${name} is stale`);
});

test("the catalog is a valid LibraryCatalog, and every clip carries the pack's credit", () => {
  const catalog = LibraryCatalog.parse(JSON.parse(readFileSync(path.join(catalogRoot, "catalog.json"), "utf8")));
  assert.equal(catalog.clips.length, manifest.entries.length);
  assert.deepEqual(catalog.packs.map((p) => p.id), ["godflow"]);
  for (const clip of catalog.clips) {
    assert.equal(clip.entry.origin, "library");
    assert.equal(clip.entry.credit?.text, "MIDI by GodFlow (flowknows) for Flowstate.");
    assert.equal(clip.entry.credit?.producer, "GodFlow (flowknows)");
    assert.match(clip.entry.id, /^lib:godflow\/[a-z0-9-]+$/);
    assert.equal(clip.file, clipFileName("godflow", clip.entry.id.split("/")[1]!));
    Score.parse(clip.score);
  }
});

test("classification: lanes, genres, meter and length match the content", () => {
  const byId = new Map(build.catalog.clips.map((c) => [c.entry.id.split("/")[1]!, c]));
  for (const e of manifest.entries) {
    const clip = byId.get(e.id)!;
    assert.deepEqual(clip.entry.roles, [e.lane], e.id);
    assert.deepEqual(clip.score.parts.map((p) => p.role), [e.lane], e.id);
    assert.deepEqual(clip.entry.genres, e.genres);
    assert.equal(`${clip.entry.meterNumerator}/${clip.entry.meterDenominator}`, "4/4", e.id);
    assert.ok([4, 8, 16].includes(clip.entry.bars), `${e.id}: ${clip.entry.bars} bars`);
    // The IR plays the clip's rhythm back exactly.
    assert.equal(clip.fidelity.rhythm, 1, e.id);
  }
  // v1's pop "melodies" are chord stacks: they import as pop chords.
  for (const n of ["01", "02", "03", "04", "05"]) assert.deepEqual(byId.get(`pop-chords-${n}`)!.entry.roles, ["chords"]);
  // Spot checks against the voicings (rnb-jazz-chords-15 is Am F C G in C major).
  assert.deepEqual(byId.get("rnb-jazz-chords-15")!.harmony, ["Am", "F", "C", "G"]);
  assert.equal(byId.get("rnb-jazz-chords-15")!.entry.tonic, "C");
  assert.equal(byId.get("rnb-jazz-chords-15")!.entry.mode, "major");
});

test("a key is either detected with its evidence or blank with the reason", () => {
  for (const c of build.catalog.clips) {
    if (c.entry.tonic) assert.match(c.entry.keyNote!, /^(Detected|Set by the producer)/, c.entry.id);
    else assert.match(c.entry.keyNote!, /^Blank: (no clear tonal centre|ambiguous between|too few notes)/, c.entry.id);
    assert.equal(c.entry.tonic === null, c.entry.mode === null);
  }
});

test("descriptors are in range and nothing shown leaks a path", () => {
  for (const c of build.catalog.clips) {
    for (const v of [c.entry.energy, c.entry.density, c.entry.complexity]) assert.ok(v !== null && v >= 0 && v <= 1);
    assert.ok(c.entry.tempoMin <= c.entry.tempo + 8 && c.entry.tempoMax >= c.entry.tempoMin);
    for (const text of [c.entry.title, c.entry.feel ?? "", c.score.title]) assert.equal(leakReason(text), null);
  }
  assert.doesNotMatch(catalogText(build.catalog), /\/home\/|\\\\|[A-Z]:\\|\.\.\//);
});

// ---------- Path safety and "no fake entries", on throwaway packs ----------

function tempPack(edit: (m: LibraryPackManifest, dir: string) => void, files: Record<string, string> = {}): string {
  const root = mkdtempSync(path.join(tmpdir(), "flowstate-pack-"));
  const dir = path.join(root, "test-pack");
  mkdirSync(path.join(dir, "midi"), { recursive: true });
  copyFileSync(path.join(godflow, "midi/chords/rnb-jazz/rnb-jazz-chords-15.mid"), path.join(dir, "midi/a.mid"));
  writeFileSync(path.join(dir, "CREDITS.md"), "MIDI by Test for Flowstate.\n");
  for (const [rel, from] of Object.entries(files)) {
    mkdirSync(path.dirname(path.join(dir, rel)), { recursive: true });
    copyFileSync(from, path.join(dir, rel));
  }
  const m: LibraryPackManifest = {
    schema: "flowstate.libraryPack.v2", id: "test-pack", title: "Test", producer: "Test", credit: "MIDI by Test for Flowstate.",
    creditFile: "CREDITS.md",
    license: { id: "test", name: "Test", allowsPreview: true, allowsExport: true, allowsStyleExamples: false },
    laneNotes: [], omitted: [],
    entries: [{ id: "a", file: "midi/a.mid", lane: "chords", title: "A", genres: ["pop"], tags: [], feel: null,
      tempoMin: null, tempoMax: null, key: null, notes: null }],
  };
  edit(m, dir);
  writeFileSync(path.join(dir, "manifest.json"), JSON.stringify(m));
  return dir;
}

const problemsOf = (dir: string) => loadPack(dir).problems.map((p) => `${p.where}: ${p.message}`);

test("unsafe paths are named, never followed", () => {
  assert.equal(unsafePathReason("midi/a.mid"), null);
  assert.match(unsafePathReason("/etc/a.mid")!, /absolute/);
  assert.match(unsafePathReason("C:/a.mid")!, /absolute/);
  assert.match(unsafePathReason("midi\\a.mid")!, /backslash/);
  assert.match(unsafePathReason("midi/../../a.mid")!, /'\.\.'/);
  assert.match(unsafePathReason("midi//a.mid")!, /empty/);

  assert.deepEqual(problemsOf(tempPack(() => {})), []);
  assert.match(problemsOf(tempPack((m) => { m.entries[0]!.file = "../a.mid"; })).join(), /entries\[0\]\.file: \.\.\/a\.mid has an empty, '\.' or '\.\.' segment/);
  assert.match(problemsOf(tempPack((m) => { m.entries[0]!.file = "midi/missing.mid"; })).join(), /does not exist in the pack/);
  assert.match(problemsOf(tempPack((m) => { m.entries[0]!.file = "CREDITS.md"; })).join(), /is not a \.mid file/);
  const outside = path.join(mkdtempSync(path.join(tmpdir(), "flowstate-out-")), "x.mid");
  copyFileSync(path.join(godflow, "midi/bass/bass-02.mid"), outside);
  assert.match(problemsOf(tempPack((m, dir) => {
    symlinkSync(outside, path.join(dir, "midi/link.mid"));
    m.entries[0]!.file = "midi/link.mid";
    m.omitted.push({ file: "midi/a.mid", reason: "test" });
  })).join(), /symbolic link/);
});

test("manifest rules: ids, folder name, credit, silent files and leaks", () => {
  assert.match(problemsOf(tempPack((m) => { m.id = "other"; })).join(), /must equal the pack's folder name/);
  assert.match(problemsOf(tempPack((m) => { m.entries.push({ ...m.entries[0]! }); })).join(), /"a" is used twice/);
  assert.match(problemsOf(tempPack((m) => { m.credit = "Someone else."; })).join(), /creditFile: must quote the manifest's credit line/);
  assert.match(problemsOf(tempPack(() => {}, { "midi/extra.mid": path.join(godflow, "midi/bass/bass-02.mid") })).join(),
    /midi\/extra\.mid is in the pack but is neither an entry nor omitted/);
  assert.deepEqual(problemsOf(tempPack((m) => { m.omitted.push({ file: "midi/extra.mid", reason: "test" }); },
    { "midi/extra.mid": path.join(godflow, "midi/bass/bass-02.mid") })), []);
  assert.match(problemsOf(tempPack((m) => { m.entries[0]!.notes = "from SS_SD_bass_loop.mid"; })).join(), /names a MIDI file/);
  assert.match(problemsOf(tempPack((m) => { m.entries[0]!.lane = "pad" as never; })).join(), /entries\.0\.lane/);
  assert.match(problemsOf(tempPack((m) => { m.entries[0]!.genres = ["Hip Hop"]; })).join(), /lowercase kebab-case/);
});

test("an entry the content contradicts fails with the analyzer's precise reason", () => {
  const dir = tempPack((m) => {
    m.entries[0]!.lane = "melody";
    m.entries.push({ ...m.entries[0]!, id: "b", file: "midi/b.mid", lane: "bass" });
    m.entries.push({ ...m.entries[0]!, id: "c", file: "midi/c.mid", lane: "chords" });
  }, {
    "midi/b.mid": path.join(godflow, "midi/chords/pop/pop-chords-01.mid"),
    "midi/c.mid": path.join(godflow, "CREDITS.md"),
  });
  const result = buildCatalog(path.dirname(dir), [dir]);
  assert.equal(result.ok, false);
  const errors = Object.fromEntries(result.results[0]!.entries.map((e) => [e.id, e.error]));
  assert.match(errors.a!, /^lane_conflict: the manifest says melody, but the content is clearly chords/);
  assert.match(errors.b!, /^bass_range: notes from .* leave the bass range C1 \(24\) to C4 \(60\)/);
  assert.match(errors.c!, /^malformed: not a Standard MIDI File/);
  assert.equal(result.catalog.clips.length, 0);
  rmSync(path.dirname(dir), { recursive: true, force: true });
});
