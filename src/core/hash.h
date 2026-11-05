#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace nsync::core {

std::uint64_t hash64_bytes(const void* data, std::size_t size, std::uint64_t seed = 0);
std::uint64_t hash64(std::string_view text, std::uint64_t seed = 0);
std::uint64_t hash_combine(std::uint64_t hash, std::uint64_t value);

} // namespace nsync::core
