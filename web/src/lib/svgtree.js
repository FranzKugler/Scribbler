/**
 * svgtree
 * The main thread's half of the SVG conversion: parse the file with the
 * browser's own XML parser and hand the worker a plain tree of
 * {tag, attrs, children, text} - a web worker has no DOMParser, and a plain
 * tree is what the worker's host tests use too. Then run the worker.
 *
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */

/** SVG text -> tree; throws Error('svgUnreadable') when it is not an SVG. */
export function parseSvg(text) {
  const doc = new DOMParser().parseFromString(text, 'image/svg+xml');
  const root = doc.documentElement;
  if (!root || doc.getElementsByTagName('parsererror').length > 0 || root.localName !== 'svg') {
    throw new Error('svgUnreadable');
  }
  const walk = (el) => {
    const attrs = {};
    for (const a of el.attributes) attrs[a.name] = a.value;
    const children = [];
    for (const c of el.children) children.push(walk(c));
    return { tag: el.localName, attrs, children, text: el.localName === 'style' ? el.textContent : '' };
  };
  return walk(root);
}

let worker = null;
let nextId = 1;
const pending = new Map();

/**
 * Converts in the worker. Resolves with the pipeline's result; a newer call
 * does not cancel an older one, the caller keeps the latest.
 */
export function convertInWorker(tree, area, home, options) {
  if (!worker) {
    worker = new Worker(new URL('../worker/convert.worker.js', import.meta.url), { type: 'module' });
    worker.onmessage = (event) => {
      const { id, result, error } = event.data;
      const p = pending.get(id);
      if (!p) return;
      pending.delete(id);
      if (error) p.reject(new Error(error));
      else p.resolve(result);
    };
  }
  const id = nextId++;
  return new Promise((resolve, reject) => {
    pending.set(id, { resolve, reject });
    // $state proxies do not survive structured cloning; plain copies do.
    worker.postMessage({ id, tree, area: { ...area }, home: { ...home }, options: { ...options } });
  });
}
