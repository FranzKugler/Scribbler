/**
 * test_jobformat
 * The job line parser against Mural's format, including the line endings
 * Mural's own reader trips over.
 *
 * Runs on the host: pio test -e native.
 *
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#include <unity.h>

#include "JobFormat.h"

using JobFormat::Kind;
using JobFormat::parse;

void setUp() {}
void tearDown() {}

static void test_header_lines()
{
    JobFormat::Line l = parse("d12345.6");
    TEST_ASSERT_TRUE(l.kind == Kind::Distance);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 12345.6, l.value);

    l = parse("h420");
    TEST_ASSERT_TRUE(l.kind == Kind::Height);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 420.0, l.value);
}

/** Mural drops the last character of the d-line; with plain LF that is a digit. */
static void test_line_endings_do_not_eat_digits()
{
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 12345.0, parse("d12345\n").value);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 12345.0, parse("d12345\r\n").value);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 12345.0, parse("d12345").value);
}

static void test_moves()
{
    JobFormat::Line l = parse("12.5 300.25");
    TEST_ASSERT_TRUE(l.kind == Kind::Move);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 12.5, l.x);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 300.25, l.y);

    l = parse("  0 0\r\n");
    TEST_ASSERT_TRUE(l.kind == Kind::Move);

    l = parse("1e2\t-3.5");
    TEST_ASSERT_TRUE(l.kind == Kind::Move);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 100.0, l.x);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, -3.5, l.y);   // the runner refuses it, the parser reads it
}

static void test_pen()
{
    TEST_ASSERT_TRUE(parse("p1").kind == Kind::PenDown);
    TEST_ASSERT_TRUE(parse("p1\r").kind == Kind::PenDown);
    TEST_ASSERT_TRUE(parse("p0").kind == Kind::PenUp);
    TEST_ASSERT_TRUE(parse("p2").kind == Kind::PenUp);   // Mural: any p-line but p1 is up
}

/** Garbage is Unknown, never a move to (0, 0) as Mural's toDouble() makes it. */
static void test_unknown_is_not_a_move()
{
    TEST_ASSERT_TRUE(parse("hello").kind == Kind::Unknown);
    TEST_ASSERT_TRUE(parse("12").kind == Kind::Unknown);
    TEST_ASSERT_TRUE(parse("12 30abc").kind == Kind::Unknown);
    TEST_ASSERT_TRUE(parse("12-3").kind == Kind::Unknown);
    TEST_ASSERT_TRUE(parse("pause").kind == Kind::Unknown);
    TEST_ASSERT_TRUE(parse("dabc").kind == Kind::Unknown);
    TEST_ASSERT_TRUE(parse("1 2 3").kind == Kind::Unknown);
    TEST_ASSERT_TRUE(parse("nan 3").kind == Kind::Unknown);
}

static void test_empty_and_comments()
{
    TEST_ASSERT_TRUE(parse("").kind == Kind::Empty);
    TEST_ASSERT_TRUE(parse("   \r\n").kind == Kind::Empty);
    TEST_ASSERT_TRUE(parse("# a comment").kind == Kind::Empty);
    TEST_ASSERT_TRUE(parse(nullptr).kind == Kind::Empty);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_header_lines);
    RUN_TEST(test_line_endings_do_not_eat_digits);
    RUN_TEST(test_moves);
    RUN_TEST(test_pen);
    RUN_TEST(test_unknown_is_not_a_move);
    RUN_TEST(test_empty_and_comments);
    return UNITY_END();
}
