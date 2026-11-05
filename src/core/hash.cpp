#include "core/hash.h"

namespace nsync::core {
namespace {

constexpr std::uint64_t kPrime1 = 11400714785074694791ULL;
constexpr std::uint64_t kPrime2 = 14029467366897019727ULL;
constexpr std::uint64_t kPrime3 = 1609587929392839161ULL;
constexpr std::uint64_t kPrime4 = 9650029242287828579ULL;
constexpr std::uint64_t kPrime5 = 2870177450012600261ULL;

constexpr std::uint64_t rotate_left(std::uint64_t value, int amount) {
    return (value << amount) | (value >> (64 - amount));
}

constexpr std::uint64_t read_u32(const std::uint8_t* data) {
    return static_cast<std::uint64_t>(data[0]) |
           (static_cast<std::uint64_t>(data[1]) << 8) |
           (static_cast<std::uint64_t>(data[2]) << 16) |
           (static_cast<std::uint64_t>(data[3]) << 24);
}

constexpr std::uint64_t read_u64(const std::uint8_t* data) {
    return read_u32(data) | (read_u32(data + 4) << 32);
}

constexpr std::uint64_t round(std::uint64_t accumulator, std::uint64_t input) {
    accumulator += input * kPrime2;
    accumulator = rotate_left(accumulator, 31);
    accumulator *= kPrime1;
    return accumulator;
}

constexpr std::uint64_t merge_round(std::uint64_t accumulator, std::uint64_t value) {
    accumulator ^= round(0, value);
    accumulator = accumulator * kPrime1 + kPrime4;
    return accumulator;
}

constexpr std::uint64_t avalanche(std::uint64_t value) {
    value ^= value >> 33;
    value *= kPrime2;
    value ^= value >> 29;
    value *= kPrime3;
    value ^= value >> 32;
    return value;
}

} // namespace

std::uint64_t hash64_bytes(const void* data, std::size_t size, std::uint64_t seed) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    const auto* cursor = bytes;
    const auto* end = bytes + size;
    std::uint64_t hash = 0;

    if (size >= 32) {
        std::uint64_t v1 = seed + kPrime1 + kPrime2;
        std::uint64_t v2 = seed + kPrime2;
        std::uint64_t v3 = seed;
        std::uint64_t v4 = seed - kPrime1;

        do {
            v1 = round(v1, read_u64(cursor));
            v2 = round(v2, read_u64(cursor + 8));
            v3 = round(v3, read_u64(cursor + 16));
            v4 = round(v4, read_u64(cursor + 24));
            cursor += 32;
        } while (cursor <= end - 32);

        hash = rotate_left(v1, 1) + rotate_left(v2, 7) + rotate_left(v3, 12) + rotate_left(v4, 18);
        hash = merge_round(hash, v1);
        hash = merge_round(hash, v2);
        hash = merge_round(hash, v3);
        hash = merge_round(hash, v4);
    } else {
        hash = seed + kPrime5;
    }

    hash += static_cast<std::uint64_t>(size);

    while (cursor + 8 <= end) {
        hash ^= round(0, read_u64(cursor));
        hash = rotate_left(hash, 27) * kPrime1 + kPrime4;
        cursor += 8;
    }

    if (cursor + 4 <= end) {
        hash ^= read_u32(cursor) * kPrime1;
        hash = rotate_left(hash, 23) * kPrime2 + kPrime3;
        cursor += 4;
    }

    while (cursor < end) {
        hash ^= static_cast<std::uint64_t>(*cursor) * kPrime5;
        hash = rotate_left(hash, 11) * kPrime1;
        ++cursor;
    }

    return avalanche(hash);
}

std::uint64_t hash64(std::string_view text, std::uint64_t seed) {
    return hash64_bytes(text.data(), text.size(), seed);
}

std::uint64_t hash_combine(std::uint64_t hash, std::uint64_t value) {
    return hash64_bytes(&value, sizeof(value), hash);
}

} // namespace nsync::core
