<script>
  /**
   * JobMaker
   * The Jobs tab's converter: an SVG file in, a job in Mural's format out -
   * previewed on the drawing area with its travel moves, measured, timed,
   * then saved to the card through the usual upload (or downloaded).
   *
   * The work is the worker's (src/worker/pipeline.js); this is the form and
   * the picture. Every option change converts again, the newest answer wins.
   *
   * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
   * @version  1.0
   * @created  15.11.2026
   * @updated  15.11.2026
   */
  import * as api from './api.js';
  import { dict } from './i18n.svelte.js';
  import { parseSvg, convertInWorker } from './svgtree.js';

  /** plotter: /currentState's plotter; cardPresent; onSaved: reload the card list. */
  let { plotter, cardPresent, onSaved } = $props();

  const t = $derived(dict());

  let picker = $state(null);
  let canvas = $state(null);
  let fileName = $state('');
  let tree = $state.raw(null);
  let name = $state('');
  let result = $state.raw(null);
  let error = $state(null);
  let working = $state(false);
  let saving = $state(null);   // progress 0..1 while uploading
  let saved = $state(false);

  let sizeMode = $state('fit');
  let widthPercent = $state(80);
  let outlines = $state(true);
  let infill = $state(false);
  let hatchSpacing = $state(2);
  let hatchAngle = $state(45);

  // Numbers, not objects: /currentState is polled, and every poll brings a
  // new object - deriving those would reconvert the drawing on every poll.
  const areaWidth = $derived(plotter?.width ?? 0);
  const areaDepth = $derived(plotter?.maxY ?? 0);
  const homeX = $derived(plotter?.home?.x ?? 0);
  const homeY = $derived(plotter?.home?.y ?? 0);
  const drawSpeed = $derived(plotter?.drawSpeed || 15);
  const configured = $derived(areaWidth > 0 && areaDepth > 0);

  let warnings = $state.raw([]);
  let latest = 0;

  async function pick(event) {
    const file = event.currentTarget.files?.[0];
    event.currentTarget.value = '';
    if (!file) return;
    error = null;
    result = null;
    saved = false;
    try {
      tree = parseSvg(await file.text());
    } catch (err) {
      tree = null;
      error = t[`err_${err.message}`] ?? err.message;
      return;
    }
    fileName = file.name;
    const base = file.name.replace(/\.svg$/i, '').replace(/[^A-Za-z0-9._-]/g, '_').replace(/^\.+/, '');
    name = `${(base || 'drawing').slice(0, 60)}.txt`;
  }

  // Convert whenever the file or an option changes.
  $effect(() => {
    if (!tree || !configured) return;
    const area = { width: areaWidth, maxY: areaDepth };
    const home = { x: homeX, y: homeY };
    const options = {
      sizeMode,
      widthPercent: Number(widthPercent) || 100,
      outlines,
      infill,
      hatchSpacing: Math.max(0.3, Number(hatchSpacing) || 2),
      hatchAngle: Number(hatchAngle) || 0,
      drawSpeed
    };
    const id = ++latest;
    working = true;
    saved = false;
    convertInWorker(tree, area, home, options).then(
      (r) => {
        if (id !== latest) return;
        working = false;
        result = r.error ? null : r;
        error = r.error ? (t[`err_${r.error}`] ?? r.error) : null;
        warnings = r.warnings ?? [];
      },
      (err) => {
        if (id !== latest) return;
        working = false;
        result = null;
        error = err.message;
      }
    );
  });

  // The preview: the drawing area down to a little below the drawing (or
  // home, whichever is lower), strokes dark, travel faint, home a dot.
  $effect(() => {
    if (!canvas || !configured) return;
    const area = { width: areaWidth, maxY: areaDepth };
    const r = result;
    const css = getComputedStyle(canvas);
    const color = (v) => css.getPropertyValue(v).trim();
    const depth = Math.min(area.maxY, Math.max(r?.stats.heightMm ?? 0, homeY) * 1.1 + 10);
    const dpr = window.devicePixelRatio || 1;
    const w = canvas.clientWidth;
    const h = Math.round((w * depth) / area.width);
    canvas.width = Math.round(w * dpr);
    canvas.height = Math.round(h * dpr);
    canvas.style.height = `${h}px`;
    const ctx = canvas.getContext('2d');
    const s = (w / area.width) * dpr;
    ctx.setTransform(s, 0, 0, s, 0, 0);
    ctx.clearRect(0, 0, area.width, depth);
    ctx.lineWidth = 1 / s;
    ctx.strokeStyle = color('--border');
    ctx.strokeRect(0, 0, area.width, depth);
    if (r) {
      ctx.strokeStyle = color('--muted');
      ctx.setLineDash([4 / s, 4 / s]);
      ctx.globalAlpha = 0.5;
      ctx.beginPath();
      for (const [[x0, y0], [x1, y1]] of r.preview.travel) {
        ctx.moveTo(x0, y0);
        ctx.lineTo(x1, y1);
      }
      ctx.stroke();
      ctx.setLineDash([]);
      ctx.globalAlpha = 1;
      ctx.strokeStyle = color('--text');
      ctx.lineWidth = 1.2 / s;
      ctx.beginPath();
      for (const pts of r.preview.draw) {
        ctx.moveTo(pts[0][0], pts[0][1]);
        for (let i = 1; i < pts.length; i++) ctx.lineTo(pts[i][0], pts[i][1]);
      }
      ctx.stroke();
    }
    ctx.fillStyle = color('--accent');
    ctx.beginPath();
    ctx.arc(homeX, homeY, 4 / (s / dpr), 0, 2 * Math.PI);
    ctx.fill();
  });

  const validName = $derived(/^[A-Za-z0-9_-][A-Za-z0-9._-]{0,63}$/.test(name));

  async function save() {
    if (!result) return;
    saving = 0;
    error = null;
    try {
      await api.uploadJob(name, new Blob([result.text], { type: 'text/plain' }), (f) => (saving = f));
      saved = true;
      onSaved?.();
    } catch (err) {
      error = err.message;
    }
    saving = null;
  }

  function download() {
    const url = URL.createObjectURL(new Blob([result.text], { type: 'text/plain' }));
    const a = document.createElement('a');
    a.href = url;
    a.download = name;
    a.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  }

  const m = (mm) => (mm >= 1000 ? `${(mm / 1000).toFixed(1)} m` : `${Math.round(mm)} mm`);
  const duration = (s) => {
    const min = Math.round(s / 60);
    return min < 60 ? `${Math.max(1, min)} min` : `${Math.floor(min / 60)} h ${min % 60} min`;
  };
</script>

<section class="card">
  <h2>{t.makeTitle}</h2>
  {#if !configured}
    <p class="hint">{t.makeNeedsAnchor}</p>
  {:else}
    <input type="file" accept=".svg,image/svg+xml" bind:this={picker} onchange={pick} hidden />
    <div class="field">
      <span class={fileName ? 'key' : 'hint'}>{fileName || t.makeHint}</span>
      <button type="button" class="secondary" onclick={() => picker.click()}>{t.makePick}</button>
    </div>

    {#if tree}
      <div class="field">
        <label for="mk-size">{t.makeSize}</label>
        <select id="mk-size" bind:value={sizeMode}>
          <option value="fit">{t.makeSizeFit}</option>
          <option value="actual">{t.makeSizeActual}</option>
        </select>
      </div>
      {#if sizeMode === 'fit'}
        <div class="field">
          <label for="mk-width">{t.makeWidth}</label>
          <input id="mk-width" type="number" min="1" max="100" step="1" bind:value={widthPercent} />
        </div>
      {/if}
      <div class="field">
        <label for="mk-outlines">{t.makeOutlines}</label>
        <input id="mk-outlines" type="checkbox" bind:checked={outlines} />
      </div>
      <div class="field">
        <label for="mk-infill">{t.makeInfill}</label>
        <input id="mk-infill" type="checkbox" bind:checked={infill} />
      </div>
      {#if infill}
        <div class="field">
          <label for="mk-spacing">{t.makeSpacing}</label>
          <input id="mk-spacing" type="number" min="0.3" max="20" step="0.1" bind:value={hatchSpacing} />
        </div>
        <div class="field">
          <label for="mk-angle">{t.makeAngle}</label>
          <input id="mk-angle" type="number" min="-90" max="90" step="5" bind:value={hatchAngle} />
        </div>
      {/if}

      <canvas class="preview" bind:this={canvas}></canvas>
      {#if working}<p class="hint">{t.makeWorking}</p>{/if}

      {#if result}
        <p class="hint">
          {t.makeStats(
            Math.round(result.stats.widthMm),
            Math.round(result.stats.heightMm),
            m(result.stats.drawMm),
            m(result.stats.travelMm),
            result.stats.penLifts,
            duration(result.stats.seconds)
          )}
        </p>
        {#each warnings as w}
          <p class="hint">{t.makeWarnings[w.code]?.(w) ?? w.code}</p>
        {/each}
        <div class="field">
          <label for="mk-name">{t.makeName}</label>
          <input id="mk-name" type="text" maxlength="64" bind:value={name} />
        </div>
        {#if !validName}<p class="hint">{t.err_jobName}</p>{/if}
        {#if saving !== null}
          <div class="progress" role="progressbar" aria-valuenow={Math.round(saving * 100)}>
            <div class="bar" style="width: {Math.round(saving * 100)}%"></div>
          </div>
        {/if}
        <div class="field">
          <span class="hint">{saved ? t.makeSaved(name) : `${Math.round(result.stats.bytes / 1024)} kB`}</span>
          <span>
            <button type="button" class="secondary" onclick={download} disabled={!validName}>{t.jobDownload}</button>
            <button type="button" class="primary" onclick={save}
              disabled={!validName || !cardPresent || saving !== null || working}>{t.makeSave}</button
            >
          </span>
        </div>
      {/if}
    {/if}
    {#if error}<p class="banner">{error}</p>{/if}
  {/if}
</section>

<style>
  .preview {
    display: block;
    width: 100%;
    margin: 0.75rem 0;
    border-radius: var(--radius);
    background: var(--bg);
  }
</style>
