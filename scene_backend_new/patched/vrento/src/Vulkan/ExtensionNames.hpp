#pragma once

namespace vrento::vulkan::detail
{
using namespace rstd::prelude;
using rstd::collections::BTreeSet;
using rstd::ffi::CString;

static auto SelectExtensionNames(const BTreeSet<String>& supported, slice<Extension> requested)
    -> Result<Vec<CString>, String> {
    auto selected = BTreeSet<String>::make();
    for (const auto& extension : requested) {
        if (supported.contains(extension.name)) {
            selected.insert(String::make(extension.name));
        } else if (extension.required) {
            return Err(String::make(extension.name));
        }
    }
    auto names = Vec<CString>::with_capacity(selected.len());
    auto iterator = selected.iter();
    for (auto name : rstd::iter::for_range(iterator)) {
        auto encoded = CString::make(Vec<u8>::from(name->as_str().as_bytes()));
        if (encoded.is_err()) return Err(name->clone());
        names.push(rstd::move(encoded).unwrap());
    }
    return Ok(rstd::move(names));
}

} // namespace vrento::vulkan::detail
