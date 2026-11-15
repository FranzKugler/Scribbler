/**
 * JobRunner
 * A job file on the SD card, read a line at a time: checked before the first
 * move, then handed out command by command, with progress by distance.
 *
 * Streamed, never loaded: a job is easily megabytes and the heap is not
 * (CLAUDE.md, "Job format"). The check is a full pass over the file before
 * anything moves - every move inside the drawing area, at least one move,
 * and the total path length, which the progress bar then trusts rather than
 * the file's own d-line. Mural does the same dry run; here it runs in slices
 * from loop(), so a large file does not freeze the web UI while it is read.
 *
 * What it is not: the executor. It does not know about belts, pens or
 * speeds - Plotter asks for the next command and reports the distance it
 * covered. That split is what lets the planner (phase 6) replace Plotter's
 * executor without touching this.
 *
 * Refusal and failure codes: jobName, noCard, jobNotFound, jobOpen,
 * jobEmpty, outsideArea (with the line number), jobRead, jobChanged.
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#ifndef JOBRUNNER_H
#define JOBRUNNER_H

#include <Arduino.h>
#include "JobFormat.h"

namespace JobRunner
{
    enum class Phase : uint8_t
    {
        Idle,       // no job
        Checking,   // the dry pass is reading the file
        Running,    // handing out commands
        Finished,   // the file ran out
        Failed      // the check or a read failed; error() says why
    };

    /** A job name: 1-64 of [A-Za-z0-9._-], not starting with a dot. */
    bool validName(const char *name);

    /** Where to pick a job up again (Checkpoint): the file is still checked whole. */
    struct Resume
    {
        uint32_t size;     // must match the file's, or the job is refused (jobChanged)
        uint32_t offset;   // the byte to read on from
        uint32_t line;
        double done;       // mm already behind
    };

    /**
     * Opens /jobs/<name> and starts the check. The pen starts at (startX,
     * startY), which the first move's distance is counted from. With
     * `resume`, running starts at its offset instead of the top. nullptr or
     * a refusal code.
     */
    const char *open(const char *name, double startX, double startY, double width, double maxY,
                     const Resume *resume = nullptr);

    /** Advances the check by up to `lines` lines. Call while Checking. */
    void check(uint32_t lines);

    /** The next command (Move, PenDown, PenUp); false at the end or on failure. */
    bool next(JobFormat::Line &out);

    /** Plotter covered this much of the path. */
    void moved(double mm);

    /** Ends the job, whatever its phase. The record of it stays for publish(). */
    void close();

    /** Forgets even the record - a new job, or none. */
    void reset();

    Phase phase();
    const char *phaseName();
    const char *name();
    const char *error();
    uint32_t errorLine();
    double total();
    double done();

    /** The file's size, and the byte and line the next read starts at - for a checkpoint. */
    uint32_t size();
    uint32_t offset();
    uint32_t line();
}

#endif
