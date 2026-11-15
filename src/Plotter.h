/**
 * Plotter
 * The device's state, the one table of what each state allows, and the
 * calibration flow that takes a bot from "just switched on" to "hanging on the
 * wall at a known position".
 *
 * The flow is Mural's (its phases SetTopDistance, RetractBelts, ExtendToHome),
 * with the states named for what is true rather than for the screen showing
 * them:
 *
 *   Unconfigured    no anchor distance yet - nothing can be computed
 *   NeedsRetract    distance known, belt lengths not: wind both belts in
 *                   until their ends sit at the stop (jog, by hand)
 *   Retracted       both belts at the stop - their lengths are known
 *                   (RETRACTED_MM each); the pulley can be calibrated here
 *   ExtendingHome   paying out both belts to the lengths of the home position
 *   AtHome          belts at home length, bot not yet on the wall: hang the
 *                   loops over the pins
 *   Ready           hanging at a known position; Cartesian moves allowed
 *
 * Every action is checked against the state before anything moves, and a
 * refusal is a code (see the .cpp), so the UI can grey out what the device
 * would refuse anyway (PLAN.md section 2). "busy" is not a state: any motion
 * in progress refuses every action but stop.
 *
 * The position is not persisted. A restart, or a raw jog from the Hardware
 * tab, sends a homed bot back to NeedsRetract - the honest answer until
 * checkpoints exist (phase 8).
 *
 * What it is not: the job runner (phase 5) or the planner (phase 6). Moves
 * here are point-to-point in belt space - see Motion.h.
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#ifndef PLOTTER_H
#define PLOTTER_H

#include <Arduino.h>
#include <ArduinoJson.h>

#include "Driver.h"

namespace Plotter
{
    enum class State : uint8_t
    {
        Unconfigured,
        NeedsRetract,
        Retracted,
        ExtendingHome,
        AtHome,
        Ready
    };

    /** Reads the anchor distance and pulley from the settings. After Motion::begin(). */
    void begin();

    /** Advances the transitions that wait for a move to end. Call from loop(). */
    void poll();

    /** The pulley or microsteps were changed elsewhere (Hardware tab): re-read. */
    void settingsChanged();

    State state();
    const char *stateName(State s);

    /** For /currentState: state, geometry, position, belts, tilt. */
    void publish(JsonObject out);

    // ------ actions: nullptr on success, otherwise a refusal code ------

    const char *setAnchorDistance(double mm);

    /** Before homing only: wind a belt in (mm < 0) or out. */
    const char *jogBelt(Driver::Side side, double mm);

    /** The user says both belts sit at their stop. */
    const char *confirmRetracted();

    /** Pays both belts out to the home lengths. */
    const char *extendToHome();

    /** The user says the bot hangs on the pins. */
    const char *confirmHung();

    /** Ready only: moves the pen by (dx, dy) mm, within the drawing area. */
    const char *jogXY(double dx, double dy);

    /** Ready only: back to the home position. */
    const char *goHome();

    /**
     * Pulley calibration, from Retracted: pays the left belt out by `mm`, then
     * measured() takes what a tape measure says came out and corrects the
     * diameter by the ratio, and the belt winds back to the stop.
     */
    const char *pulleyExtend(double mm);
    const char *pulleyMeasured(double mm);

    /** Stops any motion. Never refused. */
    void stop();

    /**
     * The position can no longer be trusted - a raw jog from the Hardware tab
     * moved a belt behind the plotter's back. Back to NeedsRetract.
     */
    void forgetPosition();
}

#endif
