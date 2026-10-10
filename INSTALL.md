# Wallpaper Engine for macOS — Install Guide

Two ways to get this:

1. **Use the prebuilt app** — copy `WallpaperEngineMac.app` (in the `Share/` folder) to your Mac.
2. **Build from source** — you got the source code and want to compile it yourself.

---

## Part 1 — Install the prebuilt app

### Requirements

- Apple Silicon Mac (arm64)
- macOS 15.0 or newer
- Steam with **Wallpaper Engine** installed (Steam App ID 431960) — needed for its wallpapers; the app reads them from your Steam library folders

### Steps

1. Copy `WallpaperEngineMac.app` into `/Applications`.
2. **First launch:** control-click (right-click) the app → **Open** → confirm. The app is signed ad-hoc (not notarized with Apple), so Gatekeeper stops a plain double-click the first time. After that, it opens normally.
3. **Accessibility permission:** the first time a mouse-follow wallpaper is used, macOS asks for Accessibility access (or the app shows a prompt). Grant it in **System Settings → Privacy & Security → Accessibility**.
   - **Important:** the app is re-signed on every rebuild, which changes its code signature and **resets this permission** — you have to grant it again after every update.

### Using it

- The app lives in the **menu bar** (top-right). Click it → **Open Gallery**.
- **Click** a wallpaper to set it on all screens.
- **Right-click** a wallpaper: *Preview*, **Settings & Info…**, *Open in Finder*, *Remove Wallpaper*.
- The **slider button** in the gallery toolbar opens Settings & Info for the wallpaper currently on screen.
- **Settings** (menu bar → Settings…) has a **Launch at Login** toggle (on by default) — it registers the app with System Settings → General → Login Items via SMAppService, so the wallpaper restarts with the system.
- **Settings & Info** shows the wallpaper's metadata (title, type, ID, version, tags, description, folder) plus its own settings from its `project.json` — checkboxes, sliders, color pickers, combos, text fields. Changes are saved per wallpaper and applied immediately. It also has a **Display** section with a flip control (None/Horizontal/Vertical/Both) that applies live to any wallpaper.
- If the gallery is empty, click **Choose Steam Library…** and select the folder that contains `steamapps/` (the one with `steamapps/workshop/content/431960`).

### What makes it self-contained

`WallpaperEngineMac.app/Contents/Frameworks/` carries the app's own copies of the libraries it needs (MoltenVK, FFmpeg, lz4, fontconfig, freetype, …). The app runs on a Mac **without Homebrew** — you only need Homebrew on the machine that built it.

### Uninstall

- Delete `/Applications/WallpaperEngineMac.app`.
- Settings live in `~/Library/Preferences/com.wallpaperengine.mac.plist`; per-wallpaper settings are in the same store (keys `wallpaperProperties.*`).
- Remove the app from System Settings → Privacy & Security → Accessibility if you granted it.

---

## Part 2 — Build from source

### What to install

| Tool | Why | How |
|---|---|---|
| Xcode Command Line Tools | `swiftc`, `clang++`, `libtool` | `xcode-select --install` |
| Homebrew | package manager for the libraries below | https://brew.sh |
| `molten-vk` | Vulkan → Metal translation (the engine renders with Vulkan) | `brew install molten-vk` |
| `ffmpeg` | video wallpaper decoding | `brew install ffmpeg` |
| `lz4`, `fontconfig`, `freetype` | scene archive and font support | `brew install lz4 fontconfig freetype` |
| `llvm` | clang + lld for the engine build (Apple clang has no lld) | `brew install llvm` |
| `glslang` | shader compilation | `brew install glslang` |
| **lito** | the engine's build system | build from source at `~/lito-repo` (see the repo README); its binaries end up in `~/lito-repo/build/install/bin` |

### Build order

```bash
# 1. The scene engine (open-wallpaper-engine, patched for MoltenVK)
#    -> scene_backend_new/build/install/lib/libwescene-renderer.a
bash scene_backend_new/BuildSceneBackend.sh

# 2. The app: compiles the ObjC++ bridge and Swift sources,
#    packages WallpaperEngineMac.app in build_manual/, and
#    bundles every Homebrew dylib it needs into the .app
#    (so the result runs without Homebrew installed)
bash build_app_manual.sh
```

The finished app is `build_manual/WallpaperEngineMac.app`. To install it, copy it to `/Applications` and follow the first-launch steps above.

### Notes

- **Ad-hoc signing:** the build re-signs the app with `codesign --force --sign -`. This invalidates the previous signature, so macOS treats each rebuild as a new app: Gatekeeper asks again on first launch and the Accessibility permission must be re-granted.
- **Source layout:**
  - `WallpaperEngineMac/` — the macOS app (Swift + an ObjC++ bridge to the engine)
  - `scene_backend_new/` — the current scene engine (from [open-wallpaper-engine](https://github.com/waywallen/open-wallpaper-engine), patched for MoltenVK; patches live in `scene_backend_new/patched/`)
  - `scene_backend/` — the older engine, kept for rollback; not built by the scripts
  - `Share/` — the prebuilt `.app` plus this guide, ready to hand to someone
- `scene_backend_new/BuildSceneBackend.sh` puts `~/lito-repo/build/install/bin` and Homebrew's LLVM on `PATH` itself; you don't have to do that manually.
