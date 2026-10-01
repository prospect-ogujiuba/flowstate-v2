// Icons: v1's SVGs (../flowstate/assets) with fills turned into currentColor, plus a few drawn for
// v2 in the same 16 px style. They are decorative; the control that holds one carries the label.

const files = import.meta.glob<string>(["../assets/icons/*.svg", "../assets/status/*.svg"], {
  query: "?raw",
  import: "default",
  eager: true,
});

const svgs: Record<string, string> = {};
for (const [path, svg] of Object.entries(files)) svgs[path.replace(/^.*\/(.*)\.svg$/, "$1")] = svg;

export const iconNames = Object.keys(svgs).sort();

export function Icon({ name, size = 16, class: cls }: { name: string; size?: number; class?: string }) {
  const svg = svgs[name];
  if (svg === undefined) throw new Error(`unknown icon: ${name}`);
  return (
    <span
      class={cls ? `fs-icon ${cls}` : "fs-icon"}
      style={{ "--icon-size": `${size}px` }}
      aria-hidden="true"
      dangerouslySetInnerHTML={{ __html: svg }}
    />
  );
}
