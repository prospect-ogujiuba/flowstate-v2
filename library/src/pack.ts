// Loads a pack folder and checks its manifest: the v2 schema plus v1's path-safety and "no fake
// entries" rules. Every problem names the field or file it is about; none contains an absolute path.
import { existsSync, lstatSync, readFileSync, readdirSync, realpathSync } from "node:fs";
import path from "node:path";
import { LibraryPackManifest } from "@flowstate/schema";

export interface Pack {
  dir: string;
  manifest: LibraryPackManifest;
}

export interface PackLoad {
  pack: Pack | null;
  /** Problems with the pack as a whole or with a field; `where` is a manifest path like entries[3].file. */
  problems: { where: string; message: string }[];
}

/** v1's rule: a relative .mid path with forward slashes, no '..', no absolute or drive prefix. */
export function unsafePathReason(p: string): string | null {
  if (p.length === 0) return "is empty";
  if (p.includes("\\")) return "uses a backslash; use forward slashes";
  if (p.startsWith("/") || /^[A-Za-z]:/.test(p)) return "is absolute; use a path relative to the pack";
  if (p.split("/").some((s) => s === ".." || s === "." || s === "")) return "has an empty, '.' or '..' segment";
  if (p.includes("\0")) return "contains a NUL character";
  return null;
}

/** Text shown to people or models must not leak paths or file names. */
export function leakReason(text: string): string | null {
  if (/[\\/]/.test(text)) return "contains a path separator";
  if (/\.(mid|midi)\b/i.test(text)) return "names a MIDI file";
  return null;
}

/** Resolves a manifest path inside the pack; null with a reason when it is unsafe or missing. */
function resolveInside(dir: string, rel: string, ext: string | null): { file: string } | { reason: string } {
  const unsafe = unsafePathReason(rel);
  if (unsafe) return { reason: unsafe };
  if (ext && path.extname(rel).toLowerCase() !== ext) return { reason: `is not a ${ext} file` };
  const file = path.join(dir, ...rel.split("/"));
  if (!existsSync(file)) return { reason: "does not exist in the pack" };
  if (lstatSync(file).isSymbolicLink()) return { reason: "is a symbolic link; packs hold real files only" };
  const real = realpathSync(file);
  const root = realpathSync(dir);
  if (!real.startsWith(root + path.sep)) return { reason: "resolves outside the pack" };
  if (!lstatSync(real).isFile()) return { reason: "is not a file" };
  return { file: real };
}

function midiFilesUnder(dir: string, rel = ""): string[] {
  const out: string[] = [];
  for (const e of readdirSync(path.join(dir, rel), { withFileTypes: true })) {
    const r = rel ? `${rel}/${e.name}` : e.name;
    if (e.isDirectory()) out.push(...midiFilesUnder(dir, r));
    else if (/\.midi?$/i.test(e.name)) out.push(r);
  }
  return out.sort();
}

export function loadPack(dir: string): PackLoad {
  const problems: PackLoad["problems"] = [];
  const add = (where: string, message: string) => problems.push({ where, message });
  const manifestFile = path.join(dir, "manifest.json");
  if (!existsSync(manifestFile)) return { pack: null, problems: [{ where: "manifest.json", message: "is missing" }] };
  let raw: unknown;
  try {
    raw = JSON.parse(readFileSync(manifestFile, "utf8"));
  } catch (e) {
    return { pack: null, problems: [{ where: "manifest.json", message: `is not JSON: ${(e as Error).message}` }] };
  }
  const parsed = LibraryPackManifest.safeParse(raw);
  if (!parsed.success) {
    for (const issue of parsed.error.issues) add(issue.path.join(".") || "manifest", issue.message);
    return { pack: null, problems };
  }
  const m = parsed.data;
  if (m.id !== path.basename(dir)) add("id", `"${m.id}" must equal the pack's folder name "${path.basename(dir)}"`);

  const credit = resolveInside(dir, m.creditFile, ".md");
  if ("reason" in credit) add("creditFile", `${m.creditFile} ${credit.reason}`);
  else if (!readFileSync(credit.file, "utf8").includes(m.credit)) add("creditFile", "must quote the manifest's credit line");
  for (const [field, text] of [["title", m.title], ["producer", m.producer], ["credit", m.credit]] as const) {
    const leak = leakReason(text);
    if (leak) add(field, leak);
  }

  const ids = new Set<string>();
  const files = new Set<string>();
  m.entries.forEach((e, i) => {
    const at = `entries[${i}]`;
    if (ids.has(e.id)) add(`${at}.id`, `"${e.id}" is used twice`);
    ids.add(e.id);
    if (files.has(e.file)) add(`${at}.file`, `${e.file} is listed twice`);
    files.add(e.file);
    const r = resolveInside(dir, e.file, ".mid");
    if ("reason" in r) add(`${at}.file`, `${e.file} ${r.reason}`);
    if ((e.tempoMin === null) !== (e.tempoMax === null)) add(`${at}.tempoMin`, "set both ends of the tempo range, or neither");
    if (e.tempoMin !== null && e.tempoMax !== null && e.tempoMin > e.tempoMax) add(`${at}.tempoMin`, "is above tempoMax");
    for (const [field, text] of [["title", e.title], ["feel", e.feel], ["notes", e.notes]] as const) {
      const leak = text === null ? null : leakReason(text);
      if (leak) add(`${at}.${field}`, leak);
    }
  });

  // No silent files: every MIDI file in the pack is an entry or is omitted with a reason.
  const omitted = new Set(m.omitted.map((o) => o.file));
  for (const rel of midiFilesUnder(dir)) {
    if (!files.has(rel) && !omitted.has(rel) && !omitted.has(path.basename(rel)))
      add("entries", `${rel} is in the pack but is neither an entry nor omitted with a reason`);
  }
  return { pack: problems.length ? null : { dir, manifest: m }, problems };
}

/** Pack folders under a library root (each holds a manifest.json), sorted. */
export function packDirs(root: string): string[] {
  return readdirSync(root, { withFileTypes: true })
    .filter((e) => e.isDirectory())
    .map((e) => path.join(root, e.name))
    .sort();
}
