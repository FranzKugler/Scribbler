/**
 * Kinematics
 * Mural's rigid-body belt model; see Kinematics.h for what and why.
 *
 * Kinematic model after nikivanov/mural (CC BY-NC 4.0). The order of the
 * operations below follows Mural's Movement::getBeltLengths() on purpose,
 * including two details that look like slips and are not to be "fixed" without
 * regenerating the reference table:
 *
 *   - the belt forces used for the stretch correction are the ones from the
 *     last solver iteration, computed with the tilt *before* its final update,
 *     while the lengths use the final tilt;
 *   - the torque search walks a fixed 0.2 degree grid and stops at the first
 *     step that is worse than the best so far, so its answer is quantised to
 *     that grid around the warm-start value.
 *
 * @mc       ESP32S3 (and the host, for the tests)
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
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

    // Mural's solver constants. Changing any of these changes the answers, and
    // the reference table with them.
    constexpr double TORQUE_STEP = 0.20 * DEG;      // search grid
    constexpr double TORQUE_MIN = -90.0 * DEG;      // absolute search range
    constexpr double TORQUE_MAX = 90.0 * DEG;
    constexpr double TORQUE_WINDOW = 2.0 * DEG;     // around the warm start
    constexpr int OUTER_ITERATIONS = 20;
    constexpr double OUTER_TERMINATION = 0.25 * DEG; // > TORQUE_STEP, or it never stops early
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

    double Model::solveTorque(double phiL, double phiR, double forceL, double forceR,
                              double gammaInit) const
    {
        const double s = _dt / 2.0;
        const double weight = _geometry.massKg * GRAVITY;

        double gammaBest = 99999999;
        double residualBest = 99999999;

        // A plain scan rather than a root finder: there is one zero crossing
        // in the window, and the scan's answer is what the reference holds.
        for (double gamma = gammaInit - TORQUE_WINDOW;
             gamma > TORQUE_MIN && gamma < TORQUE_MAX && gamma <= gammaInit + TORQUE_WINDOW;
             gamma += TORQUE_STEP)
        {
            const double alpha = phiL - gamma;
            const double beta = phiR + gamma;

            const double torqueL = s * std::sin(alpha) * forceL;
            const double torqueR = s * std::sin(beta) * forceR;
            const double torqueM = _dm * std::tan(gamma) * weight * std::cos(gamma);

            const double residual = torqueR - torqueL + torqueM;
            if (std::fabs(residual) < std::fabs(residualBest))
            {
                residualBest = residual;
                gammaBest = gamma;
            }
            else
            {
                return gammaBest;
            }
        }
        return gammaBest;
    }

    Pose Model::solve(double x, double y)
    {
        const double d = _geometry.anchorDistance;
        const double weight = _geometry.massKg * GRAVITY;

        const double frameX = x + _minSafeX;
        const double frameY = y + _minSafeY;

        double gamma = _lastTilt;
        double forceL = 0.0;
        double forceR = 0.0;
        bool converged = false;

        for (int i = 0; i < OUTER_ITERATIONS; i++)
        {
            double xL, yL, xR, yR;
            tangentPoints(frameX, frameY, gamma, xL, yL, xR, yR);

            // Belt angles against the line through the pins.
            const double phiL = std::atan2(yL, xL);
            const double phiR = std::atan2(yR, d - xR);

            // Forces from the static balance of the weight alone.
            const double sinSum = std::sin(phiL + phiR);
            forceR = weight * std::cos(phiL) / sinSum;
            forceL = weight * std::cos(phiR) / sinSum;

            const double previous = gamma;
            gamma = solveTorque(phiL, phiR, forceL, forceR, gamma);
            if (std::fabs(previous - gamma) < OUTER_TERMINATION)
            {
                converged = true;
                break;
            }
        }
        _lastTilt = gamma;

        double xL, yL, xR, yR;
        tangentPoints(frameX, frameY, gamma, xL, yL, xR, yR);

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
        pose.converged = converged;
        return pose;
    }

    Pose Model::settle(double x, double y)
    {
        // Each solve() can move the tilt by up to 40 degrees, and the tilt
        // never needs more than 180, so five rounds is a bound, not a guess.
        Pose pose = solve(x, y);
        for (int round = 1; round < 5 && !pose.converged; round++) pose = solve(x, y);
        return pose;
    }
}
