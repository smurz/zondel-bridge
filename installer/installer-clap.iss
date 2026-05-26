; License: MIT. Copyright (c) 2026 Zondel.
; Inno Setup script for the Zondel CLAP plug-in.
; Built with Inno Setup 6.

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif

[Setup]
; Distinct AppId from the OBS and VST3 installers.
AppId={{93B58F26-1FC0-4D71-B23E-7F2F8E3D4B58}}
AppName=Zondel CLAP
AppVersion={#AppVersion}
AppPublisher=Zondel
AppPublisherURL=https://zondel.net
AppSupportURL=https://github.com/smurz/zondel-bridge/issues
AppUpdatesURL=https://github.com/smurz/zondel-bridge/releases
; Canonical Windows CLAP location: %COMMONPROGRAMFILES%\CLAP
DefaultDirName={commonpf}\Common Files\CLAP
DisableDirPage=yes
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=Output
OutputBaseFilename=Zondel-clap-v{#AppVersion}-setup
SetupIconFile=installer-icon.ico
Compression=lzma
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Files]
; CLAP plug-ins ship as a single .clap file (a renamed DLL on Windows).
Source: "..\build_x64\src\clap\Release\Zondel.clap"; DestDir: "{app}"; \
    Flags: ignoreversion

[UninstallDelete]
Type: files; Name: "{app}\Zondel.clap"
