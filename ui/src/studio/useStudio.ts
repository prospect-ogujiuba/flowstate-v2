// The Studio's view of the plugin: the session it last sent, the host transport, and a `send` that
// applies replies and turns errors into toasts. The plugin owns all state; nothing here outlives it.

import type { Command, PluginEvent, Reply, Session } from "@flowstate/schema";
import { useCallback, useEffect, useMemo, useState } from "preact/hooks";
import { useToast, type Connection } from "../components/index.ts";
import type { Bridge } from "../host/types.ts";

export type Transport = Extract<PluginEvent, { type: "transport" }>["transport"];
export type Feature = Session["unavailable"][number]["feature"];
export type ErrorCode = NonNullable<Reply["error"]>["code"];

/** The agent service as the Studio last saw it: from generation events, since nothing else probes it. */
export type ServiceState = { state: Connection; label: string };

const UNKNOWN: ServiceState = { state: "disconnected", label: "AI service: not used yet in this session" };

function serviceAfter(code: ErrorCode, message: string): ServiceState {
  if (code === "network") return { state: "disconnected", label: `Can't reach the AI service: ${message}` };
  return { state: "error", label: `AI service: ${message}` };
}

export function useStudio(bridge: Bridge) {
  const toast = useToast();
  const [session, setSession] = useState<Session | null>(null);
  const [transport, setTransport] = useState<Transport | null>(null);
  const [service, setService] = useState<ServiceState>(UNKNOWN);

  const send = useCallback(async (command: Command): Promise<Reply> => {
    let reply: Reply;
    try {
      reply = await bridge.send(command);
    } catch (e) {
      toast({ kind: "error", message: `The plugin didn't answer: ${String(e)}` });
      return { ok: false, error: { code: "internal", message: String(e) }, requestId: null, session: null, catalog: null };
    }
    if (reply.session) setSession(reply.session);
    const err = reply.error;
    // A dismissed file chooser comes back as `cancelled`; it needs no toast.
    if (!reply.ok && err && err.code !== "cancelled") {
      toast({ kind: err.code === "unavailable" || err.code === "busy" ? "info" : "error", message: err.message });
    }
    return reply;
  }, [bridge, toast]);

  useEffect(() => {
    bridge.onEvent((event) => {
      switch (event.type) {
        case "session": setSession(event.session); break;
        case "transport": setTransport(event.transport); break;
        case "generationStarted": setService({ state: "connecting", label: "Writing with the AI service" }); break;
        case "partReady": setService({ state: "connected", label: "AI service connected" }); break;
        case "generationDone": setService({ state: "connected", label: "AI service connected" }); break;
        case "generationFailed":
          if (event.error.code === "cancelled") {
            setService((s) => (s.state === "connecting" ? UNKNOWN : s));
            toast({ message: "Generation cancelled" });
          } else {
            setService(serviceAfter(event.error.code, event.error.message));
            toast({ kind: "error", message: event.error.message });
          }
          break;
        case "notice": toast({ kind: event.level, message: event.message }); break;
      }
    });
    void send({ type: "hello", protocol: "flowstate.bridge.v0" });
  }, [bridge]);

  const gaps = useMemo(() => {
    const m = new Map<Feature, string>();
    for (const g of session?.unavailable ?? []) m.set(g.feature, g.reason);
    return m;
  }, [session?.unavailable]);

  /** Why this build can't do it, or null when it can. */
  const gap = useCallback((f: Feature) => gaps.get(f) ?? null, [gaps]);

  return { session, transport, service, send, gap };
}

export type Studio = ReturnType<typeof useStudio>;
