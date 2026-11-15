/**
 * en
 * English texts of the web UI. Same keys, same order and same grouping as
 * de.js, which is the reference - a key added there and forgotten here shows
 * up as `undefined` on screen, so the two are meant to be diffed side by side.
 *
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  31.8.2026
 * @updated  31.8.2026
 */
export default {
  // --- shell ---
  tabs: ['Status', 'Plot', 'Setup', 'WiFi', 'Hardware', 'Update', 'Debug', 'Storage'],
  loading: 'Loading the settings …',
  loadingShort: 'loading …',
  deviceUnreachable: 'Device unreachable',
  retry: 'Try again',
  writeFailed: 'Could not save',

  // --- status tab ---
  statusTitle: 'Device',
  deviceName: 'Name',
  address: 'Address',
  firmware: 'Firmware',
  webUi: 'Web interface',
  uptime: 'Uptime',
  statusHint:
    'The plotter functions are not implemented yet. What is shown here is the ' +
    'device itself - its name, its address and the two images that are ' +
    'updated independently of one another.',

  // --- wifi tab ---
  connection: 'Connection',
  network: 'Network',
  signal: 'Signal',
  hostname: 'Hostname',
  mac: 'MAC address',
  quality: ['weak', 'fair', 'good', 'excellent'],
  encrypted: 'encrypted',
  statusUnavailable: 'Status unavailable',

  name: 'Name',
  saveAndRestart: 'Save and restart',
  restarting: 'Restarting …',
  hostnameSaved: (name) => `Stored as "${name}".`,
  hostnameHint: () =>
    'The name decides the address the device answers to (<name>.local), how ' +
    'the router lists it, and what the setup network is called. Letters, ' +
    'digits and hyphens only. Two devices on one network need two names.',

  availableNetworks: 'Available networks',
  scanning: 'Scanning …',
  noNetworks: 'No network found.',
  rescan: 'Scan again',

  switchNetwork: 'Switch network',
  password: 'Password',
  passwordPlaceholder: 'leave empty if open',
  connect: 'Connect',
  connecting: 'Connecting …',
  connectedTo: (ssid) => `Connected to ${ssid}.`,
  noResponse: 'No answer from the device.',
  wifiHint: (hostname) =>
    `The device is briefly unreachable while it switches. If the new network ` +
    `does not come up it returns to the old one by itself, and this page is ` +
    `back at http://${hostname}.local/.`,

  // --- update tab ---
  installed: 'Installed',
  used: 'In use',
  roomForUpdate: 'Room for an update',
  runningFrom: 'Running from',
  unknown: 'unknown',

  updateSource: 'Update source',
  channel: 'Channel',
  channelStable: 'Stable (tagged releases)',
  channelEdge: 'Edge (every commit)',
  checkInterval: 'Check every',
  checkNever: 'never',
  hours: (n) => (n === 1 ? '1 hour' : `${n} hours`),
  autoUpdate: 'Install automatically',
  autoUpdateHint:
    'Installs between 2 and 5 in the morning. Off by default: an image that ' +
    'flashes cleanly and then crashes can only be recovered over USB.',
  available: 'Available',
  downloading: (percent) => `Downloading … ${percent} %`,
  installNow: 'Install now',
  upToDate: 'Up to date.',
  neverChecked: 'never checked',
  justNow: 'checked just now',
  minutesAgo: (n) => `checked ${n} min ago`,
  hoursAgo: (n) => `checked ${n} h ago`,
  checking: 'Checking …',
  checkNow: 'Check now',

  uploadImage: 'Upload an image',
  file: 'File',
  noFile: 'none chosen',
  chooseFile: 'Choose a file',
  detectedAs: 'Detected as',
  firmwareImage: 'Firmware',
  filesystemImage: 'Web interface',
  filesystemHint:
    'The filesystem image replaces the web interface wholesale. The settings ' +
    'live in NVS and are not affected.',
  writing: (percent) => `Writing … ${percent} %`,
  rebooting: 'The device is restarting …',
  running: 'Running …',
  upload: 'Upload',
  updateDone: (firmware, webUi) =>
    `Done. Firmware ${firmware ?? '—'}, web interface ${webUi}.`,
  noResponseAfterReboot: 'The device did not come back after the restart.',
  buildHint:
    'firmware.bin is the program, littlefs.bin the web interface. The device ' +
    'works out which one it was handed from the first byte.',
  connectionLost: 'Connection lost',
  uploadAborted: 'Upload aborted',

  // --- jobs tab ---
  jobsUnavailable: 'Plotter status unavailable',
  jobsNow: 'Current job',
  jobPhases: {
    idle: 'ended',
    checking: 'being checked …',
    running: 'drawing',
    finished: 'finished',
    failed: 'failed'
  },
  jobPaused: 'paused',
  jobFailed: (reason, line) => (line ? `${reason} (line ${line})` : reason),
  jobProgress: (done, total, pct) => `${done} of ${total} mm · ${pct} %`,
  jobsIdle: 'No job. Start one from the card.',
  jobsNotReady: 'The plotter is not set up yet - see Setup.',
  jobPause: 'Pause',
  jobResume: 'Resume',
  jobStop: 'Cancel',
  jobStopHint:
    'Pause and cancel finish what is already planned (64 moves at most) and lift the pen - the ' +
    'position stays known. Stop halts at once; the belts then have to be wound in again.',
  jobsCard: 'SD card',
  jobsNoCard: 'No SD card - insert one and restart the device.',
  jobsCardUsage: (used, total) => `${used} of ${total} used.`,
  jobsEmpty: 'No jobs on the card yet.',
  jobStart: 'Draw',
  jobDownload: 'Download',
  jobDelete: 'Delete',
  jobDeleteConfirm: (name) => `Delete ${name}?`,
  jobsTruncated: 'Not all jobs are shown.',
  jobUploading: (name) => `Uploading ${name} …`,
  jobsUploadHint: 'A job file in the Mural format (.txt).',
  jobUpload: 'Upload',
  jobsSpeedTitle: 'Speed',
  jobsSpeed: 'Drawing (mm/s)',
  jobsSpeedHint:
    'Straight lines run through without stopping, corners slow down. Moves with the pen up run faster.',

  // --- jobs tab: converter ---
  makeTitle: 'New job from an SVG',
  makeNeedsAnchor: 'Set the anchor distance in Setup first - it decides the size of the drawing area.',
  makeHint: 'Lines, shapes and fills of an SVG file. Convert text to paths first.',
  makePick: 'Choose SVG',
  makeSize: 'Size',
  makeSizeFit: 'Fit to the drawing area',
  makeSizeActual: 'As in the file',
  makeWidth: 'Width (% of the area)',
  makeOutlines: 'Draw outlines',
  makeInfill: 'Fill areas with hatching',
  makeSpacing: 'Hatch spacing (mm)',
  makeAngle: 'Hatch angle (°)',
  makeWorking: 'Converting …',
  makeStats: (w, h, draw, travel, lifts, time) =>
    `${w} × ${h} mm · drawn ${draw}, travel ${travel} · ${lifts} pen lifts · about ${time}`,
  makeWarnings: {
    skipped: (w) => `${w.count} × <${w.tag}> skipped - a pen cannot draw it (text: convert to paths).`,
    clipped: () => 'Parts outside the drawing area were cut off.',
    noSize: () => 'The file states no size - fitted to the area instead.'
  },
  makeName: 'Name on the card',
  makeSave: 'Save to card',
  makeSaved: (name) => `Saved as ${name}.`,

  // --- setup tab ---
  setupUnavailable: 'Plotter status unavailable',
  setupState: 'Plotter',
  setupStateLabel: 'State',
  plotterStates: {
    unconfigured: 'not set up',
    needsRetract: 'wind in the belts',
    retracted: 'belts at the stop',
    extendingHome: 'paying out to home length',
    atHome: 'ready to hang up',
    ready: 'ready'
  },
  setupStateHint: {
    unconfigured: 'Enter the distance between the two wall pins first.',
    needsRetract: 'Wind both belts in to the stop, then confirm.',
    retracted: 'The belt lengths are known. Calibrate the pulley if you like, then pay out to home.',
    extendingHome: 'Paying out the belts …',
    atHome: 'Hang the belt loops over the wall pins and confirm.',
    ready: 'The bot hangs at a known position.'
  },
  setupBelts: 'Belts left / right',
  setupPosition: 'Pen',
  setupTilt: 'Tilt',
  setupTiltValues: (model, imu) => `model ${model}° · IMU ${imu}°`,
  setupStop: 'Stop',
  setupAnchorTitle: 'Wall',
  setupAnchor: 'Pin distance (mm)',
  setupSave: 'Save',
  setupAnchorHint:
    'Measured centre to centre of the two pins. Changing it discards the known position.',
  setupArea: (w, h) => `Drawing area ${w} mm wide, down to ${h} mm below its top edge.`,
  setupRetractTitle: 'Wind in the belts',
  setupRetractHint:
    'Hold the bot and wind each belt in until its end sits at the stop. Small steps towards the end.',
  setupStep: 'Step',
  setupWindIn: 'Wind in',
  setupPayOut: 'Pay out',
  setupAtStop: 'Both at the stop',
  setupPulleyTitle: 'Calibrate the pulley (optional)',
  setupPulleyHint: (d) =>
    `Currently ${d} mm. Let the left belt pay out, measure how much actually came out and enter it - ` +
    'the diameter is corrected and the belt wound back in.',
  setupPulleyRun: 'Pay out (mm)',
  setupPulleyStart: 'Pay out',
  setupMeasured: (run) => `Measured for ${run} mm`,
  setupApply: 'Apply',
  setupHomeTitle: 'Pay out to home length',
  setupHomeHint:
    'Pays both belts out to the length of the home position. The bot is not on the wall yet.',
  setupExtend: 'Pay out',
  setupHangTitle: 'Hang up',
  setupHangHint: 'Put both loops over the wall pins, let go of the bot gently, then confirm.',
  setupHung: 'Hanging',
  setupMoveTitle: 'Move',
  setupUp: 'up',
  setupDown: 'down',
  setupLeft: 'left',
  setupRight: 'right',
  setupHome: 'Home',
  setupMoveHint:
    'Moves the pen within the drawing area. No straight lines yet - those come with the planner.',
  setupPenTitle: 'Pen',
  setupPenHint: 'Find the angle at which the pen just touches the wall firmly, and save it as down.',

  // --- hardware tab ---
  hwUnavailable: 'Hardware status unavailable',
  hwMotors: 'Motors',
  hwMotorsEnabled: 'Motors on',
  hwMotorsHint:
    'On the wall, the motors are what holds the bot. Switching them off lets it ' +
    'sag on its belts - take it down or hold it first.',
  hwCurrent: 'Motor current (mA RMS)',
  hwCurrentHint: (max) => `At most ${max} mA with this reference resistor.`,
  hwMicrosteps: 'Microsteps',
  hwApply: 'Apply',
  hwPulley: 'Pulley diameter (mm)',
  hwJog: 'Move belts',
  hwMotionNotReady: 'No step generator - see the debug log.',
  hwJogStep: 'Step',
  hwBelt: (side) => `Belt ${side}`,
  hwJogHint: (perMm) =>
    `Plus lengthens the belt. ${perMm} steps/mm. Counted from boot, not from a zero point.`,
  hwJogNeedsMotors: 'Switch the motors on first.',
  hwStop: 'Stop',
  hwDriver: (side) => `Driver ${side}`,
  hwLeft: 'left',
  hwRight: 'right',
  hwAbsent: (version) =>
    `No TMC2240 on the bus (version 0x${Number(version).toString(16).padStart(2, '0')}) - ` +
    'check its supply, chip select and the SPI lines.',
  hwTemperature: 'Temperature',
  hwSupply: 'Supply',
  hwStallGuard: 'StallGuard',
  hwCurrentScale: 'Current scale',
  hwStandstill: 'Standstill',
  hwFaults: 'Faults',
  hwNone: 'none',
  hwOt: 'overtemperature - shut down',
  hwOtpw: 'temperature warning',
  hwS2g: 'short to ground',
  hwOl: 'open coil',
  hwReset: 'driver restarted',
  hwDrvErr: 'driver error',
  hwUvCp: 'charge pump undervoltage',
  hwPen: 'Pen',
  hwPenAngle: 'Servo',
  hwPenUp: 'Up',
  hwPenDown: 'Down',
  hwPenTry: 'Angle',
  hwPenMove: 'Move',
  hwPenSaveUp: 'Save as up',
  hwPenSaveDown: 'Save as down',
  hwImu: 'IMU',
  hwImuAbsent: 'No IMU found.',
  hwTilt: 'Tilt',
  hwTiltAccel: 'Tilt (accelerometer only)',
  hwRate: 'Tilt rate',
  hwAccel: 'Acceleration [g]',
  hwGyro: 'Rotation [°/s]',
  hwImuHint:
    'Hanging level, the tilt must read ~0°, leaning right it must be positive. ' +
    'If the sign is wrong, the axes in Imu.cpp are mapped wrongly.',
  hwDisplay: 'Display',
  hwFound: 'found',
  hwMissing: 'not found',
  yes: 'yes',
  no: 'no',

  // --- debug tab ---
  deviceState: 'State',
  lastReset: 'Last restart',
  heapFree: 'Free heap',
  heapMin: 'Lowest since boot',
  heapBlock: 'Largest block',
  heapHint:
    'The largest contiguous block is the number that matters when an update ' +
    'fails - not the free heap as a whole.',
  resetReasons: {
    'power-on': 'power-on',
    external: 'external',
    software: 'software (ESP.restart)',
    panic: 'crash (panic)',
    'watchdog-int': 'watchdog (interrupt)',
    'watchdog-task': 'watchdog (task)',
    watchdog: 'watchdog',
    brownout: 'brownout',
    sdio: 'SDIO',
    'deep-sleep': 'deep sleep',
    usb: 'USB (flashing)',
    jtag: 'JTAG',
    efuse: 'eFuse',
    'power-glitch': 'power glitch',
    'cpu-lockup': 'CPU lockup',
    unknown: 'unknown'
  },

  logTitle: 'Log',
  logPause: 'Pause',
  logResume: 'Resume',
  logClear: 'Clear the window',
  logMissed: (n) => `${n} lines scrolled out of the ring and are missing here.`,
  logEmpty: 'Nothing logged yet.',
  logHint:
    'The ring on the device holds 200 lines and starts at the first line of ' +
    'setup() - including the seconds after a restart that nobody is ever in ' +
    'time to watch. The time is elapsed time since boot, not the time of day.',

  // --- storage tab ---
  storageTitle: 'Storage',
  storageFs: 'LittleFS',
  storageNvs: 'NVS',
  storageFsHint:
    'The filesystem this page itself is served from: the web interface, the ' +
    'setup portal, the icons, version.json.',
  storageFsWarn:
    'A filesystem update overwrites this partition wholesale. Delete ' +
    'index.html and the device still answers every REST endpoint and shows ' +
    'nothing in a browser - the way back is "pio run -t uploadfs" over USB.',
  storageNvsHint:
    'The key-value store: the settings and expert mode live here. Namespaces ' +
    'are shown as folders and keys as files, which is a reading aid rather ' +
    'than a fact.',
  storageNvsWarn:
    'The firmware holds its settings in RAM and writes them back on the next ' +
    'change. An edit here is only durable if the device restarts before ' +
    'anything else overwrites it.',

  // --- explorer, shared by both stores ---
  fsRoot: '/',
  fsEmpty: 'Empty.',
  fsTruncated: '… further entries not shown',
  fsUpload: 'Upload',
  fsUploading: (percent) => `Uploading … ${percent} %`,
  fsNewFolder: 'New folder',
  fsNewFolderHere: 'New folder here',
  fsFolderName: 'Folder name',
  fsSave: 'Save',
  fsCancel: 'Cancel',
  fsSaved: 'Saved.',
  fsDelete: 'Delete',
  fsDownload: 'Download',
  fsEdit: 'Edit',
  fsBinary: 'Not editable as text',
  fsProtected: 'Protected',
  fsConfirmDelete: (name) => `Really delete "${name}"?`,
  fsGesture: 'Right-click, or press and hold, to open an entry’s menu.',
  fsUsage: (used, total) => `${used} of ${total} used`,
  fsUsageEntries: (used, total) => `${used} of ${total} entries used`,
  fsKeys: (n) => (n === 1 ? '1 key' : `${n} keys`),
  fsCompact: 'save compact',
  fsNotJson: 'not valid JSON',
  fsConfirmRestart: 'Restart the device now?',
  fsRestart: 'Restart',
  fsRestarting: 'Restarting …',
  fsRestarted: 'Restart requested - the device will be back shortly.',

  // --- expert mode ---
  expertTitle: 'Expert mode',
  expertUnlocked: 'The device is unlocked. Update, debug and storage are open.',
  expertLock: 'Lock',
  expertEnterHint: 'Enter the password to open update, debug and storage.',
  expertSetHint:
    'No password has been set on this device yet. The first one entered here ' +
    'is the one that counts.',
  expertPassword: 'Password',
  expertUnlock: 'Unlock',
  expertSet: 'Set the password',
  expertMinLength: (n) => `At least ${n} characters.`,
  expertLockedOut:
    'Too many wrong answers. The device will not take another one for a few ' +
    'minutes.',
  expertForgotten: 'Forgotten the password',
  expertResetHint: (time) =>
    `For five minutes after the device is switched on at the plug, the ` +
    `password can be cleared without knowing it. ${time} left. After that, ` +
    `pull the plug and put it back.`,
  expertReset: 'Clear the password',

  // --- setup portal ---
  portalTitle: 'Setup',
  portalIntro: (apName) =>
    `This device is not on a network yet, so it has put up one of its own ` +
    `("${apName}"). Pick a network, enter its password - the device then ` +
    `restarts and is reachable on the network you chose.`,
  portalConnecting: (ssid) => `Connecting to ${ssid} …`,
  portalConnected: (ssid, ip) =>
    `Connected to ${ssid}. The device is restarting and will be reachable at ${ip}.`,
  portalDone:
    'This setup network is about to disappear. Rejoin your usual network and ' +
    'open the address above.',

  // --- what the device can refuse with ---
  err_notConfigured: 'Enter the wall pin distance first.',
  err_wrongState: 'Not possible in the current state.',
  err_outsideArea: 'The target lies outside the drawing area.',
  err_unreachable: 'The model finds no pose for the bot there.',
  err_noPulleyRun: 'Pay the belt out for measuring first.',
  err_motion: 'The step generator refused the move.',
  err_jobName: 'Not a valid job name.',
  err_noCard: 'No SD card.',
  err_jobNotFound: 'No such job.',
  err_jobOpen: 'The file cannot be opened.',
  err_jobEmpty: 'The file contains no move.',
  err_svgUnreadable: 'Not a readable SVG file.',
  err_svgEmpty: 'The SVG contains nothing to draw.',
  err_jobRead: 'The file could not be read.',
  err_jobWrite: 'Writing to the card failed - probably full.',
  err_jobAborted: 'The upload was aborted.',
  err_jobRename: 'The finished file could not be moved into place.',
  err_jobDelete: 'Deleting failed.',
  err_busy: 'A belt is still moving - stop it first.',
  err_motorsOff: 'The motors are switched off.',
  err_badValue: (field) => `Not a valid value: ${field}.`,
  err_wifiConnect: (ssid) => `Could not join ${ssid}.`,
  err_wifiFallback: (ssid) => `The previous network (${ssid}) does not answer either.`,
  err_wifiNoSsid: 'No network was named.',
  err_hostnameInvalid: 'The name has no usable characters in it.',
  err_portalBusy: 'A connection attempt is already running.',
  err_connectionLost: 'Lost the connection to the device.',

  err_expertLocked: 'The device is locked.',
  err_expertLockedOut: 'Too many wrong answers - wait a few minutes.',
  err_expertWrongPassword: 'Wrong password.',
  err_expertPasswordShort: 'That password is too short.',
  err_expertNoGrace: 'The reset window has closed.',

  err_otaBegin: 'The update could not be started.',
  err_otaWrite: 'Writing to flash failed.',
  err_otaIncomplete: 'The image was not accepted.',
  err_otaAborted: 'The upload was aborted.',
  err_otaNoImage: 'The request carried no image.',
  err_otaNoUpdate: 'There is no update to install.',
  err_otaBusy: 'An update is already running.',
  err_otaServer: 'The update server cannot be reached.',
  err_otaManifestHttp: 'The manifest could not be fetched.',
  err_otaManifestParse: 'The manifest is unreadable.',
  err_otaDownload: 'The download failed.',
  err_otaConnectionLost: 'The connection dropped during the download.',
  err_otaSize: 'The image is not the size it was announced as.',
  err_otaChecksum: 'The checksum does not match - the image was discarded.',

  err_fsPath: 'Not a usable path.',
  err_fsBody: 'The request was unreadable.',
  err_fsNotFound: 'Not found.',
  err_fsNotDir: 'That is not a folder.',
  err_fsIsDir: 'That is a folder.',
  err_fsOpen: 'The file could not be opened.',
  err_fsWrite: 'Writing failed - the partition is probably full.',
  err_fsRename: 'The finished file could not be moved into place.',
  err_fsDelete: 'Deleting failed.',
  err_fsMkdir: 'The folder could not be created.',
  err_fsExists: 'Already exists.',
  err_fsNotEmpty: 'The folder is not empty.',
  err_fsTooBig: 'Too big for the editor - download it and upload it back.',
  err_fsAborted: 'The upload was aborted.',

  err_nvsNamespace: 'No such namespace.',
  err_nvsRead: 'The value could not be read.',
  err_nvsWrite: 'The value could not be written.',
  err_nvsDelete: 'The key could not be erased.',
  err_nvsTooBig: 'Too big.',
  err_nvsBinary: 'A blob has no text form - download it instead.',
  err_nvsNotFound: 'No such key.',
  err_nvsMemory: 'Not enough memory to fetch the value.',
  err_nvsProtected: 'This value is not handed out.',
  err_nvsPath: 'Namespace or key missing.',
  err_nvsBody: 'The request was unreadable.',
  err_nvsNotANumber: 'The key holds a number and the text is not one.',
  err_nvsNamespaceDelete:
    'A namespace cannot be deleted; it goes with its last key.'
};
