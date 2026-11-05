#include "server/server_app.h"
#include "shared/config.h"
#include "shared/crash_handler.h"
#include "shared/log.h"
#include "shared/paths.h"
#include "shared/timer_resolution.h"
#include "shared/update_check.h"
#include "shared/version.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

void print_usage() {
    std::cout
        << "usage: nsync_server [options]\n"
        << "  --config PATH        load an ini config file\n"
        << "  --ticks N            stop after N fixed ticks (0 = run until Ctrl+C)\n"
        << "  --tick-rate N        fixed ticks per second (default 60)\n"
        << "  --max-catchup N      max fixed steps per loop (default 5)\n"
        << "  --net                run the UDP/KCP authoritative server\n"
        << "  --port N             UDP listen port (default 40100)\n"
        << "  --players N          server player slots (default 2)\n"
        << "  --map PATH           map json/bin, assets-relative or direct path\n"
        << "  --map-rotation LIST  comma-separated maps, one per match\n"
        << "  --seed N             simulation seed (default 1)\n"
        << "  --snapshot-interval N snapshot interval in ticks (default 3)\n"
        << "  --input-sync-model M original|legacy (default original)\n"
        << "  --server-lag-ticks N client simulation lag in ticks (default 8)\n"
        << "  --client-event-lag-ticks N client event lead (default 3)\n"
        << "  --sync-period-ticks N client sync period (default 30)\n"
        << "  --sync-time-to-step-ms N client step-time correction (default 0)\n"
        << "  --late-input-replay  replay late inputs at their target tick\n"
        << "  --no-late-input-replay  reject/clamp late inputs instead (default)\n"
        << "  --late-input-replay-window N replay window in ticks (default 120)\n"
        << "  --match-duration-ticks N match length (default 18000)\n"
        << "  --score-limit N      match score limit (default 30)\n"
        << "  --mode MODE          tdm|control|crystal (default tdm)\n"
        << "  --lobby              enable fake-login lobby and matchmaking\n"
        << "  --no-lobby           start the match immediately at login\n"
        << "  --lobby-countdown-ms N countdown after all humans ready (default 3000)\n"
        << "  --match-end-delay-ms N result screen duration (default 3000)\n"
        << "  --lobby-min-humans N minimum connected humans (default 1)\n"
        << "  --log-level LEVEL    trace|debug|info|warn|error|off\n"
        << "  --version            print version and exit\n"
        << "  --check-update       check update.url/--update-url and exit\n"
        << "  --update-url URL     update manifest URL or file path\n"
        << "  --crash-test         internal: trigger a crash report check\n"
        << "  --help               show this help\n";
}

std::vector<std::string> parse_map_list(const std::string& text) {
    std::vector<std::string> maps;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(',', start);
        const std::size_t length =
            end == std::string::npos ? text.size() - start : end - start;
        std::string item = text.substr(start, length);
        const std::size_t first = item.find_first_not_of(" \t\r\n");
        const std::size_t last = item.find_last_not_of(" \t\r\n");
        if (first != std::string::npos) {
            maps.push_back(item.substr(first, last - first + 1));
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return maps;
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
            std::cout << "nsync_server " << nsync::shared::version_string()
                      << "\n";
            return 0;
        }
        if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        }
    }

    if (config_path.empty()) {
        if (const auto found = nsync::shared::paths::find_config("server.ini")) {
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

    nsync::shared::LogLevel level = nsync::shared::LogLevel::Info;
    if (!nsync::shared::parse_log_level(
            config.get_string("log.level", "info"),
            level)) {
        level = nsync::shared::LogLevel::Info;
    }
    nsync::shared::set_log_level(level);
    const std::filesystem::path crash_dir =
        nsync::shared::paths::executable_dir() / "crash";
    nsync::shared::install_crash_handler("nsync_server", crash_dir);
    const nsync::shared::HighResolutionTimer timer_resolution;
    NSYNC_LOG_INFO(
        "version",
        "nsync_server ",
        nsync::shared::version_string());

    if (!config_error.empty()) {
        NSYNC_LOG_WARN("server", config_error);
    }

    bool crash_test = false;
    bool check_update = false;
    for (int i = 1; i < argc; ++i) {
        const char* argument = argv[i];
        auto require_value = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                NSYNC_LOG_ERROR("server", "missing value for ", name);
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
        if (std::strcmp(argument, "--ticks") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed)) {
                NSYNC_LOG_ERROR("server", "invalid --ticks value");
                return 1;
            }
            config.set("server.max_ticks", value);
            continue;
        }
        if (std::strcmp(argument, "--tick-rate") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed)) {
                NSYNC_LOG_ERROR("server", "invalid --tick-rate value");
                return 1;
            }
            config.set("server.tick_rate", value);
            continue;
        }
        if (std::strcmp(argument, "--max-catchup") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed)) {
                NSYNC_LOG_ERROR("server", "invalid --max-catchup value");
                return 1;
            }
            config.set("server.max_catchup_steps", value);
            continue;
        }
        if (std::strcmp(argument, "--net") == 0) {
            config.set("server.net", "true");
            continue;
        }
        if (std::strcmp(argument, "--port") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed) ||
                parsed < 1 || parsed > 65535) {
                NSYNC_LOG_ERROR("server", "invalid --port value");
                return 1;
            }
            config.set("server.port", value);
            continue;
        }
        if (std::strcmp(argument, "--players") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed) ||
                parsed < 1 || parsed > 10) {
                NSYNC_LOG_ERROR("server", "invalid --players value");
                return 1;
            }
            config.set("server.players", value);
            continue;
        }
        if (std::strcmp(argument, "--map") == 0) {
            const char* value = require_value(argument);
            if (value == nullptr || *value == '\0') {
                NSYNC_LOG_ERROR("server", "invalid --map value");
                return 1;
            }
            config.set("server.map", value);
            config.set("server.map_rotation", "");
            continue;
        }
        if (std::strcmp(argument, "--map-rotation") == 0) {
            const char* value = require_value(argument);
            if (value == nullptr || *value == '\0') {
                NSYNC_LOG_ERROR("server", "invalid --map-rotation value");
                return 1;
            }
            config.set("server.map_rotation", value);
            continue;
        }
        if (std::strcmp(argument, "--seed") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed) || parsed < 0) {
                NSYNC_LOG_ERROR("server", "invalid --seed value");
                return 1;
            }
            config.set("server.seed", value);
            continue;
        }
        if (std::strcmp(argument, "--snapshot-interval") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed) ||
                parsed < 1 || parsed > 60) {
                NSYNC_LOG_ERROR("server", "invalid --snapshot-interval value");
                return 1;
            }
            config.set("server.snapshot_interval", value);
            continue;
        }
        if (std::strcmp(argument, "--input-sync-model") == 0) {
            const char* value = require_value(argument);
            if (value == nullptr ||
                (std::strcmp(value, "original") != 0 &&
                 std::strcmp(value, "legacy") != 0)) {
                NSYNC_LOG_ERROR("server", "invalid --input-sync-model value");
                return 1;
            }
            config.set("server.input_sync_model", value);
            continue;
        }
        if (std::strcmp(argument, "--server-lag-ticks") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed) ||
                parsed < 0 || parsed > 60) {
                NSYNC_LOG_ERROR(
                    "server", "invalid --server-lag-ticks value");
                return 1;
            }
            config.set("server.server_lag_ticks", value);
            continue;
        }
        if (std::strcmp(argument, "--client-event-lag-ticks") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed) ||
                parsed < 0 || parsed > 10) {
                NSYNC_LOG_ERROR(
                    "server", "invalid --client-event-lag-ticks value");
                return 1;
            }
            config.set("server.client_event_lag_ticks", value);
            continue;
        }
        if (std::strcmp(argument, "--sync-period-ticks") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed) ||
                parsed < 0 || parsed > 240) {
                NSYNC_LOG_ERROR(
                    "server", "invalid --sync-period-ticks value");
                return 1;
            }
            config.set("server.sync_period_ticks", value);
            continue;
        }
        if (std::strcmp(argument, "--sync-time-to-step-ms") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed) ||
                parsed < -2000 || parsed > 2000) {
                NSYNC_LOG_ERROR(
                    "server", "invalid --sync-time-to-step-ms value");
                return 1;
            }
            config.set("server.sync_time_to_step_ms", value);
            continue;
        }
        if (std::strcmp(argument, "--late-input-replay") == 0) {
            config.set("server.late_input_replay", "true");
            continue;
        }
        if (std::strcmp(argument, "--no-late-input-replay") == 0) {
            config.set("server.late_input_replay", "false");
            continue;
        }
        if (std::strcmp(argument, "--late-input-replay-window") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed) ||
                parsed < 1 || parsed > 600) {
                NSYNC_LOG_ERROR(
                    "server", "invalid --late-input-replay-window value");
                return 1;
            }
            config.set("server.late_input_replay_window_ticks", value);
            continue;
        }
        if (std::strcmp(argument, "--match-duration-ticks") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed) ||
                parsed < 1 || parsed > 10000000) {
                NSYNC_LOG_ERROR(
                    "server", "invalid --match-duration-ticks value");
                return 1;
            }
            config.set("server.match_duration_ticks", value);
            continue;
        }
        if (std::strcmp(argument, "--score-limit") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed) ||
                parsed < 1 || parsed > 1000) {
                NSYNC_LOG_ERROR("server", "invalid --score-limit value");
                return 1;
            }
            config.set("server.score_limit", value);
            continue;
        }
        if (std::strcmp(argument, "--mode") == 0) {
            const char* value = require_value(argument);
            if (value == nullptr ||
                (std::strcmp(value, "tdm") != 0 &&
                 std::strcmp(value, "control") != 0 &&
                 std::strcmp(value, "crystal") != 0)) {
                NSYNC_LOG_ERROR("server", "invalid --mode value");
                return 1;
            }
            config.set("server.mode", value);
            continue;
        }
        if (std::strcmp(argument, "--lobby") == 0) {
            config.set("server.lobby", "true");
            continue;
        }
        if (std::strcmp(argument, "--no-lobby") == 0) {
            config.set("server.lobby", "false");
            continue;
        }
        if (std::strcmp(argument, "--lobby-countdown-ms") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed) ||
                parsed < 0 || parsed > 60000) {
                NSYNC_LOG_ERROR(
                    "server", "invalid --lobby-countdown-ms value");
                return 1;
            }
            config.set("server.lobby_countdown_ms", value);
            continue;
        }
        if (std::strcmp(argument, "--match-end-delay-ms") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed) ||
                parsed < 0 || parsed > 60000) {
                NSYNC_LOG_ERROR(
                    "server", "invalid --match-end-delay-ms value");
                return 1;
            }
            config.set("server.match_end_delay_ms", value);
            continue;
        }
        if (std::strcmp(argument, "--lobby-min-humans") == 0) {
            const char* value = require_value(argument);
            int parsed = 0;
            if (value == nullptr || !parse_int(value, parsed) ||
                parsed < 1 || parsed > 10) {
                NSYNC_LOG_ERROR(
                    "server", "invalid --lobby-min-humans value");
                return 1;
            }
            config.set("server.lobby_min_humans", value);
            continue;
        }
        if (std::strcmp(argument, "--crash-test") == 0) {
            crash_test = true;
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
                NSYNC_LOG_ERROR("server", "invalid --log-level value: ", value);
                return 1;
            }
            nsync::shared::set_log_level(level);
            continue;
        }

        NSYNC_LOG_ERROR("server", "unknown option: ", argument);
        print_usage();
        return 1;
    }

    if (crash_test) {
        nsync::shared::set_last_snapshot(777, 0x0123456789ABCDEFULL);
        NSYNC_LOG_INFO("server", "crash handler test requested");
        std::cout.flush();
        std::cerr.flush();
#ifdef _WIN32
        RaiseException(
            0xC0000005UL,
            EXCEPTION_NONCONTINUABLE,
            0,
            nullptr);
#else
        std::abort();
#endif
    }

    nsync::server::ServerOptions options;
    const std::string mode = config.get_string("server.mode", "tdm");
    if (mode == "control") {
        options.game_mode = nsync::core::GameMode::Control;
    } else if (mode == "crystal") {
        options.game_mode = nsync::core::GameMode::Crystal;
    } else if (mode == "tdm") {
        options.game_mode = nsync::core::GameMode::Tdm;
    } else {
        NSYNC_LOG_ERROR("server", "invalid server.mode: ", mode);
        return 1;
    }
    options.tick_rate = config.get_int("server.tick_rate", 60, 1, 240);
    options.max_catchup_steps =
        config.get_int("server.max_catchup_steps", 5, 1, 60);
    options.max_ticks = config.get_int("server.max_ticks", 0, 0, 100000000);
    options.net = config.get_bool("server.net", false);
    options.port = config.get_int("server.port", 40100, 1, 65535);
    options.players = config.get_int("server.players", 2, 1, 10);
    options.map = config.get_string("server.map", "maps/arena_01.json");
    options.map_rotation = parse_map_list(
        config.get_string("server.map_rotation", ""));
    options.seed = static_cast<std::uint64_t>(
        config.get_int("server.seed", 1, 0, 2147483647));
    options.snapshot_interval =
        config.get_int("server.snapshot_interval", 3, 1, 60);
    const std::string input_sync_model =
        config.get_string("server.input_sync_model", "original");
    if (input_sync_model == "legacy") {
        options.original_input_sync = false;
    } else if (input_sync_model == "original") {
        options.original_input_sync = true;
    } else {
        NSYNC_LOG_ERROR(
            "server",
            "invalid server.input_sync_model: ",
            input_sync_model);
        return 1;
    }
    options.server_lag_ticks =
        config.get_int("server.server_lag_ticks", 8, 0, 60);
    options.client_event_lag_ticks =
        config.get_int("server.client_event_lag_ticks", 3, 0, 10);
    options.sync_period_ticks =
        config.get_int("server.sync_period_ticks", 30, 0, 240);
    options.sync_time_to_step_ms =
        config.get_int("server.sync_time_to_step_ms", 0, -2000, 2000);
    options.late_input_replay =
        config.get_bool("server.late_input_replay", false);
    options.late_input_replay_window_ticks =
        config.get_int("server.late_input_replay_window_ticks", 120, 1, 600);
    options.match_duration_ticks =
        config.get_int("server.match_duration_ticks", 18000, 1, 10000000);
    options.score_limit =
        config.get_int("server.score_limit", 30, 1, 1000);
    options.lobby = config.get_bool("server.lobby", true);
    options.lobby_countdown_ms =
        config.get_int("server.lobby_countdown_ms", 3000, 0, 60000);
    options.match_end_delay_ms =
        config.get_int("server.match_end_delay_ms", 3000, 0, 60000);
    options.lobby_min_humans =
        config.get_int("server.lobby_min_humans", 1, 1, options.players);

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

    return nsync::server::run_server(options);
}


