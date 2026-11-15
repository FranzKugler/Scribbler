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

    State current = State::Unconfigured;
    Kinematics::Model model;

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
        if (current == State::Ready)
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
    }

    const char *setAnchorDistance(double mm)
    {
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

    void stop()
    {
        Motion::stop();
    }

    void forgetPosition()
    {
        if (current == State::Unconfigured || current == State::NeedsRetract) return;
        debugW("Plotter: a belt moved outside the plotter - position forgotten");
        pulleyRun = 0.0;
        pendingDiameter = 0.0;
        enter(State::NeedsRetract);
    }
}
