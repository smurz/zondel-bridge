# Zondel for OBS Studio

A free OBS Studio plugin that routes your audio through [Zondel](https://zondel.net)
for noise suppression, echo cancellation, EQ, gating, and AGC — without leaving OBS.

> **This plugin is a thin client.** All the audio processing happens inside the
> Zondel desktop app. You need Zondel installed and running for this plugin to do
> anything. Without Zondel, the plugin passes audio through unchanged.

---

## What it does

Adds a single audio filter — **Zondel Audio Processor** — that you can attach to
any OBS audio source. The filter sends each audio frame to Zondel over a local
named pipe, gets the processed audio back, and hands it to OBS. Round-trip
latency is well under 1 ms.

If Zondel isn't running, the filter passes audio through unchanged. Streams
never go silent.

### Format support

The plugin handles **mono and stereo** sources at any common sample rate
(44.1 / 48 / 96 kHz, etc.). It does this by:

- Downmixing stereo to mono before processing (Zondel processes mono only).
- Resampling to 48 kHz with a 4-point Catmull-Rom cubic interpolator if your
  OBS project isn't already at 48 kHz.
- Chunking OBS's variable callback sizes into the 480-sample blocks Zondel needs.

The processed mono signal is then broadcast back into both channels of stereo
sources. If your source has more than 2 channels (5.1, 7.1), the filter passes
audio through unchanged with a clear status message; surround support is
planned for v2.

## Requirements

- Windows 10 1809 or newer (x64; ARM64 is best-effort)
- OBS Studio 31.1 or newer
- [Zondel](https://zondel.net/download) — paid desktop app

The plugin itself is free and GPLv3+. Zondel is a separate commercial product.
This plugin does nothing useful without it; if you just want a free OBS noise
filter, OBS already includes RNNoise out of the box.

## Install

### Option A — Installer (recommended)

1. Download `zondel-obs-plugin-vX.Y.Z-setup.exe` from the
   [latest release](https://github.com/smurz/zondel-bridge/releases/latest).
2. Close OBS if it's open.
3. Run the installer. It will detect your OBS install and place files in:
   - `%ProgramFiles%\obs-studio\obs-plugins\64bit\zondel-obs-plugin.dll`
   - `%ProgramFiles%\obs-studio\data\obs-plugins\zondel-obs-plugin\`

Windows will show a SmartScreen warning because the installer is unsigned in
v1 (see [Known issues](#known-issues)). Click "More info" → "Run anyway".

### Option B — Manual (.zip)

1. Download `zondel-obs-plugin-vX.Y.Z-windows-x64.zip`.
2. Close OBS.
3. Extract into your OBS install directory.

## Use

1. Launch Zondel. Its icon should appear in the system tray.
2. In OBS, right-click your mic (or other audio source) → **Filters**.
3. Click **+** under "Audio/Video Filters" → choose **Zondel Audio Processor**.
4. The Status indicator should read **● Connected**.

All audio tuning happens inside the Zondel app. The OBS filter is just the pipe.

## Troubleshooting

**Status reads "Disconnected".** Zondel may have crashed or its pipe server
didn't start. Restart Zondel.

**My audio sounds the same with and without the filter.** Toggle Bypass on/off —
you should hear a difference. If you don't, the format gate may have fired;
check the status indicator. Common cause: your source has more than 2 channels,
which is not supported in v1.

**Audio glitches or pops only with this filter enabled.** The pipe round-trip
is exceeding the timeout. Open the filter's Advanced section and raise Pipe
timeout to 10 or 20 ms.

**OBS crashed when I added the filter.** Please file an issue with your OBS
version, Zondel version, plugin version, and `%APPDATA%/obs-studio/logs/`.

## How it works

```
OBS source (any sample rate, mono or stereo, variable frame size)
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
[cubic resampler → OBS rate] ← [pull from ring]
   │
   ▼
[broadcast back to mono/stereo channels]
```

The plugin is ~1000 lines of C. It opens a Windows named pipe to Zondel, sends
each 480-sample mono audio frame as float32 PCM with a 10-byte header (frame
size, sample rate, channel count), and writes the response back into OBS's
buffer. On any pipe error it falls back to pass-through.

The wire protocol is documented in [docs/PROTOCOL.md](docs/PROTOCOL.md) — third
parties are welcome to build their own bridges.

## Known issues

- **Installer is unsigned in v1.** Windows SmartScreen will warn. Click
  "More info" → "Run anyway". v2 will ship with an EV-signed installer.
- **Surround (5.1 / 7.1) sources pass through unprocessed.** v2 will add
  surround downmix.
- **Voice-grade cubic resampler.** Sufficient for the SRC fallback path
  (most users are at 48 kHz already and don't trigger SRC). If real-world
  quality complaints surface, v1.1 will swap to Speex.
- **OBS plugin auto-update** isn't supported by OBS itself; check this repo's
  Releases page or watch the repo for updates.

## Build from source

```powershell
git clone https://github.com/smurz/zondel-bridge.git
cd zondel-bridge
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
```

Requires Visual Studio 2022 or 2026, CMake 3.28+, Windows SDK. OBS dependencies
are fetched automatically via `buildspec.json`.

## License

GPLv3+. (OBS Studio itself is GPLv2+, which is forward-compatible — recipients
may use this plugin under GPLv3 alongside an OBS install.) See [LICENSE](LICENSE).

## Issues and support

- Plugin bugs: [GitHub issues](https://github.com/smurz/zondel-bridge/issues)
- Zondel app bugs or DSP feedback: <https://zondel.net/support>

---

*Not affiliated with the OBS Project. Uses the public OBS Studio plugin API.*
