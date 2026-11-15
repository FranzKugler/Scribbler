/**
 * Path
 * Straight pen moves in, synchronised belt motion out: the planner (Planner.h)
 * fed from the job, sliced every 5 ms by a task of its own, each slice's end
 * point solved for belt lengths and handed to both steppers as one timed move
 * of the same duration (FastAccelStepper's moveTimed()).
 *
 * Why a task: the steppers' queues hold ~150 ms of slices. loop() also serves
 * the web server, and an SD upload or a directory listing can hold it longer
 * than that - a queue that runs dry at speed stops the motor dead. The task
 * runs on core 1 above loop()'s priority, tops the queues up to 50 ms ahead,
 * and is the only caller of FastAccelStepper while a path is active. It has a
 * Kinematics model of its own, so nothing is shared with loop() but the
 * planner, which a mutex guards.
 *
 * Lockstep: a slice goes to both belts or to neither - only when both queues
 * have room. The integer arithmetic of moveTimed() makes each slice a few
 * ticks off; that drift is carried into the next slice per belt, so the belts
 * never wander apart in time. A direction-change pause the library injects on
 * one belt is mirrored on the other.
 *
 * What it is not: aware of jobs, files or the pen. Plotter pushes segments,
 * calls finish() before anything that needs standstill (a pen change, a
 * pause), and waits for idle().
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#ifndef PATH_H
#define PATH_H

#include <Arduino.h>
#include "Kinematics.h"

namespace Path
{
    /** Creates the task. After Motion::begin(). */
    void begin();

    /**
     * Takes over the steppers with the pen at (x, y) image mm, solved on
     * `geometry` at the current steps/mm. Only while nothing moves. False
     * when the steppers are not idle.
     */
    bool start(double x, double y, const Kinematics::Geometry &geometry, double stepsPerMm);

    /** Hands the steppers back. Only once idle(). */
    void end();

    bool active();

    /** A straight move to (x, y) at up to `speed` mm/s; false when the buffer is full. */
    bool push(double x, double y, double speed);

    /** Plan to a stop at the end of what is buffered. */
    void finish();

    /** Nothing buffered and both belts standing. */
    bool idle();

    /** Emergency: drop the plan and stop both belts at once. Position is then unknown. */
    void abort();

    /** Where the planned pen is: exact once idle(). */
    double x();
    double y();

    /**
     * Slices that found a queue already empty - the motor stopped between two
     * slices. Should stay 0; anything else means the task is starved.
     */
    uint32_t underruns();

    /** A slice no pose could be found for, or a move the library refused. */
    const char *fault();
}

#endif
