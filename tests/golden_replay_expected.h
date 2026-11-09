#pragma once

#include <cstdint>

namespace nsync::test {

inline constexpr std::uint32_t kGoldenTickRate = 10;
inline constexpr std::uint32_t kGoldenTickCount = 120;
inline constexpr std::uint64_t kGoldenTickHashes[] = {
    0xBCD9A0A679CC0154ULL,
    0xEC5DD3B3D46B94E4ULL,
    0x88D543E609DB7B8EULL,
    0x4F3B8B6C17A5BF9BULL,
    0x1DC3DBC915DE058DULL,
    0x4038A3055DB2AB7FULL,
    0xABCC3E70ACC6B29DULL,
    0x5735277A6C8747A2ULL,
    0xF8762AA008AA3DB2ULL,
    0x562263C671D0EA25ULL,
    0xB8CA263187C73B86ULL,
    0x0C0FF9CB19734B8EULL,
    0xDFDEEDEEB52FC2B6ULL,
};

} // namespace nsync::test
