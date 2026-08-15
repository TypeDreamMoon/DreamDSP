; DreamDSP installer.
;
; Built by .github/workflows/release.yml, which passes AppVersion, StageDir and
; OutDir. Buildable by hand too:
;
;   ISCC.exe /DAppVersion=0.2.0 /DStageDir=..\build\dreamdsp /DOutDir=..\build\installer DreamDSP.iss
;
; Two things this installer deliberately does NOT do:
;
;   * register the audio processing object. That writes HKLM, restarts the
;     audio service and puts code inside audiodg.exe -- a thing to opt into
;     from the settings page, having read what it says, not a thing to have
;     happen because you ran a setup program.
;   * install for all users. Everything it writes lives under one user's
;     profile, so it needs no elevation at all; the one action that does need
;     it asks for itself, once, when you use it.
;
; Uninstalling, on the other hand, DOES remove the processing object, because
; leaving a registered component pointing at deleted files would give the
; endpoint an effect chain that cannot load.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef StageDir
  #define StageDir "..\build\dreamdsp"
#endif
#ifndef OutDir
  #define OutDir "..\build\installer"
#endif

#define AppName "DreamDSP"
#define AppPublisher "TypeDreamMoon"
#define AppUrl "https://github.com/TypeDreamMoon/DreamDSP"
#define AppExe "DreamDSP.exe"

[Setup]
AppId={{6D2F1C55-5E4B-4A7E-9C31-0D5A6C4B7E11}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}/issues
AppUpdatesURL={#AppUrl}/releases
VersionInfoVersion={#AppVersion}

; Per-user, so no elevation is needed to install or to update.
PrivilegesRequired=lowest
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir={#OutDir}
OutputBaseFilename={#AppName}-{#AppVersion}-win64-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

; The updater launches this with /SILENT while the old copy is still shutting
; down; give it a moment rather than failing on a locked file.
CloseApplications=yes
CloseApplicationsFilter=*.exe,*.dll
RestartApplications=no

[Languages]
Name: "chinese"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加任务:"
Name: "autostart";   Description: "开机自动启动(可在设置页随时改)"; GroupDescription: "附加任务:"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\卸载 {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon
Name: "{userstartup}\{#AppName}"; Filename: "{app}\{#AppExe}"; Parameters: "--tray"; Tasks: autostart

[Run]
Filename: "{app}\{#AppExe}"; Description: "立即启动 {#AppName}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; Removes the CLSID and the AudioEngine registration, and detaches the object
; from every endpoint. Runs before the files go, because it is one of them --
; and elevated, because unregistering writes HKLM.
;
; runascurrentuser is deliberately absent: this is the one part of uninstalling
; that needs administrator rights, and skipping it would leave every endpoint
; it had been attached to pointing at a component that no longer exists.
Filename: "{app}\{#AppExe}"; Parameters: "--apo-uninstall --apo-restart-audio"; \
    Flags: waituntilterminated skipifdoesntexist runhidden; \
    StatusMsg: "正在移除音频组件…"

[UninstallDelete]
; The staged copy of the processing object and its diagnostics. The parameter
; files stay: they are the user's settings, and reinstalling should find them.
Type: files; Name: "{commonappdata}\DreamDSP\DreamDspApo.dll"
Type: files; Name: "{commonappdata}\DreamDSP\apo.log"
Type: files; Name: "{commonappdata}\DreamDSP\install.log"
