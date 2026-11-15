/**
 * JobFormat
 * See JobFormat.h.
 *
 * strtod rather than sscanf: it says where it stopped, which is what tells
 * "12.5 30" from "12.5 30abc" - the second is Unknown, not a move.
 *
 * @mc       ESP32S3 (and the host, for the tests)
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#include "JobFormat.h"

#include <cmath>
#include <cstdlib>

namespace
{
    bool space(char c)
    {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    }

    const char *skipSpace(const char *p)
    {
        while (*p && space(*p)) p++;
        return p;
    }

    /** Nothing but whitespace from p to the end. */
    bool atEnd(const char *p)
    {
        return *skipSpace(p) == '\0';
    }

    /** A finite number at p; `end` points past it. */
    bool number(const char *p, double &out, const char *&end)
    {
        char *stop = nullptr;
        out = std::strtod(p, &stop);
        if (stop == p || !std::isfinite(out)) return false;
        end = stop;
        return true;
    }
}

namespace JobFormat
{
    Line parse(const char *text)
    {
        Line line;
        if (!text) return line;

        const char *p = skipSpace(text);
        if (*p == '\0' || *p == '#') return line;   // Empty

        const char *end = nullptr;
        switch (*p)
        {
            case 'd':
            case 'h':
                if (number(p + 1, line.value, end) && atEnd(end))
                {
                    line.kind = *p == 'd' ? Kind::Distance : Kind::Height;
                    return line;
                }
                line.kind = Kind::Unknown;
                return line;

            case 'p':
                // Mural: "p1" is down, any other p-line is up. Kept, but only
                // for p followed by digits - "pause" is not a pen command.
                if (p[1] == '1' && atEnd(p + 2)) line.kind = Kind::PenDown;
                else if (p[1] >= '0' && p[1] <= '9' && atEnd(p + 2)) line.kind = Kind::PenUp;
                else line.kind = Kind::Unknown;
                return line;

            default:
                break;
        }

        double x, y;
        if (number(p, x, end))
        {
            const char *second = skipSpace(end);
            // "x y" needs the space: "12-3" is not two numbers to a reader.
            if (second != end && number(second, y, end) && atEnd(end))
            {
                line.kind = Kind::Move;
                line.x = x;
                line.y = y;
                return line;
            }
        }
        line.kind = Kind::Unknown;
        return line;
    }
}
