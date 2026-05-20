<h1 align="center"> Minimal Image Viewer </h1>

<p align="center">
  <img src="https://github.com/deminimis/minimalimageviewer/blob/main/src/app.ico?raw=true" alt="Minimal Image Viewer Logo" width="120">
</p>

<h3 align="center">The most 🪶 lightweight image viewer available for Windows</h3>

<p align="center">
  <a href="https://apps.microsoft.com/detail/9PFRKSW3VHQ5?hl=en-us&gl=US&ocid=pdpshare">
    <img src="https://img.shields.io/badge/Get%20it%20on-Microsoft%20Store-0078D4?style=for-the-badge&logo=microsoft&logoColor=white" alt="Microsoft Store">
  </a>
  <a href="https://github.com/deminimis/minimalimageviewer/releases">
    <img src="https://img.shields.io/github/v/release/deminimis/minimalimageviewer?style=for-the-badge&label=Latest%20Release" alt="Latest Release">
  </a>
  <a href="https://github.com/deminimis/minimalimageviewer">
    <img src="https://img.shields.io/github/license/deminimis/minimalimageviewer?style=for-the-badge" alt="License">
  </a>
</p>

<p align="center">
  <b>Fast. Native. Private. Minimal.</b>
  <br>
  A lightweight and secure image viewer for Windows, built with native Win32 and modern C++.
  <br>
  Designed to open everything from small PNGs to large RAW and HDR images without unnecessary bloat.
</p>


&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;



# Download

### Recommended: Install the latest signed version from the Microsoft Store:

<a href="https://apps.microsoft.com/detail/9PFRKSW3VHQ5?hl=en-us&gl=US&ocid=pdpshare" target="_blank"><img src="https://get.microsoft.com/images/en-us%20dark.svg" alt="Get it from Microsoft" width="200" style="margin-top: 10px;"></a>

### Alternative: GitHub Releases

💾 [**Download Portable (.exe)**](https://github.com/deminimis/minimalimageviewer/releases)



For the ultra-minimal legacy build, try the [stripped 21 KB executable](https://github.com/deminimis/minimalimageviewer/releases/tag/v1.6).

---

## Highlights

- ✅ Native Win32 application  
- ✅ No telemetry  
- ✅ No external runtime dependencies  
- ✅ No installer required for portable builds  
- ✅ Lightweight executable size  
- ✅ Fast startup and low memory usage  
- ✅ Supports common image formats through Windows Imaging Component  
- ✅ Designed for privacy-focused local image viewing  

---


## 📊 Comparison

| Feature | **Minimal Image Viewer** | **Windows Photos** | **IrfanView / XnView** |
|--------|-------------------------|-------------------|------------------------|
| Executable Size | **~500 KB** | ~50 MB+ | ~3–5 MB+ |
| Telemetry / Network | **None** | Yes | Optional |
| Portable | **Yes (.ini)** | No | Partial |
| Deep Zoom | **Yes (GPU streaming)** | Limited | No |
| RAW Preview Optimization | **Yes** | Limited | Plugin-based |
| SVG Support | **Yes (native GPU)** | No | Limited |
| HDR / QOI Support | **Yes** | No | Partial |
| Startup Speed | **Instant** | Slow | Moderate |
| Open Source | **Yes** | No | No |


---

## ✨ Features

### 🖼️ Image Support 

Supports a wide range of formats using WIC + custom decoders:

- **Standard**: JPEG, PNG, BMP, GIF, TIFF, ICO, WebP  
- **Modern**: HEIF, HEIC, AVIF  
- **RAW (Camera)**: CR2, CR3, NEF, DNG, ARW, ORF, RW2 *(via codecs)*  
- **Advanced Formats**:
  - **SVG (Vector rendering, GPU-accelerated)**
  - **HDR (.hdr Radiance with tone mapping from [nothings](https://github.com/nothings/stb))**
  - **QOI ([Quite OK Image](https://github.com/phoboslab/qoi)**
  - **TGA, PSD, PPM, PGM, PBM, PNM**

---

### 🎞️ Animated Images (GIF & Multi-frame)

- Full animated image playback
- Correct frame timing + disposal behavior
- Interactive controls:
  - `Space` → Pause + step frames
  - `Shift + Space` → Resume playback
  - `Shift + ← / →` → Previous / Next frame
  - `Shift + ↑ / ↓` → Jump to first frame

---

### 🔍 Viewing & Navigation

- Smooth zoom (`Ctrl + Mouse Wheel`, `Ctrl +/-`)
- Cursor-centered zooming
- Click + drag to pan
- Fit to Window (`Ctrl+0` / double-click)
- Actual Size (`Ctrl+*`)
- Custom zoom dialog (`Ctrl+Shift+Z`)
- Instant next / previous image navigation

---

### 🚀 Performance (Built for Speed)

- **Asynchronous image loading** → no UI blocking  
- **Directory preloading** → instant browsing  
- **Deep Zoom (GPU-assisted)**:
  - Downscaled preview for huge images
  - Full resolution streamed only when needed
- **RAW preview extraction**:
  - Loads embedded previews (fast)
  - Falls back to full decode only when necessary

---

### 📂 Smart File Handling

- Automatic directory scanning
- Navigate all images in current folder
- Sort by:
  - Name
  - Date Modified
  - File Size
- Ascending / descending sorting

---

### 🎨 Rendering & Visuals

- GPU rendering via **Direct2D + DXGI**
- Smooth scaling (toggle interpolation)
- Optional fade transitions
- Background modes:
  - Grey (default)
  - Black
  - White
  - Checkerboard transparency
- Automatic dark/light title bar

---

### 🛠️ Editing Tools (Non-Destructive)

- **Crop Tool**
  - `C` → Enable crop
  - Drag selection
  - `Enter` → Apply
  - `Esc` → Cancel

- **Resize Tool**
  - Custom dimensions
  - Aspect ratio lock
  - High-quality scaling (Fant)

- **Transformations**
  - Rotate (90° increments)
  - Flip horizontal
  - EXIF auto-rotation on load

- **Undo System**
  - `Ctrl+Z`
  - Memory-safe multi-step history

---

### 📊 Metadata & OSD

- Built-in EXIF + file metadata parsing
- Toggle OSD (`I`) showing:
  - Format, resolution, DPI
  - File size, attributes
  - Camera data (ISO, aperture, exposure)
  - Author / software

---

### 📁 File Operations

- Open (`Ctrl+O`) or drag & drop
- Save (`Ctrl+S`) with atomic overwrite
- Save As (`Ctrl+Shift+S`)
- Resize + save new file
- Delete (Recycle Bin safe delete)
- Clipboard support:
  - Copy image path
  - Paste images from clipboard
- Open file location instantly

---

### 🔄 Automation

- **Auto-refresh mode**
  - Detects file changes on disk
  - Reloads safely when modified externally

---

### ⚙️ Customization

- Fully customizable keybindings
- Portable `.ini` config
- Preferences include:
  - Always on Top
  - Start Fullscreen
  - Single Instance enforcement
  - Background color
  - Smooth scaling toggle
  - Fade animation toggle
  - OSD toggle
  - Default zoom mode

---

## ⚙️ Technical Highlights

### 🧱 Core Architecture

- **Language**: C++20  
- **Platform**: Native Win32 (no frameworks)  
- **Rendering**:
  - Direct2D (GPU)
  - DirectWrite (text)
  - DXGI swap chain

---

### 🖼️ Image Pipeline (WIC)

- `IWICBitmapDecoder` → load images  
- `IWICFormatConverter` → normalize formats  

**Editing pipeline (non-destructive):**

- Flip/Rotate → `IWICBitmapFlipRotator`  
- Crop → `IWICBitmapClipper`  
- Resize → `IWICBitmapScaler`  
- Convert → `IWICFormatConverter`  

---

### 🎞️ Animation Engine

- Frame-based decoding using WIC metadata
- Custom compositor:
  - Handles disposal modes
  - Maintains frame buffers
- Timer-driven playback system

---

### 🔍 Deep Zoom System

- Large images downscaled (~4K working size)
- High resolution streamed dynamically using:
  - `ID2D1ImageSource`
- Prevents VRAM spikes and improves responsiveness

---

### 🧵 Multithreading

- Background threads for:
  - Image decoding
  - Directory scanning
  - Preloading images
- UI thread synchronized via `WM_APP` messages

---

### 🧠 Smart Loading

- Full file read (avoids file locks)
- RAW/TIFF preview extraction
- Native codec downscaling when possible
- CPU fallback scaler when needed

---

### 📦 Extended Format Support

- **SVG**
  - Rendered via `DrawSvgDocument` (GPU)

- **HDR**
  - Decoded via `stb_image`
  - Tone-mapped to display space

- **QOI**
  - Decoded via embedded `qoi.h`

---

### 💾 Portable Configuration

- Settings stored in: minimal_image_viewer_settings.ini

- No registry usage
- Fully portable between systems

---

### 🔒 Safety & Reliability

- Atomic saves (temp file replacement)
- Recycle Bin deletion (recoverable)
- Memory-limited undo stack
- Large image safeguards


---

## 🎯 Philosophy

> **Do one thing extremely well.**

Minimal Image Viewer is built to be:

- ⚡ Fast  
- 🧠 Smart  
- 🔒 Private  
- 📦 Tiny  
- 🧩 Complete  

No telemetry. No dependencies. No bloat.

Just a **powerful, minimal image viewer for Windows**.

Disclaimer: This readme was redone by CoPilot to add flashy icons and dumb advertisement/SEO language. 


