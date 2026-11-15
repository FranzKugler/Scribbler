/**
 * Motion
 * The two belts as motors: FastAccelStepper on STEP/DIR, positions in steps
 * and in millimetres of belt, and moves with a ramp.
 *
 * This is phase 3 of PLAN.md: each belt on its own (jog), and stop. What it
 * deliberately does not do yet:
 *
 *   - Cartesian moves. Those need the anchor distance and a homed position
 *     (phase 4), and straight lines need the planner, which slices time and
 *     feeds both steppers' queues in lockstep (phase 6, FastAccelStepper's
 *     moveTimed()).
 *   - Run in a task of its own. Every call into FastAccelStepper comes from
 *     loop() - one caller, which is the case the library is built for (it
 *     documents no locking for move()/moveTo() from a second task). The motion
 *     task arrives with the planner, and then it becomes the only caller.
 *
 * Belt convention: positive steps lengthen the belt. Which DIR level that is
 * depends on how the motors are mounted - the two sides mirror each other, so
 * one of them is inverted (Mural inverts the left). See the constants at the
 * top of Motion.cpp; check on the first board by jogging +10 mm and watching
 * which way the bot goes.
 *
 * Millimetres to steps: 200 full steps x microsteps per pulley turn, and a
 * turn is pi x the effective pulley diameter. Rounded, not truncated as in
 * Mural - truncation biases every belt short by half a step on average.
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#ifndef MOTION_H
#define MOTION_H

#include <Arduino.h>
#include "Driver.h"

namespace Motion
{
    /** Connects both steppers. Call after Driver::begin(). */
    void begin(uint16_t microsteps, double pulleyDiameterMm);

    /** Whether the engine came up and both steppers are connected. */
    bool ready();

    /**
     * Microsteps or pulley changed. Rescales the step positions so the belt
     * lengths they stand for stay the same. Only at standstill; returns false
     * (and changes nothing) while a belt moves.
     */
    bool setScale(uint16_t microsteps, double pulleyDiameterMm);

    double stepsPerMm();

    /**
     * Lengthens (mm > 0) or shortens one belt, ramped. Refused while that belt
     * is already moving: the caller stops first, so a click on the button does
     * not quietly turn into a different move than the one asked for.
     */
    bool jog(Driver::Side side, double mm);

    /** Ramps both belts down to a stop. */
    void stop();

    bool moving();
    bool moving(Driver::Side side);

    int32_t steps(Driver::Side side);
    double millimetres(Driver::Side side);
}

#endif
