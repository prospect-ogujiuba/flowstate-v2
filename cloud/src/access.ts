// Who may use the hosted service, and how much (P1-13). Each tester has a token. The service keeps only its
// SHA-256 in a tokens file (`<tester> <sha256 hex>` per line), so the file grants nothing if it leaks.
// Limits are per tester: streams open at once, and requests in the last hour.
import { createHash, randomBytes } from "node:crypto";
import { FlowstateError } from "./errors.ts";

export interface Limits {
  /** Plan or edit streams one tester may have open at once. */
  concurrent: number;
  /** Requests one tester may start in any 60 minutes. */
  perHour: number;
}

export const DEFAULT_LIMITS: Limits = { concurrent: 4, perHour: 120 };
const HOUR_MS = 60 * 60 * 1000;

export const hashToken = (token: string) => createHash("sha256").update(token, "utf8").digest("hex");

/** A new tester token: "fst_" and 32 random bytes, base64url. */
export const newToken = () => `fst_${randomBytes(32).toString("base64url")}`;

/** Tester per token hash, from a tokens file. Blank lines and `#` comments are skipped. */
export function parseTokens(text: string): Map<string, string> {
  const testers = new Map<string, string>();
  text.split(/\r?\n/).forEach((raw, i) => {
    const line = raw.replace(/#.*/, "").trim();
    if (!line) return;
    const [name, hash, ...rest] = line.split(/\s+/);
    if (!name || !hash || rest.length || !/^[0-9a-f]{64}$/.test(hash) || !/^[\w.-]{1,64}$/.test(name))
      throw new Error(`tokens file line ${i + 1}: expected "<tester> <sha256 hex>"`);
    if (testers.has(hash)) throw new Error(`tokens file line ${i + 1}: the same token is listed twice`);
    testers.set(hash, name);
  });
  return testers;
}

export class Access {
  private readonly open = new Map<string, number>();
  private readonly started = new Map<string, number[]>();

  constructor(
    private readonly testers: Map<string, string>,
    readonly limits: Limits = DEFAULT_LIMITS,
    private readonly now: () => number = Date.now,
  ) {}

  /** The tester for an `Authorization: Bearer <token>` header. Throws `unauthorized`. */
  authenticate(header: string | undefined): string {
    const token = /^Bearer\s+(\S+)$/i.exec(header?.trim() ?? "")?.[1];
    if (!token) throw new FlowstateError("unauthorized", "This service needs a tester token (Authorization: Bearer ...)");
    const tester = this.testers.get(hashToken(token));
    if (!tester) throw new FlowstateError("unauthorized", "That tester token isn't valid");
    return tester;
  }

  /**
   * Takes a stream slot for the tester, counted against the hourly limit. Returns the release, which is
   * safe to call more than once. Throws `rate_limited` with the seconds to wait in `retryAfterS`.
   */
  admit(tester: string): () => void {
    const now = this.now();
    const recent = (this.started.get(tester) ?? []).filter((t) => now - t < HOUR_MS);
    if (recent.length >= this.limits.perHour) {
      const wait = Math.ceil((recent[0]! + HOUR_MS - now) / 1000);
      throw new RateLimited(`${this.limits.perHour} requests an hour is the limit; try again in ${Math.ceil(wait / 60)} min`, wait);
    }
    const open = this.open.get(tester) ?? 0;
    if (open >= this.limits.concurrent)
      throw new RateLimited(`${this.limits.concurrent} generations at once is the limit; wait for one to finish`, 5);
    recent.push(now);
    this.started.set(tester, recent);
    this.open.set(tester, open + 1);
    let released = false;
    return () => {
      if (released) return;
      released = true;
      this.open.set(tester, (this.open.get(tester) ?? 1) - 1);
    };
  }
}

export class RateLimited extends FlowstateError {
  constructor(message: string, readonly retryAfterS: number) {
    super("rate_limited", message);
  }
}
