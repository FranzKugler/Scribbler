/**
 * HardwareRoutes
 * See HardwareRoutes.h.
 *
 * Refusals are codes, never sentences: badValue (with the field in
 * errorDetail), the expert lock's own codes from Expert::guard().
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#include "HardwareRoutes.h"

#include <WebServer.h>
#include <ArduinoJson.h>

#include "Settings.h"
#include "Expert.h"
#include "LogBuffer.h"
#include "Driver.h"
#include "Pen.h"
#include "Imu.h"
#include "Display.h"

extern WebServer server;
extern Settings settings;
void scheduleSettingsSave();

namespace
{
    constexpr float DEG = 180.0f / PI;

    void refuse(const char *field)
    {
        JsonDocument doc;
        doc["error"] = "badValue";
        doc["errorDetail"] = field;
        String out;
        serializeJson(doc, out);
        server.send(400, "application/json", out);
    }

    void sendOk()
    {
        server.send(200, "application/json", "{\"ok\":true}");
    }

    /** NaN is not JSON; a missing reading goes out as null. */
    void number(JsonObject obj, const char *key, float value)
    {
        if (isnan(value)) obj[key] = nullptr;
        else obj[key] = value;
    }

    void sendHardware()
    {
        if (!Expert::guard()) return;

        JsonDocument doc;
        doc["motorsEnabled"] = Driver::enabled();
        doc["motorCurrent"] = settings.getMotorCurrent();
        doc["maxCurrent"] = Driver::maxCurrentMa();
        doc["microsteps"] = settings.getMicrosteps();

        JsonArray drivers = doc["drivers"].to<JsonArray>();
        for (uint8_t i = 0; i < Driver::COUNT; i++)
        {
            const Driver::Status s = Driver::status((Driver::Side)i);
            JsonObject d = drivers.add<JsonObject>();
            d["side"] = i == Driver::LEFT ? "left" : "right";
            d["present"] = s.present;
            d["version"] = s.version;
            if (!s.present) continue;
            number(d, "temperature", s.temperatureC);
            number(d, "supply", s.supplyV);
            d["stallGuard"] = s.stallGuard;
            d["currentScale"] = s.currentScale;
            d["standstill"] = s.standstill;
            d["overtemperature"] = s.overtemperature;
            d["overtemperatureWarn"] = s.overtemperatureWarn;
            d["shortToGround"] = s.shortToGround;
            d["openLoad"] = s.openLoad;
            d["globalStatus"] = s.globalStatus;
            d["drvStatus"] = s.drvStatus;
        }

        JsonObject pen = doc["pen"].to<JsonObject>();
        pen["angle"] = Pen::angle();
        pen["target"] = Pen::target();
        pen["down"] = Pen::isDown();
        pen["settled"] = Pen::settled();
        pen["upAngle"] = settings.getPenUp();
        pen["downAngle"] = settings.getPenDown();

        const Imu::Reading r = Imu::reading();
        JsonObject imu = doc["imu"].to<JsonObject>();
        imu["present"] = r.present;
        if (r.present)
        {
            // Degrees on the wire: these are read by people, and the offset
            // is applied, so the numbers mean "how far from level".
            number(imu, "tilt", r.tilt * DEG);
            number(imu, "tiltAccel", r.tiltAccel * DEG);
            number(imu, "rate", r.rate * DEG);
            JsonArray accel = imu["accel"].to<JsonArray>();
            JsonArray gyro = imu["gyro"].to<JsonArray>();
            for (int i = 0; i < 3; i++)
            {
                accel.add(r.accel[i]);
                gyro.add(r.gyro[i]);
            }
            imu["samples"] = r.samples;
        }

        doc["display"]["present"] = Display::present();

        String out;
        serializeJson(doc, out);
        server.send(200, "application/json", out);
    }

    void updateMotors()
    {
        if (!Expert::guard()) return;
        JsonDocument doc;
        deserializeJson(doc, server.arg("plain"));
        if (!doc["enabled"].is<bool>()) return refuse("enabled");
        Driver::enable(doc["enabled"].as<bool>());
        sendOk();
    }

    void updatePen()
    {
        if (!Expert::guard()) return;
        JsonDocument doc;
        deserializeJson(doc, server.arg("plain"));

        if (doc["angle"].is<int>())
        {
            const int angle = doc["angle"];
            if (angle < 0 || angle > 180) return refuse("angle");
            Pen::moveTo((uint8_t)angle);
            return sendOk();
        }
        const char *state = doc["state"] | "";
        if (strcmp(state, "up") == 0) Pen::up();
        else if (strcmp(state, "down") == 0) Pen::down();
        else return refuse("state");
        sendOk();
    }

    void updateConfig()
    {
        if (!Expert::guard()) return;
        JsonDocument doc;
        deserializeJson(doc, server.arg("plain"));

        // Validate everything before changing anything: a request that is
        // half applied is harder to reason about than one refused whole.
        uint16_t current = settings.getMotorCurrent();
        uint16_t microsteps = settings.getMicrosteps();
        uint8_t up = settings.getPenUp();
        uint8_t down = settings.getPenDown();

        if (!doc["motorCurrent"].isNull())
        {
            const int v = doc["motorCurrent"] | -1;
            if (v < 100 || v > Driver::maxCurrentMa()) return refuse("motorCurrent");
            current = (uint16_t)v;
        }
        if (!doc["microsteps"].isNull())
        {
            const int v = doc["microsteps"] | -1;
            if (v < 1 || v > 256 || (v & (v - 1)) != 0) return refuse("microsteps");
            microsteps = (uint16_t)v;
        }
        if (!doc["penUp"].isNull())
        {
            const int v = doc["penUp"] | -1;
            if (v < 0 || v > 180) return refuse("penUp");
            up = (uint8_t)v;
        }
        if (!doc["penDown"].isNull())
        {
            const int v = doc["penDown"] | -1;
            if (v < 0 || v > 180) return refuse("penDown");
            down = (uint8_t)v;
        }

        const bool driverChanged = current != settings.getMotorCurrent() ||
                                   microsteps != settings.getMicrosteps();
        settings.setMotorCurrent(current);
        settings.setMicrosteps(microsteps);
        settings.setPenUp(up);
        settings.setPenDown(down);
        scheduleSettingsSave();

        if (driverChanged) Driver::configure(current, microsteps);
        Pen::setAngles(up, down);
        sendOk();
    }
}

namespace Hw
{
    void begin()
    {
        server.on("/hardware", HTTP_GET, sendHardware);
        server.on("/hardware/motors", HTTP_POST, updateMotors);
        server.on("/hardware/pen", HTTP_POST, updatePen);
        server.on("/hardware/config", HTTP_POST, updateConfig);
    }
}
