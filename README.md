# Minimal Image Viewer

<div style="overflow: auto;">
  <img src="https://github.com/deminimis/minimalimageviewer/blob/main/assets/app.png?raw=true" alt="Project Logo" width="20%" align="left">
</div> 

### The most 🪶 LIGHTWEIGHT image viewer available for Windows
<br>

Minimal Image Viewer is an open-source, C++-based image viewing application engineered for Windows, prioritizing performance and minimalism. With a compiled size of only ~82KB. Leveraging native Windows APIs and the Windows Imaging Component (WIC), it ensures accurate functionality. It is able to open png photos of over 100mb in seconds (depending on your specs). 
<br><br><br><br>

### 💾 Download the latest version [here](https://github.com/deminimis/minimalimageviewer/releases).
<br>

## Key Features

- **Comprehensive Image Format Support**:
  - Supports a wide array of formats via WIC, including JPEG, PNG, BMP, GIF, TIFF, ICO, WebP, HEIF, AVIF, and RAW formats (e.g., .cr2, .cr3, .nef, .dng, .arw, .orf, .rw2) with appropriate codecs installed.
  - Dynamically validates files as images using WIC’s `IWICBitmapDecoder`, enabling support for any WIC-compatible format without hardcoding extensions.
  - Preserves original file formats during save operations, ensuring fidelity for formats like TIFF and GIF.

- **Intuitive Navigation**:
  - Directory-based image navigation via Left/Right arrow keys or context menu ("Next Image," "Previous Image").
  - Automatically indexes all WIC-supported images in the current directory, sorted lexicographically.

- **Flexible Viewing**:
  - Internally sorts images based on name, size, etc. 
  - Smooth zoom (0.1x–10x) via Ctrl++/-, mouse wheel, or context menu, implemented with `SetWorldTransform` for GPU-friendly scaling.
  - Fits images to window (Ctrl+0 or double-click), adjusting for rotation angles.
  - Rotates images in 90° increments.
  - **Configurable Background**: Change the background to Grey (default), Black, White, or a transparent checkerboard pattern via the right-click menu.
    
- **Image Management**:
  - Saves rotated images (Ctrl+S or context menu) in their original WIC-supported format, using temporary files to ensure atomic operations.
  - Deletes images to the Recycle Bin (Delete key or context menu) for recoverable deletions.
  - Opens images with a comprehensive filter for common image formats, falling back to WIC for validation.
  - Copy the current view to the clipboard (Ctrl+C) or paste an image/file path from the clipboard (Ctrl+V).

- **Minimalist Interface**:
  - Borderless window for a distraction-free experience, with dynamic cursor feedback for resizing.
  - Displays "Right click to see hotkeys" when no image is loaded, using GDI text rendering.

- **Window Control**:
  - Supports window dragging (non-full-screen) and edge-based resizing.
  - Toggles full-screen mode (F11 or context menu) with automatic image fitting, preserving window state.
  - Exits via Esc or context menu, ensuring clean resource deallocation.

- **Single-Instance Enforcement**:
  - Uses `FindWindowW` to prevent multiple instances, forwarding command-line arguments to the existing instance via `WM_COPYDATA` for memory management.
 
- **EXIF Data**:
  - View commmon EXIF data in a properties window or overlayed on image. 
 

## Security and Privacy

Minimal Image Viewer is designed for OpSec-sensitive environments, prioritizing a minimal attack surface and zero telemetry.

- **Offline Operation**:
  - No network activity or telemetry, ensuring complete data privacy.

- **Minimal Attack Surface**:
  - The modular codebase is easy to audit. It relies exclusively on hardened, native Windows APIs and WIC, avoiding third-party library dependencies.
  - Strict memory management mitigates buffer overflows and leaks, validated with static analysis tools.

- **Safe File Handling**:
  - Opens files with shared access (`FILE_SHARE_READ | FILE_SHARE_WRITE`) to prevent lock-based exploits.
  - Saves via temporary files to ensure atomic writes, minimizing data loss risks.
  - Deletes to Recycle Bin with user confirmation, preventing accidental permanent data loss.

- **No Registry Modifications**:
  - Operates without persistent system changes, leaving no forensic footprint beyond the executable.

## Why Modular?

Minimal Image Viewer adheres to the Unix philosophy of “do one thing and do it well,” offering distinct advantages:

- **Efficiency**: Minimal resource usage (<10 MB RAM, ~100 KB disk) enables integration into resource-constrained workflows, outperforming bloated alternatives like Windows Photos (~100 MB RAM).
- **Security**: Focused functionality reduces attack vectors compared to feature-heavy tools with cloud integration or telemetry (e.g., Windows Photos, Adobe Bridge).
- **Flexibility**: Complements specialized tools (e.g., ExifTool, RawTherapee) for workflows in photography, digital forensics, or development.
- **Maintainability**: Small codebase simplifies updates, security patches, and community contributions.

## Comparison with Alternatives

| Feature                     | Minimal Image Viewer | Windows Photos | IrfanView | XnView |
|-----------------------------|----------------------|----------------|-----------|--------|
| **Executable Size**         | ~0.08 MB             | ~50 MB         | ~3 MB     | ~5 MB  |
| **Dependencies**            | None (Windows APIs) | UWP Framework  | Optional Plugins | Optional Plugins |
| **Telemetry**               | None                | Yes            | Optional  | Optional |
| **Offline Operation**       | Yes                 | Partial        | Yes       | Yes    |
| **Image Format Support**    | WIC-dependent (all formats) | Codec-dependent | Plugin-dependent | Plugin-dependent |
| **Open-Source**             | Yes           | No             | No        | No     |
| **Footprint (RAM)**         | <10 MB             | ~100 MB        | ~20 MB    | ~30 MB |

Minimal Image Viewer excels in size, privacy, and format support, leveraging WIC’s extensibility for unparalleled compatibility.

## System Requirements

- **OS**: Windows 10 or later (64-bit recommended for optimal WIC codec support).
- **Dependencies**: None (uses standard Windows libraries: `user32`, `gdi32`, `windowscodecs`, etc.).
- **Image Format Support**: Supports all WIC-compatible formats; advanced formats (e.g., WebP, HEIF, AVIF) require installed codecs (e.g., Microsoft Store extensions).
- **Disk Space**: 400 KB.
- **Memory**: <10 MB runtime for typical images, scaling with image resolution.

## Installation and Usage

1. See the [release page](https://github.com/deminimis/minimalimageviewer/releases) for single .exe, or build yourself (instructions below). 

### File Operations

- **Open Image:** Ctrl+O, Right-click → "Open Image", or **Drag & Drop** a file onto the window.
    
- **Save:** Ctrl+S (overwrites the original file if rotated) or Right-click → "Save".
    
- **Save As:** Ctrl+Shift+S or Right-click → "Save As".
    
- **Delete Image:** `Delete` key or Right-click → "Delete Image" (moves file to Recycle Bin).
    
- **Copy:** Ctrl+C (copies the image file or bitmap to the clipboard).
    
- **Paste:** Ctrl+V (pastes an image from the clipboard).
    
- **Open Location:** Right-click → "Open File Location".
    
- **Properties:** Right-click → "Properties...".

- **EXIF overlay:** "i"
    

### Image Navigation

- **Next/Previous Image:** `Right Arrow` / `Left Arrow` or Right-click menu.
    
- **Pan Image:** When zoomed in, **click and drag** the image to move it.
    

### Image Viewing

- **Zoom In/Out:** Ctrl+`+` / Ctrl+`-`, **Mouse Wheel**, or Right-click menu.
    
- **Fit to Window:** Ctrl+0, **Double-Click** the image, or Right-click → "Fit to Window".
    
- **Actual Size (100%):** Ctrl+`*` or Right-click → "Actual Size (100%)".
    
- **Rotate:** `Up Arrow` (Clockwise) / `Down Arrow` (Counter-Clockwise) or Right-click menu.
  


3. If you are having difficulties setting as your main image viewer, see [setting as default viwer](https://github.com/deminimis/minimalimageviewer/blob/main/Instructions/Default%20Viewer.md).



## Technical Highlights

Minimal Image Viewer is built with a modern C++/Win32 architecture using Direct2D for high-performance, hardware-accelerated rendering.

### Core Implementation

- **Direct2D / DirectWrite**:
  - All rendering is handled by Direct2D (`ID2D1HwndRenderTarget`) for smooth, hardware-accelerated drawing. This avoids legacy GDI limitations and flicker.
  - Text (like "Loading..." or the help prompt) is rendered using DirectWrite (`IDWriteFactory`, `IDWriteTextFormat`) for superior text clarity and positioning.
  - Creates a checkerboard `ID2D1BitmapBrush` on the fly to render the "Transparent" background option.

- **Windows Imaging Component (WIC)**:
  - Decodes all images using `IWICImagingFactory`. This allows the app to load any format Windows supports (JPG, PNG, WebP, HEIF, AVIF, RAW, etc.) without needing custom libraries.
  - Images are converted to a standard pixel format (`GUID_WICPixelFormat32bppPBGRA`) via `IWICFormatConverter` for compatibility with Direct2D.
  - The WIC converter is used to create a Direct2D bitmap (`CreateBitmapFromWicBitmap`) for rendering.
  - Saving images (including rotations) uses `IWICBitmapEncoder` to preserve the original file's container format (e.g., saving a rotated PNG as a PNG).

- **Asynchronous Loading & Preloading**:
  - To keep the UI fast and responsive, image loading is done on a separate thread (`std::thread` in `LoadImageFromFile`).
  - A `WM_APP_IMAGE_LOADED` custom message is posted back to the main window thread when loading is complete to safely update the UI.
  - The application pre-loads the *next* and *previous* images in the directory on background threads (`StartPreloading`) to make navigation instantaneous.

- **Graphics and Transformations**:
  - Zooming, panning (dragging), and rotation are not done by manipulating the image pixels.
  - Instead, Direct2D's transformation matrix (`D2D1::Matrix3x2F`) is modified. The `Render` function applies a combination of `Scale`, `Rotation`, and `Translation` matrices. This is extremely fast and high-quality, as all the work is done by the GPU.

- **Settings and State**:
  - Window position/size and user preferences (Start Full Screen, Background Color) are saved to a simple `settings.ini` file in the application's directory.
  - Uses `GetPrivateProfileIntW` and `WritePrivateProfileStringW` for easy, registry-free persistence.

- **File System and Shell Integration**:
  - Supports drag-and-drop (`WM_DROPFILES`), clipboard paste (`CF_HDROP`, `CF_DIB`), and file association registration (`RegisterApp`).
  - "Delete Image" uses `SHFileOperationW` to send files to the Recycle Bin (`FOF_ALLOWUNDO`).
  - "Open File Location" uses `SHOpenFolderAndSelectItems` to open Explorer with the current file highlighted.

### Performance Optimizations

- **Minimal Footprint**:
  - Compiles to a ~82 KB executable with `/O2` optimization, requiring <10 MB runtime memory for most images.
  - No external dependencies beyond standard Windows libraries (`user32.lib`, `gdi32.lib`, `windowscodecs.lib`, etc.).

- **Resource Management**:
  - Strictly manages WIC (`Release`) and GDI (`DeleteObject`) resources to prevent memory leaks, verified with tools like Application Verifier.
  - Closes file handles (`CloseHandle`) immediately after use to ensure system resource availability.
  - Uses `FindWindowW` to prevent multiple instances, forwarding command-line arguments to the existing instance via `WM_COPYDATA` for memory management.

### Build Process

- **Build Commands**:
  - **Visual Studio**:
    ```cmd
    rc.exe /fo resource.res resource.rc
    cl.exe /O2 /EHsc /Fe:MinimalImageViewer.exe main.cpp ui_handlers.cpp image_drawing.cpp image_io.cpp settings_handler.cpp registry_handler.cpp resource.res /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comdlg32.lib shlwapi.lib windowscodecs.lib ole32.lib shell32.lib propsys.lib oleaut32.lib d2d1.lib dwrite.lib advapi32.lib
    ```



## Contributing

Contributions are welcome. 



To contribute:

1. Fork the repository.
2. Create a feature branch (`git checkout -b feature/YourFeature`).
3. Commit changes (`git commit -m "Add YourFeature"`).
4. Push to the branch (`git push origin feature/YourFeature`).
5. Open a pull request, ensuring alignment with the project’s minimalist and security-focused philosophy.

Include unit tests (e.g., for new WIC format handling) and update documentation for new features.

---

**Minimal Image Viewer: A fast, secure, and extensible image viewer for privacy-conscious professionals.**
