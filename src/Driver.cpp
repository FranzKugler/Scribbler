/**
 * Driver
 * TMC2240 over SPI; see Driver.h for what and why.
 *
 * The SPI datagram is 40 bits, MSB first, SPI mode 3: one address byte (bit 7
 * set for a write) and 32 data bits. The answer to a datagram arrives during
 * the *next* one - the chip shifts out the data of the previous read address -
 * so a read is two transfers, and the first answer is discarded.
 *
 * The bus is shared with nothing but the second driver, and each driver has
 * its own CS. Both are only ever touched from one task (the main loop today,
 * the motion task later); there is no lock, and adding a second caller means
 * adding one.
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#include "Driver.h"

#include <SPI.h>

#include "Board.h"
#include "LogBuffer.h"

namespace
{
    // ------ registers (TMC2240 datasheet, register map) ------

    constexpr uint8_t REG_GCONF        = 0x00;
    constexpr uint8_t REG_GSTAT        = 0x01;
    constexpr uint8_t REG_IOIN         = 0x04;
    constexpr uint8_t REG_DRV_CONF     = 0x0A;
    constexpr uint8_t REG_GLOBALSCALER = 0x0B;
    constexpr uint8_t REG_IHOLD_IRUN   = 0x10;
    constexpr uint8_t REG_TPOWERDOWN   = 0x11;
    constexpr uint8_t REG_TPWMTHRS     = 0x13;
    constexpr uint8_t REG_ADC_VSUPPLY  = 0x50;
    constexpr uint8_t REG_ADC_TEMP     = 0x51;
    constexpr uint8_t REG_CHOPCONF     = 0x6C;
    constexpr uint8_t REG_DRV_STATUS   = 0x6F;
    constexpr uint8_t REG_PWMCONF      = 0x70;

    constexpr uint8_t WRITE = 0x80;

    constexpr uint8_t TMC2240_VERSION = 0x40;

    // Slow enough to be forgiving of a breadboard, fast enough that a status
    // read of both drivers is a few tens of microseconds. The internal clock
    // runs at ~12.5 MHz, and the datasheet allows SCK up to half of it.
    constexpr uint32_t SPI_HZ = 2000000;

    // ------ current ------
    //
    // Full-scale (peak) current is K_IFS / R_REF. K_IFS depends on
    // DRV_CONF.CURRENT_RANGE: 11.75 / 24 / 36 A*kOhm for the 1 / 2 / 3 A
    // ranges. With the board's 12k: 0.98 / 2.0 / 3.0 A peak.
    //
    // The 2 A range: Mural's pancake motors are rated 1 A and run at ~0.7 A
    // RMS, which is 1 A peak - half the range, where GLOBALSCALER (32..256)
    // still has resolution left both ways. The 3 A range would put the same
    // current at a third of full scale for no gain.
    constexpr uint8_t CURRENT_RANGE = 1;        // 2 A
    constexpr float K_IFS = 24.0f;              // A * kOhm for that range

    float fullScalePeakA()
    {
        return K_IFS / Board::DRIVER_RREF_KOHM;
    }

    // ------ state ------

    const uint8_t CS[Driver::COUNT] = {Board::CS_LEFT, Board::CS_RIGHT};
    SPIClass bus(FSPI);
    bool outputsOn = false;

    // ------ transport ------

    uint32_t transfer(Driver::Side side, uint8_t address, uint32_t data)
    {
        bus.beginTransaction(SPISettings(SPI_HZ, MSBFIRST, SPI_MODE3));
        digitalWrite(CS[side], LOW);
        bus.transfer(address);   // the status byte comes back here; unused
        uint32_t answer = 0;
        for (int shift = 24; shift >= 0; shift -= 8)
        {
            answer |= (uint32_t)bus.transfer((uint8_t)(data >> shift)) << shift;
        }
        digitalWrite(CS[side], HIGH);
        bus.endTransaction();
        return answer;
    }

    void writeRegister(Driver::Side side, uint8_t reg, uint32_t value)
    {
        transfer(side, reg | WRITE, value);
    }

    uint32_t readRegister(Driver::Side side, uint8_t reg)
    {
        transfer(side, reg, 0);          // asks; the answer is the previous read's
        return transfer(side, reg, 0);   // ... and this one carries ours
    }

    /** MRES field value for a microstep count: 256 -> 0, 128 -> 1 ... 1 -> 8. */
    uint8_t mres(uint16_t microsteps)
    {
        uint8_t field = 0;
        for (uint16_t m = 256; m > microsteps && field < 8; m >>= 1) field++;
        return field;
    }

    void configureOne(Driver::Side side, uint16_t runCurrentMa, uint16_t microsteps)
    {
        // Clear the reset flag, so a later GSTAT.reset means the driver lost
        // power on its own - the first thing to look at after a crash that
        // shook the board.
        writeRegister(side, REG_GSTAT, 0x07);

        // DRV_CONF: current range; SLOPE_CONTROL left at the default.
        writeRegister(side, REG_DRV_CONF, CURRENT_RANGE);

        // Current. Run current is set entirely through GLOBALSCALER with IRUN
        // at its maximum, so the scale has its full 8-bit resolution; IHOLD at
        // half is the usual compromise between a bot that sags and a motor
        // that cooks while the pen is up.
        const float peakA = runCurrentMa / 1000.0f * 1.41421356f;
        int scaler = (int)lroundf(peakA / fullScalePeakA() * 256.0f);
        if (scaler < 32) scaler = 32;     // values below 32 are not allowed
        if (scaler > 255) scaler = 0;     // 0 means 256, full scale
        writeRegister(side, REG_GLOBALSCALER, (uint32_t)scaler);

        constexpr uint32_t IRUN = 31, IHOLD = 16, IHOLDDELAY = 6, IRUNDELAY = 4;
        writeRegister(side, REG_IHOLD_IRUN,
                      IHOLD | (IRUN << 8) | (IHOLDDELAY << 16) | (IRUNDELAY << 24));

        // ~2 s to standstill current once the steps stop.
        writeRegister(side, REG_TPOWERDOWN, 10);

        // CHOPCONF: TOFF 3, HSTRT 4 (field value), HEND 1, TBL 2, interpolation
        // to 256 on, MRES from the microsteps. The SpreadCycle values are the
        // datasheet's starting point; StealthChop is what runs at plotting
        // speeds, so they matter little until a fast travel move.
        const uint32_t chopconf = 3u | (4u << 4) | (1u << 7) | (2u << 15) |
                                  ((uint32_t)mres(microsteps) << 24) | (1u << 28);
        writeRegister(side, REG_CHOPCONF, chopconf);

        // StealthChop everywhere (TPWMTHRS 0 = no switch-over), automatic
        // PWM tuning. Quiet, and a plotter never needs the torque at speed
        // that SpreadCycle would add.
        writeRegister(side, REG_PWMCONF, 0xC40C001Eu);   // datasheet reset value, pwm_autoscale/grad on
        writeRegister(side, REG_TPWMTHRS, 0);
        writeRegister(side, REG_GCONF, 1u << 2);         // en_pwm_mode
    }
}

namespace Driver
{
    void begin(uint16_t runCurrentMa, uint16_t microsteps)
    {
        // Outputs off first, whatever the pull-up has done so far.
        pinMode(Board::DRV_ENN, OUTPUT);
        digitalWrite(Board::DRV_ENN, HIGH);
        outputsOn = false;

        for (uint8_t i = 0; i < COUNT; i++)
        {
            pinMode(CS[i], OUTPUT);
            digitalWrite(CS[i], HIGH);
        }
        pinMode(Board::DIAG_LEFT, INPUT);
        pinMode(Board::DIAG_RIGHT, INPUT);

        bus.begin(Board::SPI_SCK, Board::SPI_MISO, Board::SPI_MOSI, -1);

        configure(runCurrentMa, microsteps);

        for (uint8_t i = 0; i < COUNT; i++)
        {
            const Status s = status((Side)i);
            if (s.present)
            {
                debugI("Driver %s: TMC2240 v%02X, %.1f V, %.0f C",
                       i == LEFT ? "left" : "right", s.version, s.supplyV, s.temperatureC);
            }
            else
            {
                // Not fatal: the web UI must still come up to say so.
                debugE("Driver %s: no TMC2240 on the bus (IOIN version %02X)",
                       i == LEFT ? "left" : "right", s.version);
            }
        }
    }

    void configure(uint16_t runCurrentMa, uint16_t microsteps)
    {
        for (uint8_t i = 0; i < COUNT; i++) configureOne((Side)i, runCurrentMa, microsteps);
        debugI("Drivers: %u mA RMS, 1/%u steps (full scale %.2f A peak)",
               runCurrentMa, microsteps, fullScalePeakA());
    }

    void enable(bool on)
    {
        digitalWrite(Board::DRV_ENN, on ? LOW : HIGH);
        if (on != outputsOn) debugI("Motors %s", on ? "on" : "off");
        outputsOn = on;
    }

    bool enabled()
    {
        return outputsOn;
    }

    Status status(Side side)
    {
        Status s;
        const uint32_t ioin = readRegister(side, REG_IOIN);
        s.version = (uint8_t)(ioin >> 24);
        s.present = s.version == TMC2240_VERSION;
        if (!s.present) return s;

        s.globalStatus = (uint8_t)(readRegister(side, REG_GSTAT) & 0x07);

        const uint32_t d = readRegister(side, REG_DRV_STATUS);
        s.drvStatus = d;
        s.stallGuard = (uint16_t)(d & 0x3FF);
        s.currentScale = (uint8_t)((d >> 16) & 0x1F);
        s.overtemperature = d & (1u << 25);
        s.overtemperatureWarn = d & (1u << 26);
        s.shortToGround = d & ((1u << 27) | (1u << 28));
        s.openLoad = d & ((1u << 29) | (1u << 30));
        s.standstill = d & (1u << 31);

        // ADC scaling from the datasheet: VSUPPLY in 9.732 mV steps,
        // temperature (ADC_TEMP - 2038) / 7.7 degrees C.
        const uint32_t vs = readRegister(side, REG_ADC_VSUPPLY) & 0x1FFF;
        s.supplyV = vs * 0.009732f;
        const uint32_t temp = readRegister(side, REG_ADC_TEMP) & 0x1FFF;
        s.temperatureC = ((float)temp - 2038.0f) / 7.7f;
        return s;
    }

    uint16_t maxCurrentMa()
    {
        return (uint16_t)(fullScalePeakA() / 1.41421356f * 1000.0f);
    }
}
