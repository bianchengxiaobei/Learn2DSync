#pragma once

#include "client/client_settings.h"
#include "core/world.h"

#include <filesystem>
#include <string>

namespace nsync::client {

struct ClientOptions {
    int width = 1280;
    int height = 720;
    bool vsync = true;
    // Frame limiter used when vsync is off (0 = uncapped).
    int max_fps = 144;
    bool fullscreen = false;
    bool blood_enabled = true;
    // 1.0 matches the original CameraScale; larger values zoom out.
    float camera_zoom = 1.0f;
    bool show_settings = false;
    KeyBindings key_bindings = default_key_bindings();
    std::filesystem::path settings_path;
    int tick_rate = 60;
    int max_catchup_steps = 5;
    int max_frames = 0;
    int max_ticks = 0;
    bool simulate_click = false;
    bool show_menu = true;
    bool menu_explicit = false;
    bool hot_reload = false;
    bool benchmark_heroes = false;
    std::string gpu_vendor = "auto";
    bool audio_enabled = true;
    float master_volume = 0.8f;
    std::string language = "zh";
    bool net = false;
    bool connect = false;
    bool auto_ready = true;
    bool auto_rematch = false;
    bool client_lag = false;
    bool fixed_step_scheduler = false;
    std::string player_name = "player";
    std::string hero = "scout";
    std::string hero_art = "auto";
    std::string server_host = "127.0.0.1";
    int server_port = 40100;
    int net_latency_ms = 150;
    int net_jitter_ms = 30;
    int net_loss_percent = 0;
    int net_duplicate_percent = 0;
    int input_delay_ticks = 3;
    std::string map = "maps/arena_01.json";
    core::GameMode game_mode = core::GameMode::Tdm;
};

int run_client(const ClientOptions& options);

} // namespace nsync::client

