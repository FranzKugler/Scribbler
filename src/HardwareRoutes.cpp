/**
 * HardwareRoutes
 * See HardwareRoutes.h.
 *
 * Refusals are codes, never sentences: badValue (with the field in
 * errorDetail), busy (a belt is moving and the request needs standstill),
 * motorsOff (a jog with the drivers disabled would count steps the motor
 * never makes), and the expert lock's own codes from Expert::guard().
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
#include "Motion.h"
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

    void refuseCode(int status, const char *code)
    {
        String out = "{\"error\":\"";
        out += code;
        out += "\"}";
        server.send(status, "application/json", out);
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
        doc["pulleyDiameter"] = settings.getPulleyDiameter();

        JsonObject motion = doc["motion"].to<JsonObject>();
        motion["ready"] = Motion::ready();
        motion["moving"] = Motion::moving();
        motion["stepsPerMm"] = Motion::stepsPerMm();
        JsonArray belts = motion["belts"].to<JsonArray>();
        for (uint8_t i = 0; i < Driver::COUNT; i++)
        {
            JsonObject b = belts.add<JsonObject>();
            b["side"] = i == Driver::LEFT ? "left" : "right";
            b["steps"] = Motion::steps((Driver::Side)i);
            // Relative to wherever the belt was at boot - there is no homing
            // yet, so this is distance travelled, not belt length.
            b["mm"] = Motion::millimetres((Driver::Side)i);
            b["moving"] = Motion::moving((Driver::Side)i);
        }

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
        const bool on = doc["enabled"].as<bool>();
        // Cutting the outputs under a moving belt: the step generator would
        // go on counting steps the motor no longer makes.
        if (!on && Motion::moving()) return refuseCode(409, "busy");
        Driver::enable(on);
        sendOk();
    }

    void jog()
    {
        if (!Expert::guard()) return;
        JsonDocument doc;
        deserializeJson(doc, server.arg("plain"));

        const char *side = doc["side"] | "";
        Driver::Side which;
        if (strcmp(side, "left") == 0) which = Driver::LEFT;
        else if (strcmp(side, "right") == 0) which = Driver::RIGHT;
        else return refuse("side");

        // A belt is at most a few metres; anything beyond is a typo.
        if (!doc["mm"].is<float>()) return refuse("mm");
        const double mm = doc["mm"].as<double>();
        if (!(mm >= -1000.0 && mm <= 1000.0)) return refuse("mm");

        if (!Driver::enabled()) return refuseCode(409, "motorsOff");
        if (!Motion::jog(which, mm)) return refuseCode(409, "busy");
        sendOk();
    }

    void stopMotion()
    {
        if (!Expert::guard()) return;
        Motion::stop();
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
        double pulley = settings.getPulleyDiameter();
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
        if (!doc["pulleyDiameter"].isNull())
        {
            const double v = doc["pulleyDiameter"] | -1.0;
            if (!(v >= 5.0 && v <= 40.0)) return refuse("pulleyDiameter");
            pulley = v;
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
        const bool scaleChanged = microsteps != settings.getMicrosteps() ||
                                  pulley != settings.getPulleyDiameter();
        // The step positions are rescaled with the scale, which needs both
        // belts standing still - checked before anything is changed.
        if (scaleChanged && Motion::moving()) return refuseCode(409, "busy");

        settings.setMotorCurrent(current);
        settings.setMicrosteps(microsteps);
        settings.setPulleyDiameter(pulley);
        settings.setPenUp(up);
        settings.setPenDown(down);
        scheduleSettingsSave();

        if (driverChanged) Driver::configure(current, microsteps);
        if (scaleChanged) Motion::setScale(microsteps, pulley);
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
        server.on("/hardware/jog", HTTP_POST, jog);
        server.on("/hardware/stop", HTTP_POST, stopMotion);
        server.on("/hardware/pen", HTTP_POST, updatePen);
        server.on("/hardware/config", HTTP_POST, updateConfig);
    }
}
