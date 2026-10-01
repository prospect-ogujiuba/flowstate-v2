// npm run -w library validate [-- packs/<id> ...]   check packs; every entry imports or says why not
// npm run -w library build                         write library/catalog (catalog.json + normalized clips)
// npm run -w library check                         fail if library/catalog differs from a fresh build
import { existsSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import path from "node:path";
import { buildCatalog, catalogRoot, catalogText, packsRoot, type Build } from "./catalog.ts";

function report(build: Build): void {
  for (const r of build.results) {
    const good = r.entries.filter((e) => e.ok).length;
    console.log(`${r.packId}: ${good}/${r.entries.length} entries import${r.problems.length ? `, ${r.problems.length} manifest problems` : ""}`);
    for (const p of r.problems) console.log(`  ✗ ${p.where}: ${p.message}`);
    for (const e of r.entries) {
      if (!e.ok) console.log(`  ✗ ${e.id}: ${e.error}`);
      for (const w of e.warnings) console.log(`  · ${e.id}: ${w}`);
    }
  }
}

const [command, ...rest] = process.argv.slice(2);
const only = rest.length ? rest.map((p) => path.resolve(packsRoot, "..", p)) : undefined;

if (command === "validate") {
  const build = buildCatalog(packsRoot, only);
  report(build);
  process.exit(build.ok ? 0 : 1);
} else if (command === "build" || command === "check") {
  const build = buildCatalog();
  report(build);
  if (!build.ok) {
    console.error("Not written: fix the entries above, or move them to `omitted` with a reason.");
    process.exit(1);
  }
  const wanted = new Map<string, Buffer>([["catalog.json", Buffer.from(catalogText(build.catalog))], ...build.files]);
  if (command === "build") {
    rmSync(catalogRoot, { recursive: true, force: true });
    mkdirSync(catalogRoot, { recursive: true });
    for (const [name, data] of wanted) writeFileSync(path.join(catalogRoot, name), data);
    console.log(`wrote ${build.catalog.clips.length} clips to ${path.relative(process.cwd(), catalogRoot) || "."}`);
  } else {
    const present = existsSync(catalogRoot) ? readdirSync(catalogRoot) : [];
    const drift = [
      ...[...wanted].filter(([n, d]) => !present.includes(n) || !readFileSync(path.join(catalogRoot, n)).equals(d)).map(([n]) => n),
      ...present.filter((n) => !wanted.has(n)),
    ];
    if (drift.length) {
      console.error(`library/catalog is stale (${drift.join(", ")}). Run 'npm run -w library build' and commit the result.`);
      process.exit(1);
    }
    console.log("library/catalog is up to date");
  }
} else {
  console.error("usage: tsx src/cli.ts validate [packs/<id> ...] | build | check");
  process.exit(2);
}
