/**
 * svg
 * An SVG document's element tree -> the shapes a pen would draw: each with
 * its path data, its transform to the page and whether it is stroked or
 * filled. The page is the root's viewBox (or width/height).
 *
 * The tree comes in as plain objects {tag, attrs, children, text}, because a
 * web worker has no DOMParser: the main thread parses (lib/svgtree.js), the
 * worker does the rest. Written fresh, not taken from Mural's worker.
 *
 * Supported: path, rect, circle, ellipse, line, polyline, polygon; g, nested
 * svg, use (also of symbols); transforms; presentation attributes, the style
 * attribute and simple <style> rules (.class, #id, tag, comma lists).
 * Skipped and counted, so the UI can say so: text, image, foreignObject.
 * Ignored: clip paths, masks, markers, patterns - a plotter draws what is
 * there, not what is shown.
 *
 * What it is not: the flattener (geometry.js) or anything about millimetres
 * on the wall (pipeline.js).
 *
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
import { IDENTITY, multiply, parseTransform, shapePath } from './geometry.js';

const SHAPES = new Set(['path', 'rect', 'circle', 'ellipse', 'line', 'polyline', 'polygon']);
const CONTAINERS = new Set(['g', 'svg', 'a', 'switch']);
const NEVER_DRAWN = new Set([
  'defs', 'symbol', 'clipPath', 'mask', 'marker', 'pattern', 'linearGradient',
  'radialGradient', 'filter', 'style', 'title', 'desc', 'metadata', 'script'
]);
const SKIPPED = new Set(['text', 'image', 'foreignObject']);
const INHERITED = ['fill', 'stroke', 'fill-rule', 'visibility'];

/** Millimetres per unit of an SVG length. A bare number is CSS px. */
const UNIT_MM = { mm: 1, cm: 10, in: 25.4, pt: 25.4 / 72, pc: 25.4 / 6, px: 25.4 / 96, '': 25.4 / 96 };

/** "210mm" -> 210; null for percentages and garbage. */
export function lengthMm(text) {
  const m = /^\s*([+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)\s*(mm|cm|in|pt|pc|px)?\s*$/.exec(text || '');
  if (!m) return null;
  return Number(m[1]) * UNIT_MM[m[2] || ''];
}

function parseDeclarations(text) {
  const out = {};
  for (const part of (text || '').split(';')) {
    const i = part.indexOf(':');
    if (i < 0) continue;
    const key = part.slice(0, i).trim();
    const value = part.slice(i + 1).replace(/!important/, '').trim();
    if (key) out[key] = value;
  }
  return out;
}

/** <style> text -> [{selector, decl}] for the simple selectors only. */
export function parseStyleSheet(text) {
  const rules = [];
  const css = (text || '').replace(/\/\*[\s\S]*?\*\//g, '');
  const re = /([^{}]+)\{([^}]*)\}/g;
  let m;
  while ((m = re.exec(css))) {
    const decl = parseDeclarations(m[2]);
    for (const sel of m[1].split(',')) {
      const s = sel.trim();
      if (/^[.#]?[A-Za-z_][\w-]*$/.test(s)) rules.push({ selector: s, decl });
    }
  }
  return rules;
}

function matches(selector, node) {
  if (selector[0] === '.') return (node.attrs.class || '').split(/\s+/).includes(selector.slice(1));
  if (selector[0] === '#') return node.attrs.id === selector.slice(1);
  return node.tag === selector;
}

/**
 * The matrix that maps a viewBox onto a viewport of w x h, as
 * preserveAspectRatio's default (xMidYMid meet) does.
 */
export function viewBoxMatrix(vb, w, h) {
  if (!vb || !(vb[2] > 0) || !(vb[3] > 0) || !(w > 0) || !(h > 0)) return IDENTITY;
  const s = Math.min(w / vb[2], h / vb[3]);
  const tx = (w - vb[2] * s) / 2 - vb[0] * s;
  const ty = (h - vb[3] * s) / 2 - vb[1] * s;
  return [s, 0, 0, s, tx, ty];
}

function parseViewBox(text) {
  const v = (text || '').split(/[\s,]+/).filter(Boolean).map(Number);
  return v.length === 4 && v.every(isFinite) && v[2] > 0 && v[3] > 0 ? v : null;
}

/**
 * The page: the root's coordinate box in user units, plus its physical size
 * in mm when width/height say so. Null box when the document gives neither a
 * viewBox nor a size - the pipeline then falls back to the drawing's bounds.
 */
export function page(root) {
  const vb = parseViewBox(root.attrs.viewBox);
  const wMm = lengthMm(root.attrs.width);
  const hMm = lengthMm(root.attrs.height);
  if (vb) {
    return {
      box: vb,
      widthMm: wMm ?? (hMm !== null ? (hMm * vb[2]) / vb[3] : null),
      heightMm: hMm ?? (wMm !== null ? (wMm * vb[3]) / vb[2] : null)
    };
  }
  const w = parseFloat(root.attrs.width), h = parseFloat(root.attrs.height);
  if (w > 0 && h > 0 && wMm !== null && hMm !== null) {
    // No viewBox: one user unit is one px, whatever unit the size is in.
    const pxPerUnit = 25.4 / 96;
    return { box: [0, 0, wMm / pxPerUnit, hMm / pxPerUnit], widthMm: wMm, heightMm: hMm };
  }
  return { box: null, widthMm: null, heightMm: null };
}

/**
 * All drawable shapes under the root, in root user units (the root's own
 * viewBox is the page, not a transform). Each: {d, matrix, stroke, fill,
 * fillRule}. `skipped` counts the elements a pen cannot draw.
 */
export function collect(root) {
  const byId = new Map();
  const rules = [];
  (function index(node) {
    if (node.attrs.id) byId.set(node.attrs.id, node);
    if (node.tag === 'style') rules.push(...parseStyleSheet(node.text));
    for (const c of node.children) index(c);
  })(root);

  const shapes = [];
  const skipped = {};

  function styleOf(node, inherited) {
    // Precedence as in CSS: presentation attribute < style sheet < style attribute.
    const own = {};
    for (const k of [...INHERITED, 'display']) if (node.attrs[k] !== undefined) own[k] = node.attrs[k];
    for (const r of rules) if (matches(r.selector, node)) Object.assign(own, r.decl);
    Object.assign(own, parseDeclarations(node.attrs.style));
    const style = { ...inherited };
    for (const k of INHERITED) {
      if (own[k] !== undefined && own[k] !== 'inherit') style[k] = own[k];
    }
    style.display = own.display;
    return style;
  }

  function walk(node, matrix, inherited, depth, viaUse) {
    if (depth > 64) return;   // a <use> cycle, or a hostile file
    if (NEVER_DRAWN.has(node.tag) && !(viaUse && node.tag === 'symbol')) return;
    if (SKIPPED.has(node.tag)) {
      skipped[node.tag] = (skipped[node.tag] || 0) + 1;
      return;
    }
    const style = styleOf(node, inherited);
    if (style.display === 'none') return;
    let m = multiply(matrix, parseTransform(node.attrs.transform));

    if (SHAPES.has(node.tag)) {
      if (style.visibility === 'hidden' || style.visibility === 'collapse') return;
      const d = shapePath(node.tag, node.attrs);
      if (!d) return;
      const paint = (v) => v !== undefined && v !== 'none' && v !== 'transparent';
      shapes.push({
        d,
        matrix: m,
        // SVG's defaults: filled black, not stroked.
        fill: style.fill === undefined ? true : paint(style.fill),
        stroke: paint(style.stroke),
        fillRule: style['fill-rule'] === 'evenodd' ? 'evenodd' : 'nonzero'
      });
      return;
    }

    if (node.tag === 'use') {
      const ref = (node.attrs.href || node.attrs['xlink:href'] || '').replace(/^#/, '');
      const target = byId.get(ref);
      if (!target) return;
      const x = parseFloat(node.attrs.x) || 0, y = parseFloat(node.attrs.y) || 0;
      m = multiply(m, [1, 0, 0, 1, x, y]);
      if (target.tag === 'symbol') {
        const vb = parseViewBox(target.attrs.viewBox);
        const w = parseFloat(node.attrs.width ?? target.attrs.width);
        const h = parseFloat(node.attrs.height ?? target.attrs.height);
        if (vb) m = multiply(m, viewBoxMatrix(vb, w || vb[2], h || vb[3]));
      }
      walk(target, m, style, depth + 1, true);
      return;
    }

    if (node.tag === 'svg' && depth > 0) {
      // A nested viewport: placed at x/y, its viewBox onto its width/height.
      const x = parseFloat(node.attrs.x) || 0, y = parseFloat(node.attrs.y) || 0;
      m = multiply(m, [1, 0, 0, 1, x, y]);
      const vb = parseViewBox(node.attrs.viewBox);
      if (vb) {
        const w = parseFloat(node.attrs.width) || vb[2], h = parseFloat(node.attrs.height) || vb[3];
        m = multiply(m, viewBoxMatrix(vb, w, h));
      }
    }

    if (CONTAINERS.has(node.tag) || depth === 0 || viaUse) {
      for (const c of node.children) walk(c, m, style, depth + 1, false);
    }
  }

  walk(root, IDENTITY, {}, 0, false);
  return { shapes, skipped };
}
