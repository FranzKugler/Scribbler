/**
 * svgtree
 * The main thread's half of the conversion: parse an SVG with the browser's
 * own XML parser and hand the worker a plain tree of {tag, attrs, children,
 * text} - a web worker has no DOMParser, and a plain tree is what the
 * worker's host tests use too. A photo is decoded and scaled here too, into
 * grey values. Then run the worker.
 *
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  16.11.2026
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

// Enough for the squiggle: a row is sampled every half millimetre or so, and
// 400 px across a 600 mm drawing is already finer than the waves.
const IMAGE_MAX_PX = 400;

/**
 * An image file -> {gray, width, height}: 0 black .. 1 white, transparency
 * over white paper. Throws Error('imageUnreadable').
 */
export async function imageToGray(file) {
  let bitmap;
  try {
    bitmap = await createImageBitmap(file);
  } catch {
    throw new Error('imageUnreadable');
  }
  const scale = Math.min(1, IMAGE_MAX_PX / Math.max(bitmap.width, bitmap.height));
  const width = Math.max(1, Math.round(bitmap.width * scale));
  const height = Math.max(1, Math.round(bitmap.height * scale));
  const canvas = new OffscreenCanvas(width, height);
  const ctx = canvas.getContext('2d');
  ctx.fillStyle = '#fff';
  ctx.fillRect(0, 0, width, height);
  ctx.drawImage(bitmap, 0, 0, width, height);
  bitmap.close?.();
  const { data } = ctx.getImageData(0, 0, width, height);
  const gray = new Float32Array(width * height);
  for (let i = 0; i < gray.length; i++) {
    // Rec. 709 luma, on 0..1.
    gray[i] = (0.2126 * data[4 * i] + 0.7152 * data[4 * i + 1] + 0.0722 * data[4 * i + 2]) / 255;
  }
  return { gray, width, height };
}

let worker = null;
let nextId = 1;
const pending = new Map();

/**
 * Converts in the worker: `input` is {tree} for an SVG or {image} for a
 * photo. Resolves with the pipeline's result; a newer call does not cancel an
 * older one, the caller keeps the latest.
 */
export function convertInWorker(input, area, home, options) {
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
    worker.postMessage({ id, ...input, area: { ...area }, home: { ...home }, options: { ...options } });
  });
}
