// v1's shell around the Studio (../flowstate/Source/ui/HeaderBar.cpp, FooterBar.cpp): nav pills on
// the left, the logo in the middle, the track pill and header buttons on the right; a footer with
// the AI warning, a meter in the middle and the copyright.

import type { Session } from "@flowstate/schema";
import { Button, ConnectionStatus, Logo } from "../components/index.ts";
import { ROLE_LABELS } from "./clip.ts";
import type { ServiceState, Transport } from "./useStudio.ts";

export type View = "studio" | "library";

/** What this instance sends (the per-instance MIDI out), in v1's yellow track pill. */
export function sendsLabel(midiOut: Session["midiOut"]) {
  const what = midiOut.role ? `${ROLE_LABELS[midiOut.role]} only` : "All parts";
  return midiOut.channel ? `${what} · ch ${midiOut.channel}` : what;
}

export function Header({ view, onView, midiOut, service, onSettings, onAccount, onTrack }: {
  view: View;
  onView: (v: View) => void;
  midiOut: Session["midiOut"] | null;
  service: ServiceState;
  onSettings: () => void;
  onAccount: () => void;
  onTrack: () => void;
}) {
  const nav: { id: View; label: string }[] = [{ id: "studio", label: "Studio" }, { id: "library", label: "Library" }];
  return (
    <header class="st-header">
      <nav class="st-nav" aria-label="Views">
        {nav.map((n) => (
          <button
            key={n.id}
            type="button"
            class={view === n.id ? "st-pill is-active" : "st-pill"}
            aria-current={view === n.id ? "page" : undefined}
            onClick={() => onView(n.id)}
          >
            {n.label}
          </button>
        ))}
      </nav>
      <button type="button" class="st-brand" aria-label="Flowstate Studio" title="Studio" onClick={() => onView("studio")}>
        <Logo height={50} />
      </button>
      <div class="st-hud">
        {midiOut && (
          <button type="button" class="st-track fs-num" aria-label={`MIDI out: ${sendsLabel(midiOut)}`} title="What this instance sends (MIDI out)" onClick={onTrack}>
            {sendsLabel(midiOut)}
          </button>
        )}
        <ConnectionStatus state={service.state} label={service.label} />
        <Button variant="ghost" icon="sliders" label="Settings" class="st-hud__btn" onClick={onSettings} />
        <Button variant="ghost" icon="user-account" label="Account and usage" class="st-hud__btn" onClick={onAccount} />
      </div>
    </header>
  );
}

/** Where the host is in the current idea, as a thin bar where v1 had its level meter. */
export function Footer({ transport, clipBeats }: { transport: Transport | null; clipBeats: number | null }) {
  const playing = Boolean(transport?.playing);
  const position = playing && clipBeats ? (transport!.positionPpq % clipBeats) / clipBeats : 0;
  const where = transport && playing ? `Bar ${transport.bar} · beat ${Math.floor(transport.beat)} · ${Math.round(transport.tempo)} bpm` : "Host stopped";
  return (
    <footer class="st-footer">
      <span class="st-footer__warning">AI can make mistakes. Always double check its output.</span>
      <div class="st-footer__pos">
        <div class="st-footer__track" role="img" aria-label={playing && clipBeats ? `Loop position ${Math.round(position * 100)}%` : "Loop position: stopped"}>
          <span class="st-footer__fill" style={{ width: `${position * 100}%` }} />
        </div>
        <span class="st-footer__where fs-num">{where}</span>
      </div>
      <strong class="st-footer__copy">{new Date().getFullYear()} Flowstate ©</strong>
    </footer>
  );
}
