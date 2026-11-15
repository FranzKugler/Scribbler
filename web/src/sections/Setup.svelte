<script>
  /**
   * Setup
   * The calibration flow, one card per step, from "just switched on" to
   * "hanging on the wall at a known position" - Mural's set-distance,
   * retract, extend-to-home sequence, plus the pulley and the pen.
   *
   * The device's state decides which card is live; a card for a step the
   * device would refuse is shown, but disabled, so the whole way is visible
   * from the start. Every button is a request the firmware checks against the
   * same state (Plotter.cpp), so this page greying something out is a
   * courtesy, not the safety.
   *
   * Open, like the device's own plotting controls. The raw hardware is in the
   * Hardware tab, behind the lock.
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

  // Quick enough to watch a belt pay out, cheap enough for the device.
  const POLL_MS = 500;

  let p = $state(null);
  let error = $state(null);
  let busy = $state(false);

  let anchor = $state(null);
  let beltStep = $state(10);
  let xyStep = $state(10);
  let pulleyMm = $state(500);
  let measured = $state(null);
  let penAngle = $state(70);

  let timer = null;
  let inFlight = false;

  async function load() {
    if (inFlight) return;
    inFlight = true;
    try {
      const state = await api.fetchState();
      p = state.plotter;
      error = null;
      if (anchor === null && p.anchorDistance > 0) anchor = p.anchorDistance;
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

  const is = (...states) => p && states.includes(p.state);
  const idle = $derived(p && !p.moving && !busy);
  const fmt = (v, digits = 1) => (v === null || v === undefined ? '—' : v.toFixed(digits));

  const BELT_STEPS = [1, 10, 50, 200];
  const XY_STEPS = [1, 10, 50];
</script>

{#if error && !p}
  <section class="card"><p class="banner">{t.setupUnavailable} — {error}</p></section>
{:else if !p}
  <section class="card"><p class="hint">{t.loadingShort}</p></section>
{:else}
  <section class="card">
    <h2>{t.setupState}</h2>
    <div class="field">
      <span class="key">{t.setupStateLabel}</span>
      <span>{t.plotterStates[p.state] ?? p.state}{p.moving ? ' …' : ''}</span>
    </div>
    {#if p.belts}
      <div class="field">
        <span class="key">{t.setupBelts}</span>
        <span>{fmt(p.belts.left)} / {fmt(p.belts.right)} mm</span>
      </div>
    {/if}
    {#if p.position}
      <div class="field">
        <span class="key">{t.setupPosition}</span>
        <span>x {fmt(p.position.x)} · y {fmt(p.position.y)} mm</span>
      </div>
      <div class="field">
        <span class="key">{t.setupTilt}</span>
        <span>{t.setupTiltValues(fmt(p.tiltModel, 2), p.tiltImu === undefined ? '—' : fmt(p.tiltImu, 2))}</span>
      </div>
    {/if}
    <div class="field">
      <span class="hint">{t.setupStateHint[p.state] ?? ''}</span>
      <button type="button" class="primary" onclick={() => act(api.stopAll)}>{t.setupStop}</button>
    </div>
  </section>

  <!-- 1: the wall -->
  <section class="card">
    <h2>1 · {t.setupAnchorTitle}</h2>
    <div class="field">
      <label for="anchor">{t.setupAnchor}</label>
      <input id="anchor" type="number" min="300" max="5000" step="1" bind:value={anchor} />
      <button type="button" class="secondary" disabled={!idle || !anchor || anchor === p.anchorDistance}
        onclick={() => act(() => api.setAnchor(anchor))}>{t.setupSave}</button
      >
    </div>
    <p class="hint">{t.setupAnchorHint}</p>
    {#if p.width}
      <p class="hint">{t.setupArea(fmt(p.width, 0), fmt(p.maxY, 0))}</p>
    {/if}
  </section>

  <!-- 2: belts to the stop -->
  <section class="card">
    <h2>2 · {t.setupRetractTitle}</h2>
    <p class="hint">{t.setupRetractHint}</p>
    <div class="field">
      <label for="beltstep">{t.setupStep}</label>
      <select id="beltstep" bind:value={beltStep}>
        {#each BELT_STEPS as s (s)}<option value={s}>{s} mm</option>{/each}
      </select>
    </div>
    {#each ['left', 'right'] as side (side)}
      <div class="field">
        <span class="key">{t.hwBelt(side === 'left' ? t.hwLeft : t.hwRight)}</span>
        <span>
          <button type="button" class="secondary" disabled={!idle || !is('needsRetract', 'retracted')}
            onclick={() => act(() => api.jog({ side, mm: -beltStep }))}>{t.setupWindIn}</button
          >
          <button type="button" class="secondary" disabled={!idle || !is('needsRetract', 'retracted')}
            onclick={() => act(() => api.jog({ side, mm: beltStep }))}>{t.setupPayOut}</button
          >
        </span>
      </div>
    {/each}
    <div class="field">
      <span></span>
      <button type="button" class="primary" disabled={!idle || !is('needsRetract', 'retracted')}
        onclick={() => act(api.confirmRetracted)}>{t.setupAtStop}</button
      >
    </div>
  </section>

  <!-- 2a: the pulley, optional -->
  <section class="card">
    <h2>{t.setupPulleyTitle}</h2>
    <p class="hint">{t.setupPulleyHint(fmt(p.pulleyDiameter, 4))}</p>
    {#if p.pulleyRun > 0}
      <div class="field">
        <label for="measured">{t.setupMeasured(fmt(p.pulleyRun, 0))}</label>
        <input id="measured" type="number" step="0.5" bind:value={measured} />
        <button type="button" class="primary" disabled={!idle || !measured}
          onclick={() => act(() => api.pulleyMeasured(measured))}>{t.setupApply}</button
        >
      </div>
    {:else}
      <div class="field">
        <label for="pulleymm">{t.setupPulleyRun}</label>
        <input id="pulleymm" type="number" min="100" max="2000" step="50" bind:value={pulleyMm} />
        <button type="button" class="secondary" disabled={!idle || !is('retracted')}
          onclick={() => act(() => api.pulleyExtend(pulleyMm))}>{t.setupPulleyStart}</button
        >
      </div>
    {/if}
  </section>

  <!-- 3 and 4: home, then hang -->
  <section class="card">
    <h2>3 · {t.setupHomeTitle}</h2>
    <p class="hint">{t.setupHomeHint}</p>
    <div class="field">
      <span>{p.home ? `x ${fmt(p.home.x)} · y ${fmt(p.home.y)} mm` : ''}</span>
      <button type="button" class="primary" disabled={!idle || !is('retracted')}
        onclick={() => act(api.extendHome)}>{t.setupExtend}</button
      >
    </div>
  </section>

  <section class="card">
    <h2>4 · {t.setupHangTitle}</h2>
    <p class="hint">{t.setupHangHint}</p>
    <div class="field">
      <span></span>
      <button type="button" class="primary" disabled={!idle || !is('atHome')} onclick={() => act(api.confirmHung)}
        >{t.setupHung}</button
      >
    </div>
  </section>

  <!-- 5: ready - move and pen -->
  <section class="card">
    <h2>5 · {t.setupMoveTitle}</h2>
    <div class="field">
      <label for="xystep">{t.setupStep}</label>
      <select id="xystep" bind:value={xyStep}>
        {#each XY_STEPS as s (s)}<option value={s}>{s} mm</option>{/each}
      </select>
    </div>
    <div class="pad">
      <span></span>
      <button type="button" class="secondary" disabled={!idle || !is('ready')}
        onclick={() => act(() => api.jog({ dx: 0, dy: -xyStep }))} aria-label={t.setupUp}>↑</button
      >
      <span></span>
      <button type="button" class="secondary" disabled={!idle || !is('ready')}
        onclick={() => act(() => api.jog({ dx: -xyStep, dy: 0 }))} aria-label={t.setupLeft}>←</button
      >
      <button type="button" class="secondary" disabled={!idle || !is('ready')} onclick={() => act(api.goHome)}
        >{t.setupHome}</button
      >
      <button type="button" class="secondary" disabled={!idle || !is('ready')}
        onclick={() => act(() => api.jog({ dx: xyStep, dy: 0 }))} aria-label={t.setupRight}>→</button
      >
      <span></span>
      <button type="button" class="secondary" disabled={!idle || !is('ready')}
        onclick={() => act(() => api.jog({ dx: 0, dy: xyStep }))} aria-label={t.setupDown}>↓</button
      >
      <span></span>
    </div>
    <p class="hint">{t.setupMoveHint}</p>
  </section>

  <section class="card">
    <h2>{t.setupPenTitle}</h2>
    <div class="field">
      <span class="key">{t.hwPen}</span>
      <span>
        <button type="button" class="secondary" disabled={busy} onclick={() => act(() => api.movePen({ state: 'up' }))}
          >{t.hwPenUp}</button
        >
        <button type="button" class="secondary" disabled={busy}
          onclick={() => act(() => api.movePen({ state: 'down' }))}>{t.hwPenDown}</button
        >
      </span>
    </div>
    <div class="field">
      <label for="penangle">{t.hwPenTry}</label>
      <input id="penangle" type="number" min="0" max="180" bind:value={penAngle} />
      <button type="button" class="secondary" disabled={busy}
        onclick={() => act(() => api.movePen({ angle: penAngle }))}>{t.hwPenMove}</button
      >
    </div>
    <div class="field">
      <button type="button" class="secondary" disabled={busy}
        onclick={() => act(() => api.savePenAngles({ up: penAngle }))}>{t.hwPenSaveUp}</button
      >
      <button type="button" class="secondary" disabled={busy}
        onclick={() => act(() => api.savePenAngles({ down: penAngle }))}>{t.hwPenSaveDown}</button
      >
    </div>
    <p class="hint">{t.setupPenHint}</p>
  </section>
{/if}

<style>
  .pad {
    display: grid;
    grid-template-columns: repeat(3, minmax(0, 5rem));
    gap: 0.4rem;
    justify-content: center;
    margin: 0.5rem 0;
  }
</style>
