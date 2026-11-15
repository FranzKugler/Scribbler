/**
 * Settings
 * See Settings.h.
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  31.8.2026
 * @updated  31.8.2026
 */
#include <Preferences.h>

#include "Settings.h"
#include "LogBuffer.h"

// The settings live in NVS, not in the filesystem: NVS has its own partition,
// which neither a firmware nor a filesystem update touches. The whole record
// goes in as one JSON string rather than as individual keys - partly to share
// the serialisation with getJSONSettings(), partly because NVS keys are capped
// at 15 characters and a descriptive field name does not fit.
#define NVS_NAMESPACE "scribbler"
#define NVS_KEY_CONF  "conf"

// Bumped whenever a stored field changes meaning, so loadSettings() can convert
// an older record instead of silently misreading it.
#define SETTINGS_SCHEMA 1

// Also the name of the setup access point, so it is the first thing anybody
// sees of this device.
#define DEFAULT_HOSTNAME "Scribbler"

// The plotter's defaults, in one place so the constructor and the fallbacks in
// loadSettings() cannot drift apart. Reasons are in Settings.h.
#define DEFAULT_MOTOR_CURRENT 700
#define DEFAULT_MICROSTEPS    16
#define DEFAULT_PEN_UP        90
#define DEFAULT_PEN_DOWN      70
#define DEFAULT_PULLEY        10.14926

Settings::Settings()
{
    strlcpy(Hostname, DEFAULT_HOSTNAME, sizeof(Hostname));
    OtaChannel = 0;         // stable
    OtaAutoUpdate = false;  // opt-in, see Settings.h
    OtaCheckInterval = 24;

    MotorCurrent = DEFAULT_MOTOR_CURRENT;
    Microsteps   = DEFAULT_MICROSTEPS;
    PenUp        = DEFAULT_PEN_UP;
    PenDown      = DEFAULT_PEN_DOWN;
    AnchorDistance = 0.0;
    PulleyDiameter = DEFAULT_PULLEY;
    ImuOffset    = 0.0f;
}

void Settings::loadSettings()
{
    Preferences preferences;
    if (!preferences.begin(NVS_NAMESPACE, true))
    {
        // No namespace yet is the normal state of a device that has never
        // stored anything - the constructor's defaults are the right answer.
        debugI("No stored settings found, using the defaults");
        return;
    }

    String json = preferences.getString(NVS_KEY_CONF, "");
    preferences.end();

    if (json.length() == 0)
    {
        debugI("No stored settings found, using the defaults");
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, json))
    {
        // Read on regardless: every field below falls back to the value the
        // constructor set, so a corrupted record costs the settings and not
        // the boot.
        debugE("Failed to parse the stored settings");
    }

    strlcpy(Hostname, doc["Hostname"] | DEFAULT_HOSTNAME, sizeof(Hostname));
    OtaChannel       = doc["OtaChannel"] | 0;
    OtaAutoUpdate    = doc["OtaAutoUpdate"] | false;
    OtaCheckInterval = doc["OtaCheckInterval"] | 24;

    MotorCurrent = doc["MotorCurrent"] | DEFAULT_MOTOR_CURRENT;
    Microsteps   = doc["Microsteps"] | DEFAULT_MICROSTEPS;
    PenUp        = doc["PenUp"] | DEFAULT_PEN_UP;
    PenDown      = doc["PenDown"] | DEFAULT_PEN_DOWN;
    AnchorDistance = doc["AnchorDistance"] | 0.0;
    PulleyDiameter = doc["PulleyDiameter"] | DEFAULT_PULLEY;
    ImuOffset    = doc["ImuOffset"] | 0.0f;

    // Outside what Plotter accepts, the record is not something to plot by.
    if (!(AnchorDistance >= 300.0 && AnchorDistance <= 5000.0)) AnchorDistance = 0.0;

    // A pulley of 0 would divide every belt length by zero.
    if (!(PulleyDiameter >= 5.0 && PulleyDiameter <= 40.0)) PulleyDiameter = DEFAULT_PULLEY;

    // Only powers of two are microstep settings the driver has; anything else
    // in the record is not something to guess the meaning of.
    if (Microsteps == 0 || Microsteps > 256 || (Microsteps & (Microsteps - 1)) != 0)
        Microsteps = DEFAULT_MICROSTEPS;

    // A record from the future, or a corrupted one, must not put the update
    // module on a channel it has no URL for.
    if (OtaChannel > 1) OtaChannel = 0;
}

void Settings::fillDocument(JsonDocument &doc)
{
    doc["Schema"]           = SETTINGS_SCHEMA;
    doc["Hostname"]         = String(Hostname);
    doc["OtaChannel"]       = OtaChannel;
    doc["OtaAutoUpdate"]    = OtaAutoUpdate;
    doc["OtaCheckInterval"] = OtaCheckInterval;

    doc["MotorCurrent"]     = MotorCurrent;
    doc["Microsteps"]       = Microsteps;
    doc["PenUp"]            = PenUp;
    doc["PenDown"]          = PenDown;
    doc["AnchorDistance"]   = AnchorDistance;
    doc["PulleyDiameter"]   = PulleyDiameter;
    doc["ImuOffset"]        = ImuOffset;
}

void Settings::storeSettings()
{
    JsonDocument doc;
    fillDocument(doc);

    String json;
    serializeJson(doc, json);

    Preferences preferences;
    if (!preferences.begin(NVS_NAMESPACE, false))
    {
        debugE("Cannot open NVS to store the settings");
        return;
    }

    // Nothing to do when the stored record is already identical. NVS appends a
    // new entry on every write and only erases a sector once a page is full,
    // so skipping the no-ops here directly saves erase cycles.
    if (preferences.getString(NVS_KEY_CONF, "") == json)
    {
        preferences.end();
        return;
    }

    size_t written = preferences.putString(NVS_KEY_CONF, json);
    preferences.end();

    if (written == 0) debugE("Failed to write the settings to NVS");
    else              debugI("Settings written to NVS, %u bytes", (unsigned)written);
}

void Settings::publish(JsonDocument &doc)
{
    // Also in /wifi, which is where it is edited; here so the shell can put it
    // in the heading and the page title without waiting for the WLAN tab.
    doc["hostname"] = String(Hostname);

    // The update channel is deliberately absent. It is settable only through
    // /ota/config, which is behind the lock, and reporting it here would put
    // it on a page anybody on the network can read.
}
