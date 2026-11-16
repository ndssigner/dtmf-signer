// The microphone of this computer as the signer's line-in: captured with an
// AudioWorklet (from this site, no other origin), resampled to 16 kHz.
// Only for trying it out: the build is meant for an audio cable.

export const MIC_ACTIVE_DBFS = -50;   // above this, the line counts as active

// Linear interpolation from `rate` to 16 kHz, continuous across blocks:
// state.pos is where the next output sample falls, counted from the start of
// the next block (-1 is the previous block's last sample, kept in state.last).
export function resampleTo16k(input, rate, state) {
  const step = rate / 16000;
  const n = input.length;
  const out = [];
  let pos = state.pos;
  while (pos <= n - 1) {
    const i = Math.floor(pos), f = pos - i;
    const a = i < 0 ? state.last : input[i];
    const b = i + 1 < n ? input[i + 1] : a;
    const v = a + (b - a) * f;
    out.push(Math.max(-32768, Math.min(32767, Math.round(v * 32767))));
    pos += step;
  }
  if (n) { state.pos = pos - n; state.last = input[n - 1]; }
  return Int16Array.from(out);
}

export function levelDbfs(samples) {
  if (!samples.length) return -100;
  let sum = 0;
  for (const s of samples) sum += s * s;
  const rms = Math.sqrt(sum / samples.length) / 32768;
  return rms > 0 ? Math.max(-100, 20 * Math.log10(rms)) : -100;
}

export const MIC_LOWPASS_HZ = 7000;   // before 16 kHz: nothing above 8 kHz folds into the DTMF band

// Starts listening; resolves to { stop() }. Rejects with "mic:unsupported"
// (no getUserMedia, or the audio cannot start) or "mic:denied" (no
// permission, no microphone). Pass an audioContext made in the click itself
// (browsers start a context made later suspended), or an AudioContext class.
export async function startMic({ getUserMedia, audioContext, AudioContext, AudioWorkletNode, onSamples, onLevel, workletUrl }) {
  const ctx = audioContext ?? (AudioContext ? new AudioContext() : null);
  const closeCtx = () => { try { ctx?.close(); } catch { /* already closed */ } };
  if (typeof getUserMedia !== "function" || !ctx) { closeCtx(); throw new Error("mic:unsupported"); }
  let stream;
  try {
    stream = await getUserMedia({ audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false } });
  } catch {
    closeCtx();
    throw new Error("mic:denied");
  }
  let node = null, live = true;
  const stop = () => {
    live = false;
    for (const t of stream.getTracks()) t.stop();
    if (node) { node.port.onmessage = null; node.disconnect?.(); }
    closeCtx();
  };
  try {
    await ctx.resume();
    await ctx.audioWorklet.addModule(workletUrl ?? new URL("./mic-worklet.js", import.meta.url).href);
    const src = ctx.createMediaStreamSource(stream);
    const lowpass = ctx.createBiquadFilter();
    lowpass.type = "lowpass";
    lowpass.frequency.value = MIC_LOWPASS_HZ;
    node = new AudioWorkletNode(ctx, "dsw-mic");
    const state = { pos: 0, last: 0 };
    node.port.onmessage = (e) => {
      if (!live) return;
      const out = resampleTo16k(e.data, ctx.sampleRate, state);
      onSamples(out);
      onLevel?.(levelDbfs(out));
    };
    src.connect(lowpass);
    lowpass.connect(node);
    node.connect(ctx.destination);   // pulled by the graph; it outputs silence
  } catch {
    stop();
    throw new Error("mic:unsupported");
  }
  return { stop };
}

// The Listen/Stop switch: one start at a time (a double click waits for the
// same one), and a cancel while starting stops the microphone as soon as it
// is up, so no capture can be left running unseen.
export class MicSwitch {
  constructor(start) { this.start = start; this.mic = null; this.pending = null; this.token = 0; }
  get on() { return Boolean(this.mic); }
  get starting() { return Boolean(this.pending); }
  toggle() {
    if (this.pending) return this.pending;
    if (this.mic) { this.mic.stop(); this.mic = null; return Promise.resolve(); }
    const token = ++this.token;
    const p = this.start().then((m) => {
      if (token !== this.token) m.stop();
      else this.mic = m;
    }).finally(() => { if (this.pending === p) this.pending = null; });
    this.pending = p;
    return p;
  }
  cancel() {
    this.token++;
    this.pending = null;
    if (this.mic) { this.mic.stop(); this.mic = null; }
  }
}
