import { useEffect, useState } from "preact/hooks";
import logo from "../assets/logo.png";
import { Icon } from "./Icon.tsx";

export function Logo({ height = 34 }: { height?: number }) {
  return <img class="fs-logo" src={logo} alt="Flowstate" height={height} />;
}

export type Connection = "connected" | "connecting" | "disconnected" | "error";

const labels: Record<Connection, string> = {
  connected: "AI service connected",
  connecting: "Connecting to the AI service",
  disconnected: "AI service disconnected",
  error: "AI service error",
};

/** v1's signal-bars indicator. Connecting steps through the four frames. */
export function ConnectionStatus({ state, label }: { state: Connection; /** Overrides the default description. */ label?: string }) {
  const [frame, setFrame] = useState(1);
  useEffect(() => {
    if (state !== "connecting" || matchMedia("(prefers-reduced-motion: reduce)").matches) return;
    const t = setInterval(() => setFrame((f) => (f % 4) + 1), 250);
    return () => clearInterval(t);
  }, [state]);
  const icon = state === "connecting" ? `ai-connection-connecting-${frame}` : `ai-connection-${state}`;
  return (
    <span class={`fs-connection fs-connection--${state}`} role="img" aria-label={label ?? labels[state]} title={label ?? labels[state]}>
      <Icon name={icon} size={16} />
    </span>
  );
}
