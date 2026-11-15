/**
 * PlotRoutes
 * See PlotRoutes.h. The refusal codes are Plotter's (listed in Plotter.cpp);
 * badValue carries the field in errorDetail.
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#include "PlotRoutes.h"

#include <WebServer.h>
#include <ArduinoJson.h>

#include "Settings.h"
#include "Plotter.h"
#include "Pen.h"

extern WebServer server;
extern Settings settings;
void scheduleSettingsSave();

namespace
{
    /** Answers a Plotter result: ok, or the code with a status that fits it. */
    void answer(const char *code, const char *field = nullptr)
    {
        if (!code)
        {
            server.send(200, "application/json", "{\"ok\":true}");
            return;
        }
        JsonDocument doc;
        doc["error"] = code;
        if (field && strcmp(code, "badValue") == 0) doc["errorDetail"] = field;
        String out;
        serializeJson(doc, out);
        // 400 for a request that could never succeed, 409 for one that
        // cannot succeed *now* - the UI retries neither, but the difference
        // tells a reader of the log which side was wrong.
        server.send(strcmp(code, "badValue") == 0 ? 400 : 409, "application/json", out);
    }

    bool body(JsonDocument &doc)
    {
        return !deserializeJson(doc, server.arg("plain"));
    }

    void anchor()
    {
        JsonDocument doc;
        body(doc);
        if (!doc["mm"].is<float>()) return answer("badValue", "mm");
        answer(Plotter::setAnchorDistance(doc["mm"].as<double>()), "mm");
    }

    void retracted() { answer(Plotter::confirmRetracted()); }
    void extendHome() { answer(Plotter::extendToHome()); }
    void hung()       { answer(Plotter::confirmHung()); }
    void home()       { answer(Plotter::goHome()); }

    void stop()
    {
        Plotter::stop();
        answer(nullptr);
    }

    void pulleyExtend()
    {
        JsonDocument doc;
        body(doc);
        if (!doc["mm"].is<float>()) return answer("badValue", "mm");
        answer(Plotter::pulleyExtend(doc["mm"].as<double>()), "mm");
    }

    void pulleyMeasured()
    {
        JsonDocument doc;
        body(doc);
        if (!doc["measured"].is<float>()) return answer("badValue", "measured");
        answer(Plotter::pulleyMeasured(doc["measured"].as<double>()), "measured");
    }

    void jog()
    {
        JsonDocument doc;
        body(doc);

        if (doc["side"].is<const char *>())
        {
            const char *side = doc["side"];
            Driver::Side which;
            if (strcmp(side, "left") == 0) which = Driver::LEFT;
            else if (strcmp(side, "right") == 0) which = Driver::RIGHT;
            else return answer("badValue", "side");
            if (!doc["mm"].is<float>()) return answer("badValue", "mm");
            return answer(Plotter::jogBelt(which, doc["mm"].as<double>()), "mm");
        }

        const double dx = doc["dx"] | 0.0;
        const double dy = doc["dy"] | 0.0;
        answer(Plotter::jogXY(dx, dy), "dx");
    }

    void pen()
    {
        // The job owns the pen while it draws.
        if (Plotter::state() == Plotter::State::Plotting) return answer("wrongState");
        JsonDocument doc;
        body(doc);
        if (doc["angle"].is<int>())
        {
            const int angle = doc["angle"];
            if (angle < 0 || angle > 180) return answer("badValue", "angle");
            Pen::moveTo((uint8_t)angle);
            return answer(nullptr);
        }
        const char *state = doc["state"] | "";
        if (strcmp(state, "up") == 0) Pen::up();
        else if (strcmp(state, "down") == 0) Pen::down();
        else return answer("badValue", "state");
        answer(nullptr);
    }

    void startJob()
    {
        JsonDocument doc;
        body(doc);
        answer(Plotter::startJob(doc["name"] | ""));
    }

    void pauseJob()  { answer(Plotter::pauseJob()); }
    void resumeJob() { answer(Plotter::resumeJob()); }
    void stopJob()   { answer(Plotter::stopJob()); }

    void plotConfig()
    {
        JsonDocument doc;
        body(doc);
        if (!doc["drawSpeed"].is<float>()) return answer("badValue", "drawSpeed");
        answer(Plotter::setDrawSpeed(doc["drawSpeed"].as<double>()), "drawSpeed");
    }

    void penAngles()
    {
        JsonDocument doc;
        body(doc);
        uint8_t up = settings.getPenUp();
        uint8_t down = settings.getPenDown();
        if (!doc["up"].isNull())
        {
            const int v = doc["up"] | -1;
            if (v < 0 || v > 180) return answer("badValue", "up");
            up = (uint8_t)v;
        }
        if (!doc["down"].isNull())
        {
            const int v = doc["down"] | -1;
            if (v < 0 || v > 180) return answer("badValue", "down");
            down = (uint8_t)v;
        }
        settings.setPenUp(up);
        settings.setPenDown(down);
        scheduleSettingsSave();
        Pen::setAngles(up, down);
        answer(nullptr);
    }
}

namespace Plot
{
    void begin()
    {
        server.on("/calib/anchor", HTTP_POST, anchor);
        server.on("/calib/retracted", HTTP_POST, retracted);
        server.on("/calib/home", HTTP_POST, extendHome);
        server.on("/calib/hung", HTTP_POST, hung);
        server.on("/calib/pulley/extend", HTTP_POST, pulleyExtend);
        server.on("/calib/pulley", HTTP_POST, pulleyMeasured);
        server.on("/calib/pen", HTTP_POST, penAngles);
        server.on("/jog", HTTP_POST, jog);
        server.on("/home", HTTP_POST, home);
        server.on("/stop", HTTP_POST, stop);
        server.on("/pen", HTTP_POST, pen);
        server.on("/plot/start", HTTP_POST, startJob);
        server.on("/plot/pause", HTTP_POST, pauseJob);
        server.on("/plot/resume", HTTP_POST, resumeJob);
        server.on("/plot/stop", HTTP_POST, stopJob);
        server.on("/plot/config", HTTP_POST, plotConfig);
    }
}
