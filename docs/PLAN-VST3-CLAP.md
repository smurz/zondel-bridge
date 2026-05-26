# VST3 + CLAP implementation plan

> Status: planning — not started.
> Owner: smurz.
> Target: ship Zondel as a VST3 plugin and a CLAP plugin in addition to
> the existing OBS Studio plugin, all from this repo, all on top of the
> `zondel-core` static library.

## Why

Zondel ships as a desktop DSP app today. The OBS plugin (already in this
repo) lets streamers route OBS audio through Zondel. The same `zondel-core`
that powers the OBS plugin — pipe IPC, ring-buffered chunking, cubic
resampling, stereo↔mono downmix, connection-state machine, auto-launch —
is host-agnostic. Wrapping it in a VST3 entry point unlocks every major DAW
(Reaper, Cubase, Studio One, Ableton Live, FL Studio, Bitwig, Pro Tools-via-
bridge), and wrapping it in CLAP unlocks the same hosts under a permissive
license without Steinberg's GPL/proprietary fork in the legal path.

Both plugins are ~10% format glue around 90% shared core. Adding them is
linear work, not a research project.

## Scope

### v1 (this plan)

- Windows x64 only. ARM64 is best-effort behind `continue-on-error` in CI,
  same posture as the OBS plugin's CI matrix today.
- Mono and stereo I/O. Multi-channel (5.1/7.1) is out of scope; the plugin
  declares unsupported and passes through, matching the OBS filter's v1
  behaviour.
- Pass-through fallback when Zondel is not running. No splash, no modal —
  just a "Disconnected" indicator and unmodified audio out, identical to
  the OBS plugin.
- Parameters: **Bypass** (bool), **Pipe timeout (µs)** (int, default 5000).
  Status text is read-only, surfaced via the controller.
- No internal DSP. Every algorithm lives in the Zondel app; this plugin
  is a transport layer.

### Out of scope (future v2)

- macOS / Linux. Pipe protocol is Windows-named-pipe today.
- AudioUnit (macOS-native format).
- Surround mixdown.
- Per-DAW automation curves for tuning parameters (the Zondel app owns
  tuning; the plugin parameters are limited to transport behaviour).
- Steinberg / Apple notarisation. v1 ships unsigned, same as OBS plugin v1.

## Constraints from the existing repo

These are not negotiable and shape every decision below.

1. **`zondel-obs-plugin.dll`'s identity must not change.** OBS looks for
   it by name. The OBS target's `OUTPUT_NAME` stays `zondel-obs-plugin`;
   only new artefacts get new names.
2. **`zondel-core` is the truth.** Every plugin links it; no plugin
   duplicates ring-buffer / resampler / pipe-client logic. If a bug is
   found in the core, the fix is one commit and rebuilds three plugins.
3. **The pipe wire protocol is frozen at v1** (see
   [docs/PROTOCOL.md](PROTOCOL.md)). VST3 and CLAP wrappers speak the
   same protocol — they are bit-identical clients of the same Zondel app.
4. **Tests target `zondel-core`, not the wrappers.** Existing tests in
   `test/` already do this. New wrapper code adds integration tests, not
   re-tests of resampler / ring-buffer.
5. **License is GPLv3+ repo-wide.** VST3 SDK forces this on the GPL side;
   CLAP and OBS are permissive enough to coexist.

## Repo layout target

```
zondel-bridge/
├── src/
│   ├── core/                  ← unchanged; the static library every plugin links
│   ├── obs/                   ← unchanged; ships today
│   ├── vst3/                  ← NEW
│   │   ├── CMakeLists.txt
│   │   ├── ZondelProcessor.h/.cpp
│   │   ├── ZondelController.h/.cpp
│   │   ├── ZondelIDs.h        ← VST3 ProcessorUID + ControllerUID
│   │   ├── factory.cpp        ← VST3 module entry point
│   │   └── zondel-engine.h/.cpp ← thin C++ wrapper around zondel-core
│   └── clap/                  ← NEW
│       ├── CMakeLists.txt
│       ├── zondel-clap.c      ← clap_plugin_t implementation
│       └── factory.c          ← clap entry point
├── third_party/               ← NEW (gitignored; populated by CMake FetchContent)
│   ├── vst3sdk/               ← Steinberg VST3 SDK 3.7.x
│   └── clap/                  ← free-audio/clap headers
├── test/
│   ├── (existing core tests)
│   └── test-zondel-engine.cpp ← NEW; format-agnostic integration test
├── docs/
│   ├── PROTOCOL.md            ← unchanged
│   └── PLAN-VST3-CLAP.md      ← this file
└── installer/
    ├── installer.iss          ← OBS today
    ├── installer-vst3.iss     ← NEW; copies Zondel.vst3 → %CommonProgramFiles%\VST3\
    └── installer-clap.iss     ← NEW; copies Zondel.clap → %CommonProgramFiles%\CLAP\
```

## Architecture (one diagram, then phases)

```
                          ┌────────────────────────────────┐
                          │       zondel-core (.lib)        │
                          │  pipe IPC · ring buffer · SRC  │
                          │   downmix · state · launch     │
                          └───────────────┬────────────────┘
                                          │ linked statically
        ┌─────────────────────────────────┼─────────────────────────────────┐
        ▼                                 ▼                                 ▼
┌──────────────────┐            ┌──────────────────┐              ┌──────────────────┐
│  src/obs/        │            │  src/vst3/       │              │  src/clap/       │
│ plugin-main.c    │            │ ZondelProcessor  │              │ zondel-clap.c    │
│ zondel-filter.c  │            │ ZondelController │              │ factory.c        │
└──────────────────┘            │ factory.cpp      │              └──────────────────┘
        │                       └──────────────────┘                       │
        │                                 │                                │
        ▼                                 ▼                                ▼
zondel-obs-plugin.dll               Zondel.vst3                       Zondel.clap
   (OBS Studio)                  (Reaper, Cubase, Live, FL,      (Reaper, Bitwig,
                                  Studio One, Bitwig, …)          FL 21+, Studio One 7)
```

The shared `zondel-engine` (planned, src/vst3/zondel-engine.{h,cpp})
abstracts the per-channel pipeline state — one ring buffer in, one ring
buffer out, one pipe client, one resampler pair — behind a C++ class.
This lets both VST3 and CLAP wrappers stay short and identical in shape.

## Phases

Each phase is shippable on its own. Phase N+1 doesn't depend on Phase N
having shipped to users — only on Phase N being merged.

---

### Phase 1 — VST3 SDK integration + hello-world target

**Goal:** `cmake -DBUILD_VST3=ON` produces a loadable `.vst3` bundle that
the Steinberg `validator` tool accepts. No audio yet.

**Tasks:**

1. Create `src/vst3/CMakeLists.txt`.
2. In the parent `CMakeLists.txt`, the `add_subdirectory(src/vst3)` gate
   is already present (added in the restructure commit). Phase 1's sub-
   directory CMake should:
   - Use `FetchContent` to pull
     `https://github.com/steinbergmedia/vst3sdk` at a pinned tag
     (start with `v3.7.13_build_42` — current LTS as of plan date).
   - Add the SDK's `public.sdk` static lib.
   - Define `Zondel.vst3` as a `smtg_add_vst3plugin(...)` target.
3. Write a placeholder `ZondelProcessor` (subclassing `AudioEffect`) and
   `ZondelController` (subclassing `EditController`). Both can be empty —
   the processor's `process()` returns `kResultOk` after copying input to
   output unchanged.
4. Write `ZondelIDs.h` with two stable UIDs generated once via `uuidgen`.
   These are the plugin's permanent identity — DAWs save them in project
   files, so changing them later breaks user sessions.
5. Write `factory.cpp`: the `BEGIN_FACTORY_DEF` / `DEF_CLASS2` block that
   the VST3 loader calls.
6. Link `zondel-core` into the processor target (no-op for now; sets up
   the linkage so Phase 2 can use it).

**Verification:**

- Run `Validator.exe Zondel.vst3` (ships with VST3 SDK). Expect all
  mandatory tests pass.
- Load `Zondel.vst3` in Reaper. Insert on an audio track. Confirm it
  shows up under "VST3 / Zondel" and produces unchanged audio.

**Acceptance:**

- `Zondel.vst3` bundle exists at
  `build_x64/RelWithDebInfo/Zondel.vst3/Contents/x86_64-win/Zondel.vst3`.
- Steinberg validator: zero failures.
- Reaper loads, no crash, audio passes through.

**Why this phase is isolated:** if the SDK fetch / link is wrong, we
discover it before any Zondel logic is involved. Total LoC: ~200 across
factory.cpp, ZondelProcessor, ZondelController.

---

### Phase 2 — VST3 processor wired to `zondel-core`

**Goal:** `Zondel.vst3` processes audio through the running Zondel app.
If Zondel isn't running, audio passes through unchanged. Identical
behavioural contract as the OBS plugin.

**Tasks:**

1. Create `src/vst3/zondel-engine.{h,cpp}` — a C++ class that owns one
   per-instance bundle of `zondel-core` state: input ring buffer, output
   ring buffer, resampler pair, pipe client, state machine. Constructor
   takes host sample rate + channel count. `process(float** in, float**
   out, int frames)` runs one VST3 process block.
   - This class lives in `src/vst3/` (not `src/core/`) because it pulls in
     C++-only abstractions. The shared C primitives in `src/core/` stay
     pure C.
   - CLAP will use the same `zondel-engine` in Phase 5 — that's why it
     goes in `src/vst3/` for now and gets promoted to `src/shared-cpp/`
     in Phase 5 if needed. Premature factoring deferred until both
     consumers exist.
2. `ZondelProcessor::setupProcessing()` — given the host's
   `ProcessSetup`, construct the `ZondelEngine`. Disallow `kOffline` and
   `kPrefetch` modes; we only support `kRealtime`.
3. `ZondelProcessor::process()` — convert the VST3 `ProcessData` block
   layout (samples per channel × frames) into the float** input the
   engine wants, call `engine.process()`, write back.
4. Handle channel-count mismatches: if host gives us more than 2
   channels, set the state to `ZS_UNSUPPORTED_FORMAT` and pass through.
5. Handle sample-rate changes: `setupProcessing` re-creates the engine
   with the new rate.

**Verification:**

- Insert plugin in Reaper at 48 kHz. Mic input goes through Zondel
  (audible noise suppression).
- Insert at 44.1 kHz. Same — confirms the resampler in `zondel-core` is
  being driven correctly through the VST3 layer.
- Kill the Zondel app while audio is playing. Plugin falls back to
  pass-through within 2 seconds (the `BACKOFF_NS` window in
  `zondel-state.c`). No silence, no crash.
- Restart Zondel. Plugin reconnects within 2 seconds.

**Acceptance:**

- Audible processing in Reaper at 44.1, 48, 96 kHz, both mono and stereo
  sources.
- No audio dropouts at the plugin's nominal block sizes (64, 128, 256,
  512, 1024).
- Plugin survives a Zondel app crash without crashing the host DAW.

---

### Phase 3 — VST3 controller (parameters + status surface)

**Goal:** The plugin's UI exposes the same controls and status indicator
as the OBS filter.

**Tasks:**

1. Define two parameters in `ZondelController::initialize()`:
   - `kBypassParam` — bool, default 0. Hosts may already auto-bypass via
     `IAudioProcessor::setProcessing(false)`, but a parameter-level bypass
     is what users see in the DAW's parameter list.
   - `kPipeTimeoutParam` — int 1000…20000 µs, default 5000.
2. Implement `ZondelProcessor::processParameterChanges()` to push changes
   into the engine.
3. Build a minimal IPlugView. Two choices:
   - **3a (recommended for v1):** Use the SDK's `VSTGUI` for a tiny UI
     showing a status text ("● Connected" / "● Disconnected" / "Bypass")
     and a Bypass toggle. ~200 LoC, no external Qt-style framework.
   - **3b (fallback):** Don't ship a custom view. The DAW renders the
     parameter list itself. Status indicator becomes a read-only
     parameter with `ParameterInfo::kIsReadOnly`. Hosts that don't render
     it (Live, Cubase) will hide it; Reaper shows it in its generic UI.
4. Plumb the status string from the processor to the controller via
   `IMessage` (the VST3-blessed cross-thread channel). The state machine
   in `zondel-core` already provides `zondel_state_t::status`; convert
   to a 3-state enum string in the engine and ship it to the controller
   on every block.

**Verification:**

- In Reaper, toggle Bypass — audio audibly cuts the Zondel processing in
  and out.
- Change Pipe timeout from 5 ms → 20 ms while audio is playing. Confirm
  the engine picks it up (no parameter-binding bug).
- Kill Zondel → "● Disconnected" appears in the UI. Restart Zondel →
  reverts to "● Connected".

**Acceptance:**

- Bypass parameter is host-automatable.
- Status string updates within 100 ms of state change.
- No threading violations under the validator's stress test.

---

### Phase 4 — VST3 packaging + Steinberg validator in CI

**Goal:** `windows-x64-vst3` CI job builds, validates, and uploads the
`.vst3` bundle as a release artefact.

**Tasks:**

1. `.github/workflows/build-project.yaml` matrix gains an entry: the
   `windows-build` job already runs once for `x64` and once for `arm64`;
   split the build step so it builds three plugin targets:
   - `cmake --build … --target zondel-obs-plugin`
   - `cmake --build … --target Zondel-vst3` (if `BUILD_VST3=ON`)
   - `cmake --build … --target zondel-clap` (Phase 6)
2. New action `.github/actions/validate-vst3/action.yaml` — downloads or
   builds Steinberg's `validator` and runs it against the output bundle.
   Build job fails on any validator error.
3. New action `.github/actions/package-vst3/action.yaml` — packages the
   `.vst3` bundle into `Zondel-vst3-vX.Y.Z-windows-x64.zip`.
4. Extend `.github/workflows/release.yaml` to also publish the VST3 zip
   as a release asset alongside the existing OBS plugin artefacts. Each
   release shows three downloads per arch:
   `zondel-obs-plugin-…zip`, `Zondel-vst3-…zip`, `Zondel-clap-…zip`
   (CLAP added in Phase 6).
5. Update `installer.iss` family: add `installer-vst3.iss` that drops
   the bundle into `{commonpf}\VST3\Zondel.vst3` and adds an uninstall
   entry. The existing OBS installer is unchanged.

**Verification:**

- Trigger a `workflow_dispatch` build of the matrix. All matrix entries
  pass.
- Download the VST3 zip artefact, extract into
  `%CommonProgramFiles%\VST3\`, launch Reaper, confirm plugin loads.

**Acceptance:**

- Green CI on `main`.
- `Zondel-vst3-vX.Y.Z-windows-x64.zip` is a release asset.
- Steinberg validator runs in CI; it gates merges.

---

### Phase 5 — CLAP plugin (mirror VST3 on the same engine)

**Goal:** `Zondel.clap` ships. Loads in Reaper, Bitwig, FL Studio 21+,
Studio One 7. Behaviour identical to the VST3 build.

**Decision: native CLAP vs `clap-wrapper`.**

`clap-wrapper` (free-audio/clap-wrapper) auto-generates CLAP from VST3
(and vice-versa). Sounds like a shortcut. In practice, for a plugin this
small, the manual CLAP wrapper is ~250 LoC of plain C and is simpler
than the wrapper's build-system glue. Decision: **write CLAP natively.**
Revisit if maintenance of two parallel wrappers turns out to be painful.

**Tasks:**

1. Create `src/clap/CMakeLists.txt`. FetchContent
   `https://github.com/free-audio/clap` (header-only). Build target
   `zondel-clap` as a `MODULE` library with output extension `.clap`.
2. `src/clap/factory.c` — the `clap_plugin_entry_t` symbol exported as
   `clap_entry`. Single plugin descriptor with a stable plugin ID
   (`com.zondel.bridge.clap`), name, vendor, URL, version, features
   (`audio-effect`, `voice-processor`).
3. `src/clap/zondel-clap.c` — the `clap_plugin_t` v-table. Implement:
   `init`, `destroy`, `activate`, `deactivate`, `start_processing`,
   `stop_processing`, `process`, `on_main_thread`, `get_extension`. The
   `process` callback wraps `ZondelEngine` exactly like the VST3 path.
4. Extensions to support: `audio-ports` (declare 1 stereo in + 1 stereo
   out), `params` (Bypass, Pipe timeout — matching VST3 IDs / ranges),
   `state` (save/load via two ints — kept simple, no JSON), `gui`
   (deferred to a follow-up; v1 CLAP uses the host's generic param UI).
5. CMake gate: when `BUILD_CLAP=ON`, also `BUILD_VST3=ON` is not
   required — they're independent (CLAP has no SDK dependency on VST3).
   The shared `ZondelEngine` is what they both depend on; if it's still
   under `src/vst3/`, move it to `src/shared-cpp/` here.

**Verification:**

- Reaper loads `Zondel.clap`. Audio passes through Zondel.
- Bitwig loads `Zondel.clap`. Same.
- FL Studio 21 loads `Zondel.clap`. Same.
- `clap-validator` (independent CLI tool) passes all tests.

**Acceptance:**

- `Zondel.clap` is a single `.clap` file (MODULE library), ~400 KB.
- Loads in all three target hosts.
- Parameter IDs match the VST3 plugin so users with cross-format projects
  see the same controls.

---

### Phase 6 — CI matrix expansion + cross-DAW validation pass

**Goal:** every push to `main` builds + validates all three plugins
across x64 and arm64-best-effort. Manual DAW validation matrix is run
once before release.

**Tasks:**

1. Extend the CI matrix to include `BUILD_VST3` and `BUILD_CLAP` jobs.
   Three matrix entries per arch: `obs`, `vst3`, `clap`.
2. Add `.github/actions/validate-clap/action.yaml` running
   `clap-validator`.
3. Document the manual DAW validation matrix in `docs/DAW-MATRIX.md`:
   Reaper, Cubase, Studio One, Ableton Live, FL Studio, Bitwig — what to
   test in each, what's a known limitation. Run the matrix manually
   pre-release; results pinned to the GitHub release notes.

**Acceptance:**

- CI matrix has 6 jobs per arch (3 plugins × 2 configs).
- `docs/DAW-MATRIX.md` exists and lists the host versions tested.

---

### Phase 7 — Unified installer + release

**Goal:** one combined Windows installer that installs all three plugins
with per-format checkboxes. Or three separate installers — pick one based
on which is less work given the existing OBS installer.

**Recommendation:** keep three separate installers
(`zondel-obs-plugin-setup.exe`, `Zondel-vst3-setup.exe`,
`Zondel-clap-setup.exe`) and let users grab the ones they want. Combined
installers are a UX win but a maintenance liability — they have to detect
DAW installs to be useful, and that's a feature-creep rabbit hole.

**Tasks:**

1. Write `installer/installer-vst3.iss` and `installer/installer-clap.iss`,
   reusing the existing OBS installer's structure.
2. Each writes to the appropriate canonical path:
   - VST3: `%CommonProgramFiles%\VST3\Zondel.vst3\` (bundle dir)
   - CLAP: `%CommonProgramFiles%\CLAP\Zondel.clap` (single file)
3. Extend `.github/workflows/release.yaml` to produce all three installer
   `.exe` artefacts.
4. Eventually: EV-sign installers (deferred to v2 across the board,
   matching OBS plugin v1's stance).

**Acceptance:**

- Each installer is independently downloadable from the release page.
- Uninstall via Add/Remove Programs leaves no traces.

---

## Risks and mitigations

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| VST3 SDK API breaking change between releases | low | medium | Pin the SDK to a specific tag in FetchContent; bump intentionally. |
| Steinberg legal posture shift on GPL-side users | very low | high | We're GPL-side only; if Steinberg ever revokes, the plugin's still legal under existing SDK terms. CLAP exists as the always-permissive fallback. |
| CLAP host fragmentation (different hosts implement different extensions) | medium | low | Stick to mandatory extensions only in v1. Add advanced extensions as host adoption grows. |
| Cross-thread bugs (VST3 separates processor and controller threads strictly) | high | medium | Mirror the OBS plugin's single-threaded pipe IPC. The processor never touches the controller's state directly — only via `IMessage`. |
| `zondel-engine` API churn from supporting two formats | medium | medium | Phase 2 ships engine API used only by VST3. Phase 5 forces a second consumer; refactor at that point with real evidence, not speculation. |
| OBS plugin breakage from shared-core changes | medium | high | Existing core tests run against every PR; any PR touching `src/core/` must also pass OBS plugin build. |

## Estimated effort

Rough order of magnitude per phase. These are calendar estimates with
"do nothing else" focus, not engineering-day estimates.

| Phase | Effort | Why |
|---|---|---|
| 1 — VST3 SDK + hello-world | 1 day | SDK setup is most of it; processor + factory is boilerplate. |
| 2 — VST3 wired to zondel-core | 2 days | Engine extraction + integration. |
| 3 — VST3 controller + UI | 1–2 days | Depends on choice 3a vs 3b. |
| 4 — VST3 CI + installer | 1 day | Validator action is the new part; package is templated from OBS. |
| 5 — CLAP (native, not wrapper) | 1–2 days | Most of the work is already in engine. |
| 6 — Cross-DAW validation | 1 day (calendar — needs DAW licenses) | Manual testing across 6 DAWs. |
| 7 — Installers + release wiring | 0.5 day | Templated from OBS installer. |

Total: ~7–10 focused days. The shared core is the multiplier — every
day spent on the core pays off three times.

## Open questions

1. **VST3 UID source.** Need to generate two stable UUIDs (processor +
   controller) once and lock them. Proposal: generate them in Phase 1
   commit, document in `ZondelIDs.h`, never change.
2. **CLAP plugin ID format.** Steinberg's format is UUID; CLAP's is a
   reverse-DNS string. Proposed: `com.zondel.bridge.clap` — fits the
   convention used by Surge, Vital, u-he plugins.
3. **Buildspec.json scope.** Today it's OBS-only (versions, OBS deps).
   Question: extend it to a generic `pluginspec.json` that VST3/CLAP also
   read for version + author? Or keep buildspec.json OBS-specific and
   give VST3/CLAP their own version constants in CMakeLists? Lean toward
   the latter to avoid touching the OBS plugin template at all.
4. **Should the CLAP plugin be Linux/macOS-capable from day one?** CLAP
   itself is portable; the blocker is `zondel-core`'s named-pipe IPC,
   which is Windows-only. Decision can wait; macOS+Linux is a v2 topic.

## Cross-references

- Existing OBS plugin spec:
  [docs/superpowers/specs/2026-05-13-zondel-obs-plugin-design.md](../../../docs/superpowers/specs/2026-05-13-zondel-obs-plugin-design.md)
  (lives in the apm-plugin parent repo).
- Pipe wire protocol: [PROTOCOL.md](PROTOCOL.md).
- OBS plugin source as reference for shared-core integration patterns:
  [`src/obs/zondel-filter.c`](../src/obs/zondel-filter.c).
- VST3 SDK: <https://github.com/steinbergmedia/vst3sdk>
- CLAP: <https://github.com/free-audio/clap>
- clap-validator: <https://github.com/free-audio/clap-validator>
