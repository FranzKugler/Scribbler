/**
 * Pen
 * The pen-lift servo: an angle, two named positions, and a ramp between them.
 *
 * Mural's slowUp()/slowDown() block for the length of the ramp plus 200 ms.
 * Here the ramp runs in poll() and settled() says when it is over, so the
 * motion side can wait for a pen segment without stopping everything else -
 * the pen is a segment in the same queue as the moves (PLAN.md section 2),
 * and that queue must not stall the web server.
 *
 * What it is not: aware of the plot. It does not know whether the pen should
 * be down, only where it was told to go.
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#ifndef PEN_H
#define PEN_H

#include <Arduino.h>

namespace Pen
{
    /** Attaches the servo and moves it straight to `upAngle` - no ramp at boot. */
    void begin(uint8_t upAngle, uint8_t downAngle);

    /** The two named positions, from the settings. Does not move the pen. */
    void setAngles(uint8_t upAngle, uint8_t downAngle);

    void up();
    void down();

    /** Any angle 0..180, ramped - for calibrating the down position. */
    void moveTo(uint8_t angle);

    /** Advances the ramp. Call from loop(). */
    void poll();

    /** The ramp has ended and the servo has had time to get there. */
    bool settled();

    /** Where the ramp is now, and where it is going. */
    float angle();
    uint8_t target();
    bool isDown();
}

#endif
