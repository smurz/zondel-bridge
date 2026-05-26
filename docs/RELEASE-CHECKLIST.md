# Release checklist

Pre-release steps for cutting a tagged release of `zondel-bridge`.
Each release publishes six artefacts: zip + setup.exe for each of OBS,
VST3, CLAP.

## Pre-flight

- [ ] `CHANGELOG.md` has a section for the new version with a one-line
      summary + bullet-list of user-visible changes. Use kebab-prefix
      tags: `feat:`, `fix:`, `docs:`, etc.
- [ ] `buildspec.json` `version` field bumped (OBS pipeline reads it).
- [ ] `src/vst3/version.h` `MAJOR/SUB/RELEASE` bumped (VST3 reads it).
- [ ] `src/clap/zondel-clap.cpp` `kPluginVersion` string bumped (CLAP
      reads it).
- [ ] `installer/installer*.iss` files have no hard-coded version
      override (they accept `/DAppVersion=…` from CI).

## Build verification

- [ ] All three GitHub Actions workflows are green on `main` for the
      commit you're about to tag:
      - `Build Project` (OBS)
      - `Build VST3`
      - `Build CLAP`
- [ ] Tests are green:
      - VST3 workflow runs `validator.exe` → 0 failures
      - VST3 workflow runs `test-zondel-engine.exe` + `test-realtime-budget.exe` → exit 0
      - CLAP workflow runs `clap-validator validate` → 0 failures

## Manual DAW validation

Run the matrix in [`DAW-MATRIX.md`](DAW-MATRIX.md). At least one
representative per format/category should pass before tagging:

- [ ] VST3: Reaper (smoke) + at least one Steinberg-family host
      (Cubase 14 or Studio One 7) + Ableton Live 12.
- [ ] CLAP: Reaper + Bitwig + FL Studio 2024.1+.
- [ ] OBS: 31.x on Windows 10 + Windows 11.

Pin the results in the release notes as a "Tested on" table.

## Tagging

```powershell
git checkout main
git pull
git tag -a vX.Y.Z -m "vX.Y.Z"
git push origin vX.Y.Z
```

The `release.yaml` workflow runs on tag push and:

1. Builds OBS plug-in via `build-project.yaml` (workflow_call).
2. Builds VST3 plug-in via `build-vst3.yaml` (workflow_call).
3. Builds CLAP plug-in via `build-clap.yaml` (workflow_call).
4. Builds the OBS Inno installer (the VST3 + CLAP workflows build their
   own installers).
5. Collects every `.zip` / `.exe`, generates `SHA256SUMS.txt`, creates
   the GitHub Release with extracted changelog notes.

## Post-release

- [ ] Verify the GitHub Release page lists all six artefacts:
      - `zondel-obs-plugin-vX.Y.Z-windows-x64.zip`
      - `zondel-obs-plugin-vX.Y.Z-setup.exe`
      - `Zondel-vst3-vX.Y.Z-windows-x64.zip`
      - `Zondel-vst3-vX.Y.Z-setup.exe`
      - `Zondel-clap-vX.Y.Z-windows-x64.zip`
      - `Zondel-clap-vX.Y.Z-setup.exe`
      - `SHA256SUMS.txt`
- [ ] Download each `setup.exe` and confirm it installs to the canonical
      path:
      - OBS: `%ProgramFiles%\obs-studio\obs-plugins\64bit\`
      - VST3: `%CommonProgramFiles%\VST3\Zondel.vst3\`
      - CLAP: `%CommonProgramFiles%\CLAP\Zondel.clap`
- [ ] Smoke-test each installer's uninstall path leaves no orphans.
- [ ] Update <https://zondel.net/download> page links if URLs changed.
- [ ] Announce in the project Discord / mailing list as appropriate.

## Pre-release / RC tags

Tags containing a dash (`v0.2.0-rc.1`, `v0.2.0-beta.3`) are picked up
by `release.yaml` and marked as **pre-release** automatically. Use for:

- Beta builds going to a known tester pool
- RC builds for the DAW-matrix validation pass
- Pipeline smoke-tests of the release wiring itself
