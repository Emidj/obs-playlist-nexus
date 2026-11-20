# Building OBS Playlist NEXUS

## Requirements

- Windows x64, Visual Studio 2022 Build Tools and Windows SDK.
- CMake 3.24 or newer; C++20 support.
- OBS public headers, generated `obsconfig.h` and matching `obs.lib`.
- Qt 6.8 or newer, with Widgets, compatible with the target OBS runtime.
- FFmpeg 7 headers and import libraries: avcodec 61, avformat 61, avutil 59 and swscale 8.

## Build the production plugin

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DNEXUS_BUILD_PLUGIN=ON `
  -DNEXUS_BUILD_FRONTEND_TEST=OFF `
  -DOBS_INCLUDE_DIR="C:/SDK/obs/libobs" `
  -DOBS_CONFIG_INCLUDE_DIR="C:/SDK/obs/generated" `
  -DOBS_LIBRARY="C:/SDK/obs/lib/obs.lib" `
  -DQt6_DIR="C:/SDK/qt/lib/cmake/Qt6" `
  -DCMAKE_PREFIX_PATH="C:/SDK/ffmpeg"
cmake --build build --config RelWithDebInfo --target obs-playlist-nexus
```

Adjust the SDK locations for your environment. The production module is `build/RelWithDebInfo/obs-playlist-nexus.dll`. Optional developer targets are separate from the production plugin. Do not distribute the optional frontend helper module.

## Runtime installation

Close OBS before copying or replacing plugin files. The current installer source expects the traditional Windows `obs-plugins/64bit` layout. Deploy the module with the exact runtime dependencies required by the linked FFmpeg build. OBS 33 can ship a newer FFmpeg major; it does not automatically satisfy imports for FFmpeg 7.

Do not replace shared DLLs from an existing OBS installation with different versions. Use an isolated OBS installation when establishing compatibility for your build. Qt and the Microsoft runtime must be compatible with the host.

The installer source and packaging scripts are included for developers. A distributable binary package must include all applicable dependency notices, licenses and corresponding source arrangements. No third-party runtime binaries are distributed in this repository.
