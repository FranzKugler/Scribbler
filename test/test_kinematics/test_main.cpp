/**
 * test_kinematics
 * Holds src/Kinematics to the reference table that
 * scripts/kinematics_reference.py computes with a port of Mural's own code,
 * and holds our solver to Mural's wherever Mural's is right.
 *
 * Runs on the host: pio test -e native.
 *
 * Two kinds of tolerance:
 *
 *   solveMural() vs. the table: 1e-6 mm and 1e-9 rad. Both sides do the same
 *   double arithmetic in the same order, so they agree to the last few bits;
 *   the slack is for libm implementations rounding sin/atan2 differently. A
 *   real disagreement - a grid step taken differently - is a 0.2 degree jump
 *   in the tilt and tenths of a millimetre in the lengths, far outside it.
 *
 *   solve() vs. the table: half of Mural's 0.2 degree grid plus a little,
 *   because Mural's answer is quantised to that grid and ours is not.
 *   Measured: at most 0.107 degrees and 0.072 mm over both reference paths.
 *
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.1
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#include <unity.h>
#include <cmath>
#include <cstdio>

#include "Kinematics.h"
#include "reference.h"

static constexpr double DEG = M_PI / 180.0;

static constexpr double EXACT_LENGTH = 1e-6;    // mm
static constexpr double EXACT_TILT = 1e-9;      // rad
static constexpr double GRID_LENGTH = 0.10;     // mm
static constexpr double GRID_TILT = 0.12 * DEG;

void setUp() {}
void tearDown() {}

static Kinematics::Model modelFor(const ReferenceSet &set)
{
    Kinematics::Geometry geometry;
    geometry.anchorDistance = set.anchorDistance;
    geometry.pulleyDiameter = set.pulleyDiameter;
    Kinematics::Model model(geometry);
    model.resetTilt();
    return model;
}

static Kinematics::Model mural1000()
{
    Kinematics::Geometry geometry;
    geometry.anchorDistance = 1000.0;
    return Kinematics::Model(geometry);
}

/** The model transcription: Mural's solver reproduces Mural's numbers. */
static void test_mural_solver_matches_reference()
{
    for (const ReferenceSet &set : REFERENCE_SETS)
    {
        Kinematics::Model model = modelFor(set);
        for (int i = 0; i < set.count; i++)
        {
            const ReferencePoint &p = set.points[i];
            const Kinematics::Pose pose = model.solveMural(p.x, p.y);

            char where[96];
            snprintf(where, sizeof where, "d=%.0f point %d (%.1f, %.1f)",
                     set.anchorDistance, i, p.x, p.y);
            TEST_ASSERT_DOUBLE_WITHIN_MESSAGE(EXACT_LENGTH, p.left, pose.left, where);
            TEST_ASSERT_DOUBLE_WITHIN_MESSAGE(EXACT_LENGTH, p.right, pose.right, where);
            TEST_ASSERT_DOUBLE_WITHIN_MESSAGE(EXACT_TILT, p.tilt, pose.tilt, where);
            TEST_ASSERT_EQUAL_MESSAGE(p.converged != 0, pose.converged, where);
        }
    }
}

/** Ours agrees with Mural's within its grid, wherever Mural's converged. */
static void test_solver_matches_reference_within_grid()
{
    for (const ReferenceSet &set : REFERENCE_SETS)
    {
        Kinematics::Model model = modelFor(set);
        int compared = 0;
        for (int i = 0; i < set.count; i++)
        {
            const ReferencePoint &p = set.points[i];
            const Kinematics::Pose pose = model.solve(p.x, p.y);

            char where[96];
            snprintf(where, sizeof where, "d=%.0f point %d (%.1f, %.1f)",
                     set.anchorDistance, i, p.x, p.y);
            TEST_ASSERT_TRUE_MESSAGE(pose.converged, where);
            if (!p.converged) continue;   // Mural's answer is wrong there
            TEST_ASSERT_DOUBLE_WITHIN_MESSAGE(GRID_LENGTH, p.left, pose.left, where);
            TEST_ASSERT_DOUBLE_WITHIN_MESSAGE(GRID_LENGTH, p.right, pose.right, where);
            TEST_ASSERT_DOUBLE_WITHIN_MESSAGE(GRID_TILT, p.tilt, pose.tilt, where);
            compared++;
        }
        // The table marks exactly the jumps as not converged; if that ever
        // grows, the comparison above quietly gets weaker.
        TEST_ASSERT_GREATER_OR_EQUAL(set.count - 2, compared);
    }
}

/**
 * The answer belongs to the point, not to the route: the same point twice,
 * and the same point after a jump across the area, give the same lengths.
 * Mural's solver fails both - see the header of Kinematics.h.
 */
static void test_solver_is_independent_of_route()
{
    Kinematics::Model model = mural1000();

    model.solve(300.0, 350.0);
    const Kinematics::Pose a = model.solve(0.0, 0.0);
    const Kinematics::Pose b = model.solve(0.0, 0.0);
    model.solve(model.width(), 600.0);            // far side, tilted ~ -40 degrees
    const Kinematics::Pose c = model.solve(0.0, 0.0);

    TEST_ASSERT_DOUBLE_WITHIN(1e-7, a.left, b.left);
    TEST_ASSERT_DOUBLE_WITHIN(1e-7, a.right, b.right);
    TEST_ASSERT_DOUBLE_WITHIN(1e-7, a.left, c.left);
    TEST_ASSERT_DOUBLE_WITHIN(1e-7, a.right, c.right);
    TEST_ASSERT_DOUBLE_WITHIN(1e-8, a.tilt, c.tilt);
}

/**
 * Along a straight line in 0.5 mm steps, belt length is a smooth function of
 * x: its second difference is the curvature, ~4e-4 mm here. Mural's grid
 * makes it jump by up to 0.13 mm - the chatter a time-sliced planner would
 * turn into steps. 5e-3 separates the two by an order of magnitude each way.
 */
static void test_solver_is_smooth_along_a_line()
{
    Kinematics::Model model = mural1000();
    model.solve(0.0, 300.0);

    double previous2 = 0.0, previous1 = 0.0, worst = 0.0;
    for (int i = 0; i <= 1200; i++)
    {
        const double left = model.solve(i * 0.5, 300.0).left;
        if (i >= 2) worst = std::fmax(worst, std::fabs(left - 2.0 * previous1 + previous2));
        previous2 = previous1;
        previous1 = left;
    }
    TEST_ASSERT_DOUBLE_WITHIN(5e-3, 0.0, worst);
}

/** The drawing area follows from d alone: 0.6 d wide, 0.2 d / 0.25 d in. */
static void test_area()
{
    Kinematics::Model model = mural1000();
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 600.0, model.width());
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 200.0, model.originX());
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 250.0, model.originY());
}

/** On the centre line the bot hangs level and both belts are equal. */
static void test_symmetry()
{
    Kinematics::Model model = mural1000();
    const Kinematics::Pose pose = model.solve(model.width() / 2, 300.0);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, pose.left, pose.right);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, pose.tilt);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_area);
    RUN_TEST(test_symmetry);
    RUN_TEST(test_mural_solver_matches_reference);
    RUN_TEST(test_solver_matches_reference_within_grid);
    RUN_TEST(test_solver_is_independent_of_route);
    RUN_TEST(test_solver_is_smooth_along_a_line);
    return UNITY_END();
}
