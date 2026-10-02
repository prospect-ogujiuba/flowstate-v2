// The library catalog, loaded once from library/catalog. Picking and showing examples is the library capability's
// (capabilities/library-examples.ts); `--examples N` on the CLI puts them in the request, off by default.
import { readFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { LibraryCatalog } from "@flowstate/schema";

const here = path.dirname(fileURLToPath(import.meta.url));
export const catalogPath = path.join(here, "..", "..", "library", "catalog", "catalog.json");

let cached: LibraryCatalog | undefined;

export function loadCatalog(file = catalogPath): LibraryCatalog {
  if (file === catalogPath && cached) return cached;
  const catalog = LibraryCatalog.parse(JSON.parse(readFileSync(file, "utf8")));
  if (file === catalogPath) cached = catalog;
  return catalog;
}

export { examplesText, pickExamples, type ExampleRequest } from "./capabilities/library-examples.ts";
