<script>
  /**
   * App
   * Shell of the configuration SPA: loads the device's settings once, then
   * hands them to the section shown by the selected tab.
   *
   * Which tabs exist is the device's answer, not this component's: four of
   * the eight sit behind expert mode, and until the device says otherwise it is
   * treated as locked. That is the safe way round - a firmware without
   * `/expert` at all answers 404, and the tabs stay away rather than being
   * offered against endpoints that refuse them.
   *
   * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
   * @version  1.0
   * @created  31.8.2026
   * @updated  31.8.2026
   */
  import { onMount } from 'svelte';
  import { fetchState, fetchExpert } from './lib/api.js';
  import { status } from './lib/status.svelte.js';
  import { dict } from './lib/i18n.svelte.js';
  import { appName, setAppName } from './lib/appname.svelte.js';
  import Status from './sections/Status.svelte';
  import Wifi from './sections/Wifi.svelte';
  import Ota from './sections/Ota.svelte';
  import Debug from './sections/Debug.svelte';
  import Hardware from './sections/Hardware.svelte';
  import Setup from './sections/Setup.svelte';
  import Jobs from './sections/Jobs.svelte';
  import Storage from './sections/Storage.svelte';
  import Expert from './sections/Expert.svelte';

  // Labels come from t.tabs, in this order. All but the first two are only
  // offered once the device is unlocked; t.tabs keeps all of them either way, so
  // the label of a tab is found by its place in this list rather than in the
  // visible one.
  const ALL_TABS = ['status', 'jobs', 'setup', 'wifi', 'hardware', 'ota', 'debug', 'storage'];
  const OPEN_TABS = 4;

  let active = $state('status');
  let device = $state(null);
  let loadError = $state(null);

  // Until the device says otherwise it is locked; see the note above.
  let expert = $state({ enrolled: false, unlocked: false, grace: 0, lockedOut: false });

  // Only relevant on narrow screens, where the tab row collapses into a menu.
  let menuOpen = $state(false);
  let navEl = $state(null);

  const t = $derived(dict());
  const tabIds = $derived(expert.unlocked ? ALL_TABS : ALL_TABS.slice(0, OPEN_TABS));
  const labelOf = (id) => t.tabs[ALL_TABS.indexOf(id)];
  // The expert screen is reachable by address and by nothing else.
  const activeLabel = $derived(tabIds.includes(active) ? labelOf(active) : t.expertTitle);

  function select(id) {
    active = id;
    menuOpen = false;
    // Leave the hash behind, or reloading the page would land back on it.
    if (location.hash) history.replaceState(null, '', location.pathname);
  }

  /**
   * The expert screen has no chip in the tab row - there is nothing there for
   * someone who has not gone looking, and a visible one would only invite
   * guessing. `#expert` in the address is the way in.
   */
  async function syncFromHash() {
    if (location.hash !== '#expert') return;

    active = 'expert';

    // Fetched again on the way in: the reset window is counting down, and a
    // value read when the page loaded would be stale by minutes.
    try {
      expert = await fetchExpert();
    } catch {
      /* keep what we have */
    }
  }

  /**
   * Takes the state the device answered with. Locking while an expert tab is
   * open would leave it on screen with every request behind it refused, so
   * step back to the front.
   */
  function applyExpert(next) {
    expert = next;
    if (!next.unlocked && ALL_TABS.indexOf(active) >= OPEN_TABS) active = 'status';
  }

  // Close the menu the way a menu is expected to close.
  function onWindowKeydown(event) {
    if (event.key === 'Escape') menuOpen = false;
  }

  function onWindowPointerdown(event) {
    if (menuOpen && navEl && !navEl.contains(event.target)) menuOpen = false;
  }

  // Heading, browser tab and home screen label all follow the device's own
  // name, which is the only thing distinguishing two of them on one network.
  // The WLAN tab updates it in place after a rename.
  $effect(() => {
    if (device?.hostname) setAppName(device.hostname);
  });

  async function load() {
    loadError = null;
    try {
      device = await fetchState();
    } catch (err) {
      loadError = err.message;
    }

    try {
      expert = await fetchExpert();
    } catch {
      // Not fatal, and not a reason to unlock anything: the default above
      // already says locked.
    }
  }

  onMount(async () => {
    await load();
    await syncFromHash();
  });
</script>

<svelte:window
  onkeydown={onWindowKeydown}
  onpointerdown={onWindowPointerdown}
  onhashchange={syncFromHash}
/>

<div class="app">
  <!--
    The nav sits inside the header so that a narrow screen can lay the two out
    as one bar - title left, menu button right - instead of stacking them.
  -->
  <header>
    <h1>{appName()}</h1>

    {#if device}
      <nav bind:this={navEl} class:open={menuOpen}>
        <!-- Shown instead of the row when the viewport is too narrow for it. -->
        <button
          type="button"
          class="menu-toggle"
          aria-expanded={menuOpen}
          aria-controls="tabs"
          onclick={() => (menuOpen = !menuOpen)}
        >
          <span class="burger" aria-hidden="true"></span>
          <span>{activeLabel}</span>
        </button>

        <div class="tabs" id="tabs">
          {#each tabIds as id (id)}
            <button
              type="button"
              aria-current={active === id ? 'page' : undefined}
              onclick={() => select(id)}
            >
              {labelOf(id)}
            </button>
          {/each}
        </div>
      </nav>
    {/if}
  </header>

  {#if device}
    {#if status.error}
      <p class="banner">{t.writeFailed} — {status.error}</p>
    {/if}

    {#if active === 'status'}
      <Status />
    {:else if active === 'jobs'}
      <Jobs />
    {:else if active === 'setup'}
      <Setup />
    {:else if active === 'wifi'}
      <Wifi />
    {:else if active === 'ota'}
      <Ota />
    {:else if active === 'hardware'}
      <Hardware />
    {:else if active === 'debug'}
      <Debug />
    {:else if active === 'storage'}
      <Storage />
    {:else}
      <Expert {expert} onchange={applyExpert} />
    {/if}
  {:else if loadError}
    <p class="centered">
      {t.deviceUnreachable} — {loadError}
      <br />
      <button type="button" onclick={load}>{t.retry}</button>
    </p>
  {:else}
    <p class="centered">{t.loading}</p>
  {/if}
</div>
