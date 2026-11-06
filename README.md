# Surface Explorer 1.4
**A high-performance multi-backend 4D surface visualizer built with Qt6 and Qt RHI.**

Surface Explorer is built on the Qt RHI architecture and adds two major rendering and visualization capabilities: a **Ray Marching engine** for implicit surfaces and signed-distance fields, and a **Geodesic Flow** solver for intrinsic geometry on curved manifolds. By relying on the **Qt Rendering Hardware Interface (RHI)**, the application remains hardware-agnostic, automatically leveraging the most efficient graphics API available on your system: **Vulkan**, **Metal**, **Direct3D**, or **OpenGL**. This version is designed for high-precision visualization of complex mathematical surfaces in 4D space ($x, y, z, p$), providing a seamless bridge between abstract geometry and real-time GPU performance.

## 🚀 Key Features
* **Multi-mesh surfaces**: build a surface from several independent grids, one per branch, each with its own colour, transparency, light, wireframe, line density and procedural texture — what makes the Clifford labyrinths and Hopf families possible. Each strip can carry its own image, or one texture can run across the whole figure; *Meshes* draws several copies of a surface or curve, each with its own appearance.
* **Solid Curves**: a tube of adjustable thickness around a curve X(u), Y(u), Z(u) — or P(u) in 4D — with several tubes from a single script (knots, links, Hopf fibres, Villarceau circles).
* **Borders**: a tube along the edges of a parametric surface, with its own thickness, colour and texture.
* **Ray Marching Engine**: Real-time rendering of implicit surfaces and signed-distance fields directly on the GPU, with support for procedural displacement, transparency, and true multi-layer field surfaces.
* **Cross Section**: 3D slices of 4D implicit surfaces — write an equation in `x, y, z, p`; the 4D rotations and the 4D camera path move the slice through the object.
* **Geodesic Flow**: Intrinsic geometry on curved manifolds — integrate geodesics from initial conditions and explore metric-driven surfaces (S³, H²×R, SL(2,R), and more) via the Equations panel.
* **Qt RHI Engine**: Native support for **Vulkan** (Linux/Windows), **Metal** (macOS), and **Direct3D** (Windows), ensuring smoother performance and future-proof compatibility.
* **4D Spatial Exploration**: Native support for four-dimensional coordinate systems with dedicated controls for hyperspatial rotation (Omega, Phi, Psi) and projection.
* **4D Lighting Models**: Advanced lighting modes specifically designed for hyperspace, including **Directional**, **Observer**, and **Slice** lighting.
* **Dynamic GLSL Scripting**: Integrated engine to write custom shaders for procedural textures or surface logic directly within the built-in editor, with discrete constants and lower bounds (`A := int(1,10)`, `A := min(1.0)`).
* **On-screen hints**: presets, records and textures can show a short overlay message on load, naming which sliders are live.
* **Library**: ready-made Shapes, Textures, Sounds and Records, in folders you can rename and rearrange; *Restore Factory Presets* brings back the originals.
* **Scene controls**: RESET returns to how the scene opened (the view, the motions, or both); the master START/STOP stops and restarts every animated part of the scene.
* **Optimized for Fedora & Wayland**: Full integration with modern Linux desktops, including specific optimizations for **AMD GPU** drivers and **Wayland** compositors.
* **Audio-Visual Synthesis**: Real-time synchronization between geometric parameters and a mathematical sound synthesizer or external audio tracks (FFmpeg powered), with a native macOS video encoder (no ffmpeg dependency).

## 💻 System Requirements
* **OS**: Linux (Fedora/Ubuntu), Windows 10/11, macOS 14.4 or later (arm64), or iPhone and iPad via the App Store.
* **Graphics**: GPU with support for **Vulkan 1.0+**, **Metal**, **Direct3D 11+**, or **OpenGL 3.3+**.
* **Framework**: Built with **Qt 6.12** on macOS and iOS, and **Qt 6.10.2** on Linux and Windows, for maximum stability and High-DPI support.

## ⚠️ Known Issues

**Windows / Direct3D.** Presets that drive the **Geodesic Flow** solver from a metric written in the Equations panel may not render correctly on the Direct3D backend: the application can stop responding and be closed by the system. They work as expected on the other platforms.

## 📦 Releases
You don't need to compile the project to try it! Check out the **[Latest Releases](https://github.com/dioscorid-design/SurfaceExplorer/releases)** for portable, standalone binaries:

* **macOS**: Download `SurfaceExplorer-*-macos.dmg`, open it, and drag the app to your Applications folder. The app is signed with an Apple Developer ID and notarized, so it opens normally on first launch — no Gatekeeper workaround needed.
* **Windows**: Download `SurfaceExplorer-*-windows-x64.zip`, extract, and run `SurfaceExplorer.exe`.
* **Linux**: Download `SurfaceExplorer-*-linux-x86_64.AppImage` — a self-contained binary that bundles Qt, so it runs on any x86_64 distribution with no installation. Every release page carries the exact commands to make it executable and, with `install-linux.sh`, to add it to your applications menu.
* **iPhone and iPad**: available on the [App Store](https://apps.apple.com/app/id6787015297).

Version 1.4 is available for every platform. macOS already has [v1.5](https://github.com/dioscorid-design/SurfaceExplorer/releases/tag/v1.5), and the 2.0 and 2.1 betas for macOS add scenes with several surfaces together.

## 🛠️ Build from Source
To compile Surface Explorer manually, ensure you have **Qt 6.10.2** and **CMake** installed:

```bash
# 1. Clone the repository
git clone https://github.com/dioscorid-design/SurfaceExplorer.git
cd SurfaceExplorer

# 2. Configure and Build
cmake -B build -S .
cmake --build build -j$(nproc)
```
