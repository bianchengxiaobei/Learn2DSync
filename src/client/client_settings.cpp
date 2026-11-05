#include "client/client_settings.h"

#include "client/localization.h"
#include "shared/config.h"

#include <SDL3/SDL.h>

#include <array>
#include <string>

namespace nsync::client {
namespace {

constexpr std::array<const char*, kGameActionCount> kActionIds = {
    "left",
    "right",
    "jump",
    "down",
    "reload",
    "weapon",
    "gadget1",
    "gadget2",
    "gadget3",
    "scoreboard",
    "pause",
};

constexpr std::array<const char*, kGameActionCount> kActionLabels = {
    "MOVE LEFT",
    "MOVE RIGHT",
    "JUMP",
    "DROP DOWN",
    "RELOAD",
    "SWITCH WEAPON",
    "SKILL 1",
    "SKILL 2",
    "SKILL 3",
    "SCOREBOARD",
    "PAUSE",
};

constexpr std::array<int, kGameActionCount> kDefaultScancodes = {
    SDL_SCANCODE_A,
    SDL_SCANCODE_D,
    SDL_SCANCODE_W,
    SDL_SCANCODE_S,
    SDL_SCANCODE_R,
    SDL_SCANCODE_Q,
    SDL_SCANCODE_E,
    SDL_SCANCODE_F,
    SDL_SCANCODE_G,
    SDL_SCANCODE_TAB,
    SDL_SCANCODE_ESCAPE,
};

} // namespace

void KeyBindings::reset_defaults() {
    scancodes = kDefaultScancodes;
}

KeyBindings default_key_bindings() {
    KeyBindings bindings;
    bindings.reset_defaults();
    return bindings;
}

bool parse_game_action(std::string_view id, GameAction& action) {
    for (std::size_t index = 0; index < kActionIds.size(); ++index) {
        if (id == kActionIds[index]) {
            action = static_cast<GameAction>(index);
            return true;
        }
    }
    return false;
}

const char* game_action_id(GameAction action) {
    const std::size_t index = static_cast<std::size_t>(action);
    return index < kActionIds.size() ? kActionIds[index] : "";
}

const char* game_action_label(GameAction action) {
    const std::size_t index = static_cast<std::size_t>(action);
    return index < kActionLabels.size() ? kActionLabels[index] : "";
}

std::string key_name(int scancode) {
    if (scancode <= SDL_SCANCODE_UNKNOWN ||
        scancode >= SDL_SCANCODE_COUNT) {
        return "UNBOUND";
    }
    const char* name = SDL_GetScancodeName(
        static_cast<SDL_Scancode>(scancode));
    if (name == nullptr || *name == '\0') {
        return "UNBOUND";
    }
    return name;
}

std::string resolution_text(const DisplayResolution& resolution) {
    return std::to_string(resolution.width) + " x " +
        std::to_string(resolution.height);
}

void load_client_settings(
    const shared::Config& config,
    ClientSettings& settings) {
    settings.width = config.get_int("client.width", 1280, 320, 7680);
    settings.height = config.get_int("client.height", 720, 240, 4320);
    settings.fullscreen = config.get_bool("client.fullscreen", false);
    settings.vsync = config.get_bool("client.vsync", true);
    settings.blood_enabled = config.get_bool("client.blood", true);
    settings.audio_enabled = config.get_bool("audio.enabled", true);
    settings.master_volume = config.get_int("audio.volume", 80, 0, 100);
    settings.language = language_code(parse_language(
        config.get_string("client.language", "zh")));

    settings.bindings.reset_defaults();
    for (std::size_t index = 0; index < kGameActionCount; ++index) {
        const GameAction action = static_cast<GameAction>(index);
        const std::string key = "input." + std::string(kActionIds[index]);
        std::string name =
            config.get_string(key, std::string());
        if (name.empty() && action == GameAction::Gadget1) {
            name = config.get_string("input.ability", std::string());
        }
        if (name.empty()) {
            continue;
        }
        const SDL_Scancode scancode =
            SDL_GetScancodeFromName(name.c_str());
        if (scancode != SDL_SCANCODE_UNKNOWN) {
            settings.bindings.set(action, static_cast<int>(scancode));
        }
    }
}

void save_client_settings(
    shared::Config& config,
    const ClientSettings& settings) {
    config.set("client.width", std::to_string(settings.width));
    config.set("client.height", std::to_string(settings.height));
    config.set(
        "client.fullscreen",
        settings.fullscreen ? "true" : "false");
    config.set("client.vsync", settings.vsync ? "true" : "false");
    config.set(
        "client.blood",
        settings.blood_enabled ? "true" : "false");
    config.set(
        "audio.enabled",
        settings.audio_enabled ? "true" : "false");
    config.set("audio.volume", std::to_string(settings.master_volume));
    config.set("client.language", language_code(parse_language(settings.language)));

    for (std::size_t index = 0; index < kGameActionCount; ++index) {
        const GameAction action = static_cast<GameAction>(index);
        config.set(
            "input." + std::string(kActionIds[index]),
            key_name(settings.bindings.get(action)));
    }
}

} // namespace nsync::client
