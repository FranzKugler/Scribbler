/**
 * HardwareRoutes
 * The /hardware endpoints: what the drivers, the pen, the IMU and the display
 * report, and the few things worth doing to them by hand - motors on and off,
 * pen up, down or to an angle, current and microsteps.
 *
 * For bring-up and diagnosis, which is why all of it sits behind the expert
 * lock: a request that switches the motors on is not one anybody on the
 * network should be able to send. The plotter's own controls - jog, homing,
 * the calibration flow - come with the state machine and live in PlotRoutes,
 * where each action is checked against the state that allows it. Nothing
 * here knows about states; it talks to the hardware directly.
 *
 *   GET  /hardware            everything below, read fresh
 *   POST /hardware/motors     {enabled}
 *   POST /hardware/pen        {state: "up"|"down"} or {angle}
 *   POST /hardware/config     {motorCurrent, microsteps, penUp, penDown}, each optional
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#ifndef HARDWAREROUTES_H
#define HARDWAREROUTES_H

namespace Hw
{
    /** Hangs the handlers on the server. Call before server.begin(). */
    void begin();
}

#endif
