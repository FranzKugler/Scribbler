/**
 * i18n
 * Display language of the web UI.
 *
 * There is nothing on this device for the language to follow - no display, no
 * setting - so it follows the browser, which is the next best guess about who
 * is reading. German unless the browser asks for English; anything else falls
 * back to German rather than to a half-translated page.
 *
 * That is a deliberate difference from the clock this scaffolding comes from,
 * where the UI followed the language set for the clock face. If the plotter ever
 * grows a language setting, `setLanguage()` is the one place to call from it
 * and nothing else here changes.
 *
 * Components read the texts through `dict()` inside a `$derived`, which makes
 * them re-render when the language changes:
 *
 *     const t = $derived(dict());
 *
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  31.8.2026
 * @updated  31.8.2026
 */
import de from './locales/de.js';
import en from './locales/en.js';

const DICTS = { de, en };

/** What the browser asks for, reduced to a language we have. */
function preferred() {
  const wanted = (navigator.languages ?? [navigator.language ?? 'de']).map((tag) =>
    String(tag).toLowerCase().split('-')[0]
  );
  return wanted.find((code) => code in DICTS) ?? 'de';
}

const ui = $state({ locale: preferred() });

/** Switches the UI. Only ever called with a code the dictionaries have. */
export function setLanguage(locale) {
  if (!(locale in DICTS) || locale === ui.locale) return;
  ui.locale = locale;
  // Keeps hyphenation and spell checking in step with what is on screen.
  document.documentElement.lang = locale;
}

/** Texts of the current language; German for anything unexpected. */
export function dict() {
  return DICTS[ui.locale] ?? de;
}

// The served HTML says lang="de" because that is the fallback; correct it when
// the browser asked for something else, so it is right from the first paint.
document.documentElement.lang = ui.locale;
