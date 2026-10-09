#pragma once

namespace wavsen::audio::backend {
auto create_process_tap(unsigned int* tap, const void** uid) noexcept -> int;
void destroy_process_tap(unsigned int tap) noexcept;
}
