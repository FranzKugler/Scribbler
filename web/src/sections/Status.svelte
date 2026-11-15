<script>
  /**
   * Status
   * What the device is, in the four lines somebody actually asks for.
   *
   * The landing tab, and the one that is open while the device is locked -
   * which is why it reads /currentState rather than /ota/status. Both know the
   * two version numbers; only one of them is behind the password, and "which
   * build is this" is a question a locked device should still answer.
   *
   * This is also where the plotter's own controls belong once they exist
   * (the Plot tab in PLAN.md). What is here now is deliberately the device and
   * not the plotter: better an honest empty page than a mock-up of controls
   * that do nothing.
   *
   * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
   * @version  1.0
   * @created  31.8.2026
   * @updated  31.8.2026
   */
  import { onMount, onDestroy } from 'svelte';
  import * as api from '../lib/api.js';
  import { dict } from '../lib/i18n.svelte.js';

  const t = $derived(dict());

  let info = $state(null);
  let error = $state(null);

  let timer = null;
  onDestroy(() => clearInterval(timer));

  async function load() {
    try {
      info = await api.fetchState();
      error = null;
    } catch (err) {
      error = err.message;
    }
  }

  onMount(() => {
    load();
    // Only the uptime moves, so once a minute is plenty - and it keeps the
    // page from looking frozen when it has been left open.
    timer = setInterval(load, 60000);
  });

  /** millis() as "3 d 4 h" / "5 h 12 min" / "12 min". */
  function duration(ms) {
    const total = Math.floor(ms / 1000);
    const d = Math.floor(total / 86400);
    const h = Math.floor(total / 3600) % 24;
    const m = Math.floor(total / 60) % 60;
    return d > 0 ? `${d} d ${h} h` : h > 0 ? `${h} h ${m} min` : `${m} min`;
  }
</script>

<section class="card">
  <h2>{t.statusTitle}</h2>

  {#if error && !info}
    <p class="banner">{t.statusUnavailable} — {error}</p>
  {:else if info}
    <div class="field">
      <span class="key">{t.deviceName}</span><span>{info.hostname}</span>
    </div>
    <div class="field">
      <span class="key">{t.address}</span><span>{info.ip || '—'}</span>
    </div>
    <div class="field">
      <span class="key">{t.firmware}</span><span>{info.firmwareVersion || '—'}</span>
    </div>
    <div class="field">
      <span class="key">{t.webUi}</span><span>{info.webUiVersion || t.unknown}</span>
    </div>
    <div class="field">
      <span class="key">{t.uptime}</span><span>{duration(info.uptime)}</span>
    </div>
    {#if info.plotter}
      <div class="field">
        <span class="key">{t.setupState}</span>
        <span>{t.plotterStates[info.plotter.state] ?? info.plotter.state}</span>
      </div>
    {/if}

    <p class="hint">{t.statusHint}</p>
  {:else}
    <p class="hint">{t.loadingShort}</p>
  {/if}
</section>
