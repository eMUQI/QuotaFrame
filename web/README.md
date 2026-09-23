# QuotaFrame website

The Astro static product page covers device selection, USB installation and desktop
setup. Hardware metadata and firmware manifests still come from the shared
`quotaframe_bridge.targets` registry and verified release assets.

## Build and check

From the repository root, with Node 22.22.2 or later and Python 3.11 or later:

```sh
npm ci --prefix web --ignore-scripts
npm run check --prefix web
npm run build --prefix web
npm test --prefix web
```

`build` generates the Chinese and English routes from `src/pages/`, copies the
shared styles, and bundles the browser application. It also bundles the pinned
ESP Web Tools 10.4.0 flashing engine and its transitive dependencies into
`web/dist/assets/flash-engine.js`. ESP32-S31 support uses
the official esptool-js source pinned to commit
`5f860b71218fb30aa33a3d039039c496862c360f` through an npm override.
The bundler resolves its TypeScript entry directly; installation requires no
dependency build scripts. No vendor dialog or
Material components are used. The build also emits dependency notices.

The build exports `devices.json` from `quotaframe_bridge.targets` using Python
(`PYTHON` can select the executable). This catalog contains display metadata only.
A preview `targets.json` explicitly has no firmware version or installable targets.
Assembly replaces that file with the verified Release version and firmware paths.

Assemble a self-contained site using the corresponding full images from a
release (replace the version and release directory):

```sh
python scripts/assemble_web_flasher.py \
  --version 1.2.3 \
  --release-assets /path/to/release-assets \
  --web-root web \
  --vendor-root web/node_modules/esp-web-tools \
  --output /tmp/quotaframe-site
python -m http.server 8000 --bind 127.0.0.1 --directory /tmp/quotaframe-site
```

Build before assembly and UI tests. The assembler publishes the complete
`web/dist/` tree, then adds verified firmware and manifests. The page references assembled
asset paths; serving `web/` directly does not provide a working installation page. Web Serial
requires a supported desktop browser and HTTPS (or localhost).

## Website maintenance

Edit `src/components/HomePage.astro` for the shared homepage, `src/styles.css` for shared styles,
and `src/app.js` for browser interactions. The page uses an unprocessed module
script to preserve its relative asset URLs and existing initialization behavior.
Astro HTML compression is disabled to preserve whitespace in the original layout.
There is no client-side router; navigation uses normal document loads.

Add future routes under `src/pages/` as Astro or Markdown pages. Documentation
and changelog content can share the same site build.
Keep firmware manifests and target metadata generated from the target registry.

For a local preview after building, run `npm run preview --prefix web`.
Device cards and selection work without downloading firmware. Installation stays
disabled with a preview notice; the flashing engine is not loaded. If Release
metadata fails to load in an assembled site, device cards remain available and
installation stays disabled. Real installation requires the assembled site above.

## Languages

Chinese is served at `/`; English is served at `/en/`. Both routes render the
same `HomePage.astro` component. Chinese source text is the translation key;
`src/i18n/en.js` holds its English translation. Keep each `t(...)` call and its
translation together when changing copy. Interpolated messages use named
placeholders such as `{current}` and `{next}`. Do not translate device IDs,
firmware paths or manifest fields.

Missing English page translations fail rendering. The build also verifies
translations for all maintained device descriptions. Tests cover runtime
messages, English installation paths and language-switch locking. The browser
loads the shared catalog and firmware files from the site root in both languages.
The native language selector shows the current language, preserves the current
section when switching, and is disabled while installing.
On the Chinese homepage, a small script in the head selects the saved manual
language preference, or the browser's primary language on a first visit. Chinese
browser locales stay on `/`; other languages go to `/en/`. Direct visits to
`/en/` remain English. Redirects retain query parameters and the current section.
Manual selections are saved in localStorage. If storage is unavailable, a `lang`
query parameter preserves the explicit choice and prevents a redirect loop.

`astro.config.mjs` defines the production URL used for canonical and alternate
language links. Update it if the public domain changes.

## Installation contract

- `app.js` owns selection, confirmation, localized progress/error text and desktop
  platform instructions. Device selection and erase settings are locked while
  the engine owns the port; preparation details collapse during installation.
- `flash-engine.js` binds the pinned upstream engine to `flash-session.js`.
  The session validates the manifest, downloads every image it declares and
  checks it against the SHA-256 the assembler recorded before the engine can
  write anything; a manifest without a digest is rejected. Verified bytes are
  handed to the engine as object URLs, so each image is fetched once. The
  session preserves upstream failures and releases its transport even if
  resetting the device throws. Cleanup uses the loader
  exposed by ESP Web Tools 10.4.0; upgrades must recheck this integration.
- A browser chooser cancellation starts no write. Manifest/download failure,
  initialization failure, unsupported chips and write failure have distinct
  recovery guidance. Cleanup failure disables retry until the page is reloaded.
- Writing 100% starts the finalization state. Success requires the engine to
  report completion and resolve after port cleanup. This does not establish
  that the application firmware has booted or that BLE pairing works.
- Full images are still installed at address `0x0`. Skipping whole-chip erase
  does not promise preservation of configuration. Existing users should prefer
  desktop OTA for routine updates.

## Visual assets and verification

Device photos are from official Waveshare, M5Stack and Espressif documentation; sources and
attribution are in `assets/devices/README.md`. The hero and Bridge demo screens use synthetic data: `src/components/HomePage.astro` carries a `screen-template` that
reproduces the AMOLED panel in device pixels, scaled to its container, and
`app.js` only switches pages. Geometry, colours and the screensaver percentage
rules follow `firmware/targets/esp32_s3_touch_amoled_216/main/display_ui.cpp`
and `firmware/components/usage_panel_state/screensaver_usage.cpp`; the values shown are sample data and are labeled as
such.

Unit tests cover UI gating, cancellation, progress, retry and adapter cleanup.
Browser fixtures with simulated engine events verify presentation only. Real
USB connection, erase/write/reset, physical startup and BLE onboarding require
hardware verification before claiming end-to-end installation success.

## Cloudflare Pages Git deployment

Connect the Cloudflare Pages project to `eMUQI/QuotaFrame` only after its first
complete stable Release is available. Configure both production and preview
builds with repository root `/`, build
command `python3 scripts/build_site.py`, and output directory `pages-site`.
Use Node.js 22.22.2 or later and Python 3.11 or later. Enable automatic production
deployments for `main`, preview deployments for all desired branches, and build
watch paths `*` with no exclusions. Pushes to `main` update the production URL;
other enabled branches receive preview deployments. Local commits require a push.

To build with a specific published Release, use:

```sh
python scripts/build_site.py --version v1.2.3
```

Both `1.2.3` and `v1.2.3` are accepted; an explicitly selected prerelease is also
supported. The version must exist with all required full images and SHA-256
checksums. The build never falls back to another version if an asset is missing.
Omit `--version` for the default latest stable behavior.

By default, the build uses the checked-out website source and resolves the latest published
stable Release from `eMUQI/QuotaFrame` once through GitHub's `/releases/latest`
redirect, without calling the REST API or requiring a token. It downloads the
registry's full firmware images from that exact version, verifies SHA-256 checksums, and assembles the site.
Missing assets or failed checksums fail the deployment. The displayed firmware
version can remain unchanged across website commits; it is not the source version.

Create a Cloudflare Pages Deploy Hook targeting `main` and save its URL as the
`eMUQI/QuotaFrame` repository Actions secret `CLOUDFLARE_PAGES_DEPLOY_HOOK`. The release
workflow calls it after a stable Release is published and verified. This rebuild
picks up firmware published after the initial Git push. Without this secret,
Git pushes still deploy, but a new Release requires a manual rebuild. Treat the
hook URL as a secret.
