#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace nsync::shared {
class Config;
}

namespace nsync::client {

enum class GameAction : std::uint8_t {
    MoveLeft,
    MoveRight,
    Jump,
    Down,
    Reload,
    WeaponSwitch,
    Gadget1,
    Gadget2,
    Gadget3,
    Scoreboard,
    Pause,
    Count,
};

inline constexpr std::size_t kGameActionCount =
    static_cast<std::size_t>(GameAction::Count);

struct KeyBindings {
    std::array<int, kGameActionCount> scancodes{};

    [[nodiscard]] int get(GameAction action) const {
        const std::size_t index = static_cast<std::size_t>(action);
        return index < scancodes.size() ? scancodes[index] : 0;
    }

    void set(GameAction action, int scancode) {
        const std::size_t index = static_cast<std::size_t>(action);
        if (index < scancodes.size()) {
            scancodes[index] = scancode;
        }
    }

    void reset_defaults();
};

struct DisplayResolution {
    int width = 0;
    int height = 0;

    bool operator==(const DisplayResolution&) const = default;
};

[[nodiscard]] KeyBindings default_key_bindings();
[[nodiscard]] bool parse_game_action(
    std::string_view id,
    GameAction& action);
[[nodiscard]] const char* game_action_id(GameAction action);
[[nodiscard]] const char* game_action_label(GameAction action);
[[nodiscard]] std::string key_name(int scancode);
[[nodiscard]] std::string resolution_text(const DisplayResolution& resolution);

struct ClientSettings {
    int width = 1280;
    int height = 720;
    bool fullscreen = false;
    bool vsync = true;
    bool blood_enabled = true;
    bool audio_enabled = true;
    int master_volume = 80;
    std::string language = "zh";
    KeyBindings bindings = default_key_bindings();
};

void load_client_settings(
    const shared::Config& config,
    ClientSettings& settings);
void save_client_settings(
    shared::Config& config,
    const ClientSettings& settings);

} // namespace nsync::client
