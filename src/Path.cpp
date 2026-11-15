/**
 * Path
 * The planner's slices onto both steppers; see Path.h for what and why.
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#include "Path.h"

#include <atomic>
#include <FastAccelStepper.h>
#include <freertos/semphr.h>

#include "Planner.h"
#include "Motion.h"
#include "LogBuffer.h"

namespace
{
    // One slice: short enough that a straight line between two solved belt
    // positions is the pen's line to well under a step, long enough that the
    // kinematics (~0.5 ms in software doubles) is a tenth of the time.
    constexpr double SLICE_S = 0.005;

    // How far ahead the queues are kept, and how often the task looks. The
    // margin between them is what a busy moment of the task itself may cost.
    constexpr uint32_t AHEAD_TICKS = TICKS_PER_S / 20;   // 50 ms
    constexpr TickType_t PERIOD = pdMS_TO_TICKS(2);

    // Leave room for moveTimed()'s two reserved slots and a slice that needs
    // two entries (more than 255 steps).
    constexpr uint8_t ROOM = QUEUE_LEN - 6;

    // Pen acceleration. Mural has none; a jolt-free start matters more on a
    // hanging bot than on a gantry, because the bot swings.
    constexpr double ACCEL_MM_S2 = 300.0;

    constexpr int TASK_PRIORITY = 5;     // above loop() (1)
    constexpr uint32_t TASK_STACK = 6144;

    SemaphoreHandle_t lock = nullptr;
    Planner::Queue planner;
    Kinematics::Model model;
    double perMm = 0.0;

    // Written by loop() and the task, read by both: atomic rather than volatile.
    std::atomic<bool> running{false};   // a path owns the steppers
    bool started = false;            // the queues have been kicked off
    int32_t committed[Driver::COUNT] = {0, 0};
    int32_t drift[Driver::COUNT] = {0, 0};
    double planX = 0.0, planY = 0.0;
    std::atomic<uint32_t> underrunCount{0};
    const char *faultCode = nullptr;

    FastAccelStepper *stepper(uint8_t i)
    {
        return Motion::stepperFor((Driver::Side)i);
    }

    bool isPause(MoveTimedResultCode rc)
    {
        return rc == MoveTimedResultCode::DirChangePauseInjected ||
               rc == MoveTimedResultCode::DirPin2msPauseAdded;
    }

    /**
     * One timed move on one belt, with the library's pause retries (the
     * pattern its header documents: an injected pause is not the move, retry).
     * `actual` gets the ticks the move itself took, `pause` those of any pause
     * injected before it. False when the library refused.
     */
    bool timed(uint8_t i, int32_t steps, uint32_t duration, bool start, uint32_t &actual, uint32_t &pause)
    {
        actual = 0;
        pause = 0;
        for (int attempt = 0; attempt < 4; attempt++)
        {
            uint32_t ticks = 0;
            const MoveTimedResultCode rc = stepper(i)->moveTimed((int16_t)steps, duration, &ticks, start);
            if (isPause(rc))
            {
                pause += ticks;
                continue;
            }
            if (rc == MoveTimedResultCode::OK || rc == MoveTimedResultCode::MoveEmpty)
            {
                if (rc == MoveTimedResultCode::MoveEmpty && started) underrunCount++;
                actual = ticks;
                return true;
            }
            debugE("Path: moveTimed refused on belt %u: %s", i, toString(rc));
            return false;
        }
        return false;
    }

    /** Slices while both queues have room and are short of AHEAD_TICKS. */
    void feed()
    {
        while (running && !faultCode && planner.ready())
        {
            FastAccelStepper *l = stepper(Driver::LEFT), *r = stepper(Driver::RIGHT);
            if (l->queueEntries() >= ROOM || r->queueEntries() >= ROOM) return;
            if (started && l->ticksInQueue() >= AHEAD_TICKS && r->ticksInQueue() >= AHEAD_TICKS) return;

            Planner::Slice s;
            if (!planner.slice(SLICE_S, s)) return;

            const Kinematics::Pose pose = model.solve(s.x, s.y);
            if (!pose.converged)
            {
                faultCode = "unreachable";
                return;
            }
            planX = s.x;
            planY = s.y;

            const int32_t target[Driver::COUNT] = {(int32_t)lround(pose.left * perMm),
                                                   (int32_t)lround(pose.right * perMm)};
            const uint32_t ticks = (uint32_t)lround(s.dt * TICKS_PER_S);

            // First slice after an idle stretch: fill both queues without
            // starting them, then start both - so they set off together.
            const bool startNow = started;
            uint32_t pause[Driver::COUNT] = {0, 0};
            for (uint8_t i = 0; i < Driver::COUNT; i++)
            {
                // The slice, corrected by what the last ones came out off by.
                const int32_t want = (int32_t)ticks + drift[i];
                const uint32_t duration = want > 0 ? (uint32_t)want : ticks;
                uint32_t actual;
                if (!timed(i, target[i] - committed[i], duration, startNow, actual, pause[i]))
                {
                    faultCode = "motion";
                    return;
                }
                drift[i] = (int32_t)duration - (int32_t)actual;
                committed[i] = target[i];
            }
            // A pause injected on one belt delays it; delay the other as much.
            for (uint8_t i = 0; i < Driver::COUNT; i++)
            {
                if (pause[1 - i] <= pause[i]) continue;
                uint32_t actual, extra;
                if (!timed(i, 0, pause[1 - i] - pause[i], startNow, actual, extra))
                {
                    faultCode = "motion";
                    return;
                }
            }
            if (!started)
            {
                uint32_t none;
                l->moveTimed(0, 0, &none, true);
                r->moveTimed(0, 0, &none, true);
                started = true;
            }
        }
    }

    void task(void *)
    {
        for (;;)
        {
            vTaskDelay(PERIOD);
            if (!running) continue;
            xSemaphoreTake(lock, portMAX_DELAY);
            feed();
            // Back to "not started" once everything has run out, so the next
            // push starts both belts together again.
            if (planner.idle() && !stepper(0)->isRunning() && !stepper(1)->isRunning()) started = false;
            xSemaphoreGive(lock);
        }
    }

    Planner::Limits limits()
    {
        Planner::Limits l;
        l.accel = ACCEL_MM_S2;
        return l;
    }
}

namespace Path
{
    void begin()
    {
        lock = xSemaphoreCreateMutex();
        planner.setLimits(limits());
        // Core 1, with loop() and the step engine; core 0 is the radio's.
        xTaskCreatePinnedToCore(task, "path", TASK_STACK, nullptr, TASK_PRIORITY, nullptr, 1);
    }

    bool start(double x, double y, const Kinematics::Geometry &geometry, double stepsPerMm)
    {
        if (running || Motion::moving() || !Motion::ready()) return false;
        xSemaphoreTake(lock, portMAX_DELAY);
        model.setGeometry(geometry);
        model.resetTilt();
        perMm = stepsPerMm;
        planner.clear();
        planner.setPosition(x, y);
        planX = x;
        planY = y;
        for (uint8_t i = 0; i < Driver::COUNT; i++)
        {
            committed[i] = stepper(i)->getCurrentPosition();
            drift[i] = 0;
        }
        started = false;
        faultCode = nullptr;
        running = true;
        xSemaphoreGive(lock);
        return true;
    }

    void end()
    {
        xSemaphoreTake(lock, portMAX_DELAY);
        running = false;
        xSemaphoreGive(lock);
    }

    bool active()
    {
        return running;
    }

    bool push(double x, double y, double speed)
    {
        xSemaphoreTake(lock, portMAX_DELAY);
        const bool ok = planner.push(x, y, speed);
        xSemaphoreGive(lock);
        return ok;
    }

    void finish()
    {
        xSemaphoreTake(lock, portMAX_DELAY);
        planner.finish();
        xSemaphoreGive(lock);
    }

    size_t stopSoon(Planner::Target *dropped, size_t max)
    {
        xSemaphoreTake(lock, portMAX_DELAY);
        const size_t n = planner.stopSoon(dropped, max);
        xSemaphoreGive(lock);
        return n;
    }

    bool idle()
    {
        xSemaphoreTake(lock, portMAX_DELAY);
        const bool still = planner.idle() && !stepper(0)->isRunning() && !stepper(1)->isRunning();
        xSemaphoreGive(lock);
        return still;
    }

    void abort()
    {
        xSemaphoreTake(lock, portMAX_DELAY);
        planner.clear();
        for (uint8_t i = 0; i < Driver::COUNT; i++)
        {
            stepper(i)->forceStop();
            // forceStop suspends raw commands until resumed (FastAccelStepper 2.0).
            stepper(i)->resumeCommands();
        }
        running = false;
        started = false;
        xSemaphoreGive(lock);
    }

    double x() { return planX; }
    double y() { return planY; }
    uint32_t underruns() { return underrunCount; }
    const char *fault() { return faultCode; }
}
