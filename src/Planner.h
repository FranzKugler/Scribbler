/**
 * Planner
 * Straight segments in, a smooth motion out: a trapezoidal speed profile per
 * segment, junction speeds from the angle between segments, look-ahead over
 * the buffered ones, and the result cut into time slices.
 *
 * This is the part of phase 6 (PLAN.md) that needs no hardware, and so it is
 * pure C++ with host tests, like Kinematics. The firmware side - a motion task
 * that asks for a slice every few milliseconds, solves the belt lengths at its
 * end and hands both steppers the same slice with FastAccelStepper's
 * moveTimed() - is built on this and adds nothing to the maths.
 *
 * It plans the *pen*, in image millimetres. Belts appear only when a slice's
 * end point goes through Kinematics (CLAUDE.md: "Do not let belt lengths leak
 * into the planner"). Speed and acceleration limits are therefore limits on
 * the pen; what they mean for a belt depends on where on the wall the bot is.
 *
 * The method is the one GRBL made standard:
 *
 *   - Junction speed. Where two segments meet at an angle, the pen may pass at
 *     the speed of a circle that deviates `junctionDeviation` from the corner:
 *     v^2 = a * delta * sin(theta/2) / (1 - sin(theta/2)). A straight
 *     continuation passes at full speed, a reversal stops. Mural stops at every
 *     millimetre instead.
 *   - Look-ahead. Each new segment re-plans the buffer: a backward pass makes
 *     every block able to slow to the next one's entry speed, a forward pass
 *     makes it reachable from the previous one's. The end of the buffer is
 *     always treated as a stop, so the pen never runs at speed into what the
 *     planner has not seen yet.
 *   - Freezing. A block's profile is fixed when it starts to run - a slice
 *     already handed to the motors cannot be re-planned.
 *
 * What it is not: aware of the pen, files, time or Arduino. A pen change is a
 * stop: the caller calls finish(), slices until idle(), moves the pen, and goes
 * on pushing. The planner never knows why.
 *
 * @mc       ESP32S3 (and the host, for the tests)
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#ifndef PLANNER_H
#define PLANNER_H

#include <stddef.h>

namespace Planner
{
    struct Limits
    {
        double accel = 500.0;              // pen acceleration [mm/s^2]
        double junctionDeviation = 0.05;   // [mm]; larger corners faster, rounder
    };

    /** Where the slice ended, and how much of the asked-for time it used. */
    struct Slice
    {
        double x = 0.0;
        double y = 0.0;
        double dt = 0.0;     // <= the requested dt; less only when the buffer ran out
        double speed = 0.0;  // pen speed at the end of the slice [mm/s]
    };

    /** A move's target, as pushed: what stopSoon() hands back for later. */
    struct Target
    {
        double x = 0.0;
        double y = 0.0;
        double speed = 0.0;
    };

    class Queue
    {
    public:
        // Look-ahead depth. Deep enough that a run of 1 mm pieces (curves come
        // as those) still plans over a stopping distance at drawing speeds:
        // 64 mm stops from 200 mm/s at 500 mm/s^2 with room to spare.
        static constexpr size_t CAPACITY = 64;

        explicit Queue(const Limits &limits = Limits()) : _limits(limits) {}

        /** Where the pen is when nothing is buffered. Only while idle(). */
        void setPosition(double x, double y);

        void setLimits(const Limits &limits) { _limits = limits; }

        /**
         * A straight move from the end of the buffer to (x, y) at up to `speed`
         * mm/s. False when the buffer is full - slice first. A move of zero
         * length is accepted and dropped.
         */
        bool push(double x, double y, double speed);

        bool full() const { return _count == CAPACITY; }
        size_t size() const { return _count; }

        /** Nothing buffered, nothing running: the pen stands still. */
        bool idle() const { return _count == 0; }

        /**
         * Whether the caller should hand out the next slice now or keep
         * pushing. From standstill: only once the buffer is full or finish()
         * was called - more look-ahead is still to be had. Once moving: always,
         * because a pen in motion must never be starved; the end of the buffer
         * is a planned stop anyway, so slicing a short buffer is safe, only
         * slower.
         */
        bool ready() const;

        /** No more segments are coming for now: plan the buffer to a stop. */
        void finish();

        /**
         * Stop as soon as the acceleration allows, on the path: the running
         * block is cut where the pen is, the plan ends v²/2a further along
         * (through corners if need be), everything beyond is dropped. The
         * dropped targets go to `dropped` (at most `max`, in order) so that
         * the caller can push them again to carry on where the pen stopped -
         * first the end of the block that was cut short, then the rest.
         * Returns how many. For pause and cancel: finish() would run the
         * whole buffer out first, up to CAPACITY moves.
         */
        size_t stopSoon(Target *dropped, size_t max);

        /**
         * Forgets everything buffered, at once - for an emergency stop, after
         * which the caller no longer knows where the pen is anyway.
         */
        void clear();

        /** Advances by up to dt seconds. Returns false (and dt 0) when idle. */
        bool slice(double dt, Slice &out);

        /** The end point of everything buffered - where the next push starts. */
        double endX() const { return _endX; }
        double endY() const { return _endY; }

    private:
        struct Block
        {
            double x0, y0;          // start
            double ux, uy;          // unit direction
            double length;
            double vMax;            // requested speed
            double vEntryMax;       // junction and speed limit on the entry
            double vEntry, vExit;   // planned
            // Frozen profile: accelerate, cruise, decelerate.
            double vCruise, tAccel, tCruise, tDecel;
            bool frozen;
        };

        Block &at(size_t i) { return _blocks[(_head + i) % CAPACITY]; }
        const Block &at(size_t i) const { return _blocks[(_head + i) % CAPACITY]; }

        void replan();
        void freeze(Block &b);
        double position(const Block &b, double t, double &speed) const;
        double duration(const Block &b) const { return b.tAccel + b.tCruise + b.tDecel; }

        Limits _limits;
        Block _blocks[CAPACITY];
        size_t _head = 0;
        size_t _count = 0;
        double _endX = 0.0, _endY = 0.0;
        double _time = 0.0;        // into the running block
        // The speed the head block starts at while it is not frozen yet: the
        // exit of the block that ran before it, 0 from standstill. Without it
        // a push landing between two blocks would re-plan the head from 0
        // while the pen arrives at speed.
        double _headEntry = 0.0;
        bool _finishing = false;
    };
}

#endif
