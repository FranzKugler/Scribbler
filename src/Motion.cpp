/**
 * Motion
 * FastAccelStepper for both belts; see Motion.h for what and why.
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#include "Motion.h"

#include <algorithm>
#include <FastAccelStepper.h>

#include "Board.h"
#include "LogBuffer.h"

namespace
{
    // ------ mounting: to be checked on the first board ------
    // true: DIR high counts up, i.e. lengthens the belt. The sides mirror each
    // other, so they differ; which one is which is a guess until a motor turns.
    constexpr bool LEFT_DIR_HIGH_LENGTHENS = false;
    constexpr bool RIGHT_DIR_HIGH_LENGTHENS = true;

    constexpr double FULL_STEPS_PER_TURN = 200.0;

    // Jog speed and ramp, in belt millimetres. Mural travels at 2000 steps/s
    // at 1/8, ~40 mm/s, with no ramp at all; a jog by hand wants less of both.
    constexpr double JOG_SPEED_MM_S = 30.0;
    constexpr double JOG_ACCEL_MM_S2 = 300.0;

    // The engine's task on core 1, away from the WiFi stack on core 0: a burst
    // of radio work must not delay the refill of the step queues.
    constexpr uint8_t ENGINE_CORE = 1;

    FastAccelStepperEngine engine;
    FastAccelStepper *stepper[Driver::COUNT] = {nullptr, nullptr};

    uint16_t microsteps = 16;
    double pulleyDiameter = 10.14926;
    double perMm = 0.0;

    double computeStepsPerMm(uint16_t ms, double diameter)
    {
        return FULL_STEPS_PER_TURN * ms / (PI * diameter);
    }

    /**
     * Speed and ramp in belt millimetres, converted at the current scale, so a
     * move is the same in mm/s at any microstep setting. Set before every move:
     * a later move may have left the other belt's profile scaled down.
     */
    void setProfile(FastAccelStepper *s, double speedMmS, double accelMmS2)
    {
        // At least one step per second and per second squared: zero is
        // refused by the library, and a belt with almost no way to go still
        // has to get there.
        s->setSpeedInHz((uint32_t)std::max(1L, lround(speedMmS * perMm)));
        s->setAcceleration((int32_t)std::max(1L, lround(accelMmS2 * perMm)));
    }
}

namespace Motion
{
    void begin(uint16_t ms, double diameter)
    {
        microsteps = ms;
        pulleyDiameter = diameter;
        perMm = computeStepsPerMm(ms, diameter);

        engine.init(ENGINE_CORE);

        const uint8_t stepPin[Driver::COUNT] = {Board::STEP_LEFT, Board::STEP_RIGHT};
        const uint8_t dirPin[Driver::COUNT] = {Board::DIR_LEFT, Board::DIR_RIGHT};
        const bool dirHigh[Driver::COUNT] = {LEFT_DIR_HIGH_LENGTHENS, RIGHT_DIR_HIGH_LENGTHENS};

        for (uint8_t i = 0; i < Driver::COUNT; i++)
        {
            stepper[i] = engine.stepperConnectToPin(stepPin[i]);
            if (!stepper[i])
            {
                debugE("Motion: no step generator for the %s belt (GPIO%u)",
                       i == Driver::LEFT ? "left" : "right", stepPin[i]);
                continue;
            }
            stepper[i]->setDirectionPin(dirPin[i], dirHigh[i]);
            // No enable pin here: ENN is shared and belongs to Driver, which
            // switches both drivers at once.
            stepper[i]->setCurrentPosition(0);
        }

        debugI("Motion: %.3f steps/mm (1/%u, pulley %.4f mm)", perMm, microsteps, pulleyDiameter);
    }

    bool ready()
    {
        return stepper[Driver::LEFT] && stepper[Driver::RIGHT];
    }

    bool setScale(uint16_t ms, double diameter)
    {
        if (moving()) return false;

        const double next = computeStepsPerMm(ms, diameter);
        for (FastAccelStepper *s : stepper)
        {
            if (!s) continue;
            const double mm = s->getCurrentPosition() / perMm;
            s->setCurrentPosition((int32_t)lround(mm * next));
        }
        microsteps = ms;
        pulleyDiameter = diameter;
        perMm = next;
        debugI("Motion: %.3f steps/mm (1/%u, pulley %.4f mm)", perMm, microsteps, pulleyDiameter);
        return true;
    }

    double stepsPerMm()
    {
        return perMm;
    }

    bool jog(Driver::Side side, double mm)
    {
        FastAccelStepper *s = stepper[side];
        if (!s || s->isRunning()) return false;
        const int32_t delta = (int32_t)lround(mm * perMm);
        if (delta == 0) return true;
        setProfile(s, JOG_SPEED_MM_S, JOG_ACCEL_MM_S2);
        // 0 is success in every version of the result code - a plain int in
        // the 0.x series, MoveResultCode since; the cast covers both spellings.
        return static_cast<int>(s->move(delta)) == 0;
    }

    bool moveBelts(double leftMm, double rightMm, double speedMmS, double accelMmS2)
    {
        if (!ready() || moving()) return false;

        const int32_t target[Driver::COUNT] = {(int32_t)lround(leftMm * perMm),
                                               (int32_t)lround(rightMm * perMm)};
        int32_t way[Driver::COUNT];
        int32_t longest = 0;
        for (uint8_t i = 0; i < Driver::COUNT; i++)
        {
            way[i] = abs(target[i] - stepper[i]->getCurrentPosition());
            longest = std::max(longest, way[i]);
        }
        if (longest == 0) return true;

        // Scaled copies of one trapezoid take the same time for their ways:
        // both phases - ramp and cruise - shrink by the same factor.
        for (uint8_t i = 0; i < Driver::COUNT; i++)
        {
            if (way[i] == 0) continue;
            const double share = (double)way[i] / longest;
            setProfile(stepper[i], speedMmS * share, accelMmS2 * share);
        }
        bool ok = true;
        for (uint8_t i = 0; i < Driver::COUNT; i++)
        {
            if (way[i] == 0) continue;
            ok = static_cast<int>(stepper[i]->moveTo(target[i])) == 0 && ok;
        }
        return ok;
    }

    bool setLengths(double leftMm, double rightMm)
    {
        if (!ready() || moving()) return false;
        stepper[Driver::LEFT]->setCurrentPosition((int32_t)lround(leftMm * perMm));
        stepper[Driver::RIGHT]->setCurrentPosition((int32_t)lround(rightMm * perMm));
        return true;
    }

    void stop()
    {
        for (FastAccelStepper *s : stepper)
        {
            if (s) s->stopMove();
        }
    }

    bool moving()
    {
        return moving(Driver::LEFT) || moving(Driver::RIGHT);
    }

    bool moving(Driver::Side side)
    {
        return stepper[side] && stepper[side]->isRunning();
    }

    int32_t steps(Driver::Side side)
    {
        return stepper[side] ? stepper[side]->getCurrentPosition() : 0;
    }

    double millimetres(Driver::Side side)
    {
        return steps(side) / perMm;
    }
}
