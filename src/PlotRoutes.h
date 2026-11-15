/**
 * PlotRoutes
 * The plotter's own controls over HTTP: the calibration flow, jogging, the
 * pen. Each one is a call into Plotter, which checks it against the state.
 *
 *   POST /calib/anchor          {mm}
 *   POST /calib/retracted       both belts sit at the stop
 *   POST /calib/home            pay out to the home lengths
 *   POST /calib/hung            the bot hangs on the pins
 *   POST /calib/pulley/extend   {mm}: pay the left belt out for measuring
 *   POST /calib/pulley          {measured}: correct the diameter, wind back
 *   POST /calib/pen             {up, down}: store the servo angles, each optional
 *   POST /jog                   {side, mm} before homing, {dx, dy} when ready
 *   POST /home                  ready: travel back home
 *   POST /stop                  any motion, never refused
 *   POST /pen                   {state: "up"|"down"} or {angle}
 *
 * The state, position and belts go out with /currentState (Plotter::publish).
 *
 * Open, unlike /hardware: these are the plotter's normal controls, as the
 * colour of the word clock is - and every one of them goes through the state
 * table, so the worst a stranger on the network can do is what the owner
 * could. The raw hardware, which bypasses the table, stays behind the lock.
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#ifndef PLOTROUTES_H
#define PLOTROUTES_H

namespace Plot
{
    /** Hangs the handlers on the server. Call before server.begin(). */
    void begin();
}

#endif
