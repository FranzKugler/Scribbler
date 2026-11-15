/**
 * raster
 * A photo as a pen draws one: the "squiggle" look of polargraphs - one wavy
 * line per row, its amplitude following how dark the image is under it.
 * Light areas come out as straight lines, dark ones as dense waves; with
 * `continuous`, the rows are joined at alternate ends into a single line, so
 * the pen never lifts.
 *
 * The image arrives as grey values (0 black .. 1 white) on a w x h grid -
 * the main thread scales it with a canvas, the worker never decodes images.
 * Mural has a zigzag mode of this kind; this one is written fresh.
 *
 * What it is not: an image editor. Brightness and contrast are one gamma.
 *
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  16.11.2026
 * @updated  16.11.2026
 */

/** Darkness 0..1 at image coordinates (u, v), bilinear. */
function darkness(img, u, v) {
  const { gray, width: w, height: h } = img;
  const x = Math.min(Math.max(u, 0), w - 1), y = Math.min(Math.max(v, 0), h - 1);
  const x0 = Math.floor(x), y0 = Math.floor(y);
  const x1 = Math.min(x0 + 1, w - 1), y1 = Math.min(y0 + 1, h - 1);
  const fx = x - x0, fy = y - y0;
  const g = (gray[y0 * w + x0] * (1 - fx) + gray[y0 * w + x1] * fx) * (1 - fy) +
    (gray[y1 * w + x0] * (1 - fx) + gray[y1 * w + x1] * fx) * fy;
  return 1 - g;
}

/**
 * Squiggle lines for `img` placed at (left, top), `width` x `height` mm.
 *
 * @param o.lineSpacing  mm between rows
 * @param o.wavelength   mm per wave in the darkest parts
 * @param o.gamma        > 1 darker, < 1 lighter
 * @param o.continuous   one line, rows joined at the ends
 * @returns point lists in mm
 */
export function squiggle(img, left, top, width, height, o) {
  const spacing = Math.max(0.5, o.lineSpacing);
  const wave = Math.max(0.5, o.wavelength);
  const gamma = o.gamma > 0 ? o.gamma : 1;
  // Six points per wave keep it round enough at pen width; a straight run
  // is thinned later anyway.
  const dx = wave / 6;
  const amp = spacing * 0.48;   // neighbouring rows never touch
  const sx = img.width / width, sy = img.height / height;

  const rows = [];
  let phase = 0;
  for (let r = 0, y = top + spacing / 2; y < top + height; r++, y += spacing) {
    const pts = [];
    const steps = Math.max(1, Math.ceil(width / dx));
    for (let i = 0; i <= steps; i++) {
      const x = left + (width * i) / steps;
      const d = Math.pow(darkness(img, (x - left) * sx, (y - top) * sy), gamma);
      // Below a hint of grey, a straight line: paper stays paper.
      const a = d < 0.04 ? 0 : amp * d;
      if (i > 0) phase += (2 * Math.PI * (width / steps)) / wave;
      pts.push([x, y + a * Math.sin(phase)]);
    }
    if (r % 2 === 1) pts.reverse();
    rows.push(pts);
  }

  if (!o.continuous) return rows;
  // Boustrophedon: each row starts where the last ended, one spacing down.
  const line = [];
  for (const row of rows) line.push(...row);
  return line.length > 1 ? [line] : [];
}
