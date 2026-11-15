/**
 * Planner
 * See Planner.h for what and why.
 *
 * @mc       ESP32S3 (and the host, for the tests)
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#include "Planner.h"

#include <cmath>

namespace
{
    // Below this a move is noise, not a segment: its direction is undefined.
    constexpr double MIN_LENGTH = 1e-6;   // mm

    // A floor for a junction that would otherwise be planned at zero speed
    // when it does not have to be - and the speed every stop starts from.
    constexpr double MIN_JUNCTION_SPEED = 0.0;

    double sq(double v) { return v * v; }
}

namespace Planner
{
    void Queue::setPosition(double x, double y)
    {
        if (!idle()) return;
        _endX = x;
        _endY = y;
    }

    bool Queue::push(double x, double y, double speed)
    {
        if (full()) return false;

        const double dx = x - _endX, dy = y - _endY;
        const double length = std::sqrt(dx * dx + dy * dy);
        if (length < MIN_LENGTH || !(speed > 0.0)) return true;

        Block b{};
        b.x0 = _endX;
        b.y0 = _endY;
        b.ux = dx / length;
        b.uy = dy / length;
        b.length = length;
        b.vMax = speed;
        b.frozen = false;

        // Junction with the previous block, if there is one to meet.
        if (_count == 0)
        {
            b.vEntryMax = 0.0;   // from standstill
        }
        else
        {
            const Block &prev = at(_count - 1);
            // cos of the angle between the two paths as GRBL defines it:
            // -1 straight on, +1 straight back.
            const double cosTheta = -(prev.ux * b.ux + prev.uy * b.uy);
            double v;
            if (cosTheta > 0.999999)
            {
                v = MIN_JUNCTION_SPEED;                  // reversal: stop
            }
            else if (cosTheta < -0.999999)
            {
                v = 1e9;                                 // straight on: no limit
            }
            else
            {
                const double sinHalf = std::sqrt(0.5 * (1.0 - cosTheta));
                v = std::sqrt(_limits.accel * _limits.junctionDeviation * sinHalf / (1.0 - sinHalf));
            }
            b.vEntryMax = std::fmin(v, std::fmin(b.vMax, prev.vMax));
        }

        if (_count == 0) _headEntry = 0.0;   // from standstill
        at(_count) = b;
        _count++;
        _endX = x;
        _endY = y;
        _finishing = false;
        replan();
        return true;
    }

    bool Queue::ready() const
    {
        if (_count == 0) return false;
        if (at(0).frozen || _headEntry > 0.0) return true;   // moving: never starve it
        return _finishing || full();
    }

    void Queue::finish()
    {
        _finishing = true;
        replan();
    }

    size_t Queue::stopSoon(Target *dropped, size_t max)
    {
        if (_count == 0) return 0;
        const double a = _limits.accel;

        // Where the pen is in the head block, and how fast.
        Block &h = at(0);
        double s = 0.0, v = _headEntry;
        if (h.frozen) s = position(h, _time, v);

        // Cut the head there: the rest of it is a fresh block, entered at the
        // current speed, which replan() may give a new profile. Its junction
        // limit no longer applies - the pen is past that corner.
        h.x0 += h.ux * s;
        h.y0 += h.uy * s;
        h.length -= s;
        h.frozen = false;
        _time = 0.0;
        _headEntry = v;
        if (h.length < MIN_LENGTH)
        {
            // Exactly at the head's end: the next block is the head now, and
            // the planned junction speed at its start is the current one.
            _head = (_head + 1) % CAPACITY;
            _count--;
            if (_count == 0)
            {
                _headEntry = 0.0;
                return 0;
            }
        }

        // How far a stop from v takes. A hair more, so rounding never asks
        // the head to start slower than the pen already moves.
        const double need = sq(v) / (2.0 * a) * (1.0 + 1e-9) + 1e-9;

        size_t n = 0;
        auto drop = [&](const Block &b) {
            if (n < max) dropped[n++] = {b.x0 + b.ux * b.length, b.y0 + b.uy * b.length, b.vMax};
        };

        double covered = 0.0;
        for (size_t i = 0; i < _count; i++)
        {
            Block &b = at(i);
            if (covered + b.length < need)
            {
                covered += b.length;
                continue;
            }
            // The stop lands in block i: shorten it, drop what follows.
            const double keep = need - covered;
            size_t kept = i + 1;
            if (keep < b.length - MIN_LENGTH)
            {
                drop(b);
                if (keep < MIN_LENGTH && i > 0)
                {
                    kept = i;   // the stop is at the block's start: drop it whole
                }
                else
                {
                    b.length = keep > MIN_LENGTH ? keep : MIN_LENGTH;
                }
            }
            for (size_t j = i + 1; j < _count; j++) drop(at(j));
            _count = kept;
            break;
        }

        const Block &last = at(_count - 1);
        _endX = last.x0 + last.ux * last.length;
        _endY = last.y0 + last.uy * last.length;
        _finishing = true;
        replan();
        return n;
    }

    void Queue::clear()
    {
        _head = 0;
        _count = 0;
        _time = 0.0;
        _headEntry = 0.0;
        _finishing = false;
    }

    void Queue::replan()
    {
        if (_count == 0) return;
        const double a = _limits.accel;

        // Backward: the buffer ends in a stop; every block must be able to
        // slow from its entry to the next block's entry within its length.
        double next = 0.0;
        for (size_t i = _count; i-- > 0;)
        {
            Block &b = at(i);
            // A frozen block's profile cannot change - and only the head can
            // be frozen, so nothing before it is left to plan.
            if (b.frozen) break;
            b.vExit = next;
            double entryMax = b.vEntryMax;
            // The head starts at the speed the pen arrives with, whatever its
            // junction would have allowed.
            if (i == 0) entryMax = _headEntry;
            b.vEntry = std::fmin(entryMax, std::sqrt(sq(next) + 2.0 * a * b.length));
            next = b.vEntry;
        }

        // Forward: and reach it from the previous block's entry.
        double prevExit = _headEntry;
        for (size_t i = 0; i < _count; i++)
        {
            Block &b = at(i);
            if (b.frozen)
            {
                prevExit = b.vExit;   // as frozen
                continue;
            }
            b.vEntry = std::fmin(b.vEntry, prevExit);
            b.vExit = std::fmin(b.vExit, std::sqrt(sq(b.vEntry) + 2.0 * a * b.length));
            prevExit = b.vExit;
            // The next block's entry is this one's exit.
            if (i + 1 < _count) at(i + 1).vEntry = std::fmin(at(i + 1).vEntry, b.vExit);
        }
    }

    void Queue::freeze(Block &b)
    {
        const double a = _limits.accel;
        const double v0 = b.vEntry, v1 = b.vExit;

        // The highest speed the length allows between the two ends, capped at
        // the requested one: a trapezoid, or a triangle when it is short.
        double vc = std::sqrt((2.0 * a * b.length + sq(v0) + sq(v1)) / 2.0);
        if (vc > b.vMax) vc = b.vMax;
        if (vc < v0) vc = v0;   // rounding: never cruise below the ends
        if (vc < v1) vc = v1;

        const double dAccel = (sq(vc) - sq(v0)) / (2.0 * a);
        const double dDecel = (sq(vc) - sq(v1)) / (2.0 * a);
        double dCruise = b.length - dAccel - dDecel;
        if (dCruise < 0.0) dCruise = 0.0;   // rounding only

        b.vCruise = vc;
        b.tAccel = (vc - v0) / a;
        b.tDecel = (vc - v1) / a;
        b.tCruise = vc > 0.0 ? dCruise / vc : 0.0;
        b.frozen = true;
    }

    double Queue::position(const Block &b, double t, double &speed) const
    {
        const double a = _limits.accel;
        if (t <= b.tAccel)
        {
            speed = b.vEntry + a * t;
            return b.vEntry * t + 0.5 * a * t * t;
        }
        const double sA = b.vEntry * b.tAccel + 0.5 * a * sq(b.tAccel);
        t -= b.tAccel;
        if (t <= b.tCruise)
        {
            speed = b.vCruise;
            return sA + b.vCruise * t;
        }
        const double sC = b.vCruise * b.tCruise;
        t -= b.tCruise;
        if (t > b.tDecel) t = b.tDecel;
        speed = b.vCruise - a * t;
        const double s = sA + sC + b.vCruise * t - 0.5 * a * t * t;
        return s < b.length ? s : b.length;
    }

    bool Queue::slice(double dt, Slice &out)
    {
        out.dt = 0.0;
        if (_count == 0)
        {
            out.x = _endX;
            out.y = _endY;
            out.speed = 0.0;
            return false;
        }

        double left = dt;
        double speed = 0.0;
        while (left > 0.0 && _count > 0)
        {
            Block &b = at(0);
            if (!b.frozen)
            {
                freeze(b);
                _time = 0.0;
                // The blocks behind it re-plan against its fixed exit.
                replan();
            }
            const double remaining = duration(b) - _time;
            if (remaining > left)
            {
                _time += left;
                out.dt += left;
                left = 0.0;
                const double s = position(b, _time, speed);
                out.x = b.x0 + b.ux * s;
                out.y = b.y0 + b.uy * s;
            }
            else
            {
                // This block ends inside the slice: land exactly on its end,
                // so rounding never accumulates across blocks.
                out.dt += remaining;
                left -= remaining;
                out.x = b.x0 + b.ux * b.length;
                out.y = b.y0 + b.uy * b.length;
                speed = b.vExit;
                _headEntry = _count > 1 ? b.vExit : 0.0;
                _head = (_head + 1) % CAPACITY;
                _count--;
                _time = 0.0;
            }
        }
        out.speed = speed;
        return true;
    }
}
