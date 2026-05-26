# VST3 + CLAP implementation plan (v2)

> Status: planning — not started.
> Owner: smurz.
> Target: ship Zondel as a VST3 plugin and a CLAP plugin in addition to
> the existing OBS Studio plugin, all from this repo, all on top of the
> `zondel-core` static library.
>
> **v2 changelog (2026-05-26):** revised after Codex peer review. Major
> changes: VST3 SDK relicensed to MIT (license rationale rewritten); bus
> arrangement negotiation moved to Phase 1; state persistence and PDC
> moved to Phase 2 (foundation, not afterthought); Data Exchange API
> replaces per-block `IMessage`; realtime budget defined; `ZondelEngine`
> goes in `src/shared-cpp/` from day one; effort estimate adjusted to
> 15–25 focused days. Original v1 lives in git history at the parent
> commit of this file.

## Why

Zondel ships as a desktop DSP app today. The OBS plugin (already in this
repo) lets streamers route OBS audio through Zondel. The same
`zondel-core` that powers the OBS plugin — pipe IPC, ring-buffered
chunking, cubic resampling, stereo↔mono downmix, connection-state
machine, auto-launch — is host-agnostic. Wrapping it in a VST3 entry
point unlocks every major DAW (Reaper, Cubase, Studio One, Ableton Live,
FL Studio, Bitwig). Wrapping it in CLAP additionally unlocks the
permissive-license path for Reaper, Bitwig, FL Studio 2024.1+, and
Studio One 7.

Both plugins are ~10% format glue around 90% shared core. Adding them
is linear work, not a research project — provided the foundational VST3
contracts (bus arrangement, state persistence, PDC, threading) are
respected from Phase 1, not bolted on later.

## Scope

### v1 (this plan)

- Windows x64 only. ARM64 is best-effort behind `continue-on-error` in CI,
  same posture as the OBS plugin's CI matrix today.
- Mono and stereo I/O. Multi-channel (5.1/7.1) is **not declared at all**
  (no surround bus arrangement) — hosts that ask for it get rejected at
  arrangement negotiation, which is the VST3-correct way to signal
  unsupported. (v1 of the OBS plugin detected `>2` channels inside
  `process()` and passed through; that's an OBS-ism that doesn't
  translate to VST3.)
- Pass-through fallback when Zondel is not running. No splash, no modal —
  just a "Disconnected" status string and unmodified audio out.
- Realtime-only. Offline bounce in DAWs that respect `processMode`
  (Cubase, Nuendo) will refuse to enter `kOffline`. CLAP plugins declare
  no offline rendering capability via the render extension.
- Parameters: **Bypass** (bool, flagged `kIsBypass`), **Pipe timeout
  (µs)** (int, default 5000). Status text is read-only, surfaced via
  the controller through the Data Exchange API.
- No internal DSP. Every algorithm lives in the Zondel app; this plugin
  is a transport layer.

### Out of scope (future v2)

- macOS / Linux. Pipe protocol is Windows-named-pipe today.
- AudioUnit (macOS-native format).
- Surround mixdown.
- Per-DAW automation curves for tuning parameters (the Zondel app owns
  tuning; the plugin parameters are limited to transport behaviour).
- Steinberg / Apple notarisation. v1 ships unsigned, same as OBS plugin
  v1.
- Async worker thread for pipe IPC. v1 stays synchronous on the audio
  thread with a hard realtime budget; if the budget proves untenable
  in real-world DAWs, v2 introduces a worker thread + lock-free queue.

## Constraints from the existing repo

These are not negotiable and shape every decision below.

1. **`zondel-obs-plugin.dll`'s identity must not change.** OBS looks for
   it by name. The OBS target's `OUTPUT_NAME` stays `zondel-obs-plugin`;
   only new artefacts get new names.
2. **`zondel-core` is the truth.** Every plugin links it; no plugin
   duplicates ring-buffer / resampler / pipe-client logic.
3. **The pipe wire protocol is frozen at v1** (see [PROTOCOL.md](PROTOCOL.md)).
4. **Tests target `zondel-core`, not the wrappers.** Existing tests in
   `test/` already do this.
5. **License is MIT repo-wide.** Matches OBS Studio. The VST3 SDK is
   **MIT-licensed since 2025** (see "License rationale" below) so it
   does not constrain our choice; we keep MIT for cross-plugin
   consistency. CLAP is MIT — same story, no constraint.

## License rationale

The VST3 SDK at <https://github.com/steinbergmedia/vst3sdk> is
**MIT-licensed** as of the current `LICENSE.txt` on master (copyright
2025, Steinberg Media Technologies GmbH). It is no longer dual-licensed
under GPLv3 / proprietary as in previous years. This means:

- Our VST3 plugin can be any license. We pick **MIT** to match the
  existing OBS plugin and avoid mixed-license confusion across the
  repo.
- CLAP headers are MIT (already permissive). Same story.
- The OBS Studio plugin is MIT (forced by libobs's MIT). The
  repo-wide MIT choice is driven by OBS, not by VST3/CLAP.

Pin VST3 SDK to **`v3.8.0_build_66`** (Oct 20, 2025) — the first
release that ships with MIT licensing throughout.

## Repo layout target

```
zondel-bridge/
├── src/
│   ├── core/                   ← unchanged; pure C, the static library every plugin links
│   ├── obs/                    ← unchanged; ships today
│   ├── shared-cpp/             ← NEW; ZondelEngine (C++ wrapper around zondel-core)
│   │   ├── CMakeLists.txt
│   │   ├── ZondelEngine.h
│   │   └── ZondelEngine.cpp
│   ├── vst3/                   ← NEW
│   │   ├── CMakeLists.txt
│   │   ├── ZondelProcessor.h/.cpp
│   │   ├── ZondelController.h/.cpp
│   │   ├── ZondelIDs.h
│   │   ├── ZondelStatusReceiver.h/.cpp   ← IDataExchangeReceiver in controller
│   │   └── factory.cpp
│   └── clap/                   ← NEW (C++, to share ZondelEngine)
│       ├── CMakeLists.txt
│       ├── zondel-clap.cpp     ← clap_plugin_t v-table
│       └── factory.cpp         ← clap_entry
├── third_party/                ← NEW (gitignored; populated by CMake FetchContent)
│   ├── vst3sdk/                ← Steinberg VST3 SDK 3.8.0_build_66
│   └── clap/                   ← free-audio/clap headers
├── test/
│   ├── (existing core tests)
│   ├── test-zondel-engine.cpp  ← NEW; integration test, format-agnostic
│   └── test-realtime-budget.cpp ← NEW; measures pipe round-trip vs budget
├── docs/
│   ├── PROTOCOL.md
│   ├── PLAN-VST3-CLAP.md       ← this file
│   └── DAW-MATRIX.md           ← NEW (Phase 8)
└── installer/
    ├── installer.iss
    ├── installer-vst3.iss      ← NEW
    └── installer-clap.iss      ← NEW
```

## Architecture

```
                          ┌────────────────────────────────┐
                          │       zondel-core (.lib, C)     │
                          │  pipe IPC · ring buffer · SRC  │
                          │   downmix · state · launch     │
                          └───────────────┬────────────────┘
                                          │ linked statically
                       ┌──────────────────┴──────────────────┐
                       ▼                                     ▼
            ┌──────────────────┐                  ┌──────────────────┐
            │  src/obs/        │                  │  src/shared-cpp/ │
            │  (C, OBS-only)   │                  │  ZondelEngine    │
            └──────────────────┘                  │  (C++ class)     │
                       │                          └────────┬─────────┘
                       │                                   │ linked statically
                       │                ┌──────────────────┴──────────────────┐
                       │                ▼                                     ▼
                       │      ┌──────────────────┐                  ┌──────────────────┐
                       │      │  src/vst3/       │                  │  src/clap/       │
                       │      │  ZondelProcessor │                  │  zondel-clap.cpp │
                       │      │  ZondelController│                  │  factory.cpp     │
                       │      │  factory.cpp     │                  └──────────────────┘
                       │      └──────────────────┘                           │
                       ▼                │                                    ▼
       zondel-obs-plugin.dll            ▼                              Zondel.clap
                                  Zondel.vst3
```

`ZondelEngine` is the single C++ class that owns one plugin instance's
per-channel pipeline: input ring buffer, output ring buffer, resampler
pair, pipe client, state machine, latency model. It lives in
`src/shared-cpp/` from Phase 2 so that Phase 6's CLAP work doesn't
require a move-and-rebuild. Wrappers stay short and identical in shape.

CLAP is implemented in C++ (not pure C) so it can use `ZondelEngine`
directly. CLAP headers are C and work fine from C++. If a pure-C CLAP
wrapper is ever needed (vanishingly unlikely), the engine grows a
`zondel_engine_c.h` facade — but YAGNI.

## Realtime contract

This section is new in v2 and informs Phases 2, 3, 6.

**The pipe round-trip happens on the audio thread.** This is identical
to the OBS plugin's approach, but DAW audio threads are less forgiving
than OBS's. Define the realtime budget up front:

- Plugin's *advertised* internal latency: **480 samples @ 48 kHz = 10 ms**
  (the chunk size Zondel processes). Hosts get this via
  `getLatencySamples()` (VST3) and `clap.latency` (CLAP). This buys
  hosts permission to delay parallel tracks by 10 ms for PDC alignment.
- Plugin's *target round-trip*: pipe write + Zondel DSP + pipe read in
  under **2 ms**. The OBS plugin's measurement is "well under 1 ms" so
  there's headroom. The 5000 µs default timeout in `pipe-client` is the
  cliff edge; if we hit it, fall back to pass-through for this block.
- **Hung-but-connected** behaviour: if Zondel accepts the write but
  never responds within the timeout, `pipe-client` already returns
  `PIPE_ERR_READ_TIMEOUT` and closes the handle. Engine treats this
  like any other disconnect: switch to pass-through for this block,
  state machine bumps fail-streak, after 3 failures backs off for 2 s.
  Audio thread never sees more than one timeout per block.
- **Block-size invariance**: hosts give us blocks of 32, 64, 128, …,
  8192 samples at arbitrary sample rates. The engine's input ring
  buffer absorbs jitter; the resampler converts to 48 kHz; the 480-
  sample chunk size to Zondel is decoupled from the host block size.

Compliance test: see `test/test-realtime-budget.cpp` (new in Phase 2)
which exercises the engine against a synthetic 480-sample-chunked echo
server and asserts P99 round-trip < 2 ms.

## Phases

Each phase is shippable on its own. Phase N+1 doesn't depend on Phase
N having shipped to users — only on Phase N being merged.

---

### Phase 1 — VST3 SDK + skeleton (with bus arrangement!)

**Goal:** `cmake -DBUILD_VST3=ON` produces a loadable `.vst3` bundle
that the Steinberg `validator` tool accepts, **including the mandatory
bus arrangement test**.

**Why bus arrangement is in Phase 1, not later:** VST3 hosts negotiate
channel configuration via `setBusArrangements()` *before* `process()`
ever runs. Hosts that ask for an unsupported arrangement (e.g. 5.1)
expect a `kResultFalse` return — the OBS-style "accept anything,
detect-and-pass-through in process()" pattern causes validator
failures and host-side misrouting (especially in Cubase and Studio
One). Get this right from day one.

**Tasks:**

1. `src/vst3/CMakeLists.txt`:
   - `FetchContent` Steinberg VST3 SDK at tag `v3.8.0_build_66`.
   - `smtg_add_vst3plugin(Zondel.vst3 …)` target.
   - Link `zondel-core` and (Phase 2) `zondel-shared-cpp`.
2. `src/vst3/ZondelIDs.h` — two stable UUIDs generated *once* via
   `uuidgen` and *never* changed. DAWs use them to identify the plugin
   in project files. Commit them in Phase 1's PR.
3. `src/vst3/factory.cpp` — VST3 module entry. Declare one effect class
   with the `Fx|Restoration` category.
4. `src/vst3/ZondelProcessor`:
   - Subclass `AudioEffect`.
   - Constructor: declare one audio input + one audio output, both
     stereo by default. (Bus *count* is fixed; arrangement is what
     negotiates.)
   - Override `setBusArrangements(SpeakerArrangement* inputs, int32
     numIns, SpeakerArrangement* outputs, int32 numOuts)`:
     - Accept iff `numIns == 1 && numOuts == 1` and the arrangements
       are both `SpeakerArr::kMono` or both `SpeakerArr::kStereo`.
     - Reject anything else with `kResultFalse`. **This is what the
       validator tests.**
   - `process()`: copy in→out unchanged (no Zondel involvement yet).
5. `src/vst3/ZondelController` — subclass `EditController`. No
   parameters yet; just a stub.

**Verification:**

- `validator.exe Zondel.vst3` → zero failures, including the
  `BusInvalidIndex`, `BusConsistency`, `ChannelArrangementCheck` tests.
- Reaper loads, no crash, audio passes through. Channel-routing combos
  tested: mono→mono, stereo→stereo. Surround attempts get refused
  cleanly by the host.

**Acceptance:**

- `Zondel.vst3` bundle exists at the canonical path.
- Steinberg validator: zero failures.
- Plugin appears in Reaper's FX list under Zondel/Effects.

**Estimated effort: 1.5–2 days.**

---

### Phase 2 — Shared engine extraction + VST3 audio with state, PDC, processMode

**Goal:** `Zondel.vst3` processes audio through the running Zondel app
with full VST3 lifecycle correctness: state persistence, PDC, threading
contracts, processMode handling, null-engine pass-through.

**This is the big phase.** Most of the technical risk lives here.

**Tasks:**

#### 2a — `src/shared-cpp/ZondelEngine`

- C++ class. Constructor `ZondelEngine(double sampleRate, int channels,
  int maxBlockSize)`. Allocates everything: input ring, output ring,
  resampler pair, pipe client, state machine.
- Method `int32_t process(const float* const* inputs, float* const*
  outputs, int32_t frames, bool bypass)`:
  - If `bypass` → memcpy and return `kProcessOk_PassThrough`.
  - Else run the same OBS-side pipeline: downmix → resample → ring →
    pipe (up to 480-sample chunks) → ring → resample → broadcast.
- Method `uint32_t getLatencySamples() const` → returns the engine's
  contribution to host PDC. Computed from chunk size + ring depth.
- **No allocation outside the constructor.** `process()` is allocation-
  free and uses only the buffers allocated up front. Verified by
  ASAN/heap counter assertions in tests.
- State accessor `ZondelStateSnapshot getState() const` → returns the
  three-state enum (`Connected` / `Disconnected` / `Bypass`) plus a
  monotonic update counter. Used by VST3/CLAP wrappers to drive the
  status surface without taking locks.

#### 2b — VST3 processor wiring

- `setupProcessing(ProcessSetup&)` — called by host while plugin is
  *inactive*. Constructs/reconstructs the `ZondelEngine` here. **Safe
  to allocate.**
- `setProcessing(bool state)` — called potentially on the audio thread
  when the host enables/disables processing. **Must not allocate.**
  Only resets state. Validator's threading audit catches violations.
- `process(ProcessData& data)`:
  - If `engine == nullptr` (e.g. `setupProcessing` failed) → pass-
    through. Log once at the OBS logging level (or DBG output).
  - Read processMode: if `kOffline` and we hit Phase 1's filter (see
    next bullet), pass-through; otherwise run the engine.
  - Convert VST3's planar `ProcessData` to the engine's `float**`.
  - Apply Bypass parameter (Phase 3 plumbs the value here).
- `canProcessSampleSize(int32 symbolicSampleSize)` → accept only
  `kSample32`.
- **Offline/prefetch posture**: override `setProcessing` to accept all
  process modes but pass-through when `processMode == kOffline`. CLAP
  side declares no offline rendering via `clap_plugin_render` returning
  unsupported (Phase 6).

#### 2c — State persistence

- `IComponent::getState(IBStream*)` → serialise Bypass + Pipe timeout as
  two 4-byte LE ints into the stream.
- `IComponent::setState(IBStream*)` → deserialise; clamp to valid
  ranges; never crash on malformed input.
- `EditController::setComponentState(IBStream*)` → controller mirrors
  the same parse so its parameter cache matches the processor's.
- Tested in CI via the validator's
  `ProgramListCheck` / `ParameterSaveLoadCheck` suites.

#### 2d — PDC

- `IAudioProcessor::getLatencySamples()` → return
  `engine->getLatencySamples()` (typically 480 samples @ engine sample
  rate, converted to host sample rate by the wrapper).
- When the host changes its sample rate via `setupProcessing`, the
  engine is reconstructed and the latency announcement implicitly
  refreshes.

#### 2e — Realtime budget test

- `test/test-realtime-budget.cpp` — links `ZondelEngine` + a synthetic
  `fake-pipe-server` (already in `test/`). Runs 1000 process blocks at
  various sizes (64, 128, 256, 512, 1024) and various host sample rates
  (44.1, 48, 96 kHz). Asserts P99 < 2 ms, P100 < 5 ms (the timeout
  cliff).
- Added to `BUILD_PLUGIN_TESTS` matrix.

**Verification:**

- Insert plugin in Reaper at 48 kHz mono — Zondel processes (audible).
- Insert at 44.1 kHz stereo — SRC fallback path engages, still
  processes.
- Save Reaper project → restart Reaper → reload project → confirm
  Bypass and Pipe timeout values are restored.
- Disable Zondel → plugin shows Disconnected within 2 s and audio is
  pass-through. Re-enable Zondel → reconnects within 2 s.
- Try to bounce/render in offline mode → plugin passes through, no
  silence, no host hang.

**Acceptance:**

- Engine has zero realtime allocations (verified by heap-counter test).
- Validator: zero failures on state, threading, latency, processMode
  tests.
- P99 round-trip < 2 ms in `test-realtime-budget`.
- Reaper project save/load preserves parameter values.

**Estimated effort: 5–7 days.** This includes ZondelEngine extraction,
VST3 lifecycle correctness, state persistence, PDC, and the realtime
test. Single biggest phase.

---

### Phase 3 — VST3 controller, parameters, status via Data Exchange API

**Goal:** Plugin's UI exposes Bypass + Pipe timeout, and shows live
status without taking the audio thread hostage.

**Tasks:**

1. Two parameters declared in `ZondelController::initialize()`:
   - `kBypassParam` (id 0) — bool, default 0, flagged
     **`ParameterInfo::kIsBypass`**. Hosts use this to auto-bypass on
     mute / during freeze rendering, and DAWs render it as a dedicated
     Bypass button rather than a generic toggle.
   - `kPipeTimeoutParam` (id 1) — `kInteger` step type, range 1000…
     20000 µs, default 5000.
2. `ZondelProcessor::process()` reads parameter changes from
   `data.inputParameterChanges` and pushes Bypass to the engine
   (engine does the actual pass-through on its side; the host's own
   Bypass logic is layered on top).
3. **Status surface via VST3 Data Exchange API** (SDK ≥3.7.9, confirmed
   in `pluginterfaces/vst/ivstdataexchange.h`):
   - Processor opens a queue via `IDataExchangeHandler::openQueue`.
   - On `process()` exit, if the engine's state-change counter
     incremented, allocate a `DataExchangeBlock` from the queue and
     write the new state snapshot.
   - Controller implements `IDataExchangeReceiver::onDataExchangeBlocks`
     — runs on UI thread, updates a parameter-derived display string.
4. UI choice (decision):
   - **3a (recommended):** Minimal VSTGUI view — one status indicator
     LED, one parameter list. ~200 LoC. Lives in
     `src/vst3/ZondelStatusReceiver.cpp` + the VSTGUI .uidesc XML.
   - **3b (fallback if VSTGUI integration is more pain than expected):**
     No custom view. Host renders the parameter list itself. Status
     becomes a read-only parameter flagged `kIsReadOnly`.
   - Decision deferred to start of phase based on how much SDK 3.8 has
     improved VSTGUI ergonomics.

**Verification:**

- In Reaper, toggle Bypass → audio audibly switches between processed
  and dry within the next block.
- Bypass shows up as a dedicated bypass button in Studio One (which
  inspects `kIsBypass`), not just a generic toggle.
- Kill Zondel → status reads "● Disconnected" in the UI within 100 ms.
- Validator's thread-safety stress test passes (no Data Exchange
  contract violations).

**Acceptance:**

- Bypass is host-automatable and respects `kIsBypass`.
- Status updates within 100 ms of state change.
- No audio-thread blocking on Data Exchange operations (verified by
  realtime budget test re-run with status updates active).

**Estimated effort: 2–3 days** (option 3a) or **1 day** (option 3b).

---

### Phase 4 — VST3 packaging + CI + Steinberg validator gate

**Goal:** `windows-x64-vst3` CI job builds, validates, and uploads the
`.vst3` bundle as a release artefact.

**Tasks:**

1. Extend the `windows-build` job to build `Zondel.vst3` (matrix
   already covers x64 + arm64).
2. `.github/actions/validate-vst3/action.yaml` — downloads the
   `validator.exe` built by the SDK at the same tag and runs it. Build
   job fails on any validator error.
3. `.github/actions/package-vst3/action.yaml` — packages the bundle into
   `Zondel-vst3-vX.Y.Z-windows-x64.zip`.
4. Extend `.github/workflows/release.yaml` to publish the VST3 zip as a
   release asset alongside the existing OBS plugin artefacts.
5. `installer/installer-vst3.iss` — Inno Setup script that drops the
   bundle into `{commonpf}\VST3\Zondel.vst3\`.

**Verification:**

- Trigger a `workflow_dispatch` run. Green build.
- Download the VST3 zip, extract, confirm Reaper loads.

**Acceptance:**

- Green CI on `main`.
- `Zondel-vst3-vX.Y.Z-windows-x64.zip` and `Zondel-vst3-…-setup.exe`
  are release assets.
- Validator gates merges (red validator = red CI).

**Estimated effort: 1–2 days.**

---

### Phase 5 — CLAP plugin

**Goal:** `Zondel.clap` ships. Loads in Reaper, Bitwig, FL Studio
2024.1+, Studio One 7. Behaviour identical to the VST3 build.

**Decision: native CLAP, not `clap-wrapper`.**

`clap-wrapper` auto-generates CLAP from VST3 (or vice-versa). For a
plugin this small, the native CLAP wrapper is ~300 LoC of C++ and is
simpler than the wrapper's build-system glue. The wrapper makes more
sense for very large plugins.

**Tasks:**

1. `src/clap/CMakeLists.txt`. FetchContent
   `https://github.com/free-audio/clap` (header-only). Output: a
   MODULE library with extension `.clap`.
2. `src/clap/factory.cpp` — the `clap_plugin_entry_t` symbol exported as
   `clap_entry`. Single plugin descriptor:
   - Plugin ID: `com.zondel.bridge.clap` (stable, never change).
   - Features: `"audio-effect"`, `"voice-processor"`,
     `"noise-suppressor"`.
3. `src/clap/zondel-clap.cpp` — `clap_plugin_t` v-table backed by
   `ZondelEngine`. Implement:
   - `init`, `destroy`, `activate`, `deactivate`, `start_processing`,
     `stop_processing`, `process`, `on_main_thread`, `get_extension`,
     `reset`.
   - `process()` mirrors the VST3 processor: read params, run engine,
     write outputs.
4. CLAP extensions to implement:
   - **`audio-ports`** — declare one stereo input, one stereo output.
     For mono use, declare a second port pair (CLAP's `audio_ports`
     supports multiple bus configurations; hosts pick the one that
     matches the track).
   - **`params`** — same two params (Bypass, Pipe timeout) with the
     same IDs as VST3 so cross-format projects stay coherent.
   - **`state`** — `save()` / `load()` write the same two-int blob as
     VST3's `getState`/`setState`.
   - **`latency`** — `get()` returns `engine->getLatencySamples()`.
     Called by host only between `activate` and the first
     `start_processing`; latency change requires host restart, but
     we don't change latency dynamically.
   - **`render`** — declare `CLAP_RENDER_REALTIME` only. Hosts that
     respect this won't ask us to do offline rendering. (Mirrors the
     VST3 `kOffline` pass-through.)
   - **`thread-check`** — call into the host's main-thread / audio-
     thread predicates inside debug assertions, never in release.
   - **`gui`** — deferred. v1 CLAP uses host's generic parameter UI.

**Verification:**

- Reaper loads `Zondel.clap`. Audio processes.
- Bitwig 5 loads `Zondel.clap`. Audio processes.
- FL Studio 2024.1+ loads `Zondel.clap`. Audio processes.
- `clap-validator` (independent CLI from free-audio/clap-validator)
  passes every check.
- Save Bitwig project → restart → reload → params restored.

**Acceptance:**

- `Zondel.clap` is a single `.clap` file (MODULE library), ~400–600 KB.
- Loads in all four target hosts.
- Parameter IDs match VST3.
- `clap-validator` clean.

**Estimated effort: 2–3 days.** Less than VST3 because the engine and
state contracts are already established.

---

### Phase 6 — CLAP CI + installer + validator

**Goal:** CLAP gets the same CI treatment as VST3.

**Tasks:**

1. `.github/actions/validate-clap/action.yaml` — pulls
   `free-audio/clap-validator` (or builds it from source) and runs it.
2. Matrix entry `windows-x64-clap` in `build-project.yaml`.
3. `installer/installer-clap.iss` — Inno Setup that drops the `.clap`
   into `{commonpf}\CLAP\Zondel.clap`.
4. Release pipeline gains the third artefact stream.

**Verification:**

- CI green across all six matrix entries (3 plugins × 2 archs).
- All three installers downloadable from a release.

**Estimated effort: 1 day.**

---

### Phase 7 — Cross-DAW validation matrix

**Goal:** Manual validation matrix run before each release, results
pinned to release notes.

**Tasks:**

1. `docs/DAW-MATRIX.md` — document the host versions tested and per-
   host gotchas. Initial matrix:
   - VST3: Reaper 7.x, Cubase 13/14, Ableton Live 12, Studio One 6/7,
     FL Studio 2024.x, Bitwig 5.x.
   - CLAP: Reaper 7.x, Bitwig 5.x, FL Studio 2024.1+, Studio One 7.
2. Smoke tests per host:
   - Load, channel-route mono+stereo, bypass toggle, project
     save/load, Zondel kill+restart resilience, sample-rate change
     mid-session, freeze/bounce behaviour.
3. Pre-release checklist in `docs/RELEASE-CHECKLIST.md` references
   `DAW-MATRIX.md`.

**Estimated effort: 1–2 days calendar** (needs DAW licenses / trials).

---

### Phase 8 — Unified release wiring

**Goal:** One GitHub release per tag, three plugins shipped as separate
installers and zips.

**Tasks:**

- Extend `release.yaml` to produce six artefacts per release:
  `zondel-obs-plugin-{zip,setup.exe}`,
  `Zondel-vst3-{zip,setup.exe}`,
  `Zondel-clap-{zip,setup.exe}` (per arch).
- Release notes template includes the DAW matrix link and the SDK
  versions used.

**Estimated effort: 0.5–1 day.**

---

## Risks and mitigations (revised)

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| VST3 SDK API breaking change between releases | low | medium | Pin SDK to specific tag (`v3.8.0_build_66`); bump intentionally. |
| Steinberg license posture shift | very low | medium | SDK is MIT now (2025 relicense); if Steinberg reverted, we'd be grandfathered into the MIT release that's already public. |
| CLAP host fragmentation | medium | low | Stick to mandatory + well-established extensions only in v1. |
| Cross-thread bugs in VST3 (processor / controller separation) | high | medium | Status updates flow via Data Exchange API (one-way, lock-free). All param flow uses VST3's queued mechanism. No direct cross-thread access. |
| Realtime budget violation under load (Zondel slow to respond) | medium | medium | Hard 5 ms timeout already in `pipe-client`; state machine backs off after 3 failures; realtime budget test catches regressions in CI. |
| ZondelEngine API churn from supporting two formats | low | low | Engine API designed up front in Phase 2 with both VST3 and CLAP processor shapes in mind. No premature factoring needed. |
| OBS plugin breakage from shared-core changes | medium | high | Existing core tests run against every PR; PRs touching `src/core/` must also pass OBS build. |
| VST3 validator finds an arrangement-test edge case we missed | medium | low | Phase 1 specifically targets arrangement validation; if a host quirk emerges later, narrow the supported arrangement list rather than papering over. |
| FL Studio CLAP support exact minimum version | low | low | Tested floor stated as 2024.1+; if older works, advertise more broadly post-release. |

## Estimated effort (revised)

| Phase | Effort |
|---|---|
| 1 — VST3 SDK + skeleton + bus arrangements | 1.5–2 days |
| 2 — Shared engine + VST3 audio/state/PDC/threading | 5–7 days |
| 3 — VST3 controller + Data Exchange status | 1–3 days (option-dependent) |
| 4 — VST3 CI + installer + validator gate | 1–2 days |
| 5 — CLAP plugin | 2–3 days |
| 6 — CLAP CI + installer + validator | 1 day |
| 7 — Cross-DAW validation | 1–2 days calendar |
| 8 — Unified release wiring | 0.5–1 day |

**Total: 13–21 focused days** (or 15–25 calendar days when accounting
for DAW-matrix testing time). v1's "7–10 days" was wishful; this is
realistic for an experienced C++ developer new to VST3 but with the
existing C core to lean on.

## Open questions

1. **VST3 UID values.** Generate two UUIDs in Phase 1 PR and commit
   them to `ZondelIDs.h` once. Never change.
2. **CLAP plugin ID format.** Proposed `com.zondel.bridge.clap` — same
   reverse-DNS convention as Surge, Vital, u-he. Lock at Phase 5 start.
3. **VSTGUI vs no custom UI** (Phase 3 option 3a vs 3b). Defer to start
   of phase based on how much SDK 3.8 has improved VSTGUI's CMake
   integration. The fallback (no custom UI) is fully functional.
4. **macOS / Linux** is a v2 conversation. CLAP itself is portable; the
   blocker is `zondel-core`'s named-pipe IPC, which is Windows-only.

## Cross-references

- Existing OBS plugin spec:
  [docs/superpowers/specs/2026-05-13-zondel-obs-plugin-design.md](../../../docs/superpowers/specs/2026-05-13-zondel-obs-plugin-design.md)
  (in the apm-plugin parent repo).
- Pipe wire protocol: [PROTOCOL.md](PROTOCOL.md).
- OBS plugin reference for shared-core integration patterns:
  [`src/obs/zondel-filter.c`](../src/obs/zondel-filter.c).
- VST3 SDK: <https://github.com/steinbergmedia/vst3sdk> (MIT, current
  tag `v3.8.0_build_66`).
- VST3 Data Exchange API:
  `vst3sdk/pluginterfaces/vst/ivstdataexchange.h` (released 3.7.9).
- VST3 bus arrangement contract: `IAudioProcessor::setBusArrangements`.
- CLAP: <https://github.com/free-audio/clap> (MIT).
- CLAP latency extension: `clap/ext/latency.h` (`clap.latency`).
- CLAP render extension: `clap/ext/render.h`.
- `clap-validator`: <https://github.com/free-audio/clap-validator>.
