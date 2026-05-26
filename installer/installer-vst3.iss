; License: MIT. Copyright (c) 2026 Zondel.
; Inno Setup script for the Zondel VST3 plug-in.
; Built with Inno Setup 6.

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif

; Build directory parameterised so local dev (build_x64) and CI
; (build_vst3) both work without script edits. CI passes
; /DBuildDir=build_vst3.
#ifndef BuildDir
  #define BuildDir "build_x64"
#endif

; Build configuration name within the build dir. Local presets default
; to RelWithDebInfo; CI builds Release. Pass /DBuildConfig=Release etc.
#ifndef BuildConfig
  #define BuildConfig "RelWithDebInfo"
#endif

[Setup]
; Distinct AppId from the OBS installer so they uninstall independently.
AppId={{2E18C5D4-9A3A-4B17-8E0E-9B0C53F6A4D2}}
AppName=Zondel VST3
AppVersion={#AppVersion}
AppPublisher=Zondel
AppPublisherURL=https://zondel.net
AppSupportURL=https://github.com/smurz/zondel-bridge/issues
AppUpdatesURL=https://github.com/smurz/zondel-bridge/releases
; Canonical Windows VST3 location: %CommonProgramFiles%\VST3
DefaultDirName={commonpf}\Common Files\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=Output
OutputBaseFilename=Zondel-vst3-v{#AppVersion}-setup
SetupIconFile=installer-icon.ico
Compression=lzma
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Files]
; VST3 ships as a bundle directory. Inno Setup copies the bundle's
; contents recursively. The bundle layout is:
;   Zondel.vst3\
;     Contents\
;       x86_64-win\
;         Zondel.vst3        (the DLL)
;       moduleinfo.json
;       Resources\           (icons, presets — may be empty)
Source: "..\{#BuildDir}\VST3\{#BuildConfig}\Zondel.vst3\*"; \
    DestDir: "{app}\Zondel.vst3"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
; Inno cleans tracked files but leaves the bundle dir behind otherwise.
Type: filesandordirs; Name: "{app}\Zondel.vst3"
