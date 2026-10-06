; Remember — Inno Setup script
;
; Builds Remember-<version>-setup.exe in ../dist/.
;
; Expects fresh Release builds at:
;   ../App/build/RememberApp_artefacts/Release/Remember.exe
;   ../Plugin/build/RememberPlugin_artefacts/Release/VST3/Remember.vst3/...
;
; Invoke directly with:
;   ISCC.exe Remember.iss
;
; or use ../installer/build-release.bat which builds Release first and then
; runs ISCC on this script.

#define MyAppName       "Remember"
#define MyAppVersion    "1.0.0-beta3"
#define MyAppPublisher  "Martin Garcia Pacheco"
#define MyAppURL        "https://github.com/martingarciapacheco/remember"
#define MyAppExeName    "Remember.exe"

[Setup]
; AppId uniquely identifies the application (used for upgrade detection).
; DO NOT change this between releases of the same product — a new GUID
; would make every release look like a different product to Windows.
AppId={{C3F5A6E2-9F2D-4C8A-9F0E-1D2B3C4E5F60}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename={#MyAppName}-{#MyAppVersion}-setup
Compression=lzma
SolidCompression=yes
WizardStyle=modern
; Beta marker in the title bar so the tester always sees it
SetupIconFile=
UninstallDisplayName={#MyAppName} {#MyAppVersion}
; VersionInfoVersion / VersionInfoProductVersion must be pure numeric (a.b.c.d).
; The user-visible string {#MyAppVersion} (which contains "-beta3") is shown in
; the wizard caption via AppVersion above.
VersionInfoVersion=1.0.0.3
VersionInfoCompany={#MyAppPublisher}
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion=1.0.0.3

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut for the browser app"; \
    GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
; ── Browser app ─────────────────────────────────────────────────────────────
Source: "..\App\build\RememberApp_artefacts\Release\Remember.exe"; \
    DestDir: "{app}"; Flags: ignoreversion

; ── VST3 bundle (entire directory tree, including moduleinfo.json) ──────────
Source: "..\Plugin\build\RememberPlugin_artefacts\Release\VST3\Remember.vst3\*"; \
    DestDir: "{commoncf64}\VST3\Remember.vst3"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

; ── Release notes (so testers find them in the install dir) ─────────────────
Source: "..\BETA_NOTES.md"; DestDir: "{app}"; Flags: ignoreversion isreadme

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{#MyAppName} on GitHub"; Filename: "{#MyAppURL}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{commondesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; \
    Flags: nowait postinstall skipifsilent

[UninstallDelete]
; The plugin lives outside {app}; Inno's [Files] section already records it
; and uninstall will roll it back. This extra block catches any stray cache.
Type: filesandordirs; Name: "{commoncf64}\VST3\Remember.vst3"
