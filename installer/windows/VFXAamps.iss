; VFXAamps — Windows installer (Inno Setup 6)
; Build:  iscc /DMyAppVersion=1.0.0 /DArtefacts=<...\VFXAamps_artefacts\Release> /DOutputDir=<dir> VFXAamps.iss

#ifndef MyAppVersion
  #define MyAppVersion "1.0.0"
#endif
#ifndef NumVersion
  #define NumVersion "1.0.0"
#endif
#ifndef Artefacts
  #define Artefacts "..\..\build\source\VFXAamps_artefacts\Release"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\build\dist"
#endif

#define MyAppName "VFXAamps"
#define MyPublisher "Viridian Audio Works"

[Setup]
AppId={{6E1F4C2A-9B7D-4E3A-A5C1-7D2F0B8E9A41}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
VersionInfoVersion={#NumVersion}
AppPublisher={#MyPublisher}
AppPublisherURL=https://github.com/vfxchris581/Vfxraman
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename={#MyAppName}-{#MyAppVersion}-Windows-x64-Setup
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayName={#MyAppName} {#MyAppVersion}

[Types]
Name: "full"; Description: "VST3 plug-in and standalone app"
Name: "custom"; Description: "Choose components"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in (C:\Program Files\Common Files\VST3)"; Types: full custom
Name: "standalone"; Description: "Standalone app"; Types: full custom

[Files]
Source: "{#Artefacts}\VST3\{#MyAppName}.vst3\*"; DestDir: "{commoncf64}\VST3\{#MyAppName}.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Artefacts}\Standalone\{#MyAppName}.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppName}.exe"; Components: standalone
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppName}.exe"; Components: standalone; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Components: standalone; Flags: unchecked

[Run]
Filename: "{app}\{#MyAppName}.exe"; Description: "Launch {#MyAppName}"; Components: standalone; Flags: nowait postinstall skipifsilent
