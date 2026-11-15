"""
kinematics_reference
Generates test/test_kinematics/reference.h: belt lengths for a fixed path,
computed by a line-by-line Python port of Mural's Movement::getBeltLengths()
(nikivanov/mural, src/movement.cpp, CC BY-NC 4.0).

Ported from Mural's C++, not from src/Kinematics.cpp - the point of the table
is that two independent transcriptions of the same model agree. If they do not,
one of them is wrong, and this one is the shorter to check against the original.

Mural's code stops at integer steps; the port stops one line earlier, at the
stretch-corrected leg length in millimetres, because Scribbler converts to
steps elsewhere and does not truncate.

The solver is warm-started from the previous point, so the table is a *path*,
not a set: the test has to solve the points in the order given, and resets the
tilt to zero at the start of each geometry, as Mural's constructor does.

Regenerate only on purpose - after a deliberate change to the model - and say
why in the commit:

    python scripts/kinematics_reference.py

CI runs it with --check, which recomputes the table and compares it with the
committed one numerically. Not byte for byte: math.sin and math.atan2 come from
the platform's libm, and Windows and Linux may disagree in the last bit, which
%.17g faithfully prints.

@author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
@version  1.0
@created  15.11.2026
@updated  15.11.2026
"""

import math
import os
import re
import sys

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(PROJECT_DIR, "test", "test_kinematics", "reference.h")

PI = 3.1415926535897932384626433832795   # Arduino's PI


class Movement:
    """The subset of Mural's Movement class that getBeltLengths() touches."""

    # movement.h
    pulleyAxisDistance = 89.50
    midPulleyToWall = 41.0
    axesToCenterOfMass = 10.0
    mass_bot = 0.55
    g_constant = 9.81
    belt_elongation_coefficient = 5e-5
    safeYFraction = 0.25
    safeXFraction = 0.2

    def __init__(self, top_distance, diameter):
        self.gamma_last_position = 0.0
        self.diameter = diameter
        self.updatePulleyGeometry()
        self.setTopDistance(top_distance)

    def updatePulleyGeometry(self):
        q = self.diameter / (2.0 * math.sqrt(2.0))
        self.d_t = self.pulleyAxisDistance - 2.0 * q
        self.d_p = q
        self.d_m = self.axesToCenterOfMass + q

    def setTopDistance(self, d):
        self.topDistance = d
        self.minSafeY = self.safeYFraction * d
        self.minSafeXOffset = self.safeXFraction * d
        self.width = d - 2 * self.minSafeXOffset

    def getLeftTangentPoint(self, frameX, frameY, gamma):
        s_L = self.d_t / 2.0
        P_LX = s_L * math.cos(gamma) - self.d_p * math.sin(gamma)
        P_LY = s_L * math.sin(gamma) + self.d_p * math.cos(gamma)
        return frameX - P_LX, frameY - P_LY

    def getRightTangentPoint(self, frameX, frameY, gamma):
        s_R = self.d_t / 2.0
        P_RX = s_R * math.cos(gamma) + self.d_p * math.sin(gamma)
        P_RY = s_R * math.sin(gamma) - self.d_p * math.cos(gamma)
        return frameX + P_RX, frameY + P_RY

    def getBeltAngles(self, frameX, frameY, gamma):
        x_PL, y_PL = self.getLeftTangentPoint(frameX, frameY, gamma)
        phi_L = math.atan2(y_PL, x_PL)
        x_PR, y_PR = self.getRightTangentPoint(frameX, frameY, gamma)
        phi_R = math.atan2(y_PR, self.topDistance - x_PR)
        return phi_L, phi_R

    def getBeltForces(self, phi_L, phi_R):
        F_G = self.mass_bot * self.g_constant
        F_R = F_G * math.cos(phi_L) / math.sin(phi_L + phi_R)
        F_L = F_G * math.cos(phi_R) / math.sin(phi_L + phi_R)
        return F_L, F_R

    def solveTorqueEquilibrium(self, phi_L, phi_R, F_L, F_R, gamma_init):
        s_L = self.d_t / 2.0
        s_R = self.d_t / 2.0
        gamma_best = 99999999
        T_delta_best = 99999999
        gamma_step = 0.20 * PI / 180.0
        gamma_min = -90.0 * PI / 180.0
        gamma_max = 90.0 * PI / 180.0
        gamma_search_window = 2.0 * PI / 180.0

        gamma = gamma_init - gamma_search_window
        while gamma > gamma_min and gamma < gamma_max and gamma <= gamma_init + gamma_search_window:
            alpha = phi_L - gamma
            beta = phi_R + gamma
            T_L = s_L * math.sin(alpha) * F_L
            T_R = s_R * math.sin(beta) * F_R
            s_m = self.d_m * math.tan(gamma)
            F_G = self.mass_bot * self.g_constant
            F_m = F_G * math.cos(gamma)
            T_m = s_m * F_m
            T_delta = T_R - T_L + T_m
            if abs(T_delta) < abs(T_delta_best):
                T_delta_best = T_delta
                gamma_best = gamma
            else:
                return gamma_best
            gamma += gamma_step
        return gamma_best

    def getDilationCorrectedBeltLength(self, belt_length, F_belt):
        return belt_length / (1 + self.belt_elongation_coefficient * F_belt)

    def getBeltLengths(self, x, y):
        frameX = x + self.minSafeXOffset
        frameY = y + self.minSafeY
        gamma = self.gamma_last_position
        F_L = F_R = 0.0
        # Not in Mural's code: whether the loop ended by its termination test
        # rather than by running out of iterations. The table carries it so the
        # test can tell Mural's right answers from its wrong ones (after a jump).
        self.converged = False
        for _ in range(20):
            phi_L, phi_R = self.getBeltAngles(frameX, frameY, gamma)
            F_L, F_R = self.getBeltForces(phi_L, phi_R)
            gamma_last = gamma
            gamma = self.solveTorqueEquilibrium(phi_L, phi_R, F_L, F_R, gamma)
            if abs(gamma_last - gamma) < 0.25 / 180.0 * PI:
                self.converged = True
                break
        self.gamma_last_position = gamma

        leftX, leftY = self.getLeftTangentPoint(frameX, frameY, gamma)
        rightX, rightY = self.getRightTangentPoint(frameX, frameY, gamma)
        leftLegFlat = math.sqrt(leftX ** 2 + leftY ** 2)
        rightLegFlat = math.sqrt((self.topDistance - rightX) ** 2 + rightY ** 2)
        leftLeg = math.sqrt(leftLegFlat ** 2 + self.midPulleyToWall ** 2)
        rightLeg = math.sqrt(rightLegFlat ** 2 + self.midPulleyToWall ** 2)
        leftLeg = self.getDilationCorrectedBeltLength(leftLeg, F_L)
        rightLeg = self.getDilationCorrectedBeltLength(rightLeg, F_R)
        return leftLeg, rightLeg, gamma


def path(width, height, step):
    """Home first, then a serpentine over the area: the order a plot would
    visit points in, so the warm start is exercised the way it is in use."""
    points = [(width / 2, 350.0)]
    ys = [i * step for i in range(int(height // step) + 1)]
    xs = [i * step for i in range(int(width // step) + 1)]
    if xs[-1] != width:
        xs.append(width)
    for row, y in enumerate(ys):
        for x in (xs if row % 2 == 0 else reversed(xs)):
            points.append((x, y))
    # A jump across the whole area, where the warm start is furthest off.
    points += [(0.0, 0.0), (width, height), (width / 2, 350.0)]
    return points


# (anchor distance, pulley diameter, drawing height, grid step)
GEOMETRIES = [
    (1000.0, 10.14926, 600.0, 50.0),   # Mural's README example, default pulley
    (1600.0, 10.19, 900.0, 75.0),      # a larger wall, a calibrated pulley
]


def table():
    """Every row of every set, in order: (set, x, y, left, right, tilt, converged)."""
    for n, (d, diameter, height, step) in enumerate(GEOMETRIES):
        m = Movement(d, diameter)
        for x, y in path(m.width, height, step):
            left, right, gamma = m.getBeltLengths(x, y)
            yield n, x, y, left, right, gamma, int(m.converged)


def check(tolerance=1e-9):
    """Compares the committed table with a fresh computation; exit 1 on drift."""
    with open(OUT, encoding="utf-8") as f:
        committed = [tuple(float(v) for v in row)
                     for row in re.findall(r"\{([-+.\deE]+), ([-+.\deE]+), ([-+.\deE]+), "
                                           r"([-+.\deE]+), ([-+.\deE]+), ([01])\},", f.read())]
    fresh = [row[1:] for row in table()]
    if len(committed) != len(fresh):
        print("reference.h has %d rows, the script makes %d - regenerate it"
              % (len(committed), len(fresh)))
        return 1
    worst = max(abs(a - b) for c, f in zip(committed, fresh) for a, b in zip(c, f))
    print("reference.h: %d rows, largest difference %.3g" % (len(fresh), worst))
    if worst > tolerance:
        print("reference.h is out of date - regenerate it")
        return 1
    return 0


def main():
    if "--check" in sys.argv[1:]:
        return check()

    lines = [
        "// Generated by scripts/kinematics_reference.py - do not edit.",
        "// Belt lengths from a Python port of Mural's Movement::getBeltLengths()",
        "// (nikivanov/mural, CC BY-NC 4.0). See that script for how and why.",
        "#ifndef KINEMATICS_REFERENCE_H",
        "#define KINEMATICS_REFERENCE_H",
        "",
        "struct ReferencePoint { double x, y, left, right, tilt; int converged; };",
        "struct ReferenceSet { double anchorDistance, pulleyDiameter; int count; const ReferencePoint *points; };",
        "",
    ]
    sets = []
    for n, (d, diameter, height, step) in enumerate(GEOMETRIES):
        m = Movement(d, diameter)
        rows = []
        for x, y in path(m.width, height, step):
            left, right, gamma = m.getBeltLengths(x, y)
            rows.append("    {%.17g, %.17g, %.17g, %.17g, %.17g, %d},"
                        % (x, y, left, right, gamma, m.converged))
        lines.append("static const ReferencePoint REFERENCE_%d[] = {" % n)
        lines += rows
        lines.append("};")
        lines.append("")
        sets.append("    {%.17g, %.17g, %d, REFERENCE_%d}," % (d, diameter, len(rows), n))
    lines.append("static const ReferenceSet REFERENCE_SETS[] = {")
    lines += sets
    lines.append("};")
    lines.append("")
    lines.append("#endif")

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8", newline="\n") as out:
        out.write("\n".join(lines) + "\n")
    print("wrote %s (%s points)" % (os.path.relpath(OUT, PROJECT_DIR),
                                   " + ".join(str(s.split(",")[2].strip()) for s in sets)))


if __name__ == "__main__":
    sys.exit(main())
