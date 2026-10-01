/** On/off switch with a visible label, which is also its accessible name. */
export function Toggle({ label, checked, onChange, disabled }: {
  label: string;
  checked: boolean;
  onChange: (checked: boolean) => void;
  disabled?: boolean;
}) {
  return (
    <button
      type="button"
      role="switch"
      aria-checked={checked}
      disabled={disabled}
      class="fs-toggle"
      onClick={() => onChange(!checked)}
    >
      <span class="fs-toggle__track" aria-hidden="true"><span class="fs-toggle__thumb" /></span>
      <span class="fs-toggle__label">{label}</span>
    </button>
  );
}
