# Setting MinimalImageViewer as the Default Image Viewer

## Steps

1.  **Open Notepad** and copy the content below:

```
Windows Registry Editor Version 5.00

; Register MinimalImageViewer.exe as the default handler for .jpg files

[HKEY_CLASSES_ROOT\.jpg]
@="jpgfile"
"PerceivedType"="image"

[HKEY_CLASSES_ROOT\jpgfile\shell\open\command]
@="\"<FULL_PATH_TO_MINIMALIMAGEVIEWER_EXE>\" \"%1\""

[HKEY_CLASSES_ROOT\Applications\MinimalImageViewer.exe\shell\open\command]
@="\"<FULL_PATH_TO_MINIMALIMAGEVIEWER_EXE>\" \"%1\""

[HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.jpg\UserChoice]
"ProgId"="Applications\\MinimalImageViewer.exe"
```




    
2.  **Replace** `<FULL_PATH_TO_MINIMALIMAGEVIEWER_EXE>` with the full absolute path to your `.exe` file.
    
    -   Example: `C:\\Timothy\\Documents\\MinimalImageViewer.exe`
        
    -   Use **double backslashes (`\\`)** in paths.
        
3.  **Save the file** as `setdefault_viewer.reg. 
    
4. Double-click it and allow registry changes


---


If the problem persists with other file types (I have not had this issue), you can also add them the same way, by created a `.reg`, adding:

```
[HKEY_CLASSES_ROOT\.png]
@="pngfile"
"PerceivedType"="image"

[HKEY_CLASSES_ROOT\pngfile\shell\open\command]
@="\"<FULL_PATH_TO_MINIMALIMAGEVIEWER_EXE>\" \"%1\""

[HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.png\UserChoice]
"ProgId"="Applications\\MinimalImageViewer.exe"
```

Example file types: .jpeg .png .bmp .gif .tiff .tif .webp .heic .heif .avif




