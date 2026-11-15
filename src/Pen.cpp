/**
 * Pen
 * The servo on LEDC; see Pen.h for what and why.
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#include "Pen.h"

#include "Board.h"
#include "LogBuffer.h"

namespace
{
    // A standard hobby servo: 50 Hz, 0.5..2.5 ms for 0..180 degrees. The
    // MG90S Mural uses covers about that; a servo that buzzes at either end
    // wants the range narrowed here, not a smaller angle in the settings.
    constexpr uint32_t PWM_HZ = 50;
    constexpr uint8_t PWM_BITS = 14;
    constexpr float PULSE_MIN_US = 500.0f;
    constexpr float PULSE_MAX_US = 2500.0f;

    // How fast the ramp moves. Slow on purpose: a pen slammed onto the wall
    // leaves a dot, and the bot swings on its belts with every jolt.
    constexpr float DEGREES_PER_SECOND = 120.0f;

    // After the ramp, how long the servo is given to actually arrive. Mural
    // waits 200 ms for the same reason.
    constexpr uint32_t SETTLE_MS = 200;

    uint8_t upAngle = 90;
    uint8_t downAngle = 70;

    float current = 90.0f;
    uint8_t goal = 90;
    uint32_t lastStep = 0;
    uint32_t arrivedAt = 0;   // 0 while the ramp runs

    void write(float angle)
    {
        const float us = PULSE_MIN_US + (PULSE_MAX_US - PULSE_MIN_US) * angle / 180.0f;
        const uint32_t duty = (uint32_t)(us / 20000.0f * ((1u << PWM_BITS) - 1));
        ledcWrite(Board::SERVO_PWM, duty);
    }

    void start(uint8_t angle)
    {
        if (angle > 180) angle = 180;
        goal = angle;
        lastStep = millis();
        arrivedAt = 0;
    }
}

namespace Pen
{
    void begin(uint8_t up, uint8_t down)
    {
        setAngles(up, down);
        if (!ledcAttach(Board::SERVO_PWM, PWM_HZ, PWM_BITS))
        {
            debugE("Pen: could not attach the servo PWM");
            return;
        }
        // Straight to up, unramped: at power-on nobody knows where the arm
        // is, and up is the one position that cannot draw on the wall.
        current = upAngle;
        goal = upAngle;
        write(current);
        arrivedAt = millis();
    }

    void setAngles(uint8_t up, uint8_t down)
    {
        upAngle = up > 180 ? 180 : up;
        downAngle = down > 180 ? 180 : down;
    }

    void up()   { start(upAngle); }
    void down() { start(downAngle); }
    void moveTo(uint8_t angle) { start(angle); }

    void poll()
    {
        if (arrivedAt) return;

        const uint32_t now = millis();
        const float step = DEGREES_PER_SECOND * (now - lastStep) / 1000.0f;
        lastStep = now;

        if (fabsf(goal - current) <= step)
        {
            current = goal;
            arrivedAt = now == 0 ? 1 : now;
        }
        else
        {
            current += goal > current ? step : -step;
        }
        write(current);
    }

    bool settled()
    {
        return arrivedAt && millis() - arrivedAt >= SETTLE_MS;
    }

    float angle()     { return current; }
    uint8_t target()  { return goal; }
    bool isDown()     { return goal == downAngle && goal != upAngle; }
}
