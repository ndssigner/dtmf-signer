// The home page's keypad: press a key and hear the two tones a telephone
// sends for it, one from the low group (the key's row) and one from the
// high group (its column). The pure parts are exported and tested in Node;
// mount() is the only part that touches the page and WebAudio.
export const KEYS = ["123A", "456B", "789C", "*0#D"];
export const LOW = [697, 770, 852, 941];
export const HIGH = [1209, 1336, 1477, 1633];

// Row and column of a key in the grid.
export function position(key) {
  for (let r = 0; r < KEYS.length; r++) {
    const c = KEYS[r].indexOf(key);
    if (c >= 0 && key.length === 1) return { row: r, col: c };
  }
  throw new Error(`not a DTMF key: ${JSON.stringify(key)}`);
}

// The two frequencies of a key: [low, high].
export function freqs(key) {
  const { row, col } = position(key);
  return [LOW[row], HIGH[col]];
}

// SVG polyline points (a 400 x 54 box, the zero line at 27) for each tone
// and for their sum, over `span` seconds.
export function wavePoints(low, high, n = 200, span = 0.012) {
  const pts = (fn) => Array.from({ length: n + 1 }, (_, i) =>
    `${+(i * 400 / n).toFixed(2)},${+(27 - fn(i / n * span) * 11).toFixed(2)}`).join(" ");
  const sine = (f) => (t) => Math.sin(2 * Math.PI * f * t);
  return { low: pts(sine(low)), high: pts(sine(high)), sum: pts((t) => (sine(low)(t) + sine(high)(t)) / 1.4) };
}

// The caption's text: {key}, {low} and {high} filled in.
export const caption = (template, key) => {
  const [low, high] = freqs(key);
  return template.replace("{key}", key).replace("{low}", low).replace("{high}", high);
};

// Both tones, 200 ms, 10 ms ramps in and out, gain 0.18; ctx is an AudioContext.
export function play(ctx, low, high) {
  const t = ctx.currentTime, gain = ctx.createGain();
  gain.gain.setValueAtTime(0, t);
  gain.gain.linearRampToValueAtTime(0.18, t + 0.01);
  gain.gain.setValueAtTime(0.18, t + 0.19);
  gain.gain.linearRampToValueAtTime(0, t + 0.2);
  gain.connect(ctx.destination);
  for (const f of [low, high]) {
    const osc = ctx.createOscillator();
    osc.frequency.value = f;
    osc.connect(gain);
    osc.start(t);
    osc.stop(t + 0.21);
  }
}

const SVG = "http://www.w3.org/2000/svg";

// Builds the grid, the wave and the caption inside root, and plays on
// press. The texts come from root's data-hint and data-caption.
export function mount(root) {
  const doc = root.ownerDocument;
  const el = (tag, cls, text) => {
    const e = doc.createElement(tag);
    if (cls) e.className = cls;
    if (text !== undefined) e.textContent = text;
    return e;
  };
  const grid = el("div", "grid");
  grid.appendChild(el("span"));
  HIGH.forEach((f, c) => { const s = el("span", "hz hz-col", String(f)); s.dataset.c = c; grid.appendChild(s); });
  KEYS.forEach((row, r) => {
    const s = el("span", "hz hz-row", `${LOW[r]} Hz`);
    s.dataset.r = r;
    grid.appendChild(s);
    [...row].forEach((k, c) => {
      const b = el("button", "ABCD".includes(k) ? "dim" : "", k);
      b.type = "button";
      b.dataset.r = r; b.dataset.c = c;
      grid.appendChild(b);
    });
  });
  const wave = doc.createElementNS(SVG, "svg");
  wave.setAttribute("class", "wave");
  wave.setAttribute("viewBox", "0 0 400 54");
  wave.setAttribute("preserveAspectRatio", "none");
  wave.setAttribute("aria-hidden", "true");
  const cap = el("p", "cap", root.dataset.hint ?? "");
  cap.setAttribute("aria-live", "polite");
  root.append(grid, wave, cap);

  const draw = (low, high) => {
    const p = wavePoints(low, high);
    wave.replaceChildren(...[["lo", p.low], ["hi", p.high], ["sum", p.sum]].map(([cls, points]) => {
      const line = doc.createElementNS(SVG, "polyline");
      line.setAttribute("class", cls);
      line.setAttribute("points", points);
      return line;
    }));
  };
  draw(LOW[0], HIGH[0]);

  let ctx = null, timer = null;
  grid.addEventListener("click", (ev) => {
    const b = ev.target.closest?.("button");
    if (!b) return;
    const r = Number(b.dataset.r), c = Number(b.dataset.c), key = b.textContent;
    for (const x of grid.querySelectorAll(".on, .cross")) x.classList.remove("on", "cross");
    // A neutral cross: the key's whole row and column.
    for (const x of grid.querySelectorAll(`button[data-r="${r}"], button[data-c="${c}"]`)) x.classList.add("cross");
    b.classList.add("on");
    grid.querySelector(`.hz-row[data-r="${r}"]`).classList.add("on");
    grid.querySelector(`.hz-col[data-c="${c}"]`).classList.add("on");
    draw(LOW[r], HIGH[c]);
    cap.textContent = caption(root.dataset.caption ?? "{key}: {low} Hz + {high} Hz", key);
    try {
      ctx ??= new (globalThis.AudioContext ?? globalThis.webkitAudioContext)();
      ctx.resume?.();
      play(ctx, LOW[r], HIGH[c]);
    } catch { /* no sound here: the cross and the caption still work */ }
    clearTimeout(timer);
    timer = setTimeout(() => b.classList.remove("on"), 220);
  });
}

if (typeof document !== "undefined") {
  const root = document.querySelector("[data-keypad]");
  if (root) mount(root);
}
