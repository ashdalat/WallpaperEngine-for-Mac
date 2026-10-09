# Building open-wallpaper-engine

OWE is built with [lito](https://github.com/litocpp/lito).  

## Requirements

| Dependency | Version | Notes |
|------------|---------|-------|
| Clang | 22+ | C++20 modules toolchain |
| lito | 0.3.0+ | C++ builder |
| CMake | 4.0+ | used for CMake dependencies |
| Ninja | recent | CMake dependency builds |
| pkg-config | - | system dependency discovery |
| Vulkan SDK | 1.1+ | loader and headers |
| GLFW3 | - | standalone viewers |
| liblz4 | - | `.pkg` decompression |
| EGL / GLESv2 / X11 / wayland-egl | - | web viewer presenters |

The complete release environment is declared in `environment.yml`.

## Build

The default workspace members are the scene and web viewers:

```bash
lito build --profile release
```

Build an individual package when only one surface is needed:

```bash
lito build -p owe-sceneviewer --profile release
lito build -p owe-waywallen-scene-renderer --profile release
lito build -p owe-waywallen-web-renderer --profile release
```

Release viewer binaries are written under:

```text
build/release/bin/owe-sceneviewer/SceneViewer
build/release/bin/owe-webviewer/WebViewer
```

## Plugin bundle

The release helper builds and installs `waywallen-bridge`, invokes lito for the OWE
plugin, and packages:

```bash
./scripts/build_waywallen_plugin.sh
```

The bundle contains:

```text
plugin.toml
files.txt
main.lua
wallpaper_engine/
bin/waywallen-wescene-renderer
bin/weweb/waywallen-weweb-renderer
bin/weweb/libcef.so
bin/weweb/locales/
```