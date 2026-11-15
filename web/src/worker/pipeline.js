/**
 * pipeline
 * An SVG element tree or a photo -> a job file in Mural's format, plus what
 * the Jobs tab shows before uploading it: the strokes for a preview,
 * distances, pen lifts and an estimated time.
 *
 *   SVG:   collect (svg.js) -> place on the drawing area -> flatten
 *          (geometry.js) -> outlines + hatch fills (hatch.js) -> hidden parts
 *          cut away (occlude.js)
 *   photo: placed on the area -> squiggle rows (raster.js)
 *   both:  clip to the area -> thin straight runs -> order (order.js)
 *          -> commands
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
 * @version  1.1
 * @created  15.11.2026
 * @updated  16.11.2026
 */
import { apply, flatten, maxScale, multiply, parsePath } from './geometry.js';
import { collect, page } from './svg.js';
import { hatch } from './hatch.js';
import { occluder, cutAway } from './occlude.js';
import { squiggle } from './raster.js';
import { clean, clip, order } from './order.js';

// How far a flattened curve may stray from the true one. Well under a belt
// step's worth of the pen's line width.
const TOLERANCE_MM = 0.1;

// Ends closer than this are drawn as one stroke, without lifting the pen.
const JOIN_MM = 0.05;

// A point this close to the line through its neighbours adds nothing.
const STRAIGHT_MM = 0.01;

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
  hideOccluded: true,   // cut away what later fills cover
  // photos
  lineSpacing: 3,       // mm between rows
  wavelength: 3,        // mm per wave
  gamma: 1,
  continuous: true,
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

const same = (p, q) => p[0] === q[0] && p[1] === q[1];

/** A polyline with its closing point appended when it is a loop. */
const closedPoints = (s) => (s.closed && !same(s.points[0], s.points[s.points.length - 1]) ? [...s.points, s.points[0]] : s.points);

/** Drops points that lie on the line between their neighbours. */
function thin(points) {
  if (points.length < 3) return points;
  const out = [points[0]];
  for (let i = 1; i < points.length - 1; i++) {
    const a = out[out.length - 1], b = points[i], c = points[i + 1];
    const dx = c[0] - a[0], dy = c[1] - a[1];
    const len = Math.hypot(dx, dy);
    const off = len < 1e-12 ? Math.hypot(b[0] - a[0], b[1] - a[1]) : Math.abs((b[0] - a[0]) * dy - (b[1] - a[1]) * dx) / len;
    if (off > STRAIGHT_MM) out.push(b);
  }
  out.push(points[points.length - 1]);
  return out;
}

/** Width and offset of a drawing `aspect` (h / w) wide, as the options place it. */
function place(area, o, naturalWidthMm, warnings) {
  let widthMm;
  if (o.sizeMode === 'actual' && naturalWidthMm) {
    widthMm = naturalWidthMm;
  } else {
    if (o.sizeMode === 'actual') warnings.push({ code: 'noSize' });
    widthMm = (area.width * Math.min(100, Math.max(1, o.widthPercent))) / 100;
  }
  // Centred across the area, from its top edge.
  return { widthMm, left: (area.width - widthMm) / 2 };
}

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

  const { widthMm, left } = place(area, o, pg.widthMm, warnings);
  const scale = widthMm / box[2];
  const heightMm = box[3] * scale;
  const toArea = [scale, 0, 0, scale, left - box[0] * scale, -box[1] * scale];

  // Per shape, in paint order: what it draws, and what it hides of the
  // shapes before it.
  const painted = [];
  for (const s of shapes) {
    const m = multiply(toArea, s.matrix);
    const tol = TOLERANCE_MM / Math.max(maxScale(m), 1e-12);
    const subpaths = flatten(parsePath(s.d), tol).map((sp) => ({
      points: sp.points.map((p) => apply(m, p)),
      closed: sp.closed
    }));
    const strokes = [];
    if (o.outlines && (s.stroke || s.fill)) strokes.push(...subpaths);
    if (o.infill && s.fill) {
      const lines = hatch(subpaths.map((sp) => sp.points), s.fillRule, o.hatchSpacing, o.hatchAngle);
      for (const points of lines) strokes.push({ points, closed: false });
    }
    const hides = s.fill && subpaths.length ? occluder(subpaths.map((sp) => sp.points), s.fillRule) : null;
    painted.push({ strokes, hides });
  }

  let strokes = [];
  if (o.hideOccluded) {
    // Walk back to front, collecting the fills painted after each shape.
    const later = [];
    let hidden = false;
    for (let i = painted.length - 1; i >= 0; i--) {
      for (const s of painted[i].strokes) {
        const pts = closedPoints(s);
        const parts = cutAway(pts, later);
        const whole = parts.length === 1 && parts[0] === pts;
        if (!whole) hidden = true;
        if (whole) strokes.push(s);
        else for (const p of parts) strokes.push({ points: p, closed: false });
      }
      if (painted[i].hides) later.push(painted[i].hides);
    }
    strokes.reverse();
    if (hidden) warnings.push({ code: 'hidden' });
  } else {
    for (const p of painted) strokes.push(...p.strokes);
  }

  return finish(strokes, area, home, o, warnings, widthMm, heightMm);
}

/**
 * @param img      {gray: 0..1 per pixel, row by row, width, height}
 * @param area     {width, maxY} in mm
 * @param home     {x, y}
 * @param options  see DEFAULTS (sizeMode is always 'fit' - a photo has no mm)
 */
export function convertImage(img, area, home, options = {}) {
  const o = { ...DEFAULTS, ...options, sizeMode: 'fit' };
  const warnings = [];
  if (!(img?.width > 0 && img?.height > 0) || img.gray.length !== img.width * img.height) {
    return { error: 'imageUnreadable', warnings };
  }
  const { widthMm, left } = place(area, o, null, warnings);
  const heightMm = (widthMm * img.height) / img.width;
  const lines = squiggle(img, left, 0, widthMm, heightMm, o);
  return finish(lines.map((points) => ({ points, closed: false })), area, home, o, warnings, widthMm, heightMm);
}

/** Clip, thin, order, and write: the part SVGs and photos share. */
function finish(strokes, area, home, o, warnings, widthMm, heightMm) {
  // Clip to the area. A loop that is cut open is no longer a loop.
  const kept = [];
  let clipped = false;
  for (const s of strokes) {
    const pts = closedPoints(s);
    const parts = clip(pts, area.width, area.maxY);
    const whole = parts.length === 1 && parts[0].length === pts.length &&
      same(parts[0][0], pts[0]) && same(parts[0][parts[0].length - 1], pts[pts.length - 1]);
    if (!whole) clipped = true;
    for (const part of parts) {
      let c = clean(part);
      if (!c) continue;
      c = thin(c);
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
