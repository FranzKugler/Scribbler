/**
 * geometry
 * SVG's affine transforms and path data, turned into polylines: the parsing
 * and flattening half of the SVG -> job pipeline (see pipeline.js).
 *
 * Written fresh from the SVG 1.1 specification, not taken from Mural's worker
 * (which leaves this to paper.js). Plain functions on plain arrays, so it runs
 * in a web worker and under `node --test` alike.
 *
 * A matrix is [a, b, c, d, e, f] as in SVG: x' = a·x + c·y + e,
 * y' = b·x + d·y + f. A point is [x, y].
 *
 * What it is not: aware of styles, elements or the plotter.
 *
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */

export const IDENTITY = [1, 0, 0, 1, 0, 0];

export function multiply(m, n) {
  return [
    m[0] * n[0] + m[2] * n[1],
    m[1] * n[0] + m[3] * n[1],
    m[0] * n[2] + m[2] * n[3],
    m[1] * n[2] + m[3] * n[3],
    m[0] * n[4] + m[2] * n[5] + m[4],
    m[1] * n[4] + m[3] * n[5] + m[5]
  ];
}

export function apply(m, p) {
  return [m[0] * p[0] + m[2] * p[1] + m[4], m[1] * p[0] + m[3] * p[1] + m[5]];
}

/**
 * How much the matrix enlarges lengths, at most - so a flattening tolerance
 * given in output millimetres can be turned into one in user units.
 */
export function maxScale(m) {
  // Largest singular value of the 2x2 part.
  const s1 = m[0] * m[0] + m[1] * m[1] + m[2] * m[2] + m[3] * m[3];
  const det = m[0] * m[3] - m[1] * m[2];
  const s2 = Math.sqrt(Math.max(0, s1 * s1 - 4 * det * det));
  return Math.sqrt((s1 + s2) / 2);
}

const NUMBER = /[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?/g;

function numbers(text) {
  return (text.match(NUMBER) || []).map(Number);
}

/** The `transform` attribute, as one matrix. Unknown functions are skipped. */
export function parseTransform(text) {
  let m = IDENTITY;
  if (!text) return m;
  const re = /(matrix|translate|scale|rotate|skewX|skewY)\s*\(([^)]*)\)/g;
  let match;
  while ((match = re.exec(text))) {
    const v = numbers(match[2]);
    let t = IDENTITY;
    switch (match[1]) {
      case 'matrix':
        if (v.length === 6) t = v;
        break;
      case 'translate':
        t = [1, 0, 0, 1, v[0] || 0, v[1] || 0];
        break;
      case 'scale':
        t = [v[0] ?? 1, 0, 0, v[1] ?? v[0] ?? 1, 0, 0];
        break;
      case 'rotate': {
        const a = ((v[0] || 0) * Math.PI) / 180;
        const r = [Math.cos(a), Math.sin(a), -Math.sin(a), Math.cos(a), 0, 0];
        if (v.length >= 3) {
          t = multiply(multiply([1, 0, 0, 1, v[1], v[2]], r), [1, 0, 0, 1, -v[1], -v[2]]);
        } else {
          t = r;
        }
        break;
      }
      case 'skewX':
        t = [1, 0, Math.tan(((v[0] || 0) * Math.PI) / 180), 1, 0, 0];
        break;
      case 'skewY':
        t = [1, Math.tan(((v[0] || 0) * Math.PI) / 180), 0, 1, 0, 0];
        break;
    }
    m = multiply(m, t);
  }
  return m;
}

/**
 * Path data -> segments in absolute coordinates: M, L, C, Q, A and Z only
 * (H/V become L, S/T get their reflected control point, relative becomes
 * absolute). A malformed tail ends the path there, as SVG renderers do.
 */
export function parsePath(d) {
  const tokens = (d || '').match(/[MmLlHhVvCcSsQqTtAaZz]|[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?/g) || [];
  const out = [];
  let i = 0;
  let cmd = '';
  let x = 0, y = 0, sx = 0, sy = 0;
  let lastCtrl = null;   // [x, y, kind] of the last C/S or Q/T control point
  const num = () => (i < tokens.length && !/[A-Za-z]/.test(tokens[i]) ? Number(tokens[i++]) : NaN);
  // Arc flags may be written without separators ("a1 1 0 011 1"); the
  // tokenizer reads "011" as one number, so flags are split off here.
  const flag = () => {
    const t = tokens[i];
    if (t === undefined || /[A-Za-z]/.test(t)) return NaN;
    if (t[0] === '0' || t[0] === '1') {
      if (t.length === 1) {
        i++;
      } else {
        tokens[i] = t.slice(1);
      }
      return Number(t[0]);
    }
    return NaN;
  };

  while (i < tokens.length) {
    if (/[A-Za-z]/.test(tokens[i])) {
      cmd = tokens[i++];
    } else if (!cmd || cmd === 'Z' || cmd === 'z') {
      break;
    }
    const rel = cmd === cmd.toLowerCase();
    const ox = rel ? x : 0, oy = rel ? y : 0;
    switch (cmd.toUpperCase()) {
      case 'M': {
        const nx = num(), ny = num();
        if (isNaN(ny)) return out;
        x = ox + nx; y = oy + ny; sx = x; sy = y;
        out.push(['M', x, y]);
        cmd = rel ? 'l' : 'L';   // further pairs are line-tos
        lastCtrl = null;
        break;
      }
      case 'L': {
        const nx = num(), ny = num();
        if (isNaN(ny)) return out;
        x = ox + nx; y = oy + ny;
        out.push(['L', x, y]);
        lastCtrl = null;
        break;
      }
      case 'H': {
        const nx = num();
        if (isNaN(nx)) return out;
        x = ox + nx;
        out.push(['L', x, y]);
        lastCtrl = null;
        break;
      }
      case 'V': {
        const ny = num();
        if (isNaN(ny)) return out;
        y = oy + ny;
        out.push(['L', x, y]);
        lastCtrl = null;
        break;
      }
      case 'C':
      case 'S': {
        let x1, y1;
        if (cmd.toUpperCase() === 'C') {
          x1 = ox + num(); y1 = oy + num();
        } else if (lastCtrl && lastCtrl[2] === 'C') {
          x1 = 2 * x - lastCtrl[0]; y1 = 2 * y - lastCtrl[1];
        } else {
          x1 = x; y1 = y;
        }
        const x2 = ox + num(), y2 = oy + num(), ex = ox + num(), ey = oy + num();
        if ([x1, y1, x2, y2, ex, ey].some(isNaN)) return out;
        out.push(['C', x1, y1, x2, y2, ex, ey]);
        lastCtrl = [x2, y2, 'C'];
        x = ex; y = ey;
        break;
      }
      case 'Q':
      case 'T': {
        let x1, y1;
        if (cmd.toUpperCase() === 'Q') {
          x1 = ox + num(); y1 = oy + num();
        } else if (lastCtrl && lastCtrl[2] === 'Q') {
          x1 = 2 * x - lastCtrl[0]; y1 = 2 * y - lastCtrl[1];
        } else {
          x1 = x; y1 = y;
        }
        const ex = ox + num(), ey = oy + num();
        if ([x1, y1, ex, ey].some(isNaN)) return out;
        out.push(['Q', x1, y1, ex, ey]);
        lastCtrl = [x1, y1, 'Q'];
        x = ex; y = ey;
        break;
      }
      case 'A': {
        const rx = num(), ry = num(), rot = num(), large = flag(), sweep = flag();
        const ex = ox + num(), ey = oy + num();
        if ([rx, ry, rot, large, sweep, ex, ey].some(isNaN)) return out;
        out.push(['A', rx, ry, rot, large, sweep, ex, ey]);
        x = ex; y = ey;
        lastCtrl = null;
        break;
      }
      case 'Z':
        out.push(['Z']);
        x = sx; y = sy;
        lastCtrl = null;
        break;
      default:
        return out;
    }
  }
  return out;
}

/** Cubic Bézier into `pts` (start point already there), chord error <= tol. */
function cubic(pts, x0, y0, x1, y1, x2, y2, x3, y3, tol, depth = 0) {
  // Flat enough when both control points are within tol of the chord.
  const dx = x3 - x0, dy = y3 - y0;
  const len = Math.hypot(dx, dy);
  let d1, d2;
  if (len < 1e-12) {
    d1 = Math.hypot(x1 - x0, y1 - y0);
    d2 = Math.hypot(x2 - x0, y2 - y0);
  } else {
    d1 = Math.abs((x1 - x0) * dy - (y1 - y0) * dx) / len;
    d2 = Math.abs((x2 - x0) * dy - (y2 - y0) * dx) / len;
  }
  if (depth > 16 || Math.max(d1, d2) <= tol) {
    pts.push([x3, y3]);
    return;
  }
  const ax = (x0 + x1) / 2, ay = (y0 + y1) / 2;
  const bx = (x1 + x2) / 2, by = (y1 + y2) / 2;
  const cx = (x2 + x3) / 2, cy = (y2 + y3) / 2;
  const abx = (ax + bx) / 2, aby = (ay + by) / 2;
  const bcx = (bx + cx) / 2, bcy = (by + cy) / 2;
  const mx = (abx + bcx) / 2, my = (aby + bcy) / 2;
  cubic(pts, x0, y0, ax, ay, abx, aby, mx, my, tol, depth + 1);
  cubic(pts, mx, my, bcx, bcy, cx, cy, x3, y3, tol, depth + 1);
}

/** Elliptical arc (SVG endpoint form, F.6.5 of SVG 1.1) into `pts`. */
function arc(pts, x1, y1, rx, ry, rotDeg, large, sweep, x2, y2, tol) {
  if (x1 === x2 && y1 === y2) return;
  rx = Math.abs(rx);
  ry = Math.abs(ry);
  if (rx === 0 || ry === 0) {
    pts.push([x2, y2]);
    return;
  }
  const phi = (rotDeg * Math.PI) / 180;
  const cos = Math.cos(phi), sin = Math.sin(phi);
  const dx = (x1 - x2) / 2, dy = (y1 - y2) / 2;
  const xp = cos * dx + sin * dy, yp = -sin * dx + cos * dy;
  // Radii too small for the endpoints are scaled up (F.6.6).
  const lambda = (xp * xp) / (rx * rx) + (yp * yp) / (ry * ry);
  if (lambda > 1) {
    rx *= Math.sqrt(lambda);
    ry *= Math.sqrt(lambda);
  }
  const num = rx * rx * ry * ry - rx * rx * yp * yp - ry * ry * xp * xp;
  const den = rx * rx * yp * yp + ry * ry * xp * xp;
  let k = Math.sqrt(Math.max(0, num / den));
  if (large === sweep) k = -k;
  const cxp = (k * rx * yp) / ry, cyp = (-k * ry * xp) / rx;
  const cx = cos * cxp - sin * cyp + (x1 + x2) / 2;
  const cy = sin * cxp + cos * cyp + (y1 + y2) / 2;
  const angle = (ux, uy, vx, vy) => Math.atan2(ux * vy - uy * vx, ux * vx + uy * vy);
  const t1 = angle(1, 0, (xp - cxp) / rx, (yp - cyp) / ry);
  let dt = angle((xp - cxp) / rx, (yp - cyp) / ry, (-xp - cxp) / rx, (-yp - cyp) / ry);
  if (!sweep && dt > 0) dt -= 2 * Math.PI;
  if (sweep && dt < 0) dt += 2 * Math.PI;
  // Steps so that the sagitta r·(1 - cos(step/2)) stays within tol.
  const r = Math.max(rx, ry);
  const step = tol >= r ? Math.PI / 2 : 2 * Math.acos(1 - tol / r);
  const n = Math.max(1, Math.ceil(Math.abs(dt) / step));
  for (let s = 1; s <= n; s++) {
    if (s === n) {
      pts.push([x2, y2]);   // exactly, so a closing arc closes
      break;
    }
    const t = t1 + (dt * s) / n;
    const ex = rx * Math.cos(t), ey = ry * Math.sin(t);
    pts.push([cos * ex - sin * ey + cx, sin * ex + cos * ey + cy]);
  }
}

/**
 * Parsed segments -> subpaths [{points, closed}] in user units, curves
 * flattened to within `tol` user units.
 */
export function flatten(segments, tol) {
  const subpaths = [];
  let cur = null;
  let x = 0, y = 0;
  const begin = (px, py) => {
    cur = { points: [[px, py]], closed: false };
    subpaths.push(cur);
  };
  for (const s of segments) {
    switch (s[0]) {
      case 'M':
        begin(s[1], s[2]);
        x = s[1]; y = s[2];
        break;
      case 'L':
        if (!cur) begin(x, y);
        cur.points.push([s[1], s[2]]);
        x = s[1]; y = s[2];
        break;
      case 'C':
        if (!cur) begin(x, y);
        cubic(cur.points, x, y, s[1], s[2], s[3], s[4], s[5], s[6], tol);
        x = s[5]; y = s[6];
        break;
      case 'Q': {
        if (!cur) begin(x, y);
        // A quadratic is a cubic with its control point at 2/3.
        const c1x = x + (2 / 3) * (s[1] - x), c1y = y + (2 / 3) * (s[2] - y);
        const c2x = s[3] + (2 / 3) * (s[1] - s[3]), c2y = s[4] + (2 / 3) * (s[2] - s[4]);
        cubic(cur.points, x, y, c1x, c1y, c2x, c2y, s[3], s[4], tol);
        x = s[3]; y = s[4];
        break;
      }
      case 'A':
        if (!cur) begin(x, y);
        arc(cur.points, x, y, s[1], s[2], s[3], s[4], s[5], s[6], s[7], tol);
        x = s[6]; y = s[7];
        break;
      case 'Z':
        if (cur) {
          const start = cur.points[0];
          cur.closed = true;
          x = start[0]; y = start[1];
          // A later drawing command without M starts at the same point.
          cur = null;
          begin(x, y);
        }
        break;
    }
  }
  return subpaths.filter((p) => p.points.length > 1);
}

/**
 * Basic shapes as path data, so everything goes through one flattener. Rounded
 * rectangles and ellipses come out as arcs.
 */
export function shapePath(tag, a) {
  const n = (k, fallback = 0) => {
    const v = parseFloat(a[k]);
    return isFinite(v) ? v : fallback;
  };
  switch (tag) {
    case 'rect': {
      const x = n('x'), y = n('y'), w = n('width'), h = n('height');
      if (w <= 0 || h <= 0) return '';
      let rx = a.rx !== undefined ? n('rx') : n('ry');
      let ry = a.ry !== undefined ? n('ry') : rx;
      rx = Math.min(Math.max(rx, 0), w / 2);
      ry = Math.min(Math.max(ry, 0), h / 2);
      if (rx === 0 || ry === 0) return `M${x} ${y}H${x + w}V${y + h}H${x}Z`;
      return (
        `M${x + rx} ${y}H${x + w - rx}A${rx} ${ry} 0 0 1 ${x + w} ${y + ry}` +
        `V${y + h - ry}A${rx} ${ry} 0 0 1 ${x + w - rx} ${y + h}` +
        `H${x + rx}A${rx} ${ry} 0 0 1 ${x} ${y + h - ry}` +
        `V${y + ry}A${rx} ${ry} 0 0 1 ${x + rx} ${y}Z`
      );
    }
    case 'circle':
    case 'ellipse': {
      const cx = n('cx'), cy = n('cy');
      const rx = tag === 'circle' ? n('r') : n('rx');
      const ry = tag === 'circle' ? n('r') : n('ry');
      if (rx <= 0 || ry <= 0) return '';
      return `M${cx + rx} ${cy}A${rx} ${ry} 0 0 1 ${cx - rx} ${cy}A${rx} ${ry} 0 0 1 ${cx + rx} ${cy}Z`;
    }
    case 'line':
      return `M${n('x1')} ${n('y1')}L${n('x2')} ${n('y2')}`;
    case 'polyline':
    case 'polygon': {
      const v = numbers(a.points || '');
      if (v.length < 4) return '';
      let d = `M${v[0]} ${v[1]}`;
      for (let i = 2; i + 1 < v.length; i += 2) d += `L${v[i]} ${v[i + 1]}`;
      return tag === 'polygon' ? d + 'Z' : d;
    }
    case 'path':
      return a.d || '';
  }
  return '';
}
