/**
 * Checkpoint
 * See Checkpoint.h.
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  16.11.2026
 * @updated  16.11.2026
 */
#include "Checkpoint.h"

#include <Preferences.h>

#include "LogBuffer.h"

namespace
{
    constexpr const char *NAMESPACE = "checkpoint";
    constexpr const char *KEY = "job";

    // Bumped when Record changes shape, so an old blob is dropped rather than
    // misread.
    constexpr uint8_t RECORD_VERSION = 1;

    struct Stored
    {
        uint8_t version;
        Checkpoint::Record record;
    };

    Checkpoint::Record cached;
    bool have = false;
}

namespace Checkpoint
{
    void begin()
    {
        Preferences p;
        if (!p.begin(NAMESPACE, true)) return;   // nothing stored yet
        Stored s{};
        if (p.getBytesLength(KEY) == sizeof s && p.getBytes(KEY, &s, sizeof s) == sizeof s &&
            s.version == RECORD_VERSION)
        {
            s.record.name[sizeof s.record.name - 1] = '\0';
            cached = s.record;
            have = cached.name[0] != '\0';
        }
        p.end();
        if (have) debugI("Checkpoint: %s at %.0f of %.0f mm", cached.name, cached.done, cached.total);
    }

    bool present() { return have; }

    const Record &get() { return cached; }

    void save(const Record &r)
    {
        Stored s{};
        s.version = RECORD_VERSION;
        s.record = r;
        Preferences p;
        if (!p.begin(NAMESPACE, false)) return;
        p.putBytes(KEY, &s, sizeof s);
        p.end();
        cached = r;
        have = true;
    }

    void clear()
    {
        if (!have) return;
        Preferences p;
        if (p.begin(NAMESPACE, false))
        {
            p.remove(KEY);
            p.end();
        }
        have = false;
        cached = Record();
    }
}
