# Vendored dependency patches (macOS)

Copies of the engine's dependencies with local patches, wired in
through `.lito/config.toml`'s `[patch]` table so the workspace
`lito.toml` still declares the upstream requirements. The old
engine's `scene_backend/ApplyMoltenVKPatches.sh` equivalents live
here; `scene_backend/` (the old catsout/wallpaper-scene-renderer
backend) is kept untouched for rollback.

## vvk (registry `litocpp/vvk` 0.1.0-9feeb7d057f3fa4d)

- `src/loader.cppm` — **MoltenVK library name.** The upstream
  loader only knows the official SDK layout (`libvulkan.1.dylib`).
  Homebrew's MoltenVK installs as `libMoltenVK.dylib`, so on macOS
  `Open()` tries that name first and falls back to the SDK name.
  (Old backend's ApplyMoltenVKPatches.sh patch #1.)

## wavsen (git `hypengw/wavsen` @ 73be92f4f9db)

- `src/video/apple/mod.cppm` — **Metal texture ownership registry.**
  vrento tracks Metal video textures as raw `MTLTexture` handles.
  The owning `MetalVideoAdapter`/`MetalVideoTexture` must outlive
  the Vulkan share of that texture, so they are parked in a registry
  keyed by the native handle and released through
  `release_apple_video_metal_texture()`. Implements
  `create_apple_video_metal_texture()` / `release_apple_video_metal_texture()`,
  the C API vrento's `TextureCache` calls.
- `lito.toml` — **explicit sources for the `[lib]` target.** lito's
  module-discovery mode only compiles *generated* source groups, so
  the `apple-native` group (`src/video/apple/metal.mm`,
  `src/audio/backend/coreaudio/process_tap.mm` — the Metal/CVPixelBuffer
  and CoreAudio tap implementations) would never be compiled on
  macOS. Explicit-sources mode compiles both the listed sources and
  existing source groups. The Pulse backend files are omitted: they
  are only imported under `#if !defined(__APPLE__)`.

## vrento (git `vecren/vrento` @ a781859c9403)

- `src/Vulkan/TextureCache.cpp` — added `import wavsen.video.apple;`
  so the Metal texture helpers above are visible where the cache
  creates/releases Metal-backed textures.

## Rebuilding

`scene_backend_new/BuildSceneBackend.sh` builds the workspace with
lito (bootstrapped at `/tmp/lito-repo`, brew LLVM + lld on PATH),
then merges every archive into `build/install/lib/libwescene-renderer.a`
and installs `src/Scene/Bridge/wescene_bridge.h`.

Engine logging: the bridge installs rstd-log's `EnvLogger`
(`wescene_bridge_enable_env_logging()`), so
`RSTD_LOG=info ./WallpaperEngineMac ...` shows scene-load, shader
and parse messages on stderr.
