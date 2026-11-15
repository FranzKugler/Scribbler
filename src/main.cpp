/**
 * main
 * Scribbler - wall plotter on a Seeed XIAO ESP32-S3 Plus.
 *
 * What is here is the scaffolding every one of these devices needs: the boot,
 * the network, the web server and the two things that keep a device
 * maintainable once it hangs on a wall - a log that starts before anyone is
 * watching, and an update path that does not need a cable. The scaffolding is
 * DoorOpener's, renamed; see the xiao-esp32s3-project skill.
 *
 * The boot has two outcomes, and setup() is mostly the business of choosing
 * between them:
 *
 *   a network        the configuration SPA, /ota, /fs, /nvs, mDNS, espota, SNTP
 *   no network       the setup portal, and nothing else at all (see Portal.h)
 *
 * Where the plotter goes: modules of their own with a begin() and a poll(),
 * called from setup() and loop() next to the others, their settings in
 * Settings.h and their routes in PlotRoutes.cpp (PLAN.md section 2). Everything
 * in this file is deliberately about the device rather than about what the
 * device does.
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  31.8.2026
 * @updated  31.8.2026
 */
#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <esp_wifi.h>     // asking the driver whether it has credentials at all
#include <time.h>

#include "Version.h"
#include "LogBuffer.h"
#include "Settings.h"
#include "Expert.h"
#include "WebRoutes.h"
#include "FileRoutes.h"
#include "NvsRoutes.h"
#include "OtaUpdate.h"
#include "Portal.h"
#include "Board.h"
#include "Driver.h"
#include "Motion.h"
#include "Pen.h"
#include "Display.h"
#include "Imu.h"
#include "HardwareRoutes.h"

#include <Wire.h>

#if __has_include("Secrets.h")
#include "Secrets.h"
#endif

// The objects every module reaches for by `extern`. One web server, one
// settings record; nothing here is worth a second instance of either.
WebServer server(80);
Settings settings;
DebugLog Debug;

// Whether the device currently has a network. Written here and read by the
// update module, which will not fetch a manifest without one.
bool wifiConnected = false;
// Set when a connection is fresh - at boot, and again after the WLAN tab has
// switched networks - so the things that only make sense on a live interface
// are done once per connection rather than once per boot.
bool wifiFirstConnected = false;

// ------ time ------

// Local time, for the update module's night window. A fixed rule rather than a
// setting: the only thing the wall clock is used for so far is deciding
// whether it is the middle of the night, and a timezone tab for that alone
// would be a tab nobody opens. Central Europe with its EU daylight-saving
// rule; change it here, or give it a setting the day the plotter needs one.
#define POSIX_TZ "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER "pool.ntp.org"

/** Local hour 0..23, or -1 while SNTP has not set the clock yet. */
static int localHour()
{
    time_t seconds = time(nullptr);
    // Anything before 2021 means the clock has never been set: the epoch is
    // what the RTC counts from at power-on, and the update module has to be
    // able to tell "it is 3 a.m." from "I have no idea what time it is".
    if (seconds < 1600000000) return -1;

    struct tm local;
    localtime_r(&seconds, &local);
    return local.tm_hour;
}

// ------ deferred settings write ------

// NVS appends on every write and erases a sector once a page fills, so writing
// on each keystroke of a slider would wear the partition for no reason. A
// change asks for a write; the write happens once things have gone quiet.
#define SETTINGS_SAVE_DELAY 10000

static unsigned long settingsSaveAt = 0;

/**
 * Asks for the settings to be written a few seconds from now.
 *
 * Called by anything that changes them - the update module when the channel
 * moves, and whatever the plotter ends up storing. The rename in the WLAN
 * tab deliberately does *not* use this: it restarts the device, and a pending
 * write would be lost with the reboot, so that one writes through.
 */
void scheduleSettingsSave()
{
    settingsSaveAt = millis() + SETTINGS_SAVE_DELAY;
}

static void saveSettingsIfDue()
{
    if (settingsSaveAt == 0) return;
    if ((long)(millis() - settingsSaveAt) < 0) return;

    settingsSaveAt = 0;
    settings.storeSettings();
}

// ------ network ------

// Transmit power. The part comes up at its maximum, 19.5 dBm, and a transmit
// burst at that level pulls 350-500 mA for a moment. A board fed through a USB
// cable - or, here, through a USB-C PD sink and a 3.3 V regulator shared with
// two stepper drivers and an IMU - does not always have that to give: the 3.3 V rail dips, the
// radio drops out mid-burst, and what you see is an access point that appears
// for a second and is gone again. That symptom looks like anything except a
// supply problem, which is why this is worth doing before it is diagnosed
// rather than after.
//
// It matters most for the setup portal, which is the one mode where the radio
// is transmitting continuously into a room with nothing else running yet - and
// the one mode where a failure means the device cannot be set up at all.
//
// 13 dBm is 6 dB down, a quarter of the power, and still far more than a room
// needs. The same value QlockThreeW32 settled on; Rotator goes further, to
// 8.5 dBm. Raise it if a device ends up somewhere with a weak signal; that is
// the trade this number makes.
#define WIFI_TX_POWER WIFI_POWER_13dBm

/**
 * Sets the transmit power on whichever interface has just come up.
 *
 * It has to be done per interface and after that interface has started, not
 * once in setup(): the value lives in the driver, and starting STA or AP puts
 * it back to the maximum. Hence the call from the event handler, which is the
 * only place that knows the radio is up.
 */
static void applyTxPower(const char *which)
{
    WiFi.setTxPower(WIFI_TX_POWER);
    // Read back rather than printed from the constant: the driver clamps to
    // what the interface will actually do, and a value that was refused is
    // exactly what this line is for. The cast is not decoration - getTxPower()
    // returns the enum, and multiplying an enumeration by a float is
    // deprecated in C++20. The unit is quarter-dBm.
    debugI("%s: transmit power set to %.1f dBm", which, (int)WiFi.getTxPower() * 0.25f);
}

/**
 * The two events worth acting on, out of the two dozen the driver emits.
 *
 * Everything else about the connection is handled by polling in
 * holdConnection() rather than from here: an event handler runs on the WiFi
 * task, with a small stack and no business calling into the web server, and
 * the state it would set is state loop() has to look at anyway.
 */
static void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info)
{
    switch (event)
    {
        case ARDUINO_EVENT_WIFI_STA_START: applyTxPower("STA"); break;
        case ARDUINO_EVENT_WIFI_AP_START:  applyTxPower("AP");  break;
        default: break;
    }
}

// How long the stored network is given at boot before the portal takes over.
#define WIFI_BOOT_TIMEOUT 20000

// How long a lost connection is left to come back on its own before this code
// asks the driver to try again. The driver reconnects by itself in most cases;
// this is the fallback for the ones where it does not.
#define WIFI_RECONNECT_EVERY 30000

/**
 * Tries the network the driver already knows about.
 *
 * The credentials are the WiFi driver's own, in its own NVS namespace: they
 * are put there by WiFi.begin(ssid, pass) - from the setup portal or from the
 * WLAN tab - and read back by WiFi.begin() with no arguments. Keeping a second
 * copy in this project's settings was the obvious alternative and is worse:
 * two stores of the same secret that can disagree, and the driver would still
 * hold the one that actually decides.
 *
 * @return true when connected; false when there was nothing to try or it did
 *         not come up in time. `hadCredentials` says which of the two.
 */
static bool connectStoredNetwork(bool &hadCredentials)
{
    WiFi.mode(WIFI_STA);
    // The name the router lists the device under. Only read while the
    // interface comes up, so it has to be set before the connection is made.
    WiFi.setHostname(settings.getHostname());
    // Off, or the radio parks itself between beacons and the web UI answers
    // half a second late for no gain on a mains-powered device.
    WiFi.setSleep(false);

    // Asked of the driver rather than inferred from what WiFi.begin() returns:
    // before the first attempt WiFi.SSID() is empty whether or not anything is
    // stored, and the difference decides between "start the portal and stay
    // there" and "start the portal but retry in ten minutes".
    wifi_config_t stored;
    hadCredentials = esp_wifi_get_config(WIFI_IF_STA, &stored) == ESP_OK &&
                     stored.sta.ssid[0] != '\0';
    if (!hadCredentials)
    {
        debugA("No stored network");
        return false;
    }

    debugA("Connecting to %s", (const char *)stored.sta.ssid);
    WiFi.begin();

    unsigned long deadline = millis() + WIFI_BOOT_TIMEOUT;
    while (WiFi.status() != WL_CONNECTED && (long)(millis() - deadline) < 0)
    {
        delay(250);
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        debugW("Could not reach %s within %d s", WiFi.SSID().c_str(), WIFI_BOOT_TIMEOUT / 1000);
        return false;
    }

    debugA("Connected - local IP %s", WiFi.localIP().toString().c_str());
    return true;
}

/**
 * Everything that wants a live interface. Run once per connection, not once
 * per boot: after the WLAN tab has moved the device to another network the
 * mDNS record belongs to the old interface and the SNTP client is pointed at a
 * server it can no longer reach.
 *
 * MDNS.end() first, because begin() on a responder that is already running
 * leaves the old record advertised alongside the new one and the device then
 * answers to itself twice.
 */
static void announceServices()
{
    MDNS.end();
    // What the display shows while nothing is plotting: where to find the
    // device. Here because this runs once per connection, with the address
    // that actually holds.
    char host[40];
    snprintf(host, sizeof host, "%s.local", settings.getHostname());
    Display::show(settings.getHostname(), WiFi.localIP().toString().c_str(), host);

    if (MDNS.begin(settings.getHostname()))
    {
        MDNS.addService("http", "tcp", 80);
        debugA("mDNS responder started: %s.local", settings.getHostname());
    }
    else
    {
        debugE("Could not start the mDNS responder");
    }

    // configTzTime rather than configTime: the POSIX rule goes in with the
    // servers, so localtime_r() is right from the first answer instead of
    // being UTC until something calls setenv("TZ") afterwards.
    configTzTime(POSIX_TZ, NTP_SERVER);
}

/**
 * Keeps the connection up, without fighting the WLAN tab's own switch.
 *
 * The driver reconnects by itself in most cases and this only ever nudges it,
 * which is why the nudge is on a timer rather than on every pass: calling
 * reconnect() in a tight loop cancels the attempt already in flight, and the
 * device then never comes back from an outage it would have survived.
 */
static void holdConnection()
{
    static unsigned long nextAttempt = 0;

    // The switch has its own state machine and its own fallback; two pieces of
    // code calling begin() and reconnect() at once is how both fail.
    if (Web::switchingNetwork()) return;

    bool up = WiFi.status() == WL_CONNECTED;

    if (up != wifiConnected)
    {
        wifiConnected = up;
        if (up)
        {
            debugA("Network back: %s, %s", WiFi.SSID().c_str(),
                   WiFi.localIP().toString().c_str());
            wifiFirstConnected = true;
            nextAttempt = 0;
            return;
        }
        debugW("Network lost");
        nextAttempt = millis() + WIFI_RECONNECT_EVERY;
    }

    if (up) return;

    if (nextAttempt && (long)(millis() - nextAttempt) >= 0)
    {
        debugI("Asking the driver to reconnect");
        WiFi.reconnect();
        nextAttempt = millis() + WIFI_RECONNECT_EVERY;
    }
}

// ------ espota ------

static void startArduinoOta()
{
    ArduinoOTA.setPort(8266);
    ArduinoOTA.setHostname(settings.getHostname());
#ifdef OTA_PASSWORD
    ArduinoOTA.setPassword(OTA_PASSWORD);
#else
    // No Secrets.h: espota accepts anyone who can reach the device. Fine for a
    // public release image, which is why the build does not insist on one.
#warning "No OTA_PASSWORD set - copy src/Secrets.example.h to src/Secrets.h"
#endif

    ArduinoOTA
        .onStart([]()
        {
            debugA("espota: updating the %s",
                   ArduinoOTA.getCommand() == U_FLASH ? "firmware" : "filesystem");
        })
        .onEnd([]() { debugA("espota: done"); })
        .onError([](ota_error_t error) { debugE("espota: error %u", error); });

    ArduinoOTA.begin();
}

// ------ setup and loop ------

void setup()
{
    Serial.begin(115200);
    Serial.println();

    // Start capturing before anything has something to say. Two halves:
    // Log::begin() takes over ESP-IDF's own logging, and setSerialEnabled()
    // opens RemoteDebug's gate - isActive() answers false until either the
    // serial echo is on or a telnet client has connected, so every debugX
    // between here and Debug.begin() would otherwise be discarded. That is the
    // whole of the boot: mounting the filesystem, loading the settings, and
    // deciding between the stored network and the setup portal.
    Log::begin();
    Debug.setSerialEnabled(true);

    debugA("Scribbler %s, built %s %s", FIRMWARE_VERSION, __DATE__, __TIME__);

    // Which memory the build actually got, rather than which one the build
    // configuration asked for. psramFound() answers false both when PSRAM is
    // switched off and when it was switched on but did not initialise, and
    // those two look identical from every other line in the log.
    debugI("heap %u free of %u, largest block %u; PSRAM %u bytes",
           ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMaxAllocHeap(),
           psramFound() ? ESP.getPsramSize() : 0);

    if (!LittleFS.begin())
    {
        // Not fatal, and worth carrying on for: the REST endpoints all work
        // without a filesystem, and the update tab is how a missing web UI
        // gets put back - except that it lives in the filesystem too. The
        // honest answer is the log line and `pio run -t uploadfs`.
        debugE("LittleFS mount failed - no web interface will be served");
    }

    settings.loadSettings();

    // The drivers before anything slow: ENN has a pull-up and the motors are
    // off until this point anyway, but the drivers' own register defaults are
    // not ours, and the sooner they are replaced the less there is to wonder
    // about if something moves during a boot.
    Driver::begin(settings.getMotorCurrent(), settings.getMicrosteps());
    Motion::begin(settings.getMicrosteps(), settings.getPulleyDiameter());
    Pen::begin(settings.getPenUp(), settings.getPenDown());

    // One bus for the display and the IMU, started here once rather than by
    // whichever of the two happens to come first.
    Wire.begin(Board::I2C_SDA, Board::I2C_SCL, 400000);
    Display::begin();
    Display::show(settings.getHostname(), "starting ...");
    Imu::begin();
    Imu::setOffset(settings.getImuOffset());

    // Reads the stored lock state and opens the reset window if this run
    // started at the plug. Before the server, so no request can arrive while
    // the answer to "is this device unlocked" is still the default.
    Expert::begin();

    // Before anything brings an interface up, in either mode: the handler's
    // whole job is to turn the transmit power down the moment one starts, and
    // an interface that started before it was registered keeps the maximum.
    WiFi.onEvent(onWiFiEvent);

    bool hadCredentials = false;
    wifiConnected = connectStoredNetwork(hadCredentials);

    if (!wifiConnected)
    {
        // Setup mode. Nothing but the portal is registered - see Portal.h for
        // why the rest of the endpoints stay away from an open access point.
        Portal::begin(hadCredentials);
        // The one moment the display is indispensable: a device on no network
        // has no other way to say where to go.
        Display::show("Setup: join WLAN", settings.getHostname(), "then open",
                      WiFi.softAPIP().toString().c_str());
        return;
    }

    wifiFirstConnected = true;

    // Telnet debugging, now that there is an address to reach it on.
    Debug.begin(settings.getHostname(), RemoteDebug::INFO);
    Debug.setSerialEnabled(true);

    // Reads the web UI's version out of the filesystem and hangs the /ota
    // routes on the server, so it wants LittleFS mounted and the server not
    // yet started.
    Ota::begin();

    Web::begin();
    Files::begin();
    Nvs::begin();
    Hw::begin();
    server.begin();

    startArduinoOta();

    debugA("Ready: http://%s.local/ (%s)", settings.getHostname(),
           WiFi.localIP().toString().c_str());
}

void loop()
{
    // A restart is pending - after an update, or after a rename. Nothing else
    // may run in the meantime: on a filesystem update LittleFS has been
    // unmounted underneath us, and anything that touches it now is reading a
    // partition that is being rewritten.
    if (Ota::restartPending())
    {
        if (Ota::restartDue())
        {
            debugA("Restarting");
            Serial.flush();
            ESP.restart();
        }
        // The response still has to get out, so the server keeps running and
        // nothing else does.
        server.handleClient();
        return;
    }

    if (Portal::active())
    {
        Portal::poll();
        server.handleClient();
        return;
    }

    server.handleClient();
    ArduinoOTA.handle();
    Debug.handle();

    Pen::poll();
    Imu::poll();

    // The network switch requested by the WLAN tab, step by step.
    Web::poll();

    holdConnection();

    // A connection that is new - this boot's, or the one the WLAN tab just
    // switched to. mDNS and SNTP are announced against the interface that
    // actually exists rather than the one that did at boot.
    if (wifiFirstConnected && wifiConnected)
    {
        wifiFirstConnected = false;
        announceServices();
    }

    // Whether there is a new release, and - if that is switched on and it is
    // the middle of the night - installing it.
    Ota::poll(wifiConnected, localHour());

    saveSettingsIfDue();
}
