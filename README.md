# Minimal Image Viewer

<div style="overflow: auto;">
  <img src="https://github.com/deminimis/minimalimageviewer/blob/main/src/app.ico?raw=true" alt="Project Logo" width="20%" align="left">
</div> 

### The most 🪶 LIGHTWEIGHT image viewer available for Windows
<br>

Minimal Image Viewer is an open-source image viewer and editor (**now with OCR**), prioritizing performance and minimalism. With a compiled size as low as ~20.5kb. It is able to open png photos of over 100mb in seconds (depending on your specs). 
<br><br><br><br>

### 💾 Download the latest version [here](https://github.com/deminimis/minimalimageviewer/releases).
<br>

_For a stripped-down 21kb version, you can try the [stable stripped .exe](https://github.com/deminimis/minimalimageviewer/releases/tag/v1.6)_

## Key Features

### Viewing & Navigation

- **Comprehensive Format Support**: Leverages Windows Imaging Component (WIC) to support JPEG, PNG, BMP, GIF, TIFF, ICO, WebP, HEIF, AVIF, and many RAW formats (.cr2, .nef, .dng, etc.) with the proper codecs installed.
    
- **Animated Image Support**: Plays animated formats (like GIF) by decoding all frames and their metadata-defined delays.
    
- **Asynchronous Pre-loading**: Intelligently pre-loads the next and previous images in a directory on background threads for instantaneous browsing.
    
- **Flexible Sorting**: Sorts all images in the current directory by **Name**, **Date Modified**, or **File Size** in ascending or descending order.
    
- **Smooth Viewing**:
    
    - Smooth zoom (Ctrl + Mouse Wheel).
        
    - Pan by clicking and dragging the image.
        
    - Fit to Window (Ctrl+0 / Double-Click).
        
    - Actual Size (100%) (Ctrl+*).
        
- **Configurable Interface**:
    
    - **Backgrounds**: Switch between Grey (default), Black, White, or a transparent checkerboard.
        
    - **Always on Top**: Toggle to keep the window on top.
        
    - **Default Zoom**: Set the default zoom to "Fit to Window" or "Actual Size (100%)" in Preferences.
 

### OCR

- **Whole Image OCR**: Press 'q' to OCR the entire image.
- **Area OCR**: Press 'w' to OCR a selected area. 
        

### Editing & Tools

- **Non-Destructive Editing**: All edits (crop, rotate, flip, grayscale) are previewed in real-time without altering the original file until you explicitly save.
    
- **Visual Crop**: Press 'C' to enable crop mode. Click and drag to select a region, then press Enter to apply the crop. The save operation will only save the cropped area.
    
- **High-Quality Resize**: Resize images to new dimensions with aspect-ratio locking, using a high-quality Fant interpolation scaler.
    
- **Rotate & Flip**: Rotate images in 90° increments or flip them horizontally. All changes are saved correctly.
    
- **Grayscale Filter**: Instantly apply or remove a non-destructive grayscale filter.
    
- **Eyedropper Tool**: Hold the `Alt` key to activate an eyedropper. It displays the **RGB** and **Hex** value of the pixel under your cursor. Click to copy the Hex value to your clipboard.

- **Image Effects**: Click to open a box with sliders to adjust the contrast, brightness, and saturation. 
    

### Data & File Management

- **Detailed Properties Window**: Opens a separate window showing extensive file properties and detailed EXIF metadata, including camera, lens, and shot settings (F-stop, ISO, Exposure, etc.).
    
- **OSD Overlay**: Press 'I' to toggle an on-screen display (OSD) with key file and image data for quick reference.
    
- **File Operations**:
    
    - **Save (Ctrl+S)**: Overwrites the original file with any applied edits (rotate, flip, crop, grayscale).
        
    - **Save As (Ctrl+Shift+S)**: Save the edited image to a new file in PNG, JPEG, or BMP format.
        
    - **Delete (Delete)**: Securely moves the current file to the Recycle Bin.
        
    - **Clipboard**: Copy the image file or bitmap to the clipboard (Ctrl+C) and paste an image or file path from the clipboard (Ctrl+V).
        
    - **Drag & Drop**: Open an image by dragging it onto the window.

    - **Auto Refresh**: Polls the image to see if it has changed elsewhere, and automatically updates if you have auto-refresh turned on. 
        

---

## Why Minimal Image Viewer?

This viewer is built on the philosophy of "do one thing and do it well," offering distinct advantages in resource-constrained or OpSec-sensitive environments.

- **🔒 Secure & Private**:
    
    - **Zero Telemetry**: The application performs **no network activity** and contains no tracking or telemetry, ensuring complete privacy.
        
    - **Minimal Attack Surface**: Relies only on hardened, native Windows APIs (Direct2D, WIC) and avoids large third-party libraries.
        
    - **Safe File Handling**: Uses atomic save operations via temporary files to prevent data corruption and deletes to the Recycle Bin to prevent accidental data loss.
        
- **🚀 Efficient & Portable**:
    
    - **Lightweight**: Compiles to a tiny executable (~100 KB) and uses minimal RAM (<10 MB for most images).
        
    - **Portable Settings**: Saves all preferences (background color, window size, etc.) to a `minimal_image_viewer_settings.ini` file in the same directory, making it fully portable and leaving no trace in the Windows Registry.
        
    - **Hardware Accelerated**: Uses Direct2D for all rendering, ensuring smooth, GPU-accelerated scaling, panning, and animations.
        

### Comparison with Alternatives

| **Feature**              | **Minimal Image Viewer** | **Windows Photos** | **IrfanView / XnView** |
| ------------------------ | ------------------------ | ------------------ | ---------------------- |
| **Executable Size**      | **~100 KB**              | ~50 MB+ (UWP)      | ~3-5 MB+               |
| **Telemetry / Network**  | **None**                 | Yes                | Optional               |
| **Settings**             | **Portable `.ini`**      | Registry / UWP     | Portable or Registry   |
| **Animated GIF Support** | **Yes**                  | Yes                | Yes (with plugins)     |
| **Open Source**          | **Yes**                  | No                 | No                     |

---

## Installation and Usage

Download the `.exe` from the [Releases](https://github.com/deminimis/minimalimageviewer/releases) page and run it. No installation is required.

To set it as your default viewer, see the [Default Viewer Instructions](https://github.com/deminimis/minimalimageviewer/blob/main/Instructions/Default%20Viewer.md).

### Hotkeys

#### File Operations

- **Open Image**: `Ctrl+O` or **Drag & Drop**
    
- **Save (Overwrite)**: `Ctrl+S`
    
- **Save As**: `Ctrl+Shift+S`
    
- **Delete Image**: `Delete` (Moves to Recycle Bin)
    
- **Copy Image**: `Ctrl+C`
    
- **Paste Image**: `Ctrl+V`
    
- **Open File Location**: Right-Click → "Open File Location"
    
- **Properties**: Right-Click → "Properties..."
    

#### Image Navigation

- **Next/Previous Image**: `Right Arrow` / `Left Arrow`
    
- **Pan Image**: **Click and Drag** (when zoomed in)
    
- **Sort By**: Right-Click → "Sort By" → (Name/Date/Size)
    

#### Viewing

- **Zoom In/Out**: `Ctrl+` / `Ctrl-` or **Mouse Wheel**
    
- **Fit to Window**: `Ctrl+0` or **Double-Click**
    
- **Actual Size (100%)**: `Ctrl+*`
    
- **Toggle Fullscreen**: `F11`
    
- **Exit**: `Esc`
    

#### Editing & Tools

- **Rotate Clockwise**: `Up Arrow`
    
- **Rotate Counter-Clockwise**: `Down Arrow`
    
- **Flip Horizontal**: `F`
    
- **Toggle Crop Mode**: `C`
    
- **Apply Crop**: `Enter` (while in crop mode)
    
- **Cancel Crop**: `Esc` (while in crop mode)
    
- **Toggle Grayscale**: Right-Click → "Edit" → "Grayscale"
    
- **Resize Image**: Right-Click → "Edit" → "Resize Image..."
    
- **Toggle OSD**: `I`
    
- **Eyedropper Tool**: **Hold Alt** (Click to copy hex code)
    
- **Preferences**: Right-Click → "Preferences..."
    

---

## Technical Highlights

- **Core Technology**: Built with modern C++ and native Win32 APIs. All rendering is hardware-accelerated via **Direct2D** (`ID2D1HwndRenderTarget`) and text is rendered with **DirectWrite** for superior clarity.
    
- **WIC Pipeline**: All image I/O and transformations are handled by the **Windows Imaging Component (WIC)**.
    
    - **Loading**: `IWICBitmapDecoder` and `IWICFormatConverter` are used to load and convert any WIC-supported format to a Direct2D-compatible pixel format (`GUID_WICPixelFormat32bppPBGRA`).
        
    - **Editing**: Edits are applied by chaining WIC components before saving:
        
        - `IWICBitmapFlipRotator` (for rotation/flipping)
            
        - `IWICBitmapClipper` (for cropping)
            
        - `IWICBitmapScaler` (for resizing)
            
        - `IWICFormatConverter` (for grayscale)
            
    - **Animation**: `IWICBitmapDecoder::GetFrameCount()` is used to detect multi-frame images. Frame delays are read from the metadata (`/grctlext/Delay`) and a `WM_TIMER` event is used to cycle frames.
        
- **Concurrency**: Image loading is fully asynchronous on a separate `std::thread` to keep the UI responsive. A custom `WM_APP_IMAGE_LOADED` message is posted to the main window thread to safely transfer the decoded image. Directory pre-loading (`StartPreloading`) uses two additional threads for the next/previous images.
    
- **State Management**: Window state and user preferences are persisted in a portable `minimal_image_viewer_settings.ini` file using `GetPrivateProfileIntW` and `WritePrivateProfileStringW`.
    

## Build Process

1. Open `MinimalImageViewer-VS.vcxproj` in Visual Studio 2022.
    
2. Set the build configuration to "Release" and "x64".
    
3. Build the solution.
    

The project links against the following standard Windows libraries:

user32.lib, gdi32.lib, comdlg32.lib, shlwapi.lib, windowscodecs.lib, ole32.lib, shell32.lib, propsys.lib, oleaut32.lib, d2d1.lib, dwrite.lib, advapi32.lib.

## Contributing

Contributions are welcome! This project adheres to a minimalist and security-focused philosophy.

1. Fork the repository.
    
2. Create a feature branch (`git checkout -b feature/YourFeature`).
    
3. Commit your changes (`git commit -m "Add YourFeature"`).
    
4. Push to the branch (`git push origin feature/YourFeature`).
    
5. Open a Pull Request.


