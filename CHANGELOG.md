# Changelog

All notable changes to this project will be documented here.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [0.1.0-beta.3] - 2026-05-26

First release to ship the **VST3** and **CLAP** plugins alongside the OBS
Studio plugin. All three are thin clients over the same `zondel-core`
named-pipe protocol; the repo was renamed `zondel-obs-plugin` →
`zondel-bridge` and relicensed GPLv2+ → MIT to reflect the broader scope.

### Added
- **VST3 plugin** (`Zondel.vst3`, x64). AudioEffect + EditController on the
  shared `zondel::Engine`. Bus-arrangement negotiation (mono+mono /
  stereo+stereo only; surround declined host-correctly), state persistence
  via `getState`/`setState`, PDC via `getLatencySamples`, Bypass flagged
  `kIsBypass`, Pipe-timeout `RangeParameter`, live Status surfaced through
  the VST3 Data Exchange API (SDK >= 3.7.9). Passes the Steinberg validator
  with zero failures. Pinned to VST3 SDK `v3.8.0_build_66` (MIT).
- **CLAP plugin** (`Zondel.clap`, x64). Native C++ wrapper on the same
  engine. Extensions: audio-ports, params (Bypass `CLAP_PARAM_IS_BYPASS`,
  Pipe timeout), state (little-endian blob, cross-format-compatible with
  VST3), latency, render (hard-realtime). Passes clap-validator.
- **Shared C++ engine** (`src/shared-cpp/ZondelEngine`) — single
  realtime-safe audio path linked by both VST3 and CLAP. Allocation-free
  `process()`, one-chunk PDC pre-fill, bounded chunk drain, lock-free
  status snapshot.
- CI workflows `build-vst3.yaml` + `build-clap.yaml` (build -> validator ->
  engine/realtime tests -> zip + Inno Setup installer). `release.yaml`
  fans out to all three plugins and publishes six artefacts per tag.
- Installers `installer-vst3.iss` (-> `%CommonProgramFiles%\VST3\`) and
  `installer-clap.iss` (-> `%CommonProgramFiles%\CLAP\`).
- `docs/PLAN-VST3-CLAP.md`, `docs/DAW-MATRIX.md`, `docs/RELEASE-CHECKLIST.md`.
- `test/test-zondel-engine.cpp` and `test/test-realtime-budget.cpp`.

### Changed
- Repo restructured into `src/core/` (shared C), `src/obs/`, `src/shared-cpp/`,
  `src/vst3/`, `src/clap/`. OBS plugin binary identity (`zondel-obs-plugin.dll`)
  and behaviour unchanged.
- Relicensed GPLv2+ -> MIT repo-wide (VST3 SDK is MIT since 2025; libobs's
  GPL is satisfied by MIT compatibility).

### Fixed
- pipe-client (Win32): reuse a single OVERLAPPED event instead of allocating
  kernel handles per round-trip; cancel + drain pending I/O on timeout
  (prevents the kernel writing to freed stack buffers); apply one deadline
  across the whole round-trip instead of per-op; honour the response status
  byte.
- Engine: bail out of the chunk-drain loop after the first pipe failure per
  block (was risking tens of ms of audio-thread stall); recv-ring pre-fill +
  capacity scaled to block size (no mid-stream zero-pad clicks, no overflow
  at large blocks); clear rings on bypass/back-off transitions (no stale
  audio on resume); atomic status snapshot; `_viable` guards on alloc
  failure.
- VST3: atomic Bypass / Pipe-timeout (no race with state save during
  playback); `\u` Unicode escapes for parameter labels (no mojibake);
  silence-flag handling preserves host optimisation only in bypass.
- CI/installers: build-dir + build-config parameterised so local
  (`build_x64`/RelWithDebInfo) and CI (`build_vst3`/Release) both resolve;
  installer build failures now fail CI instead of shipping empty releases;
  release signing-step condition fixed.

## [0.1.0-beta.2] - 2026-05-13

Patch beta. The `v0.1.0-beta.1` tag was published but its release-packaging
workflow failed (artifact-download pattern referenced `displayName` instead
of `name`, so no artifacts were downloaded). No GitHub Release was created
for beta.1; this beta supersedes it.

### Fixed
- `release.yaml`: artifact-pattern uses literal repo name `zondel-obs-plugin`
  not the `displayName` output, and added a defensive guard with a clearer
  error if the pattern matches nothing.

## [0.1.0-beta.1] - 2026-05-13

First public beta. The plugin's source ID (`zondel_audio_filter`) is **not yet
frozen** during the v0.x series — scene-collection compatibility guarantees
begin at v1.0.0.

### Added
- OBS audio filter `zondel_audio_filter` (display: "Zondel Audio Processor").
- Win32 named-pipe client targeting `\\.\pipe\Zondel` with overlapped I/O,
  5 ms hard timeout, close-and-reconnect on timeout.
- Properties UI: Bypass toggle, Status indicator, Open Zondel button, Advanced
  group (pipe endpoint, pipe timeout 1-20 ms, default 5 ms).
- Format adaptation in the plugin: handles mono + stereo at 8/16/24/32/44.1/48/96 kHz
  via downmix and 4-point Catmull-Rom cubic resampler with 480-sample chunking
  ring buffer.
- Back-off circuit breaker: 3 consecutive pipe failures → 2 s pass-through window.
- End-user `README.md` (install, use, troubleshooting, build-from-source).
- Public `docs/PROTOCOL.md` documenting the wire protocol.
- Inno Setup installer (`zondel-obs-plugin-vX.Y.Z-setup.exe`).
- GitHub Actions release workflow producing `.zip`, `.exe`, and `SHA256SUMS.txt`
  on every `v*.*.*` tag.
- Branch protection on `main`: PR-required, `check-format` + `build-project`
  must pass, linear history, 1 approval on PRs touching `src/`.

### Known limitations
- Windows-only. Linux/macOS scaffolding exists as stubs but is not built.
- Sources with more than 2 channels (5.1/7.1) are passed through unprocessed
  (v2 will add surround downmix).
- Resampler is voice-grade cubic; if quality complaints surface in beta we may
  swap in Speex for v1.1.
- Installer is **unsigned**; SmartScreen will warn users.
- ARM64 Windows builds are best-effort (matrix `continue-on-error`); hardened
  in v1.1.
