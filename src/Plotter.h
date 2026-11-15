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
 *   Plotting        a job runs (JobRunner checks it first, then this draws it)
 *   Paused          a job waits, pen up, at a known position
 *
 * Every action is checked against the state before anything moves, and a
 * refusal is a code (see the .cpp), so the UI can grey out what the device
 * would refuse anyway (PLAN.md section 2). "busy" is not a state: any motion
 * in progress refuses every action but stop.
 *
 * The position is not persisted. A restart, a raw jog from the Hardware tab,
 * or an emergency stop in the middle of a move sends a homed bot back to
 * NeedsRetract - the honest answer until checkpoints exist (phase 8). Pausing
 * or stopping a job does not: both wait for the current millimetre to end, so
 * the position stays known.
 *
 * Drawing: every move of a job, pen up or down, goes to Path - the planner
 * with look-ahead and a task that slices it onto both belts (Path.h). A pen
 * change needs standstill, so the path runs dry first; so do pause and cancel,
 * which therefore finish what is already planned (at most Planner's 64
 * buffered moves) before the pen lifts - the position stays known. Moves
 * outside a job (homing, jogs) are still point-to-point in belt space.
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
        Ready,
        Plotting,
        Paused
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

    /** Ready only: checks /jobs/<name> and draws it. */
    const char *startJob(const char *name);

    /** Once the planned moves have run out, pen up, wait. */
    const char *pauseJob();
    const char *resumeJob();

    /** Once the planned moves have run out, pen up, give the job up - position kept. */
    const char *stopJob();

    /** Drawing speed of the pen [mm/s]. */
    const char *setDrawSpeed(double mmPerS);

    /**
     * Emergency stop: ramps both belts down now. A move cut short leaves the
     * bot somewhere along it, so the position is forgotten (NeedsRetract); a
     * job is given up. Never refused.
     */
    void stop();

    /**
     * The position can no longer be trusted - a raw jog from the Hardware tab
     * moved a belt behind the plotter's back. Back to NeedsRetract.
     */
    void forgetPosition();
}

#endif
