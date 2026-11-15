/**
 * pipeline
 * SVG element tree -> a job file in Mural's format, plus what the Jobs tab
 * shows before uploading it: the strokes for a preview, distances, pen lifts
 * and an estimated time.
 *
 *   collect (svg.js) -> place on the drawing area -> flatten (geometry.js)
 *   -> outlines + hatch fills (hatch.js) -> clip to the area -> order
 *   (order.js) -> commands
 *
 * The order of steps is Mural's (flatten, infill, optimise, commands); the
 * code is not - see PLAN.md §2. Mural's own web UI stays an alternative: its
 * files plot unchanged.
 *
 * Coordinates are image mm as in CLAUDE.md: origin at the top-left of the
 * drawing area, +y down, area `width` wide and `maxY` deep.
 *
 * What it is not: aware of the device. The area, home and speeds come in as
 * options; the result goes out through the usual upload.
 *
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
import { apply, flatten, maxScale, multiply, parsePath } from './geometry.js';
import { collect, page } from './svg.js';
import { hatch } from './hatch.js';
import { clean, clip, order } from './order.js';

// How far a flattened curve may stray from the true one. Well under a belt
// step's worth of the pen's line width.
const TOLERANCE_MM = 0.1;

// Ends closer than this are drawn as one stroke, without lifting the pen.
const JOIN_MM = 0.05;

// For the estimate: the firmware's numbers (Path.cpp, Plotter.cpp, Pen.cpp).
const ACCEL = 300;          // mm/s², Path::ACCEL_MM_S2
const TRAVEL_SPEED = 40;    // mm/s, Plotter's TRAVEL_MM_S
const PEN_CHANGE_S = 0.4;   // a 20° ramp at 120°/s plus the 200 ms settle

export const DEFAULTS = {
  sizeMode: 'fit',      // 'fit': widthPercent of the area; 'actual': the document's own size
  widthPercent: 100,
  outlines: true,
  infill: false,
  hatchSpacing: 2,      // mm
  hatchAngle: 45,       // degrees
  drawSpeed: 15         // mm/s
};

/** Time for one straight move of `len` at up to `v`, from and to a stop. */
function moveTime(len, v) {
  if (len <= 0) return 0;
  const ramp = (v * v) / ACCEL;   // distance to accelerate and brake
  return len >= ramp ? len / v + v / ACCEL : 2 * Math.sqrt(len / ACCEL);
}

const fmt = (v) => {
  const s = v.toFixed(2);
  return s.includes('.') ? s.replace(/\.?0+$/, '') : s;
};

/**
 * @param tree     the SVG's element tree (lib/svgtree.js)
 * @param area     {width, maxY} in mm
 * @param home     {x, y}: where the pen starts and ends
 * @param options  see DEFAULTS
 */
export function convert(tree, area, home, options = {}) {
  const o = { ...DEFAULTS, ...options };
  const pg = page(tree);
  const { shapes, skipped } = collect(tree);
  const warnings = [];
  for (const [tag, count] of Object.entries(skipped)) warnings.push({ code: 'skipped', tag, count });

  // The page box in user units; without one, the drawing's own bounds.
  let box = pg.box;
  if (!box) {
    let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
    for (const s of shapes) {
      for (const sp of flatten(parsePath(s.d), 1e9)) {
        for (const p of sp.points) {
          const [x, y] = apply(s.matrix, p);
          x0 = Math.min(x0, x); y0 = Math.min(y0, y);
          x1 = Math.max(x1, x); y1 = Math.max(y1, y);
        }
      }
    }
    box = isFinite(x0) && x1 > x0 ? [x0, y0, x1 - x0, Math.max(y1 - y0, 1e-6)] : null;
  }
  if (!box) return { error: 'svgEmpty', warnings };

  let scale;
  if (o.sizeMode === 'actual' && pg.widthMm) {
    scale = pg.widthMm / box[2];
  } else {
    if (o.sizeMode === 'actual') warnings.push({ code: 'noSize' });
    scale = (area.width * Math.min(100, Math.max(1, o.widthPercent))) / 100 / box[2];
  }
  const widthMm = box[2] * scale, heightMm = box[3] * scale;
  // Centred across the area, from its top edge.
  const offsetX = (area.width - widthMm) / 2;
  const toArea = [scale, 0, 0, scale, offsetX - box[0] * scale, -box[1] * scale];

  const strokes = [];   // {points, closed}
  for (const s of shapes) {
    const m = multiply(toArea, s.matrix);
    const tol = TOLERANCE_MM / Math.max(maxScale(m), 1e-12);
    const subpaths = flatten(parsePath(s.d), tol).map((sp) => ({
      points: sp.points.map((p) => apply(m, p)),
      closed: sp.closed
    }));
    if (o.outlines && (s.stroke || s.fill)) strokes.push(...subpaths);
    if (o.infill && s.fill) {
      const lines = hatch(subpaths.map((sp) => sp.points), s.fillRule, o.hatchSpacing, o.hatchAngle);
      for (const points of lines) strokes.push({ points, closed: false });
    }
  }

  // Clip to the area. A loop that is cut open is no longer a loop.
  const kept = [];
  let clipped = false;
  for (const s of strokes) {
    let pts = s.points;
    if (s.closed) {
      const a = pts[0], b = pts[pts.length - 1];
      if (a[0] !== b[0] || a[1] !== b[1]) pts = [...pts, a];
    }
    const parts = clip(pts, area.width, area.maxY);
    const same = (p, q) => p[0] === q[0] && p[1] === q[1];
    const whole = parts.length === 1 && parts[0].length === pts.length &&
      same(parts[0][0], pts[0]) && same(parts[0][parts[0].length - 1], pts[pts.length - 1]);
    if (!whole) clipped = true;
    for (const part of parts) {
      const c = clean(part);
      if (!c) continue;
      if (s.closed && whole) {
        c.pop();   // order() wants loops without the repeated end
        if (c.length > 1) kept.push({ points: c, closed: true });
      } else {
        kept.push({ points: c, closed: false });
      }
    }
  }
  if (clipped) warnings.push({ code: 'clipped' });
  if (kept.length === 0) return { error: 'svgEmpty', warnings };

  const ordered = order(kept, home.x, home.y);

  // Commands, and the numbers for the tab.
  const lines = [];
  const preview = { draw: [], travel: [] };
  let px = home.x, py = home.y;
  let drawMm = 0, travelMm = 0, lifts = 0, seconds = 0;
  let maxYSeen = 0;
  let stroke = null;
  for (const pts of ordered) {
    const [sx, sy] = pts[0];
    const gap = Math.hypot(sx - px, sy - py);
    if (stroke && gap <= JOIN_MM) {
      // Continues the stroke that is down already.
    } else {
      if (stroke) {
        lines.push('p0');
        seconds += PEN_CHANGE_S;
      }
      travelMm += gap;
      seconds += moveTime(gap, TRAVEL_SPEED);
      preview.travel.push([[px, py], [sx, sy]]);
      lines.push(`${fmt(sx)} ${fmt(sy)}`, 'p1');
      seconds += PEN_CHANGE_S;
      lifts++;
      stroke = [[sx, sy]];
      preview.draw.push(stroke);
    }
    let strokeMm = 0;
    for (let i = 1; i < pts.length; i++) {
      const [x, y] = pts[i];
      strokeMm += Math.hypot(x - pts[i - 1][0], y - pts[i - 1][1]);
      lines.push(`${fmt(x)} ${fmt(y)}`);
      stroke.push([x, y]);
      maxYSeen = Math.max(maxYSeen, y, pts[i - 1][1]);
    }
    drawMm += strokeMm;
    // Corners are mostly taken at speed by the planner; one ramp per stroke.
    seconds += moveTime(strokeMm, o.drawSpeed);
    [px, py] = pts[pts.length - 1];
  }
  lines.push('p0');
  seconds += PEN_CHANGE_S;
  const back = Math.hypot(home.x - px, home.y - py);
  seconds += moveTime(back, TRAVEL_SPEED);
  preview.travel.push([[px, py], [home.x, home.y]]);

  const total = drawMm + travelMm;
  const text = [`d${fmt(total)}`, `h${fmt(Math.max(maxYSeen, heightMm))}`, ...lines].join('\n') + '\n';

  return {
    text,
    preview,
    stats: {
      widthMm,
      heightMm,
      drawMm,
      travelMm: travelMm + back,
      penLifts: lifts,
      seconds,
      lines: lines.length + 2,
      bytes: text.length
    },
    warnings
  };
}
