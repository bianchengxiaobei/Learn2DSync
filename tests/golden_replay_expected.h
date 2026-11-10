#pragma once

#include <cstdint>

namespace nsync::test {

inline constexpr std::uint32_t kGoldenTickRate = 10;
inline constexpr std::uint32_t kGoldenTickCount = 120;
inline constexpr std::uint64_t kGoldenTickHashes[] = {
    0xD1964822F1BF45FBULL,
    0xBDD35F8D6A68BD46ULL,
    0x68DB823EF21A314FULL,
    0xB114140C45EBC67CULL,
    0x1EDD10F0403443FBULL,
    0x73CBA493C55EDA9BULL,
    0xCF1714D2D0AD7247ULL,
    0x7ED3366D92621A4AULL,
    0x09E8151EF2E44FDCULL,
    0x6A8BC294D964AB93ULL,
    0xDF72746684DE19F1ULL,
    0x00E7B35A4F513683ULL,
    0x6325F653DD2B1B56ULL,
};

} // namespace nsync::test
