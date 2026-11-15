/**
 * Checkpoint
 * Where a running job could be picked up again after a power cut, a crash or
 * an emergency stop: the job's name and size, the file offset and line just
 * after a pen lift, and the progress up to there. One record in NVS
 * (namespace "checkpoint"), read once at boot.
 *
 * Why at a pen lift: there the path has run dry, so the pen's position, the
 * file offset and the distance done all describe the same moment - which no
 * point in the middle of a planned stroke does, with up to 64 moves buffered
 * ahead of the motors. Resuming from there redraws at most the strokes since
 * the last checkpoint, over the lines already on the wall.
 *
 * What it is not: the position. After a restart the belts are wound in and the
 * bot homed again (Plotter's states); the drawing's coordinates are the same
 * once it hangs where it hung, so the job can go on.
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  16.11.2026
 * @updated  16.11.2026
 */
#ifndef CHECKPOINT_H
#define CHECKPOINT_H

#include <Arduino.h>

namespace Checkpoint
{
    struct Record
    {
        char name[65] = "";
        uint32_t size = 0;      // the file's size then: a changed file is not resumed
        uint32_t offset = 0;    // the byte to read on from
        uint32_t line = 0;      // the line number there, for error reports
        double done = 0.0;      // mm of path behind it
        double total = 0.0;     // mm in the whole job, for the UI
    };

    /** Reads the stored record, if any. */
    void begin();

    /** A record is stored. */
    bool present();

    const Record &get();

    /** Stores `r`, replacing the old one. */
    void save(const Record &r);

    /** Forgets it - the job finished or was cancelled. */
    void clear();
}

#endif
