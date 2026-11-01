; Kural for Windows: setup wizard (Inno Setup 6.7 or newer: dark style, custom colours, PNG artwork).
; Artwork: packaging/windows/make_images.py (wizard.png, wizard-small.png, kural.ico).
; Built by GitHub Actions (.github/workflows/build.yml) after the plugin has passed its checks:
;   iscc /DAppVersion=0.1.0 /DPluginDir=<folder holding Kural.vst3> /DOutDir=<output folder> packaging\windows\kural.iss
; Installs Kural.vst3 into the VST3 folder (C:\Program Files\Common Files\VST3 unless chosen otherwise),
; replaces an earlier version cleanly and registers an uninstaller (Settings -> Apps).
; Administrator rights are needed because the standard VST3 folder is a system folder.

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef PluginDir
  #define PluginDir "..\..\dist"
#endif
#ifndef OutDir
  #define OutDir "..\..\installer"
#endif

[Setup]
AppId={{5E2B9C71-4F0A-4D8E-9B3C-71A6D2E84F19}
AppName=Kural
AppVersion={#AppVersion}
AppVerName=Kural {#AppVersion}
AppPublisher=R4N
AppPublisherURL=https://github.com/R4N-WAV/Kural
AppSupportURL=https://github.com/R4N-WAV/Kural/releases/tag/latest
VersionInfoVersion={#AppVersion}
VersionInfoCompany=R4N
VersionInfoDescription=Kural setup
DefaultDirName={commoncf64}\VST3
DirExistsWarning=no
AppendDefaultDirName=no
DisableProgramGroupPage=yes
DisableWelcomePage=no
UsePreviousAppDir=yes
UninstallDisplayName=Kural (VST3) by R4N
UninstallDisplayIcon={uninstallexe}
; the uninstaller and the guide live in Program Files\R4N\Kural, never in the VST3 folder
UninstallFilesDir={commonpf64}\R4N\Kural
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=admin
WizardStyle=modern dark
WizardBackColor=#030c13
WizardImageFile=wizard.png
WizardImageBackColor=#030c13
WizardSmallImageFile=wizard-small.png
WizardSmallImageBackColor=#030c13
SetupIconFile=kural.ico
OutputDir={#OutDir}
OutputBaseFilename=Kural-Setup-Windows
Compression=lzma2/max
SolidCompression=yes
CloseApplications=no

[Messages]
WelcomeLabel1=Welcome to Kural
WelcomeLabel2=The vocal harmonizer by R4N.%n%nThis is a development build ({#AppVersion}) with four harmony voices: Kural follows your vocal and sings harmonies in the key you choose. More voices, presets and MIDI control arrive in coming versions.%n%nThis installs Kural (VST3, 64-bit) for FL Studio, Cubase, Reaper and other VST3 hosts.%n%nPlease close your music app before you continue.
SelectDirLabel3=Kural.vst3 goes into this VST3 folder. The standard folder is right for almost every host; only change it if your host scans a different one.
FinishedHeadingLabel=Kural is installed
FinishedLabelNoIcons=Open your music app and rescan your plugins:%n%nFL Studio: Options > Manage plugins > Find installed plugins.%nCubase: Studio > VST Plug-in Manager > Rescan (or restart Cubase).%nReaper: Options > Preferences > Plug-ins > VST > Re-scan.%n%nKural is listed under R4N. Put it on a vocal track, set the key, and play the track.

[InstallDelete]
; replace an earlier version completely (old and new files are never mixed)
Type: filesandordirs; Name: "{app}\Kural.vst3"

[Files]
Source: "{#PluginDir}\Kural.vst3\*"; DestDir: "{app}\Kural.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\INSTALL.md"; DestDir: "{commonpf64}\R4N\Kural"; Flags: ignoreversion

[UninstallDelete]
Type: filesandordirs; Name: "{app}\Kural.vst3"
Type: dirifempty; Name: "{commonpf64}\R4N\Kural"
Type: dirifempty; Name: "{commonpf64}\R4N"

[Code]
// a host that has the plugin loaded keeps its file locked: say so instead of failing halfway
function PrepareToInstall (var NeedsRestart: Boolean): String;
var
  f: String;
begin
  Result := '';
  f := ExpandConstant ('{app}\Kural.vst3\Contents\x86_64-win\Kural.vst3');
  if FileExists (f) and not RenameFile (f, f + '.check') then
    Result := 'Kural is in use (your music app has it loaded). Close the app and click Back, then Next to try again.'
  else if FileExists (f + '.check') then
    RenameFile (f + '.check', f);
end;
