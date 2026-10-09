#pragma once

// C API for the open-wallpaper-engine scene renderer.
//
// The engine is built as C++20 modules with the lito
// build system; this header is the only part the host
// application needs. Link against libwescene-renderer.a
// plus MoltenVK, lz4 and the frameworks the engine uses.
//
// Paths must be UTF-8. All calls are non-blocking: they
// post to the engine's internal loopers and return
// immediately.

#ifdef __cplusplus
extern "C" {
#endif

// Creates a renderer that presents into the given
// CAMetalLayer (pass the layer pointer; the engine
// creates its Vulkan surface from it). width/height are
// the layer's pixel size. Returns NULL if the engine
// failed to initialize.
void* wescene_bridge_create(void* metal_layer, unsigned width, unsigned height);

// Releases the renderer and stops its threads.
void wescene_bridge_destroy(void* bridge);

// Installs the engine's environment logger, so engine
// messages appear on stderr when the RSTD_LOG environment
// variable is set (e.g. RSTD_LOG=info). Safe to call
// repeatedly; only the first call takes effect.
void wescene_bridge_enable_env_logging(void);

// Loads (or reloads) a wallpaper. scene_json is the
// path of the scene's scene.json — the engine derives
// the scene.pkg archive from it. assets_dir is the
// unpacked assets directory, cache_dir a scratch
// directory the engine may use.
void wescene_bridge_load(void* bridge,
                         const char* scene_json,
                         const char* assets_dir,
                         const char* cache_dir);

void wescene_bridge_play(void* bridge);
void wescene_bridge_pause(void* bridge);

// Mouse position in normalized view coordinates
// (0..1, y measured from the top of the view).
void wescene_bridge_mouse_input(void* bridge, double x, double y);

void wescene_bridge_set_fps(void* bridge, unsigned fps);
// fill_mode: 0 = stretch, 1 = aspect fit, 2 = aspect crop.
void wescene_bridge_set_fill_mode(void* bridge, int fill_mode);
void wescene_bridge_set_speed(void* bridge, float speed);
void wescene_bridge_set_volume(void* bridge, float volume);
void wescene_bridge_set_muted(void* bridge, int muted);

#ifdef __cplusplus
}
#endif
