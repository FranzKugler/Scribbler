<script>
  /**
   * Hardware
   * Bring-up and diagnosis: what the two drivers, the pen, the IMU and the
   * display report, and the few things worth doing to them by hand.
   *
   * Behind the expert lock, like the firmware endpoints it reads: switching
   * the motors on is not something anybody on the network should be able to
   * do. The plotter's own controls - jog, homing, calibration - belong to the
   * Plot and Setup tabs, which check every action against the device's state.
   * This tab does not; it talks to the hardware directly, which is the point
   * of it while a board is being brought up.
   *
   * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
   * @version  1.0
   * @created  15.11.2026
   * @updated  15.11.2026
   */
  import { onMount, onDestroy } from 'svelte';
  import * as api from '../lib/api.js';
  import { dict } from '../lib/i18n.svelte.js';

  const t = $derived(dict());

  // Fast enough to watch the IMU follow a hand tilting the bot; the request
  // also reads both drivers over SPI, which costs the device next to nothing.
  const POLL_MS = 500;

  let hw = $state(null);
  let error = $state(null);
  let busy = $state(false);

  // Edited copies, so a poll does not overwrite what is being typed.
  let current = $state(null);
  let microsteps = $state(null);
  let pulley = $state(null);
  let jogStep = $state(10);
  let angle = $state(90);

  let timer = null;
  let inFlight = false;

  async function load() {
    if (inFlight) return;
    inFlight = true;
    try {
      hw = await api.fetchHardware();
      error = null;
      if (current === null) current = hw.motorCurrent;
      if (microsteps === null) microsteps = hw.microsteps;
      if (pulley === null) pulley = hw.pulleyDiameter;
    } catch (err) {
      error = err.message;
    } finally {
      inFlight = false;
    }
  }

  onMount(() => {
    load();
    timer = setInterval(load, POLL_MS);
  });
  onDestroy(() => clearInterval(timer));

  async function act(call) {
    busy = true;
    await call();
    busy = false;
    load();
  }

  const fmt = (v, digits = 1) => (v === null || v === undefined ? '—' : v.toFixed(digits));

  /** The driver's fault flags as words, or "none". */
  function faults(d) {
    const list = [];
    if (d.overtemperature) list.push(t.hwOt);
    else if (d.overtemperatureWarn) list.push(t.hwOtpw);
    if (d.shortToGround) list.push(t.hwS2g);
    // Open load is only meaningful while the motor turns; at standstill the
    // driver reports it for a perfectly good coil.
    if (d.openLoad && !d.standstill) list.push(t.hwOl);
    if (d.globalStatus & 1) list.push(t.hwReset);
    if (d.globalStatus & 2) list.push(t.hwDrvErr);
    if (d.globalStatus & 4) list.push(t.hwUvCp);
    return list.length ? list.join(', ') : t.hwNone;
  }

  const MICROSTEPS = [1, 2, 4, 8, 16, 32, 64, 128, 256];
  const JOG_STEPS = [1, 10, 50, 200];

  const scaleUnchanged = $derived(
    hw && current === hw.motorCurrent && microsteps === hw.microsteps && pulley === hw.pulleyDiameter
  );
</script>

{#if error && !hw}
  <section class="card">
    <p class="banner">{t.hwUnavailable} — {error}</p>
  </section>
{:else if !hw}
  <section class="card"><p class="hint">{t.loadingShort}</p></section>
{:else}
  <section class="card">
    <h2>{t.hwMotors}</h2>
    <div class="field">
      <span class="key">{t.hwMotorsEnabled}</span>
      <span class="switch">
        <input
          type="checkbox"
          checked={hw.motorsEnabled}
          disabled={busy}
          onchange={(e) => act(() => api.setMotors(e.currentTarget.checked))}
        /><span></span>
      </span>
    </div>
    <p class="hint">{t.hwMotorsHint}</p>

    <div class="field">
      <label for="current">{t.hwCurrent}</label>
      <input id="current" type="number" min="100" max={hw.maxCurrent} step="50" bind:value={current} />
    </div>
    <div class="field">
      <label for="microsteps">{t.hwMicrosteps}</label>
      <select id="microsteps" bind:value={microsteps}>
        {#each MICROSTEPS as m (m)}
          <option value={m}>1/{m}</option>
        {/each}
      </select>
    </div>
    <div class="field">
      <label for="pulley">{t.hwPulley}</label>
      <input id="pulley" type="number" min="5" max="40" step="0.001" bind:value={pulley} />
    </div>
    <div class="field">
      <span class="hint">{t.hwCurrentHint(hw.maxCurrent)}</span>
      <button
        type="button"
        class="primary"
        disabled={busy || scaleUnchanged}
        onclick={() =>
          act(() => api.setHardwareConfig({ motorCurrent: current, microsteps, pulleyDiameter: pulley }))}
        >{t.hwApply}</button
      >
    </div>
  </section>

  <section class="card">
    <h2>{t.hwJog}</h2>
    {#if !hw.motion.ready}
      <p class="banner">{t.hwMotionNotReady}</p>
    {:else}
      <div class="field">
        <label for="jogstep">{t.hwJogStep}</label>
        <select id="jogstep" bind:value={jogStep}>
          {#each JOG_STEPS as s (s)}
            <option value={s}>{s} mm</option>
          {/each}
        </select>
      </div>
      {#each hw.motion.belts as b (b.side)}
        <div class="field">
          <span class="key">{t.hwBelt(b.side === 'left' ? t.hwLeft : t.hwRight)}</span>
          <span>{fmt(b.mm, 2)} mm {b.moving ? '…' : ''}</span>
          <span>
            <button type="button" class="secondary" disabled={busy || !hw.motorsEnabled || b.moving}
              onclick={() => act(() => api.jogBelt(b.side, -jogStep))}>−</button
            >
            <button type="button" class="secondary" disabled={busy || !hw.motorsEnabled || b.moving}
              onclick={() => act(() => api.jogBelt(b.side, jogStep))}>+</button
            >
          </span>
        </div>
      {/each}
      <div class="field">
        <span class="hint">{hw.motorsEnabled ? t.hwJogHint(fmt(hw.motion.stepsPerMm, 2)) : t.hwJogNeedsMotors}</span>
        <button type="button" class="primary" onclick={() => act(() => api.stopMotion())}>{t.hwStop}</button>
      </div>
    {/if}
  </section>

  {#each hw.drivers as d (d.side)}
    <section class="card">
      <h2>{t.hwDriver(d.side === 'left' ? t.hwLeft : t.hwRight)}</h2>
      {#if !d.present}
        <p class="banner">{t.hwAbsent(d.version)}</p>
      {:else}
        <div class="field"><span class="key">{t.hwTemperature}</span><span>{fmt(d.temperature)} °C</span></div>
        <div class="field"><span class="key">{t.hwSupply}</span><span>{fmt(d.supply, 2)} V</span></div>
        <div class="field"><span class="key">{t.hwStallGuard}</span><span>{d.stallGuard}</span></div>
        <div class="field"><span class="key">{t.hwCurrentScale}</span><span>{d.currentScale} / 31</span></div>
        <div class="field"><span class="key">{t.hwStandstill}</span><span>{d.standstill ? t.yes : t.no}</span></div>
        <div class="field"><span class="key">{t.hwFaults}</span><span>{faults(d)}</span></div>
        <div class="field">
          <span class="key">DRV_STATUS</span><code>0x{d.drvStatus.toString(16).padStart(8, '0')}</code>
        </div>
      {/if}
    </section>
  {/each}

  <section class="card">
    <h2>{t.hwPen}</h2>
    <div class="field">
      <span class="key">{t.hwPenAngle}</span>
      <span>{fmt(hw.pen.angle, 0)}° → {hw.pen.target}° {hw.pen.settled ? '' : '…'}</span>
    </div>
    <div class="field">
      <button type="button" class="secondary" disabled={busy} onclick={() => act(() => api.setPen({ state: 'up' }))}
        >{t.hwPenUp} ({hw.pen.upAngle}°)</button
      >
      <button type="button" class="secondary" disabled={busy} onclick={() => act(() => api.setPen({ state: 'down' }))}
        >{t.hwPenDown} ({hw.pen.downAngle}°)</button
      >
    </div>
    <div class="field">
      <label for="angle">{t.hwPenTry}</label>
      <input id="angle" type="number" min="0" max="180" bind:value={angle} />
      <button type="button" class="secondary" disabled={busy} onclick={() => act(() => api.setPen({ angle }))}
        >{t.hwPenMove}</button
      >
    </div>
    <div class="field">
      <button type="button" class="secondary" disabled={busy}
        onclick={() => act(() => api.setHardwareConfig({ penUp: angle }))}>{t.hwPenSaveUp}</button
      >
      <button type="button" class="secondary" disabled={busy}
        onclick={() => act(() => api.setHardwareConfig({ penDown: angle }))}>{t.hwPenSaveDown}</button
      >
    </div>
  </section>

  <section class="card">
    <h2>{t.hwImu}</h2>
    {#if !hw.imu.present}
      <p class="hint">{t.hwImuAbsent}</p>
    {:else}
      <div class="field"><span class="key">{t.hwTilt}</span><span>{fmt(hw.imu.tilt, 2)}°</span></div>
      <div class="field"><span class="key">{t.hwTiltAccel}</span><span>{fmt(hw.imu.tiltAccel, 2)}°</span></div>
      <div class="field"><span class="key">{t.hwRate}</span><span>{fmt(hw.imu.rate, 2)} °/s</span></div>
      <div class="field">
        <span class="key">{t.hwAccel}</span><span>{hw.imu.accel.map((v) => fmt(v, 3)).join(' / ')}</span>
      </div>
      <div class="field">
        <span class="key">{t.hwGyro}</span><span>{hw.imu.gyro.map((v) => fmt(v, 2)).join(' / ')}</span>
      </div>
      <p class="hint">{t.hwImuHint}</p>
    {/if}
  </section>

  <section class="card">
    <h2>{t.hwDisplay}</h2>
    <div class="field">
      <span class="key">SSD1306</span><span>{hw.display.present ? t.hwFound : t.hwMissing}</span>
    </div>
  </section>

  {#if error}
    <p class="hint">{error}</p>
  {/if}
{/if}
