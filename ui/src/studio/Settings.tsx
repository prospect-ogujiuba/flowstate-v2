// The settings sheet (v1's Settings and AI Connection, as one sheet): provider and model, your own
// key, MIDI out and the preview synth, usage, and the build ID.

import type { Session } from "@flowstate/schema";
import { useEffect, useRef, useState } from "preact/hooks";
import { Button, SecretField, Select, Sheet, SheetSection, TextField, Toggle, UsageMeter } from "../components/index.ts";
import { ROLE_LABELS, type Role } from "./clip.ts";
import type { Studio } from "./useStudio.ts";

export type SettingsSection = "ai" | "midi" | "account";

const PROVIDERS = [
  { value: "", label: "Managed (Flowstate's default)" },
  { value: "anthropic", label: "Anthropic" },
  { value: "openai", label: "OpenAI" },
  { value: "google", label: "Google" },
  { value: "openrouter", label: "OpenRouter" },
  { value: "deepseek", label: "DeepSeek" },
];

export function SettingsSheet({ open, section, studio, session, onClose }: {
  open: boolean;
  section: SettingsSection | null;
  studio: Studio;
  session: Session;
  onClose: () => void;
}) {
  const { send, gap } = studio;
  const s = session.settings;
  const [provider, setProvider] = useState(s.provider?.provider ?? "");
  const [model, setModel] = useState(s.provider?.model ?? "");
  const [key, setKey] = useState("");
  const body = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!open) { setKey(""); return; }
    setProvider(s.provider?.provider ?? "");
    setModel(s.provider?.model ?? "");
    if (section) requestAnimationFrame(() => body.current?.querySelector(`[data-section="${section}"]`)?.scrollIntoView({ block: "start" }));
  }, [open]);

  const chosen = provider ? { provider, model: model.trim() } : null;
  const saved = s.provider ? `${s.provider.provider}/${s.provider.model}` : "";
  const dirty = (chosen ? `${chosen.provider}/${chosen.model}` : "") !== saved;
  const keyGap = gap("apiKey");
  const u = s.usage;

  return (
    <Sheet open={open} title="Settings" onClose={onClose} footer={<Button size="sm" onClick={onClose}>Done</Button>}>
      <div ref={body} class="st-sheet-body">
        <div data-section="ai">
          <SheetSection title="AI connection">
            <div class="st-pair">
              <Select label="Provider" value={provider} onChange={setProvider} options={PROVIDERS} />
              <TextField label="Model" value={provider ? model : ""} disabled={!provider} placeholder={provider ? "e.g. openai/gpt-5.5" : "Chosen by Flowstate"} onInput={setModel} />
            </div>
            <div class="st-row">
              <Button size="sm" disabled={!dirty || (chosen !== null && !chosen.model)} onClick={() => void send({ type: "setProvider", provider: chosen })}>Use this model</Button>
              <span class="st-note">Now: {s.provider ? `${s.provider.provider} • ${s.provider.model}` : "managed default"}</span>
            </div>
            {s.byokEnabled && (
              <>
                <SecretField
                  label="Your API key"
                  value={key}
                  placeholder={s.hasKey ? "A key is stored for this provider" : "Stored in your OS keychain, never in the project"}
                  hint={provider ? `For ${PROVIDERS.find((p) => p.value === provider)?.label ?? provider}. Flowstate never shows it again.` : "Choose a provider first; the managed default needs no key."}
                  onInput={setKey}
                />
                <div class="st-row">
                  <Button size="sm" disabled={!keyGap && (!provider || !key.trim())} unavailable={keyGap}
                    onClick={() => { void send({ type: "setApiKey", provider, key: key.trim() }); setKey(""); }}>Save key</Button>
                  {s.hasKey && <Button size="sm" variant="ghost" unavailable={keyGap} onClick={() => void send({ type: "setApiKey", provider, key: null })}>Remove key</Button>}
                </div>
              </>
            )}
          </SheetSection>
        </div>
        <div data-section="midi">
          <SheetSection title="Sound and MIDI out">
            <Toggle label="Preview synth (built-in sounds)" checked={s.previewSynth} onChange={(enabled) => void send({ type: "setPreviewSynth", enabled })} />
            <div class="st-pair">
              <Select label="This instance sends" value={session.midiOut.role ?? ""} onChange={(v) => void send({ type: "setMidiOut", midiOut: { ...session.midiOut, role: v ? (v as Role) : null } })}
                options={[{ value: "", label: "All parts" }, ...(["chords", "bass", "melody", "drums", "pad", "arp", "counter"] as Role[]).map((r) => ({ value: r, label: `${ROLE_LABELS[r]} only` }))]} />
              <Select label="MIDI channel" value={session.midiOut.channel === null ? "" : String(session.midiOut.channel)} onChange={(v) => void send({ type: "setMidiOut", midiOut: { ...session.midiOut, channel: v ? Number(v) : null } })}
                options={[{ value: "", label: "Each part's own" }, ...Array.from({ length: 16 }, (_, i) => ({ value: String(i + 1), label: `Channel ${i + 1}` }))]} />
            </div>
            <p class="st-note">One Flowstate per track works with no routing: set each instance to send one part.</p>
          </SheetSection>
        </div>
        <div data-section="account">
          <SheetSection title="Account and usage">
            {u ? (
              <UsageMeter label="Usage" value={u.limit ? Math.max(0, 1 - u.used / u.limit) : 1}
                valueText={u.limit ? `${Math.round(Math.max(0, 1 - u.used / u.limit) * 100)}% remaining` : `${u.used} used, no limit`}
                tone={u.limit && u.used / u.limit > 0.9 ? "danger" : u.limit && u.used / u.limit > 0.7 ? "warn" : "ok"} />
            ) : (
              <p class="st-note">Usage shows here once the AI service reports it. No sign-in is needed in this build.</p>
            )}
            <p class="st-note">Build <span class="fs-num">{s.buildId}</span></p>
          </SheetSection>
        </div>
      </div>
    </Sheet>
  );
}
