# Scribbler — plan

A wall-hanging two-belt pen plotter (polargraph), built on the mechanics and kinematic model of
[nikivanov/mural](https://github.com/nikivanov/mural) and on the house firmware scaffolding of
DoorOpener / QlockThreeW32: own setup portal, Svelte 5 UI in the house look, expert lock, debug
tab with log ring, storage explorer, OTA over a GitHub release channel, first-flash page.

Status: **planning** — nothing is built yet. Written 2026-11-12.

---

## 0. The board is undecided

Two candidates, and the firmware is written so that either works (all pins in `Board.h`):

- **(A) Mural's board** — the "TMC2209 Pen/Laser FluidNC CNC Controller" (Bart Dring's design,
  sold through Elecrow / V1 Engineering; the Tindie listing is marked retired). Mural's firmware
  builds for `board = esp32dev`: a **classic ESP32 (WROOM)**, not an ESP32-S3. Mural ships no PCB
  of its own.
- **(B) An own board around the XIAO ESP32-S3 Plus** with the two TMC2209 on the board itself, no
  StepSticks — the house standard without deviations. The open risk is the TMC2209 circuit; §0.2
  is about exactly that.

Recommended order: firmware against a **breadboard rig** first (XIAO S3 Plus + two UART-capable
TMC2209 StepSticks, e.g. BTT V1.3 with the PDN_UART jumper set). It talks to the drivers exactly
like the integrated chips will — same UART, same addresses, same registers — so firmware and PCB
risk are separated: when the own board arrives and misbehaves, the firmware is already known good.

### 0.1 If it becomes board (A): the classic-ESP32 deviations

Before anything else, **look at the module**: `esptool chip-id` and `esptool flash-id` settle chip,
flash size and PSRAM in a minute. If it is a classic ESP32, the house standard applies with these
deviations (each one written down in `platformio.ini` / `partitions.csv` with its reason, the way
the XIAO ones are):

| House standard (XIAO S3 Plus) | Classic ESP32 on the Pen/Laser board |
|---|---|
| 16 MB flash, 2 × 3 MB OTA, ~9.9 MB LittleFS | almost certainly **4 MB**: 2 × 1.75 MB OTA, ~384 kB LittleFS (enough for the ~170 kB UI; jobs live on the SD card, not in LittleFS) |
| native USB, `before_reset = usb-reset` | USB-UART bridge → the default reset works; drop `usb-reset` |
| octal PSRAM, `-DBOARD_HAS_PSRAM` | WROOM has **no PSRAM** → drop the flag; the heap is ~300 kB, which rules out buffering whole jobs in RAM (Mural streams from SD for this reason) |
| bootloader at `0x0` | bootloader at **`0x1000`** — `web_tools_manifest.py` must not hard-code the S3 offset, or the first-flash page writes a board that boots to nothing |
| pioarduino fork, Arduino core 3.3.x | unchanged — the fork supports the classic ESP32 too |
| TX power 13 dBm from the event handler | unchanged — keep it; the board runs off a USB-C PD brick through a trigger module and a 3.3 V regulator nobody has measured yet |

### 0.2 If it becomes board (B): TMC2209 on the board, without StepSticks

What a StepStick hides and an own board has to provide — per driver, after the TMC2209 datasheet
(rev 1.08, "Typical application") and the TMC2209-BOB / SilentStepStick schematics, which are the
references to copy from:

| Item | Value / note |
|---|---|
| Part | **TMC2209-LA** (QFN28, exposed pad) — or **TMC2226-SA** (HTSSOP28): same register set and UART, better thermal path, inspectable joints. Check JLCPCB stock for both before choosing. |
| VS (motor supply) | 4.75–29 V; ≥ 100 µF low-ESR bulk near each driver **plus** 100 nF at each VS pin. The bulk cap is what protects the driver when the PD supply is hot-plugged — the classic way to kill a TMC. |
| Charge pump | 22 nF CPO–CPI, 100 nF VCP–VS |
| 5VOUT (internal LDO) | 4.7 µF + 100 nF to GND. It does **not** feed VCC_IO: VCC_IO comes from the XIAO's 3.3 V so the logic levels match |
| Sense resistors | 2 × 0.11 Ω (≥ 0.5 W, 1206/2010) on BRA/BRB → ~1.7 A RMS max with `vsense=0`; the 1 A pancake motors run at ~0.7 A. Internal-Rsense mode exists but is less accurate — keep the resistors. |
| UART | PDN_UART pin, single-wire. MCU TX → 1 kΩ → bus, MCU RX directly on the bus (as on the FYSETC/BTT boards). Both drivers on one bus, **address by MS1/MS2 straps**: left = 0 (GND/GND), right = 1 (VCC_IO/GND). With MS1/MS2 used as address, microsteps come from the UART — which is what we want anyway. |
| Microstep / mode straps | SPREAD → GND (StealthChop default), INDEX unconnected or to a test pad, CLK → GND (internal clock) |
| ENN | one shared MCU pin, pulled **up** (10 kΩ) so the motors stay off while the ESP32 boots and its pins float |
| DIAG | to MCU (StallGuard homing) — or to test pads only, StallGuard can also be polled over UART (`SG_RESULT`) |
| Thermal | exposed pad → ≥ 9 vias into a GND plane on the bottom; 2 layers suffice at 0.7 A, 4 layers if the current is to go above 1 A |
| Motor connectors | JST-XH 4-pin, coil order as on the stepper datasheet — check the coil pairing of the actual motors with a multimeter before the first power-up |

Power path of the whole board: USB-C PD sink (**CH224K**, voltage set by resistor: 20 V or 15 V)
→ VS; buck converter → 5 V for servo (MG90S stall current ~1 A — the buck must take it) and the
XIAO's 5 V pin. **Diode or ideal-diode between that 5 V and the XIAO's 5 V pin**: the XIAO ties
its 5 V pin to USB VBUS, so without it the board back-feeds into the PC's USB port whenever both
are plugged in.

**Pin budget on the XIAO ESP32-S3 Plus.** Needed:

| Function | Pins |
|---|---|
| STEP L/R, DIR L/R | 4 |
| ENN (shared) | 1 |
| TMC UART (single-wire: TX via 1 kΩ + RX) | 2 (1 with half-duplex tricks — not worth it) |
| DIAG L/R | 2 (optional) |
| Pen servo | 1 |
| OLED (I2C SDA/SCL) | 2 |
| microSD (SPI: SCK, MISO, MOSI, CS) | 4 (3 in SD_MMC 1-bit mode) |
| **Total** | **14–16** |

The standard XIAO has 11 GPIO — not enough. The **Plus** adds 9 castellated pads on the back
(~20 GPIO). The D11–D19 → GPIO mapping differs between Seeed's wiki pages (D11 = GPIO38 in one,
GPIO42 in another); **take it from the Plus schematic, not the wiki**, before routing. Avoid the
strapping pins (GPIO0, 3, 45, 46) for STEP/DIR/ENN — a pulse at boot moves a motor.

### 0.3 Sensors: an IMU yes, shaft encoders no (footprint only)

Discussed 2026-11-12. The error budget of a polargraph, in orders of magnitude (31.9 mm pulley
circumference):

| Error source | Size | Seen by a shaft encoder? |
|---|---|---|
| step resolution at 1/16 | 0.01 mm | yes — already negligible |
| microstep angle error (±5 % of a full step) | ±0.01 mm, not cumulative | yes — irrelevant |
| lost steps | zero or catastrophic | **yes** |
| effective pulley diameter off by 0.5 % | ~5 mm over 1 m of belt | no |
| anchor distance d mismeasured by ±2 mm | several mm distortion at the edges | no |
| belt length at homing (stop screw) | ±0.5 mm | no |
| tilt γ, belt elongation | mm range, modelled | no |
| pen friction on the wall, sway | ~0.5–1 mm hysteresis | no |

A shaft encoder measures what the stepper already knows; everything between the shaft and the pen
tip — pulley, belt, geometry, tilt, friction — is invisible to it, and that is where the
millimetres are. (Rotator was the opposite case: gearing and backlash sat *behind* the motor, and
the output angle was the quantity that mattered.) What encoders could still do — detect lost steps,
and tell after a power cut whether the bot pulled the belts while the drivers were off — is covered
by StallGuard and, for the power cut, only within half a turn of ~30 turns per metre, so it does not
replace homing either. They would also need double-shaft motors, since the pulley sits on the only
shaft of a typical pancake stepper.

Decisions:

- **IMU on the board: Bosch BMI270** (accelerometer + gyro, on the OLED's I2C bus — no extra pin;
  in stock). The electronics ride on the bot, so it measures γ directly — the quantity the kinematic
  model only estimates. Uses: validate the model; self-calibration (measured vs. predicted γ over the
  area points at a wrong d or pulley diameter); detect sway and limit acceleration accordingly;
  detect "taken off the wall" and a slipped belt.
  - Why not the ADXL345 (the Klipper classic): it is accelerometer-only, and while the bot moves an
    accelerometer sees path acceleration on top of gravity, so it yields γ only at standstill. The
    BMI270's gyro measures the rate of γ itself (rotation about the wall normal); a complementary /
    Mahony filter gives γ during motion and separates sway from tilt. The ADXL345's strength, a
    3.2 kHz data rate for resonance measurement, is not needed — the bot sways at a few Hz.
  - Resolution needed: 0.1° of γ at a ~40 mm lever to the pen tip is ~0.07 mm — comfortably within
    the BMI270 (16 bit, ~0.06 mg/LSB at ±2 g) with averaging.
  - The BMI270 needs Bosch's ~8 kB config blob uploaded on every power-up (Bosch API / SparkFun
    library do this) — flash cost and a few ms at boot.
  - Mounting offset: the board is never exactly parallel to the bot's axis. Calibrate it once, at a
    point where the model predicts γ ≈ 0 or from two positions 180° apart, and store it in NVS.
- **No shaft encoders in the concept.** Footprints for two (MT6701 or AS5047P) under the motor
  shafts as an **unpopulated option**, so the experiment stays possible without the design depending
  on it. *Superseded 2026-11-15:* the finished schematic (`Scribbler_pretty`) drops the option and
  grounds the drivers' encoder inputs.
- **Driver choice stays open** — see §0.4.

### 0.4 Driver: TMC2209 (UART) or TMC2240 (SPI or UART)

The TMC2209 has **no SPI**; going to SPI means changing the driver. The candidate is the
**TMC2240**: SPI or UART, up to 36 V / 2.1 A, StallGuard 2 and 4, and **integrated current
sensing — no sense resistors**, which removes the part of the §0.2 circuit with the most to get
wrong. Not yet checked: JLCPCB stock and price.

Pin budget with SPI (two drivers, plus the optional encoders):

| Function | Pins |
|---|---|
| SCK, MOSI, MISO | 3 |
| CS × 2 drivers (+ 2 encoders) | 2 (4) |
| STEP/DIR × 2, ENN | 5 |
| DIAG × 2 (optional) | 2 |
| Servo | 1 |
| I2C (OLED, IMU) | 2 |
| microSD — **SD_MMC 1-bit, never on the shared SPI bus** | 3 |
| **Total** | **16–20** |

Twenty is everything the XIAO Plus has. The SD card stays off the shared bus regardless: it needs a
slow clock during init, some cards do not release MISO cleanly, and long writes hold the bus while
the motion task needs the drivers.

The firmware hides the choice behind a thin driver layer (`Driver::begin`, `setCurrent`,
`setMicrosteps`, `status`, `stallValue`), so the breadboard rig with TMC2209 StepSticks stays
valid whichever driver the board gets.

### 0.5 OLED

Part of the build: 0.91" SSD1306, 128 × 32, I2C (as in Mural). It shows what the web UI cannot when
there is no network: in portal mode the AP name and `192.168.4.1`; otherwise hostname/IP, state,
progress and the current error code. Optional at runtime — a missing display is detected and
skipped, never a boot failure.

---

## 1. What we take from where

### From Mural (re-implemented, not copied file by file)

- **Mechanics and BOM**: two NEMA17 pancake steppers, GT2 16T pulleys + 6 mm belt, MG90S pen-lift
  servo, USB-C PD supply + trigger, microSD, optional 0.91" SSD1306 OLED. STLs from the repo.
- **Kinematic model** (`KinematicModel.md`, `Movement::getBeltLengths`): not just two hypotenuses
  but the bot as a rigid body that tilts by γ — tangent points of both belts at the pulleys, belt
  angles, belt forces from the bot's weight, torque equilibrium solved for γ (iterated, warm-started
  from the last position), out-of-plane leg to the wall (`midPulleyToWall` = 41 mm), and belt
  elongation `L / (1 + 5e-5·F)`. Constants: pulley axis distance 89.5 mm, pulley Ø 10.149 mm
  (calibratable), centre of mass 10 mm below the axes, 0.55 kg.
- **Drawing-area convention**: origin at the left anchor, +y down; drawing area starts 0.25·d below
  and 0.2·d inside the anchors, so it is 0.6·d wide. One SVG unit = 1 mm.
- **Calibration flow**: set anchor distance → retract belts onto the stop → extend to home
  (centre, 350 mm down) → pen-down angle → pulley-diameter ("e-steps") calibration → draw.
- **Job format on SD** (`d<total>`, `h<height>`, `x y` lines, `p0`/`p1`): kept **compatible**, so
  Mural's browser pipeline output can be plotted from day one and our own pipeline can be checked
  against it.
- **The idea of the browser doing the heavy lifting**: SVG → flattened polylines → clipped →
  deduplicated → path-ordered → optional hatch infill → command file, all in a web worker.

### From the house scaffolding (copied from DoorOpener, see the `xiao-esp32s3-project` skill)

`main.cpp` mode split (portal **or** app, never both), `Portal`, `WebRoutes`, `OtaUpdate`, `Expert`,
`LogBuffer`, `FileRoutes`, `NvsRoutes`, `Settings`; Svelte 5 SPA + portal from one Vite build,
shared `app.css` and `lib/`; de/en locales; error codes instead of prose; `server.js` mock with the
same contract; release workflow (edge/stable), first-flash page on GitHub Pages.

### From Rotator

`FastAccelStepper` + TMC2209 over UART (Rotator uses the janelia `TMC2209` library at 250 kBd) —
already proven on your hardware. This replaces Mural's AccelStepper + TMCStepper.

### What we deliberately do differently from Mural

| Mural | Scribbler | Why |
|---|---|---|
| AccelStepper polled from `loop()`, acceleration = 999 999 999 | FastAccelStepper (hardware step generation) fed from a motion task on core 1 | step timing must not depend on how long an HTTP handler takes; infinite acceleration loses steps / shakes the bot at higher speeds |
| constant speed, segments chopped into fixed mm increments | time-sliced interpolation along the path with a trapezoidal profile and junction-deviation lookahead | faster plots at the same quality, no stop-and-go at every vertex |
| ESPAsyncWebServer + WiFiManager | the house `WebServer` + own portal | one stack across all projects; the portal looks like the app |
| state machine as "phases" each refusing most calls | one explicit state enum, one `canDo(action)` table, codes for every refusal | the UI can grey out what the device would refuse anyway |
| `ESP.restart()` at the end of every job | stays up; pen up, travel home, `Idle` | a restart is not a state transition |
| no pause / resume | pause, resume, stop; checkpoint (job, byte offset, last x/y) in NVS every n mm | resume after a power cut or a dried-out pen |
| 1/8 µstep, fixed 707 mA | 1/16 µstep, StealthChop, current and microsteps as settings | quieter, and the bot's mass differs from build to build |
| homing against a stop screw by eye | same stop, optionally detected by StallGuard (DIAG on GPIO 36/39) | later phase, see §5 |

---

## 2. Architecture

```
            ┌────────────────── core 0 ──────────────────┐   ┌────────── core 1 ──────────┐
 browser ──►│ WebServer: /plot/* /jobs/* /calib/* /jog/* │   │ Motion task                 │
 (SPA,      │            + house routes (wifi, ota, fs,  │   │  segment queue ◄─ JobRunner │
 worker)    │              nvs, expert, log)             │   │  planner (trapezoid, LA)    │
            │ JobRunner: streams SD file → segments ─────┼──►│  slicer (Δt = 5–10 ms)      │
            │ Pen (servo, LEDC)                          │   │  Kinematics::beltSteps(x,y) │
            │ Display (OLED, optional)                   │   │  → FastAccelStepper queues  │
            └────────────────────────────────────────────┘   └─────────────────────────────┘
```

Firmware modules (new ones; the house modules keep their names):

| Module | Owns | Is not |
|---|---|---|
| `Board.h` | the pin map and board facts, one block per board | configurable at runtime |
| `Kinematics.*` | pure maths: (x, y) in image mm → (left, right) belt length in mm, and the inverse; the tilt solver | aware of steppers, time or Arduino — builds on the host |
| `Planner.*` | segment queue, trapezoidal profile, junction lookahead, time slicing | aware of belts — it plans in Cartesian mm |
| `Motion.*` | the motion task, FastAccelStepper, TMC2209 setup, enable, position bookkeeping, homing | the place the job file is parsed |
| `Pen.*` | servo angle, up/down with ramp, the pen-down angle | blocking — the planner waits for it via a pen segment |
| `Display.*` | the SSD1306: one screen per state, refreshed on change, not per frame | required — absent display = no-op |
| `Imu.*` | the accelerometer: tilt γ, sway, "off the wall" | an input to the kinematics — it validates and calibrates, it does not steer |
| `Driver.*` | the TMC behind a thin interface: current, microsteps, status, stall value | step generation — that is FastAccelStepper in `Motion` |
| `JobRunner.*` | reading the job from SD, progress by distance, pause/resume/stop, checkpoints | a file manager — `/jobs/*` and the explorer do that |
| `PlotterState.*` | the state enum, the transition table, `canDo()`, the refusal codes | where anything moves |
| `SdStore.*` | mounting the card, the third store in the storage explorer | required for boot — the device runs without a card, it just cannot plot |
| `PlotRoutes.*` | `/plot/*`, `/jobs/*`, `/calib/*`, `/jog/*` | the house routes, which stay in `WebRoutes` |

`Kinematics` is the one module that gets **host unit tests** (`pio test -e native`): a table of
(x, y) → belt lengths computed with Mural's algorithm is the reference; ours must match it to
< 0.01 mm before it is allowed near a motor.

### Settings (NVS namespace `scribbler`)

Anchor distance, pulley diameter, pen-up / pen-down angle, draw speed, travel speed, acceleration,
motor current, microsteps, home position, bot mass and geometry (expert), hostname, OTA fields.
Three places per field, as in the house `Settings` module.

### REST contract (project part; house routes unchanged)

```
GET  /currentState            state, position, progress, settings — the landing tab polls this
POST /plot/start   {job}      POST /plot/pause   POST /plot/resume   POST /plot/stop
GET  /jobs                    list on SD: name, size, total distance, height
POST /jobs/upload?name=       streamed, chunked like Mural's (offset/total/final), into /jobs/
GET  /jobs/read?name=         POST /jobs/delete {name}
POST /calib/anchor  {mm}      POST /calib/retract   POST /calib/home
POST /calib/pen     {angle}   POST /calib/pulley    {measured mm}
POST /jog           {left|right|x|y, mm}            POST /motors {enabled}
```

Refusals are codes: `notHomed`, `busy`, `noCard`, `cardFull`, `outsideArea`, `uploadOutOfOrder`,
`noJob`, … each with an `err_*` key in `de.js` and `en.js`.

### Web UI tabs

- **Plot** (landing, open while locked): state, live position on a sketch of the wall, progress,
  start / pause / stop, pen up/down.
- **Jobs**: SVG/image import → web worker pipeline → preview with travel moves and estimated time →
  upload; list of jobs on the card.
- **Setup**: the calibration wizard, one step per screen, each step only enabled in the state
  that allows it.
- **Jog**: belts individually and in x/y, motors off.
- **WLAN** — open. **Update, Debug, Storage** (LittleFS + NVS + SD) — behind the expert lock.

The worker pipeline is written fresh in TypeScript/JS, using Mural's structure as the map
(flatten → trim → dedupe → optimise → infill → toCommands). If any of Mural's worker code is taken
over, it carries attribution and stays non-commercial (see §4).

---

## 3. Phases

Each phase ends with something checkable on the device, not with a green build.

1. **Scaffold** — run the `xiao-esp32s3-project` skill on a bare XIAO ESP32-S3 Plus: build,
   portal, WLAN, OTA, expert, debug, storage working. `Board.h` with the breadboard-rig pin map
   (and Mural's map for board A, checked against `tmc2209_Pen_Laser.h` in bdring/TMC2209_Pen_Laser,
   if that board turns up). OLED shows AP/IP from day one.
2. **Kinematics on the host** — `Kinematics` + native tests against a reference table generated
   from Mural's algorithm. A small page in the mock (or a script) that draws the belt lengths and
   the tilt γ over the drawing area.
   *Done 2026-11-15 (Mural-faithful port, 359-point reference, CI runs it).* Two findings in
   Mural's solver that matter for our time-sliced planner, both visible in the reference table:
   - **No convergence after a jump.** The torque search moves at most 2° per iteration and gives
     up after 20, so a pose > ~40° of tilt away from the warm start comes out wrong (a jump across
     the area lands at −3.8° instead of +22.4°). Mural hides this by chopping every move into 1 mm
     pieces. Handled by `Model::settle()` for the first pose after homing, resume or a jog.
   - **Limit cycle at standstill.** The 0.2° search grid is anchored at the warm start, so solving
     the same point repeatedly alternates between two grid values (22.4° ↔ 22.6° at a top corner)
     — ~0.07 mm of belt, i.e. several microsteps of chatter when the planner's 5–10 ms slices
     barely move.
   *Both fixed 2026-11-15* by a continuous solver (`Model::solve()`: bracket the root of the full
   torque balance, refine with Illinois) — within 0.11° / 0.072 mm of Mural wherever Mural
   converges, identical for any route to a point, smooth along a line (second difference
   4e-4 mm vs. Mural's 0.13 mm). Mural's solver stays as `solveMural()` for the reference test.
3. **Motors** — TMC2209 over UART (`test_connection`, current, microsteps, StealthChop), FastAccelStepper,
   jog endpoints, the Jog tab. Debug tab shows driver status (`DRV_STATUS`, temperature flags).
   *Built 2026-11-15, not yet on hardware:* the board has TMC2240 over SPI instead (`Driver`),
   FastAccelStepper 2.0.1 (`Motion`), belt jog and stop, driver status — all in a Hardware tab
   behind the expert lock rather than a separate Jog tab; the user-facing jog comes with the
   state machine in phase 4. Also built: `Pen` (non-blocking ramp), `Display`, `Imu` (BMI270,
   complementary filter).
4. **Calibration flow** — state machine, Setup wizard, settings persisted. Draw a 100 mm square
   in four positions of the wall; pulley-diameter calibration from the measured result.
   *Built 2026-11-15, not yet on hardware:* `Plotter` (states, refusal codes, retract → home →
   hang, XY jog and go-home in belt space), `PlotRoutes` (open), the Setup tab, anchor distance
   and pulley as settings. The pulley is calibrated by paying out a measured length rather
   than from a drawn square - Mural's way, and possible before the bot is on the wall. The
   square test needs straight lines, so it waits for the planner.
5. **Jobs** — SD, chunked upload, `JobRunner` with Mural-compatible format, progress, pause/stop.
   First plot from a file produced by Mural's own web UI.
   *Built 2026-11-15, not yet on hardware:* `SdStore`, `JobFormat` (host-tested; reads Mural
   files without Mural's d-line digit loss and refuses garbage instead of moving to 0,0),
   `JobRunner` (dry-run check in slices, progress by distance), `JobRoutes` (streamed multipart
   upload rather than Mural's chunked protocol - the file format is what stays compatible),
   pause/cancel at the end of a millimetre, the Plot tab. The first plot of a Mural file
   waits for hardware.
6. **Planner** — trapezoid + lookahead; compare plot time and quality against phase 5 on the same file.
7. **Own SVG pipeline** in the Jobs tab, with preview and time estimate.
8. **Polish** — checkpoint/resume after power loss, first-flash page, README, release.

**Release and first flash — requested 2026-11-15, not urgent, not to be forgotten.** The house
pipeline is already in the repository (from DoorOpener): `release.yml` builds both images on
every push to `main` (edge pre-release, running since the first push) and on a `v*` tag (stable
release + the GitHub Pages flashing page in `docs/`). What is still to do:
- GitHub settings, which the repository cannot carry (xiao-esp32s3-project skill,
  `references/github-setup.md`): **Pages source = GitHub Actions**, and on the `github-pages`
  environment a **deployment rule for `v*` tags** — without it the first stable release
  publishes and then fails at deploy-pages.
- `docs/index.html`: the lede and the steps still read as DoorOpener's text with the name
  swapped; rewrite for the plotter (what the board is, what to do after flashing: the
  `Scribbler` access point, then Setup).
- Check `scripts/web_tools_manifest.py` against `partitions.csv` once more, then tag
  **`v0.1.0`** and open the flashing page in a browser against a blank board.

In parallel with phases 2–6, if board (B) is chosen: **KiCad board** per §0.2 — schematic,
review, layout, JLCPCB order. It replaces the breadboard rig when it arrives; the only firmware
change should be `Board.h`.

---

## 4. Licence

Mural is **CC BY-NC 4.0** (the whole repo, code included). Your projects use **CC BY-NC-SA**. An
adaptation of Mural's model and code may be shared non-commercially with attribution, so this
fits; put the attribution ("kinematic model and mechanics after nikivanov/mural, CC BY-NC 4.0") in
the README and in the header of `Kinematics.*`. (Not legal advice.)

---

## 5. Suggestions beyond the port

- **Self-calibration from the IMU.** Visit a handful of points across the area, compare the measured
  tilt with the model's, fit d and the pulley diameter — replaces the tape measure and the drawn test
  square for most of the error budget in §0.3.
- **Sensorless retract (StallGuard).** Retract both belts until the driver reports a stall instead
  of watching the stop screw — the first step towards "hang it up, press one button".
- **Wall-position sanity check.** After homing, a two-second wiggle that measures belt tension via
  StallGuard's SG_RESULT: detects a belt that slipped off the pulley before the plot starts.
- **Live preview from the device.** The Plot tab draws the actual position stream (`/currentState`
  polled at ~4 Hz) over the job preview — makes a lost step visible while it happens.
- **Raster images.** Mural's worker has a vectoriser and a hatch filler; for photos a TSP-art or
  "squiggle" mode is the classic polargraph look and cheap to implement in the worker.
- **Speed vs. γ.** The tilt changes with position; at the edges the pen moves slower than the bot.
  The planner can limit speed by the Jacobian of the kinematics, not just by a constant.
- **Mock that moves.** `server.js` simulates a running job (progress, position along the path), so
  the Plot tab can be built without a wall.

---

## 6. Open questions (answers change the work)

Settled 2026-11-12: repository `FranzKugler/Scribbler` (public — devices fetch release assets
without credentials, and the first-flash page needs Pages); OLED is part of the build.

1. **Board** — (A) or (B), see §0. Leaning towards (B); the breadboard rig makes the choice
   non-blocking for the firmware.
2. **Driver** — TMC2209-LA / TMC2226-SA over UART, or TMC2240 over SPI/UART (§0.4); decided by
   JLCPCB stock and price when the schematic starts.
3. **Motor voltage** — 15 V or 20 V from PD (20 V: more torque at speed; 15 V: works with more
   chargers). Decides the CH224K resistor and the capacitor voltage ratings (≥ 35 V either way).
4. **Setup portal** — own portal (house default) assumed. **UI languages** — de + en assumed.
