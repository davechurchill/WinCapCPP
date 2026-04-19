# WinCapCPP

Windows desktop/window capture demo in C++20 with OpenCV, featuring live backend switching between:

- `WGC` (Windows Graphics Capture)
- `DXGI` (Desktop Duplication)
- `GDI` (`BitBlt` / `PrintWindow`)

This project is useful for testing capture reliability and performance across different window types and fullscreen modes.

## Features

- Real-time capture preview with `cv::imshow`
- Keyboard-driven menu for selecting windows (`A-Z`)
- Hot-switch capture backend at runtime (`1/2/3`)
- GDI auto mode detection (`7`) and manual method toggle (`8`)
- WGC border and cursor toggles (`5` / `6`)
- Mouse-wheel zoom in capture view
- On-screen capture timing overlay
- Closing the OpenCV window with `X` exits the app cleanly

## Requirements

- Windows 10/11
- Visual Studio 2022 (MSVC v143) with Desktop C++ workload
- CMake `3.20+` (if building with CMake)
- OpenCV 4.x

Notes:

- The included Visual Studio project is configured for `x64` and links against `x64-windows-static` OpenCV libs.
- Output executables are written to `bin/`.

## Build (Visual Studio)

1. Open [visualstudio/CaptureOpenCV.sln](visualstudio/CaptureOpenCV.sln).
2. Select `x64` + `Debug` or `Release`.
3. Build the solution.

Output:

- `bin/CaptureOpenCV_d.exe` (Debug)
- `bin/CaptureOpenCV.exe` (Release)

## Build (CMake)

From repo root:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

If OpenCV is installed via vcpkg, add your toolchain file:

```powershell
cmake -S . -B build -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

Output:

- `bin/CaptureOpenCV.exe`

## Run

```powershell
.\bin\CaptureOpenCV.exe
```

## License

MIT. See [LICENSE](LICENSE).
