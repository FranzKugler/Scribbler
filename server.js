/**
 * server
 * A stand-in for the device, so the web UI can be worked on without one.
 *
 * `npm run mock` starts it on :8080 and `npm run dev` proxies the endpoints to
 * it (see API_ROUTES in vite.config.js). It implements the same contract the
 * firmware does - the same paths, the same shapes, the same refusals - which
 * is the only thing that makes it useful: a mock that is merely plausible
 * teaches the UI to expect something the device does not send.
 *
 * The two modes the device has are both here, chosen at startup:
 *
 *     npm run mock            normal operation, the configuration SPA
 *     npm run mock -- portal  the setup portal
 *
 * What it does not simulate: flash wear, a heap running out, or an update that
 * fails halfway. Those are the failures worth having a real device for.
 *
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  31.8.2026
 * @updated  31.8.2026
 */
import express from 'express';

const app = express();
app.use(express.json({ limit: '1mb' }));

// 8080 by default, which is what vite.config.js proxies to. Overridable so a
// second mock - another project's, say - can run alongside this one.
const PORT = Number(process.env.PORT ?? 8080);
const PORTAL_MODE = process.argv.includes('portal');
const BOOTED = Date.now();

/*
 * In portal mode nothing but the portal answers - which is not a detail, it is
 * the security property the real firmware has: an open access point that
 * anyone within radio range can join is not somewhere to expose a filesystem
 * explorer or a firmware upload, so main.cpp registers either the portal's
 * routes or the others and never both. A mock that answered everything would
 * let a mistake here look fine and fail on the device.
 */
app.use((req, res, next) => {
  if (!PORTAL_MODE || req.path.startsWith('/portal/')) return next();
  res.status(404).json({ error: 'fsNotFound' });
});

/* ------ the state a device would keep ------ */

const settings = {
  hostname: 'Scribbler',
  channel: 0,
  autoUpdate: false,
  checkInterval: 24
};

// Expert mode. Not hashed here - the point of this file is the shape of the
// answers, and a mock that made a real hash would only be slower to read.
const expert = { password: null, unlocked: false, failures: 0 };

const grace = () => Math.max(0, Math.round((300000 - (Date.now() - BOOTED)) / 1000));

const expertState = () => ({
  enrolled: expert.password !== null,
  unlocked: expert.unlocked,
  grace: grace(),
  lockedOut: expert.failures >= 5
});

/** The one line at the top of every handler expert mode covers. */
function guard(res) {
  if (expert.unlocked) return true;
  res.status(403).json({ error: 'expertLocked' });
  return false;
}

const NETWORKS = [
  { ssid: 'FRITZ!Box 7590', rssi: -48, secure: true },
  { ssid: 'FRITZ!Box 7590', rssi: -61, secure: true }, // a repeat, on purpose
  { ssid: 'Gaeste-WLAN', rssi: -66, secure: true },
  { ssid: 'Nachbar', rssi: -79, secure: false },
  { ssid: '', rssi: -71, secure: true } // hidden, and unpickable
];

// A scan takes a moment on a device, so it takes one here too - the UI polls
// for it, and a mock that answered at once would never exercise that path.
let scanStartedAt = 0;
function scanAnswer() {
  if (scanStartedAt === 0) {
    scanStartedAt = Date.now();
    return { scanning: true };
  }
  if (Date.now() - scanStartedAt < 2500) return { scanning: true };
  scanStartedAt = 0;
  return { scanning: false, networks: NETWORKS };
}

/* ------ settings, status, WLAN ------ */

app.get('/currentState', (req, res) =>
  res.json({
    hostname: settings.hostname,
    firmwareVersion: '0.1.0-mock',
    webUiVersion: '0.1.0',
    uptime: Date.now() - BOOTED,
    ip: '192.168.178.42'
  })
);

app.post('/hostname', (req, res) => {
  const wanted = String(req.body?.hostname ?? '')
    .replace(/[^A-Za-z0-9-]/g, '')
    .replace(/^-+|-+$/g, '')
    .slice(0, 32);
  if (!wanted) return res.status(400).json({ error: 'hostnameInvalid' });

  const restarting = wanted !== settings.hostname;
  settings.hostname = wanted;
  res.json({ hostname: wanted, restarting });
});

app.post('/restart', (req, res) => {
  if (!guard(res)) return;
  res.json({ restarting: true });
});

app.get('/wifi', (req, res) =>
  res.json({
    connected: true,
    ssid: 'FRITZ!Box 7590',
    ip: '192.168.178.42',
    rssi: -52,
    mac: '84:fc:e6:00:11:22',
    hostname: settings.hostname,
    switching: false,
    error: '',
    errorDetail: ''
  })
);

app.get('/wifi/scan', (req, res) => res.json(scanAnswer()));

app.post('/wifi', (req, res) => {
  if (!req.body?.ssid) return res.status(400).json({ error: 'wifiNoSsid' });
  res.json({ msg: '' });
});

/* ------ the setup portal ------ */

const portal = { state: 'idle', ssid: '', error: '', errorDetail: '' };

app.get('/portal/status', (req, res) =>
  res.json({
    portal: true,
    apName: settings.hostname,
    hostname: settings.hostname,
    state: portal.state,
    ssid: portal.ssid,
    ip: portal.state === 'connected' ? '192.168.178.42' : '',
    error: portal.error,
    errorDetail: portal.errorDetail
  })
);

app.get('/portal/scan', (req, res) => res.json(scanAnswer()));

app.post('/portal/connect', (req, res) => {
  if (!req.body?.ssid) return res.status(400).json({ error: 'wifiNoSsid' });
  if (portal.state === 'connecting') return res.status(409).json({ error: 'portalBusy' });

  portal.ssid = req.body.ssid;
  portal.state = 'connecting';
  portal.error = '';
  portal.errorDetail = '';

  // "Nachbar" is the one that fails, so the failure path can be looked at
  // without unplugging a router.
  const willFail = portal.ssid === 'Nachbar';
  setTimeout(() => {
    portal.state = willFail ? 'failed' : 'connected';
    if (willFail) {
      portal.error = 'wifiConnect';
      portal.errorDetail = portal.ssid;
    }
  }, 5000);

  res.json({ state: portal.state, ssid: portal.ssid });
});

/* ------ expert mode ------ */

app.get('/expert', (req, res) => res.json(expertState()));

app.post('/expert', (req, res) => {
  const body = req.body ?? {};

  if (body.off) {
    expert.unlocked = false;
    return res.json(expertState());
  }

  if (body.reset) {
    if (grace() === 0) return res.status(403).json({ error: 'expertNoGrace' });
    expert.password = null;
    expert.unlocked = false;
    expert.failures = 0;
    return res.json(expertState());
  }

  if (expert.failures >= 5) return res.status(429).json({ error: 'expertLockedOut' });

  const offered = String(body.password ?? '');

  if (expert.password === null) {
    if (offered.length < 6) return res.status(403).json({ error: 'expertPasswordShort' });
    expert.password = offered;
    expert.unlocked = true;
    return res.json(expertState());
  }

  if (offered !== expert.password) {
    expert.failures += 1;
    return res.status(403).json({ error: 'expertWrongPassword' });
  }

  expert.failures = 0;
  expert.unlocked = true;
  res.json(expertState());
});

/* ------ the log ------ */

const LOG_LINES = 200;
const log = [];
let seqNext = 0;

function say(level, text) {
  log.push({ s: seqNext++, t: Date.now() - BOOTED, l: level, m: text });
  while (log.length > LOG_LINES) log.shift();
}

say(3, 'Scribbler 0.1.0-mock, built by server.js');
say(2, 'LittleFS mounted');
say(3, 'Connecting to FRITZ!Box 7590');
say(3, 'Connected - local IP 192.168.178.42');
say(4, 'No OTA_PASSWORD set - espota accepts anyone on this network');
say(2, 'mDNS responder started: scribbler.local');

// Something arriving now and then, so the tab has a tail to follow.
setInterval(() => say(2, `still here, heap ${180000 + Math.round(Math.random() * 4000)}`), 5000);

app.get('/log', (req, res) => {
  if (!guard(res)) return;

  const since = Number(req.query.since ?? 0);
  const oldest = log.length ? log[0].s : seqNext;
  const from = Math.max(since, oldest);

  const batch = log.filter((line) => line.s >= from).slice(0, 100);
  const served = batch.length ? batch[batch.length - 1].s + 1 : Math.min(Math.max(from, oldest), seqNext);

  res.json({
    oldest,
    seq: served,
    more: served < seqNext,
    uptime: Date.now() - BOOTED,
    heap: 182_000,
    heapMin: 141_000,
    heapBlock: 110_000,
    reset: 'usb',
    lines: batch
  });
});

/* ------ updates ------ */

const ota = { state: 'idle', progress: 0, lastCheck: -1, available: '', notes: '' };

const otaStatus = () => ({
  firmwareVersion: '0.1.0-mock',
  fsVersion: '0.1.0',
  sketchSize: 1_312_768,
  freeSpace: 6_553_600,
  error: '',
  errorDetail: '',
  partition: 'app0',
  channel: settings.channel,
  autoUpdate: settings.autoUpdate,
  checkInterval: settings.checkInterval,
  state: ota.state,
  progress: ota.progress,
  availableVersion: ota.available,
  availableNotes: ota.notes,
  updateAvailable: ota.available !== '' && ota.available !== '0.1.0-mock',
  lastCheck: ota.lastCheck
});

app.get('/ota/status', (req, res) => {
  if (!guard(res)) return;
  res.json(otaStatus());
});

app.get('/ota/check', (req, res) => {
  if (!guard(res)) return;
  ota.lastCheck = 0;
  ota.available = '0.2.0';
  ota.notes = 'What a release note would look like.';
  ota.state = 'available';
  res.json(otaStatus());
});

app.post('/ota/install', (req, res) => {
  if (!guard(res)) return;
  if (ota.state === 'downloading') return res.status(409).json({ error: 'otaBusy' });
  if (!otaStatus().updateAvailable) return res.status(409).json({ error: 'otaNoUpdate' });

  ota.state = 'downloading';
  ota.progress = 0;
  const tick = setInterval(() => {
    ota.progress += 7;
    if (ota.progress >= 100) {
      ota.progress = 100;
      ota.state = 'installed';
      clearInterval(tick);
    }
  }, 400);

  res.json(otaStatus());
});

app.post('/ota/config', (req, res) => {
  if (!guard(res)) return;
  const body = req.body ?? {};
  if (body.channel !== undefined) {
    settings.channel = Number(body.channel);
    ota.available = '';
    ota.lastCheck = -1;
    ota.state = 'idle';
  }
  if (body.autoUpdate !== undefined) settings.autoUpdate = !!body.autoUpdate;
  if (body.checkInterval !== undefined) settings.checkInterval = Number(body.checkInterval);
  res.json(otaStatus());
});

app.post('/ota/upload', (req, res) => {
  if (!guard(res)) return;
  // The body is drained rather than parsed: what is being exercised here is
  // the progress bar in the browser, and that is XHR's own count of bytes sent.
  req.on('data', () => {});
  req.on('end', () => res.json({ msg: '', reboot: true }));
});

/* ------ LittleFS ------ */

const files = {
  '/': { dir: true },
  '/index.html': { content: '<!doctype html>\n<title>Scribbler</title>\n' },
  '/portal.html': { content: '<!doctype html>\n<title>Scribbler</title>\n' },
  '/version.json': { content: '{"version":"0.1.0","built":"2026-08-31T12:00:00.000Z"}' },
  '/favicon.svg': { content: '<svg xmlns="http://www.w3.org/2000/svg"/>' },
  '/assets': { dir: true },
  '/assets/main-a1b2c3.js': { content: 'console.log("bundle")\n' },
  '/assets/main-a1b2c3.css': { content: ':root{}\n' }
};

// The spiffs partition in partitions.csv, 0x9E0000. LittleFS reports a
// little less than this on a device - it keeps its own bookkeeping in
// there - but the fullness bar is what this feeds, and it is a bar.
const FS_TOTAL = 10_354_688;
const fsUsed = () =>
  Object.values(files).reduce((sum, entry) => sum + (entry.content?.length ?? 0), 0);

const editable = (name) => /\.(json|txt|css|html|js|csv|md)$/.test(name) || !name.includes('.');

const parentOf = (path) => {
  const cut = path.lastIndexOf('/');
  return cut <= 0 ? '/' : path.slice(0, cut);
};

app.get('/fs/list', (req, res) => {
  if (!guard(res)) return;
  const path = String(req.query.path ?? '/');
  if (!files[path]?.dir) return res.status(404).json({ error: 'fsNotFound' });

  const entries = Object.entries(files)
    .filter(([full]) => full !== '/' && parentOf(full) === path)
    .map(([full, entry]) => {
      const name = full.slice(full.lastIndexOf('/') + 1);
      return {
        name,
        dir: !!entry.dir,
        size: entry.content?.length ?? 0,
        ...(entry.dir ? {} : { edit: editable(name) })
      };
    });

  res.json({ path, total: FS_TOTAL, used: fsUsed(), editMax: 24576, entries });
});

app.get('/fs/read', (req, res) => {
  if (!guard(res)) return;
  const path = String(req.query.path ?? '');
  const entry = files[path];
  if (!entry) return res.status(404).json({ error: 'fsNotFound' });
  if (entry.dir) return res.status(400).json({ error: 'fsIsDir' });

  if (req.query.download !== undefined) {
    res.setHeader('Content-Disposition',
      `attachment; filename="${path.slice(path.lastIndexOf('/') + 1)}"`);
  }
  res.type('text/plain').send(entry.content);
});

app.post('/fs/save', (req, res) => {
  if (!guard(res)) return;
  const { path, content } = req.body ?? {};
  if (!files[path]) return res.status(404).json({ error: 'fsNotFound' });
  files[path] = { content: String(content ?? '') };
  res.json({ path, size: files[path].content.length, total: FS_TOTAL, used: fsUsed() });
});

app.post('/fs/delete', (req, res) => {
  if (!guard(res)) return;
  const { path } = req.body ?? {};
  if (!files[path]) return res.status(404).json({ error: 'fsNotFound' });
  if (files[path].dir && Object.keys(files).some((full) => parentOf(full) === path && full !== path)) {
    return res.status(409).json({ error: 'fsNotEmpty' });
  }
  delete files[path];
  res.json({ path, total: FS_TOTAL, used: fsUsed() });
});

app.post('/fs/mkdir', (req, res) => {
  if (!guard(res)) return;
  const { path } = req.body ?? {};
  if (files[path]) return res.status(409).json({ error: 'fsExists' });
  files[path] = { dir: true };
  res.json({ path });
});

app.post('/fs/upload', (req, res) => {
  if (!expert.unlocked) return res.status(403).json({ error: 'expertLocked' });
  const path = String(req.query.path ?? '');
  let size = 0;
  req.on('data', (chunk) => (size += chunk.length));
  req.on('end', () => {
    files[path] = { content: 'x'.repeat(Math.max(0, size - 200)) };
    res.json({ path, size, total: FS_TOTAL, used: fsUsed() });
  });
});

/* ------ NVS ------ */

const nvs = [
  { ns: 'scribbler', key: 'conf', type: 'str',
    value: '{"Schema":1,"Hostname":"Scribbler","OtaChannel":0,"OtaAutoUpdate":false,"OtaCheckInterval":24,' +
      '"MotorCurrent":700,"Microsteps":16,"PenUp":90,"PenDown":70,"ImuOffset":0}' },
  { ns: 'scribexpert', key: 'on', type: 'u8', value: '1' },
  { ns: 'scribexpert', key: 'hash', type: 'blob', value: null, protected: true },
  { ns: 'scribexpert', key: 'salt', type: 'blob', value: null, protected: true },
  { ns: 'nvs.net80211', key: 'sta.ssid', type: 'blob', value: null }
];

const suffixFor = (entry) => {
  if (entry.type === 'blob') return 'bin';
  if (entry.type !== 'str') return 'txt';
  return /^\s*[[{]/.test(entry.value ?? '') ? 'json' : 'txt';
};

const findNvs = (ns, key) => nvs.find((e) => e.ns === ns && e.key === key);

app.get('/nvs/list', (req, res) => {
  if (!guard(res)) return;
  res.json({
    used: 141,
    total: 630,
    namespaces: new Set(nvs.map((e) => e.ns)).size,
    editMax: 4096,
    entries: nvs.map((entry) => ({
      ns: entry.ns,
      key: entry.key,
      type: entry.type,
      size: entry.value?.length ?? 0,
      suffix: suffixFor(entry),
      edit: !entry.protected && entry.value !== null,
      ...(entry.protected ? { protected: true } : {})
    }))
  });
});

app.get('/nvs/read', (req, res) => {
  if (!guard(res)) return;
  const entry = findNvs(String(req.query.ns), String(req.query.key));
  if (!entry) return res.status(404).json({ error: 'nvsNotFound' });
  if (entry.protected) return res.status(403).json({ error: 'nvsProtected' });
  if (entry.value === null) return res.status(400).json({ error: 'nvsBinary' });
  res.type(suffixFor(entry) === 'json' ? 'application/json' : 'text/plain').send(entry.value);
});

app.post('/nvs/save', (req, res) => {
  if (!guard(res)) return;
  const { ns, key, content } = req.body ?? {};
  const entry = findNvs(ns, key);
  if (!entry) return res.status(404).json({ error: 'nvsNotFound' });
  if (entry.protected) return res.status(403).json({ error: 'nvsProtected' });
  if (entry.value === null) return res.status(400).json({ error: 'nvsBinary' });
  if (entry.type !== 'str' && !/^-?\d+$/.test(String(content).trim())) {
    return res.status(400).json({ error: 'nvsNotANumber' });
  }
  entry.value = String(content);
  res.json({ ns, key, size: entry.value.length, cached: true });
});

app.post('/nvs/delete', (req, res) => {
  if (!guard(res)) return;
  const { ns, key } = req.body ?? {};
  const at = nvs.findIndex((e) => e.ns === ns && e.key === key);
  if (at < 0) return res.status(404).json({ error: 'nvsNotFound' });
  nvs.splice(at, 1);
  res.json({ ns, key });
});

/* ------ hardware ------ */

// Same contract as src/HardwareRoutes.cpp: all of it behind the lock, refusals
// as badValue with the field in errorDetail. The right driver is "missing" on
// purpose, so the absent-driver card can be looked at without unplugging one.

const MAX_CURRENT = 1414; // 2 A range, 12k R_REF: 2 A peak = 1.41 A RMS

const hw = {
  motorsEnabled: false,
  motorCurrent: 700,
  microsteps: 16,
  pen: { angle: 90, target: 90, upAngle: 90, downAngle: 70, movedAt: 0, from: 90 }
};

function penNow() {
  // The firmware ramps at 120 degrees per second and waits 200 ms after.
  const p = hw.pen;
  const elapsed = (Date.now() - p.movedAt) / 1000;
  const span = Math.abs(p.target - p.from);
  const done = Math.min(1, span === 0 ? 1 : (elapsed * 120) / span);
  p.angle = p.from + (p.target - p.from) * done;
  return { angle: p.angle, target: p.target, down: p.target === p.downAngle && p.target !== p.upAngle,
           settled: done >= 1 && elapsed - span / 120 >= 0.2, upAngle: p.upAngle, downAngle: p.downAngle };
}

function movePen(angle) {
  hw.pen.from = penNow().angle;
  hw.pen.target = angle;
  hw.pen.movedAt = Date.now();
}

const refuse = (res, field) => res.status(400).json({ error: 'badValue', errorDetail: field });

app.get('/hardware', (req, res) => {
  if (!guard(res)) return;
  const t = Date.now() / 1000;
  // A bot swinging gently by a degree, so the IMU card has something to show.
  const tilt = Math.sin(t * 1.3) * 1.2;
  res.json({
    motorsEnabled: hw.motorsEnabled,
    motorCurrent: hw.motorCurrent,
    maxCurrent: MAX_CURRENT,
    microsteps: hw.microsteps,
    drivers: [
      { side: 'left', present: true, version: 0x40, temperature: 31.5 + Math.sin(t / 20),
        supply: 19.94, stallGuard: hw.motorsEnabled ? 312 : 0, currentScale: hw.motorsEnabled ? 16 : 0,
        standstill: true, overtemperature: false, overtemperatureWarn: false, shortToGround: false,
        openLoad: false, globalStatus: 0, drvStatus: 0x80100138 },
      { side: 'right', present: false, version: 0x00 }
    ],
    pen: penNow(),
    imu: { present: true, tilt, tiltAccel: tilt + (Math.random() - 0.5) * 0.4,
           rate: Math.cos(t * 1.3) * 1.56, // the derivative of the swing above
           accel: [Math.sin(tilt / 57.3), Math.cos(tilt / 57.3), 0.01],
           gyro: [0.1, -0.05, -Math.cos(t * 1.3) * 1.56], samples: Math.floor(t * 100) },
    display: { present: true }
  });
});

app.post('/hardware/motors', (req, res) => {
  if (!guard(res)) return;
  if (typeof req.body?.enabled !== 'boolean') return refuse(res, 'enabled');
  hw.motorsEnabled = req.body.enabled;
  say(3, `Motors ${hw.motorsEnabled ? 'on' : 'off'}`);
  res.json({ ok: true });
});

app.post('/hardware/pen', (req, res) => {
  if (!guard(res)) return;
  const { angle, state } = req.body ?? {};
  if (Number.isInteger(angle)) {
    if (angle < 0 || angle > 180) return refuse(res, 'angle');
    movePen(angle);
  } else if (state === 'up') movePen(hw.pen.upAngle);
  else if (state === 'down') movePen(hw.pen.downAngle);
  else return refuse(res, 'state');
  res.json({ ok: true });
});

app.post('/hardware/config', (req, res) => {
  if (!guard(res)) return;
  const b = req.body ?? {};
  // Validate all, then apply all - as the firmware does.
  const isPow2 = (v) => Number.isInteger(v) && v >= 1 && v <= 256 && (v & (v - 1)) === 0;
  if (b.motorCurrent !== undefined && !(Number.isInteger(b.motorCurrent) &&
      b.motorCurrent >= 100 && b.motorCurrent <= MAX_CURRENT)) return refuse(res, 'motorCurrent');
  if (b.microsteps !== undefined && !isPow2(b.microsteps)) return refuse(res, 'microsteps');
  for (const key of ['penUp', 'penDown']) {
    if (b[key] !== undefined && !(Number.isInteger(b[key]) && b[key] >= 0 && b[key] <= 180))
      return refuse(res, key);
  }
  if (b.motorCurrent !== undefined) hw.motorCurrent = b.motorCurrent;
  if (b.microsteps !== undefined) hw.microsteps = b.microsteps;
  if (b.penUp !== undefined) hw.pen.upAngle = b.penUp;
  if (b.penDown !== undefined) hw.pen.downAngle = b.penDown;
  res.json({ ok: true });
});

/* ------ the manifest the device builds itself ------ */

app.get('/manifest.webmanifest', (req, res) =>
  res.type('application/manifest+json').json({
    name: settings.hostname,
    short_name: settings.hostname,
    start_url: '/',
    display: 'standalone',
    background_color: '#0d0d0d',
    theme_color: '#0d0d0d',
    icons: [
      { src: '/icon-192.png', sizes: '192x192', type: 'image/png' },
      { src: '/icon-512.png', sizes: '512x512', type: 'image/png' },
      { src: '/icon-512-maskable.png', sizes: '512x512', type: 'image/png', purpose: 'maskable' }
    ]
  })
);

app.listen(PORT, () => {
  console.log(`Mock device on http://localhost:${PORT}`);
  console.log(PORTAL_MODE
    ? 'Setup portal mode - open /portal.html through the Vite dev server.'
    : 'Normal operation - open / through the Vite dev server.');
  console.log('Expert mode starts unenrolled: any password of six characters sets it.');
});
