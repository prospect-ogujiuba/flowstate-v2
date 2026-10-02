import type { ComponentChildren } from "preact";
import { useId, useState } from "preact/hooks";
import { Icon } from "./Icon.tsx";

type Common = {
  label: string;
  hint?: string;
  error?: string;
  /** Keeps the label for screen readers only (e.g. the prompt bar). */
  hideLabel?: boolean;
  class?: string;
};

function Frame({ id, label, hint, error, hideLabel, class: cls, children }: Common & { id: string; children: ComponentChildren }) {
  return (
    <div class={cls ? `fs-field ${cls}` : "fs-field"}>
      <label for={id} class={hideLabel ? "sr-only" : "fs-field__label"}>{label}</label>
      {children}
      {error ? (
        <p id={`${id}-msg`} class="fs-field__error">{error}</p>
      ) : hint ? (
        <p id={`${id}-msg`} class="fs-field__hint">{hint}</p>
      ) : null}
    </div>
  );
}

const describedBy = (id: string, p: Common) => (p.error || p.hint ? `${id}-msg` : undefined);

export function TextField(p: Common & {
  value: string;
  onInput: (value: string) => void;
  placeholder?: string;
  type?: "text" | "email" | "password" | "search";
  inputMode?: "text" | "decimal" | "numeric";
  disabled?: boolean;
  readOnly?: boolean;
}) {
  const id = useId();
  return (
    <Frame {...p} id={id}>
      <input
        id={id}
        class="fs-input"
        type={p.type ?? "text"}
        inputMode={p.inputMode}
        value={p.value}
        placeholder={p.placeholder}
        disabled={p.disabled}
        readOnly={p.readOnly}
        spellcheck={false}
        aria-invalid={p.error ? true : undefined}
        aria-describedby={describedBy(id, p)}
        onInput={(e) => p.onInput(e.currentTarget.value)}
      />
    </Frame>
  );
}

export function TextArea(p: Common & { value: string; onInput: (value: string) => void; placeholder?: string; rows?: number }) {
  const id = useId();
  return (
    <Frame {...p} id={id}>
      <textarea
        id={id}
        class="fs-input fs-textarea"
        rows={p.rows ?? 4}
        value={p.value}
        placeholder={p.placeholder}
        aria-invalid={p.error ? true : undefined}
        aria-describedby={describedBy(id, p)}
        onInput={(e) => p.onInput(e.currentTarget.value)}
      />
    </Frame>
  );
}

export function Select<T extends string>(p: Common & {
  value: T;
  options: { value: T; label: string }[];
  onChange: (value: T) => void;
  disabled?: boolean;
}) {
  const id = useId();
  return (
    <Frame {...p} id={id}>
      <div class="fs-select">
        <select
          id={id}
          class="fs-input"
          value={p.value}
          disabled={p.disabled}
          aria-describedby={describedBy(id, p)}
          onChange={(e) => p.onChange(e.currentTarget.value as T)}
        >
          {p.options.map((o) => <option key={o.value} value={o.value}>{o.label}</option>)}
        </select>
        <Icon name="chevron" size={10} class="fs-select__chevron" />
      </div>
    </Frame>
  );
}

/** A key or password: masked, with a show/hide toggle. */
export function SecretField(p: Common & { value: string; onInput: (value: string) => void; placeholder?: string }) {
  const id = useId();
  const [shown, setShown] = useState(false);
  return (
    <Frame {...p} id={id}>
      <div class="fs-secret">
        <input
          id={id}
          class="fs-input"
          type={shown ? "text" : "password"}
          value={p.value}
          placeholder={p.placeholder}
          autocomplete="off"
          spellcheck={false}
          aria-describedby={describedBy(id, p)}
          onInput={(e) => p.onInput(e.currentTarget.value)}
        />
        <button
          type="button"
          class="fs-secret__toggle"
          aria-label={`Show ${p.label}`}
          aria-pressed={shown}
          title={shown ? "Hide" : "Show"}
          onClick={() => setShown(!shown)}
        >
          <Icon name="eye" size={14} />
        </button>
      </div>
    </Frame>
  );
}
