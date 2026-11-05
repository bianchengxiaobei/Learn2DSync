#pragma once

#include "client/localization.h"

#include <RmlUi/Core.h>

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace nsync::client {

enum class UiScreen {
    Menu,
    Lobby,
    Match,
    Paused,
    Results,
    Settings,
    Loading,
};

struct UiHero {
    std::string id;
    std::string name;
    int health = 0;
    int shield = 0;
    int speed_percent = 0;
    std::string ability_name;
};

struct UiPlayerRow {
    int slot = 0;
    int team = 0;
    std::string name;
    std::string hero;
    int health = 0;
    int max_health = 0;
    int kills = 0;
    int deaths = 0;
    std::uint32_t damage_dealt = 0;
    std::uint32_t blocked_damage = 0;
    bool alive = false;
    bool bot = false;
    bool ready = false;
    bool connected = true;
    bool empty = false;
    bool local = false;
};

struct UiFeedItem {
    std::string text;
    int team = 2;
};

struct UiMenuModel {
    std::string player_name;
    std::string selected_hero;
    std::string map_name;
    std::string content_label;
    int mode = 0;
    int fps = 0;
};

struct UiLobbyModel {
    std::string status;
    std::string server_label;
    std::string selected_hero;
    int phase = 0;
    int local_slot = 0;
    int ready_count = 0;
    int human_count = 0;
    int player_count = 0;
    int min_humans = 1;
    std::uint32_t countdown_ms = 0;
    bool local_ready = false;
    std::vector<UiPlayerRow> members;
};

struct UiGadgetSlot {
    std::string name;
    std::string key;
    int cooldown_seconds = 0;
    int cooldown_percent = 0;
    bool active = false;
    bool ready = false;
};

struct UiMatchModel {
    bool online = false;
    int mode = 0;
    bool match_over = false;
    int score0 = 0;
    int score1 = 0;
    int score_limit = 30;
    int winner = 0;
    std::uint32_t time_left_seconds = 0;
    int local_health = 0;
    int local_max_health = 100;
    int local_shield = 0;
    int local_max_shield = 0;
    int local_ammo = 0;
    int local_weapon = 0;
    std::string hero_name;
    std::string weapon_name;
    std::array<UiGadgetSlot, 3> gadgets;
    bool local_alive = false;
    std::uint32_t respawn_seconds = 0;
    int objective_owner = 0;
    int objective_capturing = 0;
    int objective_progress_percent = 0;
    int objective_score_percent = 0;
    int local_carried = 0;
    int depot_progress_percent = 0;
    int fps = 0;
    float damage_flash = 0.0f;
    std::string net_status;
    std::string notice;
    std::vector<UiPlayerRow> players;
    std::vector<UiFeedItem> feed;
};

struct UiResultsModel {
    bool online = false;
    bool allow_rematch = true;
    bool allow_menu = true;
    int winner = 0;
    int score0 = 0;
    int score1 = 0;
    int local_team = 0;
    std::string score_limit;
    std::vector<UiPlayerRow> players;
};

struct UiSettingsBinding {
    std::string action;
    std::string label;
    std::string key;
    bool capturing = false;
};

struct UiSettingsModel {
    bool controls_tab = false;
    bool audio_enabled = true;
    bool audio_available = false;
    int master_volume = 80;
    bool fullscreen = false;
    bool vsync = true;
    bool blood_enabled = true;
    std::string language_label;
    std::string resolution;
    std::string status;
    std::vector<UiSettingsBinding> bindings;
};

struct UiLoadingModel {
    std::string kicker;
    std::string step;
    std::string detail;
    float progress = 0.0f;
    bool failed = false;
    std::string error;
    std::string recommendation;
};

class GameUi {
public:
    using ActionHandler =
        std::function<void(const std::string& action, const std::string& value)>;

    bool initialize(
        Rml::Context& context,
        std::vector<UiHero> heroes,
        ActionHandler handler);
    void shutdown();

    void set_dimensions(int width, int height);
    void set_heroes(std::vector<UiHero> heroes);
    void set_screen(UiScreen screen);
    [[nodiscard]] UiScreen screen() const { return screen_; }

    void set_scoreboard_visible(bool visible);
    [[nodiscard]] bool scoreboard_visible() const {
        return scoreboard_visible_;
    }

    void update_menu(const UiMenuModel& model);
    void update_lobby(const UiLobbyModel& model);
    void update_match(const UiMatchModel& model);
    void update_results(const UiResultsModel& model);
    void update_settings(const UiSettingsModel& model);
    void update_loading(const UiLoadingModel& model);
    void set_language(UiLanguage language);
    void set_controls_text(std::string_view text);

    [[nodiscard]] Rml::ElementDocument* document() const {
        return document_;
    }

private:
    class ClickRouter;

    void render_hero_cards();
    void render_lobby_teams(const UiLobbyModel& model);
    void render_feed(const UiMatchModel& model);
    void render_scoreboard(const UiMatchModel& model);
    void render_settings_bindings(const UiSettingsModel& model);
    void update_hero_selection();
    void apply_localization();
    [[nodiscard]] std::string tr(std::string_view key) const;

    Rml::ElementDocument* document_ = nullptr;
    ClickRouter* click_router_ = nullptr;
    UiScreen screen_ = UiScreen::Menu;
    bool scoreboard_visible_ = false;

    std::vector<UiHero> heroes_;
    std::string selected_hero_;
    Localization localization_;

    Rml::Element* screen_menu_ = nullptr;
    Rml::Element* screen_lobby_ = nullptr;
    Rml::Element* screen_hud_ = nullptr;
    Rml::Element* screen_pause_ = nullptr;
    Rml::Element* screen_results_ = nullptr;
    Rml::Element* screen_settings_ = nullptr;
    Rml::Element* screen_loading_ = nullptr;

    Rml::Element* menu_description_ = nullptr;
    Rml::Element* menu_player_ = nullptr;
    Rml::Element* menu_heroes_ = nullptr;
    Rml::Element* menu_map_ = nullptr;
    Rml::Element* menu_mode_tdm_ = nullptr;
    Rml::Element* menu_mode_control_ = nullptr;
    Rml::Element* menu_mode_crystal_ = nullptr;
    Rml::Element* menu_status_ = nullptr;
    Rml::Element* menu_controls_ = nullptr;

    Rml::Element* lobby_status_ = nullptr;
    Rml::Element* lobby_ready_count_ = nullptr;
    Rml::Element* lobby_countdown_ = nullptr;
    Rml::Element* lobby_blue_count_ = nullptr;
    Rml::Element* lobby_red_count_ = nullptr;
    Rml::Element* lobby_team_blue_ = nullptr;
    Rml::Element* lobby_team_red_ = nullptr;
    Rml::Element* lobby_heroes_ = nullptr;
    Rml::Element* lobby_local_ = nullptr;
    Rml::Element* lobby_ready_ = nullptr;
    Rml::Element* lobby_server_ = nullptr;

    Rml::Element* hud_mode_ = nullptr;
    Rml::Element* hud_score_blue_ = nullptr;
    Rml::Element* hud_score_red_ = nullptr;
    Rml::Element* hud_clock_ = nullptr;
    Rml::Element* hud_limit_ = nullptr;
    Rml::Element* hud_objective_ = nullptr;
    Rml::Element* hud_objective_label_ = nullptr;
    Rml::Element* hud_objective_fill_ = nullptr;
    Rml::Element* hud_objective_value_ = nullptr;
    Rml::Element* hud_feed_ = nullptr;
    Rml::Element* hud_notice_ = nullptr;
    Rml::Element* hud_hero_ = nullptr;
    Rml::Element* hud_alive_ = nullptr;
    Rml::Element* hud_health_fill_ = nullptr;
    Rml::Element* hud_health_text_ = nullptr;
    Rml::Element* hud_shield_fill_ = nullptr;
    Rml::Element* hud_shield_text_ = nullptr;
    std::array<Rml::Element*, 3> hud_gadget_names_{};
    std::array<Rml::Element*, 3> hud_gadget_states_{};
    std::array<Rml::Element*, 3> hud_gadget_keys_{};
    std::array<Rml::Element*, 3> hud_gadget_fills_{};
    Rml::Element* hud_weapon_ = nullptr;
    Rml::Element* hud_ammo_ = nullptr;
    Rml::Element* hud_vitals_ = nullptr;
    Rml::Element* hud_death_ = nullptr;
    Rml::Element* hud_death_timer_ = nullptr;
    Rml::Element* hud_net_ = nullptr;
    Rml::Element* hud_controls_ = nullptr;
    Rml::Element* damage_flash_ = nullptr;
    Rml::Element* scoreboard_ = nullptr;
    Rml::Element* scoreboard_score_ = nullptr;
    Rml::Element* scoreboard_teams_ = nullptr;

    Rml::Element* results_kicker_ = nullptr;
    Rml::Element* results_title_ = nullptr;
    Rml::Element* results_score_ = nullptr;
    Rml::Element* results_subtitle_ = nullptr;
    Rml::Element* results_teams_ = nullptr;
    Rml::Element* results_rematch_ = nullptr;
    Rml::Element* results_menu_ = nullptr;

    Rml::Element* settings_tab_game_ = nullptr;
    Rml::Element* settings_tab_controls_ = nullptr;
    Rml::Element* settings_page_game_ = nullptr;
    Rml::Element* settings_page_controls_ = nullptr;
    Rml::Element* settings_audio_toggle_ = nullptr;
    Rml::Element* settings_audio_status_ = nullptr;
    Rml::Element* settings_volume_value_ = nullptr;
    Rml::Element* settings_resolution_ = nullptr;
    Rml::Element* settings_fullscreen_ = nullptr;
    Rml::Element* settings_vsync_ = nullptr;
    Rml::Element* settings_blood_ = nullptr;
    Rml::Element* settings_language_ = nullptr;
    Rml::Element* settings_bindings_ = nullptr;
    Rml::Element* settings_status_ = nullptr;

    Rml::Element* loading_kicker_ = nullptr;
    Rml::Element* loading_step_ = nullptr;
    Rml::Element* loading_detail_ = nullptr;
    Rml::Element* loading_progress_ = nullptr;
    Rml::Element* loading_fill_ = nullptr;
    Rml::Element* loading_error_ = nullptr;
    Rml::Element* loading_error_title_ = nullptr;
    Rml::Element* loading_error_text_ = nullptr;
    Rml::Element* loading_error_recommendation_ = nullptr;

    std::string last_feed_rml_;
    std::string last_scoreboard_rml_;
    std::string last_lobby_blue_rml_;
    std::string last_lobby_red_rml_;
    std::string last_results_teams_rml_;
    std::string last_settings_bindings_rml_;
};

} // namespace nsync::client

