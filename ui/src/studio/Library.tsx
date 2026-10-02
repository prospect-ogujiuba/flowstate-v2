// The library browser (P1-17): built-in clips and this session's AI results in one search. Every
// library clip shows its credit. Preview plays in time with the host; Use makes it the current idea.

import type { CatalogEntry, CatalogQuery, Session } from "@flowstate/schema";
import { useEffect, useRef, useState } from "preact/hooks";
import { Badge, Button, Segmented, Select, TextField, Toggle } from "../components/index.ts";
import { modeLabel, ROLE_LABELS, type Role } from "./clip.ts";
import type { Studio } from "./useStudio.ts";

const PAGE = 24;
type Origin = "all" | "library" | "ai";

export function Library({ studio, session, onUsed }: { studio: Studio; session: Session; onUsed: () => void }) {
  const { send } = studio;
  const [text, setText] = useState("");
  const [origin, setOrigin] = useState<Origin>("all");
  const [role, setRole] = useState<"" | Role>("");
  const [fit, setFit] = useState(false);
  const [entries, setEntries] = useState<CatalogEntry[]>([]);
  const [total, setTotal] = useState(0);
  const [loading, setLoading] = useState(true);
  const seq = useRef(0);

  const query = (offset: number): CatalogQuery => ({
    text: text.trim(),
    origins: origin === "all" ? null : [origin],
    roles: role ? [role] : null,
    genres: null,
    fitContext: fit,
    limit: PAGE,
    offset,
  });

  const load = async (offset: number) => {
    const mine = ++seq.current;
    setLoading(true);
    const reply = await send({ type: "searchCatalog", query: query(offset) });
    if (mine !== seq.current) return;
    setLoading(false);
    if (!reply.catalog) return;
    setTotal(reply.catalog.total);
    setEntries((prev) => (offset === 0 ? reply.catalog!.entries : [...prev, ...reply.catalog!.entries]));
  };

  useEffect(() => {
    const t = setTimeout(() => void load(0), 200);
    return () => clearTimeout(t);
  }, [text, origin, role, fit]);

  // Leaving the library stops a preview.
  const previewing = useRef(session.preview);
  previewing.current = session.preview;
  useEffect(() => () => { if (previewing.current) void send({ type: "previewEntry", entryId: null }); }, []);

  return (
    <section class="st-library" aria-labelledby="st-library-h">
      <div class="st-library__head">
        <h2 id="st-library-h" class="st-panel__title">Library</h2>
        <p class="st-note">Built-in MIDI and your earlier results. Preview plays in time with your song.</p>
      </div>
      <div class="st-library__filters">
        <TextField label="Search" type="search" value={text} placeholder="neo-soul, bass, dusty…" onInput={setText} />
        <Segmented label="Source" value={origin} onChange={setOrigin} options={[{ value: "all", label: "All" }, { value: "library", label: "Library" }, { value: "ai", label: "Your results" }]} />
        <Select label="Part" value={role} onChange={(v) => setRole(v as "" | Role)}
          options={[{ value: "", label: "Any part" }, ...(["chords", "bass", "melody", "drums", "pad", "arp", "counter"] as Role[]).map((r) => ({ value: r, label: ROLE_LABELS[r] }))]} />
        <Toggle label="Fit to this session" checked={fit} onChange={setFit} />
      </div>
      <p class="st-library__count st-note" role="status">{loading ? "Searching…" : `${total} ${total === 1 ? "clip" : "clips"}`}</p>
      <ul class="st-library__list">
        {entries.map((e) => (
          <EntryCard
            key={e.id}
            entry={e}
            previewing={session.preview === e.id}
            current={e.nodeId !== null && e.nodeId === session.currentNodeId}
            onPreview={() => void send({ type: "previewEntry", entryId: session.preview === e.id ? null : e.id })}
            onUse={async () => {
              const r = await send({ type: "useEntry", entryId: e.id });
              if (r.ok) {
                if (session.preview) void send({ type: "previewEntry", entryId: null });
                onUsed();
              }
            }}
            onDrag={() => void send({ type: "dragEntry", entryId: e.id })}
          />
        ))}
      </ul>
      {entries.length < total && (
        <div class="st-library__more"><Button size="sm" disabled={loading} onClick={() => void load(entries.length)}>Show more</Button></div>
      )}
    </section>
  );
}

function EntryCard({ entry: e, previewing, current, onPreview, onUse, onDrag }: {
  entry: CatalogEntry;
  previewing: boolean;
  current: boolean;
  onPreview: () => void;
  onUse: () => void;
  onDrag: () => void;
}) {
  const key = e.tonic ? `${e.tonic} ${e.mode ? modeLabel(e.mode) : ""}`.trim() : "Key unknown";
  const meta = [key, `${Math.round(e.tempo)} bpm`, `${e.meterNumerator}/${e.meterDenominator}`, `${e.bars} bars`, ...e.genres].join(" · ");
  const noExport = e.credit && !e.credit.allowsExport ? "This clip's license doesn't allow dragging it out." : null;
  return (
    <li>
      <article class={current ? "fs-card st-entry is-current" : "fs-card st-entry"} aria-label={e.title}>
        <div class="st-entry__head">
          <h3 class="st-entry__title">{e.title}</h3>
          <Badge tone={e.origin === "library" ? "muted" : "accent"}>{e.origin === "library" ? "Library" : "Your result"}</Badge>
        </div>
        <p class="st-entry__meta">{e.roles.map((r) => ROLE_LABELS[r]).join(", ")} · {meta}</p>
        {e.prompt && <p class="st-entry__meta">"{e.prompt}"</p>}
        {e.credit && <p class="st-entry__credit">{e.credit.text}</p>}
        <div class="st-entry__actions">
          <Button variant="ghost" size="sm" icon={previewing ? "stop" : "play"} label={`Preview ${e.title}`} pressed={previewing} onClick={onPreview} />
          <Button variant="ghost" size="sm" icon="restore" label={`Use ${e.title}`} onClick={onUse} />
          <Button
            variant="ghost"
            size="sm"
            icon="grip"
            label={`Drag ${e.title} into your DAW`}
            unavailable={noExport}
            onPointerDown={(ev) => { ev.preventDefault(); onDrag(); }}
          />
        </div>
      </article>
    </li>
  );
}
