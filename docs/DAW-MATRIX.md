# DAW compatibility matrix

> Manual validation matrix run before each release. Results pinned to
> the GitHub Release notes for that tag.

Last automated CI pass: see latest run of [Build VST3](
https://github.com/smurz/zondel-bridge/actions/workflows/build-vst3.yaml)
and [Build CLAP](
https://github.com/smurz/zondel-bridge/actions/workflows/build-clap.yaml).

## Target hosts (v1)

### VST3

| Host                | Version       | Status   | Notes                                                                 |
|---------------------|---------------|----------|-----------------------------------------------------------------------|
| Reaper              | 7.x           | Targeted | Streamer favourite; tested first.                                     |
| Cubase / Nuendo     | 13 / 14       | Targeted | Strict bus-arrangement enforcement — Phase 1 validator covers it.     |
| Studio One          | 6 / 7         | Targeted | Inspects `kIsBypass` for the dedicated bypass UI.                     |
| Ableton Live        | 12.x          | Targeted | Doesn't support CLAP; this is the only Zondel option for Live users.  |
| FL Studio           | 2024.x        | Targeted |                                                                       |
| Bitwig Studio       | 5.x           | Targeted | Prefers CLAP; VST3 supported but lower priority for Bitwig users.     |

### CLAP

| Host                | Version       | Status      | Notes                                                                 |
|---------------------|---------------|-------------|-----------------------------------------------------------------------|
| Reaper              | 7.x           | Targeted    | Native CLAP support since v6.71.                                      |
| Bitwig Studio       | 5.x           | Targeted    | Primary CLAP host; reference implementation.                          |
| FL Studio           | **2024.1+**   | Targeted    | First version with CLAP support; older versions cannot load it.       |
| Studio One          | 7             | Targeted    | First Studio One with CLAP support.                                   |
| Ableton Live        | —             | Unsupported | Live 12 does not load CLAP plug-ins. Use VST3 instead.                |
| Cubase / Nuendo     | —             | Unsupported | Steinberg-native; CLAP not on the roadmap. Use VST3 instead.          |

## Smoke-test matrix (per host, per plug-in format)

Run all rows before publishing a release. Each cell records `PASS` /
`FAIL` / `N/A` / `?`. A `FAIL` blocks the release.

| Test                                          | Pass criterion                                                                            |
|-----------------------------------------------|-------------------------------------------------------------------------------------------|
| Plug-in scan                                  | Plug-in appears in host's effect list under the Zondel/Fx category.                       |
| Insert on a mono track                        | Plug-in instantiates; "● Connected" (Zondel running) or "● Disconnected" (Zondel off).    |
| Insert on a stereo track                      | Same — plug-in handles both channels (downmixed internally).                              |
| Surround track (5.1 / 7.1)                    | Host refuses to load OR shows the plug-in greyed out. **Never silently misroutes**.       |
| Audio processes audibly                       | With Zondel running, noise suppression is audible relative to bypass.                     |
| Toggle Bypass parameter                       | Audible difference (processed vs dry). Sample-accurate boundaries.                        |
| Change Pipe timeout                           | Latency to switch is at most one process block.                                           |
| Kill Zondel mid-stream                        | Plug-in passes through within 2 s; no silence, no crash. Status reads "● Disconnected".   |
| Restart Zondel                                | Plug-in reconnects within 2 s. Status reads "● Connected".                                |
| Change project sample rate (e.g. 48 → 96 kHz) | Plug-in re-allocates engine cleanly. No drop-outs longer than one block.                  |
| Save project, restart DAW, reload             | Bypass and Pipe timeout values restored.                                                  |
| Offline render / bounce                       | Plug-in passes through (does not block render). Audio quality may differ from realtime.   |
| Freeze track                                  | Same behaviour as offline render.                                                         |
| Duplicate instance                            | Each instance has independent state. No cross-contamination.                              |

## Per-host gotchas

### Reaper (VST3 + CLAP)
- Reaper exposes the generic param UI by default. The "Status" string-list
  param is rendered as a labelled dropdown — that's the intended UX, not
  a bug.
- The Bypass param shows up under Reaper's own bypass toggle thanks to
  `kIsBypass` / `CLAP_PARAM_IS_BYPASS`.

### Cubase / Studio One (VST3)
- Strict on `setBusArrangements` return values. Phase 1's validator
  pass covers this contract; if a future SDK update changes the
  arrangement enumeration, this is the first place to check.
- Studio One shows the Bypass param as a dedicated button (not in the
  generic param list).

### Ableton Live (VST3)
- Live caches plug-in metadata aggressively. After upgrading the plug-in
  binary, the user may need to delete `Live Database/` to pick up
  changes.
- Live 12 introduced Live Set inspector tooling that surfaces our
  Status param cleanly.

### FL Studio (VST3 + CLAP from 2024.1+)
- CLAP support landed in **FL Studio 2024.1**, not "FL 21+". Older
  versions cannot load `Zondel.clap`.
- FL Studio's wrapper UI is parameter-list-only by default; install a
  Bridge if a custom view is ever added in a future version.

### Bitwig Studio (CLAP preferred)
- Reference CLAP host. If something works in Bitwig but breaks in
  Reaper's CLAP loader, Reaper is usually the bug.
- Bitwig respects `clap_plugin_render.has_hard_realtime_requirement`
  and won't ask the plug-in to render offline.

## What we explicitly don't test (yet)

- **Pro Tools**. AAX format only; we don't ship AAX. Users can route
  through a Pro Tools AAX bridge if they really need it, but unsupported
  in v1.
- **macOS / Linux DAWs.** The pipe protocol is Windows-only.
- **DAWs older than the floor listed above.** Issues filed for older
  versions are closed as `not-supported`.
