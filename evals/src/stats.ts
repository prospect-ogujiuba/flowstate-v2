// Small statistics helpers for the A/B scoring.

/** Deterministic 32-bit PRNG (mulberry32). */
export function mulberry32(seed: number): () => number {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

/** FNV-1a hash of a string to a 32-bit seed. */
export function hashSeed(text: string): number {
  let h = 2166136261;
  for (let i = 0; i < text.length; i++) h = Math.imul(h ^ text.charCodeAt(i), 16777619) >>> 0;
  return h >>> 0;
}

/** P(X = k) for X ~ Binomial(n, 1/2), computed in log space. */
function halfBinomialPmf(n: number, k: number): number {
  let logC = 0;
  for (let i = 1; i <= k; i++) logC += Math.log(n - k + i) - Math.log(i);
  return Math.exp(logC - n * Math.LN2);
}

/**
 * Exact sign test for `wins` successes out of `n` non-tie trials under H0: p = 0.5.
 * Returns the two-sided p-value (doubled smaller tail, capped at 1) and the one-sided
 * p-value for "wins is larger than chance" (P(X >= wins)).
 */
export function signTest(wins: number, n: number): { twoSided: number; oneSidedGreater: number } {
  if (!Number.isInteger(wins) || !Number.isInteger(n) || n < 0 || wins < 0 || wins > n) throw new Error(`bad sign test input ${wins}/${n}`);
  if (n === 0) return { twoSided: 1, oneSidedGreater: 1 };
  let upper = 0;
  for (let k = wins; k <= n; k++) upper += halfBinomialPmf(n, k);
  let lower = 0;
  for (let k = 0; k <= wins; k++) lower += halfBinomialPmf(n, k);
  return { twoSided: Math.min(1, 2 * Math.min(upper, lower)), oneSidedGreater: Math.min(1, upper) };
}
