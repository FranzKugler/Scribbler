/**
 * occlude
 * Hidden-line removal for a pen: SVG paints in document order, so a filled
 * shape hides whatever was painted before it where it lies. A pen cannot
 * paint over, so the hidden parts of earlier strokes are cut away instead -
 * outlines and hatching alike. Strokes of shapes without fill hide nothing.
 *
 * Each stroke segment is split at every crossing with a later fill's outline;
 * each piece is kept or dropped by whether its midpoint lies inside one of
 * those fills (under that fill's own rule). Bounding boxes keep it near
 * linear for drawings whose shapes do not all overlap.
 *
 * Mural's "flatten" option does this with paper.js booleans; this is written
 * fresh, on plain point lists in mm.
 *
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  16.11.2026
 * @updated  16.11.2026
 */
import { inside } from './hatch.js';

function bbox(points, into = [Infinity, Infinity, -Infinity, -Infinity]) {
  for (const [x, y] of points) {
    if (x < into[0]) into[0] = x;
    if (y < into[1]) into[1] = y;
    if (x > into[2]) into[2] = x;
    if (y > into[3]) into[3] = y;
  }
  return into;
}

const overlaps = (a, b) => a[0] <= b[2] && b[0] <= a[2] && a[1] <= b[3] && b[1] <= a[3];

/** A filled shape as something that hides: its rings, rule and bounds. */
export function occluder(rings, rule) {
  const box = [Infinity, Infinity, -Infinity, -Infinity];
  for (const r of rings) bbox(r, box);
  return { rings, rule, box };
}

/** Parameters t in (0, 1) where segment a-b crosses an edge of `o`'s rings. */
function crossings(ax, ay, bx, by, o, out) {
  const dx = bx - ax, dy = by - ay;
  for (const ring of o.rings) {
    for (let i = 0, n = ring.length; i < n; i++) {
      const [cx, cy] = ring[i];
      const [ex, ey] = ring[(i + 1) % n];
      const fx = ex - cx, fy = ey - cy;
      const den = dx * fy - dy * fx;
      if (Math.abs(den) < 1e-12) continue;   // parallel: no single crossing
      const t = ((cx - ax) * fy - (cy - ay) * fx) / den;
      const u = ((cx - ax) * dy - (cy - ay) * dx) / den;
      if (t > 1e-9 && t < 1 - 1e-9 && u >= 0 && u <= 1) out.push(t);
    }
  }
}

/**
 * The visible parts of one polyline, given the fills painted after it.
 * Returns point lists; an untouched line comes back as itself.
 */
export function cutAway(points, occluders) {
  if (occluders.length === 0) return [points];
  const box = bbox(points);
  const near = occluders.filter((o) => overlaps(o.box, box));
  if (near.length === 0) return [points];

  const pieces = [];
  let cur = null;
  const hidden = (x, y) => near.some((o) => x >= o.box[0] && x <= o.box[2] && y >= o.box[1] &&
    y <= o.box[3] && inside(o.rings, o.rule, x, y));

  for (let i = 0; i + 1 < points.length; i++) {
    const [ax, ay] = points[i];
    const [bx, by] = points[i + 1];
    const seg = [Math.min(ax, bx), Math.min(ay, by), Math.max(ax, bx), Math.max(ay, by)];
    const ts = [0];
    for (const o of near) if (overlaps(o.box, seg)) crossings(ax, ay, bx, by, o, ts);
    ts.push(1);
    ts.sort((p, q) => p - q);
    for (let k = 0; k + 1 < ts.length; k++) {
      const t0 = ts[k], t1 = ts[k + 1];
      if (t1 - t0 < 1e-12) continue;
      const tm = (t0 + t1) / 2;
      if (hidden(ax + (bx - ax) * tm, ay + (by - ay) * tm)) {
        cur = null;
        continue;
      }
      const p0 = [ax + (bx - ax) * t0, ay + (by - ay) * t0];
      const p1 = [ax + (bx - ax) * t1, ay + (by - ay) * t1];
      if (!cur) {
        cur = [p0];
        pieces.push(cur);
      }
      cur.push(p1);
    }
  }
  return pieces.filter((p) => p.length > 1);
}
