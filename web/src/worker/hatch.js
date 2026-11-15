/**
 * hatch
 * Fills a shape with parallel lines, as a pen fills an area: scanlines at a
 * given angle and spacing, cut by the shape's outline under its fill rule,
 * then chained back and forth so that one pen-down stroke covers many lines
 * - every pen lift costs the better part of a second on the wall.
 *
 * Neighbouring lines are joined only where the short connector between them
 * stays inside the shape (checked at its midpoint), so a join never draws
 * across a gap.
 *
 * Written fresh, not taken from Mural's worker (whose infill is paper.js
 * boolean operations). Works in millimetres.
 *
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */

/** Winding number of `rings` around (x, y). */
function winding(rings, x, y) {
  let w = 0;
  for (const ring of rings) {
    for (let i = 0, n = ring.length; i < n; i++) {
      const [x0, y0] = ring[i];
      const [x1, y1] = ring[(i + 1) % n];
      if (y0 <= y) {
        if (y1 > y && (x1 - x0) * (y - y0) - (x - x0) * (y1 - y0) > 0) w++;
      } else if (y1 <= y && (x1 - x0) * (y - y0) - (x - x0) * (y1 - y0) < 0) {
        w--;
      }
    }
  }
  return w;
}

export function inside(rings, rule, x, y) {
  const w = winding(rings, x, y);
  return rule === 'evenodd' ? (w & 1) === 1 : w !== 0;
}

/**
 * Hatch lines for one filled shape. `rings` are closed point lists in mm
 * (every subpath counts as closed for filling, as in SVG). Returns polylines.
 */
export function hatch(rings, rule, spacing, angleDeg) {
  if (!(spacing > 0) || rings.length === 0) return [];
  const a = (angleDeg * Math.PI) / 180;
  const cos = Math.cos(a), sin = Math.sin(a);
  // Into a frame where the hatch lines are horizontal, and back.
  const fwd = ([x, y]) => [x * cos + y * sin, -x * sin + y * cos];
  const back = ([u, v]) => [u * cos - v * sin, u * sin + v * cos];
  const rot = rings.map((r) => r.map(fwd));

  let vMin = Infinity, vMax = -Infinity;
  for (const r of rot) for (const p of r) {
    vMin = Math.min(vMin, p[1]);
    vMax = Math.max(vMax, p[1]);
  }
  // On a grid common to all shapes, so neighbouring fills line up.
  const first = Math.ceil(vMin / spacing - 0.5) + 0.5;

  const lines = [];   // per scanline: [[u0, u1], ...]
  for (let k = first; k * spacing < vMax; k++) {
    const v = k * spacing;
    const hits = [];
    for (const r of rot) {
      for (let i = 0, n = r.length; i < n; i++) {
        const [u0, v0] = r[i];
        const [u1, v1] = r[(i + 1) % n];
        // Half-open, so a vertex on the line is counted once.
        if ((v0 <= v && v1 > v) || (v1 <= v && v0 > v)) {
          hits.push({ u: u0 + ((v - v0) / (v1 - v0)) * (u1 - u0), dir: v1 > v0 ? 1 : -1 });
        }
      }
    }
    hits.sort((p, q) => p.u - q.u);
    const spans = [];
    let w = 0;
    for (let i = 0; i < hits.length - 1; i++) {
      w += hits[i].dir;
      const filled = rule === 'evenodd' ? (Math.abs(w) & 1) === 1 : w !== 0;
      if (filled && hits[i + 1].u - hits[i].u > 1e-9) {
        // Merge spans that touch (a nonzero overlap of two subpaths).
        const last = spans[spans.length - 1];
        if (last && Math.abs(last[1] - hits[i].u) < 1e-9) last[1] = hits[i + 1].u;
        else spans.push([hits[i].u, hits[i + 1].u]);
      }
    }
    lines.push({ v, spans });
  }

  // Chain: each span continues a chain that ended on the line before when
  // the connector is short and inside the shape.
  const done = [];
  let open = [];   // chains ending on the previous line: {pts}
  const maxJoin = 3 * spacing;
  // The connector's midpoint, nudged a hair towards the new span's centre:
  // on a straight edge both ends sit exactly on the outline, and so would
  // the midpoint - neither inside nor out.
  const joinable = (eu, ev, su, sv, centre) => {
    const mu = (eu + su) / 2, mv = (ev + sv) / 2;
    const du = centre - mu, dv = sv - mv;
    const len = Math.hypot(du, dv) || 1;
    const nudge = 1e-3 * spacing;
    return inside(rot, rule, mu + (du / len) * nudge, mv + (dv / len) * nudge);
  };
  for (const { v, spans } of lines) {
    const next = [];
    const free = open.slice();
    for (const [u0, u1] of spans) {
      let best = -1, bestD = Infinity, bestFlip = false;
      for (let c = 0; c < free.length; c++) {
        const [eu, ev] = free[c].pts[free[c].pts.length - 1];
        for (const flip of [false, true]) {
          const su = flip ? u1 : u0;
          const d = Math.hypot(su - eu, v - ev);
          if (d < bestD && d <= maxJoin && joinable(eu, ev, su, v, (u0 + u1) / 2)) {
            best = c;
            bestD = d;
            bestFlip = flip;
          }
        }
      }
      if (best >= 0) {
        const chain = free.splice(best, 1)[0];
        chain.pts.push(bestFlip ? [u1, v] : [u0, v], bestFlip ? [u0, v] : [u1, v]);
        next.push(chain);
      } else {
        next.push({ pts: [[u0, v], [u1, v]] });
      }
    }
    done.push(...free);
    open = next;
  }
  done.push(...open);
  return done.map((c) => c.pts.map(back));
}
