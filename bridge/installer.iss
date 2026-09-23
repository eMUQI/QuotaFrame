#ifndef AppVersion
  #error AppVersion is required
#endif
#ifndef SourceDir
  #define SourceDir "dist"
#endif
#ifndef OutputDir
  #define OutputDir "dist"
#endif
#ifndef AppId
  #define AppId "QuotaFrame.Bridge"
#endif

#ifndef GroupName
  #define GroupName "QuotaFrame"
#endif
#ifndef RunValue
  #define RunValue "QuotaFrameBridge"
#endif

[Setup]
AppId={#AppId}
AppName=QuotaFrame Bridge
AppVersion={#AppVersion}
AppPublisher=QuotaFrame
DefaultDirName={localappdata}\Programs\QuotaFrame
DefaultGroupName={#GroupName}
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
DisableProgramGroupPage=yes
DisableDirPage=auto
UsePreviousAppDir=yes
UninstallDisplayIcon={app}\quotaframe-bridge.exe
OutputDir={#OutputDir}
OutputBaseFilename=quotaframe-bridge-windows-v{#AppVersion}-setup
SetupIconFile=..\assets\tray\quotaframe-bridge.ico
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; OTA transfers may veto automatic shutdown.
CloseApplications=yes
CloseApplicationsFilter=quotaframe-bridge.exe
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimplified"; MessagesFile: "{#ChineseMessages}"

[Files]
Source: "{#SourceDir}\quotaframe-bridge.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\THIRD_PARTY_LICENSES.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\QuotaFrame Bridge"; Filename: "{app}\quotaframe-bridge.exe"
Name: "{group}\{cm:UninstallProgram,QuotaFrame Bridge}"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\quotaframe-bridge.exe"; Description: "{cm:LaunchProgram,QuotaFrame Bridge}"; Flags: nowait postinstall skipifsilent

[Code]
const
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';
  RunValue = '{#RunValue}';

function AutostartCommand(): String;
begin
  Result := '"' + ExpandConstant('{app}\quotaframe-bridge.exe') + '" --autostarted';
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Command: String;
begin
  { An existing opt-in follows the installed executable, including portable migrations. }
  if (CurStep = ssPostInstall) and
     RegQueryStringValue(HKCU, RunKey, RunValue, Command) and (Command <> '') then
    RegWriteStringValue(HKCU, RunKey, RunValue, AutostartCommand());
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Command: String;
begin
  if (CurUninstallStep = usUninstall) and
     RegQueryStringValue(HKCU, RunKey, RunValue, Command) and
     (CompareText(Command, AutostartCommand()) = 0) then
    RegDeleteValue(HKCU, RunKey, RunValue);
end;
