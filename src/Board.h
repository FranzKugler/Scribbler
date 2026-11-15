/**
 * Board
 * The pin map and the fixed facts of the board - the only file that knows
 * which GPIO does what.
 *
 * Taken from the netlist of hardware/pcb/Scribbler_pretty.kicad_sch (the
 * finished schematic, 15.11.2026; the GPIO map is identical to the earlier
 * Scribbler.kicad_sch), not from the XIAO wiki: Seeed's pages disagree about
 * the Plus's back pads D11-D19, and the schematic symbol is what the board
 * will actually be routed from. If the schematic changes, regenerate the netlist and compare
 * - nothing checks this file against it automatically.
 *
 * It is not configurable at runtime. A setting for a pin number is a way to
 * drive a motor coil from the SD card's clock line by mistake.
 *
 * The breadboard rig (PLAN.md section 0) is wired to these same pins, so the
 * firmware does not need a second map. TMC2240 breakout boards in SPI mode
 * stand in for the on-board drivers.
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

namespace Board
{
    // ------ stepper drivers: 2 x TMC2240, SPI mode ------
    //
    // UART_EN is tied to GND on both, which selects SPI; the AD0..AD2 pins
    // double as SDI/SCK/CSN and carry no address. CLK is grounded, so the
    // drivers run on their internal ~12.5 MHz clock.

    constexpr uint8_t SPI_SCK  = 7;   // D8
    constexpr uint8_t SPI_MISO = 8;   // D9
    constexpr uint8_t SPI_MOSI = 9;   // D10

    constexpr uint8_t CS_LEFT  = 1;   // D0
    constexpr uint8_t CS_RIGHT = 2;   // D1

    constexpr uint8_t STEP_LEFT  = 38;  // D11
    constexpr uint8_t DIR_LEFT   = 39;  // D12
    constexpr uint8_t STEP_RIGHT = 40;  // D13
    constexpr uint8_t DIR_RIGHT  = 41;  // D14

    // Shared by both drivers, active low, with a 10k pull-up on the board: the
    // motors stay off while the ESP32 boots and this pin still floats. Drive it
    // low only once the drivers have been configured. (The net is spelled
    // DVR_ENN in the schematic.)
    constexpr uint8_t DRV_ENN = 42;    // D15

    // DIAG0 of each driver. GPIO3 is a strapping pin (JTAG source select),
    // which only matters if the eFuse asks for it - harmless as an input, and
    // the rule in the board's design rules covers STEP/DIR/ENN, not this.
    constexpr uint8_t DIAG_LEFT  = 3;  // D2
    constexpr uint8_t DIAG_RIGHT = 4;  // D3

    // IREF to GND through 12k 1 % on both drivers. The full-scale current is
    // K_IFS / R_REF, with K_IFS set by DRV_CONF.CURRENT_RANGE - see Driver.cpp.
    constexpr float DRIVER_RREF_KOHM = 12.0f;

    // No shaft encoders: the finished schematic dropped the optional J7/J8 and
    // ties each driver's ENCA/ENCB/ENCN to GND. PLAN.md section 0.3 says why
    // they were never more than an option.

    // ------ pen ------

    // Servo signal through 330 R to J4; the servo runs on the board's 5 V.
    constexpr uint8_t SERVO_PWM = 10;  // D16

    // ------ I2C: SSD1306 display (J5) and BMI270 IMU ------

    constexpr uint8_t I2C_SDA = 5;     // D4
    constexpr uint8_t I2C_SCL = 6;     // D5

    constexpr uint8_t DISPLAY_ADDR = 0x3C;
    constexpr uint8_t IMU_ADDR = 0x68;  // SDO/SA0 tied to GND

    // ------ microSD, SD_MMC 1-bit ------
    //
    // On its own pins, never on the drivers' SPI bus: the card wants a slow
    // clock during init, some cards do not release MISO cleanly, and the job
    // runner reads continuously while the motion side talks to the drivers.
    // CMD and DAT0..DAT3 have 10k pull-ups on the board; DAT1..DAT3 are unused.

    constexpr uint8_t SD_CLK  = 13;    // D17
    constexpr uint8_t SD_CMD  = 12;    // D18
    constexpr uint8_t SD_DAT0 = 11;    // D19 (net SD_DATA0)

    // GPIO43/44 (D6/D7, the UART0 pins) are deliberately left free: a serial
    // console that does not depend on USB is worth two pins on a device that
    // hangs on a wall.
}

#endif
