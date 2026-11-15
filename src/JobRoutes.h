/**
 * JobRoutes
 * The job files on the SD card, over HTTP.
 *
 *   GET  /jobs                  {card: {present, total, used}, jobs: [{name, size}]}
 *   POST /jobs/upload?name=     multipart, one file part, streamed to the card
 *   GET  /jobs/read?name=       the file; &download=1 adds the attachment header
 *   POST /jobs/delete           {name}
 *
 * Open, like the plotter's controls (PlotRoutes.h says why). The upload
 * streams the way /fs/upload does - into a .part file, renamed into place at
 * the end - so a job of any size never sits in the heap, and an aborted
 * upload never replaces a good file with half of one. The job that is being
 * drawn can be neither replaced nor deleted (busy).
 *
 * Names are JobRunner::validName(): no paths, so nothing outside /jobs is
 * reachable from here.
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#ifndef JOBROUTES_H
#define JOBROUTES_H

namespace Jobs
{
    /** Hangs the handlers on the server. Call before server.begin(). */
    void begin();
}

#endif
