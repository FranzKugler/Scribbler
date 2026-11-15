/**
 * WebRoutes
 * See WebRoutes.h.
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
#include <ArduinoJson.h>
#include <esp_system.h>   // esp_reset_reason(), for the debug tab

#include "WebRoutes.h"
#include "OtaUpdate.h"    // a rename asks for a restart; /currentState names the images
#include "Version.h"
#include "Settings.h"
#include "Expert.h"
#include "LogBuffer.h"

extern WebServer server;
extern Settings settings;
extern bool wifiConnected;
// Cleared once the first connection has been dealt with; a network switch
// sets it again so loop() redoes what it does on a fresh connection.
extern bool wifiFirstConnected;

// ------ Switching WiFi networks from the web UI ------
// A wrong password would otherwise lock us out of the network for good, so the
// switch runs as a state machine in loop(): try the new credentials, and fall
// back to the previous ones if they don't come up within WIFI_SWITCH_TIMEOUT.
#define WIFI_SWITCH_TIMEOUT 20000

enum WifiSwitchState {
    WIFI_SWITCH_IDLE,
    WIFI_SWITCH_START,      // request accepted, not acted on yet
    WIFI_SWITCH_WAIT_NEW,   // waiting for the new network
    WIFI_SWITCH_WAIT_OLD    // new one failed, waiting for the old one again
};

static WifiSwitchState wifiSwitchState = WIFI_SWITCH_IDLE;
static String wifiPendingSsid, wifiPendingPass;
static String wifiPreviousSsid, wifiPreviousPass;
static unsigned long wifiSwitchDeadline = 0;
// Empty unless the last switch failed. Holds a code, not a sentence: the web
// UI translates it - see errorText() in errors.js.
static String wifiLastError;
// The variable part of that message, e.g. the SSID. Never translated.
static String wifiErrorDetail;

// ------ serving the web UI itself ------

/** Content type from the extension. Unknown is left to the browser to sniff. */
static const char *contentTypeFor(const String &path)
{
    if (path.endsWith(".html") || path.endsWith(".htm")) return "text/html";
    if (path.endsWith(".css"))  return "text/css";
    if (path.endsWith(".js"))   return "application/javascript";
    if (path.endsWith(".json")) return "application/json";
    if (path.endsWith(".svg"))  return "image/svg+xml";
    if (path.endsWith(".png"))  return "image/png";
    if (path.endsWith(".ico"))  return "image/vnd.microsoft.icon";
    if (path.endsWith(".webmanifest")) return "application/manifest+json";
    return "text/plain";
}

bool Web::serveFromFilesystem(String path)
{
    if (path.endsWith("/")) path += "index.html";

    File file = LittleFS.open(path, "r");
    if (!file) return false;
    if (file.isDirectory()) { file.close(); return false; }

    server.streamFile(file, contentTypeFor(path));
    file.close();
    return true;
}

// ------ settings ------

/**
 * Everything the shell needs before it can draw itself, plus what the status
 * tab shows.
 *
 * Open, unlike /ota/status next door, and the difference is deliberate: which
 * versions are installed and how long the device has been up are the first
 * things anybody is asked for when something looks wrong, and a password in
 * front of that helps nobody. What the lock covers is the log, the filesystem
 * and the act of flashing - not the label on the tin.
 */
static void sendCurrentState()
{
    JsonDocument doc;
    settings.publish(doc);

    doc["firmwareVersion"] = FIRMWARE_VERSION;
    doc["webUiVersion"]    = Ota::webUiVersion();
    doc["uptime"]          = millis();
    doc["ip"]              = WiFi.localIP().toString();

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

/**
 * The web app manifest, which is how Android decides what to put on a home
 * screen - it ignores the apple-touch-icon iOS uses.
 *
 * Served from here rather than shipped as a file in the filesystem image,
 * because the manifest carries the app name and that is per device.
 *
 * The icons themselves are static files and do come from the image.
 */
static void sendManifest()
{
    JsonDocument doc;
    doc["name"]             = settings.getHostname();
    doc["short_name"]       = settings.getHostname();
    doc["start_url"]        = "/";
    doc["display"]          = "standalone";
    doc["background_color"] = "#0d0d0d";
    doc["theme_color"]      = "#0d0d0d";

    JsonArray icons = doc["icons"].to<JsonArray>();

    JsonObject small = icons.add<JsonObject>();
    small["src"]   = "/icon-192.png";
    small["sizes"] = "192x192";
    small["type"]  = "image/png";

    JsonObject large = icons.add<JsonObject>();
    large["src"]   = "/icon-512.png";
    large["sizes"] = "512x512";
    large["type"]  = "image/png";

    // Declared separately because it is drawn with a wider margin: a launcher
    // may crop a maskable icon to any shape inside the middle 80 %.
    JsonObject maskable = icons.add<JsonObject>();
    maskable["src"]     = "/icon-512-maskable.png";
    maskable["sizes"]   = "512x512";
    maskable["type"]    = "image/png";
    maskable["purpose"] = "maskable";

    String out;
    serializeJson(doc, out);
    server.send(200, "application/manifest+json", out);
}

/**
 * Reduces a requested name to something usable as a DNS label: letters, digits
 * and hyphens, no hyphen at either end, at most 32 characters. Returns an empty
 * string when nothing is left, which the caller rejects.
 *
 * Done here rather than trusted from the browser - the endpoint is reachable
 * without going through the UI, and a name with a dot or a space in it would
 * produce an mDNS record nobody can reach.
 */
static String sanitizeHostname(const String &wanted)
{
    String clean;
    for (unsigned int i = 0; i < wanted.length() && clean.length() < 32; i++)
    {
        char c = wanted.charAt(i);
        if (isalnum((unsigned char)c) || c == '-') clean += c;
    }
    while (clean.length() && clean.charAt(0) == '-') clean.remove(0, 1);
    while (clean.length() && clean.charAt(clean.length() - 1) == '-') clean.remove(clean.length() - 1);
    return clean;
}

/**
 * Renames the device and restarts it.
 *
 * A restart because the name is read in five places and only mDNS can be
 * changed while the device runs - the DHCP name, the espota name and the setup
 * access point are all read as the interface comes up. Renaming without one
 * left the device answering to two different names depending on who was
 * asking.
 *
 * The settings go to NVS here and now rather than through the deferred write,
 * which would still be pending when the restart happens and would lose the new
 * name. The answer carries the name that was actually stored, which is not
 * necessarily the one that was asked for.
 */
static void updateHostname()
{
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));

    String wanted = sanitizeHostname(doc["hostname"] | "");
    if (wanted.length() == 0)
    {
        server.send(400, "application/json", "{\"error\":\"hostnameInvalid\"}");
        return;
    }

    JsonDocument answer;
    answer["hostname"]   = wanted;
    answer["restarting"] = (wanted != String(settings.getHostname()));
    String out;
    serializeJson(answer, out);

    if (wanted != String(settings.getHostname()))
    {
        settings.setHostname(wanted.c_str());
        settings.storeSettings();
        debugA("Renamed to %s, restarting", settings.getHostname());

        // Answer first, then restart from loop() - the response would never
        // reach the browser otherwise. Nothing else is pending: the write above
        // has already happened.
        server.send(200, "application/json", out);
        Ota::scheduleRestart();
        return;
    }

    server.send(200, "application/json", out);
}

/**
 * Restarts the device, and nothing else.
 *
 * It exists for one situation, which is the storage tab: the firmware holds
 * its records in RAM and writes them back on the next settings change, so an
 * edit made to `scribbler/conf` in NVS is only durable if the device is
 * restarted before anything else touches it. Saying that in a warning and then
 * leaving the reader to find a power socket would be telling half the story.
 *
 * Behind the lock like the tab it serves. The answer goes out first and the
 * restart happens in loop() through the same Ota::scheduleRestart() a rename
 * uses - it is not an update, but it needs exactly the same delay, or the
 * response never reaches the browser.
 */
static void restartDevice()
{
    if (!Expert::guard()) return;

    debugA("Restart asked for over HTTP");
    server.send(200, "application/json", "{\"restarting\":true}");
    Ota::scheduleRestart();
}

// ------ WiFi endpoints for the web UI ------

/** Current connection, plus the outcome of a switch requested via POST /wifi. */
static void sendWifiStatus()
{
    JsonDocument doc;
    doc["connected"]   = (WiFi.status() == WL_CONNECTED);
    doc["ssid"]        = WiFi.SSID();
    doc["ip"]          = WiFi.localIP().toString();
    doc["rssi"]        = WiFi.RSSI();
    doc["mac"]         = WiFi.macAddress();
    doc["hostname"]    = settings.getHostname();
    doc["switching"]   = (wifiSwitchState != WIFI_SWITCH_IDLE);
    doc["error"]       = wifiLastError;
    doc["errorDetail"] = wifiErrorDetail;

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

/**
 * Async scan: the first call kicks one off, the browser polls until it is done.
 *
 * The same answer shape the setup portal's /portal/scan gives, because the two
 * are drawn by the same Svelte component - see web/src/lib/NetworkList.svelte.
 */
static void sendWifiScan()
{
    JsonDocument doc;
    int found = WiFi.scanComplete();

    if (found == WIFI_SCAN_RUNNING)
    {
        doc["scanning"] = true;
    }
    else if (found == WIFI_SCAN_FAILED)
    {
        // Nothing running and nothing cached: start one.
        WiFi.scanNetworks(true);
        doc["scanning"] = true;
    }
    else
    {
        doc["scanning"] = false;
        JsonArray networks = doc["networks"].to<JsonArray>();
        for (int i = 0; i < found && i < 20; i++)
        {
            JsonObject net = networks.add<JsonObject>();
            net["ssid"]   = WiFi.SSID(i);
            net["rssi"]   = WiFi.RSSI(i);
            net["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
        }
        // Drop the cache so the next poll starts a fresh scan.
        WiFi.scanDelete();
    }

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

/**
 * Accepts new credentials and answers immediately - the actual switch happens
 * in loop(), otherwise the response would never make it out of the old network.
 */
static void updateWifi()
{
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));

    wifiPendingSsid = doc["ssid"] | "";
    wifiPendingPass = doc["password"] | "";

    if (wifiPendingSsid.length() == 0)
    {
        server.send(400, "application/json", "{\"error\":\"wifiNoSsid\"}");
        return;
    }

    // Remember what we are on now, so we can come back to it.
    wifiPreviousSsid = WiFi.SSID();
    wifiPreviousPass = WiFi.psk();
    wifiLastError = "";
    wifiErrorDetail = "";
    wifiSwitchState = WIFI_SWITCH_START;

    debugI("WiFi switch requested: %s", wifiPendingSsid.c_str());
    server.send(200, "application/json", "{\"msg\":\"\"}");
}

/** Drives the switch started by updateWifi(), including the fallback. */
static void handleWifiSwitch()
{
    switch (wifiSwitchState)
    {
        case WIFI_SWITCH_START:
        {
            WiFi.disconnect();
            WiFi.begin(wifiPendingSsid.c_str(), wifiPendingPass.c_str());
            wifiSwitchDeadline = millis() + WIFI_SWITCH_TIMEOUT;
            wifiSwitchState = WIFI_SWITCH_WAIT_NEW;
            break;
        }
        case WIFI_SWITCH_WAIT_NEW:
        {
            if (WiFi.status() == WL_CONNECTED)
            {
                debugI("WiFi switch done: %s", WiFi.SSID().c_str());
                wifiSwitchState = WIFI_SWITCH_IDLE;
                wifiConnected = true;
                wifiFirstConnected = true;  // re-announce the services
            }
            else if ((long)(millis() - wifiSwitchDeadline) >= 0)
            {
                debugW("WiFi switch failed, falling back to %s", wifiPreviousSsid.c_str());
                wifiLastError = "wifiConnect";
                wifiErrorDetail = wifiPendingSsid;
                WiFi.disconnect();
                WiFi.begin(wifiPreviousSsid.c_str(), wifiPreviousPass.c_str());
                wifiSwitchDeadline = millis() + WIFI_SWITCH_TIMEOUT;
                wifiSwitchState = WIFI_SWITCH_WAIT_OLD;
            }
            break;
        }
        case WIFI_SWITCH_WAIT_OLD:
        {
            if (WiFi.status() == WL_CONNECTED)
            {
                wifiSwitchState = WIFI_SWITCH_IDLE;
                wifiConnected = true;
                wifiFirstConnected = true;
            }
            else if ((long)(millis() - wifiSwitchDeadline) >= 0)
            {
                // Both networks are gone; the normal reconnect logic takes over.
                debugE("Fallback to %s failed too", wifiPreviousSsid.c_str());
                wifiLastError = "wifiFallback";
                wifiErrorDetail = wifiPreviousSsid;
                wifiSwitchState = WIFI_SWITCH_IDLE;
            }
            break;
        }
        default:
            break;
    }
}

// ------ the log ------

static const char *resetReasonName()
{
    switch (esp_reset_reason())
    {
        case ESP_RST_POWERON:  return "power-on";
        case ESP_RST_EXT:      return "external";
        case ESP_RST_SW:       return "software";    // our own ESP.restart()
        case ESP_RST_PANIC:    return "panic";       // the interesting one
        case ESP_RST_INT_WDT:  return "watchdog-int";
        case ESP_RST_TASK_WDT: return "watchdog-task";
        case ESP_RST_WDT:      return "watchdog";
        case ESP_RST_BROWNOUT: return "brownout";    // the other interesting one
        case ESP_RST_SDIO:     return "sdio";
        case ESP_RST_DEEPSLEEP: return "deep-sleep";
        // The five below are ESP32-S3 additions, and leaving them out is how
        // this would report "unknown" for the most ordinary restart there is:
        // a XIAO has no USB-serial converter, so the reset that ends a flash
        // comes from the chip's own USB peripheral and lands on ESP_RST_USB.
        // CPU_LOCKUP is the double exception - worth telling apart from a
        // plain panic, since it usually means the panic handler itself fell
        // over.
        case ESP_RST_USB:        return "usb";
        case ESP_RST_JTAG:       return "jtag";
        case ESP_RST_EFUSE:      return "efuse";
        case ESP_RST_PWR_GLITCH: return "power-glitch";
        case ESP_RST_CPU_LOCKUP: return "cpu-lockup";
        default:               return "unknown";
    }
}

/** Appends a string as a JSON string literal, quotes and all. */
static void appendJsonString(String &out, const char *text)
{
    out += '"';
    for (const char *at = text; *at; at++)
    {
        unsigned char c = (unsigned char)*at;
        if (c == '"' || c == '\\')
        {
            out += '\\';
            out += (char)c;
        }
        else if (c < 0x20)
        {
            // Should not occur - the ring stores whole lines with the control
            // characters already taken out - but a JSON document with a raw
            // control character in it is not JSON, and the tab would show
            // nothing at all rather than one odd line.
            char escaped[7];
            snprintf(escaped, sizeof(escaped), "\\u%04x", c);
            out += escaped;
        }
        else
        {
            out += (char)c;   // UTF-8 passes through; JSON is UTF-8
        }
    }
    out += '"';
}

/**
 * The device's log, and enough of its state to know what it was doing.
 *
 * Polled by the debug tab with the sequence number it last saw, so the usual
 * answer carries nothing but the handful of lines that have appeared since.
 * A freshly opened tab asks with `since=0` and gets the oldest batch first;
 * `more` then says whether another round is worth it, which is what fills the
 * window with the boot in one go rather than one screen every two seconds.
 *
 * `oldest` is not decoration: it says which line the ring still starts at, so
 * the browser can tell "nothing new" from "the ring wrapped and you missed
 * 300 lines". Dropping that silently is exactly what makes a log window
 * untrustworthy.
 *
 * Built into one String with the room reserved up front rather than through
 * ArduinoJson: a hundred lines would put every one of them into a document
 * first and serialise that into a second buffer, and this runs on the same
 * heap an update wants.
 */
static void sendLog()
{
    // The log is behind the lock: it carries whatever the device has said
    // about itself, and that is not for everyone who can reach port 80.
    if (!Expert::guard()) return;

    uint32_t since = 0;
    if (server.hasArg("since")) since = strtoul(server.arg("since").c_str(), nullptr, 10);

    static Log::Line batch[LOG_BATCH];
    size_t count = Log::collect(since, batch, LOG_BATCH);

    uint32_t oldest = Log::oldestSeq();
    uint32_t next = Log::nextSeq();
    // What the caller should ask for next time. With nothing to send that is
    // where it already stood - clamped to the ring, so a restart hands back a
    // smaller number than was asked for instead of one that never comes round.
    uint32_t served = since;
    if (count > 0)            served = batch[count - 1].seq + 1;
    else if (served < oldest) served = oldest;
    else if (served > next)   served = next;

    String out;
    out.reserve(count * (LOG_LINE_MAX + 40) + 400);

    out += "{\"oldest\":";  out += oldest;
    out += ",\"seq\":";     out += served;
    out += ",\"more\":";    out += (served < next) ? "true" : "false";

    // What to look at first when something has gone wrong, and cheap enough to
    // send on every poll.
    out += ",\"uptime\":";    out += millis();
    out += ",\"heap\":";      out += ESP.getFreeHeap();
    out += ",\"heapMin\":";   out += ESP.getMinFreeHeap();
    out += ",\"heapBlock\":"; out += ESP.getMaxAllocHeap();
    out += ",\"reset\":\"";   out += resetReasonName(); out += '"';

    out += ",\"lines\":[";
    for (size_t i = 0; i < count; i++)
    {
        if (i > 0) out += ',';
        out += "{\"s\":";  out += batch[i].seq;
        out += ",\"t\":";  out += batch[i].ms;
        out += ",\"l\":";  out += batch[i].level;
        out += ",\"m\":";
        appendJsonString(out, batch[i].text);
        out += '}';
    }
    out += "]}";

    server.send(200, "application/json", out);
}

// ------ expert mode ------

/**
 * Whether the device is unlocked, and what it would take to unlock it.
 *
 * Carries no secret: the flag, whether a password has ever been set, and how
 * long the reset window is still open. The web UI needs all three before it
 * can decide between "enter your password", "choose one" and "you have five
 * minutes to start over".
 */
static void sendExpert()
{
    JsonDocument doc;
    doc["enrolled"]  = Expert::enrolled();
    doc["unlocked"]  = Expert::unlocked();
    doc["grace"]     = Expert::graceRemaining();
    doc["lockedOut"] = Expert::lockedOut();

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

/**
 * Sets, checks or clears the password.
 *
 *   {password}       on a device with none: stores it and unlocks
 *                    otherwise: unlocks if it matches
 *   {off: true}      locks again - no password needed, see Expert.h
 *   {reset: true}    forgets the password, only inside the grace window
 *
 * Answers with the same shape GET does, so the UI has the new state without a
 * second request, or with a code from the err_* set.
 */
static void updateExpert()
{
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));

    if (doc["off"] | false)
    {
        Expert::lock();
        sendExpert();
        return;
    }

    if (doc["reset"] | false)
    {
        if (!Expert::reset())
        {
            server.send(403, "application/json", "{\"error\":\"expertNoGrace\"}");
            return;
        }
        sendExpert();
        return;
    }

    const char *password = doc["password"] | "";

    if (Expert::lockedOut())
    {
        server.send(429, "application/json", "{\"error\":\"expertLockedOut\"}");
        return;
    }

    // Enrolling and unlocking are the same request on purpose. Which one it is
    // depends on the device, not on what the browser believes about it, and a
    // browser holding a stale answer would otherwise ask for the wrong one.
    bool ok = Expert::enrolled() ? Expert::unlock(password) : Expert::enroll(password);

    if (!ok)
    {
        // "Too short" is only meaningful while enrolling; afterwards saying it
        // would tell whoever is guessing how long the real one is.
        const char *code = Expert::enrolled() ? "expertWrongPassword" : "expertPasswordShort";
        server.send(403, "application/json", String("{\"error\":\"") + code + "\"}");
        return;
    }

    sendExpert();
}

// ------ the interface the rest of the program sees ------

void Web::begin()
{
    // Anything not matched below is looked for in the filesystem, which is
    // where the web UI itself lives.
    server.onNotFound([]()
    {
        if (!Web::serveFromFilesystem(server.uri()))
        {
            server.send(404, "text/plain", "404: Not Found");
        }
    });

    // Answers cross-origin so the Vite dev server can drive a real device.
    server.enableCORS();

    server.on("/currentState", HTTP_GET, sendCurrentState);
    server.on("/manifest.webmanifest", HTTP_GET, sendManifest);
    server.on("/hostname", HTTP_POST, updateHostname);
    server.on("/restart", HTTP_POST, restartDevice);
    server.on("/wifi", HTTP_GET, sendWifiStatus);
    server.on("/wifi", HTTP_POST, updateWifi);
    server.on("/wifi/scan", HTTP_GET, sendWifiScan);
    server.on("/log", HTTP_GET, sendLog);
    server.on("/expert", HTTP_GET, sendExpert);
    server.on("/expert", HTTP_POST, updateExpert);
}

void Web::poll()
{
    handleWifiSwitch();
}

bool Web::switchingNetwork()
{
    return wifiSwitchState != WIFI_SWITCH_IDLE;
}
