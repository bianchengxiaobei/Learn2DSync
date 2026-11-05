#include "client/bgfx_app.h"
#include "client/client_settings.h"
#include "client/localization.h"
#include "shared/config.h"
#include "shared/crash_handler.h"
#include "shared/log.h"
#include "shared/paths.h"
#include "shared/timer_resolution.h"
#include "shared/update_check.h"
#include "shared/version.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void print_usage() {
    std::cout
        << "usage: nsync_client [options]\n"
        << "  --config PATH        load an ini config file\n"
        << "  --frames N           stop after N rendered frames (0 = unlimited)\n"
        << "  --ticks N            stop after N fixed ticks (0 = unlimited)\n"
        << "  --width N            window width (default 1280)\n"
        << "  --height N           window height (default 720)\n"
        << "  --tick-rate N        fixed ticks per second (default 60)\n"
        << "  --max-catchup N      max fixed steps per frame (default 5)\n"
        << "  --vsync / --no-vsync enable or disable vsync\n"
        << "  --max-fps N          frame limit without vsync (default 144, 0 = off)\n"
        << "  --fullscreen        start fullscreen\n"
        << "  --windowed          start windowed\n"
        << "  --blood / --no-blood enable or disable blood hit effects\n"
        << "  --camera-zoom N      camera zoom (1.0 = original, >1 zooms out)\n"
        << "  --settings          open the settings overlay at startup\n"
        << "  --simulate-click     synthesize one RmlUi menu click at startup\n"
        << "  --menu               start in the offline main menu\n"
        << "  --no-menu            skip the main menu and start the local match\n"
        << "  --hot-reload         watch and reload source assets (F5 reloads once)\n"
        << "  --no-hot-reload      disable the asset watcher\n"
        << "  --benchmark-heroes   run the 10-hero original-art CPU benchmark and exit\n"
        << "  --gpu-vendor NAME    auto|intel|nvidia|amd|microsoft|software\n"
        << "  --audio              enable sound effects (default)\n"
        << "  --no-audio           disable sound effects\n"
        << "  --audio-volume N     master SFX volume percent (0-100)\n"
        << "  --language CODE      ui language: en|zh (default zh)\n"
        << "  --net                use the local authoritative network testbed\n"
        << "  --connect            connect to an authoritative UDP/KCP server\n"
        << "  --server-host HOST   server host for --connect (default 127.0.0.1)\n"
        << "  --server-port N      server port for --connect (default 40100)\n"
        << "  --name NAME          fake login display name\n"
        << "  --hero NAME          hero id from content data (default scout)\n"
        << "  --hero-art MODE      auto|placeholder|original (default auto)\n"
        << "  --no-auto-ready      do not automatically ready in the lobby\n"
        << "  --auto-rematch       ready for another match after results\n"
        << "  --no-client-lag      disable server-lag local simulation\n"
        << "  --no-fixed-step-scheduler  disable estimated-server-tick step scheduling\n"
        << "  --net-latency-ms N   simulated RTT for --net (default 150)\n"
        << "  --net-jitter-ms N    simulated one-way jitter for --net (default 30)\n"
        << "  --net-loss N         packet loss percent for --net\n"
        << "  --net-duplicate N    packet duplicate percent for --net\n"
        << "  --input-delay-ticks N  fallback input lead if server timing is absent (default 3)\n"
        << "  --map PATH           map json/bin virtual path (default maps/arena_01.json)\n"
        << "  --mode MODE          tdm|control|crystal for direct play (default tdm)\n"
        << "  --log-level LEVEL    trace|debug|info|warn|error|off\n"
        << "  --version            print version and exit\n"
        << "  --check-update       check update.url/--update-url and exit\n"
        << "  --update-url URL     update manifest URL or file path\n"
        << "  --help               show this help\n";
}

bool parse_int(const char* text, int& value) {
    if (text == nullptr || *text == '\0') {
        return false;
    }
    char* end = nullptr;
    const long parsed = std::strtol(text, &end, 10);
    if (end == text || *end != '\0') {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path config_path;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 ||
            std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        }
        if (std::strcmp(argv[i], "--version") == 0) {
            std::cout << "nsync_client " << nsync::shared::version_string()
                      << "\n";
            return 0;
        }
        if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        }
    }

    if (config_path.empty()) {
        if (const auto found = nsync::shared::paths::find_config("client.ini")) {
            config_path = *found;
        }
    }

    nsync::shared::Config config;
    std::string config_error;
    if (!config_path.empty()) {
        if (!config.load_file(config_path, config_error)) {
            config_error = "config not loaded: " + config_error;
        }
    }

    std::filesystem::path settings_path;
    if (!config_path.empty()) {
        settings_path = config_path.parent_path() / "client.settings.ini";
    } else if (const auto source =
                   nsync::shared::paths::source_dir();
               !source.empty()) {
        settings_path = source / "config" / "client.settings.ini";
    } else {
        settings_path = nsync::shared::paths::executable_dir() /
            "config" / "client.settings.ini";
    }

    std::error_code settings_exists_error;
    if (!settings_path.empty() &&
        std::filesystem::exists(settings_path, settings_exists_error)) {
        nsync::shared::Config saved_settings;
        std::string settings_error;
        if (saved_settings.load_file(settings_path, settings_error)) {
            config.merge(saved_settings);
        } else {
            NSYNC_LOG_WARN(
                "client",
                "settings not loaded: ",
                settings_error);
        }
    }

    nsync::shared::LogLevel level = nsync::shared::LogLevel::Info;
    if (!nsync::shared::parse_log_level(
            config.get_string("log.level", "info"),
            level)) {
        level = nsync::shared::LogLevel::Info;
    }
    nsync::shared::set_log_level(level);
    const std::filesystem::path crash_dir =
        nsync::shared::paths::executable_dir() / "crash";
    nsync::shared::install_crash_handler("nsync_client", crash_dir);
    const nsync::shared::HighResolutionTimer timer_resolution;
    NSYNC_LOG_INFO(
        "version",
        "nsync_client ",
        nsync::shared::version_string());

    if (!config_error.empty()) {
        NSYNC_LOG_WARN("client", config_error);
    }

    bool simulate_click = config.get_bool("client.simulate_click", false);
    bool menu_explicit = false;
    bool show_settings = false;
    bool check_update = false;

    for (int i = 1; i < argc; ++i) {
        const char* argument = argv[i];
        auto require_value = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                NSYNC_LOG_ERROR("client", "missing value for ", name);
                return nullptr;
            }
            return argv[++i];
        };

        if (std::strcmp(argument, "--config") == 0) {
            if (require_value(argument) == nullptr) {
                return 1;
            }
            continue;
        }
        if (std::strcmp(argument, "--help") == 0 ||
            std::strcmp(argument, "-h") == 0) {
            print_usage();
            return 0;
        }
        if (std::strcmp(argument, "--simulate-click") == 0) {
            simulate_click = true;
            config.set("client.simulate_click", "true");
            continue;
        }
        if (std::strcmp(argument, "--menu") == 0) {
            menu_explicit = true;
            config.set("client.show_menu", "true");
            continue;
        }
        if (std::strcmp(argument, "--no-menu") == 0) {
            menu_explicit = true;
            config.set("client.show_menu", "false");
            continue;
        }
        if (std::strcmp(argument, "--hot-reload") == 0) {
            config.set("client.hot_reload", "true");
            continue;
        }
        if (std::strcmp(argument, "--no-hot-reload") == 0) {
            config.set("client.hot_reload", "false");
            continue;
        }
        if (std::strcmp(argument, "--benchmark-heroes") == 0) {
            config.set("client.benchmark_heroes", "true");
            continue;
        }
        if (std::strcmp(argument, "--gpu-vendor") == 0) {
            const char* value = require_value(argument);
            if (value == nullptr) {
                NSYNC_LOG_ERROR("client", "missing --gpu-vendor value");
                return 1;
            }
            config.set("client.gpu_vendor", value);
            continue;
        }
        if (std::strcmp(argument, "--audio") == 0) {
            config.set("audio.enabled", "true");
            continue;
        }
        if (std::strcmp(argument, "--no-audio") == 0) {
            config.set("audio.enabled", "false");
            continue;
        }
        if (std::strcmp(argument, "--audio-volume") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed) ||
                parsed < 0 || parsed > 100) {
                NSYNC_LOG_ERROR("client", "invalid --audio-volume value");
                return 1;
            }
            config.set("audio.volume", value);
            continue;
        }
        if (std::strcmp(argument, "--language") == 0) {
            const char* value = require_value(argument);
            if (value == nullptr || *value == '\0') {
                NSYNC_LOG_ERROR("client", "invalid --language value");
                return 1;
            }
            config.set(
                "client.language",
                nsync::client::language_code(
                    nsync::client::parse_language(value)));
            continue;
        }
        if (std::strcmp(argument, "--net") == 0) {
            config.set("client.net", "true");
            continue;
        }
        if (std::strcmp(argument, "--connect") == 0) {
            config.set("client.connect", "true");
            continue;
        }
        if (std::strcmp(argument, "--server-host") == 0 && i + 1 < argc) {
            config.set("client.server_host", argv[++i]);
            continue;
        }
        if (std::strcmp(argument, "--name") == 0 && i + 1 < argc) {
            config.set("client.name", argv[++i]);
            continue;
        }
        if (std::strcmp(argument, "--hero") == 0 && i + 1 < argc) {
            config.set("client.hero", argv[++i]);
            continue;
        }
        if (std::strcmp(argument, "--hero-art") == 0) {
            const char* value = require_value(argument);
            if (value == nullptr ||
                (std::strcmp(value, "auto") != 0 &&
                 std::strcmp(value, "placeholder") != 0 &&
                 std::strcmp(value, "original") != 0)) {
                NSYNC_LOG_ERROR("client", "invalid --hero-art value");
                return 1;
            }
            config.set("client.hero_art", value);
            continue;
        }
        if (std::strcmp(argument, "--no-auto-ready") == 0) {
            config.set("client.auto_ready", "false");
            continue;
        }
        if (std::strcmp(argument, "--auto-rematch") == 0) {
            config.set("client.auto_rematch", "true");
            continue;
        }
        if (std::strcmp(argument, "--client-lag") == 0) {
            config.set("client.client_step_lag", "true");
            continue;
        }
        if (std::strcmp(argument, "--no-client-lag") == 0) {
            config.set("client.client_step_lag", "false");
            continue;
        }
        if (std::strcmp(argument, "--fixed-step-scheduler") == 0) {
            config.set("client.fixed_step_scheduler", "true");
            continue;
        }
        if (std::strcmp(argument, "--no-fixed-step-scheduler") == 0) {
            config.set("client.fixed_step_scheduler", "false");
            continue;
        }
        if (std::strcmp(argument, "--server-port") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed) ||
                parsed < 1 || parsed > 65535) {
                NSYNC_LOG_ERROR("client", "invalid --server-port value");
                return 1;
            }
            config.set("client.server_port", value);
            continue;
        }
        if (std::strcmp(argument, "--net-latency-ms") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed)) {
                NSYNC_LOG_ERROR("client", "invalid --net-latency-ms value");
                return 1;
            }
            config.set("client.net_latency_ms", value);
            continue;
        }
        if (std::strcmp(argument, "--net-jitter-ms") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed)) {
                NSYNC_LOG_ERROR("client", "invalid --net-jitter-ms value");
                return 1;
            }
            config.set("client.net_jitter_ms", value);
            continue;
        }
        if (std::strcmp(argument, "--net-loss") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed)) {
                NSYNC_LOG_ERROR("client", "invalid --net-loss value");
                return 1;
            }
            config.set("client.net_loss_percent", value);
            continue;
        }
        if (std::strcmp(argument, "--net-duplicate") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed)) {
                NSYNC_LOG_ERROR("client", "invalid --net-duplicate value");
                return 1;
            }
            config.set("client.net_duplicate_percent", value);
            continue;
        }
        if (std::strcmp(argument, "--input-delay-ticks") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed) ||
                parsed < 2 || parsed > 60) {
                NSYNC_LOG_ERROR(
                    "client",
                    "invalid --input-delay-ticks value");
                return 1;
            }
            config.set("client.input_delay_ticks", value);
            continue;
        }
        if (std::strcmp(argument, "--map") == 0) {
            const char* value = require_value(argument);
            if (value == nullptr || *value == '\0') {
                NSYNC_LOG_ERROR("client", "invalid --map value");
                return 1;
            }
            config.set("client.map", value);
            continue;
        }
        if (std::strcmp(argument, "--mode") == 0) {
            const char* value = require_value(argument);
            if (value == nullptr ||
                (std::strcmp(value, "tdm") != 0 &&
                 std::strcmp(value, "control") != 0 &&
                 std::strcmp(value, "crystal") != 0)) {
                NSYNC_LOG_ERROR("client", "invalid --mode value");
                return 1;
            }
            config.set("client.mode", value);
            continue;
        }
        if (std::strcmp(argument, "--vsync") == 0) {
            config.set("client.vsync", "true");
            continue;
        }
        if (std::strcmp(argument, "--no-vsync") == 0) {
            config.set("client.vsync", "false");
            continue;
        }
        if (std::strcmp(argument, "--max-fps") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed) ||
                parsed < 0 || parsed > 1000) {
                NSYNC_LOG_ERROR("client", "invalid --max-fps value");
                return 1;
            }
            config.set("client.max_fps", value);
            continue;
        }
        if (std::strcmp(argument, "--fullscreen") == 0) {
            config.set("client.fullscreen", "true");
            continue;
        }
        if (std::strcmp(argument, "--windowed") == 0) {
            config.set("client.fullscreen", "false");
            continue;
        }
        if (std::strcmp(argument, "--blood") == 0) {
            config.set("client.blood", "true");
            continue;
        }
        if (std::strcmp(argument, "--no-blood") == 0) {
            config.set("client.blood", "false");
            continue;
        }
        if (std::strcmp(argument, "--camera-zoom") == 0) {
            const char* value = require_value(argument);
            if (value == nullptr) {
                NSYNC_LOG_ERROR("client", "invalid --camera-zoom value");
                return 1;
            }
            config.set("client.camera_zoom", value);
            continue;
        }
        if (std::strcmp(argument, "--settings") == 0) {
            show_settings = true;
            continue;
        }
        if (std::strcmp(argument, "--frames") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed)) {
                NSYNC_LOG_ERROR("client", "invalid --frames value");
                return 1;
            }
            config.set("client.max_frames", value);
            continue;
        }
        if (std::strcmp(argument, "--ticks") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed)) {
                NSYNC_LOG_ERROR("client", "invalid --ticks value");
                return 1;
            }
            config.set("client.max_ticks", value);
            continue;
        }
        if (std::strcmp(argument, "--width") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed)) {
                NSYNC_LOG_ERROR("client", "invalid --width value");
                return 1;
            }
            config.set("client.width", value);
            continue;
        }
        if (std::strcmp(argument, "--height") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed)) {
                NSYNC_LOG_ERROR("client", "invalid --height value");
                return 1;
            }
            config.set("client.height", value);
            continue;
        }
        if (std::strcmp(argument, "--tick-rate") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed)) {
                NSYNC_LOG_ERROR("client", "invalid --tick-rate value");
                return 1;
            }
            config.set("simulation.tick_rate", value);
            continue;
        }
        if (std::strcmp(argument, "--max-catchup") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed)) {
                NSYNC_LOG_ERROR("client", "invalid --max-catchup value");
                return 1;
            }
            config.set("simulation.max_catchup_steps", value);
            continue;
        }
        if (std::strcmp(argument, "--check-update") == 0) {
            check_update = true;
            continue;
        }
        if (std::strcmp(argument, "--update-url") == 0) {
            const char* value = require_value(argument);
            if (value == nullptr) {
                return 1;
            }
            config.set("update.url", value);
            continue;
        }
        if (std::strcmp(argument, "--log-level") == 0) {
            const char* value = require_value(argument);
            if (value == nullptr) {
                return 1;
            }
            if (!nsync::shared::parse_log_level(value, level)) {
                NSYNC_LOG_ERROR("client", "invalid --log-level value: ", value);
                return 1;
            }
            nsync::shared::set_log_level(level);
            continue;
        }

        NSYNC_LOG_ERROR("client", "unknown option: ", argument);
        print_usage();
        return 1;
    }

    nsync::client::ClientSettings client_settings;
    nsync::client::load_client_settings(config, client_settings);
    NSYNC_LOG_DEBUG(
        "client",
        "settings loaded: ",
        client_settings.width,
        "x",
        client_settings.height,
        " fullscreen=",
        client_settings.fullscreen ? "true" : "false",
        " audio=",
        client_settings.audio_enabled ? "true" : "false",
        " volume=",
        client_settings.master_volume,
        " language=",
        client_settings.language,
        " jump=",
        nsync::client::key_name(
            client_settings.bindings.get(
                nsync::client::GameAction::Jump)));

    nsync::client::ClientOptions options;
    options.width = client_settings.width;
    options.height = client_settings.height;
    options.vsync = client_settings.vsync;
    options.max_fps = config.get_int("client.max_fps", 144, 0, 1000);
    options.fullscreen = client_settings.fullscreen;
    options.blood_enabled = client_settings.blood_enabled;
    try {
        options.camera_zoom = std::stof(
            config.get_string("client.camera_zoom", "1.0"));
    } catch (...) {
        NSYNC_LOG_WARN("client", "invalid client.camera_zoom; using 1.0");
        options.camera_zoom = 1.0f;
    }
    options.camera_zoom = std::clamp(options.camera_zoom, 0.4f, 2.5f);
    options.show_settings = show_settings;
    options.key_bindings = client_settings.bindings;
    options.settings_path = settings_path;
    options.tick_rate = config.get_int("simulation.tick_rate", 60, 1, 240);
    options.max_catchup_steps =
        config.get_int("simulation.max_catchup_steps", 5, 1, 60);
    options.max_frames = config.get_int("client.max_frames", 0, 0, 10000000);
    options.max_ticks = config.get_int("client.max_ticks", 0, 0, 100000000);
    options.simulate_click = simulate_click;
    options.show_menu = config.get_bool("client.show_menu", true);
    options.menu_explicit = menu_explicit;
    options.hot_reload = config.get_bool("client.hot_reload", false);
    options.benchmark_heroes =
        config.get_bool("client.benchmark_heroes", false);
    options.gpu_vendor = config.get_string("client.gpu_vendor", "auto");
    options.audio_enabled = client_settings.audio_enabled;
    options.master_volume =
        static_cast<float>(client_settings.master_volume) / 100.0f;
    options.language = client_settings.language;
    options.net = config.get_bool("client.net", false);
    options.connect = config.get_bool("client.connect", false);
    options.auto_ready = config.get_bool("client.auto_ready", true);
    options.auto_rematch = config.get_bool("client.auto_rematch", false);
    options.client_lag = config.get_bool("client.client_step_lag", false);
    options.fixed_step_scheduler =
        config.get_bool("client.fixed_step_scheduler", false);
    options.player_name = config.get_string("client.name", "player");
    options.hero = config.get_string("client.hero", "scout");
    if (options.hero.empty()) {
        NSYNC_LOG_ERROR("client", "hero name is empty");
        return 1;
    }
    options.hero_art = config.get_string("client.hero_art", "auto");
    if (options.hero_art != "auto" &&
        options.hero_art != "placeholder" &&
        options.hero_art != "original") {
        NSYNC_LOG_ERROR(
            "client",
            "invalid client.hero_art: ",
            options.hero_art);
        return 1;
    }
    options.server_host =
        config.get_string("client.server_host", "127.0.0.1");
    options.server_port =
        config.get_int("client.server_port", 40100, 1, 65535);
    options.net_latency_ms =
        config.get_int("client.net_latency_ms", 150, 0, 5000);
    options.net_jitter_ms =
        config.get_int("client.net_jitter_ms", 30, 0, 1000);
    options.net_loss_percent =
        config.get_int("client.net_loss_percent", 0, 0, 100);
    options.net_duplicate_percent =
        config.get_int("client.net_duplicate_percent", 0, 0, 100);
    options.input_delay_ticks =
        config.get_int("client.input_delay_ticks", 3, 2, 60);
    options.map = config.get_string("client.map", "maps/arena_01.json");
    const std::string mode = config.get_string("client.mode", "tdm");
    if (mode == "control") {
        options.game_mode = nsync::core::GameMode::Control;
    } else if (mode == "crystal") {
        options.game_mode = nsync::core::GameMode::Crystal;
    } else if (mode != "tdm") {
        NSYNC_LOG_ERROR("client", "invalid client.mode: ", mode);
        return 1;
    }

    if (check_update) {
        const std::string source = config.get_string("update.url", "");
        if (source.empty()) {
            std::fprintf(
                stderr,
                "update.url is empty; pass --update-url URL\n");
            return 2;
        }
        nsync::shared::UpdateCheckResult result;
        std::string update_error;
        if (!nsync::shared::check_for_update(
                source,
                nsync::shared::kVersion,
                result,
                &update_error)) {
            std::fprintf(stderr, "update check failed: %s\n", update_error.c_str());
            return 2;
        }
        std::printf(
            "update current=%s latest=%s available=%d\n",
            result.current.c_str(),
            result.latest.c_str(),
            result.update_available ? 1 : 0);
        if (!result.url.empty()) {
            std::printf("url=%s\n", result.url.c_str());
        }
        if (!result.notes.empty()) {
            std::printf("notes=%s\n", result.notes.c_str());
        }
        return result.update_available ? 1 : 0;
    }

    return nsync::client::run_client(options);
}

