/**
 * vite.config
 * Build and dev-server configuration of the web UI. Builds web/ into data/,
 * the LittleFS image flashed to the device.
 *
 * Two pages come out of one build, and that is the whole point of the setup
 * portal looking the way it does:
 *
 *   index.html    the configuration SPA, served once the device is on a network
 *   portal.html   the setup portal, served from the device's own access point
 *
 * They share app.css, the i18n dictionaries and the network-list component, so
 * "same look and feel" is not a second stylesheet kept in step by hand - it is
 * literally the same code.
 *
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  31.8.2026
 * @updated  31.8.2026
 */
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';

// The device's REST endpoints. In dev these are proxied to the mock server
// (`npm run mock`); on the device the pages are served from the same origin.
const API_ROUTES = [
  '/currentState',
  '/restart',
  '/hostname',
  '/log',
  '/expert',
  '/fs',
  '/nvs',
  '/wifi',
  '/ota',
  '/portal',
  '/hardware',
  // Built by the device, not shipped in the image: it carries the device name.
  '/manifest.webmanifest'
];

const pkg = JSON.parse(readFileSync(new URL('./package.json', import.meta.url)));

/*
 * The release workflow passes the version the firmware was stamped with, so
 * both halves of a build report the same thing. package.json is the fallback
 * for a plain local build.
 */
const VERSION = process.env.WEB_UI_VERSION || pkg.version;

/**
 * Puts version.json into the filesystem image. The firmware reads it at boot
 * and reports it through /ota/status, so the update tab can show which build
 * of the web UI is actually in flash - firmware and UI are separate images and
 * are not necessarily updated together.
 */
function versionFile() {
  return {
    name: 'scribbler-version',
    generateBundle() {
      this.emitFile({
        type: 'asset',
        fileName: 'version.json',
        source: JSON.stringify({ version: VERSION, built: new Date().toISOString() })
      });
    }
  };
}

export default defineConfig({
  plugins: [svelte(), versionFile()],
  root: 'web',
  build: {
    // Built output is the LittleFS image uploaded via `pio run -t uploadfs`.
    outDir: '../data',
    emptyOutDir: true,
    rollupOptions: {
      input: {
        main: resolve(import.meta.dirname, 'web/index.html'),
        portal: resolve(import.meta.dirname, 'web/portal.html')
      }
    }
  },
  server: {
    proxy: Object.fromEntries(
      API_ROUTES.map((route) => [route, 'http://localhost:8080'])
    )
  }
});
