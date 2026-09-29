// Writes schema/score.v0.schema.json from the Zod source of truth.
import { writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import path from "node:path";
import { z } from "zod";
import { Score } from "./score.ts";

const here = path.dirname(fileURLToPath(import.meta.url));
const out = path.join(here, "..", "score.v0.schema.json");
writeFileSync(out, JSON.stringify(z.toJSONSchema(Score), null, 2) + "\n");
console.log(`wrote ${out}`);
