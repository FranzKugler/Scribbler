/**
 * Imu
 * The BMI270 on the bot: its tilt in the wall plane, measured rather than
 * modelled (PLAN.md section 0.3).
 *
 * The kinematic model computes the tilt gamma from a torque balance; this
 * measures it. The two together validate the model, and later calibrate it -
 * a model that disagrees with the measurement across the area points at a
 * wrong anchor distance or pulley diameter.
 *
 * Fused: the accelerometer gives the tilt only at standstill, because while the
 * bot moves it sees path acceleration on top of gravity; the gyro gives the
 * rate of the tilt at all times but drifts. A complementary filter takes the
 * gyro for the fast part and the accelerometer for the slow one.
 *
 * What it is not: an input to the motion. It observes. Closing the loop on it
 * is a decision for later, made with data.
 *
 * Axis mapping and signs depend on how the board is mounted on the bot, and
 * are not known yet: see the constants at the top of Imu.cpp, and check them on
 * the first build - hang the bot level, the tilt must read ~0; lean it to the
 * right, it must read positive, as in Kinematics.
 *
 * Expects Wire to be started (main.cpp does, once, for display and IMU).
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#ifndef IMU_H
#define IMU_H

#include <Arduino.h>

namespace Imu
{
    struct Reading
    {
        bool present = false;
        float tilt = NAN;        // fused tilt [rad], > 0 leans right, mounting offset applied
        float tiltAccel = NAN;   // the accelerometer's tilt alone [rad], offset applied
        float rate = NAN;        // tilt rate from the gyro [rad/s]
        float accel[3] = {NAN, NAN, NAN};   // raw, board axes [g]
        float gyro[3] = {NAN, NAN, NAN};    // raw, board axes [deg/s]
        uint32_t samples = 0;
    };

    /** Probes the bus, uploads Bosch's configuration, starts sampling. */
    void begin();
    bool present();

    /** Samples when due (~100 Hz) and updates the filter. Call from loop(). */
    void poll();

    Reading reading();

    /**
     * The board is never mounted exactly level on the bot. The offset [rad] is
     * subtracted from every tilt; calibrate it where the model predicts zero.
     */
    void setOffset(float radians);
}

#endif
