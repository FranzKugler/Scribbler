# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Status

**Planning.** No code exists yet. [PLAN.md](PLAN.md) is the current source of truth for scope,
architecture and phases; this file describes the target and the rules, and each section is to be
replaced by the real description once the code behind it exists. Do not describe here what has not
been built.

## Project overview

Scribbler is firmware and web UI for a wall-hanging two-belt pen plotter (polargraph). The bot hangs
from two GT2 belts running to two anchors on the wall; two NEMA17 steppers wind the belts, a servo
lifts the pen.

It is a fusion of two things:

- **Mechanics, kinematic model and job format** after [nikivanov/mural](https://github.com/nikivanov/mural)
  (CC BY-NC 4.0 — attribution required, non-commercial). Mural is re-implemented, not vendored.
- **House scaffolding** from DoorOpener / QlockThreeW32 — use the `xiao-esp32s3-project` skill and
  copy the modules from `w:/home/franz/Workspace/DoorOpener` as it describes: own setup portal,
  Svelte 5 UI, expert lock, log ring + debug tab, storage explorer (LittleFS/NVS, here also SD),
  OTA via GitHub release channel (stable/edge), first-flash page, `server.js` mock.
- **Stepper stack** as in `../Rotator`: FastAccelStepper + TMC2209 over UART.

Planned layout:

```
src/        firmware (PlatformIO, Arduino core 3.3.x via the pioarduino fork)
test/       host unit tests for Kinematics (pio test -e native)
web/        Svelte 5 + Vite: SPA (index.html) and setup portal (portal.html), one build
web/src/worker/   SVG → job pipeline, runs in a web worker
data/       build output of web/, flashed as LittleFS (gitignored)
docs/       first-flash page (GitHub Pages)
scripts/    version stamping, manifests, icons
server.js   mock of the device's REST API
```

## Hardware target

**Undecided** — see PLAN.md §0. Development starts on a breadboard rig: **XIAO ESP32-S3 Plus**
(16 MB, house standard unchanged) with two UART-capable TMC2209 StepSticks on one single-wire
UART bus (addresses 0 = left, 1 = right), servo, microSD and a 0.91" SSD1306 OLED on I2C. The
likely final target is an own board with the same XIAO and the TMC2209 (or TMC2226) on the PCB;
the rig is wired so that only `src/Board.h` changes when it arrives. Record the rig's pin map
here once it is wired.

The XIAO Plus's back-pad GPIO mapping (D11–D19) is inconsistent across Seeed's wiki pages — use
the schematic. Keep STEP/DIR/ENN off strapping pins (GPIO0, 3, 45, 46).

A BMI270 IMU (accelerometer + gyro, fused) on the I2C bus measures the bot's tilt γ (validation and calibration of the
kinematics, sway, "off the wall"). Shaft encoders are deliberately not part of the concept — the
reasoning is in PLAN.md §0.3. The TMC driver (TMC2209 via UART or TMC2240 via SPI/UART) is open and
sits behind `Driver.*`; nothing outside that module talks to a driver register.

The OLED is always optional at runtime: missing display = no-op, never a boot failure. In portal
mode it shows the AP name and `192.168.4.1`.

If Mural's own board is used instead (Bart Dring's "TMC2209 Pen/Laser"): it has a **classic
ESP32 (WROOM)**, not an S3 — confirm with `esptool chip-id` / `flash-id`. The deviations from the
house standard (partition table, no `usb-reset`, no `BOARD_HAS_PSRAM`, bootloader offset `0x1000`
on the first-flash page) are in PLAN.md §0.1; each must carry its reason as a comment where it is
set. Its pin map as Mural uses it — **verify against `firmware/tmc2209_Pen_Laser.h` in
bdring/TMC2209_Pen_Laser** before trusting it:

| Function | GPIO |
|---|---|
| Left STEP / DIR / DIAG | 14 / 12 / 36 |
| Right STEP / DIR / DIAG | 25 / 26 / 39 |
| Motor enable (LOW = on) | 13 |
| TMC UART RX / TX (shared, addr 0 = left, 1 = right) | 16 / 17 |
| Pen servo | 21 |
| SD (SPI) SCK / MISO / MOSI / CS | 18 / 19 / 23 / 5 |

All pins live in `src/Board.h` and nowhere else.

## Coordinates and kinematics

- Frame: origin at the **left anchor**, +x towards the right anchor, **+y down**. `d` = anchor distance.
- Image (job) coordinates: origin at the top-left of the drawing area, which starts 0.2·d right of
  the left anchor and 0.25·d below the anchor line; width 0.6·d. Units: mm.
- `Kinematics` maps image (x, y) → belt lengths using Mural's rigid-body model: the bot tilts by γ;
  γ comes from a torque equilibrium of the belt forces and the bot's weight, iterated and
  warm-started from the previous point; each leg gets the out-of-plane offset to the wall and a
  belt-elongation correction `L / (1 + k·F)`. Details and constants: PLAN.md §1 and Mural's
  `KinematicModel.md`.
- `Kinematics` is pure C++ with no Arduino includes, so it builds and is tested on the host. Its
  tests compare against a reference table generated from Mural's algorithm; keep that table in
  `test/` and regenerate it only on purpose.
- The planner works in Cartesian mm; belts appear only at the last step (slice → `Kinematics` →
  step targets). Do not let belt lengths leak into the planner.

## Motion

- Step pulses come from FastAccelStepper, fed by a motion task pinned to core 1. Nothing on the
  web/loop side may block that task or call the stepper API directly — go through the segment queue.
- Pen moves are segments in the same queue, so ordering between pen and motion is never a race.
- The device states and the actions each state allows are in one table (`PlotterState`); every
  refusal is a code (`notHomed`, `busy`, `noCard`, …), never a sentence.

## Job format (SD card, `/jobs/`)

Mural-compatible, line-oriented, streamed — never loaded into RAM:

```
d<total path length in mm>
h<drawing height in mm>
<x> <y>        move (pen state as last set)
p1 / p0        pen down / up
```

Keep it compatible: a file made by Mural's web UI must plot unchanged. Extensions go in new line
types that older parsers skip, not in changed semantics.

## Conventions (house standard)

- **Project language is English** — code, comments, README, this file, commit messages — even when
  the conversation is German. Only the web UI is translated (de reference locale, en follows with
  the same keys in the same order).
- Comments explain **why**. Each file starts with a header block (`@mc`, `@author`, `@version`,
  `@created`, `@updated`) saying what the module is and what it is **not**.
- Firmware: braces on their own line, four spaces, `namespace Foo { ... }` for module interfaces.
- Web: Prettier-shaped — two spaces, single quotes, semicolons.
- Errors from the firmware: `{"error": "<code>", "errorDetail": "<untranslated>"}`; text comes from
  `err_<code>` in both locales.
- Changing the API means four places: firmware handler, `server.js`, `web/src/lib/api.js`,
  `API_ROUTES` in `vite.config.js`.
- Adding a setting means three places in `Settings` (accessors, `loadSettings()`, `fillDocument()`).
- Portal mode registers **only** the portal. `WiFi.persistent(false)` before any attempt. TX power
  set from the WiFi event handler.
- Generated assets are committed; edit the generator, not the output.

## Commands

`pio` is not on PATH; use `%USERPROFILE%\.platformio\penv\Scripts\pio.exe`. To be filled in once
`platformio.ini` exists — expected:

```
npm install && npm run build      # web/ -> data/
npm run dev / npm run mock        # UI against the mock, no hardware
pio run / -t upload / -t uploadfs
pio test -e native                # Kinematics on the host
```

## Licence and attribution

Kinematic model, mechanics and job format follow nikivanov/mural (CC BY-NC 4.0). Keep the
attribution in the README and in the header of `Kinematics.*`. Any code taken over from Mural's web
worker must say so in its header.
