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
const MARK = { done: "✅", doing: "🔧", planned: "⏳", na: "—" };

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

export function statusTable(status, tr) {
  const rows = (part) => status[part].map((s) => {
    const note = s.note ? escapeHtml(s.note) : "";
    const extra = s.link
      ? ` <a href="${escapeHtml(s.link)}" rel="noopener noreferrer">${note || escapeHtml(tr("status.link"))}</a>`
      : note ? " " + note : "";
    return `<li><span class="state ${s.state}" title="${escapeHtml(tr(`status.${s.state}`))}">${MARK[s.state]}</span> ${escapeHtml(tr(`status.${part}.${s.stage}`))}${extra}</li>`;
  }).join("\n");
  const legend = STATES.map((s) => `${MARK[s]} ${escapeHtml(tr(`status.${s}`))}`).join(" · ");
  return `<div class="status">
<div><h3>${escapeHtml(tr("status.hardware"))}</h3>
<ul>
${rows("hardware")}
</ul></div>
<div><h3>${escapeHtml(tr("status.firmware"))}</h3>
<ul>
${rows("firmware")}
</ul></div>
</div>
<p class="legend">${legend}</p>`;
}
