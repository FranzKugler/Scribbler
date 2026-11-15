/**
 * errors
 * Turns the error codes the device reports into text in the current language.
 *
 * The firmware never sends a sentence. It sends a stable code - "otaChecksum",
 * "wifiConnect" - plus an untranslated detail where there is one: an HTTP
 * status, the SSID, the Update library's own message. Which means a firmware
 * that speaks no language at all can be read in every language the UI has.
 *
 * An unknown code is shown as-is rather than swallowed: a device running newer
 * firmware than its web UI should still say something useful.
 *
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  31.8.2026
 * @updated  31.8.2026
 */

/**
 * @param t       texts of the current language, from dict()
 * @param code    what the device put in `error`
 * @param detail  what it put in `errorDetail`, may be empty
 */
export function errorText(t, code, detail) {
  if (!code) return null;

  const entry = t[`err_${code}`];
  const text = typeof entry === 'function' ? entry(detail ?? '') : entry;

  if (!text) {
    // Unknown to this build of the UI - show the raw code so it is at least
    // searchable, rather than pretending nothing went wrong.
    return detail ? `${code} (${detail})` : code;
  }
  // Codes that take the detail into the sentence have already used it.
  return typeof entry === 'function' || !detail ? text : `${text} (${detail})`;
}
