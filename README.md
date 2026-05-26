# Zondel Bridge

Multi-format audio bridge plugin that routes host audio through
[Zondel](https://zondel.net) for noise suppression, echo cancellation, EQ,
gating, and AGC. One shared C core (`zondel-core`), three host wrappers:

- **OBS Studio** plugin — `zondel-obs-plugin.dll`
- **VST3** plugin — `Zondel.vst3` (Reaper, Cubase, Studio One, Ableton Live, FL Studio, Bitwig)
- **CLAP** plugin — `Zondel.clap` (Reaper, Bitwig, FL Studio 2024.1+, Studio One 7)

> **All three plugins are thin clients.** Every algorithm lives in the Zondel
> desktop app. The plugin opens a Windows named pipe to Zondel, hands it
> 480-sample mono blocks, and writes the processed audio back into the host's
> buffer. If Zondel isn't running, the plugin passes audio through unchanged —
> streams and sessions never go silent.

---

## Status

| Format | Code   | CI            | Validator         | Released                                |
|--------|--------|---------------|-------------------|-----------------------------------------|
| OBS    | shipped | green on main | n/a               | `v0.1.0-beta.2` (2026-05-13, pre-release) |
| VST3   | shipped | green on main | Steinberg, 0 fail | *next tag*                              |
| CLAP   | shipped | green on main | clap-validator    | *next tag*                              |

The latest GitHub Release is [v0.1.0-beta.2](https://github.com/smurz/zondel-bridge/releases/latest),
which contains **only the OBS plugin** — the VST3 and CLAP wrappers were
merged after that tag. The next tagged release will publish all six
artefacts (zip + installer per format); see [docs/RELEASE-CHECKLIST.md](docs/RELEASE-CHECKLIST.md).

Targeted DAWs and the per-host smoke-test matrix live in
[docs/DAW-MATRIX.md](docs/DAW-MATRIX.md).

## Requirements

- Windows 10 1809 or newer (x64; ARM64 is best-effort in CI)
- A host that loads the relevant format:
  - **OBS Studio 31.1 or newer** for the OBS plugin
  - Any modern **VST3** or **CLAP** host (see [docs/DAW-MATRIX.md](docs/DAW-MATRIX.md))
- [Zondel](https://zondel.net/download) — the desktop app that does the actual DSP

The bridge plugins are free and MIT. Zondel is a separate commercial product.
The plugin does nothing useful without it; if you just want a free OBS noise
filter, OBS already ships RNNoise.

## Format support

The plugins handle **mono and stereo** sources at any common sample rate
(44.1 / 48 / 96 kHz, etc.):

- Stereo is downmixed to mono before processing (Zondel processes mono only),
  and the processed mono signal is broadcast back into both channels.
- Resampling to 48 kHz uses a 4-point Catmull-Rom cubic interpolator when the
  host project isn't already at 48 kHz.
- The host's variable callback sizes are chunked into the 480-sample blocks
  Zondel needs.

Sources with more than 2 channels (5.1, 7.1):

- **OBS:** passes audio through unchanged with a status message.
- **VST3:** rejected at bus-arrangement negotiation (host-correct behaviour);
  surround tracks see the plugin greyed out or unloadable.
- **CLAP:** same — no surround port declared.

Surround mixdown is planned for v2.

## Install — OBS Studio

### Option A — Installer (recommended)

1. Download `zondel-obs-plugin-vX.Y.Z-setup.exe` from the
   [latest release](https://github.com/smurz/zondel-bridge/releases/latest).
2. Close OBS if it's open.
3. Run the installer. It detects your OBS install and places files in:
   - `%ProgramFiles%\obs-studio\obs-plugins\64bit\zondel-obs-plugin.dll`
   - `%ProgramFiles%\obs-studio\data\obs-plugins\zondel-obs-plugin\`

Windows will show a SmartScreen warning because the installer is unsigned in
v1 (see [Known issues](#known-issues)). Click "More info" → "Run anyway".

### Option B — Manual (.zip)

1. Download `zondel-obs-plugin-X.Y.Z-windows-x64.zip` from the same release.
2. Close OBS.
3. Extract into your OBS install directory.

## Install — VST3

> Available in the next tagged release. Until then, build from source (below).

### Option A — Installer

1. Download `Zondel-vst3-vX.Y.Z-setup.exe`.
2. Close your DAW.
3. Run the installer. It writes the bundle to the canonical Windows VST3 path:
   `%CommonProgramFiles%\VST3\Zondel.vst3\`

### Option B — Manual (.zip)

1. Download `Zondel-vst3-vX.Y.Z-windows-x64.zip`.
2. Extract `Zondel.vst3` to `%CommonProgramFiles%\VST3\`.

## Install — CLAP

> Available in the next tagged release. Until then, build from source (below).

### Option A — Installer

1. Download `Zondel-clap-vX.Y.Z-setup.exe`.
2. Close your DAW.
3. Run the installer. It writes `Zondel.clap` to the canonical Windows CLAP path:
   `%CommonProgramFiles%\CLAP\Zondel.clap`

### Option B — Manual (.zip)

1. Download `Zondel-clap-vX.Y.Z-windows-x64.zip`.
2. Extract `Zondel.clap` to `%CommonProgramFiles%\CLAP\`.

## Use

1. Launch **Zondel**. Its icon should appear in the system tray.
2. In your host, add the plugin as an audio effect / filter:
   - **OBS:** right-click your mic → **Filters** → **+** → **Zondel Audio Processor**
   - **VST3 / CLAP DAW:** insert *Zondel* on the target track (under the
     Zondel / Fx category)
3. The Status indicator should read **● Connected**.

Parameters in v1:

- **Bypass** — toggle dry/processed. VST3 maps this to `kIsBypass`; CLAP maps
  it to `CLAP_PARAM_IS_BYPASS`, so the host's own bypass button drives it.
- **Pipe timeout (µs)** — default 5000. Raise to 10–20 ms if you hear glitches.
- **Status** — read-only, surfaced via the VST3 Data Exchange API and CLAP
  param-info string list.

All audio tuning happens inside the Zondel app. These plugins are just the pipe.

## Troubleshooting

**Status reads "Disconnected".** Zondel may have crashed or its pipe server
didn't start. Restart Zondel; the plugin reconnects within ~2 s.

**Audio sounds the same with and without the filter.** Toggle Bypass on/off —
you should hear a difference. If you don't, the format gate may have fired
(>2 channels). Check the Status indicator.

**Audio glitches or pops with the filter enabled.** The pipe round-trip is
exceeding the timeout. Open the plugin's Advanced section and raise *Pipe
timeout* to 10 or 20 ms.

**VST3 plugin shows up greyed out on a surround track.** Working as intended —
the plugin declines surround bus arrangements rather than silently
misrouting. Use it on a mono or stereo track.

**Host crashed when I added the plugin.** Please file an issue with your
host version, Zondel version, plugin version, and any host logs.

## How it works

```
Host source (any sample rate, mono or stereo, variable frame size)
   │
   ▼
[downmix to mono] → [cubic resampler → 48 kHz] → [480-sample ring buffer]
   │                                                       │
   │                                                       ▼
   │                                              \\.\pipe\Zondel → Zondel.App
   │                                                       │
   │                                                       ▼
   │                                              [480-sample ring buffer]
   ▼
[cubic resampler → host rate] ← [pull from ring]
   │
   ▼
[broadcast back to mono/stereo channels]
```

Everything above the pipe is `zondel-core` — a host-agnostic C library shared
by all three plugins. The OBS, VST3, and CLAP wrappers are ~10% format glue
around 90% shared core.

On any pipe error the plugin falls back to pass-through; three consecutive
failures open a 2-second circuit-breaker window before reconnecting.

The wire protocol is documented in [docs/PROTOCOL.md](docs/PROTOCOL.md) —
third parties are welcome to build their own bridges.

## Known issues

- **Installers are unsigned in v1.** Windows SmartScreen will warn. Click
  "More info" → "Run anyway". v2 will ship with an EV-signed installer.
- **Surround (5.1 / 7.1) sources are not processed.** OBS passes through; VST3
  and CLAP refuse arrangement. v2 will add surround downmix.
- **Voice-grade cubic resampler.** Sufficient for the SRC fallback path
  (most users are at 48 kHz already and don't trigger SRC). If real-world
  quality complaints surface, v1.1 will swap to Speex.
- **ARM64 Windows builds are best-effort** (CI matrix uses `continue-on-error`);
  hardened in v1.1.
- **No offline / freeze rendering.** VST3 refuses `kOffline`; CLAP declares
  hard-realtime via the render extension; DAWs that honour these will
  pass-through during bounces.
- **OBS plugin auto-update isn't supported by OBS itself.** Watch the repo's
  Releases page for updates.

## Build from source

```powershell
git clone https://github.com/smurz/zondel-bridge.git
cd zondel-bridge

# Default preset builds the OBS plugin only.
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo

# Opt into VST3 and/or CLAP. Each can be enabled independently.
cmake --preset windows-x64 -DBUILD_VST3=ON -DBUILD_CLAP=ON
cmake --build --preset windows-x64 --config Release
```

Requires Visual Studio 2022 or 2026, CMake 3.28+, Windows SDK. OBS
dependencies are fetched automatically via `buildspec.json`. The VST3 SDK
(`v3.8.0_build_66`) and CLAP headers (`1.2.7`) are pulled by CMake
`FetchContent` on first configure — no submodule setup needed.

Tests target `zondel-core` (the shared library) rather than each wrapper, so
one suite covers all three plugins.

## License

**MIT, repo-wide.** See [LICENSE](LICENSE).

The repo was previously GPLv2+ (matching OBS); it was relicensed to MIT on
2026-05-26 for cross-plugin consistency now that all three host SDKs (libobs,
VST3 SDK, CLAP) are MIT-compatible. The combined OBS + plugin **binary**
still falls under OBS's GPLv2+ terms when distributed together, but this
plugin's source remains MIT.

## Issues and support

- Plugin bugs: [GitHub issues](https://github.com/smurz/zondel-bridge/issues)
- Zondel app bugs or DSP feedback: <https://zondel.net/support>

---

*Not affiliated with the OBS Project, Steinberg, or the CLAP authors. Uses
the public libobs, VST3, and CLAP plugin APIs.*
