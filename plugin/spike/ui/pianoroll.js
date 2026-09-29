// Canvas piano roll: one lane per part, bars/beats grid, moving playhead.
// The static layer is cached in an offscreen canvas; the playhead is drawn per status event.

const ROLE_COLOURS = { chords: "--chords", bass: "--bass", drums: "--drums" };

function cssVar(name) {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim() || "#888";
}

export class PianoRoll {
  constructor(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext("2d");
    this.clip = null;
    this.ppq = 0;
    this.playing = false;
    this.layer = document.createElement("canvas");
    this.dpr = 1;
    new ResizeObserver(() => this.resize()).observe(canvas);
    this.resize();
  }

  setClip(clip) {
    this.clip = clip;
    this.renderLayer();
    this.draw();
  }

  setPlayhead(ppq, playing) {
    this.ppq = ppq;
    this.playing = playing;
    this.draw();
  }

  resize() {
    const r = this.canvas.getBoundingClientRect();
    this.dpr = window.devicePixelRatio || 1; // HiDPI: back the canvas at device pixels
    const w = Math.max(1, Math.round(r.width * this.dpr));
    const h = Math.max(1, Math.round(r.height * this.dpr));
    if (this.canvas.width !== w || this.canvas.height !== h) {
      this.canvas.width = w;
      this.canvas.height = h;
      this.layer.width = w;
      this.layer.height = h;
      this.renderLayer();
    }
    this.draw();
  }

  lanes() {
    const clip = this.clip;
    const parts = clip.parts.filter((p) => p.notes.length > 0);
    const out = [];
    let totalRows = 0;
    for (const p of parts) {
      let lo = 127, hi = 0;
      for (const n of p.notes) { lo = Math.min(lo, n[2]); hi = Math.max(hi, n[2]); }
      const rows = Math.max(6, hi - lo + 3);
      out.push({ part: p, lo: lo - 1, rows });
      totalRows += rows;
    }
    return { lanes: out, totalRows };
  }

  renderLayer() {
    const g = this.layer.getContext("2d");
    const W = this.layer.width, H = this.layer.height, d = this.dpr;
    g.clearRect(0, 0, W, H);
    if (!this.clip) return;

    const clip = this.clip;
    const totalTicks = clip.bars * clip.ticksPerBar;
    const beatTicks = clip.ppq * 4 / clip.meter[1];
    const x = (t) => (t / totalTicks) * W;

    // grid
    for (let t = 0; t <= totalTicks; t += beatTicks) {
      const isBar = t % clip.ticksPerBar === 0;
      g.fillStyle = isBar ? "#343a46" : "#1e222a";
      g.fillRect(Math.round(x(t)), 0, isBar ? 2 * d : d, H);
      if (isBar && t < totalTicks) {
        g.fillStyle = "#8b93a1";
        g.font = `${11 * d}px system-ui, sans-serif`;
        g.fillText(String(t / clip.ticksPerBar + 1), x(t) + 4 * d, 12 * d);
      }
    }

    const { lanes, totalRows } = this.lanes();
    const top = 16 * d;
    const rowH = (H - top) / Math.max(1, totalRows);
    let y0 = top;

    for (const lane of lanes) {
      const laneH = lane.rows * rowH;
      g.fillStyle = "#2c313b";
      g.fillRect(0, Math.round(y0), W, d);
      const colour = cssVar(ROLE_COLOURS[lane.part.role] || "--other");
      g.fillStyle = colour;
      for (const [tick, dur, pitch, vel] of lane.part.notes) {
        const row = lane.rows - 1 - (pitch - lane.lo);
        const ny = y0 + row * rowH;
        const nw = Math.max(2 * d, x(Math.min(tick + dur, totalTicks)) - x(tick) - d);
        g.globalAlpha = 0.45 + 0.55 * (vel / 127);
        g.fillRect(x(tick), ny + 1, nw, Math.max(1, rowH - 2));
      }
      g.globalAlpha = 1;
      g.fillStyle = "#8b93a1";
      g.font = `${10 * d}px system-ui, sans-serif`;
      g.fillText(`${lane.part.name || lane.part.id} · ch ${lane.part.channel}`, 4 * d, y0 + 12 * d);
      y0 += laneH;
    }
  }

  draw() {
    const g = this.ctx;
    const W = this.canvas.width, H = this.canvas.height;
    g.clearRect(0, 0, W, H);
    g.drawImage(this.layer, 0, 0);
    if (!this.clip || this.ppq < 0) return;

    const len = this.clip.lengthPpq;
    const local = ((this.ppq % len) + len) % len;
    const px = (local / len) * W;
    g.fillStyle = this.playing ? cssVar("--play") : "#8b93a1";
    g.fillRect(Math.round(px), 0, Math.max(1, Math.round(2 * this.dpr)), H);
  }
}
