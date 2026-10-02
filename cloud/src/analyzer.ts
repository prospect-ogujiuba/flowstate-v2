// The host side of the MIDI analysis capability: runs core's fs-analyze on a library clip. The clip is
// looked up by catalog entry id, and its file comes from the catalog, never from the model. The binary is
// run directly (no shell), with a timeout and the request's abort signal.
import { execFile } from "node:child_process";
import { existsSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import type { LibraryCatalog } from "@flowstate/schema";
import type { ClipAnalysis } from "./capabilities/api.ts";
import { catalogPath } from "./examples.ts";

const here = path.dirname(fileURLToPath(import.meta.url));

export function analyzerPath(): string {
  const exe = process.platform === "win32" ? "fs-analyze.exe" : "fs-analyze";
  return process.env.FLOWSTATE_FS_ANALYZE ?? path.join(here, "..", "..", "build", "core", exe);
}

/** Analysis of library clips by entry id, or null when fs-analyze isn't built. */
export function clipAnalyzer(
  catalog: LibraryCatalog,
  options: { exe?: string; catalogDir?: string } = {},
): ((entryId: string, signal?: AbortSignal) => Promise<ClipAnalysis>) | null {
  const exe = options.exe ?? analyzerPath();
  if (!existsSync(exe)) return null;
  const dir = options.catalogDir ?? path.dirname(catalogPath);
  const clips = new Map(catalog.clips.map((c) => [c.entry.id, c]));

  return (entryId, signal) =>
    new Promise((resolve, reject) => {
      const clip = clips.get(entryId);
      if (!clip) return reject(new Error(`no library clip '${entryId}'`));
      const e = clip.entry;
      const args = ["--in", path.join(dir, path.basename(clip.file)), "--lane", e.roles[0]!];
      if (e.tonic && e.mode) args.push("--key", `${e.tonic} ${e.mode}`);
      execFile(exe, args, { shell: false, timeout: 10_000, maxBuffer: 4 << 20, ...(signal ? { signal } : {}) }, (err, stdout) => {
        // Exit 3: analyzed, but the clip can't be imported; the reason is in the JSON.
        if (err && (err as { code?: unknown }).code !== 3) return reject(new Error(`fs-analyze failed on ${entryId}: ${err.message.split("\n")[0]}`));
        try {
          resolve(JSON.parse(stdout) as ClipAnalysis);
        } catch {
          reject(new Error(`fs-analyze printed no analysis for ${entryId}`));
        }
      });
    });
}
