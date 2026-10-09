module;

export module viewer.web;

export import :presenter;
export import :vulkan_blitter;
#if defined(__linux__)
export import :egl_presenter;
#endif
