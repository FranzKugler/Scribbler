/**
 * status
 * Shared status of the last request to the device. `api.js` writes to it so any
 * failed write shows up in the UI instead of being silently swallowed.
 *
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  31.8.2026
 * @updated  31.8.2026
 */
export const status = $state({
  /** Message of the last failed request, or null if the last request succeeded. */
  error: null
});
