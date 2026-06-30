#define MyAppName "CryptoArchive"
#define MyAppVersion "0.3.0"
#define MyAppPublisher "CryptoArchive"
#define MyAppExeName "CryptoArchive.exe"
#define SourceDir "..\out\release\CryptoArchive"

[Setup]
AppId={{8B7E9E30-6F2D-4A08-9E78-5C4A6B36F001}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={localappdata}\Programs\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=..\out\release
OutputBaseFilename=CryptoArchive-InnoSetup
SetupIconFile=..\assets\CryptoArchive.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
ChangesAssociations=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: checkedonce

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Classes\.cryptsh"; ValueType: string; ValueName: ""; ValueData: "CryptoArchive.cryptsh"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.cryptsh\OpenWithProgids"; ValueType: string; ValueName: "CryptoArchive.cryptsh"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\CryptoArchive.cryptsh"; ValueType: string; ValueName: ""; ValueData: "CryptoArchive encrypted archive"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\CryptoArchive.cryptsh\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"
Root: HKCU; Subkey: "Software\Classes\CryptoArchive.cryptsh\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""

Root: HKCU; Subkey: "Software\Classes\*\shell\CryptoArchiveAdd"; ValueType: string; ValueName: ""; ValueData: "Add to CryptoArchive"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\*\shell\CryptoArchiveAdd"; ValueType: string; ValueName: "Icon"; ValueData: "{app}\{#MyAppExeName},0"
Root: HKCU; Subkey: "Software\Classes\*\shell\CryptoArchiveAdd"; ValueType: string; ValueName: "MultiSelectModel"; ValueData: "Player"
Root: HKCU; Subkey: "Software\Classes\*\shell\CryptoArchiveAdd\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" --add ""%1"""

Root: HKCU; Subkey: "Software\Classes\Directory\shell\CryptoArchiveAdd"; ValueType: string; ValueName: ""; ValueData: "Add to CryptoArchive"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Directory\shell\CryptoArchiveAdd"; ValueType: string; ValueName: "Icon"; ValueData: "{app}\{#MyAppExeName},0"
Root: HKCU; Subkey: "Software\Classes\Directory\shell\CryptoArchiveAdd"; ValueType: string; ValueName: "MultiSelectModel"; ValueData: "Player"
Root: HKCU; Subkey: "Software\Classes\Directory\shell\CryptoArchiveAdd\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" --add ""%1"""

Root: HKCU; Subkey: "Software\Classes\Directory\Background\shell\CryptoArchiveAdd"; ValueType: string; ValueName: ""; ValueData: "Add current folder to CryptoArchive"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Directory\Background\shell\CryptoArchiveAdd"; ValueType: string; ValueName: "Icon"; ValueData: "{app}\{#MyAppExeName},0"
Root: HKCU; Subkey: "Software\Classes\Directory\Background\shell\CryptoArchiveAdd\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" --add ""%V"""

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent
