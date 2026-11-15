/**
 * Kinematics
 * Where the pen is, in belt lengths: image coordinates in millimetres in, the
 * length of each belt in millimetres out, with the bot's tilt solved on the way.
 *
 * The model is Mural's (nikivanov/mural, src/movement.cpp and KinematicModel.md,
 * CC BY-NC 4.0), re-implemented here, not copied: the bot hangs from two belts
 * as a rigid body and tilts by gamma until the torques of the two belt forces
 * and of its own weight cancel. Each leg then gets the out-of-plane offset to
 * the wall and a correction for the belt stretching under its load.
 *
 * Two solvers for the same model:
 *
 *   solve()       ours, and the one the firmware uses. Finds the tilt at
 *                 which the full torque balance is zero - angles, forces and
 *                 torques all evaluated at the same gamma - by bracketing the
 *                 root and refining it (Illinois). The answer is a property of
 *                 the point, not of the route to it: the same point always
 *                 gives the same lengths, and a jump across the area converges
 *                 like a small step does.
 *
 *   solveMural()  Mural's own solver, constant for constant: a 0.2 degree
 *                 grid search around the previous tilt inside an outer
 *                 fixed-point loop. Kept so that the model transcription stays
 *                 checkable against Mural's numbers (test/test_kinematics and
 *                 scripts/kinematics_reference.py) - not for use.
 *
 * Why ours: Mural's grid is anchored at the warm start, so solving the same
 * point twice can alternate between two grid values (~0.07 mm of belt), and a
 * jump of more than ~40 degrees of tilt does not converge at all. Mural never
 * notices because it chops every move into 1 mm pieces; a planner slicing time
 * into 5-10 ms would turn the first into step chatter at standstill. The two
 * agree to within half a grid step (~0.1 degree, ~0.07 mm) wherever Mural's
 * converges - the tests hold them to that.
 *
 * Coordinates: the frame has its origin at the left anchor, +x towards the
 * right anchor, +y down. The image (what a job addresses) starts
 * safeXFraction * d right of the left anchor and safeYFraction * d below the
 * anchor line, and is width() wide.
 *
 * What it is not: aware of steps, motors, time or Arduino. It builds on the
 * host. Converting millimetres to steps is Motion's job - Mural truncates
 * there, which is one of the things not carried over.
 *
 * Cost: solve() evaluates the torque balance ~11 times per point, each a dozen
 * double-precision transcendentals - and the ESP32-S3's FPU is single
 * precision, so those run in software. Roughly half a millisecond per point;
 * measure it on the device before the planner's slice gets shorter than 5 ms.
 *
 * @mc       ESP32S3 (and the host, for the tests)
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.1
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#ifndef KINEMATICS_H
#define KINEMATICS_H

namespace Kinematics
{
    /**
     * The bot and the wall. Defaults are Mural's, for its printed bot with GT2
     * 16T pulleys; anchorDistance has none, because it is measured on the wall.
     */
    struct Geometry
    {
        double anchorDistance = 0.0;        // d: centre of left pin to centre of right pin [mm]
        double pulleyDiameter = 10.14926;   // effective, belt included; calibrated [mm]
        double pulleyAxisDistance = 89.50;  // between the two pulley axes on the bot [mm]
        double midPulleyToWall = 41.0;      // belt plane at the pulley to the wall plane [mm]
        double axesToCenterOfMass = 10.0;   // centre of mass below the line of the axes [mm]
        double massKg = 0.55;
        double beltElongationPerN = 5e-5;   // relative stretch per newton of belt force
        double safeXFraction = 0.2;         // image starts this * d right of the left pin
        double safeYFraction = 0.25;        // ... and this * d below the pins
    };

    /** One solved pose. */
    struct Pose
    {
        double left = 0.0;        // left belt length, stretch-corrected [mm]
        double right = 0.0;       // right belt length, stretch-corrected [mm]
        double tilt = 0.0;        // gamma [rad]; > 0 means the bot leans right
        double forceLeft = 0.0;   // belt forces the stretch correction used [N]
        double forceRight = 0.0;
        // solve(): the root was found to tolerance - false only for a point
        // the bot cannot hang at (outside the area between the anchors).
        // solveMural(): the last update was below Mural's termination step;
        // false after a jump, where Mural's answer is wrong.
        bool converged = false;
    };

    class Model
    {
    public:
        Model() = default;
        explicit Model(const Geometry &geometry) { setGeometry(geometry); }

        /** Recomputes everything derived from the geometry. Keeps the tilt. */
        void setGeometry(const Geometry &geometry);
        const Geometry &geometry() const { return _geometry; }

        /** The drawable width: d minus a safe margin on either side [mm]. */
        double width() const { return _width; }

        /** Where the image origin lies in the frame [mm]. */
        double originX() const { return _minSafeX; }
        double originY() const { return _minSafeY; }

        /**
         * Belt lengths for the pen at image coordinates (x, y) [mm]. The
         * previous tilt only seeds the bracket; it does not change the answer.
         */
        Pose solve(double x, double y);

        /** Mural's solver - for the reference test only; see the header. */
        Pose solveMural(double x, double y);

        /** Forgets where the bot was; the next solve starts from `tilt`. */
        void resetTilt(double tilt = 0.0) { _lastTilt = tilt; }
        double lastTilt() const { return _lastTilt; }

    private:
        void tangentPoints(double frameX, double frameY, double gamma,
                           double &xL, double &yL, double &xR, double &yR) const;
        void anglesAndForces(double frameX, double frameY, double gamma,
                             double &phiL, double &phiR, double &forceL, double &forceR) const;
        double torque(double phiL, double phiR, double forceL, double forceR, double gamma) const;
        double residual(double frameX, double frameY, double gamma) const;
        double gridSearch(double phiL, double phiR, double forceL, double forceR,
                          double gammaInit) const;
        Pose lengths(double frameX, double frameY, double gamma,
                     double forceL, double forceR) const;

        Geometry _geometry;

        // Derived, after Mural's updatePulleyGeometry() and setTopDistance().
        double _dt = 0.0;         // distance between the two belt tangent points [mm]
        double _dp = 0.0;         // tangent line to the pulley axes [mm]
        double _dm = 0.0;         // tangent line to the centre of mass [mm]
        double _minSafeX = 0.0;
        double _minSafeY = 0.0;
        double _width = 0.0;

        double _lastTilt = 0.0;
    };
}

#endif
