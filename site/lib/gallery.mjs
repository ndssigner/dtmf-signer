// A build's gallery: photos and videos of real builds. Videos are links to
// where they are published, never embedded (no other origin is loaded, and
// nobody is tracked for visiting). Photos are served from the site, and a
// photo that still carries metadata (camera, place, date) is refused.
import { basename } from "node:path";
import { escapeHtml } from "./markdown.mjs";

// What metadata a JPEG or PNG still carries, or null if none.
export function photoMetadata(bytes) {
  const b = bytes;
  if (b[0] === 0xff && b[1] === 0xd8) {
    for (let i = 2; i + 4 <= b.length && b[i] === 0xff;) {
      const m = b[i + 1];
      if (m === 0xd9 || m === 0xda) break;
      const len = (b[i + 2] << 8) | b[i + 3];
      if (m === 0xe1) return "EXIF/XMP (APP1)";
      if (m === 0xed) return "IPTC (APP13)";
      if (m === 0xfe) return "comment (COM)";
      i += 2 + len;
    }
    return null;
  }
  if (b[0] === 0x89 && b[1] === 0x50) {
    for (let i = 8; i + 8 <= b.length;) {
      const len = ((b[i] << 24) | (b[i + 1] << 16) | (b[i + 2] << 8) | b[i + 3]) >>> 0;
      const type = String.fromCharCode(b[i + 4], b[i + 5], b[i + 6], b[i + 7]);
      if (["eXIf", "tEXt", "iTXt", "zTXt", "tIME"].includes(type)) return `${type} chunk`;
      if (type === "IEND") break;
      i += 12 + len;
    }
    return null;
  }
  return null;
}

// Where a gallery photo is published.
export const photoHref = (slug, src) => `img/${slug}/gallery/${basename(src)}`;

// The gallery tab's body: one card per item, or null when there is none.
export function galleryList(items, tr, root, slug) {
  if (!items?.length) return null;
  const e = (k, params) => escapeHtml(tr(k, params));
  const cards = items.map((it) => {
    const title = e(it.title), note = it.note ? `<p>${e(it.note)}</p>` : "";
    if (it.kind === "photo")
      return `<li class="photo"><figure><img src="${root}${escapeHtml(photoHref(slug, it.src))}" alt="${e(it.alt ?? it.title)}" loading="lazy">
<figcaption><strong>${title}</strong>${note}</figcaption></figure></li>`;
    return `<li class="video"><span class="g-icon" aria-hidden="true">🎬</span>
<div><strong>${title}</strong>${note}
<p><a href="${escapeHtml(it.href)}" rel="noopener noreferrer">${e("gallery.watch", { where: it.where })}</a></p></div></li>`;
  }).join("\n");
  return `<ul class="gallery">
${cards}
</ul>`;
}
