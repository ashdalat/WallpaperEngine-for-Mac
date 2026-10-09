export module vvk:format;
import rstd;
import :ffi.vulkan;

export namespace rstd
{
template<>
struct Impl<fmt::Display, VkResult> : ImplBase<VkResult> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};
template<>
struct Impl<fmt::Display, VkFormat> : ImplBase<VkFormat> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};
template<>
struct Impl<fmt::Display, VkColorSpaceKHR> : ImplBase<VkColorSpaceKHR> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};
} // namespace rstd
