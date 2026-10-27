; GoTV versioned installer (Inno Setup 6).
; Built by CI: make -f Makefile.win setup VERSION=<x.y.z>
;   iscc.exe /Q "/DAppVersion=<x.y.z>" "/DAppVerNum=<x.y.z.0>" /DSourceDir=..\release-pkg windows/installer.iss
; Output: gotv-<x.y.z>-x64-setup.exe (per-user, no admin needed).

#ifndef AppVersion
  #define AppVersion "0.9.3"
#endif
#ifndef AppVerNum
  #define AppVerNum "0.9.3.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\release-pkg"
#endif
#define AppId "{{2dd52301-591f-470a-a8ca-06381a0169d8}}"

[Setup]
AppId={#AppId}
AppName=GoTV
AppVersion={#AppVersion}
VersionInfoVersion={#AppVerNum}
VersionInfoCompany=GoTV Open Source Project
VersionInfoDescription=GoTV Setup
VersionInfoTextVersion={#AppVersion}
VersionInfoCopyright=Copyright (C) 2026 GoTV Project
VersionInfoProductName=GoTV
VersionInfoProductVersion={#AppVersion}
AppVerName=GoTV {#AppVersion}
AppPublisher=GoTV Project
AppPublisherURL=https://github.com/isthaison/gotv
AppSupportURL=https://github.com/isthaison/gotv
AppUpdatesURL=https://github.com/isthaison/gotv/releases
DefaultDirName={localappdata}\Programs\GoTV
; Same AppId keeps the existing install root/stub on upgrades.
UsePreviousAppDir=yes
PrivilegesRequired=lowest
OutputDir=..
OutputBaseFilename=gotv-{#AppVersion}-x64-setup
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; The running tray takes over gracefully through the takeover event (see
; windows/main.c), and updates only ADD a new ver\<V>\ payload dir, so setup
; never asks the user to close anything and never replaces running files.
; NEVER auto-close apps: CloseApplications once killed Explorer (black
; screen) and hangs in /VERYSILENT (unanswerable dialog).
CloseApplications=no
WizardStyle=modern
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\ver\{#AppVersion}\gotv.exe
LicenseFile=..\LICENSE
SetupIconFile=icons\gotv.ico
ShowLanguageDialog=no

[Languages]
; English-only on purpose: Inno's bundled Vietnamese.isl is unofficial and
; the choco-installed compiler does not ship it. The app UI is Vietnamese.
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
english.StartupDesc=Start with Windows
english.DesktopDesc=Create a desktop icon

[Tasks]
Name: "startup"; Description: "{cm:StartupDesc}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "desktopicon"; Description: "{cm:DesktopDesc}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Stable root: stub + current pointer. No ignoreversion on the
; stub: Inno compares its VERSIONINFO and replaces it only when the staged
; stub is newer, so day-to-day updates never touch a possibly-loaded stub
; and no app is ever closed for an update. (restartreplace stays as the
; safety net for the rare stub-version bump itself.)
Source: "{#SourceDir}\gtv_tsf.dll"; DestDir: "{app}"; Flags: skipifsourcedoesntexist restartreplace
Source: "{#SourceDir}\current.txt"; DestDir: "{app}"; Flags: ignoreversion
; Versioned payload: each release adds a new ver\<V>\ dir (tray, engine DLL,
; runtime, data). Running processes keep their mapped files until they exit
; (Chrome-updater model); new processes load the current payload.
Source: "{#SourceDir}\ver\*"; DestDir: "{app}\ver"; Flags: ignoreversion restartreplace recursesubdirs createallsubdirs
Source: "{#SourceDir}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{userprograms}\GoTV\GoTV"; Filename: "{app}\ver\{#AppVersion}\gotv.exe"
Name: "{userprograms}\GoTV\Gõ Tiếng Việt"; Filename: "{app}\ver\{#AppVersion}\gotv.exe"; Comment: "Mở GoTV"
Name: "{userprograms}\GoTV\Gỡ bỏ cài đặt"; Filename: "{uninstallexe}"
Name: "{userdesktop}\GoTV"; Filename: "{app}\ver\{#AppVersion}\gotv.exe"; Tasks: desktopicon

[Registry]
; Same Run value the tray menu manages, so both stay in sync.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "GoTV"; ValueData: """{app}\ver\{#AppVersion}\gotv.exe"""; Flags: uninsdeletevalue; Tasks: startup

[Run]
; regsvr32 loads from System32, so its DLL search misses our bundled glib
; next to gtv_tsf.dll: prepend {app} to PATH or registration silently
; fails and no keyboard appears in language settings.
; Skipped on updates (Check): the stub path never changes once registered.
Filename: "{cmd}"; Parameters: "/c set ""PATH={app};%PATH%"" && regsvr32.exe /s ""{app}\gtv_tsf.dll"""; Flags: runhidden; Check: NeedsStubRegistration
; One-time MACHINE registration of the TSF profiles/categories (HKLM):
; per-user Register/AddLanguageProfile always fail, leaving the keyboard
; invisible to Windows. Verb "runas" prompts for admin once.
; Skipped on updates (Check): no UAC prompt for day-to-day updates.
Filename: "{app}\ver\{#AppVersion}\gotv.exe"; Parameters: "--register-tsf"; Verb: "runas"; Flags: runhidden waituntilterminated shellexec; StatusMsg: "Dang ky keyboard GoTV voi he thong..."; Check: NeedsTsfProfile
; Takeover: the new payload asks any running tray to exit gracefully.
Filename: "{app}\ver\{#AppVersion}\gotv.exe"; Parameters: "--takeover"; Description: "{cm:LaunchProgram,GoTV}"; Flags: nowait postinstall

[UninstallRun]
; Ask the running tray to exit gracefully first (takeover event).
Filename: "{app}\ver\{#AppVersion}\gotv.exe"; Parameters: "--quit"; Flags: runhidden waituntilterminated skipifdoesntexist
Filename: "{cmd}"; Parameters: "/c set ""PATH={app};%PATH%"" && regsvr32.exe /s /u ""{app}\gtv_tsf.dll"""; Flags: runhidden
; The tray menu manages this same value independently of the installer's
; [Tasks]/[Registry] entry, so delete it explicitly (stale autostart would
; point at the removed {app} after uninstall).
Filename: "{cmd}"; Parameters: "/c reg delete ""HKCU\Software\Microsoft\Windows\CurrentVersion\Run"" /v GoTV /f >nul 2>nul & exit /b 0"; Flags: runhidden; RunOnceId: "DelRun"
; The elevated logon task is created by the app via schtasks (admin mode),
; never by the installer, so remove it here (needs elevation itself when the
; task is elevated; best-effort when running asInvoker).
Filename: "schtasks.exe"; Parameters: "/Delete /TN GoTV /F"; Flags: runhidden; RunOnceId: "DelTask"
Filename: "schtasks.exe"; Parameters: "/Delete /TN GoTiengViet /F"; Flags: runhidden; RunOnceId: "DelLegacyTask"
Filename: "{cmd}"; Parameters: "/c reg delete ""HKCU\Software\Microsoft\Windows\CurrentVersion\Run"" /v GoTiengViet /f >nul 2>nul & exit /b 0"; Flags: runhidden; RunOnceId: "DelLegacyRun"

[UninstallDelete]
; Catch strays the [Files] list never owned (downloaded deps, update
; leftovers): {app} holds no user data (config lives in %APPDATA%).
Type: filesandordirs; Name: "{app}"

[Code]
{ Skip re-registration on updates: the stub path never changes, so once the
  COM class and the TSF profile exist, updates install with no regsvr32 and
  no elevation (no UAC prompt on day-to-day updates). }
function NeedsStubRegistration(): Boolean;
begin
  Result := not RegValueExists(HKEY_CURRENT_USER,
    'Software\Classes\CLSID\{E3B0C442-98FC-4F2E-9C8F-7B2A3E1D4C5B}\InprocServer32', '');
end;

function NeedsTsfProfile(): Boolean;
begin
  { Re-run elevated TSF registration when the Vietnamese profile is missing
    or the stale English profile is still present —
    one UAC prompt, then never again. }
  Result := not RegKeyExists(HKEY_LOCAL_MACHINE,
    'SOFTWARE\Microsoft\CTF\TIP\{E3B0C442-98FC-4F2E-9C8F-7B2A3E1D4C5B}\LanguageProfile\0x0000042a');
  if not Result then
    Result := RegKeyExists(HKEY_LOCAL_MACHINE,
      'SOFTWARE\Microsoft\CTF\TIP\{E3B0C442-98FC-4F2E-9C8F-7B2A3E1D4C5B}\LanguageProfile\0x00000409');
end;
