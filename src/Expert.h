/**
 * Expert
 * The lock in front of the update, debug and storage tabs.
 *
 * Three things this device can do are worth keeping away from whoever happens
 * to be on the network: flashing it, reading its log, and rummaging through
 * its filesystem and its NVS.
 *
 * The model is a mode, not a login. One flag in NVS says whether the device is
 * unlocked; while it is 0, `/ota/*`, `/log`, `/fs/*`, `/nvs/*` and `/restart`
 * answer 403 and the web UI does not offer the tabs at all. Setting the flag
 * needs the password; clearing it does not - someone locking the device out of
 * spite has gained nothing, and the owner opens it again with the password.
 *
 * Deliberately not HTTP Basic authentication, which is the obvious first idea.
 * The debug tab polls `/log` every two seconds, and Basic puts the password on
 * the wire base64-encoded on every single request - some 1800 times an hour
 * with the tab left open. One unlock puts it there once.
 *
 * **The hash is made on the device, never at build time.** A hash compiled
 * into the image would be published with every release and, worse, would come
 * back with every OTA update and overwrite the owner's own. NVS is the one
 * store an update does not touch - the same reason the settings live there.
 *
 * It has its own namespace rather than a field in the settings record, for two
 * reasons that both bite in practice: that record is rewritten whole on every
 * settings change, so a field forgotten there is a field lost; and the same
 * shape is published through `/currentState`, where a password hash has no
 * business being.
 *
 * What this is worth: it keeps out someone who joins the network and goes
 * looking. It does not keep out someone who can watch the traffic - enrolment
 * and unlock both cross the wire in the clear - and it does not keep out
 * anyone holding the device, who can read the flash over USB. That last one is
 * why the reset path below is not a weakness: physical access already wins.
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  31.8.2026
 * @updated  31.8.2026
 */
#ifndef EXPERT_H
#define EXPERT_H

#include <Arduino.h>

// How long after a power-on reset the enrolment may be cleared without the
// password. This is the way back from a forgotten one, and it is deliberately
// tied to the plug: esp_reset_reason() tells a power-on from the software
// restart the web UI triggers, so rebooting the device from its own update tab
// does not open it.
#define EXPERT_GRACE_MS (5 * 60 * 1000UL)

// Wrong answers before the endpoint stops taking them for a while. Without
// this a four digit password over HTTP is guessed in seconds.
#define EXPERT_MAX_FAILURES 5
#define EXPERT_LOCKOUT_MS (5 * 60 * 1000UL)

// The NVS namespace holding the salt, the hash and the flag. Named here rather
// than only in Expert.cpp because NvsRoutes.cpp has to refuse to hand the hash
// out, and two spellings of the same namespace would be a hole nobody notices.
#define EXPERT_NAMESPACE "scribexpert"

namespace Expert
{
    /**
     * Reads the stored state. Call from setup() before the server starts, and
     * after Settings, so the grace window starts at a sensible moment.
     */
    void begin();

    /** True once a password has been set on this device. */
    bool enrolled();

    /** True while the expert endpoints and tabs are open. */
    bool unlocked();

    /**
     * True while the enrolment may still be cleared without the password -
     * the first few minutes after the device was switched on at the plug.
     */
    bool inGraceWindow();

    /** Seconds left of that window, for the UI. Zero once it has closed. */
    uint32_t graceRemaining();

    /** True while too many wrong passwords have been offered. */
    bool lockedOut();

    /**
     * Sets the password on a device that has none, and unlocks it. False when
     * one is already stored - re-enrolling goes through reset() first.
     */
    bool enroll(const char *password);

    /** Checks the password and unlocks on a match. */
    bool unlock(const char *password);

    /** Locks again. Always allowed; see the note above. */
    void lock();

    /**
     * Forgets the password and locks. Only inside the grace window, so this
     * is reachable by pulling the plug and not over the network alone.
     */
    bool reset();

    /**
     * Answers 403 and returns false when the device is locked - the one line
     * at the top of every handler expert mode covers.
     *
     * It lives here rather than in WebRoutes so that the files that need it
     * cannot drift apart on what "locked" means. A handler that forgets to
     * call it is open, so the list of callers is worth keeping in one place:
     * /log, the five /ota routes, five of the six /fs routes, the four /nvs
     * routes, and /restart.
     *
     * The odd ones out are /fs/upload and /ota/upload, and they are why
     * unlocked() is public: both stream the body into flash from a handler
     * that cannot send a response, so they ask the question themselves and
     * refuse by recording an error for the done handler. Guarding only the
     * done handler would let a stranger overwrite a file and be refused
     * afterwards, which is not a refusal.
     */
    bool guard();
}

#endif
