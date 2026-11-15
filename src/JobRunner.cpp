/**
 * JobRunner
 * See JobRunner.h.
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#include "JobRunner.h"

#include "SdStore.h"
#include "LogBuffer.h"

namespace
{
    using JobRunner::Phase;

    // Longer than any line Mural writes ("1234.5678 1234.5678" is 19). A line
    // that does not fit is read to its end and reported as Unknown.
    constexpr size_t LINE_BUFFER = 96;

    // Coordinates may sit this far outside the area: a file scaled to the
    // full width lands its rightmost point a rounding error past it.
    constexpr double EDGE_SLACK_MM = 0.5;

    Phase current = Phase::Idle;
    fs::File file;
    char jobName[65] = "";
    const char *failure = nullptr;
    uint32_t failureLine = 0;

    double totalMm = 0.0, doneMm = 0.0;
    double width = 0.0, maxY = 0.0;

    // The check's own walk along the path.
    double walkX = 0.0, walkY = 0.0, startX = 0.0, startY = 0.0;
    uint32_t lineNo = 0, moves = 0;

    void fail(const char *code, uint32_t line = 0)
    {
        failure = code;
        failureLine = line;
        current = Phase::Failed;
        if (file) file.close();
        if (line) debugW("Job %s: %s at line %u", jobName, code, (unsigned)line);
        else debugW("Job %s: %s", jobName, code);
    }

    /**
     * Reads one line into buf; false at the end of the file. A line longer
     * than the buffer is consumed to its end and comes back as "?", which the
     * parser calls Unknown.
     */
    bool readLine(char *buf, size_t size)
    {
        size_t n = 0;
        bool any = false, overflow = false;
        while (true)
        {
            const int c = file.read();
            if (c < 0) break;
            any = true;
            if (c == '\n') break;
            if (n + 1 < size) buf[n++] = (char)c;
            else overflow = true;
        }
        if (!any) return false;
        if (overflow)
        {
            buf[0] = '?';
            n = 1;
        }
        buf[n] = '\0';
        lineNo++;
        return true;
    }
}

namespace JobRunner
{
    bool validName(const char *name)
    {
        if (!name || !*name || name[0] == '.') return false;
        size_t n = 0;
        for (const char *p = name; *p; p++, n++)
        {
            const char c = *p;
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
            if (!ok || n >= 64) return false;
        }
        return true;
    }

    const char *open(const char *name, double sx, double sy, double w, double my)
    {
        if (!validName(name)) return "jobName";
        if (!SdStore::present()) return "noCard";

        close();
        reset();
        strlcpy(jobName, name, sizeof jobName);

        String path = String(SdStore::JOB_DIR) + "/" + name;
        if (!SdStore::fs().exists(path)) return "jobNotFound";
        file = SdStore::fs().open(path, "r");
        if (!file) return "jobOpen";

        width = w;
        maxY = my;
        startX = walkX = sx;
        startY = walkY = sy;
        totalMm = doneMm = 0.0;
        lineNo = moves = 0;
        current = Phase::Checking;
        debugI("Job %s: checking (%u bytes)", jobName, (unsigned)file.size());
        return nullptr;
    }

    void check(uint32_t budget)
    {
        if (current != Phase::Checking) return;

        char buf[LINE_BUFFER];
        for (uint32_t i = 0; i < budget; i++)
        {
            if (!readLine(buf, sizeof buf))
            {
                // End of the dry pass.
                if (moves == 0)
                {
                    fail("jobEmpty");
                    return;
                }
                if (!file.seek(0))
                {
                    fail("jobRead");
                    return;
                }
                lineNo = 0;
                current = Phase::Running;
                debugI("Job %s: %u moves, %.0f mm - running", jobName, (unsigned)moves, totalMm);
                return;
            }
            const JobFormat::Line l = JobFormat::parse(buf);
            if (l.kind != JobFormat::Kind::Move) continue;

            if (l.x < -EDGE_SLACK_MM || l.x > width + EDGE_SLACK_MM ||
                l.y < -EDGE_SLACK_MM || l.y > maxY + EDGE_SLACK_MM)
            {
                fail("outsideArea", lineNo);
                return;
            }
            totalMm += hypot(l.x - walkX, l.y - walkY);
            walkX = l.x;
            walkY = l.y;
            moves++;
        }
    }

    bool next(JobFormat::Line &out)
    {
        if (current != Phase::Running) return false;

        char buf[LINE_BUFFER];
        while (readLine(buf, sizeof buf))
        {
            const JobFormat::Line l = JobFormat::parse(buf);
            switch (l.kind)
            {
                case JobFormat::Kind::Move:
                    // Clamp the slack the check allowed back onto the area.
                    out = l;
                    out.x = constrain(out.x, 0.0, width);
                    out.y = constrain(out.y, 0.0, maxY);
                    return true;
                case JobFormat::Kind::PenDown:
                case JobFormat::Kind::PenUp:
                    out = l;
                    return true;
                default:
                    break;   // headers, comments, lines from a newer format
            }
        }
        current = Phase::Finished;
        file.close();
        debugI("Job %s: finished, %.0f of %.0f mm", jobName, doneMm, totalMm);
        return false;
    }

    void moved(double mm)
    {
        doneMm += mm;
    }

    void close()
    {
        if (file) file.close();
        if (current == Phase::Checking || current == Phase::Running)
        {
            debugI("Job %s: ended at %.0f of %.0f mm", jobName, doneMm, totalMm);
            current = Phase::Idle;
        }
    }

    void reset()
    {
        if (file) file.close();
        current = Phase::Idle;
        jobName[0] = '\0';
        failure = nullptr;
        failureLine = 0;
        totalMm = doneMm = 0.0;
    }

    Phase phase()        { return current; }
    const char *name()   { return jobName; }
    const char *error()  { return failure; }
    uint32_t errorLine() { return failureLine; }
    double total()       { return totalMm; }
    double done()        { return doneMm; }

    const char *phaseName()
    {
        switch (current)
        {
            case Phase::Idle:     return "idle";
            case Phase::Checking: return "checking";
            case Phase::Running:  return "running";
            case Phase::Finished: return "finished";
            case Phase::Failed:   return "failed";
        }
        return "unknown";
    }
}
