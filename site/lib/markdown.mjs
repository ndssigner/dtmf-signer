// A small Markdown subset for the site's prose, with no dependencies:
// headings (#, ##, ###), paragraphs, "- " lists, **bold**, *italic*,
// `code` and [links](url). Everything else is text: HTML is escaped, never
// passed through, and a link whose URL is not plainly safe stays text.

export function escapeHtml(s) {
  return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;").replace(/'/g, "&#39;");
}

export function slug(s) {
  return s.normalize("NFD").replace(/[̀-ͯ]/g, "").toLowerCase()
    .replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "");
}

// http(s), an anchor, or a relative path (no scheme: no ":" before any "/").
function safeUrl(url) {
  if (/^https?:\/\//i.test(url)) return true;
  const colon = url.indexOf(":");
  const slash = url.indexOf("/");
  return colon < 0 || (slash >= 0 && slash < colon);
}

function inline(text) {
  const codes = [];
  let s = escapeHtml(text).replace(/`([^`]+)`/g, (_, c) => {
    codes.push(`<code>${c}</code>`);
    return `\u0000${codes.length - 1}\u0000`;
  });
  s = s.replace(/\[([^\]]+)\]\(([^)\s]+)\)/g, (all, label, url) => {
    if (!safeUrl(url)) return all;
    const rel = /^https?:/i.test(url) ? ' rel="noopener noreferrer"' : "";
    return `<a href="${url}"${rel}>${label}</a>`;
  });
  s = s.replace(/\*\*([^*]+)\*\*/g, "<strong>$1</strong>")
    .replace(/\*([^*]+)\*/g, "<em>$1</em>");
  return s.replace(/\u0000(\d+)\u0000/g, (_, i) => codes[Number(i)]);
}

export function renderMarkdown(src) {
  const out = [];
  for (const block of src.replace(/\r\n/g, "\n").split(/\n\s*\n/)) {
    const lines = block.split("\n").filter((l) => l.trim() !== "");
    if (lines.length === 0) continue;
    const h = /^(#{1,3})\s+(.*)$/.exec(lines[0]);
    if (h && lines.length === 1) {
      const n = h[1].length;
      out.push(`<h${n} id="${slug(h[2])}">${inline(h[2])}</h${n}>`);
    } else if (lines.every((l) => l.startsWith("- "))) {
      out.push("<ul>" + lines.map((l) => `<li>${inline(l.slice(2))}</li>`).join("") + "</ul>");
    } else {
      out.push(`<p>${inline(lines.join("\n"))}</p>`);
    }
  }
  return out.join("\n");
}
