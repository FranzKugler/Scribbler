/**
 * convert.worker
 * The web worker around pipeline.js: one request, one answer, so the tab
 * stays responsive while a large SVG is hatched and ordered.
 *
 * Request:  {id, tree, area, home, options} for an SVG,
 *           {id, image, area, home, options} for a photo
 * Answer:   {id, result} or {id, error}
 *
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.1
 * @created  15.11.2026
 * @updated  16.11.2026
 */
import { convert, convertImage } from './pipeline.js';

self.onmessage = (event) => {
  const { id, tree, image, area, home, options } = event.data;
  try {
    const result = image ? convertImage(image, area, home, options) : convert(tree, area, home, options);
    self.postMessage({ id, result });
  } catch (err) {
    self.postMessage({ id, error: String(err?.message || err) });
  }
};
