/**
 * Imu
 * BMI270 through SparkFun's library, which carries Bosch's ~8 kB config blob
 * and uploads it on every begin; see Imu.h for what and why.
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#include "Imu.h"

#include <Wire.h>
#include <SparkFun_BMI270_Arduino_Library.h>

#include "Board.h"
#include "LogBuffer.h"

namespace
{
    // ------ mounting: to be checked on the first board ------
    //
    // The board stands in the wall plane, so gravity lies in its X/Y plane and
    // the tilt is a rotation about its Z axis. Which board axis points up when
    // the bot hangs level, and which way positive runs, follows from where the
    // BMI270 sits in the layout - not known while the PCB is unrouted.
    // Assumed: +Y up when level, +X to the right, Z out of the wall.
    constexpr int AXIS_RIGHT = 0;       // board axis that points right
    constexpr int AXIS_UP = 1;          // board axis that points up
    constexpr float SIGN_RIGHT = 1.0f;
    constexpr float SIGN_UP = 1.0f;
    constexpr float SIGN_RATE = -1.0f;  // gyro Z, counter-clockwise positive, vs. "leans right"

    // ------ filter ------

    constexpr uint32_t SAMPLE_MS = 10;
    // Time constant of the complementary filter. Longer trusts the gyro over
    // a longer stretch of motion; shorter lets the accelerometer pull drift out
    // sooner. Half a second is longer than a swing of the bot and far shorter
    // than the gyro's drift.
    constexpr float TAU_S = 0.5f;

    constexpr float DEG = PI / 180.0f;

    BMI270 imu;
    bool found = false;
    float offset = 0.0f;

    Imu::Reading last;
    float fused = NAN;      // without offset
    uint32_t lastSample = 0;
}

namespace Imu
{
    void begin()
    {
        Wire.beginTransmission(Board::IMU_ADDR);
        if (Wire.endTransmission() != 0)
        {
            debugW("IMU: none at 0x%02X - carrying on without", Board::IMU_ADDR);
            return;
        }
        // Uploads the config blob over I2C - a few tens of milliseconds at
        // boot, the price of the BMI270.
        const int8_t err = imu.beginI2C(Board::IMU_ADDR, Wire);
        if (err != BMI2_OK)
        {
            debugE("IMU: BMI270 did not start (error %d)", err);
            return;
        }
        found = true;
        last.present = true;
        lastSample = millis();
        debugI("IMU: BMI270 at 0x%02X", Board::IMU_ADDR);
    }

    bool present()
    {
        return found;
    }

    void poll()
    {
        if (!found) return;
        const uint32_t now = millis();
        if (now - lastSample < SAMPLE_MS) return;
        const float dt = (now - lastSample) / 1000.0f;
        lastSample = now;

        if (imu.getSensorData() != BMI2_OK) return;

        const float a[3] = {imu.data.accelX, imu.data.accelY, imu.data.accelZ};
        const float g[3] = {imu.data.gyroX, imu.data.gyroY, imu.data.gyroZ};

        const float accelTilt = atan2f(SIGN_RIGHT * a[AXIS_RIGHT], SIGN_UP * a[AXIS_UP]);
        const float rate = SIGN_RATE * g[2] * DEG;

        if (isnan(fused))
        {
            fused = accelTilt;   // first sample: nothing to integrate from
        }
        else
        {
            const float alpha = TAU_S / (TAU_S + dt);
            fused = alpha * (fused + rate * dt) + (1.0f - alpha) * accelTilt;
        }

        for (int i = 0; i < 3; i++)
        {
            last.accel[i] = a[i];
            last.gyro[i] = g[i];
        }
        last.tiltAccel = accelTilt - offset;
        last.tilt = fused - offset;
        last.rate = rate;
        last.samples++;
    }

    Reading reading()
    {
        return last;
    }

    void setOffset(float radians)
    {
        offset = radians;
    }
}
