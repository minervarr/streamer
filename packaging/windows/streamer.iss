; Streamer — Windows installer (Inno Setup 6).
;
; AppId is fixed permanently. Do not regenerate it: it is the only thing
; that lets Inno Setup recognize "this is the same app, a newer version" and
; upgrade an existing install's files in place, instead of installing a
; second copy side-by-side.
;
; MyAppVersion/MyStageDir/MyOutDir are passed in via /D from
; scripts/windows/package.ps1. The fallbacks below only exist so this file
; can be syntax-checked standalone.
#define MyAppName "Streamer"
#define MyAppExeName "streamer_gui.exe"
#ifndef MyAppVersion
  #define MyAppVersion "0.0.0"
#endif
#ifndef MyStageDir
  #define MyStageDir "stage"
#endif
#ifndef MyOutDir
  #define MyOutDir "out"
#endif

[Setup]
AppId={{575DFCF4-AF78-4AC0-8795-79846200864E}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=nava
DefaultDirName={localappdata}\Streamer
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#MyOutDir}
OutputBaseFilename=streamer-setup-{#MyAppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\gui\{#MyAppExeName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"

; Every path here is what Inno Setup tracks and removes on uninstall.
; streamer's own data — %APPDATA%\streamer\config.toml, and whatever
; download_dir/.streamer/library.db the user configured — lives entirely
; outside {app}, so nothing here can ever touch it on install or uninstall.
; Both streamer.exe (the CLI) and streamer_gui.exe (the GUI) are fully
; statically linked (see root CMakeLists.txt's -static/-static-libgcc/
; -static-libstdc++ block) — no MinGW runtime DLLs to ship, unlike
; Matrix Player. streamer_gui.exe still depends on the system's own
; vulkan-1.dll (GPU-driver-provided), never bundled by an installer.
[Files]
Source: "{#MyStageDir}\streamer.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyStageDir}\gui\streamer_gui.exe"; DestDir: "{app}\gui"; Flags: ignoreversion
Source: "{#MyStageDir}\gui\assets\*"; DestDir: "{app}\gui\assets"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\gui\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\gui\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\gui\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent
