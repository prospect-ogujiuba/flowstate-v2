// The empty Studio: v1's Home ("What do you want to make today?") with the first-run starters from
// the proposal's first flow: start from a vibe, use what I just played, surprise me.

import { useId } from "preact/hooks";
import { useToast } from "../components/index.ts";
import type { ServiceState } from "./useStudio.ts";

function Starter({ title, body, onClick, unavailable }: { title: string; body: string; onClick: () => void; unavailable?: string | null }) {
  const toast = useToast();
  const id = useId();
  return (
    <li>
      <button
        type="button"
        class={unavailable ? "st-starter is-unavailable" : "st-starter"}
        aria-disabled={unavailable ? true : undefined}
        aria-describedby={`${id}-body`}
        title={unavailable ?? undefined}
        onClick={unavailable ? () => toast({ message: unavailable }) : onClick}
      >
        <span class="st-starter__title">{title}</span>
        <span id={`${id}-body`} class="st-starter__body">{unavailable ?? body}</span>
      </button>
    </li>
  );
}

const BANNER: Record<ServiceState["state"], { tone: string; label: string }> = {
  connected: { tone: "ok", label: "Ready" },
  connecting: { tone: "ok", label: "Working" },
  disconnected: { tone: "neutral", label: "Start" },
  error: { tone: "warn", label: "Needs attention" },
};

export function Starters({ service, captureBars, captureGap, onVibe, onCapture, onSurprise, onLibrary, onSettings }: {
  service: ServiceState;
  captureBars: number;
  captureGap: string | null;
  onVibe: () => void;
  onCapture: () => void;
  onSurprise: () => void;
  onLibrary: () => void;
  onSettings: () => void;
}) {
  const banner = BANNER[service.state];
  const offline = service.label.startsWith("Can't reach");
  const text = service.state === "disconnected" && !offline ? "Describe an idea below, or pick a starter. It plays in time with your song." : service.label;
  return (
    <section class="st-home" aria-labelledby="st-home-h">
      <h2 id="st-home-h" class="st-home__title">What do you want to make today?</h2>
      <p class="st-home__lead">Describe an idea, start from what you played, or let Flowstate surprise you. Drag the result into your DAW.</p>
      <p class={`st-banner st-banner--${offline ? "danger" : banner.tone}`} role="status">
        <strong>{offline ? "Offline" : banner.label}:</strong> {text}
      </p>
      <ul class="st-starters" aria-label="Starters">
        <Starter title="Start from a vibe" body="Describe a style, mood or reference in the prompt bar below." onClick={onVibe} />
        <Starter
          title="Use what I just played"
          body={`Continue, harmonize or answer the last ${captureBars} bars you played on this track.`}
          unavailable={captureGap ?? (captureBars === 0 ? "Play something on this track first: nothing has been captured yet." : null)}
          onClick={onCapture}
        />
        <Starter title="Surprise me" body="A full idea in the session's key and tempo, no prompt needed." onClick={onSurprise} />
        <Starter title="Start from the library" body="Browse the built-in MIDI library and your earlier results." onClick={onLibrary} />
        <Starter title="Settings and connection" body="Model, your own API key, MIDI out and the preview synth." onClick={onSettings} />
      </ul>
    </section>
  );
}
