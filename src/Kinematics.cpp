/**
 * Kinematics
 * Mural's rigid-body belt model and two solvers for it; see Kinematics.h for
 * what and why.
 *
 * Kinematic model after nikivanov/mural (CC BY-NC 4.0). solveMural() follows
 * Mural's Movement::getBeltLengths() in the order of its operations on
 * purpose, including two details that look like slips and are not to be
 * "fixed" without regenerating the reference table:
 *
 *   - the belt forces used for the stretch correction are the ones from the
 *     last solver iteration, computed with the tilt *before* its final update,
 *     while the lengths use the final tilt;
 *   - the torque search walks a fixed 0.2 degree grid and stops at the first
 *     step that is worse than the best so far.
 *
 * solve() has neither: everything is evaluated at the one tilt that balances.
 *
 * @mc       ESP32S3 (and the host, for the tests)
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.1
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#include "Kinematics.h"

#include <cmath>

namespace
{
    // Arduino's PI, spelled out so the host build gets the same digits.
    constexpr double KIN_PI = 3.1415926535897932384626433832795;
    constexpr double DEG = KIN_PI / 180.0;

    constexpr double GRAVITY = 9.81;

    // ------ Mural's solver constants ------
    // Changing any of these changes solveMural()'s answers, and the reference
    // table with them.
    constexpr double TORQUE_STEP = 0.20 * DEG;      // search grid
    constexpr double TORQUE_MIN = -90.0 * DEG;      // absolute search range
    constexpr double TORQUE_MAX = 90.0 * DEG;
    constexpr double TORQUE_WINDOW = 2.0 * DEG;     // around the warm start
    constexpr int OUTER_ITERATIONS = 20;
    constexpr double OUTER_TERMINATION = 0.25 * DEG; // > TORQUE_STEP, or it never stops early

    // ------ ours ------

    // First bracket around the previous tilt; doubled until the residual
    // changes sign. Small, because along a path the root is rarely further
    // than a fraction of a degree away - two evaluations, and the bracket
    // already holds it.
    constexpr double BRACKET_START = 2.0 * DEG;
    // tan() in the gravity torque runs away at 90 degrees, and no bot hangs
    // anywhere near that.
    constexpr double BRACKET_LIMIT = 85.0 * DEG;
    // 1e-9 rad moves a belt by ~4e-8 mm on this bot: far below a microstep
    // (~0.01 mm), far above double rounding.
    constexpr double TOLERANCE = 1e-9;
    constexpr int MAX_REFINE = 60;
}

namespace Kinematics
{
    void Model::setGeometry(const Geometry &geometry)
    {
        _geometry = geometry;

        // The belt leaves the pulley at 45 degrees on average, so its tangent
        // point sits q = D / (2 * sqrt 2) off the axis in both directions.
        const double q = geometry.pulleyDiameter / (2.0 * std::sqrt(2.0));
        _dt = geometry.pulleyAxisDistance - 2.0 * q;
        _dp = q;
        _dm = geometry.axesToCenterOfMass + q;

        _minSafeY = geometry.safeYFraction * geometry.anchorDistance;
        _minSafeX = geometry.safeXFraction * geometry.anchorDistance;
        _width = geometry.anchorDistance - 2.0 * _minSafeX;
    }

    // ------ the model ------

    void Model::tangentPoints(double frameX, double frameY, double gamma,
                              double &xL, double &yL, double &xR, double &yR) const
    {
        const double s = _dt / 2.0;
        const double c = std::cos(gamma);
        const double n = std::sin(gamma);

        xL = frameX - (s * c - _dp * n);
        yL = frameY - (s * n + _dp * c);
        xR = frameX + (s * c + _dp * n);
        yR = frameY + (s * n - _dp * c);
    }

    void Model::anglesAndForces(double frameX, double frameY, double gamma,
                                double &phiL, double &phiR, double &forceL, double &forceR) const
    {
        double xL, yL, xR, yR;
        tangentPoints(frameX, frameY, gamma, xL, yL, xR, yR);

        // Belt angles against the line through the pins.
        phiL = std::atan2(yL, xL);
        phiR = std::atan2(yR, _geometry.anchorDistance - xR);

        // Forces from the static balance of the weight alone.
        const double weight = _geometry.massKg * GRAVITY;
        const double sinSum = std::sin(phiL + phiR);
        forceR = weight * std::cos(phiL) / sinSum;
        forceL = weight * std::cos(phiR) / sinSum;
    }

    /** Net torque about the tangent line's midpoint [N mm]; zero at balance. */
    double Model::torque(double phiL, double phiR, double forceL, double forceR,
                         double gamma) const
    {
        const double s = _dt / 2.0;
        const double weight = _geometry.massKg * GRAVITY;

        const double torqueL = s * std::sin(phiL - gamma) * forceL;
        const double torqueR = s * std::sin(phiR + gamma) * forceR;
        // Grouped as Mural groups it (lever s_m times force F_m): the grid
        // search compares residuals, and a different rounding can tip it into
        // the neighbouring grid step on a tie.
        const double torqueM = (_dm * std::tan(gamma)) * (weight * std::cos(gamma));
        return torqueR - torqueL + torqueM;
    }

    double Model::residual(double frameX, double frameY, double gamma) const
    {
        double phiL, phiR, forceL, forceR;
        anglesAndForces(frameX, frameY, gamma, phiL, phiR, forceL, forceR);
        return torque(phiL, phiR, forceL, forceR, gamma);
    }

    Pose Model::lengths(double frameX, double frameY, double gamma,
                        double forceL, double forceR) const
    {
        double xL, yL, xR, yR;
        tangentPoints(frameX, frameY, gamma, xL, yL, xR, yR);

        const double d = _geometry.anchorDistance;
        const double flatL = std::sqrt(xL * xL + yL * yL);
        const double flatR = std::sqrt((d - xR) * (d - xR) + yR * yR);

        const double wall = _geometry.midPulleyToWall;
        const double legL = std::sqrt(flatL * flatL + wall * wall);
        const double legR = std::sqrt(flatR * flatR + wall * wall);

        // The belt is shorter than the geometry by its stretch under load, so
        // that, loaded, it comes out as long as the geometry wants.
        Pose pose;
        pose.left = legL / (1.0 + _geometry.beltElongationPerN * forceL);
        pose.right = legR / (1.0 + _geometry.beltElongationPerN * forceR);
        pose.tilt = gamma;
        pose.forceLeft = forceL;
        pose.forceRight = forceR;
        return pose;
    }

    // ------ ours: bracket and refine ------

    Pose Model::solve(double x, double y)
    {
        const double frameX = x + _minSafeX;
        const double frameY = y + _minSafeY;
        const double seed = _lastTilt;

        // Bracket the root, widening around the previous tilt.
        double a = 0.0, b = 0.0, fa = 0.0, fb = 0.0;
        bool bracketed = false;
        for (double half = BRACKET_START; ; half *= 2.0)
        {
            a = std::fmax(seed - half, -BRACKET_LIMIT);
            b = std::fmin(seed + half, BRACKET_LIMIT);
            fa = residual(frameX, frameY, a);
            fb = residual(frameX, frameY, b);
            if (fa * fb <= 0.0)
            {
                bracketed = true;
                break;
            }
            if (a <= -BRACKET_LIMIT && b >= BRACKET_LIMIT) break;
        }

        Pose pose;
        if (!bracketed)
        {
            // No balance anywhere: the point is not one the bot can hang at.
            // Answer with the previous tilt and say so; the caller refuses.
            double phiL, phiR, forceL, forceR;
            anglesAndForces(frameX, frameY, seed, phiL, phiR, forceL, forceR);
            pose = lengths(frameX, frameY, seed, forceL, forceR);
            pose.converged = false;
            return pose;
        }

        // Illinois: regula falsi that halves the weight of an end point that
        // has stayed put twice, so the interval shrinks from both sides
        // instead of crawling in from one.
        double r = fa == 0.0 ? a : b;
        int side = 0;
        for (int i = 0; i < MAX_REFINE && fa != 0.0 && fb != 0.0; i++)
        {
            r = (fa * b - fb * a) / (fa - fb);
            const double fr = residual(frameX, frameY, r);
            if (fr * fb > 0.0)
            {
                b = r;
                fb = fr;
                if (side == -1) fa /= 2.0;
                side = -1;
            }
            else if (fa * fr > 0.0)
            {
                a = r;
                fa = fr;
                if (side == 1) fb /= 2.0;
                side = 1;
            }
            else
            {
                break;   // fr == 0: on the root
            }
            if (std::fabs(b - a) < TOLERANCE) break;
        }

        _lastTilt = r;
        double phiL, phiR, forceL, forceR;
        anglesAndForces(frameX, frameY, r, phiL, phiR, forceL, forceR);
        pose = lengths(frameX, frameY, r, forceL, forceR);
        pose.converged = true;
        return pose;
    }

    // ------ Mural's: grid search inside a fixed-point loop ------

    double Model::gridSearch(double phiL, double phiR, double forceL, double forceR,
                             double gammaInit) const
    {
        double gammaBest = 99999999;
        double residualBest = 99999999;

        for (double gamma = gammaInit - TORQUE_WINDOW;
             gamma > TORQUE_MIN && gamma < TORQUE_MAX && gamma <= gammaInit + TORQUE_WINDOW;
             gamma += TORQUE_STEP)
        {
            const double t = torque(phiL, phiR, forceL, forceR, gamma);
            if (std::fabs(t) < std::fabs(residualBest))
            {
                residualBest = t;
                gammaBest = gamma;
            }
            else
            {
                return gammaBest;
            }
        }
        return gammaBest;
    }

    Pose Model::solveMural(double x, double y)
    {
        const double frameX = x + _minSafeX;
        const double frameY = y + _minSafeY;

        double gamma = _lastTilt;
        double forceL = 0.0;
        double forceR = 0.0;
        bool converged = false;

        for (int i = 0; i < OUTER_ITERATIONS; i++)
        {
            double phiL, phiR;
            anglesAndForces(frameX, frameY, gamma, phiL, phiR, forceL, forceR);

            const double previous = gamma;
            gamma = gridSearch(phiL, phiR, forceL, forceR, gamma);
            if (std::fabs(previous - gamma) < OUTER_TERMINATION)
            {
                converged = true;
                break;
            }
        }
        _lastTilt = gamma;

        // Forces from the last iteration, lengths at the final tilt - as Mural.
        Pose pose = lengths(frameX, frameY, gamma, forceL, forceR);
        pose.converged = converged;
        return pose;
    }
}
