/**
 * JobFormat
 * One line of a job file, parsed.
 *
 * The format is Mural's (nikivanov/mural, src/runner.cpp; CC BY-NC 4.0), kept
 * compatible so that a file made by Mural's web UI plots unchanged:
 *
 *   d<total>     total path length [mm] - a hint for the progress bar
 *   h<height>    height of the drawing [mm]
 *   <x> <y>      move to image coordinates [mm], pen as last set
 *   p1           pen down
 *   p0           pen up (Mural treats every p-line but p1 as up)
 *
 * Deliberate differences from Mural's reader, none of which changes what a
 * Mural file means:
 *
 *   - Whitespace and a trailing CR are trimmed. Mural parses the d-line with
 *     substring(1, len - 1), dropping the last character on the assumption that
 *     it is a CR - in a file with plain LF endings that is the last digit.
 *   - A line it does not know is reported as Unknown and skipped by the
 *     runner, rather than read as a move to (0, 0) - which is where Mural's
 *     toDouble() puts garbage. New line types can be added that way without
 *     breaking older firmware (CLAUDE.md, "Job format").
 *   - '#' starts a comment.
 *
 * Pure C++: no Arduino, no files. The runner hands it lines; the host tests
 * hand it strings.
 *
 * @mc       ESP32S3 (and the host, for the tests)
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#ifndef JOBFORMAT_H
#define JOBFORMAT_H

namespace JobFormat
{
    enum class Kind
    {
        Empty,      // blank or comment
        Distance,   // d<value>
        Height,     // h<value>
        Move,       // x y
        PenDown,
        PenUp,
        Unknown
    };

    struct Line
    {
        Kind kind = Kind::Empty;
        double x = 0.0;
        double y = 0.0;
        double value = 0.0;   // Distance, Height
    };

    /** Parses one line, with or without its line ending. Never fails: Unknown. */
    Line parse(const char *text);
}

#endif
