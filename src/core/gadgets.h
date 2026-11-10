#pragma once

#include "core/content.h"
#include "core/fix32.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace nsync::core {

inline constexpr std::size_t kGadgetSlots = 3;

struct JetPackTuning {
    Fix32 capacity = Fix32::from_int(300);
    Fix32 consumption_per_tick = Fix32::one();
    Fix32 refuel_per_tick = Fix32::from_ratio(1, 2);
    Fix32 operating_fuel = Fix32::from_int(50);
};

inline constexpr JetPackTuning kJetPackTuning{};

enum class GadgetType : std::uint8_t {
    RubyBlade = 0,
    PowerBattery,
    MagneticNozzle,
    FragGrenade,
    StaticShield,
    Kickstart,
    RocketLaunch,
    ProtectiveDroid,
    FirstAid,
    Injection,
    RocketSalvo,
    TurboPack,
    Toughness,
    ConcussionGrenade,
    FragMine,
    Neutralizer,
    Boomerang,
    Trap,
    DoubleShotSystem,
    Sprayer,
    Chemicals,
    Bloodletter,
    Chakram,
    Dematerializator,
    Mutator,
    DemonicFace,
    FireTrap,
    Avatar,
    HyperAccelerator,
    ArmCyberProsthesis,
    Inhibitor,
    Count,
};

struct GadgetStats {
    GadgetType type = GadgetType::RubyBlade;
    std::string_view id;
    std::string_view name_en;
    std::string_view name_zh;
    std::uint16_t cooldown_ticks = 0;
    std::uint16_t active_ticks = 0;
    std::uint16_t damage = 0;
    std::uint16_t explosion_damage = 0;
    std::uint16_t effect_ticks = 0;
    std::uint16_t projectile_life = 0;
    std::uint8_t damage_percent = 0;
    Fix32 radius{};
    Fix32 speed{};
    Fix32 impulse{};
    std::uint16_t heal = 0;
    std::uint8_t damage_reduction_percent = 0;
    std::uint8_t heal_percent = 0;
};

struct GadgetKinematics {
    Fix32 joint_x{};
    Fix32 joint_y{};
    Fix32 gun_x{};
    Fix32 gun_y{};
};

[[nodiscard]] constexpr GadgetKinematics gadget_kinematics(
    GadgetType type) {
    switch (type) {
        case GadgetType::FragGrenade:
        case GadgetType::ConcussionGrenade:
            return {
                Fix32::from_ratio(1, 20),
                Fix32::from_ratio(-46, 1000),
                Fix32::from_ratio(86, 100),
                Fix32::from_ratio(84, 1000)};
        case GadgetType::RocketLaunch:
            return {
                Fix32::from_ratio(1, 20),
                Fix32::from_ratio(1, 5),
                Fix32::from_ratio(23, 20),
                Fix32::from_ratio(11, 50)};
        case GadgetType::RocketSalvo:
            return {
                Fix32::from_ratio(287, 1000),
                Fix32::from_ratio(1, 5),
                Fix32::from_ratio(6, 5),
                Fix32::from_ratio(1, 5)};
        case GadgetType::FragMine:
        case GadgetType::Trap:
            return {
                Fix32::zero(),
                Fix32::from_ratio(-3, 10),
                Fix32::zero(),
                Fix32::zero()};
        case GadgetType::Boomerang:
            return {
                Fix32::zero(),
                Fix32::zero(),
                Fix32::from_ratio(15, 100),
                Fix32::from_ratio(1, 2)};
        case GadgetType::Sprayer:
            return {
                Fix32::zero(),
                Fix32::zero(),
                Fix32::from_ratio(1, 2),
                Fix32::zero()};
        case GadgetType::Chakram:
            return {
                Fix32::from_ratio(7, 100),
                Fix32::from_ratio(166, 1000),
                Fix32::from_ratio(11, 20),
                Fix32::from_ratio(15, 100)};
        default:
            return {};
    }
}

inline constexpr GadgetStats kGadgetStats[] = {
    {
        GadgetType::RubyBlade,
        "ruby_blade",
        "RUBY BLADE",
        "红宝石之刃",
        720,
        0,
        100,
        0,
        0,
        0,
        0,
        Fix32::from_ratio(13, 10),
        Fix32::zero(),
        Fix32::zero(),
    },
    {
        GadgetType::PowerBattery,
        "power_battery",
        "POWER BATTERY",
        "能量电池",
        900,
        120,
        0,
        0,
        0,
        0,
        0,
        Fix32::zero(),
        Fix32::zero(),
        Fix32::zero(),
    },
    {
        GadgetType::MagneticNozzle,
        "magnetic_nozzle",
        "MAGNETIC NOZZLE",
        "磁力喷嘴",
        720,
        180,
        0,
        0,
        0,
        0,
        60,
        Fix32::zero(),
        Fix32::zero(),
        Fix32::zero(),
    },
    {
        GadgetType::FragGrenade,
        "frag_grenade",
        "FRAG GRENADE",
        "破片手雷",
        692,
        0,
        0,
        160,
        0,
        120,
        0,
        Fix32::from_ratio(35, 10),
        Fix32::from_ratio(1, 5),
        Fix32::zero(),
    },
    {
        GadgetType::StaticShield,
        "static_shield",
        "STATIC SHIELD",
        "静电护盾",
        1200,
        0,
        0,
        20,
        120,
        180,
        0,
        Fix32::from_int(2),
        Fix32::from_ratio(2, 5),
        Fix32::zero(),
    },
    {
        GadgetType::Kickstart,
        "kickstart",
        "KICKSTART",
        "氮气加速",
        360,
        10,
        0,
        0,
        0,
        0,
        0,
        Fix32::zero(),
        Fix32::zero(),
        Fix32::from_ratio(9, 5),
    },
    {
        GadgetType::RocketLaunch,
        "rocket_launch",
        "ROCKET LAUNCH",
        "火箭发射",
        900,
        0,
        46,
        24,
        0,
        100,
        0,
        Fix32::from_int(2),
        Fix32::from_ratio(2, 5),
        Fix32::zero(),
    },
    {
        GadgetType::ProtectiveDroid,
        "protective_droid",
        "PROTECTIVE DROID",
        "防护机器人",
        360,
        0,
        0,
        50,
        300,
        1200,
        0,
        Fix32::from_ratio(3, 2),
        Fix32::from_ratio(7, 20),
        Fix32::zero(),
    },
    {
        GadgetType::FirstAid,
        "first_aid",
        "FIRST AID",
        "急救包",
        1200,
        480,
        25,
        0,
        60,
        0,
        0,
        Fix32::zero(),
        Fix32::zero(),
        Fix32::zero(),
        25,
        0,
        8,
    },
    {
        GadgetType::Injection,
        "injection",
        "INJECTION",
        "注射器",
        1200,
        182,
        0,
        0,
        180,
        0,
        0,
        Fix32::zero(),
        Fix32::zero(),
        Fix32::zero(),
        250,
        0,
        60,
    },
    {
        GadgetType::RocketSalvo,
        "rocket_salvo",
        "ROCKET SALVO",
        "火箭齐射",
        16,
        0,
        50,
        25,
        0,
        100,
        0,
        Fix32::from_int(2),
        Fix32::from_ratio(2, 5),
        Fix32::zero(),
    },
    {
        GadgetType::TurboPack,
        "turbo_pack",
        "TURBO PACK",
        "涡轮背包",
        60,
        60,
        0,
        0,
        0,
        0,
        0,
        Fix32::zero(),
        Fix32::from_int(4),
        Fix32::from_int(4),
    },
    {
        GadgetType::Toughness,
        "toughness",
        "TOUGHNESS",
        "鍧氶煣",
        1200,
        480,
        0,
        0,
        0,
        0,
        0,
        Fix32::zero(),
        Fix32::zero(),
        Fix32::zero(),
        0,
        30,
    },
    {
        GadgetType::ConcussionGrenade,
        "concussion_grenade",
        "CONCUSSION GRENADE",
        "震荡手雷",
        692,
        0,
        0,
        50,
        120,
        120,
        0,
        Fix32::from_ratio(26, 10),
        Fix32::from_ratio(1, 5),
        Fix32::zero(),
    },
    {
        GadgetType::FragMine,
        "frag_mine",
        "FRAG MINE",
        "破片地雷",
        900,
        0,
        0,
        160,
        0,
        1500,
        0,
        Fix32::from_ratio(35, 10),
        Fix32::zero(),
        Fix32::zero(),
    },
    {
        GadgetType::Neutralizer,
        "neutralizer",
        "NEUTRALIZER",
        "中和器",
        600,
        0,
        0,
        0,
        0,
        0,
        0,
        Fix32::zero(),
        Fix32::zero(),
        Fix32::zero(),
    },
    {
        GadgetType::Boomerang,
        "boomerang",
        "BOOMERANG",
        "回旋镖",
        1160,
        0,
        80,
        0,
        0,
        90,
        0,
        Fix32::from_ratio(2, 5),
        Fix32::from_ratio(1, 2),
        Fix32::zero(),
    },
    {
        GadgetType::Trap,
        "trap",
        "TRAP",
        "陷阱",
        600,
        0,
        0,
        0,
        240,
        900,
        0,
        Fix32::from_ratio(32, 100),
        Fix32::zero(),
        Fix32::zero(),
    },
    {
        GadgetType::DoubleShotSystem,
        "double_shot_system",
        "DOUBLE SHOT SYSTEM",
        "双发系统",
        720,
        480,
        0,
        0,
        0,
        0,
        40,
        Fix32::zero(),
        Fix32::zero(),
        Fix32::zero(),
    },
    {
        GadgetType::Sprayer,
        "sprayer",
        "SPRAYER",
        "喷洒器",
        900,
        0,
        0,
        50,
        0,
        4,
        0,
        Fix32::from_ratio(16, 10),
        Fix32::one(),
        Fix32::zero(),
    },
    {
        GadgetType::Chemicals,
        "chemicals",
        "CHEMICALS",
        "化学物",
        1200,
        0,
        0,
        30,
        240,
        4,
        0,
        Fix32::from_int(3),
        Fix32::zero(),
        Fix32::zero(),
    },
    {
        GadgetType::Bloodletter,
        "bloodletter",
        "BLOODLETTER",
        "放血者",
        90,
        480,
        0,
        0,
        0,
        0,
        35,
        Fix32::zero(),
        Fix32::zero(),
        Fix32::zero(),
    },
    {
        GadgetType::Chakram,
        "chakram",
        "CHAKRAM",
        "战轮",
        706,
        0,
        100,
        0,
        0,
        72,
        0,
        Fix32::from_ratio(2, 5),
        Fix32::from_ratio(1, 2),
        Fix32::zero(),
    },
    {
        GadgetType::Dematerializator,
        "dematerializator",
        "DEMATERIALIZATOR",
        "物质分解器",
        720,
        120,
        0,
        0,
        0,
        0,
        0,
        Fix32::zero(),
        Fix32::zero(),
        Fix32::zero(),
    },
    {
        GadgetType::Mutator,
        "mutator",
        "MUTATOR",
        "变异器",
        900,
        120,
        0,
        0,
        0,
        0,
        10,
        Fix32::zero(),
        Fix32::zero(),
        Fix32::zero(),
    },
    {
        GadgetType::DemonicFace,
        "demonic_face",
        "DEMONIC FACE",
        "恶魔面具",
        900,
        0,
        20,
        0,
        0,
        90,
        0,
        Fix32::one(),
        Fix32::from_ratio(2, 5),
        Fix32::zero(),
    },
    {
        GadgetType::FireTrap,
        "fire_trap",
        "FIRE TRAP",
        "火焰陷阱",
        1200,
        0,
        0,
        140,
        120,
        150,
        0,
        Fix32::from_int(3),
        Fix32::zero(),
        Fix32::zero(),
    },
    {
        GadgetType::Avatar,
        "avatar",
        "AVATAR",
        "化身",
        900,
        390,
        0,
        0,
        0,
        0,
        50,
        Fix32::zero(),
        Fix32::zero(),
        Fix32::zero(),
    },
    {
        GadgetType::HyperAccelerator,
        "hyper_accelerator",
        "HYPER ACCELERATOR",
        "超级加速器",
        720,
        18,
        0,
        0,
        0,
        0,
        0,
        Fix32::zero(),
        Fix32::zero(),
        Fix32::from_ratio(7, 5),
    },
    {
        GadgetType::ArmCyberProsthesis,
        "arm_cyber_prosthesis",
        "ARM CYBER PROSTHESIS",
        "机械义肢",
        1200,
        0,
        0,
        100,
        90,
        16,
        0,
        Fix32::from_ratio(18, 10),
        Fix32::zero(),
        Fix32::zero(),
    },
    {
        GadgetType::Inhibitor,
        "inhibitor",
        "INHIBITOR",
        "抑制剂",
        900,
        300,
        0,
        0,
        0,
        0,
        0,
        Fix32::zero(),
        Fix32::zero(),
        Fix32::zero(),
    },
};

inline constexpr std::array<std::array<GadgetType, kGadgetSlots>, 10>
    kHeroGadgets = {{
        {GadgetType::RubyBlade,
         GadgetType::PowerBattery,
         GadgetType::MagneticNozzle},
        {GadgetType::FragGrenade,
         GadgetType::StaticShield,
         GadgetType::Injection},
        {GadgetType::RocketSalvo,
         GadgetType::TurboPack,
         GadgetType::Toughness},
        {GadgetType::ConcussionGrenade,
         GadgetType::FragMine,
         GadgetType::Neutralizer},
        {GadgetType::Boomerang,
         GadgetType::Trap,
         GadgetType::DoubleShotSystem},
        {GadgetType::Sprayer,
         GadgetType::Chemicals,
         GadgetType::Bloodletter},
        {GadgetType::Chakram,
         GadgetType::Dematerializator,
         GadgetType::Mutator},
        {GadgetType::DemonicFace,
         GadgetType::FireTrap,
         GadgetType::Avatar},
        {GadgetType::RocketLaunch,
         GadgetType::ProtectiveDroid,
         GadgetType::FirstAid},
        {GadgetType::HyperAccelerator,
         GadgetType::ArmCyberProsthesis,
         GadgetType::Inhibitor},
    }};

[[nodiscard]] constexpr const GadgetStats& gadget_stats(GadgetType type) {
    const std::size_t index = static_cast<std::size_t>(type);
    return kGadgetStats[
        index < static_cast<std::size_t>(GadgetType::Count) ? index : 0];
}

[[nodiscard]] constexpr GadgetType hero_gadget(
    HeroType hero,
    std::size_t slot) {
    const std::size_t hero_index = static_cast<std::size_t>(hero);
    if (hero_index >= kHeroGadgets.size() || slot >= kGadgetSlots) {
        return GadgetType::RubyBlade;
    }
    return kHeroGadgets[hero_index][slot];
}

[[nodiscard]] constexpr const GadgetStats& hero_gadget_stats(
    HeroType hero,
    std::size_t slot) {
    return gadget_stats(hero_gadget(hero, slot));
}

[[nodiscard]] constexpr bool gadget_is_dash(GadgetType type) {
    return type == GadgetType::Kickstart ||
           type == GadgetType::TurboPack ||
           type == GadgetType::HyperAccelerator;
}

[[nodiscard]] constexpr bool gadget_is_shock_resist(GadgetType type) {
    return type == GadgetType::Neutralizer ||
           type == GadgetType::Inhibitor;
}

[[nodiscard]] constexpr bool gadget_uses_fuel(GadgetType type) {
    return type == GadgetType::TurboPack;
}

[[nodiscard]] constexpr bool gadget_is_projectile(GadgetType type) {
    switch (type) {
        case GadgetType::FragGrenade:
        case GadgetType::StaticShield:
        case GadgetType::RocketLaunch:
        case GadgetType::ProtectiveDroid:
        case GadgetType::RocketSalvo:
        case GadgetType::ConcussionGrenade:
        case GadgetType::FragMine:
        case GadgetType::Boomerang:
        case GadgetType::Trap:
        case GadgetType::Sprayer:
        case GadgetType::Chemicals:
        case GadgetType::Chakram:
        case GadgetType::DemonicFace:
        case GadgetType::FireTrap:
        case GadgetType::ArmCyberProsthesis:
            return true;
        default:
            return false;
    }
}

} // namespace nsync::core
