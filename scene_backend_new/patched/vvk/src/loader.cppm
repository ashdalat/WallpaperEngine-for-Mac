module;
#include <rstd/macro.hpp>
export module vvk:loader;
import rstd;
import rstd.dlopn;
export import :dispatch;
using namespace rstd::prelude;

namespace vvk
{
struct LoaderState {
    Option<rstd::dlopn::Library> library;
    GlobalDispatch               global;
    LoaderState(Option<rstd::dlopn::Library> library, GlobalDispatch global)
        : library(rstd::move(library)), global(global) {}
};
export enum class LoaderErrorKind { OpenLibrary, RootSymbol, Dispatch };
export struct LoaderError {
    LoaderErrorKind kind;
    String          message;
    DispatchError   dispatch {};
};

/// The loader must outlive every derived dispatch and Vulkan object. Moving preserves table
/// addresses.
export class VulkanLoader {
    Box<LoaderState> state_;
    explicit VulkanLoader(Box<LoaderState> state): state_(rstd::move(state)) {}

public:
    VulkanLoader(const VulkanLoader&)               = delete;
    VulkanLoader& operator=(const VulkanLoader&)    = delete;
    VulkanLoader(VulkanLoader&&)                    = default;
    VulkanLoader&         operator=(VulkanLoader&&) = default;
    const GlobalDispatch& global() const noexcept [[clang::lifetimebound]] {
        return state_->global;
    }
    static auto FromResolver(PFN_vkGetInstanceProcAddr resolver)
        -> Result<VulkanLoader, LoaderError> {
        auto loaded = LoadGlobal(resolver);
        if (loaded.is_err())
            return Err(LoaderError {
                LoaderErrorKind::Dispatch, String {}, loaded.unwrap_err_unchecked() });
        return Ok(VulkanLoader(Box<LoaderState>::make(None(), loaded.unwrap_unchecked())));
    }
    static auto Open(rstd::ref<rstd::ffi::CStr> path) -> Result<VulkanLoader, LoaderError> {
        auto opened = rstd::dlopn::Library::open(path);
        if (opened.is_err())
            return Err(LoaderError { LoaderErrorKind::OpenLibrary,
                                     String::make(opened.unwrap_err_unchecked().message()) });
        auto library  = rstd::move(opened).unwrap_unchecked();
        auto resolver = library.symbol<PFN_vkGetInstanceProcAddr>(
            rstd::ffi::CStr::from_ptr("vkGetInstanceProcAddr"));
        if (resolver.is_err())
            return Err(LoaderError { LoaderErrorKind::RootSymbol,
                                     String::make(resolver.unwrap_err_unchecked().message()) });
        if (! resolver.unwrap_unchecked())
            return Err(LoaderError { LoaderErrorKind::RootSymbol, String {} });
        auto loaded = LoadGlobal(resolver.unwrap_unchecked());
        if (loaded.is_err())
            return Err(LoaderError {
                LoaderErrorKind::Dispatch, String {}, loaded.unwrap_err_unchecked() });
        return Ok(VulkanLoader(
            Box<LoaderState>::make(Some(rstd::move(library)), loaded.unwrap_unchecked())));
    }
    static auto Open() -> Result<VulkanLoader, LoaderError> {
#if RSTD_OS_MACOS
        // The upstream loader only knows the official MoltenVK SDK
        // layout (libvulkan.1.dylib). Homebrew installs the
        // implementation as libMoltenVK.dylib, so try that name
        // first and fall back to the SDK name.
        auto molten = Open(rstd::ffi::CStr::from_ptr("libMoltenVK.dylib"));
        if (molten.is_ok()) return molten;
        return Open(rstd::ffi::CStr::from_ptr("libvulkan.1.dylib"));
#else
        return Open(rstd::ffi::CStr::from_ptr("libvulkan.so.1"));
#endif
    }
};
} // namespace vvk
