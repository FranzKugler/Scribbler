/**
 * SdStore
 * SD_MMC in 1-bit mode; see SdStore.h.
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#include "SdStore.h"

#include <SD_MMC.h>

#include "Board.h"
#include "LogBuffer.h"

namespace
{
    bool mounted = false;

    // The card is read while the bot draws, a line at a time; 20 MHz is far
    // more than that needs and forgiving of a long trace. The default
    // (40 MHz) is the first thing to try if a card refuses to mount.
    constexpr int SD_KHZ = 20000;
}

namespace SdStore
{
    void begin()
    {
        if (!SD_MMC.setPins(Board::SD_CLK, Board::SD_CMD, Board::SD_DAT0))
        {
            debugE("SD: pins rejected");
            return;
        }
        // 1-bit mode; do not format a card that does not mount - it may hold
        // something, and the user can format it on a PC.
        if (!SD_MMC.begin("/sdcard", true, false, SD_KHZ))
        {
            debugW("SD: no card, or it does not mount - plotting is unavailable");
            return;
        }
        mounted = true;

        if (!SD_MMC.exists(JOB_DIR) && !SD_MMC.mkdir(JOB_DIR))
        {
            debugE("SD: cannot create %s", JOB_DIR);
        }
        debugI("SD: %llu MB card, %llu MB used",
               SD_MMC.totalBytes() / (1024 * 1024), SD_MMC.usedBytes() / (1024 * 1024));
    }

    bool present()
    {
        return mounted;
    }

    fs::FS &fs()
    {
        return SD_MMC;
    }

    uint64_t totalBytes()
    {
        return mounted ? SD_MMC.totalBytes() : 0;
    }

    uint64_t usedBytes()
    {
        return mounted ? SD_MMC.usedBytes() : 0;
    }
}
