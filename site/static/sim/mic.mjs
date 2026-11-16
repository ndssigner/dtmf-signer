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

// Starts listening; resolves to { stop() }. Rejects with "mic:unsupported"
// (no getUserMedia) or "mic:denied" (no permission, no microphone).
export async function startMic({ getUserMedia, AudioContext, AudioWorkletNode, onSamples, onLevel, workletUrl }) {
  if (typeof getUserMedia !== "function") throw new Error("mic:unsupported");
  let stream;
  try {
    stream = await getUserMedia({ audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false } });
  } catch {
    throw new Error("mic:denied");
  }
  const ctx = new AudioContext();
  const stop = () => { for (const t of stream.getTracks()) t.stop(); ctx.close(); };
  try {
    await ctx.audioWorklet.addModule(workletUrl ?? new URL("./mic-worklet.js", import.meta.url).href);
    const src = ctx.createMediaStreamSource(stream);
    const node = new AudioWorkletNode(ctx, "dsw-mic");
    const state = { pos: 0, last: 0 };
    node.port.onmessage = (e) => {
      const out = resampleTo16k(e.data, ctx.sampleRate, state);
      onSamples(out);
      onLevel?.(levelDbfs(out));
    };
    src.connect(node);
    node.connect(ctx.destination);   // pulled by the graph; it outputs silence
  } catch (err) {
    stop();
    throw new Error("mic:unsupported");
  }
  return { stop };
}
