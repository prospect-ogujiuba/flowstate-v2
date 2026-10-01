// Component gallery (P1-8): every component and state, in a browser and in the plugin WebView.
// Open with `npm run -w ui dev` (then /gallery.html), or in the plugin with FLOWSTATE_UI_PAGE=gallery.html.

import { render } from "preact";
import { useEffect, useState } from "preact/hooks";
import {
  Badge, Button, Card, ConnectionStatus, ContextChip, Icon, Knob, Lane, LevelMeter, Logo, PromptBox, PromptSetting,
  ResultCard, SecretField, Segmented, Select, Sheet, SheetSection, SuggestionChip, TextArea, TextField, Toggle,
  ToastProvider, UsageMeter, iconNames, useToast, type Connection, type LaneState,
} from "../components/index.ts";
import { inPlugin } from "../host/env.ts";
import { installHostKeys } from "../host/keys.ts";
import * as fx from "./fixtures.ts";
import "./gallery.css";

const colourTokens = [
  "--bg", "--surface", "--surface-2", "--surface-3", "--control", "--control-press", "--input-bg", "--line", "--line-strong",
  "--text", "--text-2", "--text-3", "--placeholder", "--accent", "--accent-soft", "--focus", "--user", "--idea",
  "--ok", "--warn", "--danger", "--danger-solid", "--toast-info", "--toast-ok", "--toast-warn", "--toast-error",
  "--part-chords", "--part-bass", "--part-melody", "--part-drums", "--part-extra",
];
const typeTokens = ["--text-2xs", "--text-xs", "--text-sm", "--text-md", "--text-base", "--text-lg", "--text-xl", "--text-2xl"];
const spaceTokens = ["--space-0", "--space-1", "--space-2", "--space-3", "--space-4", "--space-5", "--space-6", "--space-7", "--space-8"];
const radiusTokens = ["--radius-sm", "--radius-md", "--radius-lg", "--radius-xl", "--radius-2xl", "--radius-pill"];

function Section({ id, title, children }: { id: string; title: string; children: preact.ComponentChildren }) {
  return (
    <section id={id} class="g-section" aria-labelledby={`${id}-h`}>
      <h2 id={`${id}-h`} class="g-section__title">{title}</h2>
      {children}
    </section>
  );
}

function Specimen({ label, children }: { label: string; children: preact.ComponentChildren }) {
  return (
    <figure class="g-specimen">
      <div class="g-specimen__body">{children}</div>
      <figcaption class="g-specimen__label">{label}</figcaption>
    </figure>
  );
}

function Tokens() {
  return (
    <Section id="tokens" title="Tokens">
      <div class="g-swatches">
        {colourTokens.map((t) => (
          <div class="g-swatch" key={t}>
            <span class="g-swatch__chip" style={{ background: `var(${t})` }} />
            <code>{t}</code>
          </div>
        ))}
      </div>
      <div class="g-scale">
        {typeTokens.map((t) => <p key={t} style={{ fontSize: `var(${t})` }}><code>{t}</code> Flowstate writes the bass line</p>)}
        <p style={{ fontWeight: "var(--weight-bold)" }}><code>--weight-bold</code> Chords · Bass · Melody · Drums</p>
        <p class="fs-num"><code>.fs-num</code> 120.0 bpm · 4/4 · 8 bars</p>
      </div>
      <div class="g-row g-row--end">
        {spaceTokens.map((t) => <Specimen key={t} label={t}><span class="g-space" style={{ width: `var(${t})` }} /></Specimen>)}
      </div>
      <div class="g-row">
        {radiusTokens.map((t) => <Specimen key={t} label={t}><span class="g-radius" style={{ borderRadius: `var(${t})` }} /></Specimen>)}
      </div>
    </Section>
  );
}

function Icons() {
  return (
    <Section id="icons" title="Icons">
      <div class="g-icons">
        {iconNames.map((name) => (
          <div key={name} class="g-icon"><Icon name={name} size={16} /><code>{name}</code></div>
        ))}
      </div>
    </Section>
  );
}

function Buttons() {
  const [pressed, setPressed] = useState(true);
  return (
    <Section id="buttons" title="Buttons">
      <div class="g-row">
        <Specimen label="primary"><Button variant="primary">Generate</Button></Specimen>
        <Specimen label="secondary"><Button>Test connection</Button></Specimen>
        <Specimen label="ghost"><Button variant="ghost" icon="copy">Copy</Button></Specimen>
        <Specimen label="ok"><Button variant="ok">Save</Button></Specimen>
        <Specimen label="danger"><Button variant="danger">Cancel</Button></Specimen>
        <Specimen label="nav"><Button variant="nav">Create</Button></Specimen>
        <Specimen label="nav, active"><Button variant="nav" active>Chat</Button></Specimen>
      </div>
      <div class="g-row">
        <Specimen label="disabled"><Button variant="primary" disabled>Generate</Button></Specimen>
        <Specimen label="disabled"><Button disabled>Test connection</Button></Specimen>
        <Specimen label="small"><Button size="sm" icon="export">Export</Button></Specimen>
        <Specimen label="icon"><Button variant="ghost" icon="sliders" label="Settings" /></Specimen>
        <Specimen label="icon, small"><Button variant="ghost" size="sm" icon="copy" label="Copy" /></Specimen>
        <Specimen label="toggle (aria-pressed)">
          <Button variant="ghost" icon="lock" label="Lock" pressed={pressed} onClick={() => setPressed(!pressed)} />
        </Specimen>
      </div>
    </Section>
  );
}

function Toggles() {
  const [mode, setMode] = useState<"chat" | "create">("chat");
  const [a, setA] = useState(true);
  const [b, setB] = useState(false);
  return (
    <Section id="toggles" title="Segmented and toggles">
      <div class="g-row">
        <Specimen label="segmented">
          <Segmented label="Mode" value={mode} onChange={setMode} options={[{ value: "chat", label: "Chat" }, { value: "create", label: "Create" }]} />
        </Specimen>
        <Specimen label="toggle, on"><Toggle label="Follow host tempo" checked={a} onChange={setA} /></Specimen>
        <Specimen label="toggle, off"><Toggle label="MIDI out" checked={b} onChange={setB} /></Specimen>
        <Specimen label="toggle, disabled"><Toggle label="Bring your own key" checked={false} onChange={() => {}} disabled /></Specimen>
      </div>
    </Section>
  );
}

function Knobs() {
  const [density, setDensity] = useState(0.5);
  const [swing, setSwing] = useState(0);
  const [humanize, setHumanize] = useState(0.25);
  const [register, setRegister] = useState(1);
  return (
    <Section id="knobs" title="Knobs">
      <div class="g-row">
        <Knob label="Density" value={density} defaultValue={0.5} format={(v) => `${Math.round(v * 100)}%`} onChange={setDensity} />
        <Knob label="Swing" value={swing} defaultValue={0} format={(v) => `${Math.round(v * 100)}%`} onChange={setSwing} size={40} />
        <Knob label="Humanize" value={humanize} defaultValue={0} format={(v) => `${Math.round(v * 100)}%`} onChange={setHumanize} size={24} />
        <Knob label="Register" value={register} min={-2} max={2} step={1} defaultValue={0} format={(v) => (v > 0 ? `+${v}` : String(v))} onChange={setRegister} />
        <Knob label="Locked" value={0.5} format={(v) => `${Math.round(v * 100)}%`} onChange={() => {}} disabled />
      </div>
      <p class="g-note">Arrows step · PageUp/PageDown step 10× · Home/End · drag up or down (Shift for fine) · double-click resets</p>
    </Section>
  );
}

function Inputs() {
  const [name, setName] = useState("");
  const [email, setEmail] = useState("not-an-email");
  const [key, setKey] = useState("sk-ant-api03-example-not-a-real-key");
  const [prompt, setPrompt] = useState("");
  const [provider, setProvider] = useState("anthropic");
  return (
    <Section id="inputs" title="Inputs">
      <div class="g-grid">
        <TextField label="Name" value={name} onInput={setName} placeholder="Producer name" hint="Shown on your shared ideas." />
        <TextField label="Email" type="email" value={email} onInput={setEmail} error="Enter an email address." />
        <Select label="Provider" value={provider} onChange={setProvider} options={[
          { value: "anthropic", label: "Anthropic" }, { value: "openai", label: "OpenAI" }, { value: "google", label: "Google" }, { value: "openrouter", label: "OpenRouter" },
        ]} />
        <SecretField label="API key" value={key} onInput={setKey} />
        <TextField label="Plan" value="BYOK" onInput={() => {}} readOnly />
        <TextArea label="System prompt" value={prompt} onInput={setPrompt} placeholder="Optional: steer the music guidance" rows={3} />
      </div>
    </Section>
  );
}

function Chips() {
  return (
    <Section id="chips" title="Chips and badges">
      <div class="g-row">
        <ContextChip label="Key" value="A minor" />
        <ContextChip label="Tempo" value="92 bpm" locked />
        <ContextChip label="Meter" value="4/4" locked />
        <ContextChip label="Bars" value="8" />
      </div>
      <div class="g-row">
        <SuggestionChip>Busier drums in bar 4</SuggestionChip>
        <SuggestionChip>Darker chords</SuggestionChip>
        <SuggestionChip>Add a counter-melody</SuggestionChip>
      </div>
      <div class="g-row">
        <Badge>Track 1</Badge><Badge tone="muted">Sketch</Badge><Badge tone="ok">Valid</Badge><Badge tone="warn">Offline</Badge><Badge tone="danger">Refused</Badge>
      </div>
    </Section>
  );
}

function Meters() {
  const [t, setT] = useState(0);
  useEffect(() => {
    if (matchMedia("(prefers-reduced-motion: reduce)").matches) return;
    const id = setInterval(() => setT((x) => x + 1), 1000 / 30);
    return () => clearInterval(id);
  }, []);
  const level = 0.55 + 0.3 * Math.sin(t / 9) * Math.sin(t / 23);
  return (
    <Section id="meters" title="Meters">
      <div class="g-stack">
        <UsageMeter label="5 hr" value={0.81} valueText="81% remaining" />
        <UsageMeter label="Weekly" value={0.34} valueText="34% remaining" tone="warn" />
        <UsageMeter label="Monthly" value={0.06} valueText="6% remaining" tone="danger" />
        <LevelMeter label="Output level" level={level} peak={Math.min(1, level + 0.06)} />
        <LevelMeter label="Clipping level" level={1} peak={1} />
      </div>
      <div class="g-row">
        {(["connected", "connecting", "disconnected", "error"] as Connection[]).map((s) => (
          <Specimen key={s} label={s}><ConnectionStatus state={s} /></Specimen>
        ))}
      </div>
    </Section>
  );
}

const laneDefaults: LaneState = { playing: false, solo: false, locked: false, density: 0.5 };

function useLanes() {
  const [lanes, setLanes] = useState<Record<string, LaneState>>({
    chords: { ...laneDefaults },
    bass: { ...laneDefaults, solo: true },
    melody: { ...laneDefaults, playing: true },
    drums: { ...laneDefaults, locked: true, density: 0.7 },
  });
  const [subs, setSubs] = useState(fx.drumSublanes);
  const toast = useToast();
  const lane = (id: string) => ({
    state: lanes[id]!,
    onState: (s: LaneState) => setLanes({ ...lanes, [id]: s }),
    onVary: () => toast({ message: `Vary ${id}: needs the agent service (P1-4)` }),
    onReroll: () => toast({ message: `Re-roll ${id}: a local transform (P1-9)` }),
    onDragStart: () => toast({ message: "Drag starts a native file drag in the plugin" }),
    beats: 16,
  });
  const onSublaneMute = (id: string, muted: boolean) => setSubs(subs.map((s) => (s.id === id ? { ...s, muted } : s)));
  return { lane, subs, onSublaneMute };
}

function Lanes({ playhead }: { playhead: number | null }) {
  const { lane, subs, onSublaneMute } = useLanes();
  return (
    <div class="g-stack">
      <Lane name="Chords" role="Pad voicing, whole notes" colour="chords" notes={fx.chords} playhead={playhead} {...lane("chords")} />
      <Lane name="Bass" role="Root and fifth, syncopated" colour="bass" notes={fx.bass} playhead={playhead} {...lane("bass")} />
      <Lane name="Melody" role="Lead, stepwise" colour="melody" notes={fx.melody} playhead={playhead} {...lane("melody")} />
      <Lane name="Drums" role="Boom bap, by sublane" colour="drums" notes={fx.drums} playhead={playhead} sublanes={subs} onSublaneMute={onSublaneMute} {...lane("drums")} />
    </div>
  );
}

function Cards() {
  const [current, setCurrent] = useState("n2");
  const [playing, setPlaying] = useState<string | null>(null);
  const nodes = [
    { id: "n1", title: "Dusty soul loop", kind: "initial" as const, meta: "A minor · 92 bpm · 8 bars · 4 parts" },
    { id: "n2", title: "Darker chords", kind: "edit" as const, meta: "Chords changed · 2 min ago" },
    { id: "n3", title: "Bass variation", kind: "vary" as const, meta: "Bass · just now" },
  ];
  return (
    <Section id="cards" title="Cards">
      <div class="g-grid">
        {nodes.map((n) => (
          <ResultCard
            key={n.id}
            {...n}
            current={n.id === current}
            playing={playing === n.id}
            onPlay={() => setPlaying(playing === n.id ? null : n.id)}
            onRestore={() => setCurrent(n.id)}
          />
        ))}
        <Card title="Plain card"><p class="g-note">A surface for grouped content, like the settings summary.</p></Card>
      </div>
    </Section>
  );
}

function SettingsSheet({ open, onClose }: { open: boolean; onClose: () => void }) {
  const [provider, setProvider] = useState("anthropic");
  const [model, setModel] = useState("opus");
  const [key, setKey] = useState("");
  const [system, setSystem] = useState("");
  const [role, setRole] = useState("producer");
  const toast = useToast();
  return (
    <Sheet
      open={open}
      title="Settings"
      onClose={onClose}
      footer={<>
        <Button variant="danger" size="sm" onClick={onClose}>Cancel</Button>
        <Button variant="ok" size="sm" onClick={() => { onClose(); toast({ kind: "success", message: "Settings saved" }); }}>Save</Button>
      </>}
    >
      <SheetSection title="AI configuration">
        <div class="g-pair">
          <Select label="Provider" value={provider} onChange={setProvider} options={[{ value: "anthropic", label: "Anthropic" }, { value: "openai", label: "OpenAI" }]} />
          <Select label="Model" value={model} onChange={setModel} options={[{ value: "opus", label: "Claude Opus 5.5" }, { value: "sonnet", label: "Claude Sonnet 5.5" }]} />
        </div>
        <SecretField label="API key" value={key} onInput={setKey} placeholder="Stored in your OS keychain" />
        <TextArea label="System prompt" value={system} onInput={setSystem} placeholder="Optional: steer the music guidance" rows={4} />
        <div><Button size="sm" onClick={() => toast({ kind: "warning", message: "No key yet: add one to test the connection" })}>Test connection</Button></div>
      </SheetSection>
      <SheetSection title="Account">
        <div class="g-pair">
          <TextField label="Name" value="" onInput={() => {}} placeholder="Not signed in" />
          <TextField label="Email" type="email" value="" onInput={() => {}} placeholder="No email connected" />
        </div>
        <div class="g-pair">
          <Select label="Role" value={role} onChange={setRole} options={[{ value: "producer", label: "Producer" }, { value: "mixing", label: "Mixing engineer" }]} />
          <TextField label="Plan" value="BYOK" onInput={() => {}} readOnly />
        </div>
        <UsageMeter label="5 hr" value={0.81} valueText="81% remaining" />
        <UsageMeter label="Weekly" value={0.34} valueText="34% remaining" tone="warn" />
      </SheetSection>
    </Sheet>
  );
}

function Overlays({ onOpenSheet }: { onOpenSheet: () => void }) {
  const toast = useToast();
  return (
    <Section id="overlays" title="Sheets and toasts">
      <div class="g-row">
        <Button icon="sliders" onClick={onOpenSheet}>Open settings sheet</Button>
        <Button onClick={() => toast({ message: "Planning 4 parts…" })}>Info toast</Button>
        <Button onClick={() => toast({ kind: "success", message: "Dragged 4 parts into your DAW" })}>Success toast</Button>
        <Button onClick={() => toast({ kind: "warning", message: "Offline: sketches only", action: { label: "Retry", run: () => {} } })}>Warning toast</Button>
        <Button onClick={() => toast({ kind: "error", message: "The model refused this prompt" })}>Error toast</Button>
      </div>
    </Section>
  );
}

/** v1's chat screen rebuilt from the new components, at v1's reference size, for side-by-side screenshots. */
function Shell({ onOpenSheet }: { onOpenSheet: () => void }) {
  const [mode, setMode] = useState<"chat" | "create">("chat");
  const [prompt, setPrompt] = useState("");
  const [power, setPower] = useState(true);
  const toast = useToast();
  return (
    <Section id="shell" title="Shell (v1 reference size, 900 × 650)">
      <div class="g-shell" data-testid="shell">
        <header class="g-shell__bar">
          <Segmented label="Mode" value={mode} onChange={setMode} options={[{ value: "chat", label: "Chat" }, { value: "create", label: "Create" }]} />
          <Logo height={50} />
          <div class="g-shell__hud">
            <Badge>Track 1</Badge>
            <span class="g-shell__context fs-num" title="Context window">121k/1M</span>
            <ConnectionStatus state="connected" />
            <Button variant="ghost" icon={power ? "plugin-power-enabled" : "plugin-power-bypassed"} label="Plugin enabled" pressed={power} class="g-shell__power" onClick={() => setPower(!power)} />
            <Button variant="ghost" icon="sliders" label="Settings" onClick={onOpenSheet} />
            <Button variant="ghost" icon="user-account" label="Account" />
          </div>
        </header>
        <main class="g-shell__main">
          <div class="g-shell__strip">
            <ContextChip label="Key" value="A minor" />
            <ContextChip label="Tempo" value="92 bpm" locked />
            <ContextChip label="Meter" value="4/4" locked />
            <ContextChip label="Bars" value="4" />
          </div>
          <Lanes playhead={null} />
        </main>
        <div class="g-shell__dock">
          <PromptBox
            value={prompt}
            onInput={setPrompt}
            onSubmit={(p) => { toast({ message: `Generate: ${p}` }); setPrompt(""); }}
            placeholder="Ask for general assistance or choose a mode"
            tools={<Button variant="ghost" icon="collect-audio-data" label="Capture MIDI from the track" class="g-shell__capture" />}
            settings={<>
              <PromptSetting icon={<Icon name="bot" size={18} class="g-shell__bot" />} label="Model" value="Anthropic • Claude Opus 5.5" />
              <PromptSetting icon={<Icon name="lightning" size={14} class="g-shell__idea" />} label="Effort" value="High • Normal" />
              <PromptSetting icon={<Icon name="lightbulb" size={16} />} label="Mode" value="Idea" />
            </>}
          />
          <LevelMeter label="Output level" level={0.63} peak={0.64} />
        </div>
        <footer class="g-shell__foot">
          <span>AI can make mistakes. Check its output.</span>
          <strong>2026 Flowstate ©</strong>
        </footer>
      </div>
    </Section>
  );
}

function Gallery() {
  const [sheet, setSheet] = useState(false);
  const [beat, setBeat] = useState(0);
  useEffect(() => {
    if (matchMedia("(prefers-reduced-motion: reduce)").matches) return;
    const id = setInterval(() => setBeat((b) => (b + 0.125) % 16), 1000 / 16);
    return () => clearInterval(id);
  }, []);
  return (
    <div class="g-scroll">
      <header class="g-top">
        <Logo height={40} />
        <div>
          <h1 class="g-top__title">Components</h1>
          <p class="g-note">Design system P1-8. Tab reaches every control; Space goes to the DAW, so controls take Enter.</p>
        </div>
      </header>
      <nav class="g-toc" aria-label="Sections">
        {["tokens", "icons", "buttons", "toggles", "knobs", "inputs", "chips", "meters", "lanes", "cards", "overlays", "shell"].map((s) => (
          <a key={s} href={`#${s}`}>{s}</a>
        ))}
      </nav>
      <Tokens />
      <Icons />
      <Buttons />
      <Toggles />
      <Knobs />
      <Inputs />
      <Chips />
      <Meters />
      <Section id="lanes" title="Lanes"><Lanes playhead={beat} /></Section>
      <Cards />
      <Overlays onOpenSheet={() => setSheet(true)} />
      <Shell onOpenSheet={() => setSheet(true)} />
      <SettingsSheet open={sheet} onClose={() => setSheet(false)} />
    </div>
  );
}

installHostKeys((reason) => {
  if (inPlugin()) void import("../host/bridge.ts").then(({ send }) => send({ type: "releaseFocus", reason }));
});

/** `?only=shell` renders the v1-size shell alone, full-bleed (for side-by-side screenshots). */
function ShellOnly() {
  const [sheet, setSheet] = useState(new URLSearchParams(location.search).has("settings"));
  return (
    <div class="g-shell-only">
      <Shell onOpenSheet={() => setSheet(true)} />
      <SettingsSheet open={sheet} onClose={() => setSheet(false)} />
    </div>
  );
}

const only = new URLSearchParams(location.search).get("only");
render(<ToastProvider>{only === "shell" ? <ShellOnly /> : <Gallery />}</ToastProvider>, document.getElementById("app")!);
