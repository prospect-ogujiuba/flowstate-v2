import { createContext } from "preact";
import { useContext } from "preact/hooks";

export type ToastKind = "info" | "success" | "warning" | "error";
export type ToastInput = { kind?: ToastKind; message: string; action?: { label: string; run: () => void } };

export const ToastContext = createContext<(t: ToastInput) => void>(() => {});

/** Shows a toast. Errors stay until dismissed; the rest go after 5 s (paused while hovered or focused). */
export const useToast = () => useContext(ToastContext);
