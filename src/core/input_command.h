#pragma once

#include "core/entity.h"

#include <cstdint>
#include <vector>

namespace nsync::core {

enum Button : std::uint16_t {
    kButtonLeft = 1U << 0U,
    kButtonRight = 1U << 1U,
    kButtonJump = 1U << 2U,
    kButtonDown = 1U << 3U,
    kButtonFire = 1U << 4U,
    kButtonReload = 1U << 5U,
    kButtonWeaponSwitch = 1U << 6U,
    kButtonGadget1 = 1U << 7U,
    kButtonGadget2 = 1U << 8U,
    kButtonGadget3 = 1U << 9U,
    kButtonAbility = kButtonGadget1,
};

struct InputCommand {
    std::uint32_t tick = 0;
    std::uint16_t buttons = 0;
    std::int16_t aim_x = 0;
    std::int16_t aim_y = 0;
    std::uint8_t sequence = 0;
};

struct PlayerCommand {
    EntityId entity{};
    InputCommand command{};
};

struct InputFrame {
    std::vector<PlayerCommand> commands;
};

inline bool button_down(std::uint16_t buttons, Button button) {
    return (buttons & static_cast<std::uint16_t>(button)) != 0;
}

constexpr bool player_command_less(const PlayerCommand& a, const PlayerCommand& b) {
    if (a.entity != b.entity) {
        return entity_less(a.entity, b.entity);
    }
    if (a.command.tick != b.command.tick) {
        return a.command.tick < b.command.tick;
    }
    if (a.command.sequence != b.command.sequence) {
        return a.command.sequence < b.command.sequence;
    }
    if (a.command.buttons != b.command.buttons) {
        return a.command.buttons < b.command.buttons;
    }
    if (a.command.aim_x != b.command.aim_x) {
        return a.command.aim_x < b.command.aim_x;
    }
    return a.command.aim_y < b.command.aim_y;
}

} // namespace nsync::core
