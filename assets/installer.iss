; -- Setup Section --
[Setup]
AppName=MinimalImageViewer
AppVersion=1.3.1
DefaultDirName={autopf}\MinimalImageViewer
DefaultGroupName=MinimalImageViewer
OutputDir=.
OutputBaseFilename=MinimalImageViewer_Installer
Compression=lzma
SolidCompression=yes
SetupIconFile=minimallogo.ico
UninstallDisplayIcon={app}\MinimalImageViewer.exe

; -- Files to Install --
[Files]
Source: "..\..\..\MinimalImageViewer.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\..\minimallogo.ico"; DestDir: "{app}"; Flags: ignoreversion


; -- Optional Start Menu Shortcut --
[Icons]
Name: "{group}\MinimalImageViewer"; Filename: "{app}\MinimalImageViewer.exe"; IconFilename: "{app}\minimallogo.ico"

; -- File Type Associations --
[Registry]
Root: HKCR; Subkey: "MinimalImageViewer.Image"; ValueType: string; ValueData: "MinimalImageViewer Image File"; Flags: uninsdeletekey
Root: HKCR; Subkey: "MinimalImageViewer.Image\DefaultIcon"; ValueType: string; ValueData: "{app}\minimallogo.ico"
Root: HKCR; Subkey: "MinimalImageViewer.Image\shell\open\command"; ValueType: string; ValueData: """{app}\MinimalImageViewer.exe"" ""%1"""

; File extensions
#define ImageExt(ext) \
  "Root: HKCR; Subkey: + ext +; ValueType: string; ValueName: """"; ValueData: ""MinimalImageViewer.Image""; Flags: uninsdeletevalue"

{#ImageExt(".jpg")}
{#ImageExt(".jpeg")}
{#ImageExt(".png")}
{#ImageExt(".bmp")}
{#ImageExt(".gif")}
{#ImageExt(".tiff")}
{#ImageExt(".tif")}
{#ImageExt(".webp")}
{#ImageExt(".ico")}
{#ImageExt(".jfif")}
{#ImageExt(".heic")}
{#ImageExt(".avif")}

; -- Uninstaller Cleanup --
[UninstallDelete]
Type: filesandordirs; Name: "{app}"
