import type { ComponentChildren } from "preact";
import { useId } from "preact/hooks";
import { Button } from "./Button.tsx";
import { Icon } from "./Icon.tsx";

/**
 * v1's chat box: a prompt field, a row of settings below it, and a round submit button. Enter
 * submits; Shift+Enter adds a line.
 */
export function PromptBox({ value, onInput, onSubmit, placeholder, label = "Describe or ask", busy, tools, settings }: {
  value: string;
  onInput: (value: string) => void;
  onSubmit: (value: string) => void;
  placeholder?: string;
  label?: string;
  busy?: boolean;
  /** Buttons inside the field, on the right (v1's audio capture). */
  tools?: ComponentChildren;
  /** The row under the field (provider, effort, mode). */
  settings?: ComponentChildren;
}) {
  const id = useId();
  const submit = () => {
    const text = value.trim();
    if (text && !busy) onSubmit(text);
  };
  return (
    <form class="fs-prompt" onSubmit={(e) => { e.preventDefault(); submit(); }}>
      <div class="fs-prompt__field">
        <label for={id} class="sr-only">{label}</label>
        <textarea
          id={id}
          class="fs-prompt__input"
          rows={1}
          value={value}
          placeholder={placeholder}
          spellcheck={false}
          onInput={(e) => onInput(e.currentTarget.value)}
          onKeyDown={(e) => {
            if (e.key === "Enter" && !e.shiftKey) { e.preventDefault(); submit(); }
          }}
        />
        {tools && <div class="fs-prompt__tools">{tools}</div>}
      </div>
      <div class="fs-prompt__row">
        <div class="fs-prompt__settings">{settings}</div>
        <Button variant="primary" icon="arrow" label={busy ? "Working" : "Send"} class="fs-prompt__send" type="submit" disabled={busy || !value.trim()} />
      </div>
    </form>
  );
}

/** A settings trigger in the prompt row (v1's "Anthropic · Claude Opus", "High · Normal", "Idea"). */
export function PromptSetting({ icon, label, value, onClick }: { icon: ComponentChildren; label: string; value: string; onClick?: () => void }) {
  return (
    <button type="button" class="fs-prompt__setting" aria-label={`${label}: ${value}`} onClick={onClick}>
      {icon}
      <span aria-hidden="true">{value}</span>
      <Icon name="caret" size={8} class="fs-prompt__caret" />
    </button>
  );
}
