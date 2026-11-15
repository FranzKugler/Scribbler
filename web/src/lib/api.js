/**
 * api
 * REST client for the device's endpoints. The firmware and the mock server
 * (server.js) implement the same contract; keep all three in sync.
 *
 * Two conventions run through this file and are worth knowing before reading
 * it:
 *
 *   - A call that **throws** is one whose caller has to be able to tell
 *     "refused" from "unreachable" - the file explorer has a tree to refresh
 *     and a message to put next to a row, and the pollers must tolerate the
 *     device being briefly absent without turning that into a banner.
 *   - A call that goes through `post()` is fire-and-forget: it reports into
 *     the shared status banner and answers with a bare boolean.
 *
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  31.8.2026
 * @updated  31.8.2026
 */
import { status } from './status.svelte.js';
import { dict } from './i18n.svelte.js';
import { errorText } from './errors.js';

async function post(path, body) {
  try {
    const res = await fetch(path, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body)
    });
    if (!res.ok) {
      // The firmware answers a refusal with a code, and a banner reading
      // "HTTP 403" tells nobody what to do about it. A body that is not the
      // expected shape leaves the status, which is better than nothing.
      let said = null;
      try {
        const err = await res.json();
        said = errorText(dict(), err.error, err.errorDetail);
      } catch {
        /* not JSON, or no body at all */
      }
      throw new Error(said || `HTTP ${res.status}`);
    }
    status.error = null;
    return true;
  } catch (err) {
    // Writes are fire-and-forget: report, but never break the UI.
    status.error = `${path}: ${err.message}`;
    return false;
  }
}

/** The device's refusal as a sentence, or the status when it did not say. */
async function failure(res, what) {
  try {
    const err = await res.json();
    const said = errorText(dict(), err.error, err.errorDetail);
    if (said) return said;
  } catch {
    /* not JSON, or no body */
  }
  return `${what}: HTTP ${res.status}`;
}

/** Loads the settings. Throws, so the caller can offer a retry. */
export async function fetchState() {
  const res = await fetch('/currentState');
  if (!res.ok) throw new Error(`/currentState: HTTP ${res.status}`);
  status.error = null;
  return res.json();
}

/* ------ WLAN ------ */

/**
 * Current WiFi connection. Throws, because the caller polls this while the
 * device is switching networks and must tolerate it being briefly unreachable
 * without flagging that as an error to the user.
 */
export async function fetchWifi() {
  const res = await fetch('/wifi');
  if (!res.ok) throw new Error(`/wifi: HTTP ${res.status}`);
  return res.json();
}

/** One poll of the async scan: `{ scanning: true }` or `{ networks: [...] }`. */
export async function fetchWifiScan() {
  const res = await fetch('/wifi/scan');
  if (!res.ok) throw new Error(`/wifi/scan: HTTP ${res.status}`);
  return res.json();
}

export const connectWifi = ({ ssid, password }) => post('/wifi', { ssid, password });

/**
 * Renames the device. Answers with the name that was actually stored, which
 * the firmware may have reduced to a valid DNS label - so the caller should
 * show what comes back rather than what it sent.
 */
export async function setHostname(hostname) {
  const res = await fetch('/hostname', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ hostname })
  });
  if (!res.ok) {
    let code = `HTTP ${res.status}`;
    try {
      code = (await res.json()).error || code;
    } catch {
      /* not JSON */
    }
    throw new Error(code);
  }
  return res.json();
}

/* ------ the setup portal ------
 *
 * Separate routes from the ones above, and deliberately so: the portal runs in
 * access-point mode with a different state machine behind it, and the two
 * would only be one set of endpoints if they meant the same thing. The scan
 * answers in the same *shape*, which is what lets one component draw both.
 */

export async function fetchPortalStatus() {
  const res = await fetch('/portal/status');
  if (!res.ok) throw new Error(`/portal/status: HTTP ${res.status}`);
  return res.json();
}

export async function fetchPortalScan() {
  const res = await fetch('/portal/scan');
  if (!res.ok) throw new Error(`/portal/scan: HTTP ${res.status}`);
  return res.json();
}

/** Throws: the portal is the one screen where a refusal has to be shown at once. */
export async function portalConnect({ ssid, password }) {
  const res = await fetch('/portal/connect', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ ssid, password })
  });
  if (!res.ok) throw new Error(await failure(res, '/portal/connect'));
  return res.json();
}

/* ------ expert mode ------ */

/**
 * Whether the device is unlocked, whether a password has ever been set, and
 * how much of the reset window is left. Carries no secret.
 *
 * Throws: the shell asks for this before it can decide which tabs exist, and a
 * device that cannot answer has to be treated as locked rather than as open.
 */
export async function fetchExpert() {
  const res = await fetch('/expert');
  if (!res.ok) throw new Error(`/expert: HTTP ${res.status}`);
  return res.json();
}

/**
 * Sets, checks or clears the password: `{password}`, `{off: true}` or
 * `{reset: true}`.
 *
 * Answers with the same shape `fetchExpert` does, so the caller has the new
 * state without asking again - or with `{error}` naming why not. Not through
 * post(), which only reports a boolean: which of the four refusals it was is
 * the whole content of the answer here.
 */
export async function setExpert(body) {
  try {
    const res = await fetch('/expert', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body)
    });
    const answer = await res.json();
    if (!res.ok) return { error: answer.error || `HTTP ${res.status}` };
    status.error = null;
    return answer;
  } catch (err) {
    status.error = `/expert: ${err.message}`;
    return { error: 'connectionLost' };
  }
}

/* ------ the log ------ */

/**
 * The device's log, everything after the sequence number last seen.
 *
 * `since` of 0 asks for the oldest the ring still holds, which is what fills a
 * freshly opened tab with the boot rather than with "from now on". The answer
 * carries `more` while further lines are waiting, so the caller keeps asking
 * instead of collecting one batch every polling interval.
 *
 * Throws, because the caller polls this and a device that is briefly busy or
 * restarting must not turn into a permanent error banner.
 */
export async function fetchLog(since = 0) {
  const res = await fetch(`/log?since=${since}`);
  if (!res.ok) throw new Error(`/log: HTTP ${res.status}`);
  return res.json();
}

/* ------ plotter ------ */

// The calibration flow and the plotter's own controls. Each is checked by the
// device against its state; a refusal comes back as a code and lands in the
// banner through post(). The state itself arrives with /currentState.
export const setAnchor = (mm) => post('/calib/anchor', { mm });
export const confirmRetracted = () => post('/calib/retracted', {});
export const extendHome = () => post('/calib/home', {});
export const confirmHung = () => post('/calib/hung', {});
export const pulleyExtend = (mm) => post('/calib/pulley/extend', { mm });
export const pulleyMeasured = (measured) => post('/calib/pulley', { measured });
export const savePenAngles = (angles) => post('/calib/pen', angles);
export const jog = (body) => post('/jog', body);
export const goHome = () => post('/home', {});
export const stopAll = () => post('/stop', {});
export const movePen = (body) => post('/pen', body);
export const startJob = (name) => post('/plot/start', { name });
export const pauseJob = () => post('/plot/pause', {});
export const resumeJob = () => post('/plot/resume', {});
export const stopJob = () => post('/plot/stop', {});
export const setDrawSpeed = (drawSpeed) => post('/plot/config', { drawSpeed });

/** The card and the jobs on it. Throws, like the other polled reads. */
export async function fetchJobs() {
  const res = await fetch('/jobs');
  if (!res.ok) throw new Error(await failure(res, '/jobs'));
  return res.json();
}

export const jobUrl = (name) => `/jobs/read?name=${encodeURIComponent(name)}&download=1`;
export const deleteJob = (name) => post('/jobs/delete', { name });

/**
 * A job file to the card, streamed (multipart), with progress. The same
 * shape as uploadFile(), to /jobs/upload.
 */
export function uploadJob(name, file, onProgress) {
  return new Promise((resolve, reject) => {
    const body = new FormData();
    body.append('file', file, name);

    const xhr = new XMLHttpRequest();
    xhr.open('POST', `/jobs/upload?name=${encodeURIComponent(name)}`);
    xhr.upload.onprogress = (event) => {
      if (event.lengthComputable) onProgress?.(event.loaded / event.total);
    };
    xhr.onload = () => {
      if (xhr.status >= 200 && xhr.status < 300) {
        resolve(JSON.parse(xhr.responseText));
        return;
      }
      let message = `HTTP ${xhr.status}`;
      try {
        const err = JSON.parse(xhr.responseText);
        message = errorText(dict(), err.error, err.errorDetail) || message;
      } catch {
        /* not JSON */
      }
      reject(new Error(message));
    };
    xhr.onerror = () => reject(new Error(dict().connectionLost));
    xhr.onabort = () => reject(new Error(dict().uploadAborted));
    xhr.send(body);
  });
}

/* ------ hardware ------ */

/**
 * Drivers, pen, IMU and display, read fresh. Behind the expert lock. Throws,
 * because the hardware tab polls it and a device that is briefly busy must not
 * become a permanent banner.
 */
export async function fetchHardware() {
  const res = await fetch('/hardware');
  if (!res.ok) throw new Error(await failure(res, '/hardware'));
  return res.json();
}

export const setMotors = (enabled) => post('/hardware/motors', { enabled });
export const jogBelt = (side, mm) => post('/hardware/jog', { side, mm });
export const stopMotion = () => post('/hardware/stop', {});
export const setPen = (body) => post('/hardware/pen', body);
export const setHardwareConfig = (config) => post('/hardware/config', config);

/* ------ updates ------ */

/**
 * Installed firmware and web UI versions. Throws, because the caller polls this
 * while the device reboots after an update and must tolerate it being briefly
 * unreachable without flagging that as an error.
 */
export async function fetchOtaStatus() {
  const res = await fetch('/ota/status');
  if (!res.ok) throw new Error(`/ota/status: HTTP ${res.status}`);
  return res.json();
}

/**
 * Asks the device to poll its release channel. Takes a second or two, since it
 * fetches the manifest while answering. Returns the same shape as /ota/status.
 */
export async function checkForUpdate() {
  const res = await fetch('/ota/check');
  if (!res.ok) throw new Error(`/ota/check: HTTP ${res.status}`);
  return res.json();
}

/**
 * Starts the download. Answers immediately - the device installs in a task of
 * its own, so the caller polls /ota/status for progress.
 */
export async function installUpdate() {
  const res = await fetch('/ota/install', { method: 'POST' });
  if (!res.ok) {
    let message = `HTTP ${res.status}`;
    try {
      message = (await res.json()).error || message;
    } catch {
      /* not JSON */
    }
    throw new Error(message);
  }
  return res.json();
}

/** Channel, automatic updates and check interval. */
export async function setOtaConfig(config) {
  const res = await fetch('/ota/config', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(config)
  });
  if (!res.ok) throw new Error(`/ota/config: HTTP ${res.status}`);
  return res.json();
}

/**
 * Uploads a firmware or filesystem image; the device decides from the image
 * itself where it belongs and reboots into it.
 *
 * Uses XMLHttpRequest rather than fetch(), because only XHR reports upload
 * progress. That number is honest here: the device writes every chunk to flash
 * as it arrives, so bytes sent really are bytes flashed.
 */
export function uploadImage(file, onProgress) {
  return new Promise((resolve, reject) => {
    const body = new FormData();
    body.append('image', file, file.name);

    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/ota/upload');

    xhr.upload.onprogress = (event) => {
      if (event.lengthComputable) onProgress(event.loaded / event.total);
    };

    xhr.onload = () => {
      if (xhr.status >= 200 && xhr.status < 300) {
        resolve();
        return;
      }
      // The device answers failures as {"error": "..."}; fall back to the
      // status code if the body is something else entirely.
      let message = `HTTP ${xhr.status}`;
      try {
        message = JSON.parse(xhr.responseText).error || message;
      } catch {
        /* not JSON */
      }
      reject(new Error(message));
    };

    // Fires when the connection dies mid-upload, which on a device that is
    // being reflashed is not necessarily a failure - the caller checks back.
    xhr.onerror = () => reject(new Error(dict().connectionLost));
    xhr.onabort = () => reject(new Error(dict().uploadAborted));

    xhr.send(body);
  });
}

/* ------ the filesystem, for the explorer in the storage tab ------
 *
 * These all throw rather than going through post(), which reports into the
 * banner and answers with a bare boolean. A file explorer needs to know
 * whether the delete happened - it has a tree to refresh and a message to put
 * next to the file - and "something went wrong somewhere on the page" is not
 * that. errorText() still turns the device's code into a sentence.
 *
 * This is LittleFS, not NVS: the partition the web UI itself is served from.
 */

/** Everything the device says about one directory, plus how full it is. */
export async function fetchDirectory(path = '/') {
  const res = await fetch(`/fs/list?path=${encodeURIComponent(path)}`);
  if (!res.ok) throw new Error(await failure(res, '/fs/list'));
  return res.json();
}

/** One file as text, for the editor. Binary files are downloaded instead. */
export async function fetchFile(path) {
  const res = await fetch(`/fs/read?path=${encodeURIComponent(path)}`);
  if (!res.ok) throw new Error(await failure(res, '/fs/read'));
  return res.text();
}

/**
 * Where to point a download link.
 *
 * A plain link rather than a fetch and a blob: expert mode is a flag in the
 * device's NVS, not a header this page adds, so an ordinary GET is already
 * authorised and the browser saves the file itself with no copy in memory.
 */
export const fileUrl = (path) =>
  `/fs/read?path=${encodeURIComponent(path)}&download=1`;

export const saveFile = (path, content) => fsWrite('/fs/save', { path, content });
export const deleteEntry = (path) => fsWrite('/fs/delete', { path });
export const makeDirectory = (path) => fsWrite('/fs/mkdir', { path });

/**
 * Sends one file to the device, as multipart so the firmware can stream it
 * into flash instead of holding it in the heap.
 *
 * XMLHttpRequest for the same reason uploadImage() uses it: only XHR reports
 * upload progress, and here too the number is honest - the device writes each
 * chunk as it arrives.
 */
export function uploadFile(path, file, onProgress) {
  return new Promise((resolve, reject) => {
    const body = new FormData();
    body.append('file', file, file.name);

    const xhr = new XMLHttpRequest();
    xhr.open('POST', `/fs/upload?path=${encodeURIComponent(path)}`);

    xhr.upload.onprogress = (event) => {
      if (event.lengthComputable) onProgress?.(event.loaded / event.total);
    };

    xhr.onload = () => {
      if (xhr.status >= 200 && xhr.status < 300) {
        resolve(JSON.parse(xhr.responseText));
        return;
      }
      let message = `HTTP ${xhr.status}`;
      try {
        const err = JSON.parse(xhr.responseText);
        message = errorText(dict(), err.error, err.errorDetail) || message;
      } catch {
        /* not JSON */
      }
      reject(new Error(message));
    };

    xhr.onerror = () => reject(new Error(dict().connectionLost));
    xhr.onabort = () => reject(new Error(dict().uploadAborted));

    xhr.send(body);
  });
}

async function fsWrite(path, body) {
  const res = await fetch(path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body)
  });
  if (!res.ok) throw new Error(await failure(res, path));
  return res.json();
}

/* ------ NVS, shown as a two-level tree beside the filesystem ------
 *
 * A namespace is not a folder and a key is not a file; see src/NvsRoutes.h for
 * how far that pretence is carried and exactly where it stops. These are the
 * four calls it rests on, and they take the namespace and key apart rather
 * than a path, because that is what the store actually has.
 */

/** Every entry in the partition, in one answer. */
export async function fetchNvs() {
  const res = await fetch('/nvs/list');
  if (!res.ok) throw new Error(await failure(res, '/nvs/list'));
  return res.json();
}

/** One value as text. Blobs have none and are downloaded instead. */
export async function readNvs(ns, key) {
  const res = await fetch(nvsQuery(ns, key));
  if (!res.ok) throw new Error(await failure(res, '/nvs/read'));
  return res.text();
}

export const nvsUrl = (ns, key) => `${nvsQuery(ns, key)}&download=1`;

export const saveNvs = (ns, key, content) => fsWrite('/nvs/save', { ns, key, content });
export const deleteNvs = (ns, key) => fsWrite('/nvs/delete', { ns, key });

const nvsQuery = (ns, key) =>
  `/nvs/read?ns=${encodeURIComponent(ns)}&key=${encodeURIComponent(key)}`;

/**
 * Restarts the device.
 *
 * Its one caller is the NVS panel, where it is the answer to that panel's own
 * warning: the firmware holds its records in RAM and writes them back on the
 * next settings change, so an edit made there is only durable if the device is
 * restarted before anything else touches it.
 *
 * Throws, so the button can say what went wrong. The connection dying right
 * afterwards is not a failure - the answer goes out before the restart.
 */
export async function restartDevice() {
  const res = await fetch('/restart', { method: 'POST' });
  if (!res.ok) throw new Error(await failure(res, '/restart'));
  return res.json();
}
