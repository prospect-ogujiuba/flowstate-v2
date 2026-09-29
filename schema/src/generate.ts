// Writes the generated contract files from the Zod sources of truth:
// - schema/score.v0.schema.json   (score.ts)
// - schema/bridge.v0.schema.json  (bridge.ts)
// - schema/cpp/include/flowstate/bridge.h (bridge.ts, via cpp-gen.ts)
import { mkdirSync, writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import path from "node:path";
import { z } from "zod";
import { Score } from "./score.ts";
import { BRIDGE_ID, bridge } from "./bridge.ts";
import { generateCpp } from "./cpp-gen.ts";

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), "..");

function write(rel: string, text: string) {
  const out = path.join(root, rel);
  mkdirSync(path.dirname(out), { recursive: true });
  writeFileSync(out, text);
  console.log(`wrote ${out}`);
}

write("score.v0.schema.json", JSON.stringify(z.toJSONSchema(Score), null, 2) + "\n");

const { schemas } = z.toJSONSchema(bridge, { uri: (id) => `#/$defs/${id}` });
const defs = Object.fromEntries(
  Object.entries(schemas).map(([id, s]) => {
    const { $schema: _, ...rest } = s as Record<string, unknown>;
    return [id, rest];
  }),
);
write(
  "bridge.v0.schema.json",
  JSON.stringify({ $schema: "https://json-schema.org/draft/2020-12/schema", $id: BRIDGE_ID, $defs: defs }, null, 2) + "\n",
);
write("cpp/include/flowstate/bridge.h", generateCpp(defs as Parameters<typeof generateCpp>[0], BRIDGE_ID));
