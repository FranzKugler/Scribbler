/**
 * Driver
 * The two TMC2240 stepper drivers, over SPI: configuration, enable, status.
 *
 * The only module that knows a register address. Everything above it speaks in
 * milliamperes, microsteps and a status struct, so the breadboard rig, the own
 * board and a later change of driver differ in this file alone (PLAN.md
 * section 0.4).
 *
 * What it is not: the step generator. STEP and DIR are FastAccelStepper's, in
 * Motion; this module only makes the driver ready to follow them and reports
 * how it is doing.
 *
 * Register layout after the TMC2240 datasheet (Analog Devices, rev. 1.x).
 * Checked against the datasheet, not yet against a chip - the first thing to
 * verify on hardware is the VERSION field of IOIN (0x40) for both drivers,
 * which the status reports as `version`.
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#ifndef DRIVER_H
#define DRIVER_H

#include <Arduino.h>

namespace Driver
{
    enum Side : uint8_t
    {
        LEFT = 0,
        RIGHT = 1
    };
    constexpr uint8_t COUNT = 2;

    /** What a driver reports about itself. Read fresh by status(). */
    struct Status
    {
        bool present = false;        // answered with the TMC2240's VERSION
        uint8_t version = 0;         // IOIN.VERSION; 0x40 for a TMC2240
        uint32_t drvStatus = 0;      // DRV_STATUS, raw - the UI decodes the flags
        uint16_t stallGuard = 0;     // DRV_STATUS.SG_RESULT
        uint8_t currentScale = 0;    // DRV_STATUS.CS_ACTUAL, 0..31
        bool standstill = false;
        bool overtemperature = false;    // ot: the driver has switched off
        bool overtemperatureWarn = false;
        bool shortToGround = false;      // s2ga | s2gb
        bool openLoad = false;           // ola | olb - only meaningful while moving
        float temperatureC = NAN;        // from ADC_TEMP
        float supplyV = NAN;             // from ADC_VSUPPLY
        uint8_t globalStatus = 0;        // GSTAT: reset, drv_err, uv_cp
    };

    /**
     * Sets up the bus and the enable pin, with the motors off, and configures
     * both drivers. Call early in setup(): ENN has a pull-up on the board, so
     * the motors are off until here anyway, but the drivers' own defaults are
     * not ours.
     */
    void begin(uint16_t runCurrentMa, uint16_t microsteps);

    /**
     * Run current (RMS, mA) and microsteps for both drivers. Takes effect at
     * once; the hold current is half the run current.
     */
    void configure(uint16_t runCurrentMa, uint16_t microsteps);

    /** Motor outputs on or off - both at once, they share the ENN line. */
    void enable(bool on);
    bool enabled();

    Status status(Side side);

    /** The largest run current the board's R_REF and current range allow. */
    uint16_t maxCurrentMa();
}

#endif
