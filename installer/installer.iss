; License: GPL-2.0-or-later. Copyright (c) 2026 Zondel.
; Built with Inno Setup 6.

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif

[Setup]
AppId={{F1C9A37E-7B1F-4F3E-9B71-3A8A8B7C2D11}}
AppName=Zondel for OBS Studio
AppVersion={#AppVersion}
AppPublisher=Zondel
AppPublisherURL=https://zondel.net
AppSupportURL=https://github.com/smurz/zondel-bridge/issues
AppUpdatesURL=https://github.com/smurz/zondel-bridge/releases
DefaultDirName={autopf}\obs-studio
DefaultGroupName=Zondel for OBS Studio
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=Output
OutputBaseFilename=zondel-obs-plugin-v{#AppVersion}-setup
SetupIconFile=installer-icon.ico
Compression=lzma
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Files]
; Paths verified against cmake RelWithDebInfo output:
;   DLL lives flat in build_x64\rundir\RelWithDebInfo\
;   Locale lives in build_x64\rundir\RelWithDebInfo\zondel-obs-plugin\locale\
Source: "..\build_x64\rundir\RelWithDebInfo\zondel-obs-plugin.dll"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion
Source: "..\build_x64\rundir\RelWithDebInfo\zondel-obs-plugin\locale\en-US.ini"; DestDir: "{app}\data\obs-plugins\zondel-obs-plugin\locale"; Flags: ignoreversion

[Code]
function InitializeSetup(): Boolean;
var
  ObsExe: string;
begin
  ObsExe := ExpandConstant('{autopf}\obs-studio\bin\64bit\obs64.exe');
  if not FileExists(ObsExe) then begin
    MsgBox('OBS Studio does not appear to be installed at the default location.' + #13#10 +
           'You can continue, but you may need to choose a custom install path.', mbInformation, MB_OK);
  end;
  Result := True;
end;
