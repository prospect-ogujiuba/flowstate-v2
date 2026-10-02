// The Studio (P1-9): one screen inside v1's shell. The context strip and toolbar sit on top, the part
// lanes in the middle, the prompt bar underneath, and the thread drawer at the side. The plugin owns
// the session; this renders it and sends commands (docs/bridge-spec.md).

import type { Session } from "@flowstate/schema";
import { useEffect, useState } from "preact/hooks";
import { isTextEntry, installHostKeys } from "../host/keys.ts";
import type { Bridge } from "../host/types.ts";
import { clipBeats } from "./clip.ts";
import { ContextSheet, ContextStrip, type ContextField } from "./Context.tsx";
import { Lanes } from "./Lanes.tsx";
import { Library } from "./Library.tsx";
import { CaptureSheet, PromptBar, type PromptMode } from "./PromptBar.tsx";
import { SettingsSheet, type SettingsSection } from "./Settings.tsx";
import { Footer, Header, type View } from "./Shell.tsx";
import { Starters } from "./Starters.tsx";
import { Thread } from "./Thread.tsx";
import { Toolbar, TweakSheet } from "./Toolbar.tsx";
import { useStudio, type Studio } from "./useStudio.ts";

type SheetState =
  | { kind: "settings"; section: SettingsSection | null }
  | { kind: "context"; field: ContextField | null }
  | { kind: "capture" }
  | { kind: "tweak" }
  | null;

function useMedia(query: string) {
  const [match, setMatch] = useState(() => matchMedia(query).matches);
  useEffect(() => {
    const m = matchMedia(query);
    const on = () => setMatch(m.matches);
    m.addEventListener("change", on);
    return () => m.removeEventListener("change", on);
  }, [query]);
  return match;
}

/** Credit lines of library clips the session's nodes came from, so cards and lanes can show them. */
function useCredits(studio: Studio, session: Session | null) {
  const [credits, setCredits] = useState(new Map<string, string>());
  const missing = session?.nodes.some((n) => n.entryId && !credits.has(n.entryId)) ?? false;
  useEffect(() => {
    if (!missing) return;
    let cancelled = false;
    void (async () => {
      const next = new Map(credits);
      for (let offset = 0; ; offset += 100) {
        const r = await studio.send({ type: "searchCatalog", query: { text: "", origins: ["library"], roles: null, genres: null, fitContext: false, limit: 100, offset } });
        if (!r.catalog) break;
        for (const e of r.catalog.entries) if (e.credit) next.set(e.id, e.credit.text);
        if (offset + 100 >= r.catalog.total) break;
      }
      if (!cancelled) setCredits(next);
    })();
    return () => { cancelled = true; };
  }, [missing]);
  return credits;
}

const focusPrompt = () => document.querySelector<HTMLTextAreaElement>(".st-dock .fs-prompt__input")?.focus();

export function App({ bridge }: { bridge: Bridge }) {
  const studio = useStudio(bridge);
  const { session, transport, service, send, gap } = studio;
  const wide = useMedia("(min-width: 960px)");
  const compact = useMedia("(max-height: 600px)");
  const [view, setView] = useState<View>("studio");
  const [threadOpen, setThreadOpen] = useState(wide);
  const [prompt, setPrompt] = useState("");
  const [mode, setMode] = useState<PromptMode>("new");
  const [sheet, setSheet] = useState<SheetState>(null);
  const [splitDrums, setSplitDrums] = useState(false);
  const credits = useCredits(studio, session);

  useEffect(() => setThreadOpen(wide), [wide]);

  // Space and Escape belong to the host (docs/design/README.md). A text field that loses focus to
  // nothing hands focus back too, so the next Space reaches the DAW. Undo and redo have shortcuts
  // while the plugin has focus.
  useEffect(() => {
    const release = installHostKeys((reason) => void send({ type: "releaseFocus", reason }));
    const onFocusOut = (e: FocusEvent) => {
      if (isTextEntry(e.target as Element) && e.relatedTarget === null && !document.querySelector("dialog[open]")) void send({ type: "releaseFocus", reason: "blur" });
    };
    const onKey = (e: KeyboardEvent) => {
      if (!(e.ctrlKey || e.metaKey) || e.altKey || isTextEntry(document.activeElement) || document.querySelector("dialog[open]")) return;
      const k = e.key.toLowerCase();
      if (k === "z" && !e.shiftKey) { e.preventDefault(); void send({ type: "undo" }); }
      else if ((k === "z" && e.shiftKey) || k === "y") { e.preventDefault(); void send({ type: "redo" }); }
    };
    document.addEventListener("focusout", onFocusOut);
    document.addEventListener("keydown", onKey);
    return () => { release(); document.removeEventListener("focusout", onFocusOut); document.removeEventListener("keydown", onKey); };
  }, [send]);

  const beats = session?.clip ? clipBeats(session.clip) : null;
  const playhead = transport?.playing && beats ? transport.positionPpq % beats : null;
  const captureGap = gap("capture");
  const openCapture = () => setSheet({ kind: "capture" });

  return (
    <div class="st-app" data-compact={compact || undefined}>
      <Header
        view={view}
        onView={setView}
        midiOut={session?.midiOut ?? null}
        service={service}
        onSettings={() => setSheet({ kind: "settings", section: null })}
        onAccount={() => setSheet({ kind: "settings", section: "account" })}
        onTrack={() => setSheet({ kind: "settings", section: "midi" })}
      />
      <main class="st-main">
        {!session ? (
          <p class="st-note st-connecting" role="status">Connecting to the plugin…</p>
        ) : view === "library" ? (
          <div class="st-panel st-panel--scroll"><Library studio={studio} session={session} onUsed={() => setView("studio")} /></div>
        ) : (
          <div class="st-studio" data-thread={threadOpen ? "open" : "closed"} data-wide={wide || undefined}>
            <div class="st-panel st-work">
              <div class="st-strip">
                <ContextStrip context={session.context} override={session.override} onOpen={(field) => setSheet({ kind: "context", field })} />
                <Toolbar studio={studio} session={session} splitDrums={splitDrums} onSplitDrums={setSplitDrums} onTweak={() => setSheet({ kind: "tweak" })} threadOpen={threadOpen} onThread={() => setThreadOpen(!threadOpen)} />
              </div>
              <div class="st-stage">
                {session.clip ? (
                  <>
                    <Lanes studio={studio} clip={session.clip} parts={session.parts} playhead={playhead} splitDrums={splitDrums} compact={compact} />
                    <Credit session={session} credits={credits} />
                  </>
                ) : session.generations.length > 0 ? (
                  <p class="st-planning" role="status">Planning your idea. Parts play as they arrive…</p>
                ) : (
                  <Starters
                    service={service}
                    captureBars={session.captureBars}
                    captureGap={captureGap}
                    onVibe={focusPrompt}
                    onCapture={openCapture}
                    onSurprise={() => void send({ type: "generate", prompt: "", roles: null, count: 1, capture: null })}
                    onLibrary={() => setView("library")}
                    onSettings={() => setSheet({ kind: "settings", section: "ai" })}
                  />
                )}
              </div>
              <PromptBar studio={studio} session={session} value={prompt} onInput={setPrompt} mode={mode} onMode={setMode} onCapture={openCapture} onSettings={() => setSheet({ kind: "settings", section: "ai" })} />
            </div>
            <Thread
                open={threadOpen}
                studio={studio}
                session={session}
                credits={credits}
                splitDrums={splitDrums}
                onClose={() => setThreadOpen(false)}
                onBranch={async (node) => {
                  const r = await send({ type: "selectNode", nodeId: node.id });
                  if (r.ok) {
                    if (!wide) setThreadOpen(false);
                    focusPrompt();
                  }
                }}
              />
          </div>
        )}
      </main>
      <Footer transport={transport} clipBeats={beats} />

      {session && (
        <>
          <SettingsSheet open={sheet?.kind === "settings"} section={sheet?.kind === "settings" ? sheet.section : null} studio={studio} session={session} onClose={() => setSheet(null)} />
          <ContextSheet open={sheet?.kind === "context"} focus={sheet?.kind === "context" ? sheet.field : null} context={session.context} override={session.override}
            onApply={(override) => void send({ type: "setContextOverride", override })} onClose={() => setSheet(null)} />
          <CaptureSheet open={sheet?.kind === "capture" && !captureGap} captureBars={session.captureBars} prompt={prompt} onClose={() => setSheet(null)}
            onGenerate={async (bars, intent) => {
              const r = await send({ type: "generate", prompt: prompt.trim(), roles: null, count: 1, capture: { bars, intent } });
              if (r.ok) setPrompt("");
            }} />
          <TweakSheet open={sheet?.kind === "tweak" && !gap("tweak")} studio={studio} session={session} onClose={() => setSheet(null)} />
        </>
      )}
    </div>
  );
}

/** The current idea's credit, when it came from a library clip. */
function Credit({ session, credits }: { session: Session; credits: Map<string, string> }) {
  const node = session.nodes.find((n) => n.id === session.currentNodeId);
  if (!node?.entryId) return null;
  return <p class="st-credit">{credits.get(node.entryId) ?? "From the built-in library"}</p>;
}
