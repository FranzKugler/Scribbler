/**
 * order
 * The polylines of a drawing in the order a pen should draw them: greedy
 * nearest neighbour from where the pen is, open lines drawn from whichever
 * end is nearer, closed loops entered at their nearest vertex. Plus the
 * rectangle clip that keeps everything on the drawing area.
 *
 * Greedy is not optimal, but it removes most of the travel of an unordered
 * SVG (usually in the order the shapes were drawn in the editor), and with a
 * grid index it stays fast at tens of thousands of hatch strokes.
 *
 * Written fresh, not taken from Mural's worker. Works in millimetres.
 *
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */

/**
 * Liang-Barsky on each segment: the parts of the polyline inside
 * [0, w] x [0, h]. A line leaving and re-entering becomes two.
 */
export function clip(points, w, h) {
  const out = [];
  let cur = null;
  const eps = 1e-9;
  for (let i = 0; i + 1 < points.length; i++) {
    let [x0, y0] = points[i];
    let [x1, y1] = points[i + 1];
    const dx = x1 - x0, dy = y1 - y0;
    let t0 = 0, t1 = 1;
    let visible = true;
    for (const [p, q] of [[-dx, x0], [dx, w - x0], [-dy, y0], [dy, h - y0]]) {
      if (Math.abs(p) < eps) {
        if (q < 0) visible = false;
      } else {
        const r = q / p;
        if (p < 0) t0 = Math.max(t0, r);
        else t1 = Math.min(t1, r);
      }
    }
    if (!visible || t0 > t1) {
      cur = null;
      continue;
    }
    const a = [x0 + t0 * dx, y0 + t0 * dy];
    const b = [x0 + t1 * dx, y0 + t1 * dy];
    if (!cur || t0 > 0) {
      cur = [a];
      out.push(cur);
    }
    cur.push(b);
    if (t1 < 1) cur = null;
  }
  return out.filter((p) => p.length > 1);
}

/** Consecutive points closer than `eps` merged; null when nothing is left. */
export function clean(points, eps = 0.01) {
  const out = [points[0]];
  for (let i = 1; i < points.length; i++) {
    const last = out[out.length - 1];
    if (Math.hypot(points[i][0] - last[0], points[i][1] - last[1]) >= eps) out.push(points[i]);
  }
  return out.length > 1 ? out : null;
}

/**
 * Polylines [{points, closed}] ordered from (x, y). A closed one is given
 * without its repeated end point and comes back drawn as a full loop.
 * Returns point lists, each to be drawn pen-down.
 */
export function order(polylines, x, y) {
  const n = polylines.length;
  if (n === 0) return [];

  // Entry points: both ends of an open line, every vertex of a loop (thinned
  // on long loops - the nearest of 64 is close enough to the nearest).
  const entries = [];   // [px, py, poly, vertex, reversed]
  let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
  polylines.forEach((p, i) => {
    const pts = p.points;
    if (p.closed) {
      const stride = Math.max(1, Math.floor(pts.length / 64));
      for (let k = 0; k < pts.length; k += stride) entries.push([pts[k][0], pts[k][1], i, k, false]);
    } else {
      entries.push([pts[0][0], pts[0][1], i, 0, false]);
      const e = pts[pts.length - 1];
      entries.push([e[0], e[1], i, pts.length - 1, true]);
    }
  });
  for (const e of entries) {
    minX = Math.min(minX, e[0]); maxX = Math.max(maxX, e[0]);
    minY = Math.min(minY, e[1]); maxY = Math.max(maxY, e[1]);
  }
  const span = Math.max(maxX - minX, maxY - minY, 1);
  const cells = Math.max(1, Math.min(512, Math.ceil(Math.sqrt(entries.length / 2))));
  const size = span / cells + 1e-9;
  const grid = new Map();
  const key = (cx, cy) => cx * 1024 + cy;
  for (const e of entries) {
    const k = key(Math.floor((e[0] - minX) / size), Math.floor((e[1] - minY) / size));
    if (!grid.has(k)) grid.set(k, []);
    grid.get(k).push(e);
  }

  const used = new Uint8Array(n);
  const result = [];
  let px = x, py = y;
  for (let count = 0; count < n; count++) {
    const cx = Math.floor((px - minX) / size), cy = Math.floor((py - minY) / size);
    let best = null, bestD = Infinity;
    for (let r = 0; r <= cells + Math.ceil(Math.hypot(px - minX, py - minY) / size) + 1; r++) {
      for (let gx = cx - r; gx <= cx + r; gx++) {
        for (let gy = cy - r; gy <= cy + r; gy++) {
          if (Math.max(Math.abs(gx - cx), Math.abs(gy - cy)) !== r) continue;
          if (gx < 0 || gy < 0 || gx > cells || gy > cells) continue;
          const bucket = grid.get(key(gx, gy));
          if (!bucket) continue;
          for (let j = bucket.length - 1; j >= 0; j--) {
            const e = bucket[j];
            if (used[e[2]]) {
              // Lazy removal keeps later searches short.
              bucket[j] = bucket[bucket.length - 1];
              bucket.pop();
              continue;
            }
            const d = Math.hypot(e[0] - px, e[1] - py);
            if (d < bestD) {
              bestD = d;
              best = e;
            }
          }
        }
      }
      // Anything in ring r + 1 is at least r cells away.
      if (best && bestD <= r * size) break;
    }
    if (!best) break;
    const p = polylines[best[2]];
    used[best[2]] = 1;
    let pts;
    if (p.closed) {
      const k = best[3];
      pts = [...p.points.slice(k), ...p.points.slice(0, k + 1)];
    } else {
      pts = best[4] ? p.points.slice().reverse() : p.points;
    }
    result.push(pts);
    [px, py] = pts[pts.length - 1];
  }
  return result;
}
