/**
 * JobRoutes
 * See JobRoutes.h. Refusal codes: jobName, noCard, jobNotFound, jobOpen,
 * jobWrite, jobAborted, jobRename, jobDelete, busy.
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#include "JobRoutes.h"

#include <WebServer.h>
#include <ArduinoJson.h>

#include "SdStore.h"
#include "JobRunner.h"
#include "Plotter.h"
#include "LogBuffer.h"

extern WebServer server;

namespace
{
    // A listing is for a person to choose from; a card with more jobs than
    // this gets the first ones and says it was cut.
    constexpr int LIST_MAX = 200;

    void refuse(int status, const char *code)
    {
        String out = "{\"error\":\"";
        out += code;
        out += "\"}";
        server.send(status, "application/json", out);
    }

    String pathOf(const String &name)
    {
        return String(SdStore::JOB_DIR) + "/" + name;
    }

    /** The job being drawn or checked - the one file that may not change under it. */
    bool inUse(const String &name)
    {
        const Plotter::State s = Plotter::state();
        return (s == Plotter::State::Plotting || s == Plotter::State::Paused) &&
               name == JobRunner::name();
    }

    void list()
    {
        JsonDocument doc;
        JsonObject card = doc["card"].to<JsonObject>();
        card["present"] = SdStore::present();
        card["total"] = (double)SdStore::totalBytes();
        card["used"] = (double)SdStore::usedBytes();

        JsonArray jobs = doc["jobs"].to<JsonArray>();
        if (SdStore::present())
        {
            fs::File dir = SdStore::fs().open(SdStore::JOB_DIR);
            int n = 0;
            for (fs::File f = dir.openNextFile(); f; f = dir.openNextFile())
            {
                const String name = f.name();
                // Skips folders, half-finished uploads and anything a PC left.
                if (f.isDirectory() || name.endsWith(".part") ||
                    !JobRunner::validName(name.c_str()))
                    continue;
                if (++n > LIST_MAX)
                {
                    doc["truncated"] = true;
                    break;
                }
                JsonObject j = jobs.add<JsonObject>();
                j["name"] = name;
                j["size"] = (uint32_t)f.size();
            }
        }
        String out;
        serializeJson(doc, out);
        server.send(200, "application/json", out);
    }

    void readJob()
    {
        const String name = server.arg("name");
        if (!JobRunner::validName(name.c_str())) return refuse(400, "jobName");
        if (!SdStore::present()) return refuse(409, "noCard");

        fs::File f = SdStore::fs().open(pathOf(name), "r");
        if (!f || f.isDirectory()) return refuse(404, "jobNotFound");
        if (server.hasArg("download"))
            server.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
        server.streamFile(f, "text/plain");
        f.close();
    }

    void removeJob()
    {
        JsonDocument doc;
        deserializeJson(doc, server.arg("plain"));
        const String name = doc["name"] | "";
        if (!JobRunner::validName(name.c_str())) return refuse(400, "jobName");
        if (!SdStore::present()) return refuse(409, "noCard");
        if (inUse(name)) return refuse(409, "busy");
        if (!SdStore::fs().exists(pathOf(name))) return refuse(404, "jobNotFound");
        if (!SdStore::fs().remove(pathOf(name))) return refuse(500, "jobDelete");
        debugI("Jobs: deleted %s", name.c_str());
        server.send(200, "application/json", "{\"ok\":true}");
    }

    // ------ upload: the /fs/upload arrangement, on the card ------

    fs::File upFile;
    String upName, upPart;
    const char *upError = nullptr;
    size_t upWritten = 0;

    void uploadData()
    {
        HTTPUpload &up = server.upload();
        if (up.status == UPLOAD_FILE_START)
        {
            upError = nullptr;
            upWritten = 0;
            upName = server.arg("name");
            if (!JobRunner::validName(upName.c_str())) { upError = "jobName"; return; }
            if (!SdStore::present()) { upError = "noCard"; return; }
            if (inUse(upName)) { upError = "busy"; return; }
            upPart = pathOf(upName) + ".part";
            upFile = SdStore::fs().open(upPart, "w");
            if (!upFile) { upError = "jobOpen"; return; }
            debugI("Jobs: receiving %s", upName.c_str());
        }
        else if (up.status == UPLOAD_FILE_WRITE)
        {
            if (upError || !upFile) return;
            if (upFile.write(up.buf, up.currentSize) != up.currentSize)
            {
                upError = "jobWrite";
                upFile.close();
                SdStore::fs().remove(upPart);
                return;
            }
            upWritten += up.currentSize;
        }
        else if (up.status == UPLOAD_FILE_END)
        {
            if (upFile) upFile.close();
        }
        else if (up.status == UPLOAD_FILE_ABORTED)
        {
            if (upFile) upFile.close();
            if (SdStore::present()) SdStore::fs().remove(upPart);
            if (!upError) upError = "jobAborted";
        }
    }

    void uploadDone()
    {
        if (upError)
        {
            const bool conflict = strcmp(upError, "busy") == 0 || strcmp(upError, "noCard") == 0;
            refuse(conflict ? 409 : 400, upError);
            upError = nullptr;
            return;
        }
        // The rename is the moment the new job exists; until here the old one
        // with the same name is untouched.
        SdStore::fs().remove(pathOf(upName));
        if (!SdStore::fs().rename(upPart, pathOf(upName)))
        {
            SdStore::fs().remove(upPart);
            return refuse(500, "jobRename");
        }
        debugI("Jobs: stored %s, %u bytes", upName.c_str(), (unsigned)upWritten);

        JsonDocument doc;
        doc["name"] = upName;
        doc["size"] = (uint32_t)upWritten;
        String out;
        serializeJson(doc, out);
        server.send(200, "application/json", out);
    }
}

namespace Jobs
{
    void begin()
    {
        server.on("/jobs", HTTP_GET, list);
        server.on("/jobs/read", HTTP_GET, readJob);
        server.on("/jobs/delete", HTTP_POST, removeJob);
        server.on("/jobs/upload", HTTP_POST, uploadDone, uploadData);
    }
}
