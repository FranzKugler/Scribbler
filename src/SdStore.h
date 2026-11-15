/**
 * SdStore
 * The microSD card: mounted once at boot, in SD_MMC 1-bit mode on its own
 * pins (Board.h), with jobs under /jobs.
 *
 * Optional at runtime, like the display. No card is not a reason not to boot -
 * the web UI must come up to say that there is no card - it only means nothing
 * can be plotted. Mural halts without one; that is the opposite choice, made
 * on purpose.
 *
 * Mounted at boot only. A card pushed in later is seen after the next restart;
 * the slot has no card-detect line wired, so there is nothing to wake up on.
 *
 * What it is not: the job runner or a file manager. It answers "is there a
 * card, how full is it", and hands out the filesystem.
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#ifndef SDSTORE_H
#define SDSTORE_H

#include <Arduino.h>
#include <FS.h>

namespace SdStore
{
    /** Where jobs live on the card. */
    constexpr const char *JOB_DIR = "/jobs";

    void begin();
    bool present();

    fs::FS &fs();

    uint64_t totalBytes();
    uint64_t usedBytes();
}

#endif
