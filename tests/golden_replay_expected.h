#pragma once

#include <cstdint>

namespace nsync::test {

inline constexpr std::uint32_t kGoldenTickRate = 10;
inline constexpr std::uint32_t kGoldenTickCount = 120;
inline constexpr std::uint64_t kGoldenTickHashes[] = {
    0x9B63D323AF91BECAULL,
    0x00B5750FBEBB00CDULL,
    0x91158F57F0F7374BULL,
    0x724E4E00C037AC02ULL,
    0xE5C3B85238A96EFFULL,
    0xCDE1C16BC24C494FULL,
    0x148B0980DAB9FB0AULL,
    0xD3637C0851A1D59CULL,
    0xD90423687101587DULL,
    0xF37D8825B8534D1AULL,
    0x3446C09DAE86C07DULL,
    0x79BD944267C58A39ULL,
    0xEC026EEB7CD0D247ULL,
};

} // namespace nsync::test
