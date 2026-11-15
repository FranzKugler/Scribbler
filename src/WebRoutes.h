/**
 * WebRoutes
 * The device's HTTP interface in normal operation: every handler the
 * configuration SPA talks to, plus the state machine behind a network switch.
 *
 * That last one is not a route, but it belongs with them: POST /wifi only
 * records the request and answers immediately, because the response would
 * never leave the old network otherwise, and the switch itself then runs step
 * by step from loop(). Splitting the two halves apart would hide that.
 *
 * What is *not* here:
 *   - the /ota routes, which come with the module that serves them (OtaUpdate.h)
 *   - the /fs and /nvs routes (FileRoutes.h, NvsRoutes.h)
 *   - everything the setup portal answers, which is a different mode of the
 *     device entirely and registers nothing from this file (Portal.h)
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  31.8.2026
 * @updated  31.8.2026
 */
#ifndef WEBROUTES_H
#define WEBROUTES_H

#include <Arduino.h>

namespace Web
{
    /**
     * Hangs every handler on the server. Call from setup() before
     * server.begin(); the /ota, /fs and /nvs routes are registered separately
     * by their own modules.
     */
    void begin();

    /**
     * Drives the network switch requested through POST /wifi. Call from
     * loop() - the switch takes seconds and must not block a request.
     */
    void poll();

    /**
     * True while a switch is in flight. loop() leaves the connection alone
     * meanwhile, or its reconnect and the switch would fight over it.
     */
    bool switchingNetwork();

    /**
     * Streams one file out of LittleFS with a content type worked out from its
     * extension. False when there is no such file, and nothing has been sent.
     *
     * Public because the setup portal serves its own page and assets out of
     * the same filesystem, and two copies of the extension-to-MIME table would
     * eventually disagree about one of them.
     */
    bool serveFromFilesystem(String path);
}

#endif
