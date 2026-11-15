/**
 * convert.worker
 * The web worker around pipeline.js: one request, one answer, so the tab
 * stays responsive while a large SVG is hatched and ordered.
 *
 * Request:  {id, tree, area, home, options}
 * Answer:   {id, result} or {id, error}
 *
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
import { convert } from './pipeline.js';

self.onmessage = (event) => {
  const { id, tree, area, home, options } = event.data;
  try {
    self.postMessage({ id, result: convert(tree, area, home, options) });
  } catch (err) {
    self.postMessage({ id, error: String(err?.message || err) });
  }
};
