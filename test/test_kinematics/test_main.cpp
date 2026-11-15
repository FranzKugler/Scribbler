/**
 * test_kinematics
 * Holds src/Kinematics to the reference table that
 * scripts/kinematics_reference.py computes with a port of Mural's own code.
 *
 * Runs on the host: pio test -e native.
 *
 * Tolerances: 1e-6 mm on the lengths and 1e-9 rad on the tilt. Both sides do
 * the same double arithmetic in the same order, so they agree to the last few
 * bits; the slack is for libm implementations rounding sin/atan2 differently.
 * A real disagreement - a solver step taken differently - is a 0.2 degree jump
 * in the tilt and tenths of a millimetre in the lengths, far outside it.
 *
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#include <unity.h>
#include <cmath>
#include <cstdio>

#include "Kinematics.h"
#include "reference.h"

static constexpr double LENGTH_TOLERANCE = 1e-6;   // mm
static constexpr double TILT_TOLERANCE = 1e-9;     // rad

void setUp() {}
void tearDown() {}

static void runSet(const ReferenceSet &set)
{
    Kinematics::Geometry geometry;
    geometry.anchorDistance = set.anchorDistance;
    geometry.pulleyDiameter = set.pulleyDiameter;
    Kinematics::Model model(geometry);
    model.resetTilt();

    for (int i = 0; i < set.count; i++)
    {
        const ReferencePoint &p = set.points[i];
        const Kinematics::Pose pose = model.solve(p.x, p.y);

        char where[96];
        snprintf(where, sizeof where, "d=%.0f point %d (%.1f, %.1f)",
                 set.anchorDistance, i, p.x, p.y);
        TEST_ASSERT_DOUBLE_WITHIN_MESSAGE(LENGTH_TOLERANCE, p.left, pose.left, where);
        TEST_ASSERT_DOUBLE_WITHIN_MESSAGE(LENGTH_TOLERANCE, p.right, pose.right, where);
        TEST_ASSERT_DOUBLE_WITHIN_MESSAGE(TILT_TOLERANCE, p.tilt, pose.tilt, where);
    }
}

static void test_reference_paths()
{
    for (const ReferenceSet &set : REFERENCE_SETS) runSet(set);
}

/** The drawing area follows from d alone: 0.6 d wide, 0.2 d / 0.25 d in. */
static void test_area()
{
    Kinematics::Geometry geometry;
    geometry.anchorDistance = 1000.0;
    Kinematics::Model model(geometry);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 600.0, model.width());
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 200.0, model.originX());
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 250.0, model.originY());
}

/** On the centre line the bot hangs level and both belts are equal. */
static void test_symmetry()
{
    Kinematics::Geometry geometry;
    geometry.anchorDistance = 1000.0;
    Kinematics::Model model(geometry);
    const Kinematics::Pose pose = model.solve(model.width() / 2, 300.0);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, pose.left, pose.right);
    TEST_ASSERT_DOUBLE_WITHIN(0.2 * M_PI / 180.0, 0.0, pose.tilt);
}

/**
 * A jump across the area does not converge in one solve() - the reference
 * table holds the wrong answer Mural's code gives there - and settle() must
 * land where approaching the point gradually does. "Where" within the solver's
 * 0.2 degree grid: the grid is anchored at the warm start, so two routes to the
 * same point can end one grid step apart, ~0.07 mm of belt.
 */
static void test_settle_after_jump()
{
    const ReferenceSet &set = REFERENCE_SETS[0];
    const ReferencePoint &corner = set.points[1];   // (0, 0), reached from home
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, corner.x);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, corner.y);

    Kinematics::Geometry geometry;
    geometry.anchorDistance = set.anchorDistance;
    geometry.pulleyDiameter = set.pulleyDiameter;
    Kinematics::Model model(geometry);

    model.settle(model.width(), 600.0);             // far side, tilted ~ -40 degrees
    TEST_ASSERT_FALSE(model.solve(0.0, 0.0).converged);

    model.settle(model.width(), 600.0);
    const Kinematics::Pose pose = model.settle(0.0, 0.0);
    TEST_ASSERT_TRUE(pose.converged);
    TEST_ASSERT_DOUBLE_WITHIN(0.15, corner.left, pose.left);
    TEST_ASSERT_DOUBLE_WITHIN(0.15, corner.right, pose.right);
    TEST_ASSERT_DOUBLE_WITHIN(0.21 * M_PI / 180.0, corner.tilt, pose.tilt);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_area);
    RUN_TEST(test_symmetry);
    RUN_TEST(test_reference_paths);
    RUN_TEST(test_settle_after_jump);
    return UNITY_END();
}
