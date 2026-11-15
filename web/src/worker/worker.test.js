/**
 * worker.test
 * Host tests of the SVG -> job pipeline: `npm test` (node --test, no
 * browser). The trees are written as the main thread would hand them over
 * (lib/svgtree.js).
 *
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';

import { flatten, parsePath, parseTransform, apply, shapePath } from './geometry.js';
import { collect, page, lengthMm } from './svg.js';
import { hatch } from './hatch.js';
import { clip, order } from './order.js';
import { convert } from './pipeline.js';

const el = (tag, attrs = {}, ...children) => ({ tag, attrs, children, text: '' });
const near = (a, b, eps = 1e-6) => assert.ok(Math.abs(a - b) <= eps, `${a} != ${b} (±${eps})`);
const length = (pts) => pts.slice(1).reduce((s, p, i) => s + Math.hypot(p[0] - pts[i][0], p[1] - pts[i][1]), 0);

test('path data: relative, implicit repeats, H/V, S and packed arc flags', () => {
  const segs = parsePath('m10 10 5 0 0 5h-5v-5zM0 0a5 5 0 015 5');
  assert.deepEqual(segs.slice(0, 5), [['M', 10, 10], ['L', 15, 10], ['L', 15, 15], ['L', 10, 15], ['L', 10, 10]]);
  assert.equal(segs[5][0], 'Z');
  assert.deepEqual(segs[7], ['A', 5, 5, 0, 0, 1, 5, 5]);
  const s = parsePath('M0 0C0 10 10 10 10 0S20 -10 20 0');
  assert.deepEqual(s[2], ['C', 10, -10, 20, -10, 20, 0]);   // reflected control point
});

test('a malformed tail ends the path where it breaks', () => {
  assert.deepEqual(parsePath('M0 0L10 10L5'), [['M', 0, 0], ['L', 10, 10]]);
});

test('circle flattened within tolerance, closed, right circumference', () => {
  const [sp] = flatten(parsePath(shapePath('circle', { cx: 0, cy: 0, r: 50 })), 0.1);
  assert.equal(sp.closed, true);
  for (const [x, y] of sp.points) near(Math.hypot(x, y), 50, 1e-9);
  // Chords inside the circle by at most the tolerance.
  for (let i = 1; i < sp.points.length; i++) {
    const [ax, ay] = sp.points[i - 1], [bx, by] = sp.points[i];
    assert.ok(50 - Math.hypot((ax + bx) / 2, (ay + by) / 2) <= 0.1 + 1e-9);
  }
  near(length(sp.points), 2 * Math.PI * 50, 0.5);
});

test('cubic flattened within tolerance of the curve', () => {
  const [sp] = flatten(parsePath('M0 0C0 100 100 100 100 0'), 0.05);
  const bez = (t) => [300 * t * (1 - t) ** 2 * 0 + 3 * t * t * (1 - t) * 100 + t ** 3 * 100,
    3 * t * (1 - t) ** 2 * 100 + 3 * t * t * (1 - t) * 100];
  // Every curve point is close to some chord.
  for (let t = 0; t <= 1; t += 0.01) {
    const [x, y] = bez(t);
    let best = Infinity;
    for (let i = 1; i < sp.points.length; i++) {
      const [ax, ay] = sp.points[i - 1], [bx, by] = sp.points[i];
      const dx = bx - ax, dy = by - ay;
      const u = Math.max(0, Math.min(1, ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy)));
      best = Math.min(best, Math.hypot(ax + u * dx - x, ay + u * dy - y));
    }
    assert.ok(best <= 0.06, `t=${t}: ${best}`);
  }
});

test('transforms compose left to right, rotate about a point', () => {
  const m = parseTransform('translate(10,0) scale(2) rotate(90 1 0)');
  const p = apply(m, [2, 0]);
  near(p[0], 10 + 2 * 1);
  near(p[1], 2 * 1);
});

test('units and the page box', () => {
  near(lengthMm('210mm'), 210);
  near(lengthMm('96'), 25.4);
  assert.equal(lengthMm('50%'), null);
  const pg = page(el('svg', { width: '100mm', viewBox: '0 0 200 100' }));
  assert.deepEqual(pg.box, [0, 0, 200, 100]);
  near(pg.heightMm, 50);
});

test('styles: defaults, inheritance, class rules, display none, use', () => {
  const style = el('style');
  style.text = '.outline { fill: none; stroke: #000 } #gone{display:none}';
  const tree = el('svg', { viewBox: '0 0 100 100' },
    style,
    el('defs', {}, el('rect', { id: 'r', width: 10, height: 10 })),
    el('g', { fill: 'none', stroke: 'red' }, el('circle', { r: 5 })),
    el('rect', { class: 'outline', width: 1, height: 1 }),
    el('rect', { id: 'gone', width: 1, height: 1 }),
    el('use', { href: '#r', x: 50 }),
    el('text', {}, el('tspan'))
  );
  const { shapes, skipped } = collect(tree);
  assert.equal(shapes.length, 3);   // circle, .outline rect, the used rect (not the one in defs)
  assert.deepEqual([shapes[0].fill, shapes[0].stroke], [false, true]);
  assert.deepEqual([shapes[1].fill, shapes[1].stroke], [false, true]);
  assert.deepEqual([shapes[2].fill, shapes[2].stroke], [true, false]);   // SVG defaults
  near(shapes[2].matrix[4], 50);
  assert.deepEqual(skipped, { text: 1 });
});

test('hatch: square filled edge to edge, evenodd hole left empty, lines joined', () => {
  const square = [[0, 0], [10, 0], [10, 10], [0, 10]];
  const lines = hatch([square], 'nonzero', 1, 0);
  assert.equal(lines.length, 1);   // one zigzag, no pen lift
  const total = length(lines[0]);
  near(total, 10 * 10 + 9 * 1, 1e-6);   // ten lines, nine connectors

  const hole = [[3, 3], [7, 3], [7, 7], [3, 7]];
  const holed = hatch([square, hole], 'evenodd', 1, 0);
  for (const l of holed) {
    for (let i = 1; i < l.length; i++) {
      const mx = (l[i][0] + l[i - 1][0]) / 2, my = (l[i][1] + l[i - 1][1]) / 2;
      // Connectors run along the outer edge, so "inside" would be wrong on
      // them; what must never happen is a stroke across the hole.
      assert.ok(!(mx > 3 && mx < 7 && my > 3 && my < 7), `stroke through the hole at ${mx},${my}`);
    }
  }
});

test('hatch at an angle covers the same length', () => {
  const square = [[0, 0], [20, 0], [20, 20], [0, 20]];
  const total = hatch([square], 'nonzero', 1, 45).reduce((s, l) => s + length(l), 0);
  // Area / spacing, plus connectors; within a few lines' worth.
  assert.ok(Math.abs(total - 400) < 40, `${total}`);
});

test('clip splits a line that leaves and comes back', () => {
  const parts = clip([[-5, 5], [5, 5], [15, 5], [15, 8], [5, 8]], 10, 10);
  assert.equal(parts.length, 2);
  assert.deepEqual(parts[0], [[0, 5], [5, 5], [10, 5]]);
  assert.deepEqual(parts[1], [[10, 8], [5, 8]]);
});

test('order: nearest first, open lines reversed, loops entered at the nearest vertex', () => {
  const far = { points: [[100, 0], [90, 0]], closed: false };
  const near1 = { points: [[20, 0], [10, 0]], closed: false };
  const loop = { points: [[50, 10], [60, 10], [60, 0], [50, 0]], closed: true };
  const out = order([far, loop, near1], 0, 0);
  assert.deepEqual(out[0], [[10, 0], [20, 0]]);
  assert.deepEqual(out[1], [[50, 0], [50, 10], [60, 10], [60, 0], [50, 0]]);
  assert.deepEqual(out[2], [[90, 0], [100, 0]]);
});

test('order scales: 20000 short strokes', () => {
  const lines = [];
  for (let i = 0; i < 20000; i++) {
    const x = (i * 7919) % 1000, y = (i * 104729) % 700;
    lines.push({ points: [[x, y], [x + 1, y]], closed: false });
  }
  const start = Date.now();
  assert.equal(order(lines, 0, 0).length, 20000);
  assert.ok(Date.now() - start < 3000);
});

test('convert: a square fitted to the area makes a Mural job', () => {
  const tree = el('svg', { viewBox: '0 0 10 10' }, el('rect', { width: 10, height: 10, fill: 'none', stroke: 'black' }));
  const r = convert(tree, { width: 600, maxY: 1000 }, { x: 300, y: 350 }, { widthPercent: 50 });
  assert.equal(r.error, undefined);
  const lines = r.text.trim().split('\n');
  assert.match(lines[0], /^d\d/);
  assert.equal(lines[1], 'h300');
  assert.equal(lines.filter((l) => l === 'p1').length, 1);
  assert.equal(lines[lines.length - 1], 'p0');
  // 150..450 across, 0..300 down; a 1200 mm loop.
  near(r.stats.drawMm, 1200, 1e-6);
  for (const l of lines.filter((l) => /^[\d.]+ [\d.]+$/.test(l))) {
    const [x, y] = l.split(' ').map(Number);
    assert.ok(x >= 150 && x <= 450 && y >= 0 && y <= 300, l);
  }
  // The loop is entered at the corner nearest home and closes there.
  const coords = lines.filter((l) => /^[\d.]+ [\d.]+$/.test(l));
  assert.equal(coords[0], coords[coords.length - 1]);
});

test('convert: actual size, clipping warned, empty drawing refused', () => {
  const big = el('svg', { width: '800mm', height: '100mm', viewBox: '0 0 800 100' },
    el('line', { x1: 0, y1: 50, x2: 800, y2: 50, stroke: 'black' }));
  const r = convert(big, { width: 600, maxY: 1000 }, { x: 300, y: 350 }, { sizeMode: 'actual' });
  assert.ok(r.warnings.some((w) => w.code === 'clipped'));
  near(r.stats.drawMm, 600, 1e-6);
  const empty = convert(el('svg', { viewBox: '0 0 10 10' }, el('text')), { width: 600, maxY: 1000 }, { x: 300, y: 350 });
  assert.equal(empty.error, 'svgEmpty');
  assert.deepEqual(empty.warnings, [{ code: 'skipped', tag: 'text', count: 1 }]);
});
