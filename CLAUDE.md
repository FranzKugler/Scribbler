# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Status

**Phases 1–5 built (PLAN.md section 3).** What exists: the house scaffolding copied from
DoorOpener and renamed (portal, SPA, OTA, expert lock, log ring, storage explorer, mock server,
release workflow, first-flash page), `src/Board.h` from the schematic's netlist,
`src/Kinematics.*` with host tests, the hardware modules with a bring-up tab, and the
calibration flow (state machine, Setup tab), and jobs from the SD card in Mural's format (Plot
tab). Drawing is still Mural's 1 mm stop-and-go - no planner yet. None of it has run on hardware. [PLAN.md](PLAN.md) is the source of truth for everything not
built; this file describes what is, plus the rules. Do not describe here what has not been built.

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
- **Stepper stack**: FastAccelStepper for the step pulses, as in `../Rotator`; the drivers are
  TMC2240 over SPI (Rotator has TMC2209 over UART, so only the step side carries over).

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
hardware/pcb/     KiCad project for the own board (Scribbler.kicad_pro)
```

**KiCad files are never edited as text** (`*.kicad_sch`, `*.kicad_pcb`, `*.kicad_pro`, libraries,
lib tables) — every change goes through the Konnect MCP tools, see the `konnect` skill. The
board's design rules live in [hardware/pcb/.konnect/project.json](hardware/pcb/.konnect/project.json)
(added with `add_design_rule`, read with `list_design_rules`) and restate PLAN.md §0.2–0.4; change
both together.

## Hardware target

The own board in [hardware/pcb](hardware/pcb) — `Scribbler_pretty.kicad_sch` is the finished
schematic: **XIAO ESP32-S3 Plus** (16 MB, house standard
unchanged), **2 × TMC2240 in SPI mode** on a shared bus with one CS each, BMI270 IMU and SSD1306
display on I2C, microSD in SD_MMC 1-bit mode, servo on 5 V, USB-C PD input (CH224A). The pin map
is [src/Board.h](src/Board.h), taken from the schematic's netlist — the XIAO Plus's back-pad
mapping (D11–D19) is inconsistent across Seeed's wiki pages, so the schematic symbol is the
authority. When the schematic changes, export the netlist (`kicad-cli sch export netlist`) and
compare it with `Board.h`; nothing does that automatically. Keep STEP/DIR/ENN off strapping pins
(GPIO0, 3, 45, 46). GPIO43/44 stay free for a UART console.

No shaft encoders: the finished schematic dropped the optional J7/J8 and grounds the TMC2240's
ENCA/B/N inputs — PLAN.md §0.3. The
BMI270 measures the bot's tilt γ (validation and calibration of the kinematics, sway, "off the
wall"). Driver registers are touched only by `Driver.*` (planned).

A breadboard rig with TMC2240 breakouts wired to the same pins needs no second map.

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

## Firmware modules (the plotter's part)

The house modules (`Portal`, `WebRoutes`, `OtaUpdate`, `Expert`, `LogBuffer`, `FileRoutes`,
`NvsRoutes`, `Settings`) are DoorOpener's; their headers say what they are. The plotter's:

| File | Owns | Is not |
|---|---|---|
| [src/Board.h](src/Board.h) | every pin, from the netlist | configurable at runtime |
| [src/Kinematics.cpp](src/Kinematics.cpp) | Mural's belt model, pure C++ | aware of steps or Arduino |
| [src/Driver.cpp](src/Driver.cpp) | `Driver::` — both TMC2240 over SPI: current, microsteps, ENN, status | the step generator |
| [src/Motion.cpp](src/Motion.cpp) | `Motion::` — FastAccelStepper on STEP/DIR, steps ↔ belt mm (rounded), jog, stop | Cartesian, or a task of its own (both come with the planner) |
| [src/Pen.cpp](src/Pen.cpp) | `Pen::` — servo on LEDC, ramped in `poll()`, `settled()` | blocking (Mural's is) |
| [src/Display.cpp](src/Display.cpp) | `Display::` — SSD1306, four lines, redrawn on change | required: absent = no-op |
| [src/Imu.cpp](src/Imu.cpp) | `Imu::` — BMI270, tilt by complementary filter | an input to the motion |
| [src/HardwareRoutes.cpp](src/HardwareRoutes.cpp) | `Hw::` — `/hardware*`, all behind the lock | state-aware; a raw jog or motors-off makes Plotter forget its position |
| [src/Plotter.cpp](src/Plotter.cpp) | `Plotter::` — the state machine, what each state allows, the calibration flow, the Kinematics model | the job runner or the planner |
| [src/PlotRoutes.cpp](src/PlotRoutes.cpp) | `Plot::` — `/calib/*`, `/jog`, `/home`, `/stop`, `/pen`, `/plot/*`; open | a second state table — every handler asks Plotter |
| [src/JobFormat.cpp](src/JobFormat.cpp) | one job line, parsed; pure C++ with host tests | aware of files |
| [src/JobRunner.cpp](src/JobRunner.cpp) | `JobRunner::` — a job on the card: the sliced dry-run check, commands one by one, progress by distance | the executor (that is Plotter, and later the planner) |
| [src/SdStore.cpp](src/SdStore.cpp) | `SdStore::` — the card, SD_MMC 1-bit, mounted at boot in app mode | required: no card = no plotting, not no boot |
| [src/JobRoutes.cpp](src/JobRoutes.cpp) | `Jobs::` — `/jobs*`: list, streamed upload (.part + rename), download, delete; open | a file manager outside `/jobs` |

`main.cpp` starts the drivers right after the settings (motors off), then one `Wire` bus for
display and IMU. The display names the access point in portal mode and the address otherwise.

Every FastAccelStepper call comes from `loop()` for now - one caller, the case the library is
built for. When the planner arrives, its motion task becomes the only caller instead.

Things marked in the code as **unverified until a board exists**: the TMC2240 register values
(check `IOIN.VERSION` = 0x40 first), the IMU's axis mapping and signs (constants at the top of
`Imu.cpp`), which DIR level lengthens which belt (top of `Motion.cpp`; jog +10 mm and watch),
the servo pulse range.

### The plotter's states (Plotter.h)

`unconfigured` (no anchor distance) → `needsRetract` (wind both belts to the stop by jogging) →
`retracted` (lengths known: 40 mm each; pulley calibration happens here) → `extendingHome` →
`atHome` (hang the loops on the pins) → `ready` (Cartesian moves) ⇄ `plotting` ⇄ `paused`.
Changing the anchor distance, jogging a belt, a raw jog or motors-off from the Hardware tab, an
emergency `/stop` during a move, and a restart all go back to `needsRetract`; the position is
not persisted yet. Pausing or cancelling a job waits for the current millimetre and keeps the
position. Moves before the planner are point-to-point in belt space (`Motion::moveBelts`: both
belts on scaled copies of one ramp); a drawn segment is cut into 1 mm pieces at `drawSpeed`
(Mural's way - straight, but stop-and-go). A finished job returns home.

### REST: plotter (open; firmware, `server.js` and `api.js` share it)

- `GET /currentState` carries `plotter`: `state`, `moving`, `anchorDistance`, `pulleyDiameter`,
  `pulleyRun`, `microsteps`, `penDown`; once configured `width`, `maxY`, `home{x,y}`; from
  `retracted` on `belts{left,right}` in mm; when `ready` `position{x,y,target}` and `tiltModel`;
  `tiltImu` whenever the IMU reads (degrees).
- `POST /calib/anchor {mm}` (300–5000), `/calib/retracted`, `/calib/home`, `/calib/hung`,
  `/calib/pulley/extend {mm}` (100–2000, left belt), `/calib/pulley {measured}` (within ±20 %),
  `/calib/pen {up, down}`.
- `POST /jog` — `{side, mm}` before homing, `{dx, dy}` when ready. `POST /home`, `POST /stop`
  (never refused), `POST /pen {state|angle}`.
- `POST /plot/start {name}`, `/plot/pause`, `/plot/resume`, `/plot/stop`,
  `/plot/config {drawSpeed}` (1–100 mm/s). `/currentState` then carries `plotter.job`
  (`name`, `phase` idle|checking|running|finished|failed, `total`, `done` in mm, and `error`
  plus `line` when it failed) and `drawSpeed`; `position` also while plotting or paused.
- `GET /jobs` → `{card: {present, total, used}, jobs: [{name, size}], truncated?}`;
  `POST /jobs/upload?name=` (multipart, streamed); `GET /jobs/read?name=[&download=1]`;
  `POST /jobs/delete {name}`. Names: `[A-Za-z0-9._-]{1,64}`, no leading dot.
- Refusals: `notConfigured`, `wrongState`, `busy`, `outsideArea`, `unreachable`, `noPulleyRun`,
  `motion`, `noCard` (409); `badValue` with `errorDetail`, `jobName` (400); `jobNotFound` (404);
  a job's own failures (`jobEmpty`, `outsideArea` with `line`, `jobRead`) land in `plotter.job`.

### REST: hardware (firmware, `server.js` and `api.js` share it)

- `GET /hardware` — `motorsEnabled`, `motorCurrent`, `maxCurrent`, `microsteps`, `pulleyDiameter`,
  `motion` (`ready`, `moving`, `stepsPerMm`, `belts[]` with `side`, `steps`, `mm` counted from
  boot, `moving`), `drivers[]`
  (per side: `present`, `version`, and when present `temperature`, `supply`, `stallGuard`,
  `currentScale`, `standstill`, fault flags, `globalStatus`, raw `drvStatus`), `pen`
  (`angle`, `target`, `down`, `settled`, `upAngle`, `downAngle`), `imu` (`present`, and when
  present `tilt`, `tiltAccel`, `rate` in degrees, raw `accel[]`/`gyro[]`, `samples`),
  `display.present`. NaN goes out as `null`.
- `POST /hardware/motors` — `{enabled}`; switching off while a belt moves is `409 busy`.
- `POST /hardware/jog` — `{side: "left"|"right", mm}`, ±1000 mm; `409 motorsOff` / `409 busy`.
- `POST /hardware/stop` — both belts, ramped.
- `POST /hardware/pen` — `{state: "up"|"down"}` or `{angle}`.
- `POST /hardware/config` — `{motorCurrent, microsteps, pulleyDiameter, penUp, penDown}`, each
  optional; validated whole before anything is applied; a scale change (microsteps, pulley)
  rescales the step positions and needs standstill (`409 busy`); stored through
  `scheduleSettingsSave()`.

All behind the expert lock; refusals are `badValue` with the field in `errorDetail`. The mock's
right driver is absent on purpose, to show that card.

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
  tests compare against [test/test_kinematics/reference.h](test/test_kinematics/reference.h),
  generated by [scripts/kinematics_reference.py](scripts/kinematics_reference.py) — a line-by-line
  Python port of Mural's own code, deliberately not of ours. Regenerate only on purpose; CI runs
  the script with `--check`.
- Two solvers for the one model: `solve()` (ours: brackets and refines the root of the full
  torque balance — the answer belongs to the point, not to the route) and `solveMural()`
  (Mural's grid search, kept only so the transcription stays checkable against the reference).
  The firmware uses `solve()`. Mural's does not converge after a jump and chatters by a grid
  step at standstill; the tests show both and hold `solve()` to within half a grid step of
  Mural's wherever Mural's converges. The reference is a *path*, not a set, because Mural's
  solver is warm-started.
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

`pio` is not on PATH; use `%USERPROFILE%\.platformio\penv\Scripts\pio.exe`.

```
npm install && npm run build      # web/ -> data/
npm run dev / npm run mock        # UI against the mock, no hardware
pio run / -t upload / -t uploadfs # the device (env seeed_xiao_esp32s3)
pio test -e native                # host tests (Kinematics, JobFormat) - needs gcc/clang on PATH
python scripts/kinematics_reference.py [--check]   # regenerate / verify the reference table
python scripts/icon.py            # regenerate the icons (then copy three of them to docs/)
```

Three traps on this machine, all hit on 2026-11-15:

- **Run `pio` from PowerShell or the VS Code terminal, not from Git Bash.** The platform's
  ESP-IDF tools refuse an MSYS environment ("MSys/Mingw is not supported"), and esptool then
  fails to install. Claude Code's Bash tool is Git Bash - use its PowerShell tool for `pio`.
- **The pioarduino platform pins PlatformIO Core 6.1.19** (its fork, `pioarduino-core`) and
  reinstalls it into `~/.platformio/penv` on a build if the installed version differs. If the
  VS Code extension has upgraded the core, that reinstall fails on Windows while PIO Home runs
  from the same environment (locked files) and leaves packages half-removed - `pio` then dies
  with `ModuleNotFoundError`. Fix: stop PIO Home (its two `python.exe ... platformio ... home`
  processes; closing VS Code would also end a Claude session running inside it), then install
  the platform's pinned list from `~/.platformio/platforms/espressif32/builder/penv_setup.py`
  (`python_deps`) with that environment's `uv pip install`. A half-installed
  `packages/tool-esptoolpy` (only `package.json`/`tools.json`) and leftover
  `.cache/tmp/pkg-installing-*` folders are safe to delete; the next build fetches them again.
- **Git dependencies need full commit hashes** - see the comment at `lib_deps`. A short hash
  only ever worked from the cache.

CI: [test.yml](.github/workflows/test.yml) runs the host tests on every push;
[release.yml](.github/workflows/release.yml) builds both images on `main` (edge pre-release)
and on `v*` tags (stable release + flashing page).

## Licence and attribution

Kinematic model, mechanics and job format follow nikivanov/mural (CC BY-NC 4.0). Keep the
attribution in the README and in the header of `Kinematics.*`. Any code taken over from Mural's web
worker must say so in its header.
