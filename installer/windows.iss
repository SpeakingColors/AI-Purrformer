; Inno Setup script for the Windows installer. AGPL-3.0. Built by .github/workflows/package.yml
; Expects a folder "dist" next to this file's parent with the layout the workflow assembles.
#define AppName "AI Purrformer"
#define AppVersion "0.1.0"

[Setup]
AppId={{8E2B6D41-3A7C-4F19-B5D2-6C1A9E07F3B4}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Studio 87
AppPublisherURL=https://github.com/SpeakingColors/AI-Purrformer
DefaultDirName={autopf}\Studio 87\AI Purrformer
DefaultGroupName=Studio 87\AI Purrformer
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir=..\out
OutputBaseFilename=AI-Purrformer-Setup
Compression=lzma2/fast
SolidCompression=no
DiskSpanning=yes
DiskSliceSize=1900000000
LicenseFile=..\dist\LICENSE
InfoBeforeFile=..\dist\INSTALL.txt
WizardStyle=modern
UninstallDisplayName={#AppName}

[Tasks]
Name: "autostart"; Description: "Start the AI Purrformer engine when I log in"; Flags: unchecked

[Messages]
SelectDirLabel3=The engine, its Python and the voice model (about 3 GB) go in this folder. The plugin itself (VST3) is installed separately to Common Files\VST3 so your DAW finds it.
SelectDirBrowseLabel=Click Next to continue, or Browse to pick another folder for the engine and model.

[Files]
Source: "..\dist\AI Purrformer.vst3\*"; DestDir: "{commoncf64}\VST3\AI Purrformer.vst3"; Flags: recursesubdirs ignoreversion
Source: "..\dist\AI Purrformer Sing.vst3\*"; DestDir: "{commoncf64}\VST3\AI Purrformer Sing.vst3"; Flags: recursesubdirs ignoreversion
Source: "..\dist\engine\*"; DestDir: "{app}\engine"; Flags: recursesubdirs ignoreversion
Source: "..\dist\python\*"; DestDir: "{app}\python"; Flags: recursesubdirs ignoreversion
Source: "..\dist\site-packages\*"; DestDir: "{app}\site-packages"; Flags: recursesubdirs ignoreversion
Source: "..\dist\start-engine.bat"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\INSTALL.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\MANUAL.txt"; DestDir: "{app}"; Flags: ignoreversion isreadme
Source: "..\dist\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\AI Purrformer Engine"; Filename: "{app}\start-engine.bat"; WorkingDir: "{app}"
Name: "{group}\Manual"; Filename: "{app}\MANUAL.txt"
Name: "{group}\Install notes"; Filename: "{app}\INSTALL.txt"
Name: "{group}\Uninstall"; Filename: "{uninstallexe}"
Name: "{userstartup}\AI Purrformer Engine"; Filename: "{app}\start-engine.bat"; WorkingDir: "{app}"; Tasks: autostart

[Run]
Filename: "{app}\MANUAL.txt"; Description: "Open the quick manual"; Flags: postinstall shellexec skipifsilent
Filename: "{app}\start-engine.bat"; Description: "Start the engine now"; Flags: postinstall nowait skipifsilent

[UninstallDelete]
; runtime leftovers (python caches, logs) so the uninstaller really frees the gigabytes
Type: filesandordirs; Name: "{app}"

[Code]
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  d: String;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    d := ExpandConstant('{userappdata}') + '\Studio 87\AI Purrformer';
    if (not UninstallSilent) and DirExists(d) then
      if MsgBox('Also delete your saved voices and settings?' + #13#10 + d + #13#10#13#10 + 'Choose No to keep voices you made.', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
        DelTree(d, True, True, True);
  end;
end;
