/**
 * test_planner
 * The planner's promises, checked by slicing its output the way the firmware
 * will: in fixed time steps, reading the position at the end of each.
 *
 * Runs on the host: pio test -e native.
 *
 * What is checked, and why each matters on the wall:
 *   - every move ends exactly at its target (no drift across thousands of
 *     segments),
 *   - speed never exceeds the requested one, and changes no faster than the
 *     acceleration allows - measured from the slices, not trusted from the
 *     planner's own numbers,
 *   - a straight line cut into pieces runs through at speed, which is the
 *     whole point over Mural's stop-every-millimetre,
 *   - a corner slows, a reversal stops,
 *   - the path stays on the segments (the pen does not cut corners),
 *   - pushing while slicing - the way a job streams - gives the same result.
 *
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#include <unity.h>
#include <cmath>
#include <vector>

#include "Planner.h"

static constexpr double DT = 0.005;   // the firmware's slice
static constexpr double ACCEL = 500.0;

void setUp() {}
void tearDown() {}

struct Sample
{
    double x, y, dt, speed;
};

/** Slices until idle and appends every slice to `out`. */
static void drainInto(Planner::Queue &q, std::vector<Sample> &out)
{
    q.finish();
    Planner::Slice s;
    for (int guard = 0; guard < 2000000 && q.slice(DT, s); guard++)
        out.push_back({s.x, s.y, s.dt, s.speed});
}

static std::vector<Sample> drain(Planner::Queue &q)
{
    std::vector<Sample> out;
    drainInto(q, out);
    return out;
}

/**
 * Pushes the way the firmware will: when the buffer is full, slice until
 * there is room - and keep those slices, they are part of the motion.
 */
static void pushInto(Planner::Queue &q, double x, double y, double speed, std::vector<Sample> &out)
{
    Planner::Slice s;
    while (!q.push(x, y, speed))
    {
        TEST_ASSERT_TRUE(q.slice(DT, s));
        out.push_back({s.x, s.y, s.dt, s.speed});
    }
}

static Planner::Queue queue()
{
    Planner::Limits limits;
    limits.accel = ACCEL;
    limits.junctionDeviation = 0.05;
    Planner::Queue q(limits);
    q.setPosition(0.0, 0.0);
    return q;
}

/**
 * The two limits, each checked with the number that cannot flatter it:
 *
 *   speed  from the chord between consecutive slice ends. A chord is never
 *          longer than the path it spans - across a corner it is shorter - so
 *          a chord speed over the limit is a real violation.
 *   accel  from the speed the planner reports at each slice end. Chord speeds
 *          dip at every corner a slice straddles, and their differences would
 *          blame the planner for a geometric effect.
 *
 * And the two are tied together: the chord speed may not exceed the larger of
 * the reported speeds at its ends by more than a*dt/2 - what a peak inside the
 * slice can add (a triangle profile's top) - so the reported numbers cannot
 * quietly understate how fast the pen went.
 */
static void checkLimits(const std::vector<Sample> &samples, double x0, double y0, double vMax)
{
    double px = x0, py = y0, pv = 0.0;
    for (size_t i = 0; i < samples.size(); i++)
    {
        const Sample &s = samples[i];
        TEST_ASSERT_TRUE(s.dt > 0.0);
        const double chord = std::hypot(s.x - px, s.y - py) / s.dt;
        TEST_ASSERT_TRUE_MESSAGE(chord <= vMax * 1.001, "speed limit");
        TEST_ASSERT_TRUE_MESSAGE(chord <= std::fmax(pv, s.speed) * 1.001 + 0.5 * ACCEL * s.dt + 1e-9,
                                 "reported speed too low");
        TEST_ASSERT_TRUE_MESSAGE(std::fabs(s.speed - pv) <= ACCEL * s.dt * 1.001 + 1e-9, "accel limit");
        px = s.x;
        py = s.y;
        pv = s.speed;
    }
}

static double totalTime(const std::vector<Sample> &samples)
{
    double t = 0.0;
    for (const Sample &s : samples) t += s.dt;
    return t;
}

/** One segment: a trapezoid, ending exactly on the target, in the right time. */
static void test_single_segment()
{
    Planner::Queue q = queue();
    TEST_ASSERT_TRUE(q.push(100.0, 0.0, 50.0));
    const auto samples = drain(q);

    TEST_ASSERT_TRUE(q.idle());
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 100.0, samples.back().x);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, samples.back().y);
    checkLimits(samples, 0.0, 0.0, 50.0);

    // Analytic: accelerate to 50 in 0.1 s (2.5 mm), the same down, cruise 95 mm.
    const double expected = 0.1 + 95.0 / 50.0 + 0.1;
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, expected, totalTime(samples));
}

/** Too short to reach the speed: a triangle, peak below the limit. */
static void test_short_segment_is_a_triangle()
{
    Planner::Queue q = queue();
    q.push(2.0, 0.0, 100.0);
    const auto samples = drain(q);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 2.0, samples.back().x);
    // Peak v = sqrt(a * L) = sqrt(1000) ~ 31.6; time 2 * v / a.
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 2.0 * std::sqrt(ACCEL * 2.0) / ACCEL, totalTime(samples));
    checkLimits(samples, 0.0, 0.0, 100.0);
}

/**
 * A straight 100 mm line in 1 mm pieces - what Mural's files are made of -
 * takes as long as one 100 mm segment. Mural stops at each piece.
 */
static void test_straight_pieces_run_through()
{
    Planner::Queue q = queue();
    std::vector<Sample> samples;
    for (int i = 1; i <= 100; i++) pushInto(q, i * 1.0, 0.0, 50.0, samples);
    drainInto(q, samples);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 100.0, samples.back().x);
    checkLimits(samples, 0.0, 0.0, 50.0);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 0.1 + 95.0 / 50.0 + 0.1, totalTime(samples));
}

/** A right angle slows to the junction speed; a reversal comes to a stop. */
static void test_corner_slows_reversal_stops()
{
    // Junction speed for 90 degrees: sin(45) = 0.7071,
    // v^2 = 500 * 0.05 * 0.7071 / 0.2929 = 60.36 -> v = 7.77 mm/s.
    const double vCorner = std::sqrt(ACCEL * 0.05 * std::sqrt(0.5) / (1.0 - std::sqrt(0.5)));

    Planner::Queue q = queue();
    q.push(50.0, 0.0, 50.0);
    q.push(50.0, 50.0, 50.0);
    auto samples = drain(q);
    checkLimits(samples, 0.0, 0.0, 50.0);

    // The slowest reported speed near the corner: the junction speed, or up
    // to one slice of acceleration above it - a slice rarely ends exactly on
    // the corner.
    double slowest = 1e9;
    for (const Sample &s : samples)
        if (std::hypot(s.x - 50.0, s.y) < 2.0) slowest = std::fmin(slowest, s.speed);
    TEST_ASSERT_TRUE(slowest >= vCorner - 1e-6);
    TEST_ASSERT_TRUE(slowest <= vCorner + ACCEL * DT + 1e-6);

    // Straight back: the pen must stand at the turning point. A slice runs on
    // into the next block within its dt, so none ends exactly at the turn;
    // but one ends within half a slice of acceleration of it, at a speed no
    // higher than one slice of acceleration - possible only if it stopped.
    Planner::Queue r = queue();
    r.push(50.0, 0.0, 50.0);
    r.push(0.0, 0.0, 50.0);
    bool stoppedAtTurn = false;
    Planner::Slice s;
    r.finish();
    while (r.slice(DT, s))
    {
        if (std::fabs(s.x - 50.0) <= 0.5 * ACCEL * DT * DT + 1e-9 && s.speed <= ACCEL * DT + 1e-9)
            stoppedAtTurn = true;
    }
    TEST_ASSERT_TRUE(stoppedAtTurn);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, s.x);
}

/** Every slice end lies on one of the two segments: corners are not cut. */
static void test_path_stays_on_segments()
{
    Planner::Queue q = queue();
    q.push(30.0, 0.0, 80.0);
    q.push(30.0, 40.0, 80.0);
    for (const Sample &s : drain(q))
    {
        const bool onFirst = std::fabs(s.y) < 1e-9 && s.x >= -1e-9 && s.x <= 30.0 + 1e-9;
        const bool onSecond = std::fabs(s.x - 30.0) < 1e-9 && s.y >= -1e-9 && s.y <= 40.0 + 1e-9;
        TEST_ASSERT_TRUE(onFirst || onSecond);
    }
}

/**
 * A circle in 1 mm chords, pushed while slicing the way a job streams in, with
 * the buffer never allowed to fill: the end is exact, the limits hold, and the
 * pen never stops before the end - running dry would show as a zero speed.
 */
static void test_streaming_circle()
{
    Planner::Queue q = queue();
    const double r = 50.0;
    const int n = (int)(2.0 * M_PI * r);   // ~1 mm chords
    q.setPosition(r, 0.0);

    std::vector<Sample> samples;
    Planner::Slice s;
    for (int i = 1; i <= n; i++)
    {
        const double a = 2.0 * M_PI * i / n;
        pushInto(q, r * std::cos(a), r * std::sin(a), 40.0, samples);
        // Two slices per piece: slower than the pen eats them, so the buffer
        // stays short and the look-ahead is thin - the hard case.
        for (int k = 0; k < 2 && q.ready(); k++)
        {
            q.slice(DT, s);
            samples.push_back({s.x, s.y, s.dt, s.speed});
        }
    }
    drainInto(q, samples);

    TEST_ASSERT_DOUBLE_WITHIN(1e-9, r, samples.back().x);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, samples.back().y);
    checkLimits(samples, r, 0.0, 40.0);
    for (size_t i = 0; i + 1 < samples.size(); i++)
        TEST_ASSERT_TRUE_MESSAGE(samples[i].speed > 0.0, "stopped before the end");
}

/** finish() then more pushes: the planner picks up from standstill. */
static void test_finish_then_continue()
{
    Planner::Queue q = queue();
    q.push(10.0, 0.0, 30.0);
    auto first = drain(q);
    TEST_ASSERT_TRUE(q.idle());
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 10.0, first.back().x);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, first.back().speed);

    q.push(10.0, 10.0, 30.0);
    auto second = drain(q);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 10.0, second.back().y);
    checkLimits(second, 10.0, 0.0, 30.0);
}

/** From standstill nothing is sliced until the buffer is full or finished. */
static void test_ready_waits_for_lookahead()
{
    Planner::Queue q = queue();
    q.push(1.0, 0.0, 30.0);
    TEST_ASSERT_FALSE(q.ready());
    q.finish();
    TEST_ASSERT_TRUE(q.ready());
}

/** A zero-length move is dropped; a full buffer refuses. */
static void test_degenerate_input()
{
    Planner::Queue q = queue();
    TEST_ASSERT_TRUE(q.push(0.0, 0.0, 30.0));
    TEST_ASSERT_TRUE(q.idle());
    for (size_t i = 1; i <= Planner::Queue::CAPACITY; i++) TEST_ASSERT_TRUE(q.push(i * 1.0, 0.0, 30.0));
    TEST_ASSERT_TRUE(q.full());
    TEST_ASSERT_FALSE(q.push(1000.0, 0.0, 30.0));
}

/** Slices for `seconds` (while there is anything) into `out`. */
static void sliceFor(Planner::Queue &q, double seconds, std::vector<Sample> &out)
{
    Planner::Slice s;
    for (double t = 0.0; t < seconds && q.slice(DT, s); t += DT) out.push_back({s.x, s.y, s.dt, s.speed});
}

/**
 * stopSoon() at cruising speed on one long line: the pen stops v²/2a past
 * where it was, inside the limits, and the dropped target is the line's end.
 */
static void test_stop_soon_on_a_line()
{
    Planner::Queue q = queue();
    q.push(1000.0, 0.0, 50.0);
    q.finish();
    std::vector<Sample> samples;
    sliceFor(q, 1.0, samples);
    const double x0 = samples.back().x, v = samples.back().speed;
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 50.0, v);

    Planner::Target dropped[Planner::Queue::CAPACITY];
    const size_t n = q.stopSoon(dropped, Planner::Queue::CAPACITY);
    TEST_ASSERT_EQUAL(1, n);
    TEST_ASSERT_EQUAL_DOUBLE(1000.0, dropped[0].x);
    TEST_ASSERT_EQUAL_DOUBLE(50.0, dropped[0].speed);

    drainInto(q, samples);
    checkLimits(samples, 0.0, 0.0, 50.0);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, x0 + v * v / (2.0 * ACCEL), samples.back().x);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, samples.back().speed);
    TEST_ASSERT_TRUE(q.idle());
}

/**
 * The same through 1 mm pieces, where the stop spans several blocks - then
 * carry on from the dropped targets, as a resumed job does: the pen ends
 * where the uninterrupted path would, never goes backwards, stays in limits.
 */
static void test_stop_soon_through_pieces_then_resume()
{
    Planner::Queue q = queue();
    std::vector<Sample> samples;
    int next = 1;
    // Stream until well into the line and up to speed.
    for (; next <= 120; next++) pushInto(q, next * 1.0, 0.0, 100.0, samples);
    sliceFor(q, 0.3, samples);
    const double x0 = samples.back().x, v = samples.back().speed;
    TEST_ASSERT_TRUE(v > 90.0);

    Planner::Target dropped[Planner::Queue::CAPACITY];
    const size_t n = q.stopSoon(dropped, Planner::Queue::CAPACITY);
    const double stopAt = x0 + v * v / (2.0 * ACCEL);
    TEST_ASSERT_TRUE(n > 1);
    // The first dropped target ends the block the stop cut short.
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, std::ceil(stopAt), dropped[0].x);
    for (size_t i = 1; i < n; i++) TEST_ASSERT_DOUBLE_WITHIN(1e-9, dropped[i - 1].x + 1.0, dropped[i].x);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, next - 1.0, dropped[n - 1].x);

    drainInto(q, samples);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, stopAt, samples.back().x);
    TEST_ASSERT_TRUE(q.idle());

    // Resume: the dropped targets first, then the rest of the line.
    for (size_t i = 0; i < n; i++) pushInto(q, dropped[i].x, dropped[i].y, dropped[i].speed, samples);
    for (; next <= 200; next++) pushInto(q, next * 1.0, 0.0, 100.0, samples);
    drainInto(q, samples);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 200.0, samples.back().x);

    checkLimits(samples, 0.0, 0.0, 100.0);
    double px = 0.0;
    for (const Sample &s : samples)
    {
        TEST_ASSERT_TRUE_MESSAGE(s.x >= px - 1e-9, "went backwards");
        TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.0, s.y);
        px = s.x;
    }
}

/** Nothing buffered: nothing to do. Buffered but not started: stop at once. */
static void test_stop_soon_idle_and_unstarted()
{
    Planner::Queue q = queue();
    Planner::Target dropped[Planner::Queue::CAPACITY];
    TEST_ASSERT_EQUAL(0, q.stopSoon(dropped, Planner::Queue::CAPACITY));

    q.push(10.0, 0.0, 30.0);
    q.push(10.0, 10.0, 30.0);
    TEST_ASSERT_EQUAL(2, q.stopSoon(dropped, Planner::Queue::CAPACITY));
    TEST_ASSERT_EQUAL_DOUBLE(10.0, dropped[0].x);
    TEST_ASSERT_EQUAL_DOUBLE(10.0, dropped[1].y);
    auto rest = drain(q);
    TEST_ASSERT_TRUE(q.idle());
    TEST_ASSERT_DOUBLE_WITHIN(1e-5, 0.0, rest.empty() ? 0.0 : rest.back().x);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_stop_soon_on_a_line);
    RUN_TEST(test_stop_soon_through_pieces_then_resume);
    RUN_TEST(test_stop_soon_idle_and_unstarted);
    RUN_TEST(test_single_segment);
    RUN_TEST(test_short_segment_is_a_triangle);
    RUN_TEST(test_straight_pieces_run_through);
    RUN_TEST(test_corner_slows_reversal_stops);
    RUN_TEST(test_path_stays_on_segments);
    RUN_TEST(test_streaming_circle);
    RUN_TEST(test_finish_then_continue);
    RUN_TEST(test_ready_waits_for_lookahead);
    RUN_TEST(test_degenerate_input);
    return UNITY_END();
}
