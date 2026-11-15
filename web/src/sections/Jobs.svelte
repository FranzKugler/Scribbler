<script>
  /**
   * Jobs
   * What is on the card, and drawing it: upload a job file, start it, watch
   * it, pause or stop it.
   *
   * Job files are Mural's format (src/JobFormat.h) - for now made elsewhere,
   * e.g. by Mural's own web UI; turning an SVG into one here is phase 7.
   *
   * Two things are polled: /currentState for the running job (fast, while one
   * runs) and /jobs for the card (only after something changed it - listing a
   * card is not free, and nothing else writes to it).
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

  const POLL_MS = 700;

  let p = $state(null);
  let card = $state(null);
  let error = $state(null);
  let busy = $state(false);

  let picker = $state(null);
  let uploading = $state(null);   // { name, progress } while an upload runs
  let uploadError = $state(null);
  let speed = $state(null);

  let timer = null;
  let inFlight = false;

  async function loadState() {
    if (inFlight) return;
    inFlight = true;
    try {
      p = (await api.fetchState()).plotter;
      if (speed === null) speed = p.drawSpeed;
      error = null;
    } catch (err) {
      error = err.message;
    } finally {
      inFlight = false;
    }
  }

  async function loadCard() {
    try {
      card = await api.fetchJobs();
    } catch (err) {
      error = err.message;
    }
  }

  onMount(() => {
    loadState();
    loadCard();
    timer = setInterval(loadState, POLL_MS);
  });
  onDestroy(() => clearInterval(timer));

  async function act(call, refreshCard = false) {
    busy = true;
    await call();
    busy = false;
    loadState();
    if (refreshCard) loadCard();
  }

  /** A file name the device accepts: [A-Za-z0-9._-], no leading dot, 64 at most. */
  function jobName(fileName) {
    const cleaned = fileName.replace(/[^A-Za-z0-9._-]/g, '_').replace(/^\.+/, '');
    return (cleaned || 'job.txt').slice(0, 64);
  }

  async function upload(event) {
    const file = event.currentTarget.files?.[0];
    event.currentTarget.value = '';
    if (!file) return;
    const name = jobName(file.name);
    uploadError = null;
    uploading = { name, progress: 0 };
    try {
      await api.uploadJob(name, file, (f) => (uploading = { name, progress: f }));
    } catch (err) {
      uploadError = err.message;
    }
    uploading = null;
    loadCard();
  }

  const running = $derived(p && (p.state === 'plotting' || p.state === 'paused'));
  const percent = $derived(p?.job && p.job.total > 0 ? Math.min(100, (p.job.done / p.job.total) * 100) : 0);
  const kb = (bytes) => `${(bytes / 1024).toFixed(bytes < 10240 ? 1 : 0)} kB`;
  const mb = (bytes) => `${(bytes / 1048576).toFixed(0)} MB`;
</script>

{#if error && !p}
  <section class="card"><p class="banner">{t.jobsUnavailable} — {error}</p></section>
{:else if !p}
  <section class="card"><p class="hint">{t.loadingShort}</p></section>
{:else}
  <section class="card">
    <h2>{t.jobsNow}</h2>
    {#if p.job}
      <div class="field">
        <span class="key">{p.job.name}</span>
        <span>{t.jobPhases[p.job.phase] ?? p.job.phase}{p.state === 'paused' ? ` · ${t.jobPaused}` : ''}</span>
      </div>
      {#if p.job.error}
        <p class="banner">{t.jobFailed(t[`err_${p.job.error}`] ?? p.job.error, p.job.line)}</p>
      {:else}
        <div class="progress" role="progressbar" aria-valuenow={Math.round(percent)}>
          <div class="bar" style="width: {percent}%"></div>
        </div>
        <p class="hint">{t.jobProgress(Math.round(p.job.done), Math.round(p.job.total), Math.round(percent))}</p>
      {/if}
    {:else}
      <p class="hint">{p.state === 'ready' ? t.jobsIdle : t.jobsNotReady}</p>
    {/if}
    <div class="field">
      <span>
        {#if p.state === 'plotting'}
          <button type="button" class="secondary" disabled={busy} onclick={() => act(api.pauseJob)}>{t.jobPause}</button>
        {:else if p.state === 'paused'}
          <button type="button" class="secondary" disabled={busy} onclick={() => act(api.resumeJob)}
            >{t.jobResume}</button
          >
        {/if}
        {#if running}
          <button type="button" class="secondary" disabled={busy} onclick={() => act(api.stopJob)}>{t.jobStop}</button>
        {/if}
      </span>
      <button type="button" class="primary" onclick={() => act(api.stopAll)}>{t.setupStop}</button>
    </div>
    <p class="hint">{t.jobStopHint}</p>
  </section>

  <section class="card">
    <h2>{t.jobsCard}</h2>
    {#if !card}
      <p class="hint">{t.loadingShort}</p>
    {:else if !card.card.present}
      <p class="banner">{t.jobsNoCard}</p>
    {:else}
      <p class="hint">{t.jobsCardUsage(mb(card.card.used), mb(card.card.total))}</p>
      {#if card.jobs.length === 0}
        <p class="hint">{t.jobsEmpty}</p>
      {/if}
      {#each card.jobs as job (job.name)}
        <div class="field">
          <span class="key">{job.name} <span class="hint">{kb(job.size)}</span></span>
          <span>
            <button type="button" class="primary" disabled={busy || p.state !== 'ready' || p.moving}
              onclick={() => act(() => api.startJob(job.name))}>{t.jobStart}</button
            >
            <button type="button" class="secondary" onclick={() => window.open(api.jobUrl(job.name))}
              >{t.jobDownload}</button
            >
            <button type="button" class="secondary" disabled={busy || (running && p.job?.name === job.name)}
              onclick={() => confirm(t.jobDeleteConfirm(job.name)) && act(() => api.deleteJob(job.name), true)}
              >{t.jobDelete}</button
            >
          </span>
        </div>
      {/each}
      {#if card.truncated}<p class="hint">{t.jobsTruncated}</p>{/if}

      <input type="file" accept=".txt,text/plain" bind:this={picker} onchange={upload} hidden />
      {#if uploading}
        <div class="progress" role="progressbar" aria-valuenow={Math.round(uploading.progress * 100)}>
          <div class="bar" style="width: {Math.round(uploading.progress * 100)}%"></div>
        </div>
        <p class="hint">{t.jobUploading(uploading.name)}</p>
      {:else}
        <div class="field">
          <span class="hint">{t.jobsUploadHint}</span>
          <button type="button" class="secondary" onclick={() => picker.click()}>{t.jobUpload}</button>
        </div>
      {/if}
      {#if uploadError}<p class="banner">{uploadError}</p>{/if}
    {/if}
  </section>

  <section class="card">
    <h2>{t.jobsSpeedTitle}</h2>
    <div class="field">
      <label for="speed">{t.jobsSpeed}</label>
      <input id="speed" type="number" min="1" max="100" step="1" bind:value={speed} />
      <button type="button" class="secondary" disabled={busy || speed === p.drawSpeed}
        onclick={() => act(() => api.setDrawSpeed(speed))}>{t.setupSave}</button
      >
    </div>
    <p class="hint">{t.jobsSpeedHint}</p>
  </section>
{/if}
