// Imports packs into the LibraryCatalog: core's analyzer (fs-analyze) classifies each clip, writes it as
// score IR and normalizes its MIDI; this file adds the manifest's curated metadata and the credit.
// Theory stays in core: nothing here reads notes.
import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync, mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import {
  LIBRARY_CATALOG_ID,
  LibraryCatalog,
  Score,
  type CatalogEntry,
  type LibraryClip,
  type PackEntry,
} from "@flowstate/schema";
import { loadPack, packDirs, type Pack } from "./pack.ts";

const here = path.dirname(fileURLToPath(import.meta.url));
export const repoRoot = path.join(here, "..", "..");
export const packsRoot = path.join(repoRoot, "library", "packs");
export const catalogRoot = path.join(repoRoot, "library", "catalog");

export function analyzerPath(): string {
  const exe = process.platform === "win32" ? "fs-analyze.exe" : "fs-analyze";
  const p = process.env.FLOWSTATE_FS_ANALYZE ?? path.join(repoRoot, "build", "core", exe);
  if (!existsSync(p)) throw new Error(`fs-analyze not found at ${p}. Build core first: npm run build:core`);
  return p;
}

/** What fs-analyze prints (analysisJson in core/src/analyze.cpp). */
interface Analysis {
  ok: boolean;
  error?: { code: string; message: string };
  role: string;
  key: { tonic: string | null; mode: string | null; guess: string | null; reason: string };
  grid: { grid: number; swing: number; fit: number };
  descriptors: { density: number; complexity: number; energy: number; syncopation: number; groove: "straight" | "swing" | "triplet" };
  tempo: number;
  meter: [number, number];
  bars: number;
  harmony: string[];
  fidelity: { rhythm: number; pitch: number; notes: number; literalNotes: number };
  score: unknown;
  warnings: string[];
}

export interface EntryResult {
  id: string;
  ok: boolean;
  /** `code: message`, precise and free of paths, when the entry can't be imported. */
  error: string | null;
  warnings: string[];
}

export interface PackResult {
  packId: string;
  problems: { where: string; message: string }[];
  entries: EntryResult[];
}

export interface Build {
  catalog: LibraryCatalog;
  /** Normalized MIDI per catalog file name. */
  files: Map<string, Buffer>;
  results: PackResult[];
  ok: boolean;
}

export const clipFileName = (packId: string, entryId: string) => `${packId}--${entryId}.mid`;
export const entryCatalogId = (packId: string, entryId: string) => `lib:${packId}/${entryId}`;

function analyze(exe: string, file: string, entry: PackEntry, work: string): { analysis: Analysis; midi: Buffer | null } {
  const json = path.join(work, `${entry.id}.json`);
  const mid = path.join(work, `${entry.id}.mid`);
  const args = ["--in", file, "--lane", entry.lane, "--title", entry.title, "--name", entry.title,
    "--style", entry.genres.join(","), "--out-json", json, "--out-mid", mid];
  if (entry.key) args.push("--key", `${entry.key.tonic} ${entry.key.mode}`);
  try {
    execFileSync(exe, args, { stdio: ["ignore", "ignore", "pipe"] });
  } catch (e) {
    const status = (e as { status?: number }).status;
    if (status !== 3) throw new Error(`fs-analyze failed on ${entry.id} (exit ${status}): ${String((e as { stderr?: Buffer }).stderr ?? "")}`);
  }
  const analysis = JSON.parse(readFileSync(json, "utf8")) as Analysis;
  return { analysis, midi: analysis.ok ? readFileSync(mid) : null };
}

function clipFor(pack: Pack, entry: PackEntry, a: Analysis, source: Buffer): LibraryClip {
  const m = pack.manifest;
  const [meterNumerator, meterDenominator] = a.meter;
  const keyNote = entry.key ? "Set by the producer" : a.key.tonic ? `Detected: ${a.key.reason}` : `Blank: ${a.key.reason}`;
  const catalogEntry: CatalogEntry = {
    id: entryCatalogId(m.id, entry.id),
    origin: "library",
    title: entry.title,
    roles: [entry.lane],
    genres: entry.genres,
    tags: entry.tags,
    feel: entry.feel,
    tonic: entry.key?.tonic ?? (a.key.tonic as CatalogEntry["tonic"]),
    mode: entry.key?.mode ?? (a.key.mode as CatalogEntry["mode"]),
    keyNote,
    tempo: a.tempo,
    tempoMin: entry.tempoMin ?? Math.max(20, a.tempo - 8),
    tempoMax: entry.tempoMax ?? Math.min(400, a.tempo + 8),
    meterNumerator,
    meterDenominator,
    bars: a.bars,
    energy: a.descriptors.energy,
    density: a.descriptors.density,
    complexity: a.descriptors.complexity,
    groove: a.descriptors.groove,
    credit: {
      packId: m.id,
      packTitle: m.title,
      producer: m.producer,
      text: m.credit,
      licenseId: m.license.id,
      allowsExport: m.license.allowsExport,
      allowsStyleExamples: m.license.allowsStyleExamples,
    },
    nodeId: null,
    prompt: null,
    kind: null,
    createdAtMs: null,
  };
  return {
    entry: catalogEntry,
    file: clipFileName(m.id, entry.id),
    sha256: createHash("sha256").update(source).digest("hex"),
    harmony: a.harmony,
    fidelity: a.fidelity,
    score: Score.parse(a.score),
  };
}

/** Validates and imports every pack under `root` (or just `only`). Never throws on a bad entry. */
export function buildCatalog(root = packsRoot, only?: string[]): Build {
  const exe = analyzerPath();
  const work = mkdtempSync(path.join(tmpdir(), "flowstate-library-"));
  const results: PackResult[] = [];
  const clips: LibraryClip[] = [];
  const packs: LibraryCatalog["packs"] = [];
  const files = new Map<string, Buffer>();
  try {
    for (const dir of only ?? packDirs(root)) {
      const { pack, problems } = loadPack(dir);
      const result: PackResult = { packId: path.basename(dir), problems, entries: [] };
      results.push(result);
      if (!pack) continue;
      const m = pack.manifest;
      packs.push({
        id: m.id,
        title: m.title,
        producer: m.producer,
        credit: m.credit,
        licenseId: m.license.id,
        notes: [...m.laneNotes, ...m.omitted.map((o) => `Left out ${path.basename(o.file, ".mid")}: ${o.reason}`)],
      });
      for (const entry of m.entries) {
        const file = path.join(dir, ...entry.file.split("/"));
        const { analysis, midi } = analyze(exe, file, entry, work);
        if (!analysis.ok || !midi) {
          result.entries.push({ id: entry.id, ok: false, error: `${analysis.error?.code}: ${analysis.error?.message}`, warnings: analysis.warnings });
          continue;
        }
        const clip = clipFor(pack, entry, analysis, readFileSync(file));
        clips.push(clip);
        files.set(clip.file, midi);
        result.entries.push({ id: entry.id, ok: true, error: null, warnings: analysis.warnings });
      }
    }
  } finally {
    rmSync(work, { recursive: true, force: true });
  }
  const catalog = LibraryCatalog.parse({ schema: LIBRARY_CATALOG_ID, packs, clips });
  const ok = results.every((r) => r.problems.length === 0 && r.entries.every((e) => e.ok));
  return { catalog, files, results, ok };
}

export function catalogText(catalog: LibraryCatalog): string {
  return JSON.stringify(catalog, null, 2) + "\n";
}
