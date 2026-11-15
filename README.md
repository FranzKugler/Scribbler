# Scribbler

A wall-hanging two-belt pen plotter: the bot hangs from two belts running to two anchors on the
wall, two steppers wind them, a servo lifts the pen. This repository holds its firmware, its web
interface and its circuit board.

**Status:** the firmware and the web interface are complete up to drawing jobs from the SD card
through a look-ahead planner. None of it has run on hardware yet — the board is being designed.
The open items are in [PLAN.md](PLAN.md).

## What it does

- **Calibration in the browser**, step by step: anchor distance, belts wound in to their stop,
  out to the home position, loops hung on the anchors, pulley and pen calibration.
- **Jobs from a microSD card**, uploaded through the web interface. The format is Mural's, so a
  file made by [Mural's web UI](https://github.com/nikivanov/mural) plots unchanged. Each job is
  checked against the drawing area before the pen moves; progress, pause, resume and cancel.
- **Smooth motion**: a look-ahead planner (junction deviation, trapezoidal ramps) in Cartesian
  millimetres, sliced every 5 ms, each slice solved through Mural's rigid-body kinematic model
  (the bot tilts; belt stretch and the offset to the wall are corrected) and handed to both
  steppers in lockstep.
- **The house scaffolding**: own setup portal, German/English web UI, a password for the
  hardware, update, log and storage tabs; updates over the air from a GitHub release channel
  (stable or edge); a browser flashing page for a blank board.

## Hardware

Own board in [hardware/pcb](hardware/pcb) (KiCad): Seeed XIAO ESP32-S3 Plus, two TMC2240
stepper drivers on SPI, BMI270 IMU for the bot's tilt, SSD1306 OLED, microSD, USB-C PD input.
The pin map is [src/Board.h](src/Board.h). Mechanics — bot, pulleys, anchors — are Mural's.

## Getting it onto a board

- **Blank board:** the flashing page at
  [franzkugler.github.io/Scribbler](https://franzkugler.github.io/Scribbler/) — Chrome or Edge,
  a USB-C cable into the XIAO, one click.
- **From source:** PlatformIO (the pioarduino fork, pinned in `platformio.ini`) and Node.js.

  ```
  npm install && npm run build      # web interface -> data/
  pio run -t upload                 # firmware
  pio run -t uploadfs               # web interface
  pio test -e native                # host tests: kinematics, job format, planner
  npm run dev                       # web interface against a mock of the device, no hardware
  ```

After flashing, the board opens an access point named `Scribbler`; join it and the setup page
asks for the WiFi. Then it answers at `http://scribbler.local`.

## Releases

Every push to `main` builds an **edge** pre-release; a `v*` tag builds a **stable** release and
redeploys the flashing page. Devices follow the channel set in their Update tab.

## Documentation

[PLAN.md](PLAN.md) holds the scope, the hardware decisions and the phases;
[CLAUDE.md](CLAUDE.md) the module layout, the REST API and the conventions. The project language
is English; the web interface speaks German and English, following the browser.

## Credits and licence

The mechanics, the kinematic model and the job format follow
[nikivanov/mural](https://github.com/nikivanov/mural) by Nik Ivanov, licensed under
[CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/). Scribbler re-implements them;
it does not vendor Mural's code. Scribbler itself is under the same licence — see [LICENSE](LICENSE).
