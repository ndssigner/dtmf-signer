// The bench's geometry, without a page: which way it is laid out, and the
// path of a link between two ports (straight when they line up, else one
// right-angle elbow halfway).

export const TURN_WIDTH = 720;   // under this width the bench is drawn top to bottom

// turn: a bench with more modules side by side may stack at a wider width
// (its data-turn, from the build's config).
export function orientationFor(width, turn = TURN_WIDTH) {
  return width < turn ? "v" : "h";
}

const r = (n) => Math.round(n * 10) / 10;

export function linkPath(a, b, orientation) {
  if (orientation === "h") {
    if (a.y === b.y) return `M${r(a.x)} ${r(a.y)} H${r(b.x)}`;
    const mid = r((a.x + b.x) / 2);
    return `M${r(a.x)} ${r(a.y)} H${mid} V${r(b.y)} H${r(b.x)}`;
  }
  if (a.x === b.x) return `M${r(a.x)} ${r(a.y)} V${r(b.y)}`;
  const mid = r((a.y + b.y) / 2);
  return `M${r(a.x)} ${r(a.y)} V${mid} H${r(b.x)} V${r(b.y)}`;
}
