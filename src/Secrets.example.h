/**
 * Secrets.example
 * Copy to src/Secrets.h and fill in. The real file is not in the repository -
 * see .gitignore - so a public release image has no espota password, which is
 * the right default for a public image.
 *
 * Only espota (flashing from PlatformIO over the network) reads this. The
 * web UI's own update path is guarded by expert mode instead; see Expert.h.
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  31.8.2026
 * @updated  31.8.2026
 */
#ifndef SECRETS_H
#define SECRETS_H

#define OTA_PASSWORD "change-me"

#endif
