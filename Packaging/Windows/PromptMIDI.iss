; PromptMIDI installer for Windows (Inno Setup 6.6 or newer).
;
; Build the plug-in, stage it, then compile this script:
;   bash Packaging/stage.sh build/PromptMIDI_artefacts/Release build/stage 1.2.0
;   iscc /DAppVersion=1.2.0 /DStage=..\..\build\stage /O..\..\build Packaging\Windows\PromptMIDI.iss
;
; The installer lets people tick the parts they want (VST3, VST2 if the build has one, the app,
; the ReadMe) and choose a folder for each. The instrument sounds are always installed, once, in
; C:\ProgramData\PromptMIDI\Sounds, where every format finds them. Nothing else is needed: the
; binaries link the C++ runtime statically.
;
; Silent install (all of these are optional):
;   PromptMIDI-Windows-Setup.exe /VERYSILENT /COMPONENTS="vst3,standalone,readme"
;       /VST3DIR="D:\Plugins\VST3" /VST2DIR="D:\Plugins\VST2" /DIR="D:\Apps\PromptMIDI"

#if Ver < EncodeVer(6, 6, 0)
  #error PromptMIDI's installer needs Inno Setup 6.6 or newer (it uses the dark wizard style).
#endif

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef Stage
  #define Stage SourcePath + "..\..\build\stage"
#endif
#define Art SourcePath + "..\Art"

#if FileExists(Stage + "\VST2\PromptMIDI.dll")
  #define HaveVST2
#endif

[Setup]
AppId={{8E3C6B2A-5D4F-4B7E-9A61-2F0C7D9B4E15}
AppName=PromptMIDI
AppVersion={#AppVersion}
AppVerName=PromptMIDI {#AppVersion}
AppPublisher=PromptMIDI
AppPublisherURL=https://becomedeath.github.io/TLComposer/promptmidi.html
AppSupportURL=https://github.com/BecomeDeath/PromptMIDI/issues
AppUpdatesURL=https://becomedeath.github.io/TLComposer/promptmidi.html#download
VersionInfoVersion={#AppVersion}
VersionInfoDescription=PromptMIDI installer
DefaultDirName={autopf}\PromptMIDI
DefaultGroupName=PromptMIDI
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=no
AlwaysShowComponentsList=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputBaseFilename=PromptMIDI-Windows-Setup
Compression=lzma2/ultra64
SolidCompression=yes
CloseApplications=yes
UninstallDisplayName=PromptMIDI
UninstallDisplayIcon={app}\PromptMIDI.ico
SetupIconFile={#Art}\PromptMIDI.ico
SetupLogging=yes

; The look: PromptMIDI's own dark palette and artwork.
WizardStyle=modern dark includetitlebar hidebevels
WizardSizePercent=110
WizardBackColor=#0d0f13
WizardImageFile={#Art}\wizard-100.png,{#Art}\wizard-150.png,{#Art}\wizard-200.png,{#Art}\wizard-250.png
WizardImageBackColor=#0d0f13
WizardSmallImageFile={#Art}\wizard-small-100.png,{#Art}\wizard-small-150.png,{#Art}\wizard-small-200.png,{#Art}\wizard-small-250.png
WizardSmallImageBackColor=none

[Messages]
WelcomeLabel1=Welcome to PromptMIDI
WelcomeLabel2=Describe the music in words, get multi-track MIDI.%n%nThis installs PromptMIDI [name/ver]. On the next pages you choose which parts to install (the VST3 plug-in, the standalone app and the ReadMe) and where each one goes.%n%nIf your DAW is open, close it before you continue.
SelectComponentsDesc=Tick the parts you want.
SelectComponentsLabel2=Tick what you want to install, then click Next. Everything you install shares one copy of the instrument sounds.
FinishedHeadingLabel=PromptMIDI is installed
FinishedLabel=Open your DAW, rescan plug-ins if it asks, and add PromptMIDI to an instrument track. Or start the app on its own.%n%nWhen there is a new version, PromptMIDI tells you inside its window.
FinishedLabelNoIcons=Open your DAW, rescan plug-ins if it asks, and add PromptMIDI to an instrument track.%n%nWhen there is a new version, PromptMIDI tells you inside its window.

[Types]
Name: "custom"; Description: "Choose what to install"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in  (most DAWs)"; Types: custom
#ifdef HaveVST2
Name: "vst2"; Description: "VST2 plug-in  (older DAWs)"; Types: custom
#endif
Name: "standalone"; Description: "Standalone app  (use PromptMIDI without a DAW)"; Types: custom
Name: "readme"; Description: "ReadMe"; Types: custom
Name: "sounds"; Description: "Instrument sounds for the preview  (required)"; Types: custom; Flags: fixed

[Tasks]
Name: "desktopicon"; Description: "Put a PromptMIDI shortcut on the desktop"; Components: standalone; Flags: unchecked

[InstallDelete]
; A clean bundle every time (also clears an older copy that carried the sounds inside).
Type: filesandordirs; Name: "{code:GetVST3Dir}\PromptMIDI.vst3"; Components: vst3

[Files]
Source: "{#Stage}\VST3\PromptMIDI.vst3\*"; DestDir: "{code:GetVST3Dir}\PromptMIDI.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
#ifdef HaveVST2
Source: "{#Stage}\VST2\PromptMIDI.dll"; DestDir: "{code:GetVST2Dir}"; Components: vst2; Flags: ignoreversion
#endif
Source: "{#Stage}\Standalone\PromptMIDI.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
; The sounds are always installed (the "sounds" component only shows that in the list).
Source: "{#Stage}\Sounds\*"; DestDir: "{commonappdata}\PromptMIDI\Sounds"; Flags: ignoreversion
Source: "{#Stage}\Docs\ReadMe.txt"; DestDir: "{app}"; Components: readme; Flags: ignoreversion
Source: "{#Stage}\Docs\Licences\*"; DestDir: "{app}\Licences"; Flags: ignoreversion
Source: "{#Art}\PromptMIDI.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\PromptMIDI"; Filename: "{app}\PromptMIDI.exe"; Components: standalone
Name: "{autodesktop}\PromptMIDI"; Filename: "{app}\PromptMIDI.exe"; Components: standalone; Tasks: desktopicon

[Run]
Filename: "{app}\PromptMIDI.exe"; Description: "Open PromptMIDI now"; Components: standalone; Flags: postinstall nowait skipifsilent
Filename: "{app}\ReadMe.txt"; Description: "Show the ReadMe"; Components: readme; Flags: postinstall shellexec skipifsilent unchecked

[UninstallDelete]
Type: dirifempty; Name: "{commonappdata}\PromptMIDI\Sounds"
Type: dirifempty; Name: "{commonappdata}\PromptMIDI"
Type: dirifempty; Name: "{app}"

[Code]
const
  RowVST3 = 0;
  RowVST2 = 1;
  RowApp = 2;

var
  FoldersPage: TInputDirWizardPage;
  RowTops: array[0..2] of Integer;

{ A /NAME=value command-line value, or Fallback. }
function ParamOr(const Name, Fallback: String): String;
begin
  Result := ExpandConstant('{param:' + Name + '}');
  if Result = '' then
    Result := Fallback;
end;

{ Where other VST2 plug-ins live, if a DAW or installer has registered that. }
function DefaultVST2Dir: String;
begin
  if not RegQueryStringValue(HKLM, 'SOFTWARE\VST', 'VSTPluginsPath', Result) or (Trim(Result) = '') then
    Result := ExpandConstant('{commonpf64}\Steinberg\VSTPlugins');
end;

function VST2Selected: Boolean;
begin
#ifdef HaveVST2
  Result := WizardIsComponentSelected('vst2');
#else
  Result := False;
#endif
end;

function GetVST3Dir(Param: String): String;
begin
  Result := FoldersPage.Values[RowVST3];
end;

function GetVST2Dir(Param: String): String;
begin
  Result := FoldersPage.Values[RowVST2];
end;

function IsAbsolutePath(const Path: String): Boolean;
begin
  Result := ((Length(Path) >= 3) and (Path[2] = ':') and (Path[3] = '\')) or (Copy(Path, 1, 2) = '\\');
end;

procedure InitializeWizard;
var
  Delta: Integer;
begin
  { One setup type, so the type list is just noise. }
  if WizardForm.TypesCombo.Visible then
  begin
    Delta := WizardForm.ComponentsList.Top - WizardForm.TypesCombo.Top;
    WizardForm.TypesCombo.Visible := False;
    WizardForm.ComponentsList.Top := WizardForm.TypesCombo.Top;
    WizardForm.ComponentsList.Height := WizardForm.ComponentsList.Height + Delta;
  end;

  FoldersPage := CreateInputDirPage(wpSelectComponents,
    'Choose where to install', 'Pick a folder for each part you ticked.',
    'The suggested folders are the standard ones, which DAWs scan for plug-ins. Change them only if your DAW looks somewhere else.',
    False, '');
  FoldersPage.Add('VST3 plug-in folder:');
  FoldersPage.Add('VST2 plug-in folder:');
  FoldersPage.Add('PromptMIDI app, ReadMe and uninstaller:');

  FoldersPage.Values[RowVST3] := ParamOr('VST3DIR', GetPreviousData('VST3Dir', ExpandConstant('{commoncf64}\VST3')));
  FoldersPage.Values[RowVST2] := ParamOr('VST2DIR', GetPreviousData('VST2Dir', DefaultVST2Dir));
  FoldersPage.Values[RowApp] := WizardForm.DirEdit.Text;

  RowTops[0] := FoldersPage.PromptLabels[0].Top;
  RowTops[1] := FoldersPage.PromptLabels[1].Top;
  RowTops[2] := FoldersPage.PromptLabels[2].Top;
end;

{ Shows or hides one folder row; visible rows stack up from the top. }
procedure PlaceRow(Row: Integer; Show: Boolean; var Slot: Integer);
var
  Dy: Integer;
begin
  FoldersPage.PromptLabels[Row].Visible := Show;
  FoldersPage.Edits[Row].Visible := Show;
  FoldersPage.Buttons[Row].Visible := Show;

  if Show then
  begin
    Dy := RowTops[Slot] - FoldersPage.PromptLabels[Row].Top;
    FoldersPage.PromptLabels[Row].Top := FoldersPage.PromptLabels[Row].Top + Dy;
    FoldersPage.Edits[Row].Top := FoldersPage.Edits[Row].Top + Dy;
    FoldersPage.Buttons[Row].Top := FoldersPage.Buttons[Row].Top + Dy;
    Slot := Slot + 1;
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
var
  Slot: Integer;
begin
  if CurPageID = FoldersPage.ID then
  begin
    Slot := 0;
    PlaceRow(RowVST3, WizardIsComponentSelected('vst3'), Slot);
    PlaceRow(RowVST2, VST2Selected, Slot);
    PlaceRow(RowApp, True, Slot);

    if WizardIsComponentSelected('standalone') then
      FoldersPage.PromptLabels[RowApp].Caption := 'PromptMIDI app, ReadMe and uninstaller:'
    else
      FoldersPage.PromptLabels[RowApp].Caption := 'PromptMIDI folder (ReadMe and uninstaller):';
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;

  if CurPageID = wpSelectComponents then
  begin
    if not (WizardIsComponentSelected('vst3') or VST2Selected or WizardIsComponentSelected('standalone')) then
    begin
      MsgBox('Tick at least one of the plug-in or the app.', mbError, MB_OK);
      Result := False;
    end;
  end
  else if CurPageID = FoldersPage.ID then
  begin
    if (WizardIsComponentSelected('vst3') and not IsAbsolutePath(Trim(FoldersPage.Values[RowVST3])))
       or (VST2Selected and not IsAbsolutePath(Trim(FoldersPage.Values[RowVST2])))
       or not IsAbsolutePath(Trim(FoldersPage.Values[RowApp])) then
    begin
      MsgBox('Choose a full folder path (such as C:\...) for each part.', mbError, MB_OK);
      Result := False;
      Exit;
    end;

    { {app} comes from the hidden standard folder page. }
    WizardForm.DirEdit.Text := Trim(FoldersPage.Values[RowApp]);
  end;
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  SetPreviousData(PreviousDataKey, 'VST3Dir', FoldersPage.Values[RowVST3]);
  SetPreviousData(PreviousDataKey, 'VST2Dir', FoldersPage.Values[RowVST2]);
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := MemoComponentsInfo + NewLine + NewLine + 'Folders:' + NewLine;

  if WizardIsComponentSelected('vst3') then
    Result := Result + Space + 'VST3:  ' + AddBackslash(FoldersPage.Values[RowVST3]) + 'PromptMIDI.vst3' + NewLine;
  if VST2Selected then
    Result := Result + Space + 'VST2:  ' + AddBackslash(FoldersPage.Values[RowVST2]) + 'PromptMIDI.dll' + NewLine;

  Result := Result + Space + 'App and ReadMe:  ' + FoldersPage.Values[RowApp] + NewLine
                   + Space + 'Instrument sounds:  ' + ExpandConstant('{commonappdata}\PromptMIDI\Sounds') + NewLine;

  if MemoTasksInfo <> '' then
    Result := Result + NewLine + MemoTasksInfo;
end;
