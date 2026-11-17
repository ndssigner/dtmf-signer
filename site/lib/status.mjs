// Where a build stands: hardware/<build>/status.json, one fixed list of
// stages for the hardware and one for the firmware, the same for every
// build so that builds can be compared. Each stage has a state, and may
// have a short note and an http(s) link.
import { escapeHtml } from "./markdown.mjs";

export const STAGES = {
  hardware: ["design", "simulation", "breadboard", "perfboard", "pcb"],
  firmware: ["core", "port", "simulator", "release", "review"],
};
export const STATES = ["done", "doing", "planned", "na"];

export function parseStatus(text) {
  const status = JSON.parse(text);
  for (const [part, stages] of Object.entries(STAGES)) {
    const got = (status[part] ?? []).map((s) => s.stage);
    if (got.join() !== stages.join()) throw new Error(`${part}: stages must be ${stages.join(", ")}, in that order`);
    for (const s of status[part]) {
      if (!STATES.includes(s.state)) throw new Error(`${part}.${s.stage}: unknown state ${s.state}`);
      if (s.link !== undefined && !/^https?:\/\/[^\s"<>]+$/.test(s.link))
        throw new Error(`${part}.${s.stage}: link must be http(s)`);
    }
  }
  return status;
}

// The class each state gets on the page: done, wip (in progress), planned, na.
const CLASS = { done: "done", doing: "wip", planned: "planned", na: "na" };

// Each part (hardware, firmware) as a sequence of stages, in order: a dot
// per stage coloured by its state (the state also said in words, for screen
// readers and on hover), its label, its note and its link.
export function renderStatusSequence(status, tr) {
  const e = (k) => escapeHtml(tr(k));
  const part = (name) => {
    const items = status[name].map((s) => {
      const note = s.note ? escapeHtml(s.note) : "";
      const extra = s.link
        ? ` <a href="${escapeHtml(s.link)}" rel="noopener noreferrer">${note || e("status.link")}</a>`
        : note ? ` <span class="note">${note}</span>` : "";
      return `<li class="${CLASS[s.state]}"><span class="dot" title="${e(`status.${s.state}`)}" aria-hidden="true"></span><b>${e(`status.${name}.${s.stage}`)}</b> <span class="visually-hidden">(${e(`status.${s.state}`)})</span>${extra}</li>`;
    }).join("\n");
    return `<section class="stage-part">
<h2 id="${name}">${e(`status.${name}`)}</h2>
<ol class="stages">
${items}
</ol>
</section>`;
  };
  const key = STATES.map((s) => `<span class="${CLASS[s]}"><span class="dot" aria-hidden="true"></span>${e(`status.${s}`)}</span>`).join("\n");
  return `${part("hardware")}
${part("firmware")}
<p class="stage-key">${key}</p>`;
}

// A part's stages as a row of dots, for the home page: the same states as
// the sequence, each dot with its meaning as a tooltip and as hidden text
// (colour is never the only signal).
export function renderStageDots(status, part, tr) {
  const items = status[part].map((s) => {
    const label = escapeHtml(`${tr(`status.${part}.${s.stage}`)}: ${tr(`status.${s.state}`)}`);
    return `<li class="${CLASS[s.state]}"><span class="dot" title="${label}" aria-hidden="true"></span><span class="visually-hidden">${label}</span></li>`;
  }).join("\n");
  return `<ol class="dots" aria-label="${escapeHtml(tr(`status.${part}`))}">\n${items}\n</ol>`;
}
