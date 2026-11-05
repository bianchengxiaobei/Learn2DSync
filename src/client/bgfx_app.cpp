#include "client/bgfx_app.h"

#include "client/audio.h"
#include "client/bgfx_ui.h"
#include "client/game_ui.h"
#include "client/localization.h"
#include "client/rml_resource_file.h"
#include "client/world_renderer.h"
#include "core/aim.h"
#include "core/fixed_step.h"
#include "core/gadgets.h"
#include "core/heroes.h"
#include "core/map.h"
#include "core/weapons.h"
#include "core/world.h"
#include "net/local_net.h"
#include "net/real_client.h"
#include "shared/asset_watcher.h"
#include "shared/config.h"
#include "shared/log.h"
#include "shared/paths.h"
#include "shared/resource.h"

#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <RmlUi/Core.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef near
#undef near
#endif
#ifdef far
#undef far
#endif
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nsync::client {
namespace {

namespace fs = std::filesystem;

std::vector<char> read_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return {};
    }
    const std::streamsize size = file.tellg();
    if (size <= 0) {
        return {};
    }
    std::vector<char> data(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!file.read(data.data(), size)) {
        return {};
    }
    return data;
}

void show_startup_error(
    std::string_view title,
    std::string_view message) {
#ifdef _WIN32
    const std::string title_text(title);
    const std::string message_text(message);
    MessageBoxA(
        nullptr,
        message_text.c_str(),
        title_text.c_str(),
        MB_OK | MB_ICONERROR);
#else
    std::fprintf(
        stderr,
        "%s: %s\n",
        std::string(title).c_str(),
        std::string(message).c_str());
#endif
}

bgfx::ProgramHandle load_program(const fs::path& shader_root) {
    const std::vector<char> vs_data = read_file(shader_root / "vs_dx11.bin");
    const std::vector<char> fs_data = read_file(shader_root / "fs_dx11.bin");
    if (vs_data.empty() || fs_data.empty()) {
        NSYNC_LOG_ERROR(
            "client",
            "failed to load bgfx shaders from ",
            shader_root.string());
        return BGFX_INVALID_HANDLE;
    }

    const bgfx::ShaderHandle vertex_shader = bgfx::createShader(
        bgfx::copy(vs_data.data(), static_cast<std::uint32_t>(vs_data.size())));
    const bgfx::ShaderHandle fragment_shader = bgfx::createShader(
        bgfx::copy(fs_data.data(), static_cast<std::uint32_t>(fs_data.size())));

    if (!bgfx::isValid(vertex_shader) || !bgfx::isValid(fragment_shader)) {
        if (bgfx::isValid(vertex_shader)) {
            bgfx::destroy(vertex_shader);
        }
        if (bgfx::isValid(fragment_shader)) {
            bgfx::destroy(fragment_shader);
        }
        NSYNC_LOG_ERROR("client", "failed to create bgfx shaders");
        return BGFX_INVALID_HANDLE;
    }

    return bgfx::createProgram(vertex_shader, fragment_shader, true);
}

std::uint32_t client_now_ms() {
    using namespace std::chrono;
    static const steady_clock::time_point start = steady_clock::now();
    return static_cast<std::uint32_t>(
        duration_cast<milliseconds>(steady_clock::now() - start).count());
}
float fix_to_float(core::Fix32 value) {
    return static_cast<float>(value.raw()) /
           static_cast<float>(core::Fix32::kOne);
}

core::Fix32 float_to_fix(float value) {
    return core::Fix32::from_raw(static_cast<std::int32_t>(
        std::lround(value * static_cast<float>(core::Fix32::kOne))));
}

std::int16_t quantize_aim(float value) {
    const float scaled = std::clamp(value, -1.0f, 1.0f) * 32767.0f;
    return static_cast<std::int16_t>(std::lround(scaled));
}

void add_resolution(
    std::vector<DisplayResolution>& resolutions,
    int width,
    int height) {
    if (width < 320 || height < 240) {
        return;
    }
    const DisplayResolution candidate{width, height};
    if (std::find(
            resolutions.begin(),
            resolutions.end(),
            candidate) == resolutions.end()) {
        resolutions.push_back(candidate);
    }
}

std::vector<DisplayResolution> display_resolutions(
    SDL_Window* window,
    int current_width,
    int current_height) {
    std::vector<DisplayResolution> resolutions;
    const SDL_DisplayID display = window != nullptr
        ? SDL_GetDisplayForWindow(window)
        : SDL_GetPrimaryDisplay();
    int mode_count = 0;
    SDL_DisplayMode** modes =
        SDL_GetFullscreenDisplayModes(display, &mode_count);
    if (modes != nullptr) {
        for (int index = 0; index < mode_count; ++index) {
            const SDL_DisplayMode* mode = modes[index];
            if (mode == nullptr) {
                continue;
            }
            if (mode->w < 960 || mode->h < 600) {
                continue;
            }
            add_resolution(resolutions, mode->w, mode->h);
        }
        SDL_free(modes);
    }
    add_resolution(resolutions, current_width, current_height);
    if (resolutions.empty()) {
        add_resolution(resolutions, 1280, 720);
    }
    std::sort(
        resolutions.begin(),
        resolutions.end(),
        [](const DisplayResolution& left, const DisplayResolution& right) {
            const int left_area = left.width * left.height;
            const int right_area = right.width * right.height;
            if (left_area != right_area) {
                return left_area < right_area;
            }
            return left.width < right.width;
        });
    return resolutions;
}

int resolution_index(
    const std::vector<DisplayResolution>& resolutions,
    int width,
    int height) {
    for (std::size_t index = 0; index < resolutions.size(); ++index) {
        if (resolutions[index].width == width &&
            resolutions[index].height == height) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void ensure_resolution(
    std::vector<DisplayResolution>& resolutions,
    int width,
    int height) {
    if (resolution_index(resolutions, width, height) >= 0) {
        return;
    }
    add_resolution(resolutions, width, height);
    std::sort(
        resolutions.begin(),
        resolutions.end(),
        [](const DisplayResolution& left, const DisplayResolution& right) {
            const int left_area = left.width * left.height;
            const int right_area = right.width * right.height;
            if (left_area != right_area) {
                return left_area < right_area;
            }
            return left.width < right.width;
        });
}

std::string control_hint(const KeyBindings& bindings, bool chinese) {
    const std::string move = key_name(bindings.get(GameAction::MoveLeft)) +
        "/" + key_name(bindings.get(GameAction::MoveRight));
    const std::string jump = key_name(bindings.get(GameAction::Jump));
    const std::string reload = key_name(bindings.get(GameAction::Reload));
    const std::string weapon = key_name(bindings.get(GameAction::WeaponSwitch));
    const std::string skills =
        key_name(bindings.get(GameAction::Gadget1)) + "/" +
        key_name(bindings.get(GameAction::Gadget2)) + "/" +
        key_name(bindings.get(GameAction::Gadget3));
    const std::string score = key_name(bindings.get(GameAction::Scoreboard));
    const std::string pause = key_name(bindings.get(GameAction::Pause));
    if (chinese) {
        return move + " 移动  " + jump + " 跳跃  LMB 开火  " +
            reload + " 换弹  " + weapon + " 武器  " +
            skills + " 技能  " + score + " 计分  " + pause + " 暂停";
    }
    return move + " MOVE  " + jump + " JUMP  LMB FIRE  " +
        reload + " RELOAD  " + weapon + " WEAPON  " +
        skills + " SKILLS  " + score + " SCORE  " + pause + " PAUSE";
}

core::InputCommand make_net_bot_command(int slot, std::uint32_t tick) {
    core::InputCommand command;
    command.tick = tick;
    command.sequence = static_cast<std::uint8_t>(tick & 0xFFU);

    const bool right = ((tick / 75U) + static_cast<std::uint32_t>(slot)) % 2U == 0U;
    command.buttons |= right ? core::kButtonRight : core::kButtonLeft;
    if ((tick % 120U) == 0U) {
        command.buttons |= core::kButtonJump;
    }
    if ((tick % 4U) != 3U) {
        command.buttons |= core::kButtonFire;
    }
    if ((tick % 180U) == 0U) {
        command.buttons |= core::kButtonReload;
    }
    command.aim_x = slot % 2 == 0 ? 32767 : -32767;
    command.aim_y = 0;
    return command;
}

net::InputProvider make_net_bot_provider(int slot) {
    return [slot](std::uint32_t target_tick) {
        return make_net_bot_command(slot, target_tick);
    };
}

void configure_views(int width, int height) {
    bgfx::setViewClear(
        0,
        BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
        0x101820ff,
        1.0f,
        0);
    bgfx::setViewClear(1, BGFX_CLEAR_NONE, 0, 1.0f, 0);
    bgfx::setViewRect(
        0,
        0,
        0,
        static_cast<std::uint16_t>(width),
        static_cast<std::uint16_t>(height));
    bgfx::setViewRect(
        1,
        0,
        0,
        static_cast<std::uint16_t>(width),
        static_cast<std::uint16_t>(height));
    bgfx::setViewMode(1, bgfx::ViewMode::Sequential);

    float view[16];
    float projection[16];
    bx::mtxIdentity(view);
    bx::mtxOrtho(
        projection,
        0.0f,
        static_cast<float>(width),
        static_cast<float>(height),
        0.0f,
        0.0f,
        100.0f,
        0.0f,
        bgfx::getCaps()->homogeneousDepth);
    bgfx::setViewTransform(0, view, projection);
    bgfx::setViewTransform(1, view, projection);
}

} // namespace

int run_client(const ClientOptions& options) {
    shared::ResourceManager resources;
    std::string resource_error;
    bool mounted = false;
    std::optional<fs::path> mounted_directory;
    std::optional<fs::path> dev_asset_root;

    const auto try_directory = [&](const fs::path& directory) {
        resource_error.clear();
        if (resources.mount_directory("assets", directory, resource_error)) {
            NSYNC_LOG_INFO("client", "mounted assets directory: ", directory.string());
            mounted = true;
            mounted_directory = directory;
            return true;
        }
        NSYNC_LOG_WARN("client", resource_error);
        return false;
    };
    const auto try_pak = [&](const fs::path& pak) {
        resource_error.clear();
        if (resources.mount_pak("assets.pak", pak, resource_error)) {
            NSYNC_LOG_INFO("client", "mounted assets.pak: ", pak.string());
            mounted = true;
            return true;
        }
        NSYNC_LOG_WARN("client", resource_error);
        return false;
    };

    const fs::path executable_directory = shared::paths::executable_dir();
    std::error_code fs_error;
    if (!executable_directory.empty() &&
        fs::is_directory(executable_directory / "assets", fs_error)) {
        try_directory(executable_directory / "assets");
    } else if (!executable_directory.empty() &&
               fs::is_regular_file(executable_directory / "assets.pak", fs_error)) {
        try_pak(executable_directory / "assets.pak");
    }
    if (!mounted) {
        if (const auto directory = shared::paths::find_directory("assets")) {
            try_directory(*directory);
        }
    }
    if (!mounted) {
        if (const auto pak = shared::paths::find_file("assets.pak")) {
            try_pak(*pak);
        }
    }
    if (!mounted) {
        NSYNC_LOG_ERROR("client", "no assets directory or assets.pak found");
        show_startup_error(
            "NSYNC - Startup Error",
            "No assets directory or assets.pak was found.\n"
            "Keep assets next to the client executable or run from the source tree.");
        return 1;
    }

    if (options.hot_reload) {
        if (const auto source_assets = shared::paths::source_asset_root();
            source_assets.has_value() && try_directory(*source_assets)) {
            dev_asset_root = source_assets;
            NSYNC_LOG_INFO(
                "client",
                "hot reload watches source assets: ",
                source_assets->string());
        } else if (mounted_directory.has_value()) {
            dev_asset_root = mounted_directory;
            NSYNC_LOG_INFO(
                "client",
                "hot reload watches mounted assets: ",
                mounted_directory->string());
        } else {
            NSYNC_LOG_WARN(
                "client",
                "hot reload requires a writable assets directory; "
                "assets.pak is not watched");
        }
    }

    if (resources.exists("data/content.json")) {
        const auto content_text = resources.read_text("data/content.json");
        std::string content_error;
        if (!content_text ||
            !core::content().load_json(*content_text, &content_error)) {
            NSYNC_LOG_ERROR(
                "client",
                "failed to load content data: ",
                content_error);
            show_startup_error(
                "NSYNC - Content Error",
                "Failed to load data/content.json:\n" + content_error);
            return 1;
        }
        NSYNC_LOG_INFO(
            "client",
            "loaded content: heroes=",
            core::content().hero_count(),
            " weapons=",
            core::content().weapon_count(),
            " fingerprint=",
            core::content().fingerprint());
    } else {
        NSYNC_LOG_WARN(
            "client",
            "data/content.json missing; using built-in content");
    }

    std::string selected_hero_id = options.hero;
    const core::HeroStats* selected_hero =
        core::content().find_hero(selected_hero_id);
    if (selected_hero == nullptr) {
        NSYNC_LOG_ERROR("client", "unknown hero: ", options.hero);
        show_startup_error(
            "NSYNC - Content Error",
            "Unknown hero id: " + options.hero);
        return 1;
    }

    const auto load_map_from_resources =
        [&](const std::string& virtual_path,
            core::Map& target,
            std::string& error) -> bool {
        std::string extension = fs::path(virtual_path).extension().string();
        for (char& character : extension) {
            character = static_cast<char>(
                std::tolower(static_cast<unsigned char>(character)));
        }
        if (extension == ".bin") {
            const auto map_binary = resources.read(virtual_path);
            if (!map_binary) {
                error = "failed to read map: " + virtual_path;
                return false;
            }
            return core::decode_map_bin(
                map_binary->data(),
                map_binary->size(),
                target,
                &error);
        }

        const auto map_text = resources.read_text(virtual_path);
        if (!map_text) {
            error = "failed to read map: " + virtual_path;
            return false;
        }
        return core::load_map_json(*map_text, target, &error);
    };

    std::string map_path = options.map;
    core::Map map;
    std::string map_error;
    if (!load_map_from_resources(map_path, map, map_error)) {
        NSYNC_LOG_ERROR(
            "client",
            "failed to load map ",
            map_path,
            ": ",
            map_error);
        show_startup_error(
            "NSYNC - Map Error",
            "Failed to load map " + map_path + ":\n" + map_error);
        return 1;
    }

    core::GameMode active_game_mode = options.game_mode;
    if (!options.connect &&
        active_game_mode == core::GameMode::Control &&
        !map.objective.has_value()) {
        NSYNC_LOG_ERROR(
            "client",
            "control mode requires a map objective");
        show_startup_error(
            "NSYNC - Map Error",
            "CONTROL mode requires a map objective.");
        return 1;
    }
    if (!options.connect &&
        active_game_mode == core::GameMode::Crystal &&
        (map.drop_depots.empty() || map.drop_spawns.empty())) {
        NSYNC_LOG_ERROR(
            "client",
            "crystal mode requires drop spawns and depots");
        show_startup_error(
            "NSYNC - Map Error",
            "CRYSTAL mode requires crystal spawns and depots.");
        return 1;
    }
    NSYNC_LOG_INFO(
        "client",
        "mode=",
        active_game_mode == core::GameMode::Control
            ? "control"
            : (active_game_mode == core::GameMode::Crystal
                   ? "crystal"
                   : "tdm"),
        " drops=",
        map.drop_spawns.size(),
        " depots=",
        map.drop_depots.size());

    struct HumanInputState {
        std::uint16_t buttons = 0;
        std::int16_t aim_x = 32767;
        std::int16_t aim_y = 0;
    } human_state;
    bool window_focused = true;
    bool mouse_fire_blocked = false;

    const bool online_mode = options.connect;
    const bool net_mode = options.net && !online_mode;
    const bool automation_run =
        options.max_frames > 0 || options.max_ticks > 0;
    const bool menu_mode =
        !online_mode && !net_mode && options.show_menu &&
        (!automation_run || options.menu_explicit);
    core::World direct_world;
    net::LocalSession net_session;
    net::RealClient online_client;
    if (online_mode) {
        net::NetClientConfig online_config;
        online_config.input_delay_ticks =
            static_cast<std::uint32_t>(options.input_delay_ticks);
        online_config.auto_ready = options.auto_ready;
        online_config.auto_rematch = options.auto_rematch;
        online_config.client_step_lag = options.client_lag;
        online_config.fixed_step_scheduler = options.fixed_step_scheduler;
        online_config.display_name = options.player_name;
        online_config.login_token = options.player_name;
        online_config.desired_hero = static_cast<std::uint8_t>(selected_hero->type);
        std::string online_error;
        if (!online_client.start(
                options.server_host,
                static_cast<std::uint16_t>(options.server_port),
                map,
                online_config,
                client_now_ms(),
                &online_error)) {
            NSYNC_LOG_ERROR(
                "client",
                "failed to connect to server: ",
                online_error);
            show_startup_error(
                "NSYNC - Connection Error",
                "Failed to connect to " + options.server_host + ":" +
                    std::to_string(options.server_port) + ":\n" +
                    online_error);
            return 1;
        }
        if (!online_client.wait_connected(5000, &online_error)) {
            NSYNC_LOG_ERROR(
                "client",
                "server login failed: ",
                online_error);
            show_startup_error(
                "NSYNC - Connection Error",
                "Server login failed:\n" + online_error);
            return 1;
        }
        if (online_client.map().id != map.id) {
            map = online_client.map();
            map_path = "maps/" + map.id + ".json";
            NSYNC_LOG_INFO(
                "client",
                "server selected map: ",
                map.id);
        }
        active_game_mode = online_client.world().game_mode();
        NSYNC_LOG_INFO(
            "client",
            "connected to ",
            options.server_host,
            ":",
            options.server_port,
            " slot=",
            static_cast<int>(online_client.slot()),
            " name=",
            options.player_name,
            " map=",
            map.id,
            " phase=",
            static_cast<int>(online_client.stats().phase));
    } else if (net_mode) {
        net::NetConfig net_config;
        net_config.latency_ticks = static_cast<std::uint32_t>(std::lround(
            static_cast<double>(options.net_latency_ms) * 60.0 / 2000.0));
        net_config.jitter_ticks = static_cast<std::uint32_t>(std::lround(
            static_cast<double>(options.net_jitter_ms) * 60.0 / 1000.0));
        net_config.loss_percent =
            static_cast<std::uint32_t>(options.net_loss_percent);
        net_config.duplicate_percent =
            static_cast<std::uint32_t>(options.net_duplicate_percent);
        net_config.input_delay_ticks = net_config.latency_ticks * 2U +
            net_config.jitter_ticks * 2U + 2U;
        net_config.seed = 1;

        std::vector<net::InputProvider> providers;
        providers.reserve(4);
        providers.push_back([&human_state](std::uint32_t target_tick) {
            core::InputCommand command;
            command.tick = target_tick;
            command.buttons = human_state.buttons;
            command.aim_x = human_state.aim_x;
            command.aim_y = human_state.aim_y;
            return command;
        });
        for (int slot = 1; slot < 4; ++slot) {
            providers.push_back(make_net_bot_provider(slot));
        }

        std::string net_error;
        if (!net_session.start(
                map,
                1,
                4,
                4,
                std::move(providers),
                net_config,
                5U * 60U * 60U,
                30,
                &net_error)) {
            NSYNC_LOG_ERROR(
                "client",
                "failed to start local network session: ",
                net_error);
            show_startup_error(
                "NSYNC - Network Error",
                "Failed to start the local network session:\n" +
                    net_error);
            return 1;
        }
        NSYNC_LOG_INFO(
            "client",
            "local network session: rtt=",
            options.net_latency_ms,
            "ms jitter=",
            options.net_jitter_ms,
            "ms loss=",
            options.net_loss_percent,
            "% duplicate=",
            options.net_duplicate_percent,
            "% input_delay=",
            net_config.input_delay_ticks);
    } else {
        direct_world.set_map(map);
        direct_world.set_game_mode(active_game_mode);
        direct_world.reset(1, 4);
        direct_world.set_bot_slots(0x0E);
    }

    core::World& world = online_mode
        ? online_client.world()
        : (net_mode ? net_session.client(0).world() : direct_world);

    core::EntityId local_player;
    const std::uint8_t local_slot = online_mode
        ? online_client.slot()
        : 0U;
    world.players().for_each([&](const core::Player& player) {
        if (player.slot == local_slot) {
            local_player = player.id;
        }
    });
    if (!local_player.valid()) {
        NSYNC_LOG_ERROR("client", "no local player in world");
        show_startup_error(
            "NSYNC - Session Error",
            "No local player exists in the world.");
        return 1;
    }
    if (!online_mode && !net_mode) {
        if (core::Player* local = world.players().get(local_player)) {
            local->hero = static_cast<std::uint8_t>(selected_hero->type);
            local->health = core::Fix32::from_int(selected_hero->health);
            local->shield = selected_hero->shield;
            local->gadget_cooldowns.fill(0);
            local->gadget_active_ticks.fill(0);
            local->invulnerable_ticks = 0;
            local->heal_ticks = 0;
            local->slow_ticks = 0;
            local->stun_ticks = 0;
            local->gadget_latch = 0;
            local->shield_regen_delay = 0;
        }
        world.players().for_each([](core::Player& player) {
            core::equip_hero_default_weapon(player);
        });
    }
    const auto refresh_local_player = [&] {
        const std::uint8_t slot = online_mode
            ? online_client.slot()
            : local_slot;
        world.players().for_each([&](const core::Player& player) {
            if (player.slot == slot) {
                local_player = player.id;
            }
        });
    };
    NSYNC_LOG_INFO(
        "client",
        "loaded map ",
        map.id,
        " with ",
        map.solids.size(),
        " solids, ",
        map.one_way_platforms.size(),
        " platforms");

    RmlResourceFile file_interface(resources);

    NSYNC_LOG_INFO(
        "client",
        "starting: ",
        options.width,
        "x",
        options.height,
        " tick_rate=",
        options.tick_rate);

    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        NSYNC_LOG_ERROR("client", "SDL_Init failed: ", SDL_GetError());
        show_startup_error(
            "NSYNC - Startup Error",
            std::string("SDL initialization failed:\n") + SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "NSYNC Client",
        options.width,
        options.height,
        SDL_WINDOW_RESIZABLE |
            (options.fullscreen ? SDL_WINDOW_FULLSCREEN : 0));
    if (window == nullptr) {
        NSYNC_LOG_ERROR("client", "SDL_CreateWindow failed: ", SDL_GetError());
        show_startup_error(
            "NSYNC - Startup Error",
            std::string("Could not create the game window:\n") +
                SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowMinimumSize(window, 960, 600);

    SDL_PropertiesID window_properties = SDL_GetWindowProperties(window);
    void* native_window = SDL_GetPointerProperty(
        window_properties,
        SDL_PROP_WINDOW_WIN32_HWND_POINTER,
        nullptr);
    if (native_window == nullptr) {
        NSYNC_LOG_ERROR("client", "failed to get Win32 HWND from SDL3");
        show_startup_error(
            "NSYNC - Startup Error",
            "Could not obtain the Win32 window handle from SDL3.");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    int width = options.width;
    int height = options.height;
    SDL_GetWindowSize(window, &width, &height);

    bgfx::Init init;
    init.type = bgfx::RendererType::Direct3D11;
    if (options.gpu_vendor == "intel") {
        init.vendorId = BGFX_PCI_ID_INTEL;
    } else if (options.gpu_vendor == "nvidia") {
        init.vendorId = BGFX_PCI_ID_NVIDIA;
    } else if (options.gpu_vendor == "amd") {
        init.vendorId = BGFX_PCI_ID_AMD;
    } else if (options.gpu_vendor == "microsoft") {
        init.vendorId = BGFX_PCI_ID_MICROSOFT;
    } else if (options.gpu_vendor == "software") {
        init.vendorId = BGFX_PCI_ID_SOFTWARE_RASTERIZER;
    }
    init.platformData.nwh = native_window;
    init.resolution.width = static_cast<std::uint32_t>(width);
    init.resolution.height = static_cast<std::uint32_t>(height);
    init.resolution.reset = options.vsync ? BGFX_RESET_VSYNC : BGFX_RESET_NONE;
    init.resolution.numBackBuffers = 2;
    init.resolution.maxFrameLatency = 1;
    if (!bgfx::init(init)) {
        NSYNC_LOG_ERROR("client", "bgfx::init failed");
        show_startup_error(
            "NSYNC - Renderer Error",
            "bgfx failed to initialize Direct3D 11.");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (const bgfx::Caps* caps = bgfx::getCaps()) {
        NSYNC_LOG_INFO(
            "client",
            "renderer: type=",
            bgfx::getRendererName(caps->rendererType),
            " vendor=0x",
            std::hex,
            caps->vendorId,
            " device=0x",
            caps->deviceId,
            std::dec);
    }

    const auto shader_root = shared::paths::find_shader_root();
    if (!shader_root) {
        NSYNC_LOG_ERROR("client", "could not find shaders directory");
    }

    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    if (shader_root) {
        program = load_program(*shader_root);
    }
    const bgfx::UniformHandle sampler =
        bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(program)) {
        show_startup_error(
            "NSYNC - Renderer Error",
            "Could not load the bgfx shaders. Check the shaders directory.");
        if (bgfx::isValid(sampler)) {
            bgfx::destroy(sampler);
        }
        bgfx::shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    WorldRenderer world_renderer;
    HeroArtMode hero_art_mode = HeroArtMode::Auto;
    if (!parse_hero_art_mode(options.hero_art, hero_art_mode)) {
        NSYNC_LOG_WARN(
            "client",
            "unknown hero art mode: ",
            options.hero_art,
            "; using auto");
        hero_art_mode = HeroArtMode::Auto;
    }
    world_renderer.set_hero_art_mode(hero_art_mode);
    if (!world_renderer.initialize(resources)) {
        NSYNC_LOG_ERROR("client", "failed to initialize world renderer");
        show_startup_error(
            "NSYNC - Renderer Error",
            "Failed to initialize the world renderer.");
        bgfx::destroy(sampler);
        bgfx::destroy(program);
        bgfx::shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    configure_views(width, height);
    world_renderer.set_blood_enabled(options.blood_enabled);
    world_renderer.load_map_art(resources, map.id);

    if (options.benchmark_heroes) {
        NSYNC_LOG_INFO(
            "client",
            "running original hero benchmark: players=10 frames=600");
        std::string benchmark_error;
        const bool benchmark_ok =
            world_renderer.run_original_hero_benchmark(
                10,
                600,
                &benchmark_error);
        if (!benchmark_ok) {
            NSYNC_LOG_ERROR(
                "client",
                "hero render benchmark failed: ",
                benchmark_error);
        }
        world_renderer.shutdown();
        bgfx::destroy(sampler);
        bgfx::destroy(program);
        bgfx::shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return benchmark_ok ? 0 : 1;
    }

    BgfxUiRenderer ui_renderer(program, sampler, width, height);
    Rml::SetRenderInterface(&ui_renderer);
    Rml::SetFileInterface(&file_interface);
    if (!Rml::Initialise()) {
        NSYNC_LOG_ERROR("client", "Rml::Initialise failed");
        show_startup_error(
            "NSYNC - UI Error",
            "RmlUi failed to initialize.");
        ui_renderer.shutdown();
        world_renderer.shutdown();
        bgfx::destroy(sampler);
        bgfx::destroy(program);
        bgfx::shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (const auto font_path = shared::paths::find_system_font()) {
        if (!Rml::LoadFontFace(
                font_path->string(),
                "SpikeFont",
                Rml::Style::FontStyle::Normal,
                Rml::Style::FontWeight::Normal)) {
            NSYNC_LOG_WARN("client", "failed to load font: ", font_path->string());
        }
    } else {
        NSYNC_LOG_WARN("client", "no suitable system font found");
    }
    if (const auto cjk_path = shared::paths::find_cjk_font()) {
        if (!Rml::LoadFontFace(
                cjk_path->string(),
                "CjkFont",
                Rml::Style::FontStyle::Normal,
                Rml::Style::FontWeight::Normal)) {
            NSYNC_LOG_WARN(
                "client",
                "failed to load CJK font: ",
                cjk_path->string());
        } else {
            NSYNC_LOG_INFO(
                "client",
                "loaded CJK font: ",
                cjk_path->string());
        }
    } else {
        NSYNC_LOG_WARN("client", "no CJK font found for Chinese UI");
    }

    Rml::Context* context = Rml::CreateContext(
        "client",
        Rml::Vector2i(width, height));
    if (context == nullptr) {
        NSYNC_LOG_ERROR("client", "Rml::CreateContext failed");
        show_startup_error(
            "NSYNC - UI Error",
            "RmlUi could not create the UI context.");
        Rml::Shutdown();
        ui_renderer.shutdown();
        world_renderer.shutdown();
        bgfx::destroy(sampler);
        bgfx::destroy(program);
        bgfx::shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    const auto make_ui_heroes = [&](const std::string& language) {
        std::vector<UiHero> heroes;
        heroes.reserve(core::content().hero_count());
        for (std::size_t index = 0; index < core::content().hero_count(); ++index) {
            const core::HeroStats& hero = core::content().hero(index);
            UiHero entry;
            entry.id = hero.id;
            entry.name = hero.id;
            if (language == "zh") {
                if (hero.id == "scout") {
                    entry.name = "侦察兵";
                } else if (hero.id == "gunner") {
                    entry.name = "机枪兵";
                } else if (hero.id == "assault") {
                    entry.name = "突击兵";
                } else if (hero.id == "grenadier") {
                    entry.name = "榴弹兵";
                } else if (hero.id == "cyborg") {
                    entry.name = "改造兵";
                } else if (hero.id == "hermit") {
                    entry.name = "隐士";
                } else if (hero.id == "predator") {
                    entry.name = "掠食者";
                } else if (hero.id == "pyromancer") {
                    entry.name = "纵火者";
                } else if (hero.id == "soldier") {
                    entry.name = "士兵";
                } else if (hero.id == "warrioress") {
                    entry.name = "女战士";
                }
            }
            entry.health = static_cast<int>(hero.health);
            entry.shield = static_cast<int>(hero.shield);
            entry.speed_percent = static_cast<int>(
                hero.run_speed.raw() * 100 / core::Fix32::kOne);
            for (std::size_t slot = 0; slot < core::kGadgetSlots; ++slot) {
                const core::GadgetStats& gadget = core::hero_gadget_stats(
                    static_cast<core::HeroType>(index),
                    slot);
                if (!entry.ability_name.empty()) {
                    entry.ability_name += " / ";
                }
                entry.ability_name += language == "zh"
                    ? std::string(gadget.name_zh)
                    : std::string(gadget.name_en);
            }
            heroes.push_back(std::move(entry));
        }
        return heroes;
    };

    std::string pending_ui_action;
    std::string pending_ui_value;
    GameUi ui;
    if (!ui.initialize(
            *context,
            make_ui_heroes(options.language),
            [&](const std::string& action, const std::string& value) {
                pending_ui_action = action;
                pending_ui_value = value;
            })) {
        NSYNC_LOG_ERROR("client", "failed to initialize RmlUi game shell");
        show_startup_error(
            "NSYNC - UI Error",
            "Failed to initialize the RmlUi game shell.");
        Rml::RemoveContext("client");
        Rml::Shutdown();
        ui_renderer.shutdown();
        world_renderer.shutdown();
        bgfx::destroy(sampler);
        bgfx::destroy(program);
        bgfx::shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    Localization localization;
    localization.set_language(parse_language(options.language));
    ui.set_language(localization.language());

    UiScreen ui_screen = menu_mode
        ? UiScreen::Menu
        : (online_mode && !online_client.in_match()
               ? UiScreen::Lobby
               : UiScreen::Match);
    ui.set_screen(ui_screen);
    UiScreen rendered_screen = ui_screen;

    ClientSettings settings;
    settings.width = width;
    settings.height = height;
    settings.fullscreen = options.fullscreen;
    settings.vsync = options.vsync;
    settings.blood_enabled = options.blood_enabled;
    settings.audio_enabled = options.audio_enabled;
    settings.master_volume = static_cast<int>(
        std::lround(options.master_volume * 100.0f));
    settings.language = options.language;
    settings.bindings = options.key_bindings;
    ClientSettings draft = settings;
    int settings_capture = -1;
    bool settings_controls_tab = false;
    UiScreen settings_return_screen = UiScreen::Menu;
    std::string settings_status;
    std::vector<DisplayResolution> resolution_options =
        display_resolutions(window, width, height);
    ensure_resolution(
        resolution_options,
        settings.width,
        settings.height);

    const auto apply_surface_size = [&](int new_width, int new_height) {
        width = std::max(new_width, 1);
        height = std::max(new_height, 1);
        bgfx::reset(
            static_cast<std::uint32_t>(width),
            static_cast<std::uint32_t>(height),
            settings.vsync ? BGFX_RESET_VSYNC : BGFX_RESET_NONE);
        context->SetDimensions(Rml::Vector2i(width, height));
        ui_renderer.set_dimensions(width, height);
        ui.set_dimensions(width, height);
        configure_views(width, height);
    };

    if (options.simulate_click && menu_mode) {
        if (Rml::Element* start = ui.document()->GetElementById("menu-start")) {
            start->Click();
            context->Update();
            NSYNC_LOG_INFO("client", "synthetic RmlUi click on menu-start");
        }
    }

    AudioEngine audio;
    if (!audio.initialize(
            options.audio_enabled,
            options.master_volume,
            &resources) &&
        options.audio_enabled) {
        NSYNC_LOG_WARN("client", "continuing without audio");
    }
    if (const core::Player* local = world.players().get(local_player)) {
        audio.set_listener(
            fix_to_float(local->position.x),
            fix_to_float(local->position.y) + 0.5f);
    }

    const auto apply_audio_state = [&](const ClientSettings& value,
                                       bool stop_disabled) {
        if (value.audio_enabled) {
            if (!audio.available()) {
                audio.set_enabled(true);
            }
            audio.set_master_volume(
                static_cast<float>(value.master_volume) / 100.0f);
        } else {
            audio.set_master_volume(0.0f);
            if (stop_disabled) {
                audio.set_enabled(false);
            }
        }
    };

    const auto preview_audio = [&]() {
        if (draft.audio_enabled) {
            if (!audio.available()) {
                audio.set_enabled(true);
            }
            audio.set_master_volume(
                static_cast<float>(draft.master_volume) / 100.0f);
        } else {
            audio.set_master_volume(0.0f);
        }
    };

    const auto apply_display = [&](const ClientSettings& value) {
        bool ok = true;
        if (value.fullscreen) {
            const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
            SDL_DisplayMode mode{};
            if (display != 0 &&
                SDL_GetClosestFullscreenDisplayMode(
                    display,
                    value.width,
                    value.height,
                    0.0f,
                    false,
                    &mode)) {
                ok = SDL_SetWindowFullscreenMode(window, &mode) && ok;
            }
            ok = SDL_SetWindowFullscreen(window, true) && ok;
        } else {
            SDL_SetWindowFullscreen(window, false);
            SDL_SetWindowFullscreenMode(window, nullptr);
            ok = SDL_SetWindowSize(
                     window,
                     value.width,
                     value.height) &&
                ok;
        }
        if (!ok) {
            NSYNC_LOG_WARN(
                "client",
                "display mode request failed: ",
                SDL_GetError());
        }
        int actual_width = value.width;
        int actual_height = value.height;
        SDL_GetWindowSize(window, &actual_width, &actual_height);
        apply_surface_size(actual_width, actual_height);
        settings.width = width;
        settings.height = height;
        ensure_resolution(resolution_options, width, height);
    };

    const auto save_runtime_settings = [&]() -> bool {
        if (options.settings_path.empty()) {
            NSYNC_LOG_WARN(
                "client",
                "settings path is empty; changes are not persisted");
            return false;
        }
        shared::Config stored;
        std::string error;
        std::error_code exists_error;
        if (std::filesystem::exists(
                options.settings_path,
                exists_error) &&
            !stored.load_file(options.settings_path, error)) {
            NSYNC_LOG_WARN(
                "client",
                "settings reload failed: ",
                error);
            return false;
        }
        save_client_settings(stored, settings);
        if (!stored.save_file(options.settings_path, error)) {
            NSYNC_LOG_WARN(
                "client",
                "settings save failed: ",
                error);
            return false;
        }
        NSYNC_LOG_INFO(
            "client",
            "settings saved to ",
            options.settings_path.string());
        return true;
    };

    const auto apply_language = [&](const std::string& code) {
        localization.set_language(parse_language(code));
        ui.set_language(localization.language());
        ui.set_heroes(make_ui_heroes(code));
        ui.set_controls_text(control_hint(
            settings.bindings,
            code == "zh"));
    };

    const auto refresh_settings = [&]() {
        UiSettingsModel model;
        model.controls_tab = settings_controls_tab;
        model.audio_enabled = draft.audio_enabled;
        model.audio_available = audio.available();
        model.master_volume = draft.master_volume;
        model.fullscreen = draft.fullscreen;
        model.vsync = draft.vsync;
        model.blood_enabled = draft.blood_enabled;
        model.language_label =
            language_label(parse_language(draft.language));
        ensure_resolution(
            resolution_options,
            draft.width,
            draft.height);
        model.resolution = resolution_text(
            DisplayResolution{draft.width, draft.height});
        model.status = settings_status;
        for (std::size_t index = 0; index < kGameActionCount; ++index) {
            const GameAction action = static_cast<GameAction>(index);
            UiSettingsBinding binding;
            binding.action = game_action_id(action);
            binding.label = localization.text(
                "action." + std::string(game_action_id(action)));
            binding.key = key_name(draft.bindings.get(action));
            binding.capturing =
                settings_capture == static_cast<int>(index);
            model.bindings.push_back(std::move(binding));
        }
        ui.update_settings(model);
    };

    const auto open_settings = [&]() {
        if (ui_screen == UiScreen::Settings) {
            return;
        }
        settings_return_screen = ui_screen;
        draft = settings;
        apply_language(draft.language);
        settings_capture = -1;
        settings_status.clear();
        ensure_resolution(
            resolution_options,
            draft.width,
            draft.height);
        ui_screen = UiScreen::Settings;
        refresh_settings();
    };

    const auto cancel_settings = [&]() {
        settings_capture = -1;
        settings_status.clear();
        apply_audio_state(settings, true);
        apply_language(settings.language);
        ui_screen = settings_return_screen;
        refresh_settings();
    };

    const auto commit_settings = [&]() {
        settings_capture = -1;
        const bool display_changed =
            draft.fullscreen != settings.fullscreen ||
            draft.width != settings.width ||
            draft.height != settings.height ||
            draft.vsync != settings.vsync;
        settings = draft;
        apply_audio_state(settings, true);
        apply_language(settings.language);
        if (display_changed) {
            apply_display(settings);
        }
        world_renderer.set_blood_enabled(settings.blood_enabled);
        if (!save_runtime_settings()) {
            settings_status = localization.text("settings.save_failed");
            refresh_settings();
            return;
        }
        settings_status.clear();
        ui.set_controls_text(control_hint(
            settings.bindings,
            settings.language == "zh"));
        ui_screen = settings_return_screen;
        refresh_settings();
    };

    ui.set_controls_text(control_hint(
        settings.bindings,
        settings.language == "zh"));
    if (options.show_settings) {
        open_settings();
    }

    NSYNC_LOG_INFO(
        "client",
        "window running; close it or press Esc to exit");
    if (menu_mode) {
        NSYNC_LOG_INFO("client", "starting in offline main menu");
    }

    core::FixedStepClock clock(options.tick_rate, options.max_catchup_steps);
    clock.reset(core::FixedStepClock::Clock::now());

    Camera camera;
    if (const core::Player* local = world.players().get(local_player)) {
        camera.x = fix_to_float(local->position.x);
        camera.y = fix_to_float(local->position.y) + 0.7f;
    }

    bool running = true;
    const auto action_scancode = [&](GameAction action) {
        return settings.bindings.get(action);
    };
    int frame_count = 0;
    int frames_since_fps = 0;
    int fps = 0;
    int online_match_steps = 0;
    std::uint8_t sequence = 0;
    net::InputProvider online_provider =
        [&human_state](std::uint32_t target_tick) {
            core::InputCommand command;
            command.tick = target_tick;
            command.sequence = static_cast<std::uint8_t>(target_tick & 0xFFU);
            command.buttons = human_state.buttons;
            command.aim_x = human_state.aim_x;
            command.aim_y = human_state.aim_y;
            return command;
        };
    auto last_fps_time = core::FixedStepClock::Clock::now();

    const core::Player* previous_local = world.players().get(local_player);
    int previous_health =
        previous_local == nullptr
            ? 0
            : previous_local->health.raw() / core::Fix32::kOne;
    int previous_shield =
        previous_local == nullptr ? 0 : previous_local->shield;
    bool previous_alive = previous_local != nullptr && previous_local->alive != 0;
    std::uint8_t previous_weapon =
        previous_local == nullptr ? 0 : previous_local->weapon;
    std::uint8_t previous_carried =
        previous_local == nullptr ? 0 : previous_local->carried_drops;
    std::uint16_t previous_delivered =
        previous_local == nullptr ? 0 : previous_local->crystals_delivered;
    std::uint8_t previous_winner = world.winner();
    std::array<std::uint16_t, core::kMaxPlayers> previous_ammo{};
    std::array<std::uint16_t, core::kMaxPlayers> previous_reload_timer{};
    std::array<bool, core::kMaxPlayers> previous_audio_alive{};
    std::array<float, core::kMaxPlayers> footstep_timers{};
    std::unordered_map<std::uint32_t, core::Projectile>
        previous_projectiles;
    world.players().for_each([&](const core::Player& player) {
        if (player.slot < core::kMaxPlayers) {
            previous_ammo[player.slot] = player.ammo;
            previous_reload_timer[player.slot] = player.reload_timer;
            previous_audio_alive[player.slot] = player.alive != 0;
        }
    });

    std::optional<shared::AssetWatcher> asset_watcher;
    if (options.hot_reload && dev_asset_root.has_value()) {
        asset_watcher.emplace();
        const auto watch_asset =
            [&](const std::string& key, const std::string& virtual_path) {
                asset_watcher->watch(
                    key,
                    *dev_asset_root / fs::path(virtual_path));
            };
        watch_asset("content", "data/content.json");
        watch_asset("map", map_path);
        watch_asset("atlas", "sprites/heroes.atlas.json");
        watch_asset("animations", "data/animations.json");
        watch_asset("hero_rigs", "data/hero_rigs.json");
        watch_asset("hero_poses", "data/hero_poses.json");
        watch_asset(
            "original_hero_atlas",
            "generated/original_heroes/heroes.atlas.json");
        const std::string atlas_image =
            world_renderer.atlas_image_path().empty()
                ? std::string("sprites/heroes.png")
                : world_renderer.atlas_image_path();
        watch_asset("atlas_image", atlas_image);
        const std::string original_image =
            world_renderer.original_image_path().empty()
                ? std::string("generated/original_heroes/heroes.png")
                : world_renderer.original_image_path();
        watch_asset("original_hero_image", original_image);
        asset_watcher->prime();
        NSYNC_LOG_INFO(
            "client",
            "hot reload watching ",
            asset_watcher->size(),
            " files under ",
            dev_asset_root->string());
    }

    std::string reload_notice;
    float reload_notice_seconds = 0.0f;
    const auto set_reload_notice = [&](const std::string& text) {
        reload_notice = text;
        reload_notice_seconds = 4.0f;
    };

    const auto reload_content_data = [&]() -> bool {
        const auto content_text = resources.read_text("data/content.json");
        if (!content_text) {
            NSYNC_LOG_WARN(
                "client",
                "hot reload: data/content.json could not be read");
            return false;
        }

        core::ContentDatabase candidate;
        std::string error;
        if (!candidate.load_json(*content_text, &error)) {
            NSYNC_LOG_WARN(
                "client",
                "hot reload: content reload failed: ",
                error);
            return false;
        }

        core::content() = candidate;
        selected_hero = core::content().find_hero(selected_hero_id);
        if (selected_hero == nullptr) {
            selected_hero_id = core::content().hero(0).id;
            selected_hero = core::content().find_hero(selected_hero_id);
            NSYNC_LOG_WARN(
                "client",
                "hot reload: selected hero missing; using ",
                selected_hero_id);
        }

        ui.set_heroes(make_ui_heroes(settings.language));
        NSYNC_LOG_INFO(
            "client",
            "hot reload: content heroes=",
            core::content().hero_count(),
            " weapons=",
            core::content().weapon_count(),
            " fingerprint=",
            core::content().fingerprint());
        return true;
    };

    const auto reload_map_data = [&]() -> bool {
        core::Map next_map;
        std::string error;
        if (!load_map_from_resources(map_path, next_map, error)) {
            NSYNC_LOG_WARN(
                "client",
                "hot reload: map reload failed: ",
                error);
            return false;
        }
        if (active_game_mode == core::GameMode::Control &&
            !next_map.objective.has_value()) {
            NSYNC_LOG_WARN(
                "client",
                "hot reload: control mode requires a map objective");
            return false;
        }
        if (active_game_mode == core::GameMode::Crystal &&
            (next_map.drop_depots.empty() ||
             next_map.drop_spawns.empty())) {
            NSYNC_LOG_WARN(
                "client",
                "hot reload: crystal mode requires spawns and depots");
            return false;
        }

        const std::string map_id = next_map.id;
        map = std::move(next_map);
        world_renderer.load_map_art(resources, map.id);
        NSYNC_LOG_INFO(
            "client",
            "hot reload: map ",
            map_id,
            " fingerprint=",
            core::map_fingerprint(map));
        return true;
    };

    struct FeedEntry {
        UiFeedItem item;
        float remaining = 0.0f;
    };
    struct StreakState {
        std::uint16_t kills = 0;
        int consecutive = 0;
        int near = 0;
        std::uint32_t last_kill_tick = 0;
        std::uint32_t near_window_ticks = 240;
    };
    std::vector<FeedEntry> feed_entries;
    std::array<bool, core::kMaxPlayers> previous_player_alive{};
    std::array<bool, core::kMaxPlayers> previous_ability_active{};
    std::array<std::int32_t, 2> previous_scores{};
    std::array<StreakState, core::kMaxPlayers> streak_states{};
    bool first_blood_seen = false;
    float damage_flash_seconds = 0.0f;
    std::uint32_t last_online_match_id = 0;
    std::uint32_t last_online_map_revision =
        online_mode ? online_client.map_revision() : 0;

    const auto reset_combat_tracking = [&]() {
        feed_entries.clear();
        previous_player_alive.fill(false);
        previous_ability_active.fill(false);
        previous_ammo.fill(0);
        previous_reload_timer.fill(0);
        previous_audio_alive.fill(false);
        footstep_timers.fill(0.0f);
        previous_projectiles.clear();
        streak_states.fill({});
        first_blood_seen = false;
        world.players().for_each([&](const core::Player& player) {
            if (player.slot < core::kMaxPlayers) {
                previous_player_alive[player.slot] = player.alive != 0;
                previous_ammo[player.slot] = player.ammo;
                previous_reload_timer[player.slot] = player.reload_timer;
                previous_audio_alive[player.slot] = player.alive != 0;
                streak_states[player.slot].kills = player.kills;
                first_blood_seen =
                    first_blood_seen || player.kills != 0;
            }
        });
        previous_scores = world.scores();
        damage_flash_seconds = 0.0f;
    };

    const auto add_feed = [&](const std::string& text, int team) {
        if (feed_entries.size() >= 6) {
            feed_entries.pop_back();
        }
        feed_entries.insert(
            feed_entries.begin(),
            FeedEntry{UiFeedItem{text, team}, 6.0f});
    };

    const auto reset_direct_sandbox = [&]() {
        direct_world.set_map(map);
        direct_world.set_game_mode(active_game_mode);
        direct_world.reset(1, 4);
        direct_world.set_bot_slots(0x0E);

        direct_world.players().for_each([&](const core::Player& player) {
            if (player.slot == 0) {
                local_player = player.id;
            }
        });
        const core::HeroStats* hero =
            core::content().find_hero(selected_hero_id);
        if (hero != nullptr) {
            if (core::Player* local =
                    direct_world.players().get(local_player)) {
                local->hero = static_cast<std::uint8_t>(hero->type);
                local->health = core::Fix32::from_int(hero->health);
                local->shield = hero->shield;
                local->gadget_cooldowns.fill(0);
                local->gadget_active_ticks.fill(0);
                local->invulnerable_ticks = 0;
                local->heal_ticks = 0;
                local->slow_ticks = 0;
                local->stun_ticks = 0;
                local->gadget_latch = 0;
                local->shield_regen_delay = 0;
            }
        }
        direct_world.players().for_each([](core::Player& player) {
            core::equip_hero_default_weapon(player);
        });

        const core::Player* local =
            direct_world.players().get(local_player);
        if (local != nullptr) {
            camera.x = fix_to_float(local->position.x);
            camera.y = fix_to_float(local->position.y) + 0.7f;
            previous_health =
                local->health.raw() / core::Fix32::kOne;
            previous_shield = local->shield;
            previous_alive = local->alive != 0;
            previous_weapon = local->weapon;
        } else {
            previous_health = 0;
            previous_shield = 0;
            previous_alive = false;
            previous_weapon = 0;
        }
        previous_winner = direct_world.winner();
        sequence = 0;
        reset_combat_tracking();
        world_renderer.reset_player_visuals();
        NSYNC_LOG_INFO(
            "client",
            "direct sandbox reset at spawn");
    };

    enum class LoadingKind {
        Boot,
        Deploy,
        Error,
    };
    LoadingKind loading_kind = LoadingKind::Boot;
    bool loading_active = false;
    UiScreen loading_return_screen = ui_screen;
    float loading_duration = 0.9f;
    float loading_elapsed = 0.0f;
    std::string loading_error;
    std::string loading_recommendation;
    std::string loading_detail;

    const auto begin_loading = [&](
        LoadingKind kind,
        UiScreen return_screen,
        float duration) {
        loading_kind = kind;
        loading_return_screen = return_screen;
        loading_duration = std::max(duration, 0.01f);
        loading_elapsed = 0.0f;
        loading_error.clear();
        loading_recommendation.clear();
        loading_detail.clear();
        loading_active = true;
        ui_screen = UiScreen::Loading;
    };

    const auto show_loading_error = [&](
        std::string title,
        std::string message) {
        loading_kind = LoadingKind::Error;
        loading_active = false;
        loading_return_screen = ui_screen == UiScreen::Loading
            ? UiScreen::Menu
            : ui_screen;
        loading_error = std::move(title);
        loading_recommendation = std::move(message);
        loading_detail.clear();
        ui_screen = UiScreen::Loading;
    };

    if (!automation_run && !options.show_settings &&
        !options.simulate_click && !net_mode &&
        (menu_mode || online_mode)) {
        begin_loading(LoadingKind::Boot, ui_screen, 0.9f);
    }

    const auto reload_sprite_assets = [&]() -> bool {
        if (!world_renderer.reload_visuals(resources)) {
            return false;
        }
        world_renderer.load_map_art(resources, map.id);
        if (asset_watcher.has_value() &&
            dev_asset_root.has_value()) {
            const std::string image = world_renderer.atlas_image_path();
            if (!image.empty()) {
                asset_watcher->watch(
                    "atlas_image",
                    *dev_asset_root / fs::path(image));
            }
            const std::string original_image =
                world_renderer.original_image_path();
            if (!original_image.empty()) {
                asset_watcher->watch(
                    "original_hero_image",
                    *dev_asset_root / fs::path(original_image));
            }
        }
        return true;
    };

    const auto handle_asset_reload =
        [&](bool content_changed,
            bool map_changed,
            bool sprites_changed,
            bool manual) {
        bool attempted_sim = content_changed || map_changed;
        bool sim_reloaded = false;
        if (attempted_sim && (online_mode || net_mode)) {
            NSYNC_LOG_WARN(
                "client",
                "hot reload: content/map changes are disabled in "
                "server-authoritative mode");
            attempted_sim = false;
        } else {
            if (content_changed && reload_content_data()) {
                sim_reloaded = true;
            }
            if (map_changed && reload_map_data()) {
                sim_reloaded = true;
            }
        }

        if (sim_reloaded) {
            reset_direct_sandbox();
        }
        const bool sprites_reloaded =
            sprites_changed && reload_sprite_assets();

        if (sim_reloaded && sprites_reloaded) {
            set_reload_notice("RELOAD content/map + visuals");
        } else if (sim_reloaded) {
            set_reload_notice("RELOAD content/map (sandbox reset)");
        } else if (sprites_reloaded) {
            set_reload_notice("RELOAD visuals");
        } else if (manual) {
            set_reload_notice(
                attempted_sim || (online_mode || net_mode)
                    ? "RELOAD ignored in online mode"
                    : "RELOAD failed; keeping current assets");
        }
        if (manual) {
            NSYNC_LOG_INFO(
                "client",
                "manual asset reload complete: content=",
                sim_reloaded ? "yes" : "no",
                " visuals=",
                sprites_reloaded ? "yes" : "no");
        }
    };

    reset_combat_tracking();

    const auto process_ui_action = [&]() {
        if (pending_ui_action.empty()) {
            return;
        }
        const std::string action = std::move(pending_ui_action);
        const std::string value = std::move(pending_ui_value);
        pending_ui_action.clear();
        pending_ui_value.clear();
        NSYNC_LOG_DEBUG(
            "client",
            "ui action: ",
            action,
            value.empty() ? "" : " / ",
            value);
        audio.play(AudioCue::UiClick);

        const auto find_hero_index = [&](const std::string& hero_id) {
            for (std::size_t index = 0; index < core::content().hero_count(); ++index) {
                if (core::content().hero(index).id == hero_id) {
                    return static_cast<int>(index);
                }
            }
            return -1;
        };

        if (action == "settings-open") {
            open_settings();
        } else if (action == "settings-save") {
            commit_settings();
        } else if (action == "settings-cancel") {
            cancel_settings();
        } else if (action == "settings-tab") {
            settings_controls_tab = value == "controls";
            settings_capture = -1;
            settings_status.clear();
            refresh_settings();
        } else if (action == "settings-volume") {
            const int volume = std::clamp(std::atoi(value.c_str()), 0, 100);
            draft.master_volume = volume;
            preview_audio();
            settings_status =
                localization.text("settings.volume") + " " +
                std::to_string(volume) + "%";
            refresh_settings();
        } else if (action == "settings-audio-toggle") {
            draft.audio_enabled = !draft.audio_enabled;
            preview_audio();
            settings_status = localization.text("settings.unsaved");
            refresh_settings();
        } else if (action == "settings-fullscreen-toggle") {
            draft.fullscreen = !draft.fullscreen;
            settings_status = localization.text("settings.unsaved");
            refresh_settings();
        } else if (action == "settings-vsync-toggle") {
            draft.vsync = !draft.vsync;
            settings_status = localization.text("settings.unsaved");
            refresh_settings();
        } else if (action == "settings-blood-toggle") {
            draft.blood_enabled = !draft.blood_enabled;
            settings_status = localization.text("settings.unsaved");
            refresh_settings();
        } else if (action == "settings-language-toggle") {
            draft.language = draft.language == "zh" ? "en" : "zh";
            apply_language(draft.language);
            settings_status = localization.text("settings.unsaved");
            refresh_settings();
        } else if (action == "settings-res-prev" ||
                   action == "settings-res-next") {
            int index = resolution_index(
                resolution_options,
                draft.width,
                draft.height);
            if (index < 0) {
                index = 0;
            }
            const int delta = action == "settings-res-next" ? 1 : -1;
            const int count =
                static_cast<int>(resolution_options.size());
            index = (index + delta + count) % count;
            draft.width = resolution_options[
                static_cast<std::size_t>(index)].width;
            draft.height = resolution_options[
                static_cast<std::size_t>(index)].height;
            settings_status = localization.text("settings.unsaved");
            refresh_settings();
        } else if (action == "settings-reset") {
            const int current_width = draft.width;
            const int current_height = draft.height;
            draft = ClientSettings{};
            draft.width = current_width;
            draft.height = current_height;
            settings_capture = -1;
            apply_language(draft.language);
            preview_audio();
            settings_status = localization.text("settings.defaults_restored");
            refresh_settings();
        } else if (action == "settings-bind") {
            GameAction target = GameAction::Count;
            if (parse_game_action(value, target)) {
                settings_capture = static_cast<int>(target);
                settings_status =
                    localization.text("settings.press_key_for") + " " +
                    localization.text(
                        "action." + std::string(game_action_id(target)));
                refresh_settings();
            }
        } else if (action == "quit") {
            running = false;
        } else if (action == "resume") {
            if (ui_screen == UiScreen::Paused) {
                ui_screen = UiScreen::Match;
            }
        } else if (action == "menu") {
            if (online_mode) {
                running = false;
            } else if (!net_mode) {
                ui_screen = UiScreen::Menu;
                reset_direct_sandbox();
            }
        } else if (action == "start") {
            if (menu_mode && ui_screen == UiScreen::Menu) {
                if (active_game_mode == core::GameMode::Control &&
                    !map.objective.has_value()) {
                    show_loading_error(
                        localization.text("loading.error_kicker"),
                        localization.text("error.control_objective"));
                } else if (active_game_mode == core::GameMode::Crystal &&
                           (map.drop_depots.empty() ||
                            map.drop_spawns.empty())) {
                    show_loading_error(
                        localization.text("loading.error_kicker"),
                        localization.text("error.crystal_layout"));
                } else {
                    begin_loading(
                        LoadingKind::Deploy,
                        UiScreen::Match,
                        0.9f);
                }
            }
        } else if (action == "mode") {
            if (ui_screen == UiScreen::Menu && !online_mode && !net_mode &&
                (value == "tdm" || value == "control" ||
                 value == "crystal")) {
                if (value == "control" && !map.objective.has_value()) {
                    show_loading_error(
                        localization.text("loading.error_kicker"),
                        localization.text("error.control_objective"));
                } else if (value == "crystal" &&
                           (map.drop_depots.empty() ||
                            map.drop_spawns.empty())) {
                    show_loading_error(
                        localization.text("loading.error_kicker"),
                        localization.text("error.crystal_layout"));
                } else {
                    if (value == "control") {
                        active_game_mode = core::GameMode::Control;
                    } else if (value == "crystal") {
                        active_game_mode = core::GameMode::Crystal;
                    } else {
                        active_game_mode = core::GameMode::Tdm;
                    }
                    reset_direct_sandbox();
                }
            }
        } else if (action == "hero") {
            if (online_mode && ui_screen == UiScreen::Lobby) {
                const int index = find_hero_index(value);
                if (index >= 0) {
                    online_client.select_hero(static_cast<std::uint8_t>(index));
                }
            } else if (ui_screen == UiScreen::Menu && !online_mode && !net_mode) {
                const core::HeroStats* hero = core::content().find_hero(value);
                if (hero != nullptr) {
                    selected_hero_id = hero->id;
                    selected_hero = hero;
                    reset_direct_sandbox();
                }
            }
        } else if (action == "ready") {
            if (online_mode && ui_screen == UiScreen::Lobby) {
                online_client.set_ready(!online_client.ready());
            }
        } else if (action == "rematch") {
            if (online_mode && ui_screen == UiScreen::Results) {
                online_client.request_rematch();
            } else if (!online_mode && !net_mode &&
                       ui_screen == UiScreen::Results) {
                begin_loading(
                    LoadingKind::Deploy,
                    UiScreen::Match,
                    0.7f);
            }
        } else if (action == "loading-dismiss") {
            if (ui_screen == UiScreen::Loading) {
                loading_active = false;
                loading_error.clear();
                loading_recommendation.clear();
                loading_detail.clear();
                ui_screen = loading_return_screen;
            }
        }
        if (!pending_ui_action.empty()) {
            NSYNC_LOG_WARN(
                "client",
                "ignored recursive RmlUi action: ",
                pending_ui_action);
            pending_ui_action.clear();
            pending_ui_value.clear();
        }
    };

    const auto display_name_for_slot = [&](int slot) {
        if (online_mode) {
            for (const net::LobbyMember& member :
                 online_client.lobby_state().members) {
                if (static_cast<int>(member.slot) == slot &&
                    !member.name.empty()) {
                    return member.name;
                }
            }
        }
        if (slot == static_cast<int>(local_slot)) {
            return options.player_name;
        }
        return "BOT " + std::to_string(slot + 1);
    };

    const auto register_killstreak = [&](int slot, std::uint32_t tick) {
        if (slot < 0 || slot >= core::kMaxPlayers) {
            return;
        }
        const core::Player* player = nullptr;
        world.players().for_each([&](const core::Player& value) {
            if (value.slot == slot) {
                player = &value;
            }
        });
        if (player == nullptr) {
            return;
        }
        StreakState& state = streak_states[slot];
        if (!first_blood_seen) {
            first_blood_seen = true;
            add_feed(
                "FIRST BLOOD // " + display_name_for_slot(slot),
                player->team);
        }
        if (state.near > 0 &&
            tick - state.last_kill_tick <= state.near_window_ticks) {
            ++state.near;
            state.near_window_ticks += 120;
        } else {
            state.near = 1;
            state.near_window_ticks = 240;
        }
        state.last_kill_tick = tick;
        ++state.consecutive;
        if (state.near == 2) {
            add_feed(
                "DOUBLE KILL // " + display_name_for_slot(slot),
                player->team);
        } else if (state.near == 3) {
            add_feed(
                "TRIPLE KILL // " + display_name_for_slot(slot),
                player->team);
        } else if (state.near == 4) {
            add_feed(
                "MONSTER KILL // " + display_name_for_slot(slot),
                player->team);
        } else if (state.near >= 5) {
            add_feed(
                "SKIRMISH // " + display_name_for_slot(slot),
                player->team);
        }
        if (state.consecutive == 4) {
            add_feed(
                display_name_for_slot(slot) + " // KILLING SPREE",
                player->team);
        } else if (state.consecutive == 6) {
            add_feed(
                display_name_for_slot(slot) + " // RAMPAGE",
                player->team);
        } else if (state.consecutive == 8) {
            add_feed(
                display_name_for_slot(slot) + " // INSANITY",
                player->team);
        } else if (state.consecutive == 10) {
            add_feed(
                display_name_for_slot(slot) + " // UNSTOPPABLE",
                player->team);
        } else if (state.consecutive >= 15) {
            add_feed(
                display_name_for_slot(slot) + " // STOP HIM",
                player->team);
        }
    };

    const auto hero_id_for_index = [&](int index) {
        if (index >= 0 &&
            static_cast<std::size_t>(index) < core::content().hero_count()) {
            return core::content().hero(static_cast<std::size_t>(index)).id;
        }
        return std::string("unknown");
    };

    const auto lobby_member_for_slot =
        [&](int slot) -> const net::LobbyMember* {
            if (!online_mode) {
                return nullptr;
            }
            for (const net::LobbyMember& member :
                 online_client.lobby_state().members) {
                if (static_cast<int>(member.slot) == slot) {
                    return &member;
                }
            }
            return nullptr;
        };

    const auto collect_world_players = [&]() {
        std::vector<UiPlayerRow> rows;
        world.players().for_each([&](const core::Player& player) {
            UiPlayerRow row;
            row.slot = player.slot;
            row.team = player.team;
            row.name = display_name_for_slot(player.slot);
            row.hero = hero_id_for_index(player.hero);
            row.max_health = static_cast<int>(
                core::content().hero(player.hero).health);
            row.health = static_cast<int>(
                player.health.raw() / core::Fix32::kOne);
            row.kills = static_cast<int>(player.kills);
            row.deaths = static_cast<int>(player.deaths);
            row.damage_dealt = player.damage_dealt;
            row.blocked_damage = player.blocked_damage;
            row.alive = player.alive != 0;
            row.bot = player.bot != 0;
            row.connected = true;
            row.local = player.slot == local_slot;
            if (const net::LobbyMember* member =
                    lobby_member_for_slot(player.slot)) {
                row.ready = member->ready != 0;
                row.connected = member->connected != 0;
            }
            rows.push_back(std::move(row));
        });
        std::sort(
            rows.begin(),
            rows.end(),
            [](const UiPlayerRow& left, const UiPlayerRow& right) {
                if (left.team != right.team) {
                    return left.team < right.team;
                }
                return left.slot < right.slot;
            });
        return rows;
    };

    const auto collect_lobby_players = [&]() {
        std::vector<UiPlayerRow> rows;
        if (!online_mode) {
            return rows;
        }
        for (const net::LobbyMember& member :
             online_client.lobby_state().members) {
            UiPlayerRow row;
            row.slot = member.slot;
            row.team = member.team;
            row.name = member.name.empty()
                ? (member.bot != 0
                       ? "BOT " + std::to_string(member.slot + 1)
                       : options.player_name)
                : member.name;
            row.hero = hero_id_for_index(member.hero);
            row.max_health = static_cast<int>(
                core::content().hero(
                    std::min<std::size_t>(
                        member.hero,
                        core::content().hero_count() - 1)).health);
            row.health = member.ready != 0 ? row.max_health : 0;
            row.alive = true;
            row.bot = member.bot != 0;
            row.ready = member.ready != 0;
            row.connected = member.connected != 0;
            row.local = member.slot == local_slot;
            rows.push_back(std::move(row));
        }
        const int player_count = static_cast<int>(
            online_client.lobby_state().player_count);
        const int team_slots[2] = {
            (player_count + 1) / 2,
            player_count / 2,
        };
        for (int team = 0; team < 2; ++team) {
            for (int index = 0; index < team_slots[team]; ++index) {
                const int slot = team == 0 ? index * 2 : index * 2 + 1;
                const bool present = std::any_of(
                    rows.begin(),
                    rows.end(),
                    [slot](const UiPlayerRow& row) {
                        return row.slot == slot;
                    });
                if (present) {
                    continue;
                }
                UiPlayerRow row;
                row.slot = slot;
                row.team = team;
                row.name = "OPEN SLOT";
                row.hero.clear();
                row.empty = true;
                row.connected = false;
                rows.push_back(std::move(row));
            }
        }
        std::sort(
            rows.begin(),
            rows.end(),
            [](const UiPlayerRow& left, const UiPlayerRow& right) {
                if (left.team != right.team) {
                    return left.team < right.team;
                }
                return left.slot < right.slot;
            });
        return rows;
    };

    bool reload_requested = false;
    double reload_poll_seconds = 0.0;
    const auto loop_start = core::FixedStepClock::Clock::now();
    auto last_perf_time = loop_start;
    std::uint64_t last_perf_bytes_sent = 0;
    std::uint64_t last_perf_bytes_received = 0;
    int last_perf_frames = 0;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    running = false;
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (ui_screen == UiScreen::Loading) {
                        if (!event.key.repeat &&
                            (event.key.key == SDLK_ESCAPE ||
                             event.key.key == SDLK_RETURN)) {
                            if (loading_kind == LoadingKind::Error) {
                                loading_active = false;
                                loading_error.clear();
                                loading_recommendation.clear();
                                loading_detail.clear();
                                ui_screen = loading_return_screen;
                            } else {
                                loading_elapsed = loading_duration;
                            }
                        }
                        break;
                    }
                    if (ui_screen == UiScreen::Settings) {
                        if (event.key.repeat) {
                            break;
                        }
                        if (settings_capture >= 0) {
                            const int target_index = settings_capture;
                            const SDL_Scancode scancode =
                                event.key.scancode;
                            if (target_index < 0 ||
                                target_index >=
                                    static_cast<int>(kGameActionCount)) {
                                settings_capture = -1;
                                break;
                            }
                            const GameAction target =
                                static_cast<GameAction>(target_index);
                            if (scancode == SDL_SCANCODE_ESCAPE) {
                                settings_capture = -1;
                                settings_status =
                                    localization.text("settings.binding_cancelled");
                                refresh_settings();
                                break;
                            }
                            if (scancode == SDL_SCANCODE_BACKSPACE) {
                                const KeyBindings defaults =
                                    default_key_bindings();
                                draft.bindings.set(
                                    target,
                                    defaults.get(target));
                                settings_capture = -1;
                                settings_status =
                                    localization.text("settings.binding_reset");
                                refresh_settings();
                                break;
                            }
                            if (scancode == SDL_SCANCODE_F5) {
                                settings_status =
                                    localization.text("settings.f5_reserved");
                                refresh_settings();
                                break;
                            }
                            if (scancode == SDL_SCANCODE_UNKNOWN) {
                                break;
                            }
                            bool conflict = false;
                            for (std::size_t index = 0;
                                 index < kGameActionCount;
                                 ++index) {
                                if (static_cast<int>(index) ==
                                    target_index) {
                                    continue;
                                }
                                if (draft.bindings.get(
                                        static_cast<GameAction>(index)) ==
                                    static_cast<int>(scancode)) {
                                    settings_status =
                                        localization.text(
                                            "settings.key_bound") +
                                        " " +
                                        localization.text(
                                            "action." + std::string(
                                                game_action_id(
                                                    static_cast<GameAction>(
                                                        index))));
                                    conflict = true;
                                    break;
                                }
                            }
                            if (conflict) {
                                refresh_settings();
                                break;
                            }
                            draft.bindings.set(
                                target,
                                static_cast<int>(scancode));
                            settings_capture = -1;
                            settings_status =
                                localization.text("settings.binding_updated");
                            refresh_settings();
                            break;
                        }
                        if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                            cancel_settings();
                        }
                        break;
                    }
                    if (event.key.key == SDLK_ESCAPE && !event.key.repeat) {
                        if (ui_screen == UiScreen::Menu ||
                            ui_screen == UiScreen::Lobby) {
                            running = false;
                        } else if (ui_screen == UiScreen::Paused) {
                            ui_screen = UiScreen::Match;
                        } else if (ui_screen == UiScreen::Results) {
                            if (online_mode) {
                                running = false;
                            } else if (!net_mode) {
                                ui_screen = UiScreen::Menu;
                                reset_direct_sandbox();
                            }
                        } else if (ui_screen == UiScreen::Match) {
                            if (online_mode || net_mode) {
                                running = false;
                            } else {
                                ui_screen = UiScreen::Paused;
                                ui.set_scoreboard_visible(false);
                            }
                        }
                    }
                    if (!event.key.repeat &&
                        event.key.scancode ==
                            action_scancode(GameAction::Pause) &&
                        ui_screen == UiScreen::Match &&
                        event.key.key != SDLK_ESCAPE) {
                        if (online_mode || net_mode) {
                            running = false;
                        } else {
                            ui_screen = UiScreen::Paused;
                            ui.set_scoreboard_visible(false);
                        }
                    }
                    if (event.key.key == SDLK_F5 && !event.key.repeat) {
                        reload_requested = true;
                    }
                    if (!event.key.repeat &&
                        ui_screen == UiScreen::Match &&
                        event.key.scancode ==
                            action_scancode(GameAction::Scoreboard)) {
                        ui.set_scoreboard_visible(
                            !ui.scoreboard_visible());
                    }
                    if (!online_mode && !net_mode &&
                        ui_screen == UiScreen::Menu &&
                        !event.key.repeat) {
                        if (event.key.key == SDLK_RETURN ||
                            event.key.key == SDLK_KP_ENTER) {
                            pending_ui_action = "start";
                        } else if (event.key.key == SDLK_1 &&
                                   core::content().hero_count() > 0) {
                            pending_ui_action = "hero";
                            pending_ui_value = core::content().hero(0).id;
                        } else if (event.key.key == SDLK_2 &&
                                   core::content().hero_count() > 1) {
                            pending_ui_action = "hero";
                            pending_ui_value = core::content().hero(1).id;
                        } else if (event.key.key == SDLK_3 &&
                                   core::content().hero_count() > 2) {
                            pending_ui_action = "hero";
                            pending_ui_value = core::content().hero(2).id;
                        }
                    }
                    if (online_mode && !online_client.in_match()) {
                        if (!event.key.repeat &&
                            event.key.scancode ==
                                action_scancode(GameAction::Reload)) {
                            const bool result_visible =
                                online_client.match_end().match_id != 0 &&
                                online_client.lobby_state().phase ==
                                    static_cast<std::uint8_t>(
                                        net::LobbyPhase::MatchEnd);
                            if (result_visible) {
                                online_client.request_rematch();
                            } else if (ui_screen == UiScreen::Lobby) {
                                online_client.set_ready(
                                    !online_client.ready());
                            }
                        } else if (event.key.key == SDLK_1 &&
                                   ui_screen == UiScreen::Lobby) {
                            online_client.select_hero(0);
                        } else if (event.key.key == SDLK_2 &&
                                   ui_screen == UiScreen::Lobby) {
                            online_client.select_hero(1);
                        } else if (event.key.key == SDLK_3 &&
                                   ui_screen == UiScreen::Lobby) {
                            online_client.select_hero(2);
                        }
                    }
                    break;
                case SDL_EVENT_WINDOW_FOCUS_LOST: {
                    window_focused = false;
                    human_state.buttons = 0;
                    float focus_mouse_x = 0.0f;
                    float focus_mouse_y = 0.0f;
                    if ((SDL_GetMouseState(&focus_mouse_x, &focus_mouse_y) &
                         SDL_BUTTON_LMASK) != 0) {
                        mouse_fire_blocked = true;
                    }
                    break;
                }
                case SDL_EVENT_WINDOW_FOCUS_GAINED:
                    window_focused = true;
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                    context->ProcessMouseMove(
                        static_cast<int>(event.motion.x),
                        static_cast<int>(event.motion.y),
                        0);
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        context->ProcessMouseMove(
                            static_cast<int>(event.button.x),
                            static_cast<int>(event.button.y),
                            0);
                        context->ProcessMouseButtonDown(0, 0);
                    }
                    break;
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        context->ProcessMouseMove(
                            static_cast<int>(event.button.x),
                            static_cast<int>(event.button.y),
                            0);
                        context->ProcessMouseButtonUp(0, 0);
                    }
                    break;
                case SDL_EVENT_MOUSE_WHEEL:
                    context->ProcessMouseWheel(-event.wheel.y, 0);
                    break;
                default:
                    break;
            }
        }

        process_ui_action();
        if (ui_screen != rendered_screen) {
            ui.set_screen(ui_screen);
            rendered_screen = ui_screen;
        }

        int new_width = width;
        int new_height = height;
        SDL_GetWindowSize(window, &new_width, &new_height);
        new_width = std::max(new_width, 1);
        new_height = std::max(new_height, 1);
        if (new_width != width || new_height != height) {
            apply_surface_size(new_width, new_height);
            if (!settings.fullscreen) {
                settings.width = width;
                settings.height = height;
                ensure_resolution(
                    resolution_options,
                    settings.width,
                    settings.height);
                if (ui_screen == UiScreen::Settings &&
                    !draft.fullscreen) {
                    draft.width = width;
                    draft.height = height;
                    refresh_settings();
                }
            }
        }

        const float camera_zoom = std::clamp(options.camera_zoom, 0.4f, 2.5f);
        camera.pixels_per_unit =
            kWorldPixelsPerUnit *
            std::max(
                1.0f,
                static_cast<float>(height) /
                    kWorldCameraMaxScreenHeight) /
            camera_zoom;

        const auto now = core::FixedStepClock::Clock::now();
        const core::StepAdvance advance = clock.advance(now);
        const float frame_seconds = std::max(
            static_cast<float>(advance.frame_seconds),
            0.0f);

        if (loading_active && loading_kind != LoadingKind::Error) {
            loading_elapsed += frame_seconds;
            if (loading_elapsed >= loading_duration) {
                loading_active = false;
                if (loading_kind == LoadingKind::Deploy) {
                    reset_direct_sandbox();
                }
                ui_screen = loading_return_screen;
            }
        }

        reload_poll_seconds += frame_seconds;
        bool content_changed = false;
        bool map_changed = false;
        bool sprites_changed = false;
        if (asset_watcher.has_value() && reload_poll_seconds >= 0.5) {
            reload_poll_seconds = 0.0;
            for (const std::string& key : asset_watcher->poll()) {
                if (key == "content") {
                    content_changed = true;
                } else if (key == "map") {
                    map_changed = true;
                } else {
                    sprites_changed = true;
                }
            }
        }

        if (reload_requested) {
            reload_requested = false;
            reload_poll_seconds = 0.0;
            if (asset_watcher.has_value()) {
                asset_watcher->prime();
            }
            handle_asset_reload(true, true, true, true);
        } else if (content_changed || map_changed || sprites_changed) {
            handle_asset_reload(
                content_changed,
                map_changed,
                sprites_changed,
                false);
        }

        const bool* keys = SDL_GetKeyboardState(nullptr);
        float mouse_x = 0.0f;
        float mouse_y = 0.0f;
        const std::uint32_t mouse_buttons = SDL_GetMouseState(&mouse_x, &mouse_y);
        const bool local_gameplay = ui_screen == UiScreen::Match;
        const auto action_down = [&](GameAction action) {
            const int scancode = settings.bindings.get(action);
            return scancode > 0 && scancode < SDL_SCANCODE_COUNT &&
                keys[scancode] != 0;
        };

        for (int i = 0; i < advance.steps; ++i) {
            core::InputCommand command;
            command.tick = world.tick();
            command.sequence = sequence++;

            const core::Player* local = world.players().get(local_player);
            if (local_gameplay && local != nullptr) {
                if (action_down(GameAction::MoveLeft)) {
                    command.buttons |= core::kButtonLeft;
                }
                if (action_down(GameAction::MoveRight)) {
                    command.buttons |= core::kButtonRight;
                }
                if (action_down(GameAction::Jump)) {
                    command.buttons |= core::kButtonJump;
                }
                if (action_down(GameAction::Down)) {
                    command.buttons |= core::kButtonDown;
                }
                if (action_down(GameAction::Reload)) {
                    command.buttons |= core::kButtonReload;
                }
                if (action_down(GameAction::WeaponSwitch)) {
                    command.buttons |= core::kButtonWeaponSwitch;
                }
                if (action_down(GameAction::Gadget1)) {
                    command.buttons |= core::kButtonGadget1;
                }
                if (action_down(GameAction::Gadget2)) {
                    command.buttons |= core::kButtonGadget2;
                }
                if (action_down(GameAction::Gadget3)) {
                    command.buttons |= core::kButtonGadget3;
                }
                if ((mouse_buttons & SDL_BUTTON_LMASK) == 0) {
                    mouse_fire_blocked = false;
                }
                if ((mouse_buttons & SDL_BUTTON_LMASK) != 0 &&
                    !mouse_fire_blocked && window_focused &&
                    !ui.scoreboard_visible()) {
                    command.buttons |= core::kButtonFire;
                }

                const float mouse_world_x =
                    camera.x +
                    (mouse_x - static_cast<float>(width) * 0.5f) /
                        camera.pixels_per_unit;
                const float mouse_world_y =
                    camera.y +
                    (mouse_y - static_cast<float>(height) * 0.5f) /
                        camera.pixels_per_unit;
                core::Vec2 aim{};
                if (local->weapon < core::content().weapon_count()) {
                    aim = core::aim_target_pointer(
                        core::content().weapon(local->weapon),
                        local->position,
                        core::Vec2{
                            float_to_fix(mouse_world_x),
                            float_to_fix(mouse_world_y)});
                } else {
                    aim = core::Vec2{
                              float_to_fix(mouse_world_x),
                              float_to_fix(mouse_world_y)} -
                          local->position;
                    aim = aim.normalized();
                }
                if (aim != core::Vec2{}) {
                    command.aim_x = quantize_aim(fix_to_float(aim.x));
                    command.aim_y = quantize_aim(fix_to_float(aim.y));
                } else {
                    command.aim_x = local->facing > 0 ? 32767 : -32767;
                }
            }

            if (online_mode) {
                human_state.buttons = command.buttons;
                human_state.aim_x = command.aim_x;
                human_state.aim_y = command.aim_y;
                online_client.tick(online_provider, client_now_ms());
                refresh_local_player();
                if (online_client.map_revision() !=
                    last_online_map_revision) {
                    last_online_map_revision =
                        online_client.map_revision();
                    map = online_client.map();
                    map_path = "maps/" + map.id + ".json";
                    active_game_mode = world.game_mode();
                    world_renderer.reset_player_visuals();
                    world_renderer.load_map_art(resources, map.id);
                    NSYNC_LOG_INFO(
                        "client",
                        "server switched map: ",
                        map.id);
                }
            } else {
                if (!local_gameplay) {
                    break;
                }
                if (net_mode) {
                    human_state.buttons = command.buttons;
                    human_state.aim_x = command.aim_x;
                    human_state.aim_y = command.aim_y;
                    net_session.tick();
                } else {
                    core::InputFrame input;
                    if (local != nullptr) {
                        core::PlayerCommand player_command;
                        player_command.entity = local_player;
                        player_command.command = command;
                        input.commands.push_back(player_command);
                    }
                    world.step(input);
                }
            }

            if (online_mode && online_client.in_match()) {
                ++online_match_steps;
            }
            const bool tick_limit_reached = online_mode
                ? (online_match_steps >= options.max_ticks)
                : (world.tick() >=
                   static_cast<std::uint32_t>(options.max_ticks));
            if (options.max_ticks > 0 && tick_limit_reached) {
                running = false;
                break;
            }
        }

        if (online_mode) {
            for (const net::LocalClient::VisualCorrection& correction :
                 online_client.take_visual_corrections()) {
                world_renderer.add_position_correction(
                    correction.entity, correction.offset);
            }
        } else if (net_mode) {
            for (const net::LocalClient::VisualCorrection& correction :
                 net_session.client(0).take_visual_corrections()) {
                world_renderer.add_position_correction(
                    correction.entity, correction.offset);
            }
        }

        if (online_mode && online_client.in_match()) {
            const std::uint32_t match_id =
                online_client.stats().match_id;
            if (match_id != 0 && match_id != last_online_match_id) {
                last_online_match_id = match_id;
                reset_combat_tracking();
                world_renderer.reset_player_visuals();
                if (const core::Player* local =
                        world.players().get(local_player)) {
                    previous_health =
                        local->health.raw() / core::Fix32::kOne;
                    previous_shield = local->shield;
                    previous_alive = local->alive != 0;
                    previous_weapon = local->weapon;
                }
            }
        }

        if (const core::Player* local = world.players().get(local_player)) {
            // BattleCamera.UpdateCamera (MOUSE_REACTION):
            // pull toward the cursor with a 1.2 unit dead zone, then lerp back
            // to the hero and clamp the lead to +/-6 x / +/-4 y units.
            const float target_x = fix_to_float(local->position.x);
            const float target_y = fix_to_float(local->position.y) + 0.7f;
            const float camera_dt =
                std::clamp(frame_seconds, 0.0f, 0.05f);
            if (local_gameplay) {
                const float aim_world_x =
                    camera.x +
                    (mouse_x - static_cast<float>(width) * 0.5f) /
                        camera.pixels_per_unit;
                const float aim_world_y =
                    camera.y +
                    (mouse_y - static_cast<float>(height) * 0.5f) /
                        camera.pixels_per_unit;
                float lead_x = (aim_world_x - camera.x) * 1.0f;
                float lead_y = (aim_world_y - camera.y) * 1.5f;
                if (std::abs(lead_x) < 1.2f) {
                    lead_x = 0.0f;
                }
                if (std::abs(lead_y) < 1.2f) {
                    lead_y = 0.0f;
                }
                camera.x += lead_x * camera_dt;
                camera.y += lead_y * camera_dt;
            }
            const float response_x =
                std::clamp(2.0f * camera_dt, 0.0f, 1.0f);
            const float response_y =
                std::clamp(4.0f * camera_dt, 0.0f, 1.0f);
            camera.x += (target_x - camera.x) * response_x;
            camera.y += (target_y - camera.y) * response_y;
            camera.x = std::clamp(
                camera.x,
                target_x - 6.0f,
                target_x + 6.0f);
            camera.y = std::clamp(
                camera.y,
                target_y - 4.0f,
                target_y + 4.0f);

            // Clamp to the actual playable map bounds so a tall/wide world
            // does not leave the player outside the view near its edges.
            core::AABB camera_bounds = world.map().bounds;
            const float half_width =
                static_cast<float>(width) * 0.5f /
                camera.pixels_per_unit;
            const float half_height =
                static_cast<float>(height) * 0.5f /
                camera.pixels_per_unit;
            const float min_x =
                fix_to_float(camera_bounds.min.x) + half_width;
            const float max_x =
                fix_to_float(camera_bounds.max.x) - half_width;
            const float min_y =
                fix_to_float(camera_bounds.min.y) + half_height;
            const float max_y =
                fix_to_float(camera_bounds.max.y) - half_height;
            if (min_x <= max_x) {
                camera.x = std::clamp(camera.x, min_x, max_x);
            }
            if (min_y <= max_y) {
                camera.y = std::clamp(camera.y, min_y, max_y);
            }
            audio.set_listener(
                fix_to_float(local->position.x),
                fix_to_float(local->position.y) + 0.5f);
        }

        const core::Player* current_local = world.players().get(local_player);
        if (current_local != nullptr) {
            const int health =
                current_local->health.raw() / core::Fix32::kOne;
            const int shield = current_local->shield;
            const bool health_lost = health < previous_health;
            const bool shield_lost = shield < previous_shield;
            if (previous_alive && (health_lost || shield_lost)) {
                audio.play(
                    health_lost ? AudioCue::Hit : AudioCue::ShieldHit);
                damage_flash_seconds = 0.25f;
            }
            if (current_local->alive != 0 &&
                current_local->weapon != previous_weapon) {
                audio.play(AudioCue::WeaponSwitch);
            }
            const float local_x = fix_to_float(current_local->position.x);
            const float local_y = fix_to_float(current_local->position.y);
            if (current_local->carried_drops > previous_carried) {
                audio.play_at(AudioCue::CrystalPickup, local_x, local_y);
            }
            if (current_local->crystals_delivered > previous_delivered) {
                audio.play_at(AudioCue::CrystalDeliver, local_x, local_y);
            }
            previous_health = health;
            previous_shield = shield;
            previous_alive = current_local->alive != 0;
            previous_weapon = current_local->weapon;
            previous_carried = current_local->carried_drops;
            previous_delivered = current_local->crystals_delivered;
        }
        if (world.winner() != previous_winner) {
            if (world.winner() != 0) {
                audio.play(AudioCue::MatchEnd);
            }
            previous_winner = world.winner();
        }

        if (local_gameplay) {
            world.players().for_each([&](const core::Player& player) {
                if (player.slot >= core::kMaxPlayers) {
                    return;
                }
                const float x = fix_to_float(player.position.x);
                const float y = fix_to_float(player.position.y);
                const bool local_sound = player.slot == local_slot;
                if (player.alive != 0 &&
                    previous_audio_alive[player.slot] &&
                    player.ammo < previous_ammo[player.slot] &&
                    player.weapon < core::content().weapon_count()) {
                    const AudioCue cue = weapon_shot_cue(
                        core::content().weapon(player.weapon).id);
                    audio.play_at(
                        cue,
                        x + static_cast<float>(player.facing) * 0.8f,
                        y + 0.55f,
                        local_sound ? 0.9f : 0.8f);
                }
                if (player.alive != 0 &&
                    player.reload_timer > 0 &&
                    previous_reload_timer[player.slot] == 0) {
                    audio.play_at(
                        AudioCue::Reload,
                        x,
                        y,
                        local_sound ? 0.7f : 0.55f);
                }
                if (player.alive != 0 && !previous_audio_alive[player.slot]) {
                    audio.play_at(
                        AudioCue::Respawn,
                        x,
                        y,
                        local_sound ? 0.8f : 0.6f);
                } else if (player.alive == 0 &&
                           previous_audio_alive[player.slot]) {
                    audio.play_at(
                        AudioCue::Death,
                        x,
                        y,
                        local_sound ? 0.95f : 0.8f);
                }

                const float speed =
                    std::abs(fix_to_float(player.velocity.x));
                if (player.alive != 0 && player.grounded != 0 &&
                    speed > 0.02f) {
                    footstep_timers[player.slot] -= frame_seconds;
                    if (footstep_timers[player.slot] <= 0.0f) {
                        footstep_timers[player.slot] = 0.31f;
                        audio.play_at(
                            AudioCue::Footstep,
                            x,
                            y,
                            local_sound ? 0.38f : 0.62f);
                    }
                } else {
                    footstep_timers[player.slot] = 0.0f;
                }
                previous_ammo[player.slot] = player.ammo;
                previous_reload_timer[player.slot] = player.reload_timer;
                previous_audio_alive[player.slot] = player.alive != 0;
            });

            std::unordered_map<std::uint32_t, core::Projectile>
                current_projectiles;
            world.projectiles().for_each([&](const core::Projectile& projectile) {
                current_projectiles.emplace(projectile.id.raw(), projectile);
            });
            for (const auto& [raw_id, projectile] : previous_projectiles) {
                if (current_projectiles.find(raw_id) !=
                    current_projectiles.end()) {
                    continue;
                }
                const float hit_x =
                    fix_to_float(projectile.position.x);
                const float hit_y =
                    fix_to_float(projectile.position.y);
                const bool gadget_projectile =
                    projectile.gadget != 0 &&
                    projectile.gadget <=
                        static_cast<std::uint8_t>(
                            core::GadgetType::Count);
                if (gadget_projectile) {
                    const auto gadget = static_cast<core::GadgetType>(
                        projectile.gadget - 1);
                    audio.play_gadget(
                        gadget,
                        projectile.explosive != 0
                            ? GadgetSoundKind::Explode
                            : GadgetSoundKind::Impact,
                        hit_x,
                        hit_y,
                        projectile.explosive != 0 ? 1.0f : 0.7f);
                } else if (projectile.explosive != 0) {
                    audio.play_at(
                        AudioCue::Explosion,
                        hit_x,
                        hit_y,
                        1.0f);
                } else {
                    audio.play_at(
                        AudioCue::ProjectileImpact,
                        hit_x,
                        hit_y,
                        0.55f);
                }
            }
            previous_projectiles = std::move(current_projectiles);
        }

        if (local_gameplay) {
            const std::uint32_t tick = world.tick();
            world.players().for_each([&](const core::Player& player) {
                if (player.slot >= core::kMaxPlayers) {
                    return;
                }
                StreakState& streak = streak_states[player.slot];
                if (player.kills < streak.kills) {
                    streak.consecutive = 0;
                    streak.near = 0;
                } else {
                    for (std::uint16_t kill = streak.kills;
                         kill < player.kills;
                         ++kill) {
                        register_killstreak(player.slot, tick);
                    }
                }
                streak.kills = player.kills;

                bool ability_active = false;
                std::string ability_name;
                core::GadgetType ability_gadget = core::GadgetType::RubyBlade;
                for (std::size_t slot = 0;
                     slot < player.gadget_active_ticks.size();
                     ++slot) {
                    if (player.gadget_active_ticks[slot] == 0) {
                        continue;
                    }
                    const core::GadgetStats& gadget =
                        core::hero_gadget_stats(
                            static_cast<core::HeroType>(player.hero),
                            slot);
                    ability_active = true;
                    ability_gadget = gadget.type;
                    ability_name = settings.language == "zh"
                        ? std::string(gadget.name_zh)
                        : std::string(gadget.name_en);
                    break;
                }
                if (ability_active && !previous_ability_active[player.slot]) {
                    audio.play_gadget(
                        ability_gadget,
                        GadgetSoundKind::Activate,
                        fix_to_float(player.position.x),
                        fix_to_float(player.position.y) + 0.5f,
                        player.slot == local_slot ? 0.9f : 0.72f);
                    add_feed(
                        display_name_for_slot(player.slot) + " // " +
                            ability_name,
                        player.team);
                }
                previous_ability_active[player.slot] = ability_active;

                const bool was_alive = previous_player_alive[player.slot];
                if (was_alive && player.alive == 0) {
                    if (streak.consecutive >= 5) {
                        add_feed(
                            display_name_for_slot(player.slot) +
                                " // STREAK ENDED",
                            player.team);
                    }
                    streak.consecutive = 0;
                    streak.near = 0;
                    streak.last_kill_tick = 0;
                    streak.near_window_ticks = 240;
                    add_feed(
                        display_name_for_slot(player.slot) + " ELIMINATED",
                        player.team);
                } else if (!was_alive && player.alive != 0 &&
                           player.slot == local_slot) {
                    add_feed("YOU RESPAWNED", 2);
                }
                previous_player_alive[player.slot] = player.alive != 0;
            });
            for (int team = 0; team < 2; ++team) {
                if (world.scores()[team] > previous_scores[team]) {
                    add_feed(
                        "TEAM " + std::string(team == 0 ? "BLUE" : "RED") +
                            " SCORES",
                        team);
                }
                previous_scores[team] = world.scores()[team];
            }
        }

        damage_flash_seconds =
            std::max(0.0f, damage_flash_seconds - frame_seconds);
        for (FeedEntry& entry : feed_entries) {
            entry.remaining -= frame_seconds;
        }
        feed_entries.erase(
            std::remove_if(
                feed_entries.begin(),
                feed_entries.end(),
                [](const FeedEntry& entry) {
                    return entry.remaining <= 0.0f;
                }),
            feed_entries.end());

        ++frame_count;
        ++frames_since_fps;
        if (now - last_fps_time >= std::chrono::milliseconds(250)) {
            const auto fps_elapsed_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - last_fps_time).count();
            fps = fps_elapsed_ms > 0
                ? static_cast<int>(
                      (frames_since_fps * 1000 + fps_elapsed_ms / 2) /
                      fps_elapsed_ms)
                : 0;
            frames_since_fps = 0;
            last_fps_time = now;
        }
        const auto perf_elapsed = now - last_perf_time;
        if (perf_elapsed >= std::chrono::seconds(5)) {
            const double perf_seconds =
                std::chrono::duration<double>(perf_elapsed).count();
            const double perf_fps = perf_seconds > 0.0
                ? (frame_count - last_perf_frames) / perf_seconds
                : 0.0;
            if (online_mode && perf_seconds > 0.0) {
                const auto transport = online_client.transport_stats();
                const double up_kbps =
                    static_cast<double>(transport.bytes_sent -
                                        last_perf_bytes_sent) *
                    8.0 / 1024.0 / perf_seconds;
                const double down_kbps =
                    static_cast<double>(transport.bytes_received -
                                        last_perf_bytes_received) *
                    8.0 / 1024.0 / perf_seconds;
                NSYNC_LOG_INFO(
                    "client",
                    "perf: fps=",
                    perf_fps,
                    " frames=",
                    frame_count,
                    " net_up_kbps=",
                    up_kbps,
                    " net_down_kbps=",
                    down_kbps,
                    " rtt_ms=",
                    online_client.stats().rtt_ms,
                    " rollbacks=",
                    online_client.stats().rollbacks);
                last_perf_bytes_sent = transport.bytes_sent;
                last_perf_bytes_received = transport.bytes_received;
            } else {
                NSYNC_LOG_INFO(
                    "client",
                    "perf: fps=",
                    perf_fps,
                    " frames=",
                    frame_count);
            }
            last_perf_time = now;
            last_perf_frames = frame_count;
        }

        std::string ui_selected_hero = selected_hero_id;
        if (online_mode) {
            if (const net::LobbyMember* member =
                    lobby_member_for_slot(local_slot)) {
                ui_selected_hero = hero_id_for_index(member->hero);
            }
        }

        UiScreen target_screen = ui_screen;
        if (ui_screen == UiScreen::Settings ||
            ui_screen == UiScreen::Loading) {
            target_screen = ui_screen;
        } else if (online_mode) {
            if (online_client.in_match()) {
                target_screen = world.match_over()
                    ? UiScreen::Results
                    : UiScreen::Match;
            } else if (online_client.match_end().match_id != 0 &&
                       online_client.lobby_state().phase ==
                           static_cast<std::uint8_t>(
                               net::LobbyPhase::MatchEnd)) {
                target_screen = UiScreen::Results;
            } else {
                target_screen = UiScreen::Lobby;
            }
        } else if (ui_screen == UiScreen::Menu) {
            target_screen = UiScreen::Menu;
        } else if (world.match_over()) {
            target_screen = UiScreen::Results;
        } else if (ui_screen == UiScreen::Paused) {
            target_screen = UiScreen::Paused;
        } else {
            target_screen = UiScreen::Match;
        }
        if (target_screen != ui_screen) {
            ui_screen = target_screen;
        }
        if (ui_screen != rendered_screen) {
            ui.set_screen(ui_screen);
            rendered_screen = ui_screen;
        }

        const std::string notice =
            reload_notice_seconds > 0.0f ? reload_notice : std::string();
        const std::string content_label =
            "CONTENT " + std::to_string(core::content().fingerprint()) +
            " // MAP " + map.id;

        if (ui_screen == UiScreen::Loading) {
            UiLoadingModel loading_model;
            loading_model.failed = loading_kind == LoadingKind::Error;
            loading_model.progress = loading_model.failed
                ? 0.0f
                : std::clamp(
                      loading_elapsed / loading_duration,
                      0.0f,
                      1.0f);
            loading_model.kicker = localization.text(
                loading_model.failed
                    ? "loading.error_kicker"
                    : "loading.kicker");
            if (loading_model.failed) {
                loading_model.step =
                    localization.text("loading.error_kicker");
                loading_model.error = loading_error;
                loading_model.recommendation = loading_recommendation;
            } else if (loading_kind == LoadingKind::Deploy) {
                if (loading_model.progress < 0.45f) {
                    loading_model.step =
                        localization.text("loading.preparing");
                } else if (loading_model.progress < 0.82f) {
                    loading_model.step =
                        localization.text("loading.spawning");
                } else {
                    loading_model.step =
                        localization.text("loading.starting");
                }
                loading_model.detail =
                    localization.text("loading.deploy_detail");
            } else {
                if (loading_model.progress < 0.34f) {
                    loading_model.step =
                        localization.text("loading.resources");
                } else if (loading_model.progress < 0.72f) {
                    loading_model.step =
                        localization.text("loading.content");
                } else {
                    loading_model.step =
                        localization.text("loading.ready");
                }
                loading_model.detail =
                    localization.text("loading.detail");
            }
            loading_model.detail = loading_model.failed
                ? std::string()
                : loading_model.detail;
            ui.update_loading(loading_model);
        } else if (ui_screen == UiScreen::Menu) {
            UiMenuModel menu_model;
            menu_model.player_name = options.player_name;
            menu_model.selected_hero = selected_hero_id;
            menu_model.map_name = map_path;
            menu_model.content_label = notice.empty()
                ? content_label
                : content_label + " // " + notice;
            menu_model.mode = static_cast<int>(active_game_mode);
            menu_model.fps = fps;
            ui.update_menu(menu_model);
        } else if (ui_screen == UiScreen::Lobby) {
            const net::LobbyState& lobby = online_client.lobby_state();
            UiLobbyModel lobby_model;
            lobby_model.status = lobby.status;
            lobby_model.server_label =
                options.server_host + ":" +
                std::to_string(options.server_port) +
                " // " + map.id +
                " // PROTOCOL " +
                std::to_string(net::kProtocolVersion);
            lobby_model.selected_hero = ui_selected_hero;
            lobby_model.phase = lobby.phase;
            lobby_model.local_slot = local_slot;
            lobby_model.ready_count = lobby.ready_count;
            lobby_model.human_count = lobby.human_count;
            lobby_model.player_count = lobby.player_count;
            lobby_model.min_humans = lobby.min_humans;
            lobby_model.countdown_ms = lobby.countdown_ms;
            lobby_model.local_ready = online_client.ready();
            lobby_model.members = collect_lobby_players();
            ui.update_lobby(lobby_model);
        } else {
            const core::Player* local =
                world.players().get(local_player);
            const std::vector<UiPlayerRow> players =
                collect_world_players();
            int local_team = static_cast<int>(local_slot % 2U);
            for (const UiPlayerRow& row : players) {
                if (row.local) {
                    local_team = row.team;
                    break;
                }
            }

            UiMatchModel match_model;
            match_model.online = online_mode;
            match_model.mode = static_cast<int>(world.game_mode());
            match_model.match_over = world.match_over();
            match_model.score0 = world.scores()[0];
            match_model.score1 = world.scores()[1];
            match_model.score_limit = world.score_limit();
            match_model.winner = world.winner();
            match_model.time_left_seconds =
                world.match_ticks() / core::kTickRate;
            match_model.local_alive =
                local != nullptr && local->alive != 0;
            match_model.local_ammo =
                local == nullptr ? 0 : local->ammo;
            match_model.local_health =
                local == nullptr
                    ? 0
                    : local->health.raw() / core::Fix32::kOne;
            match_model.local_max_health = local == nullptr
                ? 100
                : static_cast<int>(
                      core::content().hero(local->hero).health);
            match_model.local_shield =
                local == nullptr ? 0 : local->shield;
            match_model.local_max_shield = local == nullptr
                ? 0
                : static_cast<int>(
                      core::content().hero(local->hero).shield);
            match_model.local_weapon =
                local == nullptr ? 0 : local->weapon;
            match_model.hero_name = local == nullptr
                ? "scout"
                : hero_id_for_index(local->hero);
            match_model.weapon_name = local == nullptr
                ? "rifle"
                : core::content().weapon(local->weapon).id;
            if (local != nullptr) {
                for (std::size_t slot = 0;
                     slot < match_model.gadgets.size();
                     ++slot) {
                    const core::GadgetStats& gadget =
                        core::hero_gadget_stats(
                            static_cast<core::HeroType>(local->hero),
                            slot);
                    UiGadgetSlot& item = match_model.gadgets[slot];
                    item.name = settings.language == "zh"
                        ? std::string(gadget.name_zh)
                        : std::string(gadget.name_en);
                    const int action_index =
                        static_cast<int>(GameAction::Gadget1) +
                        static_cast<int>(slot);
                    item.key = key_name(settings.bindings.get(
                        static_cast<GameAction>(action_index)));
                    const int cooldown =
                        static_cast<int>(local->gadget_cooldowns[slot]);
                    const int total =
                        static_cast<int>(gadget.cooldown_ticks);
                    item.cooldown_seconds =
                        (cooldown + core::kTickRate - 1) /
                        core::kTickRate;
                    item.cooldown_percent = total > 0
                        ? std::clamp(
                              (total - cooldown) * 100 / total,
                              0,
                              100)
                        : 100;
                    item.active = local->gadget_active_ticks[slot] > 0;
                    item.ready = cooldown == 0;
                }
            }
            match_model.respawn_seconds = local == nullptr
                ? 0
                : static_cast<std::uint32_t>(
                      (local->respawn_timer + core::kTickRate - 1) /
                      core::kTickRate);
            match_model.objective_owner = world.objective_owner();
            match_model.objective_capturing =
                world.objective_capturing_team();
            match_model.objective_progress_percent =
                static_cast<int>(
                    static_cast<std::uint32_t>(
                        world.objective_progress()) *
                    100U /
                    core::kObjectiveCaptureTicks);
            match_model.objective_score_percent =
                static_cast<int>(
                    static_cast<std::uint32_t>(
                        world.objective_score_ticks()) *
                    100U /
                    core::kObjectiveScoreTicks);
            match_model.local_carried =
                local == nullptr ? 0 : local->carried_drops;
            match_model.depot_progress_percent = 0;
            if (local != nullptr && local->carried_drops > 0) {
                for (const core::DropDepot& depot :
                     world.map().drop_depots) {
                    if (depot.team != core::kDropDepotNeutral &&
                        depot.team != local->team) {
                        continue;
                    }
                    if (depot.deliver_ticks == 0) {
                        continue;
                    }
                    const std::uint32_t progress =
                        static_cast<std::uint32_t>(
                            local->depot_hold_ticks) *
                        100U /
                        static_cast<std::uint32_t>(depot.deliver_ticks);
                    match_model.depot_progress_percent =
                        static_cast<int>(std::min<std::uint32_t>(
                            progress, 100U));
                    break;
                }
            }
            match_model.fps = fps;
            match_model.damage_flash =
                settings.blood_enabled && damage_flash_seconds > 0.0f
                    ? 0.35f * (damage_flash_seconds / 0.25f)
                    : 0.0f;
            match_model.notice = notice;
            match_model.players = players;
            match_model.feed.reserve(feed_entries.size());
            for (const FeedEntry& entry : feed_entries) {
                match_model.feed.push_back(entry.item);
            }
            if (online_mode) {
                const auto& net_stats = online_client.stats();
                char position_text[64]{};
                if (local != nullptr) {
                    std::snprintf(
                        position_text,
                        sizeof(position_text),
                        "%.2f,%.2f",
                        fix_to_float(local->position.x),
                        fix_to_float(local->position.y));
                } else {
                    std::snprintf(position_text, sizeof(position_text), "?");
                }
                match_model.net_status =
                    "NET SLOT " + std::to_string(net_stats.slot) +
                    " // ACK " + std::to_string(net_stats.ack_tick) +
                    " // RTT " + std::to_string(net_stats.rtt_ms) + "ms" +
                    " // SLAG " + std::to_string(net_stats.server_lag_ticks) +
                    " // ELAG " + std::to_string(net_stats.effective_server_lag_ticks) +
                    " // RTTC " + std::to_string(net_stats.rtt_compensation_ticks) +
                    " // EST " + std::to_string(net_stats.estimated_server_tick) +
                    " // CELAG " + std::to_string(net_stats.client_event_lag_ticks) +
                    " // SYNC " + std::to_string(net_stats.sync_period_ticks) +
                    " // STS " + std::to_string(net_stats.sync_time_to_step_ms) +
                    " // LEAD " + std::to_string(net_stats.input_lead_ticks) +
                    " // REC " + std::to_string(net_stats.recovery_lead_ticks) +
                    " // Q " + std::to_string(net_stats.snapshots_queued) +
                    " // RB " + std::to_string(net_stats.rollbacks) +
                    " // ERB " + std::to_string(net_stats.event_rollbacks) +
                    " // CIN " + std::to_string(net_stats.covered_inputs) +
                    " // SM " + std::to_string(net_stats.snapshot_mismatches) +
                    " // SR " + std::to_string(net_stats.snapshot_restores) +
                    " // RMC " + std::to_string(net_stats.remote_snapshot_corrections) +
                    " // CORR " + std::to_string(net_stats.corrections) +
                    " // TICK " + std::to_string(net_stats.predicted_tick) +
                    " // POS " + position_text +
                    " // OFF " + std::to_string(net_stats.clock_offset_ms) +
                    "ms";
            } else if (net_mode) {
                const auto& net_stats = net_session.client(0).stats();
                match_model.net_status =
                    "LOCAL NET // ACK " +
                    std::to_string(net_stats.ack_tick) +
                    " // RTT " +
                    std::to_string(net_stats.rtt_ticks * 1000U / 60U) +
                    "ms // RB " + std::to_string(net_stats.rollbacks) +
                    " // ERB " + std::to_string(net_stats.event_rollbacks) +
                    " // CIN " + std::to_string(net_stats.covered_inputs) +
                    " // SM " + std::to_string(net_stats.snapshot_mismatches) +
                    " // SR " + std::to_string(net_stats.snapshot_restores) +
                    " // CORR " + std::to_string(net_stats.corrections);
            } else {
                match_model.net_status =
                    "LOCAL // TICK " + std::to_string(world.tick()) +
                    " // TICK RATE " + std::to_string(options.tick_rate);
            }
            ui.update_match(match_model);

            if (ui_screen == UiScreen::Results) {
                const bool have_match_end =
                    online_mode &&
                    online_client.match_end().match_id != 0;
                UiResultsModel results;
                results.online = online_mode;
                results.allow_rematch =
                    !net_mode && (!online_mode || have_match_end);
                results.allow_menu = !online_mode && !net_mode;
                results.local_team = local_team;
                results.players = players;
                if (have_match_end) {
                    const net::MatchEnd& end = online_client.match_end();
                    results.score0 = end.score0;
                    results.score1 = end.score1;
                    results.winner = end.winner;
                } else {
                    results.score0 = world.scores()[0];
                    results.score1 = world.scores()[1];
                    results.winner = world.winner();
                }
                results.score_limit = std::to_string(world.score_limit());
                ui.update_results(results);
            }
        }

        if (reload_notice_seconds > 0.0f) {
            reload_notice_seconds =
                std::max(0.0f, reload_notice_seconds - frame_seconds);
            if (reload_notice_seconds == 0.0f) {
                reload_notice.clear();
            }
        }

        context->Update();

        configure_views(width, height);
        bgfx::touch(0);
        world_renderer.draw(
            program,
            sampler,
            world,
            local_player,
            camera,
            width,
            height,
            frame_seconds);
        context->Render();
        bgfx::frame();

        // Without vsync the loop used to run uncapped (hundreds of FPS). With
        // two clients and the server on one PC that starved the other
        // processes and produced 35-250ms hitches; a hitching client sends
        // its inputs late, the server replays them and every other client
        // has to roll that player back (visible as remote stutter). Cap the
        // frame rate like a normal game frame limiter.
        if (!settings.vsync && options.max_fps > 0) {
            static auto next_frame = core::FixedStepClock::Clock::now();
            const auto frame_budget = std::chrono::nanoseconds(
                1'000'000'000LL / options.max_fps);
            next_frame += frame_budget;
            const auto now_after = core::FixedStepClock::Clock::now();
            if (next_frame > now_after) {
                std::this_thread::sleep_until(next_frame);
            } else if (now_after - next_frame > frame_budget * 4) {
                next_frame = now_after;
            }
        }

        if (options.max_frames > 0 && frame_count >= options.max_frames) {
            running = false;
        }
    }

    const auto loop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        core::FixedStepClock::Clock::now() - loop_start).count();
    const int avg_fps_x10 =
        loop_ms > 0 ? static_cast<int>(frame_count * 10000 / loop_ms) : 0;
    NSYNC_LOG_INFO(
        "client",
        "render stats: frames=",
        frame_count,
        " elapsed_ms=",
        loop_ms,
        " avg_fps_x10=",
        avg_fps_x10);

    ui.shutdown();

    if (online_mode) {
        const auto& net_stats = online_client.stats();
        const auto transport = online_client.transport_stats();
        NSYNC_LOG_INFO(
            "client",
            "online stats: tick=",
            net_stats.predicted_tick,
            " ack=",
            net_stats.ack_tick,
            " inputs=",
            net_stats.input_packets,
            " snapshots=",
            net_stats.snapshot_packets,
            " corrections=",
            net_stats.corrections,
            " desyncs=",
            net_stats.desyncs,
            " snapshot_mismatches=",
            net_stats.snapshot_mismatches,
            " snapshot_missing=",
            net_stats.snapshot_missing_predictions,
            " snapshot_periodic=",
            net_stats.snapshot_periodic_mismatches,
            " snapshot_authoritative=",
            net_stats.snapshot_authoritative_mismatches,
            " snapshot_restores=",
            net_stats.snapshot_restores,
            " event_rollbacks=",
            net_stats.event_rollbacks,
            " rollbacks=",
            net_stats.rollbacks,
            " rtt=",
            net_stats.rtt_ms,
            " server_lag=",
            net_stats.server_lag_ticks,
            " effective_lag=",
            net_stats.effective_server_lag_ticks,
            " rtt_compensation=",
            net_stats.rtt_compensation_ticks,
            " estimated_server_tick=",
            net_stats.estimated_server_tick,
            " client_event_lag=",
            net_stats.client_event_lag_ticks,
            " sync_period=",
            net_stats.sync_period_ticks,
            " sync_time_to_step=",
            net_stats.sync_time_to_step_ms,
            " input_lead=",
            net_stats.input_lead_ticks,
            " recovery_lead=",
            net_stats.recovery_lead_ticks,
            " alignment_wait=",
            net_stats.alignment_wait_ticks,
            " snapshots_queued=",
            net_stats.snapshots_queued,
            " phase=",
            static_cast<int>(net_stats.phase),
            " match=",
            net_stats.match_id,
            " bytes_sent=",
            transport.bytes_sent,
            " bytes_received=",
            transport.bytes_received,
            " packets_sent=",
            transport.packets_sent,
            " packets_received=",
            transport.packets_received);
    }
    online_client.stop();
    Rml::RemoveContext("client");
    Rml::Shutdown();

    ui_renderer.shutdown();
    world_renderer.shutdown();
    audio.shutdown();
    bgfx::destroy(sampler);
    bgfx::destroy(program);
    bgfx::shutdown();

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

} // namespace nsync::client




