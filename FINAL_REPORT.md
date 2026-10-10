# Final Report — Wallpaper Engine Mac

**Status: COMPLETE.** The software is finished and installed at
`/Applications/WallpaperEngineMac.app`.

---

## What it is

A macOS **native** app (Swift / AppKit / SwiftUI + an Obj-C++ bridge to a
C++ render engine) that runs **Steam Wallpaper Engine** (Steam appId
`431960`) wallpapers as real macOS desktop backgrounds — no Screen Engine,
no Windows, no Steam client required at runtime.

- Reads wallpapers directly from the Steam library folders
  (`steamapps/workshop/content/431960/`, `steamapps/common/wallpaper_engine/projects/`).
- Agent app (`LSUIElement`): lives in the menu bar, no Dock icon.

## Features

| Area | Support |
|---|---|
| **Video wallpapers** | AVPlayer + AVPlayerLayer, VideoToolbox hardware decode (Apple Silicon & Intel), H.264/HEVC, volume/mute, play/pause |
| **Web wallpapers** | WKWebView with the Wallpaper Engine JS bridge (`wallpaperPropertyListener`, `applyGeneralProperties`/`applyUserProperties`, audio registration), transparent background, mouse forwarding via CGEventTap (Accessibility permission) |
| **3D scene wallpapers** | `open-wallpaper-engine` C++ renderer (Vulkan) translated to Metal via MoltenVK; scene.pkg parsing (MDL/TEXB/animations, textures, puppets, audio) |
| **Multi-monitor** | One wallpaper window per screen (keyed by CGDisplayID) |
| **Smart pause** | Pause on fullscreen/maximized/focused window (configurable modes), pause on battery / low battery, pause on screen sleep/lock |
| **Per-wallpaper settings** | Renders every `project.json` property: checkbox, slider, color picker, combo, text, button, texture browse, group headers — including conditional visibility (`shu.value==true` etc.), saved per wallpaper |
| **Display settings** | Flip (H/V/both) — Waywallen-style, applied live to the running wallpaper |
| **Gallery** | SwiftUI grid with thumbnails (preview.jpg/gif), search, type filter, preview window, context menus (Preview / Settings & Info / Reveal in Finder / Remove) |
| **Menu bar** | Pause/Resume, Random, Remove, Display Mode, Pause Mode, Volume, Mute, Mouse Input toggle, Settings (Steam path) |
| **Persistence** | Remembers the last wallpaper, per-wallpaper settings, all display settings |
| **Launch at login** | "Launch at Login" toggle in Settings (default on) — registers via SMAppService, shows in System Settings → General → Login Items |
| **Self-contained .app** | Bundles MoltenVK, FFmpeg, lz4, fontconfig, freetype, etc. into `Contents/Frameworks/` — runs on a Mac **without Homebrew** |

## Build & install (from source)

```bash
cd ~/Desktop/WallpaperEngineMac

# 1. Scene engine (open-wallpaper-engine, patched for MoltenVK)
bash scene_backend_new/BuildSceneBackend.sh
#    -> scene_backend_new/build/install/lib/libwescene-renderer.a

# 2. The app (compiles bridge + Swift, packages + bundles dylibs)
bash build_app_manual.sh
#    -> build_manual/WallpaperEngineMac.app

# 3. Install
codesign --force --sign - build_manual/WallpaperEngineMac.app
rm -rf /Applications/WallpaperEngineMac.app
cp -R build_manual/WallpaperEngineMac.app /Applications/
```

First launch: control-click → Open (ad-hoc signed, not notarized).
Accessibility permission is requested on first use of mouse-input wallpapers
(and must be re-granted after each rebuild, since re-signing changes the
code signature).

## App icon

`WallpaperEngineMac.png` (1254×1254, corners pre-rounded to the
macOS squircle shape) → `AppIcon.icns` (all standard sizes, built
by `build_app_manual.sh` via `sips` + `iconutil`; macOS Big Sur+
also applies its own system squircle mask). The earlier
`AppIcon.png` / `AppIcon_raw.png` and the interim
`wallpaper engine.png` sources were deleted. Menu-bar icon
remains `MenuBarIcon.png`.

## Known limitations (documented, accepted)

- **Scene wallpaper animation-id offset** (MDLV0023/MDLA0006 format): some
  wallpapers' animations don't play (character holds first pose) — mesh,
  textures, bones all fine. Details and fix strategy in `TODO_FIX_LATER.md`.
- Pre-existing scene-engine warnings (`_rt_shadowAtlas`, `ui_editor_effect_glitter_title`
  shader link, benign `VK_ERROR_FEATURE_NOT_PRESENT`) — cosmetic, present in
  all wallpapers, do not affect rendering.
- AV1 video: only Apple Silicon M3+; the app converts to H.264 when ffmpeg
  is available. VP9 webm: depends on macOS version.
- Audio visualization for web wallpapers (`wallpaperRegisterAudioListener`):
  bridge registered, no data feed — not implemented.
- Position (X/Y) and rotation are kept in the settings model and the
  transform code (and the `WEM_DEBUG_TRANSFORM` debug hook) but are
  hidden from the UI; only Flip is exposed.

## Docs

- `README.md` — architecture, feature mapping (Plasma6/Waywallen → macOS),
  build, troubleshooting, display-settings deep dive.
- `INSTALL.md` — install guide for a new Mac (prebuilt + from source).
- `HUONG_DAN_CAI_DAT.md` — user guide (Vietnamese).
- `TODO_FIX_LATER.md` — known scene-parser bugs with evidence and fix plans.
- `CHUYEN_MAC.md` and `Group 5.png` were deleted from the working tree
  before the final icon swap (migration checklist, superseded by INSTALL.md).

## Final commits

```
281ba63 Document rotation/flip/position display settings
161ac8c Make rotation/flip/offset apply live to the wallpaper
908a668 Start scene playback after load; fix uninitialized audio state
```
