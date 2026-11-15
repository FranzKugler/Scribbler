/**
 * Settings
 * The user settings of the device, persisted to NVS as JSON and served to the
 * web UI. NVS rather than the filesystem, because the filesystem partition is
 * overwritten wholesale by an update of the web UI - so anything kept there
 * would be lost on the next release.
 *
 * The record is deliberately small: the name the device answers to and the
 * three fields the update channel needs. The plotter's settings belong here
 * too (PLAN.md section 2), and there are three places to touch when adding one - a
 * pair of accessors below, a line in loadSettings() and a line in
 * fillDocument(). Forgetting the last one is how a setting comes out as its
 * default after every restart; they sit next to each other for that reason.
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  31.8.2026
 * @updated  31.8.2026
 */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>

class Settings
{
public:
    Settings();

    void loadSettings();
    void storeSettings();

    /**
     * Adds what /currentState publishes to a document the caller owns.
     *
     * A document rather than a finished String, because /currentState carries
     * more than the settings - the firmware version, the uptime - and having
     * this hand back JSON to be parsed and re-serialised would be work done
     * twice on a heap an update wants for itself.
     *
     * Never a secret in here: this answer is open, and anything that needs the
     * lock belongs behind Expert::guard() somewhere else.
     */
    void publish(JsonDocument &doc);

    // The name the device answers to: `<hostname>.local` over mDNS, the DHCP
    // name, the name of the setup access point, and the heading of the web UI.
    // A second device on the same network needs a second name, or the two
    // fight over one mDNS record. Store only a valid DNS label here - the
    // firmware reduces what the UI sends before it gets this far.
    void  setHostname(const char *hostname) { strlcpy(this->Hostname, hostname, sizeof(this->Hostname)); }
    char *getHostname() { return this->Hostname; }

    // Update channel: 0 = stable (tagged releases), 1 = edge (every commit).
    void setOtaChannel(byte channel) { this->OtaChannel = channel; }
    byte getOtaChannel() { return this->OtaChannel; }

    // Off by default: a bad image that flashes cleanly and then crashes can
    // only be recovered over USB, so installing unattended is opt-in.
    void    setOtaAutoUpdate(boolean autoUpdate) { this->OtaAutoUpdate = autoUpdate; }
    boolean getOtaAutoUpdate() { return this->OtaAutoUpdate; }

    // Hours between checks. 0 switches checking off entirely.
    void setOtaCheckInterval(byte hours) { this->OtaCheckInterval = hours; }
    byte getOtaCheckInterval() { return this->OtaCheckInterval; }

    // ------ the plotter ------

    // Motor run current, RMS [mA]. Mural's pancake steppers are rated 1 A;
    // 700 mA is what Mural runs them at. Clamped to what the drivers' current
    // range allows by whoever applies it (Driver::maxCurrentMa()).
    void     setMotorCurrent(uint16_t ma) { this->MotorCurrent = ma; }
    uint16_t getMotorCurrent() { return this->MotorCurrent; }

    // Microsteps per full step, a power of two 1..256. 16 is the plan's choice
    // (PLAN.md section 1); the drivers interpolate to 256 regardless.
    void     setMicrosteps(uint16_t steps) { this->Microsteps = steps; }
    uint16_t getMicrosteps() { return this->Microsteps; }

    // Pen servo angles [degrees, 0..180]. Up must clear the wall; down is
    // found in the pen calibration step and differs with every pen.
    void setPenUp(byte angle) { this->PenUp = angle; }
    byte getPenUp() { return this->PenUp; }
    void setPenDown(byte angle) { this->PenDown = angle; }
    byte getPenDown() { return this->PenDown; }

    // How far the IMU's zero is off the bot's level [rad]; see Imu::setOffset().
    void  setImuOffset(float radians) { this->ImuOffset = radians; }
    float getImuOffset() { return this->ImuOffset; }

private:
    // Fills a document with the stored representation. Kept separate so the
    // field list exists once, next to the one that reads it back.
    void fillDocument(JsonDocument &doc);

    // A DNS label: up to 63 characters in theory, kept to 32 here because it
    // is also typed into a phone's address bar.
    char    Hostname[33];

    byte    OtaChannel;       // 0 = stable, 1 = edge
    boolean OtaAutoUpdate;    // install without asking
    byte    OtaCheckInterval; // hours between checks, 0 = never

    uint16_t MotorCurrent;    // mA RMS
    uint16_t Microsteps;      // per full step
    byte     PenUp;           // degrees
    byte     PenDown;         // degrees
    float    ImuOffset;       // rad
};

#endif
