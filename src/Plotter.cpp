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

    // Drawing: Mural's 1 mm increment, and a "ramp" so steep it is none - a
    // piece is too short for one. The planner replaces both.
    constexpr double DRAW_PIECE_MM = 1.0;
    constexpr double DRAW_ACCEL_MM_S2 = 20000.0;

    // Lines of the job file checked per loop() pass: a few milliseconds of SD
    // reading, so the web server keeps answering during a long check.
    constexpr uint32_t CHECK_LINES_PER_POLL = 200;

    State current = State::Unconfigured;
    Kinematics::Model model;

    // The job's drawing state: the segment being drawn, the pen as the file
    // last set it, and the requests waiting for the current piece to end.
    bool segmentActive = false;
    double segX = 0.0, segY = 0.0;
    bool jobPenDown = false;
    bool pauseRequested = false, stopRequested = false;

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

    /** Pen up, job closed, back to Ready - the position is the last piece's end. */
    void endJob()
    {
        JobRunner::close();
        Pen::up();
        segmentActive = false;
        jobPenDown = false;
        pauseRequested = stopRequested = false;
    }

    void applyGeometry()
    {
        Kinematics::Geometry g;
        g.anchorDistance = settings.getAnchorDistance();
        g.pulleyDiameter = settings.getPulleyDiameter();
        model.setGeometry(g);
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
     * The next millimetre of the segment being drawn: solved, and run at the
     * drawing speed. The belt with the longer way sets the pace, so the pen
     * moves at drawSpeed along the chord. False when the move was refused.
     */
    bool drawPiece()
    {
        const double dx = segX - posX, dy = segY - posY;
        const double left = hypot(dx, dy);
        const double piece = left < DRAW_PIECE_MM ? left : DRAW_PIECE_MM;
        const double nx = left > 0.0 ? posX + dx / left * piece : segX;
        const double ny = left > 0.0 ? posY + dy / left * piece : segY;

        const Kinematics::Pose pose = model.solve(nx, ny);
        if (!pose.converged) return false;

        const double wayL = fabs(pose.left - Motion::millimetres(Driver::LEFT));
        const double wayR = fabs(pose.right - Motion::millimetres(Driver::RIGHT));
        const double longest = wayL > wayR ? wayL : wayR;
        if (piece > 0.0 && longest > 0.0)
        {
            const double beltSpeed = settings.getDrawSpeed() * longest / piece;
            if (!Motion::moveBelts(pose.left, pose.right, beltSpeed, DRAW_ACCEL_MM_S2)) return false;
        }
        JobRunner::moved(piece);
        posX = nx;
        posY = ny;
        if (left <= DRAW_PIECE_MM) segmentActive = false;
        return true;
    }

    /** One step of a running job, called whenever nothing moves. */
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
            if (JobRunner::phase() == Phase::Failed) enter(State::Ready);
            return;
        }

        if (stopRequested)
        {
            endJob();
            enter(State::Ready);
            return;
        }
        if (pauseRequested)
        {
            pauseRequested = false;
            Pen::up();
            enter(State::Paused);
            return;
        }

        if (segmentActive)
        {
            if (!drawPiece())
            {
                debugE("Plotter: no pose for (%.1f, %.1f) - job given up", segX, segY);
                endJob();
                enter(State::Ready);
            }
            return;
        }

        JobFormat::Line line;
        if (!JobRunner::next(line))
        {
            // The end - or a read error, which JobRunner has recorded. Pen up
            // and back home, as Mural does, unless something failed.
            const bool finished = JobRunner::phase() == Phase::Finished;
            endJob();
            enter(State::Ready);
            if (finished) travelTo(model.width() / 2.0, HOME_Y_MM);
            return;
        }

        switch (line.kind)
        {
            case JobFormat::Kind::PenDown:
                jobPenDown = true;
                Pen::down();
                break;
            case JobFormat::Kind::PenUp:
                jobPenDown = false;
                Pen::up();
                break;
            case JobFormat::Kind::Move:
                if (jobPenDown)
                {
                    segX = line.x;
                    segY = line.y;
                    segmentActive = true;
                }
                else
                {
                    // Travel: one ramped move, straightness does not matter.
                    const double way = hypot(line.x - posX, line.y - posY);
                    if (travelTo(line.x, line.y) != nullptr)
                    {
                        debugE("Plotter: cannot travel to (%.1f, %.1f) - job given up", line.x, line.y);
                        endJob();
                        enter(State::Ready);
                        return;
                    }
                    JobRunner::moved(way);
                }
                break;
            default:
                break;
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
        if (Motion::moving()) return;

        if (current == State::ExtendingHome) enter(State::AtHome);
        moveRunning = false;

        // The pen is part of the sequence: nothing moves on until it has
        // arrived (Pen::settled() includes the servo's time to get there).
        if (current == State::Plotting && Pen::settled()) runJob();

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
        if (Motion::moving()) return "busy";

        const char *refused = JobRunner::open(name, posX, posY, model.width(),
                                              MAX_Y_FRACTION * settings.getAnchorDistance());
        if (refused) return refused;

        Driver::enable(true);
        Pen::up();
        jobPenDown = false;
        segmentActive = false;
        pauseRequested = stopRequested = false;
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
        // Back down where the file had it; poll() waits for the pen.
        if (jobPenDown) Pen::down();
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
        const bool wasMoving = Motion::moving();
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
        if (current == State::Plotting || current == State::Paused) endJob();
        pulleyRun = 0.0;
        pendingDiameter = 0.0;
        enter(State::NeedsRetract);
    }
}
