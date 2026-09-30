import type { ComponentChildren } from "preact";
import { createContext } from "preact";
import { useCallback, useContext, useEffect, useRef, useState } from "preact/hooks";
import { Button } from "./Button.tsx";
import { Icon } from "./Icon.tsx";

export type ToastKind = "info" | "success" | "warning" | "error";
export type ToastInput = { kind?: ToastKind; message: string; action?: { label: string; run: () => void } };
type Toast = ToastInput & { id: number; kind: ToastKind };

const icons: Record<ToastKind, string> = { info: "dot", success: "check", warning: "lightning", error: "x" };
const DISMISS_MS = 5000;

const Ctx = createContext<(t: ToastInput) => void>(() => {});

/** Shows a toast. Errors stay until dismissed; the rest go after 5 s (paused while hovered or focused). */
export const useToast = () => useContext(Ctx);

export function ToastProvider({ children }: { children: ComponentChildren }) {
  const [toasts, setToasts] = useState<Toast[]>([]);
  const next = useRef(1);
  const push = useCallback((t: ToastInput) => {
    setToasts((ts) => [...ts.slice(-3), { ...t, kind: t.kind ?? "info", id: next.current++ }]);
  }, []);
  const dismiss = (id: number) => setToasts((ts) => ts.filter((t) => t.id !== id));

  return (
    <Ctx.Provider value={push}>
      {children}
      <div class="fs-toasts" role="region" aria-label="Notifications">
        {toasts.map((t) => <ToastItem key={t.id} toast={t} onDismiss={() => dismiss(t.id)} />)}
      </div>
    </Ctx.Provider>
  );
}

function ToastItem({ toast, onDismiss }: { toast: Toast; onDismiss: () => void }) {
  const [paused, setPaused] = useState(false);
  useEffect(() => {
    if (toast.kind === "error" || paused) return;
    const t = setTimeout(onDismiss, DISMISS_MS);
    return () => clearTimeout(t);
  }, [paused]);

  return (
    <div
      class={`fs-toast fs-toast--${toast.kind}`}
      role={toast.kind === "error" ? "alert" : "status"}
      onPointerEnter={() => setPaused(true)}
      onPointerLeave={() => setPaused(false)}
      onFocusIn={() => setPaused(true)}
      onFocusOut={() => setPaused(false)}
    >
      <Icon name={icons[toast.kind]} size={12} class="fs-toast__icon" />
      <p class="fs-toast__message">{toast.message}</p>
      {toast.action && (
        <Button variant="ghost" size="sm" onClick={() => { toast.action!.run(); onDismiss(); }}>{toast.action.label}</Button>
      )}
      <Button variant="ghost" size="sm" icon="x" label="Dismiss notification" onClick={onDismiss} />
    </div>
  );
}
