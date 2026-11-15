/**
 * de
 * German texts of the web UI, and the reference the English file follows:
 * same keys, same order, same grouping.
 *
 * Anything the firmware can refuse with is an `err_*` entry, keyed by the code
 * the device sends. A value that is a function takes the untranslated detail -
 * an SSID, an HTTP status, the Update library's own message - and puts it into
 * the sentence; a plain string gets the detail appended in brackets instead.
 * See lib/errors.js.
 *
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  31.8.2026
 * @updated  31.8.2026
 */
export default {
  // --- shell ---
  tabs: ['Status', 'WLAN', 'Hardware', 'Update', 'Debug', 'Speicher'],
  loading: 'Einstellungen werden geladen …',
  loadingShort: 'wird geladen …',
  deviceUnreachable: 'Gerät nicht erreichbar',
  retry: 'Erneut versuchen',
  writeFailed: 'Übertragung fehlgeschlagen',

  // --- status tab ---
  statusTitle: 'Gerät',
  deviceName: 'Name',
  address: 'Adresse',
  firmware: 'Firmware',
  webUi: 'Weboberfläche',
  uptime: 'Laufzeit',
  statusHint:
    'Die Plotter-Funktionen sind noch nicht implementiert. Was hier steht, ' +
    'ist das Gerät selbst — Name, Adresse und die beiden Images, die ' +
    'unabhängig voneinander aktualisiert werden.',

  // --- wlan tab ---
  connection: 'Verbindung',
  network: 'Netz',
  signal: 'Signal',
  hostname: 'Hostname',
  mac: 'MAC-Adresse',
  quality: ['schwach', 'mäßig', 'gut', 'sehr gut'],
  encrypted: 'verschlüsselt',
  statusUnavailable: 'Status nicht verfügbar',

  name: 'Name',
  saveAndRestart: 'Speichern und neu starten',
  restarting: 'Neustart …',
  hostnameSaved: (name) => `Gespeichert als „${name}“.`,
  hostnameHint: () =>
    'Der Name entscheidet, unter welcher Adresse das Gerät erreichbar ist ' +
    '(<name>.local), wie der Router es auflistet und wie das Einrichtungs-' +
    'WLAN heißt. Erlaubt sind Buchstaben, Ziffern und Bindestriche. Zwei ' +
    'Geräte im selben Netz brauchen zwei Namen.',

  availableNetworks: 'Verfügbare Netze',
  scanning: 'Suche läuft …',
  noNetworks: 'Kein Netz gefunden.',
  rescan: 'Neu suchen',

  switchNetwork: 'Netz wechseln',
  password: 'Passwort',
  passwordPlaceholder: 'leer lassen, wenn offen',
  connect: 'Verbinden',
  connecting: 'Verbinde …',
  connectedTo: (ssid) => `Verbunden mit ${ssid}.`,
  noResponse: 'Keine Antwort vom Gerät.',
  wifiHint: (hostname) =>
    `Beim Wechsel ist das Gerät kurz nicht erreichbar. Klappt es nicht, geht ` +
    `es von selbst ins alte Netz zurück — diese Seite ist dann wieder unter ` +
    `http://${hostname}.local/ zu finden.`,

  // --- update tab ---
  installed: 'Installiert',
  used: 'Belegt',
  roomForUpdate: 'Platz für ein Update',
  runningFrom: 'Läuft aus',
  unknown: 'unbekannt',

  updateSource: 'Update-Quelle',
  channel: 'Kanal',
  channelStable: 'Stabil (getaggte Releases)',
  channelEdge: 'Edge (jeder Commit)',
  checkInterval: 'Prüfen alle',
  checkNever: 'nie',
  hours: (n) => (n === 1 ? '1 Stunde' : `${n} Stunden`),
  autoUpdate: 'Automatisch installieren',
  autoUpdateHint:
    'Installiert wird dann nachts zwischen 2 und 5 Uhr. Standardmäßig aus: ' +
    'ein Image, das sauber flasht und danach abstürzt, ist nur noch über USB ' +
    'zu retten.',
  available: 'Verfügbar',
  downloading: (percent) => `Wird geladen … ${percent} %`,
  installNow: 'Jetzt installieren',
  upToDate: 'Aktuell.',
  neverChecked: 'noch nie geprüft',
  justNow: 'gerade eben geprüft',
  minutesAgo: (n) => `vor ${n} min geprüft`,
  hoursAgo: (n) => `vor ${n} h geprüft`,
  checking: 'Prüfe …',
  checkNow: 'Jetzt prüfen',

  uploadImage: 'Image hochladen',
  file: 'Datei',
  noFile: 'keine ausgewählt',
  chooseFile: 'Datei auswählen',
  detectedAs: 'Erkannt als',
  firmwareImage: 'Firmware',
  filesystemImage: 'Weboberfläche',
  filesystemHint:
    'Das Dateisystem-Image ersetzt die Weboberfläche vollständig. Die ' +
    'Einstellungen liegen im NVS und bleiben davon unberührt.',
  writing: (percent) => `Wird geschrieben … ${percent} %`,
  rebooting: 'Gerät startet neu …',
  running: 'Läuft …',
  upload: 'Hochladen',
  updateDone: (firmware, webUi) =>
    `Fertig. Firmware ${firmware ?? '—'}, Weboberfläche ${webUi}.`,
  noResponseAfterReboot: 'Das Gerät hat sich nach dem Neustart nicht gemeldet.',
  buildHint:
    'firmware.bin ist das Programm, littlefs.bin die Weboberfläche. Welches ' +
    'von beiden hochgeladen wurde, erkennt das Gerät am ersten Byte.',
  connectionLost: 'Verbindung abgebrochen',
  uploadAborted: 'Upload abgebrochen',

  // --- hardware tab ---
  hwUnavailable: 'Hardware-Status nicht verfügbar',
  hwMotors: 'Motoren',
  hwMotorsEnabled: 'Motoren eingeschaltet',
  hwMotorsHint:
    'Hängt der Bot an der Wand, halten ihn die Motoren. Ausschalten lässt ihn ' +
    'an den Riemen absacken – vorher abnehmen oder festhalten.',
  hwCurrent: 'Motorstrom (mA RMS)',
  hwCurrentHint: (max) => `Höchstens ${max} mA bei diesem Messwiderstand.`,
  hwMicrosteps: 'Mikroschritte',
  hwApply: 'Übernehmen',
  hwDriver: (side) => `Treiber ${side}`,
  hwLeft: 'links',
  hwRight: 'rechts',
  hwAbsent: (version) =>
    `Kein TMC2240 am Bus (Version 0x${Number(version).toString(16).padStart(2, '0')}) – ` +
    'Versorgung, Chip-Select und SPI-Leitungen prüfen.',
  hwTemperature: 'Temperatur',
  hwSupply: 'Versorgung',
  hwStallGuard: 'StallGuard',
  hwCurrentScale: 'Stromskala',
  hwStandstill: 'Stillstand',
  hwFaults: 'Fehler',
  hwNone: 'keine',
  hwOt: 'Übertemperatur – abgeschaltet',
  hwOtpw: 'Temperaturwarnung',
  hwS2g: 'Kurzschluss nach Masse',
  hwOl: 'offene Wicklung',
  hwReset: 'Treiber neu gestartet',
  hwDrvErr: 'Treiberfehler',
  hwUvCp: 'Unterspannung Ladungspumpe',
  hwPen: 'Stift',
  hwPenAngle: 'Servo',
  hwPenUp: 'Hoch',
  hwPenDown: 'Runter',
  hwPenTry: 'Winkel',
  hwPenMove: 'Anfahren',
  hwPenSaveUp: 'Als „hoch“ speichern',
  hwPenSaveDown: 'Als „runter“ speichern',
  hwImu: 'IMU',
  hwImuAbsent: 'Keine IMU gefunden.',
  hwTilt: 'Neigung',
  hwTiltAccel: 'Neigung (nur Beschleunigung)',
  hwRate: 'Drehrate',
  hwAccel: 'Beschleunigung [g]',
  hwGyro: 'Drehrate [°/s]',
  hwImuHint:
    'Waagrecht hängend muss die Neigung ~0° zeigen, nach rechts geneigt positiv. ' +
    'Stimmt das Vorzeichen nicht, sind die Achsen in Imu.cpp falsch zugeordnet.',
  hwDisplay: 'Display',
  hwFound: 'gefunden',
  hwMissing: 'nicht gefunden',
  yes: 'ja',
  no: 'nein',

  // --- debug tab ---
  deviceState: 'Zustand',
  lastReset: 'Letzter Neustart',
  heapFree: 'Freier Heap',
  heapMin: 'Minimum seit Start',
  heapBlock: 'Größter Block',
  heapHint:
    'Der größte zusammenhängende Block ist die Zahl, die bei einem ' +
    'fehlschlagenden Update interessant ist — nicht der freie Heap insgesamt.',
  resetReasons: {
    'power-on': 'Einschalten',
    external: 'extern',
    software: 'Software (ESP.restart)',
    panic: 'Absturz (Panic)',
    'watchdog-int': 'Watchdog (Interrupt)',
    'watchdog-task': 'Watchdog (Task)',
    watchdog: 'Watchdog',
    brownout: 'Unterspannung',
    sdio: 'SDIO',
    'deep-sleep': 'Deep Sleep',
    usb: 'USB (Flashen)',
    jtag: 'JTAG',
    efuse: 'eFuse',
    'power-glitch': 'Spannungseinbruch',
    'cpu-lockup': 'CPU-Lockup',
    unknown: 'unbekannt'
  },

  logTitle: 'Protokoll',
  logPause: 'Anhalten',
  logResume: 'Weiter',
  logClear: 'Fenster leeren',
  logMissed: (n) => `${n} Zeilen sind aus dem Ringpuffer gelaufen und fehlen hier.`,
  logEmpty: 'Noch nichts protokolliert.',
  logHint:
    'Der Ring im Gerät fasst 200 Zeilen und beginnt bei der ersten Zeile von ' +
    'setup() — auch die Sekunden nach einem Neustart, bei denen sonst nie ' +
    'jemand zusieht. Die Zeit ist die Laufzeit seit dem Start, nicht die Uhrzeit.',

  // --- storage tab ---
  storageTitle: 'Speicher',
  storageFs: 'LittleFS',
  storageNvs: 'NVS',
  storageFsHint:
    'Das Dateisystem, aus dem diese Seite selbst ausgeliefert wird: die ' +
    'Weboberfläche, das Einrichtungsportal, die Icons, version.json.',
  storageFsWarn:
    'Ein Dateisystem-Update überschreibt diese Partition vollständig. Wer ' +
    'index.html löscht, erreicht das Gerät nur noch über die REST-Endpunkte — ' +
    'zurück geht es dann mit „pio run -t uploadfs“ über USB.',
  storageNvsHint:
    'Der Schlüssel-Wert-Speicher: hier liegen die Einstellungen und der ' +
    'Expert-Modus. Namespaces werden als Ordner, Schlüssel als Dateien ' +
    'gezeigt — eine Lesehilfe, keine Tatsache.',
  storageNvsWarn:
    'Die Firmware hält ihre Einstellungen im RAM und schreibt sie beim ' +
    'nächsten Mal zurück. Eine Änderung hier hält also nur, wenn das Gerät ' +
    'neu startet, bevor etwas anderes sie überschreibt.',

  // --- explorer, shared by both stores ---
  fsRoot: '/',
  fsEmpty: 'Leer.',
  fsTruncated: '… weitere Einträge nicht angezeigt',
  fsUpload: 'Hochladen',
  fsUploading: (percent) => `Lädt … ${percent} %`,
  fsNewFolder: 'Neuer Ordner',
  fsNewFolderHere: 'Ordner hier anlegen',
  fsFolderName: 'Ordnername',
  fsSave: 'Speichern',
  fsCancel: 'Abbrechen',
  fsSaved: 'Gespeichert.',
  fsDelete: 'Löschen',
  fsDownload: 'Herunterladen',
  fsEdit: 'Bearbeiten',
  fsBinary: 'Nicht als Text bearbeitbar',
  fsProtected: 'Geschützt',
  fsConfirmDelete: (name) => `„${name}“ wirklich löschen?`,
  fsGesture: 'Rechtsklick oder langes Drücken öffnet das Menü zu einem Eintrag.',
  fsUsage: (used, total) => `${used} von ${total} belegt`,
  fsUsageEntries: (used, total) => `${used} von ${total} Einträgen belegt`,
  fsKeys: (n) => (n === 1 ? '1 Schlüssel' : `${n} Schlüssel`),
  fsCompact: 'kompakt speichern',
  fsNotJson: 'kein gültiges JSON',
  fsConfirmRestart: 'Gerät jetzt neu starten?',
  fsRestart: 'Neu starten',
  fsRestarting: 'Startet …',
  fsRestarted: 'Neustart angefordert — das Gerät ist gleich wieder da.',

  // --- expert mode ---
  expertTitle: 'Expertenmodus',
  expertUnlocked: 'Das Gerät ist entsperrt. Update, Debug und Speicher sind offen.',
  expertLock: 'Sperren',
  expertEnterHint: 'Passwort eingeben, um Update, Debug und Speicher zu öffnen.',
  expertSetHint:
    'Für dieses Gerät ist noch kein Passwort gesetzt. Das erste, das hier ' +
    'eingegeben wird, gilt ab sofort.',
  expertPassword: 'Passwort',
  expertUnlock: 'Entsperren',
  expertSet: 'Passwort setzen',
  expertMinLength: (n) => `Mindestens ${n} Zeichen.`,
  expertLockedOut:
    'Zu viele Fehlversuche. Das Gerät nimmt für ein paar Minuten keine ' +
    'weiteren Eingaben an.',
  expertForgotten: 'Passwort vergessen',
  expertResetHint: (time) =>
    `Nach dem Einschalten am Stromnetz lässt sich das Passwort fünf Minuten ` +
    `lang ohne Kenntnis löschen. Verbleibend: ${time}. Danach hilft nur noch ` +
    `Strom aus und wieder an.`,
  expertReset: 'Passwort löschen',

  // --- setup portal ---
  portalTitle: 'Einrichtung',
  portalIntro: (apName) =>
    `Dieses Gerät ist noch mit keinem WLAN verbunden und spannt deshalb ` +
    `selbst eines auf („${apName}“). Netz auswählen, Passwort eingeben — ` +
    `danach startet es neu und ist im gewählten Netz erreichbar.`,
  portalConnecting: (ssid) => `Verbinde mit ${ssid} …`,
  portalConnected: (ssid, ip) =>
    `Verbunden mit ${ssid}. Das Gerät startet neu und ist dann unter ${ip} erreichbar.`,
  portalDone:
    'Dieses Einrichtungs-WLAN verschwindet gleich. Danach das gewohnte Netz ' +
    'wählen und die genannte Adresse aufrufen.',

  // --- what the device can refuse with ---
  err_badValue: (field) => `Kein gültiger Wert: ${field}.`,
  err_wifiConnect: (ssid) => `Verbindung zu ${ssid} nicht möglich.`,
  err_wifiFallback: (ssid) => `Auch das vorherige Netz (${ssid}) antwortet nicht mehr.`,
  err_wifiNoSsid: 'Es wurde kein Netz angegeben.',
  err_hostnameInvalid: 'Der Name enthält keine verwertbaren Zeichen.',
  err_portalBusy: 'Es läuft bereits ein Verbindungsversuch.',
  err_connectionLost: 'Verbindung zum Gerät verloren.',

  err_expertLocked: 'Das Gerät ist gesperrt.',
  err_expertLockedOut: 'Zu viele Fehlversuche — bitte einige Minuten warten.',
  err_expertWrongPassword: 'Falsches Passwort.',
  err_expertPasswordShort: 'Das Passwort ist zu kurz.',
  err_expertNoGrace: 'Das Zeitfenster zum Zurücksetzen ist geschlossen.',

  err_otaBegin: 'Das Update konnte nicht begonnen werden.',
  err_otaWrite: 'Schreiben in den Flash fehlgeschlagen.',
  err_otaIncomplete: 'Das Image wurde nicht akzeptiert.',
  err_otaAborted: 'Der Upload wurde abgebrochen.',
  err_otaNoImage: 'Die Anfrage enthielt kein Image.',
  err_otaNoUpdate: 'Es liegt kein Update vor.',
  err_otaBusy: 'Es läuft bereits ein Update.',
  err_otaServer: 'Der Update-Server ist nicht erreichbar.',
  err_otaManifestHttp: 'Das Manifest konnte nicht geladen werden.',
  err_otaManifestParse: 'Das Manifest ist unlesbar.',
  err_otaDownload: 'Der Download ist fehlgeschlagen.',
  err_otaConnectionLost: 'Die Verbindung brach während des Downloads ab.',
  err_otaSize: 'Das Image hat nicht die angekündigte Größe.',
  err_otaChecksum: 'Die Prüfsumme stimmt nicht — das Image wurde verworfen.',

  err_fsPath: 'Unzulässiger Pfad.',
  err_fsBody: 'Die Anfrage war nicht lesbar.',
  err_fsNotFound: 'Nicht gefunden.',
  err_fsNotDir: 'Das ist kein Ordner.',
  err_fsIsDir: 'Das ist ein Ordner.',
  err_fsOpen: 'Die Datei ließ sich nicht öffnen.',
  err_fsWrite: 'Schreiben fehlgeschlagen — vermutlich ist die Partition voll.',
  err_fsRename: 'Die fertige Datei ließ sich nicht an ihren Platz schieben.',
  err_fsDelete: 'Löschen fehlgeschlagen.',
  err_fsMkdir: 'Der Ordner ließ sich nicht anlegen.',
  err_fsExists: 'Existiert bereits.',
  err_fsNotEmpty: 'Der Ordner ist nicht leer.',
  err_fsTooBig: 'Zu groß für den Editor — herunterladen und hochladen.',
  err_fsAborted: 'Der Upload wurde abgebrochen.',

  err_nvsNamespace: 'Diesen Namespace gibt es nicht.',
  err_nvsRead: 'Der Wert ließ sich nicht lesen.',
  err_nvsWrite: 'Der Wert ließ sich nicht schreiben.',
  err_nvsDelete: 'Der Schlüssel ließ sich nicht löschen.',
  err_nvsTooBig: 'Zu groß.',
  err_nvsBinary: 'Ein Blob hat keine Textform — nur herunterladen.',
  err_nvsNotFound: 'Diesen Schlüssel gibt es nicht.',
  err_nvsMemory: 'Zu wenig Speicher, um den Wert zu holen.',
  err_nvsProtected: 'Dieser Wert wird nicht herausgegeben.',
  err_nvsPath: 'Namespace oder Schlüssel fehlt.',
  err_nvsBody: 'Die Anfrage war nicht lesbar.',
  err_nvsNotANumber: 'Der Schlüssel enthält eine Zahl — der Text ist keine.',
  err_nvsNamespaceDelete:
    'Ein Namespace lässt sich nicht löschen; er verschwindet mit seinem ' +
    'letzten Schlüssel.'
};
