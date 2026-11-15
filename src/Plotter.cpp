/**
 * Plotter
 * See Plotter.h.
 *
 * Refusal codes (each needs an err_<code> in both locales):
 *   notConfigured  no anchor distance yet
 *   wrongState     not an action of this state
 *   busy           a belt is moving
 *   badValue       out of range (the routes name the field)
 *   outsideArea    the target lies outside the drawing area
 *   unreachable    Kinematics found no pose there
 *   noPulleyRun    a measurement without a preceding pulley extension
 *   motion         the step generator refused the move
 *   (and JobRunner's codes for a job: jobName, noCard, jobNotFound, ...)
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#include "Plotter.h"

#include "Settings.h"
#include "LogBuffer.h"
#include "Motion.h"
#include "Pen.h"
#include "Imu.h"
#include "Kinematics.h"
#include "JobRunner.h"
#include "Path.h"

extern Settings settings;
void scheduleSettingsSave();

namespace
{
    using Plotter::State;

    // Belt length with its end wound onto the stop, from the stop to the
    // belt's tangent point on the pulley [mm]. Mural's homedStepOffsetMM, a
    // property of its printed bot; ours changes with the bot's design.
    constexpr double RETRACTED_MM = 40.0;

    // Home: centred, this far below the top of the drawing area. Mural's
    // HOME_Y_OFFSET_MM.
    constexpr double HOME_Y_MM = 350.0;

    // How far below the top of the area the pen may go, in anchor distances.
    // Mural only forbids y < 0; a bottom edge keeps a typo from paying out
    // metres of belt.
    constexpr double MAX_Y_FRACTION = 1.0;

    // Travel between known points (home, jogs): Mural's 2000 steps/s at 1/8
    // is ~40 mm/s of belt, but with no ramp; ours ramps.
    constexpr double TRAVEL_MM_S = 40.0;
    constexpr double TRAVEL_MM_S2 = 200.0;

    // The pulley run: slow, so the belt comes off evenly and a tape measure
    // can follow it.
    constexpr double PULLEY_MM_S = 20.0;
    constexpr double PULLEY_MM_S2 = 100.0;

    // Commands read from the job per loop() pass: enough to keep the
    // planner's buffer full at any drawing speed, few enough that the web
    // server is not kept waiting.
    constexpr int LINES_PER_POLL = 32;

    // Lines of the job file checked per loop() pass: a few milliseconds of SD
    // reading, so the web server keeps answering during a long check.
    constexpr uint32_t CHECK_LINES_PER_POLL = 200;

    State current = State::Unconfigured;
    Kinematics::Model model;

    // The job's drawing state. Moves go to Path; anything that needs
    // standstill - a pen change, a pause, the end - first lets Path run dry
    // ("draining") and then does `after`.
    enum class After : uint8_t { Nothing, PenDown, PenUp, Pause, Stop, Finish };
    After after = After::Nothing;
    bool draining = false;
    bool waitingPen = false;
    bool jobPenDown = false;
    bool pauseRequested = false, stopRequested = false;
    // A move read from the file that did not fit into the planner yet.
    bool havePending = false;
    double pendX = 0.0, pendY = 0.0, pendSpeed = 0.0;
    // Where the last pushed move ends - for the progress count.
    double pushX = 0.0, pushY = 0.0;

    // The pen's image coordinates once Ready; where a move is going while
    // one runs (the position is the target's once it has stopped).
    double posX = 0.0, posY = 0.0;
    bool moveRunning = false;

    // Pulley calibration: how far the left belt was paid out, 0 when no run
    // is waiting for its measurement.
    double pulleyRun = 0.0;
    // The corrected diameter, waiting for the belt to be back at the stop:
    // the way back runs on the old scale, so the new one may only take over
    // once it has ended. 0 when nothing waits.
    double pendingDiameter = 0.0;

    double homeX() { return model.width() / 2.0; }

    void enter(State next)
    {
        if (next == current) return;
        debugI("Plotter: %s -> %s", Plotter::stateName(current), Plotter::stateName(next));
        current = next;
    }

    /** Pen up, job closed, the path handed back. The caller sets the state. */
    void endJob()
    {
        JobRunner::close();
        if (Path::active()) Path::end();
        Pen::up();
        jobPenDown = false;
        pauseRequested = stopRequested = false;
        draining = waitingPen = havePending = false;
        after = After::Nothing;
    }

    Kinematics::Geometry geometry()
    {
        Kinematics::Geometry g;
        g.anchorDistance = settings.getAnchorDistance();
        g.pulleyDiameter = settings.getPulleyDiameter();
        return g;
    }

    /** Hands the steppers to Path from the current pen position. */
    bool startPath()
    {
        Driver::enable(true);
        pushX = posX;
        pushY = posY;
        return Path::start(posX, posY, geometry(), Motion::stepsPerMm());
    }

    void drainThen(After next)
    {
        Path::finish();
        draining = true;
        after = next;
    }

    void applyGeometry()
    {
        model.setGeometry(geometry());
    }

    /** Solves (x, y) and starts both belts there; nullptr or a refusal code. */
    const char *travelTo(double x, double y)
    {
        if (x < 0.0 || x > model.width() || y < 0.0 ||
            y > MAX_Y_FRACTION * settings.getAnchorDistance())
            return "outsideArea";

        const Kinematics::Pose pose = model.solve(x, y);
        if (!pose.converged) return "unreachable";

        Driver::enable(true);
        if (!Motion::moveBelts(pose.left, pose.right, TRAVEL_MM_S, TRAVEL_MM_S2)) return "motion";
        posX = x;
        posY = y;
        moveRunning = true;
        return nullptr;
    }

    /**
     * One pass of a running job, every loop(): check the file, keep the
     * planner fed, and do what needs standstill once the path has run dry.
     */
    void runJob()
    {
        using JobRunner::Phase;

        if (JobRunner::phase() == Phase::Checking)
        {
            if (stopRequested)
            {
                endJob();
                enter(State::Ready);
                return;
            }
            JobRunner::check(CHECK_LINES_PER_POLL);
            if (JobRunner::phase() == Phase::Failed)
            {
                endJob();
                enter(State::Ready);
            }
            else if (JobRunner::phase() == Phase::Running && !startPath())
            {
                debugE("Plotter: the steppers are busy - job not started");
                endJob();
                enter(State::Ready);
            }
            return;
        }

        if (const char *fault = Path::fault())
        {
            // The path stopped somewhere along a move: the position is gone.
            debugE("Plotter: path fault (%s) - job given up", fault);
            Path::abort();
            endJob();
            Plotter::forgetPosition();
            return;
        }

        if (waitingPen)
        {
            if (!Pen::settled()) return;
            waitingPen = false;
        }

        if (draining)
        {
            if (!Path::idle()) return;
            draining = false;
            posX = Path::x();
            posY = Path::y();
            const After what = after;
            after = After::Nothing;
            switch (what)
            {
                case After::PenDown:
                    Pen::down();
                    jobPenDown = true;
                    waitingPen = true;
                    break;
                case After::PenUp:
                    Pen::up();
                    jobPenDown = false;
                    waitingPen = true;
                    break;
                case After::Pause:
                    Path::end();
                    Pen::up();
                    enter(State::Paused);
                    break;
                case After::Stop:
                    endJob();
                    enter(State::Ready);
                    break;
                case After::Finish:
                {
                    // The end - or a read error JobRunner has recorded. Back
                    // home as Mural does, unless something failed.
                    const bool finished = JobRunner::phase() == Phase::Finished;
                    endJob();
                    enter(State::Ready);
                    if (finished) travelTo(model.width() / 2.0, HOME_Y_MM);
                    break;
                }
                case After::Nothing:
                    break;
            }
            return;
        }

        if (stopRequested)
        {
            stopRequested = false;
            drainThen(After::Stop);
            return;
        }
        if (pauseRequested)
        {
            pauseRequested = false;
            drainThen(After::Pause);
            return;
        }

        for (int n = 0; n < LINES_PER_POLL; n++)
        {
            if (havePending)
            {
                if (!Path::push(pendX, pendY, pendSpeed)) return;   // full: next pass
                JobRunner::moved(hypot(pendX - pushX, pendY - pushY));
                pushX = pendX;
                pushY = pendY;
                havePending = false;
            }

            JobFormat::Line line;
            if (!JobRunner::next(line))
            {
                drainThen(After::Finish);
                return;
            }
            switch (line.kind)
            {
                case JobFormat::Kind::PenDown:
                    if (!jobPenDown)
                    {
                        drainThen(After::PenDown);
                        return;
                    }
                    break;
                case JobFormat::Kind::PenUp:
                    if (jobPenDown)
                    {
                        drainThen(After::PenUp);
                        return;
                    }
                    break;
                case JobFormat::Kind::Move:
                    // Drawn moves at the drawing speed, pen-up moves at travel
                    // speed - both straight now, the planner does not care.
                    pendX = line.x;
                    pendY = line.y;
                    pendSpeed = jobPenDown ? settings.getDrawSpeed() : TRAVEL_MM_S;
                    havePending = true;
                    break;
                default:
                    break;
            }
        }
    }
}

namespace Plotter
{
    void begin()
    {
        applyGeometry();
        current = settings.getAnchorDistance() > 0.0 ? State::NeedsRetract : State::Unconfigured;
        debugI("Plotter: %s (d = %.1f mm)", stateName(current), settings.getAnchorDistance());
    }

    void poll()
    {
        // A job feeds the path while it moves, so it runs before the
        // "nothing moves" gate below.
        if (current == State::Plotting)
        {
            runJob();
            return;
        }

        if (Motion::moving()) return;

        if (current == State::ExtendingHome) enter(State::AtHome);
        moveRunning = false;

        if (pendingDiameter > 0.0)
        {
            // Rescaling keeps the millimetres: the belt is at the stop, and
            // stays RETRACTED_MM long on the new scale too.
            settings.setPulleyDiameter(pendingDiameter);
            scheduleSettingsSave();
            Motion::setScale(settings.getMicrosteps(), pendingDiameter);
            applyGeometry();
            pendingDiameter = 0.0;
        }
    }

    void settingsChanged()
    {
        applyGeometry();
    }

    State state()
    {
        return current;
    }

    const char *stateName(State s)
    {
        switch (s)
        {
            case State::Unconfigured:  return "unconfigured";
            case State::NeedsRetract:  return "needsRetract";
            case State::Retracted:     return "retracted";
            case State::ExtendingHome: return "extendingHome";
            case State::AtHome:        return "atHome";
            case State::Ready:         return "ready";
            case State::Plotting:      return "plotting";
            case State::Paused:        return "paused";
        }
        return "unknown";
    }

    void publish(JsonObject out)
    {
        out["state"] = stateName(current);
        out["moving"] = Motion::moving();
        out["anchorDistance"] = settings.getAnchorDistance();
        out["pulleyDiameter"] = settings.getPulleyDiameter();
        out["pulleyRun"] = pulleyRun;
        out["microsteps"] = settings.getMicrosteps();
        if (current == State::Unconfigured) return;

        out["width"] = model.width();
        out["maxY"] = MAX_Y_FRACTION * settings.getAnchorDistance();
        out["home"]["x"] = homeX();
        out["home"]["y"] = HOME_Y_MM;

        // Belt lengths mean something from the retract stop on.
        if (current != State::NeedsRetract)
        {
            out["belts"]["left"] = Motion::millimetres(Driver::LEFT);
            out["belts"]["right"] = Motion::millimetres(Driver::RIGHT);
        }
        if (current == State::Ready || current == State::Plotting || current == State::Paused)
        {
            // Where the pen is - or, while a move runs, where it is going.
            out["position"]["x"] = posX;
            out["position"]["y"] = posY;
            out["position"]["target"] = moveRunning;
            out["tiltModel"] = model.lastTilt() * 180.0 / PI;
        }
        const Imu::Reading r = Imu::reading();
        if (r.present && !isnan(r.tilt)) out["tiltImu"] = r.tilt * 180.0 / PI;
        out["penDown"] = Pen::isDown();
        out["drawSpeed"] = settings.getDrawSpeed();
        // Slices that found a step queue already empty since boot - the motor
        // stopped between two of them. Should stay 0; the first number to
        // look at if a plot judders.
        out["underruns"] = Path::underruns();

        // The job: the running one, or the record of the last one (finished,
        // failed or stopped), so the UI can say how it ended.
        if (JobRunner::name()[0])
        {
            JsonObject job = out["job"].to<JsonObject>();
            job["name"] = JobRunner::name();
            job["phase"] = JobRunner::phaseName();
            job["total"] = JobRunner::total();
            job["done"] = JobRunner::done();
            if (JobRunner::error())
            {
                job["error"] = JobRunner::error();
                if (JobRunner::errorLine()) job["line"] = JobRunner::errorLine();
            }
        }
    }

    const char *setAnchorDistance(double mm)
    {
        if (current == State::Plotting || current == State::Paused) return "wrongState";
        if (Motion::moving()) return "busy";
        if (!(mm >= 300.0 && mm <= 5000.0)) return "badValue";

        settings.setAnchorDistance(mm);
        scheduleSettingsSave();
        applyGeometry();
        pulleyRun = 0.0;
        // Every length computed so far belongs to the old wall.
        enter(State::NeedsRetract);
        return nullptr;
    }

    const char *jogBelt(Driver::Side side, double mm)
    {
        if (current == State::Unconfigured) return "notConfigured";
        if (current != State::NeedsRetract && current != State::Retracted) return "wrongState";
        if (Motion::moving()) return "busy";
        if (!(mm >= -1000.0 && mm <= 1000.0)) return "badValue";

        Driver::enable(true);
        if (!Motion::jog(side, mm)) return "motion";
        // Moving a belt off the stop loses what Retracted knew.
        enter(State::NeedsRetract);
        return nullptr;
    }

    const char *confirmRetracted()
    {
        if (current == State::Unconfigured) return "notConfigured";
        if (current != State::NeedsRetract && current != State::Retracted) return "wrongState";
        if (Motion::moving()) return "busy";

        if (!Motion::setLengths(RETRACTED_MM, RETRACTED_MM)) return "motion";
        pulleyRun = 0.0;
        enter(State::Retracted);
        return nullptr;
    }

    const char *extendToHome()
    {
        if (current != State::Retracted) return "wrongState";
        if (Motion::moving()) return "busy";

        // The tilt at home is the one the bot will settle into once hung; the
        // solver does not need the route, so no warm start to worry about.
        const Kinematics::Pose pose = model.solve(homeX(), HOME_Y_MM);
        if (!pose.converged) return "unreachable";

        Driver::enable(true);
        if (!Motion::moveBelts(pose.left, pose.right, TRAVEL_MM_S, TRAVEL_MM_S2)) return "motion";
        posX = homeX();
        posY = HOME_Y_MM;
        enter(State::ExtendingHome);
        return nullptr;
    }

    const char *confirmHung()
    {
        if (current != State::AtHome) return "wrongState";
        if (Motion::moving()) return "busy";
        enter(State::Ready);
        return nullptr;
    }

    const char *jogXY(double dx, double dy)
    {
        if (current != State::Ready) return "wrongState";
        if (Motion::moving()) return "busy";
        if (!(fabs(dx) <= 1000.0 && fabs(dy) <= 1000.0)) return "badValue";
        return travelTo(posX + dx, posY + dy);
    }

    const char *goHome()
    {
        if (current != State::Ready) return "wrongState";
        if (Motion::moving()) return "busy";
        return travelTo(homeX(), HOME_Y_MM);
    }

    const char *pulleyExtend(double mm)
    {
        if (current != State::Retracted) return "wrongState";
        if (Motion::moving() || pendingDiameter > 0.0) return "busy";
        if (!(mm >= 100.0 && mm <= 2000.0)) return "badValue";

        Driver::enable(true);
        if (!Motion::moveBelts(RETRACTED_MM + mm, RETRACTED_MM, PULLEY_MM_S, PULLEY_MM_S2))
            return "motion";
        pulleyRun = mm;
        return nullptr;
    }

    const char *pulleyMeasured(double mm)
    {
        if (current != State::Retracted) return "wrongState";
        if (pulleyRun <= 0.0) return "noPulleyRun";
        if (Motion::moving()) return "busy";
        // More than 20 % off is a misread tape, not a pulley.
        if (!(mm >= pulleyRun * 0.8 && mm <= pulleyRun * 1.2)) return "badValue";

        // The belt paid out `measured` while the steps said `pulleyRun`: a
        // turn carries measured/pulleyRun times more belt than assumed, and
        // the effective diameter scales the same way.
        const double diameter = settings.getPulleyDiameter() * mm / pulleyRun;

        // Wind back first, on the old scale: the steps that went out come
        // back, and the belt is at the stop again whatever the diameter is.
        if (!Motion::moveBelts(RETRACTED_MM, RETRACTED_MM, PULLEY_MM_S, PULLEY_MM_S2))
            return "motion";

        debugI("Plotter: pulley %.4f -> %.4f mm (%.1f mm measured for %.1f)",
               settings.getPulleyDiameter(), diameter, mm, pulleyRun);
        pendingDiameter = diameter;   // taken over in poll(), at the stop
        pulleyRun = 0.0;
        return nullptr;
    }

    const char *startJob(const char *name)
    {
        if (current != State::Ready) return "wrongState";
        if (Motion::moving() || Path::active()) return "busy";

        const char *refused = JobRunner::open(name, posX, posY, model.width(),
                                              MAX_Y_FRACTION * settings.getAnchorDistance());
        if (refused) return refused;

        Driver::enable(true);
        Pen::up();
        jobPenDown = false;
        pauseRequested = stopRequested = false;
        draining = waitingPen = havePending = false;
        after = After::Nothing;
        // The path is taken over once the check has passed - see runJob().
        enter(State::Plotting);
        return nullptr;
    }

    const char *pauseJob()
    {
        if (current != State::Plotting) return "wrongState";
        pauseRequested = true;
        return nullptr;
    }

    const char *resumeJob()
    {
        if (current != State::Paused) return "wrongState";
        if (!startPath()) return "busy";
        // Back down where the file had it; runJob() waits for the pen.
        if (jobPenDown)
        {
            Pen::down();
            waitingPen = true;
        }
        enter(State::Plotting);
        return nullptr;
    }

    const char *stopJob()
    {
        if (current == State::Paused)
        {
            endJob();
            enter(State::Ready);
            return nullptr;
        }
        if (current != State::Plotting) return "wrongState";
        stopRequested = true;
        return nullptr;
    }

    const char *setDrawSpeed(double mmPerS)
    {
        if (!(mmPerS >= 1.0 && mmPerS <= 100.0)) return "badValue";
        settings.setDrawSpeed(mmPerS);
        scheduleSettingsSave();
        return nullptr;
    }

    void stop()
    {
        const bool wasMoving = Motion::moving() || Path::active();
        if (Path::active()) Path::abort();
        Motion::stop();
        if (current == State::Plotting || current == State::Paused) endJob();
        // A move cut short ends somewhere along its way, not at its target.
        if (wasMoving) forgetPosition();
        else if (current == State::Plotting || current == State::Paused) enter(State::Ready);
    }

    void forgetPosition()
    {
        if (current == State::Unconfigured || current == State::NeedsRetract) return;
        debugW("Plotter: position no longer known - back to winding in");
        if (Path::active()) Path::abort();
        if (current == State::Plotting || current == State::Paused) endJob();
        pulleyRun = 0.0;
        pendingDiameter = 0.0;
        enter(State::NeedsRetract);
    }
}
