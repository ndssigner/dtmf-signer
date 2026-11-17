// Language choice. On the root page (which lists its languages in
// data-langs) it goes on to the language chosen before, or else the one
// the browser prefers, or English. Every language link remembers the
// choice. Storage may be blocked: then it just is not remembered.
export function pickLanguage(prefs, codes) {
  for (const p of prefs) if (codes.includes(p.toLowerCase())) return p.toLowerCase();
  for (const p of prefs) {
    const base = p.toLowerCase().split("-")[0];
    if (codes.includes(base)) return base;
  }
  return "en";
}

if (typeof document !== "undefined") {
  for (const a of document.querySelectorAll("a[data-lang]"))
    a.addEventListener("click", () => {
      try { localStorage.setItem("lang", a.dataset.lang); } catch { /* not remembered */ }
    });
  // The language dropdown closes on a click outside it or on Escape.
  const drop = document.querySelector("details.langs");
  if (drop) {
    document.addEventListener("click", (e) => { if (drop.open && !drop.contains(e.target)) drop.open = false; });
    document.addEventListener("keydown", (e) => {
      if (e.key === "Escape" && drop.open) { drop.open = false; drop.querySelector("summary").focus(); }
    });
  }
  const codes = (document.documentElement.dataset.langs || "").split(" ").filter(Boolean);
  if (codes.length) {
    let saved = null;
    try { saved = localStorage.getItem("lang"); } catch { /* none */ }
    const prefs = navigator.languages?.length ? navigator.languages : [navigator.language || ""];
    location.replace((codes.includes(saved) ? saved : pickLanguage(prefs, codes)) + "/");
  }
}
