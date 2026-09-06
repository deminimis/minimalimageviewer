[Setup]
AppName=Minimal Image Viewer
AppVersion=2.0.3
AppPublisher=deminimis
DefaultDirName={autopf}\Minimal Image Viewer
DefaultGroupName=Minimal Image Viewer
OutputDir=installer_output
OutputBaseFilename=MinimalImageViewer_Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile=..\src\app.ico
UninstallDisplayIcon={app}\MinimalImageViewer.exe
PrivilegesRequired=lowest
WizardStyle=modern

[Files]
Source: "..\src\x64\Release\MinimalImageViewer.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Minimal Image Viewer"; Filename: "{app}\MinimalImageViewer.exe"
Name: "{group}\Uninstall"; Filename: "{uninstallexe}"

[Registry]
; File associations - register MinimalImageViewer as viewer for all supported formats
; Each extension gets: ProgID, open command, and DefaultIcon

; --- JPG/JPEG ---
Root: HKCU; Subkey: "Software\Classes\.jpg"; ValueType: string; ValueName: ""; ValueData: "MinimalImageViewer.Image"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.jpeg"; ValueType: string; ValueName: ""; ValueData: "MinimalImageViewer.Image"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\MinimalImageViewer.Image"; ValueType: string; ValueName: ""; ValueData: "JPEG Image"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\MinimalImageViewer.Image\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\MinimalImageViewer.exe,0"
Root: HKCU; Subkey: "Software\Classes\MinimalImageViewer.Image\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\MinimalImageViewer.exe"" ""%1"""

; --- PNG ---
Root: HKCU; Subkey: "Software\Classes\.png"; ValueType: string; ValueName: ""; ValueData: "MinimalImageViewer.Image"; Flags: uninsdeletevalue

; --- BMP ---
Root: HKCU; Subkey: "Software\Classes\.bmp"; ValueType: string; ValueName: ""; ValueData: "MinimalImageViewer.Image"; Flags: uninsdeletevalue

; --- GIF ---
Root: HKCU; Subkey: "Software\Classes\.gif"; ValueType: string; ValueName: ""; ValueData: "MinimalImageViewer.Image"; Flags: uninsdeletevalue

; --- TIFF/TIF ---
Root: HKCU; Subkey: "Software\Classes\.tiff"; ValueType: string; ValueName: ""; ValueData: "MinimalImageViewer.Image"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.tif"; ValueType: string; ValueName: ""; ValueData: "MinimalImageViewer.Image"; Flags: uninsdeletevalue

; --- ICO ---
Root: HKCU; Subkey: "Software\Classes\.ico"; ValueType: string; ValueName: ""; ValueData: "MinimalImageViewer.Image"; Flags: uninsdeletevalue

; --- WebP ---
Root: HKCU; Subkey: "Software\Classes\.webp"; ValueType: string; ValueName: ""; ValueData: "MinimalImageViewer.Image"; Flags: uninsdeletevalue

; --- HEIF/HEIC ---
Root: HKCU; Subkey: "Software\Classes\.heif"; ValueType: string; ValueName: ""; ValueData: "MinimalImageViewer.Image"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.heic"; ValueType: string; ValueName: ""; ValueData: "MinimalImageViewer.Image"; Flags: uninsdeletevalue

; --- AVIF ---
Root: HKCU; Subkey: "Software\Classes\.avif"; ValueType: string; ValueName: ""; ValueData: "MinimalImageViewer.Image"; Flags: uninsdeletevalue

; --- RAW formats ---
Root: HKCU; Subkey: "Software\Classes\.cr2"; ValueType: string; ValueName: ""; ValueData: "MinimalImageViewer.Image"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.cr3"; ValueType: string; ValueName: ""; ValueData: "MinimalImageViewer.Image"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.nef"; ValueType: string; ValueName: ""; ValueData: "MinimalImageViewer.Image"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.dng"; ValueType: string; ValueName: ""; ValueData: "MinimalImageViewer.Image"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.arw"; ValueType: string; ValueName: ""; ValueData: "MinimalImageViewer.Image"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.orf"; ValueType: string; ValueName: ""; ValueData: "MinimalImageViewer.Image"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.rw2"; ValueType: string; ValueName: ""; ValueData: "MinimalImageViewer.Image"; Flags: uninsdeletevalue

; --- SVG ---
Root: HKCU; Subkey: "Software\Classes\.svg"; ValueType: string; ValueName: ""; ValueData: "MinimalImageViewer.Image"; Flags: uninsdeletevalue

; --- QOI ---
Root: HKCU; Subkey: "Software\Classes\.qoi"; ValueType: string; ValueName: ""; ValueData: "MinimalImageViewer.Image"; Flags: uninsdeletevalue

; --- HDR ---
Root: HKCU; Subkey: "Software\Classes\.hdr"; ValueType: string; ValueName: ""; ValueData: "MinimalImageViewer.Image"; Flags: uninsdeletevalue

; --- TGA ---
Root: HKCU; Subkey: "Software\Classes\.tga"; ValueType: string; ValueName: ""; ValueData: "MinimalImageViewer.Image"; Flags: uninsdeletevalue

; --- PSD ---
Root: HKCU; Subkey: "Software\Classes\.psd"; ValueType: string; ValueName: ""; ValueData: "MinimalImageViewer.Image"; Flags: uninsdeletevalue

; --- PPM/PGM/PBM/PNM/PIC ---
Root: HKCU; Subkey: "Software\Classes\.ppm"; ValueType: string; ValueName: ""; ValueData: "MinimalImageViewer.Image"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.pgm"; ValueType: string; ValueName: ""; ValueData: "MinimalImageViewer.Image"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.pbm"; ValueType: string; ValueName: ""; ValueData: "MinimalImageViewer.Image"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.pnm"; ValueType: string; ValueName: ""; ValueData: "MinimalImageViewer.Image"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.pic"; ValueType: string; ValueName: ""; ValueData: "MinimalImageViewer.Image"; Flags: uninsdeletevalue

[Run]
Filename: "{app}\MinimalImageViewer.exe"; Description: "Launch Minimal Image Viewer"; Flags: nowait postinstall skipifsilent
