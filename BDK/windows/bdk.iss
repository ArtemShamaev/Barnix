; Compile after make bdk: ISCC.exe BDK\windows\bdk.iss
#ifndef BDKVersion
  #define BDKVersion "0.1.0"
#endif
#ifndef BDKDist
  #define BDKDist "..\dist"
#endif
[Setup]
AppId={{5AEE22C9-9D78-46C7-AEDB-55AE93532701}
AppName=Barnino Systems BDK
AppVersion={#BDKVersion}
AppPublisher=Barnino Systems
DefaultDirName={localappdata}\Programs\Barnino\BDK
DefaultGroupName=Barnino Systems BDK
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#BDKDist}
OutputBaseFilename=Barnino.Systems.BDK-{#BDKVersion}-win64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Barnino Systems BDK
SetupLogging=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Files]
Source: "{#BDKDist}\sdk\*"; DestDir: "{app}\sdk"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BDKDist}\Barnino.Systems.BDK.Templates.{#BDKVersion}.nupkg"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BDKDist}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BDKDist}\PROJECT.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "setup.ps1"; DestDir: "{app}\windows"; Flags: ignoreversion

[Icons]
Name: "{group}\BDK documentation"; Filename: "{app}\README.md"
Name: "{group}\Repair BDK setup"; Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\windows\setup.ps1"" -InstallDir ""{app}"""
Name: "{group}\Uninstall BDK"; Filename: "{uninstallexe}"

[Code]
function PowerShell: String;
begin
  Result := ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe');
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ExitCode: Integer;
  Params: String;
begin
  if CurStep = ssPostInstall then begin
    WizardForm.StatusLabel.Caption := 'Installing build tools and checking BDK. This may take several minutes...';
    Params := '-NoProfile -ExecutionPolicy Bypass -File "' + ExpandConstant('{app}\windows\setup.ps1') +
      '" -InstallDir "' + ExpandConstant('{app}') + '"';
    if not Exec(PowerShell, Params, '', SW_SHOW, ewWaitUntilTerminated, ExitCode) then
      RaiseException('Could not start BDK configuration. Use Repair BDK setup from the Start menu.');
    if ExitCode <> 0 then
      RaiseException('BDK configuration failed. See %LOCALAPPDATA%\Barnino\BDK\setup.log and use Repair BDK setup.');
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ExitCode: Integer;
begin
  if CurUninstallStep = usUninstall then begin
    if not Exec(PowerShell, '-NoProfile -ExecutionPolicy Bypass -File "' +
      ExpandConstant('{app}\windows\setup.ps1') + '" -InstallDir "' + ExpandConstant('{app}') +
      '" -Uninstall', '', SW_HIDE, ewWaitUntilTerminated, ExitCode) or (ExitCode <> 0) then
      MsgBox('Could not unregister BDK templates. Run: dotnet new uninstall Barnino.Systems.BDK.Templates', mbError, MB_OK);
  end;
end;
