/** Reads a design token for canvas drawing, which can't use CSS variables directly. */
export const cssVar = (el: Element, name: string): string => getComputedStyle(el).getPropertyValue(name).trim();
