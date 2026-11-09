#include "client/game_ui.h"

#include "shared/log.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <utility>

namespace nsync::client {
namespace {

std::string escape_html(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const char character : text) {
        switch (character) {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '"': result += "&quot;"; break;
            case '\'': result += "&#39;"; break;
            default: result.push_back(character); break;
        }
    }
    return result;
}

void set_text(Rml::Element* element, std::string_view text) {
    if (element != nullptr) {
        element->SetInnerRML(escape_html(text));
    }
}

void set_visible(Rml::Element* element, bool visible) {
    if (element == nullptr) {
        return;
    }
    if (visible) {
        element->RemoveProperty("display");
    } else {
        element->SetProperty("display", "none");
    }
}

void set_selected(Rml::Element* element, bool selected) {
    if (element != nullptr) {
        element->SetClass("selected", selected);
    }
}

int percent_of(int value, int maximum) {
    if (maximum <= 0) {
        return 0;
    }
    return std::clamp(value * 100 / maximum, 0, 100);
}

std::string percent_string(int value, int maximum) {
    return std::to_string(percent_of(value, maximum)) + "%";
}

std::string format_clock(std::uint32_t seconds) {
    char buffer[16] = {};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%02u:%02u",
        seconds / 60U,
        seconds % 60U);
    return buffer;
}

std::string uppercase(std::string text) {
    for (char& character : text) {
        character = static_cast<char>(
            std::toupper(static_cast<unsigned char>(character)));
    }
    return text;
}

std::string player_status(
    const UiPlayerRow& player,
    const Localization& localization) {
    if (player.empty) {
        return localization.text("generic.open_slot");
    }
    if (!player.connected) {
        return localization.text("generic.offline");
    }
    if (player.bot) {
        return localization.text("generic.bot");
    }
    return player.ready
        ? localization.text("generic.ready")
        : localization.text("generic.waiting");
}

std::string team_name(int team, const Localization& localization) {
    return localization.text(team == 0 ? "team.blue" : "team.red");
}

std::string team_class(int team) {
    return team == 0 ? "blue" : "red";
}

std::string hero_label(
    std::string_view id,
    const Localization& localization) {
    return localization.text_or(
        "hero." + std::string(id),
        uppercase(std::string(id)));
}

std::string initials(std::string_view name) {
    std::string result;
    for (const char character : name) {
        if (std::isalnum(static_cast<unsigned char>(character))) {
            result.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(character))));
            if (result.size() == 2) {
                break;
            }
        }
    }
    if (result.empty()) {
        result = "??";
    }
    return result;
}

} // namespace

class GameUi::ClickRouter final : public Rml::EventListener {
public:
    explicit ClickRouter(ActionHandler handler)
        : handler_(std::move(handler)) {}

    void ProcessEvent(Rml::Event& event) override {
        if (event.GetId() != Rml::EventId::Click) {
            return;
        }
        Rml::Element* element = event.GetTargetElement();
        while (element != nullptr) {
            const Rml::String action =
                element->GetAttribute<Rml::String>("data-action", "");
            if (!action.empty()) {
                const Rml::String value =
                    element->GetAttribute<Rml::String>("data-value", "");
                if (handler_) {
                    handler_(std::string(action), std::string(value));
                }
                event.StopPropagation();
                return;
            }
            element = element->GetParentNode();
        }
    }

private:
    ActionHandler handler_;
};

bool GameUi::initialize(
    Rml::Context& context,
    std::vector<UiHero> heroes,
    ActionHandler handler) {
    shutdown();
    heroes_ = std::move(heroes);

    document_ = context.LoadDocument("ui/game.rml");
    if (document_ == nullptr) {
        NSYNC_LOG_ERROR("client", "failed to load ui/game.rml");
        return false;
    }
    document_->Show();
    set_dimensions(context.GetDimensions().x, context.GetDimensions().y);

    click_router_ = new ClickRouter(std::move(handler));
    document_->AddEventListener("click", click_router_);

    const auto find = [&](const char* id) -> Rml::Element* {
        return document_->GetElementById(id);
    };

    screen_menu_ = find("screen-menu");
    screen_lobby_ = find("screen-lobby");
    screen_hud_ = find("screen-hud");
    screen_pause_ = find("screen-pause");
    screen_results_ = find("screen-results");
    screen_settings_ = find("screen-settings");
    screen_loading_ = find("screen-loading");

    menu_description_ = find("menu-description");
    menu_player_ = find("menu-player");
    menu_heroes_ = find("menu-heroes");
    menu_map_ = find("menu-map");
    menu_mode_tdm_ = find("menu-mode-tdm");
    menu_mode_control_ = find("menu-mode-control");
    menu_mode_crystal_ = find("menu-mode-crystal");
    menu_status_ = find("menu-status");
    menu_controls_ = find("menu-controls");

    lobby_status_ = find("lobby-status");
    lobby_ready_count_ = find("lobby-ready-count");
    lobby_countdown_ = find("lobby-countdown");
    lobby_blue_count_ = find("lobby-blue-count");
    lobby_red_count_ = find("lobby-red-count");
    lobby_team_blue_ = find("lobby-team-blue");
    lobby_team_red_ = find("lobby-team-red");
    lobby_heroes_ = find("lobby-heroes");
    lobby_local_ = find("lobby-local");
    lobby_ready_ = find("lobby-ready");
    lobby_server_ = find("lobby-server");

    hud_mode_ = find("hud-mode");
    hud_score_blue_ = find("hud-score-blue");
    hud_score_red_ = find("hud-score-red");
    hud_clock_ = find("hud-clock");
    hud_limit_ = find("hud-limit");
    hud_objective_ = find("hud-objective");
    hud_objective_label_ = find("hud-objective-label");
    hud_objective_fill_ = find("hud-objective-fill");
    hud_objective_value_ = find("hud-objective-value");
    hud_feed_ = find("hud-feed");
    hud_notice_ = find("hud-notice");
    hud_hero_ = find("hud-hero");
    hud_alive_ = find("hud-alive");
    hud_health_fill_ = find("hud-health-fill");
    hud_health_text_ = find("hud-health-text");
    hud_shield_fill_ = find("hud-shield-fill");
    hud_shield_text_ = find("hud-shield-text");
    for (std::size_t slot = 0; slot < hud_gadget_names_.size(); ++slot) {
        const std::string suffix = std::to_string(slot);
        hud_gadget_names_[slot] =
            find(("hud-gadget-" + suffix + "-name").c_str());
        hud_gadget_states_[slot] =
            find(("hud-gadget-" + suffix + "-state").c_str());
        hud_gadget_keys_[slot] =
            find(("hud-gadget-" + suffix + "-key").c_str());
        hud_gadget_fills_[slot] =
            find(("hud-gadget-" + suffix + "-fill").c_str());
    }
    hud_weapon_ = find("hud-weapon");
    hud_ammo_ = find("hud-ammo");
    hud_vitals_ = find("hud-vitals");
    hud_buff_ = find("hud-buff");
    hud_death_ = find("hud-death");
    hud_death_timer_ = find("hud-death-timer");
    hud_net_ = find("hud-net");
    hud_controls_ = find("hud-controls");
    damage_flash_ = find("damage-flash");
    scoreboard_ = find("scoreboard");
    scoreboard_score_ = find("scoreboard-score");
    scoreboard_teams_ = find("scoreboard-teams");

    results_kicker_ = find("results-kicker");
    results_title_ = find("results-title");
    results_score_ = find("results-score");
    results_subtitle_ = find("results-subtitle");
    results_teams_ = find("results-teams");
    results_rematch_ = find("results-rematch");
    results_menu_ = find("results-menu");

    settings_tab_game_ = find("settings-tab-game");
    settings_tab_controls_ = find("settings-tab-controls");
    settings_page_game_ = find("settings-page-game");
    settings_page_controls_ = find("settings-page-controls");
    settings_audio_toggle_ = find("settings-audio-toggle");
    settings_audio_status_ = find("settings-audio-status");
    settings_volume_value_ = find("settings-volume-value");
    settings_resolution_ = find("settings-resolution");
    settings_fullscreen_ = find("settings-fullscreen");
    settings_vsync_ = find("settings-vsync");
    settings_blood_ = find("settings-blood");
    settings_language_ = find("settings-language");
    settings_bindings_ = find("settings-bindings");
    settings_status_ = find("settings-status");

    loading_kicker_ = find("loading-kicker");
    loading_step_ = find("loading-step");
    loading_detail_ = find("loading-detail");
    loading_progress_ = find("loading-progress");
    loading_fill_ = find("loading-fill");
    loading_error_ = find("loading-error");
    loading_error_title_ = find("loading-error-title");
    loading_error_text_ = find("loading-error-text");
    loading_error_recommendation_ =
        find("loading-error-recommendation");

    if (screen_menu_ == nullptr || screen_lobby_ == nullptr ||
        screen_hud_ == nullptr || screen_pause_ == nullptr ||
        screen_results_ == nullptr || screen_settings_ == nullptr ||
        screen_loading_ == nullptr || loading_step_ == nullptr ||
        loading_fill_ == nullptr || loading_error_ == nullptr ||
        menu_heroes_ == nullptr ||
        lobby_team_blue_ == nullptr || hud_vitals_ == nullptr ||
        hud_shield_fill_ == nullptr || hud_shield_text_ == nullptr ||
        hud_gadget_names_[0] == nullptr || hud_gadget_names_[1] == nullptr ||
        hud_gadget_names_[2] == nullptr ||
        scoreboard_ == nullptr || results_title_ == nullptr ||
        settings_tab_game_ == nullptr ||
        settings_tab_controls_ == nullptr ||
        settings_bindings_ == nullptr) {
        NSYNC_LOG_ERROR(
            "client",
            "ui/game.rml is missing required elements");
        shutdown();
        return false;
    }

    screen_ = UiScreen::Menu;
    scoreboard_visible_ = false;
    set_screen(UiScreen::Menu);
    set_visible(lobby_countdown_, false);
    set_visible(hud_objective_, false);
    set_visible(hud_notice_, false);
    set_visible(hud_death_, false);
    set_visible(damage_flash_, false);
    set_visible(scoreboard_, false);
    set_visible(results_rematch_, false);
    set_visible(results_menu_, false);
    set_visible(screen_settings_, false);
    set_visible(screen_loading_, false);
    set_visible(loading_error_, false);
    if (settings_page_controls_ != nullptr) {
        settings_page_controls_->SetClass("hidden", true);
    }
    apply_localization();
    render_hero_cards();
    NSYNC_LOG_INFO(
        "client",
        "RmlUi game shell loaded: heroes=",
        heroes_.size());
    return true;
}

void GameUi::shutdown() {
    if (document_ != nullptr && click_router_ != nullptr) {
        document_->RemoveEventListener("click", click_router_);
    }
    delete click_router_;
    click_router_ = nullptr;
    document_ = nullptr;


    screen_menu_ = nullptr;
    screen_lobby_ = nullptr;
    screen_hud_ = nullptr;
    screen_pause_ = nullptr;
    screen_results_ = nullptr;
    screen_settings_ = nullptr;
    screen_loading_ = nullptr;
    menu_description_ = nullptr;
    menu_player_ = nullptr;
    menu_heroes_ = nullptr;
    menu_map_ = nullptr;
    menu_mode_tdm_ = nullptr;
    menu_mode_control_ = nullptr;
    menu_mode_crystal_ = nullptr;
    menu_status_ = nullptr;
    menu_controls_ = nullptr;
    lobby_status_ = nullptr;
    lobby_ready_count_ = nullptr;
    lobby_countdown_ = nullptr;
    lobby_blue_count_ = nullptr;
    lobby_red_count_ = nullptr;
    lobby_team_blue_ = nullptr;
    lobby_team_red_ = nullptr;
    lobby_heroes_ = nullptr;
    lobby_local_ = nullptr;
    lobby_ready_ = nullptr;
    lobby_server_ = nullptr;
    hud_mode_ = nullptr;
    hud_score_blue_ = nullptr;
    hud_score_red_ = nullptr;
    hud_clock_ = nullptr;
    hud_limit_ = nullptr;
    hud_objective_ = nullptr;
    hud_objective_label_ = nullptr;
    hud_objective_fill_ = nullptr;
    hud_objective_value_ = nullptr;
    hud_feed_ = nullptr;
    hud_notice_ = nullptr;
    hud_hero_ = nullptr;
    hud_alive_ = nullptr;
    hud_health_fill_ = nullptr;
    hud_health_text_ = nullptr;
    hud_shield_fill_ = nullptr;
    hud_shield_text_ = nullptr;
    hud_gadget_names_.fill(nullptr);
    hud_gadget_states_.fill(nullptr);
    hud_gadget_keys_.fill(nullptr);
    hud_gadget_fills_.fill(nullptr);
    hud_weapon_ = nullptr;
    hud_ammo_ = nullptr;
    hud_vitals_ = nullptr;
    hud_buff_ = nullptr;
    hud_death_ = nullptr;
    hud_death_timer_ = nullptr;
    hud_net_ = nullptr;
    hud_controls_ = nullptr;
    damage_flash_ = nullptr;
    scoreboard_ = nullptr;
    scoreboard_score_ = nullptr;
    scoreboard_teams_ = nullptr;
    results_kicker_ = nullptr;
    results_title_ = nullptr;
    results_score_ = nullptr;
    results_subtitle_ = nullptr;
    results_teams_ = nullptr;
    results_rematch_ = nullptr;
    results_menu_ = nullptr;
    settings_tab_game_ = nullptr;
    settings_tab_controls_ = nullptr;
    settings_page_game_ = nullptr;
    settings_page_controls_ = nullptr;
    settings_audio_toggle_ = nullptr;
    settings_audio_status_ = nullptr;
    settings_volume_value_ = nullptr;
    settings_resolution_ = nullptr;
    settings_fullscreen_ = nullptr;
    settings_vsync_ = nullptr;
    settings_blood_ = nullptr;
    settings_language_ = nullptr;
    settings_bindings_ = nullptr;
    settings_status_ = nullptr;
    loading_kicker_ = nullptr;
    loading_step_ = nullptr;
    loading_detail_ = nullptr;
    loading_progress_ = nullptr;
    loading_fill_ = nullptr;
    loading_error_ = nullptr;
    loading_error_title_ = nullptr;
    loading_error_text_ = nullptr;
    loading_error_recommendation_ = nullptr;
    last_feed_rml_.clear();
    last_scoreboard_rml_.clear();
    last_lobby_blue_rml_.clear();
    last_lobby_red_rml_.clear();
    last_results_teams_rml_.clear();
    last_settings_bindings_rml_.clear();
}

void GameUi::set_dimensions(int width, int height) {
    if (document_ == nullptr) {
        return;
    }
    document_->SetProperty(
        "width",
        std::to_string(std::max(width, 1)) + "px");
    document_->SetProperty(
        "height",
        std::to_string(std::max(height, 1)) + "px");
}

void GameUi::set_heroes(std::vector<UiHero> heroes) {
    heroes_ = std::move(heroes);
    render_hero_cards();
    update_hero_selection();
}

void GameUi::set_screen(UiScreen screen) {
    if (screen == UiScreen::Settings) {
        screen_ = screen;
        set_visible(screen_settings_, true);
        set_visible(screen_loading_, false);
        set_scoreboard_visible(false);
        return;
    }

    screen_ = screen;
    set_visible(screen_settings_, false);
    set_visible(screen_menu_, screen_ == UiScreen::Menu);
    set_visible(screen_lobby_, screen_ == UiScreen::Lobby);
    set_visible(
        screen_hud_,
        screen_ == UiScreen::Match ||
            screen_ == UiScreen::Paused ||
            screen_ == UiScreen::Results);
    set_visible(screen_pause_, screen_ == UiScreen::Paused);
    set_visible(screen_results_, screen_ == UiScreen::Results);
    set_visible(screen_loading_, screen_ == UiScreen::Loading);
    if (screen_ != UiScreen::Match) {
        set_scoreboard_visible(false);
    } else {
        set_scoreboard_visible(scoreboard_visible_);
    }
}

void GameUi::set_scoreboard_visible(bool visible) {
    scoreboard_visible_ = visible && screen_ == UiScreen::Match;
    set_visible(scoreboard_, scoreboard_visible_);
}

void GameUi::render_hero_cards() {
    const auto render = [&](Rml::Element* container) {
        if (container == nullptr) {
            return;
        }
        std::string html;
        for (const UiHero& hero : heroes_) {
            const bool selected = hero.id == selected_hero_;
            html += "<div class=\"hero-card";
            if (selected) {
                html += " selected";
            }
            html += "\" data-action=\"hero\" data-value=\"";
            html += escape_html(hero.id);
            html += "\">";
            html += "<div class=\"hero-glyph\">";
            html += escape_html(initials(hero.name));
            html += "</div><span class=\"hero-name\">";
            html += escape_html(uppercase(hero.name));
            html += "</span><span class=\"hero-stat\">HP ";
            html += std::to_string(hero.health);
            html += "</span><span class=\"hero-stat\">SH ";
            html += std::to_string(hero.shield);
            html += "</span><span class=\"hero-stat\">SPD ";
            html += std::to_string(hero.speed_percent);
            html += "</span><span class=\"hero-ability\">";
            html += escape_html(hero.ability_name);
            html += "</span></div>";
        }
        if (html.empty()) {
            html = "<div class=\"small\">" +
                escape_html(tr("generic.no_agents")) + "</div>";
        }
        container->SetInnerRML(html);
    };
    render(menu_heroes_);
    render(lobby_heroes_);
}

void GameUi::update_hero_selection() {
    const auto update = [&](Rml::Element* container) {
        if (container == nullptr) {
            return;
        }
        Rml::ElementList cards;
        container->GetElementsByClassName(cards, "hero-card");
        for (Rml::Element* card : cards) {
            if (card == nullptr) {
                continue;
            }
            const Rml::String value =
                card->GetAttribute<Rml::String>("data-value", "");
            set_selected(card, std::string(value) == selected_hero_);
        }
    };
    update(menu_heroes_);
    update(lobby_heroes_);
}

void GameUi::update_menu(const UiMenuModel& model) {
    if (selected_hero_ != model.selected_hero) {
        selected_hero_ = model.selected_hero;
        update_hero_selection();
    }
    set_text(menu_player_, model.player_name);
    set_text(menu_map_, model.map_name);
    set_text(menu_status_, model.content_label + " // " + std::to_string(model.fps) + " FPS");

    const UiHero* selected = nullptr;
    for (const UiHero& hero : heroes_) {
        if (hero.id == selected_hero_) {
            selected = &hero;
            break;
        }
    }
    if (selected != nullptr) {
        set_text(
            menu_description_,
            uppercase(selected->name) + " // " + tr("menu.hp") + " " +
                std::to_string(selected->health) + " // " +
                tr("menu.shield") + " " +
                std::to_string(selected->shield) + " // " +
                tr("menu.speed") + " " +
                std::to_string(selected->speed_percent) + " // E/F/G " +
                selected->ability_name + ". " + tr("menu.choose"));
    } else {
        set_text(menu_description_, tr("menu.choose"));
    }

    set_selected(menu_mode_tdm_, model.mode == 0);
    set_selected(menu_mode_control_, model.mode == 1);
    set_selected(menu_mode_crystal_, model.mode == 2);
}

void GameUi::render_lobby_teams(const UiLobbyModel& model) {
    std::string blue;
    std::string red;
    for (const UiPlayerRow& member : model.members) {
        std::string& target = member.team == 0 ? blue : red;
        target += "<div class=\"member-row";
        if (member.local) {
            target += " local";
        }
        if (member.empty) {
            target += " empty";
        }
        target += "\"><span class=\"member-name\">";
        target += escape_html(member.name);
        if (member.bot) {
            target += " [BOT]";
        }
        target += "</span><span class=\"member-meta\">";
        if (!member.empty) {
            target += escape_html(hero_label(member.hero, localization_));
            target += " // ";
        }
        target += "SLOT ";
        target += std::to_string(member.slot + 1);
        target += " // <span class=\"member-ready";
        if (member.ready) {
            target += " ready";
        }
        target += "\">";
        target += player_status(member, localization_);
        target += "</span></span></div>";
    }
    if (blue.empty()) {
        blue = "<div class=\"small\">" +
            escape_html(tr("generic.waiting_players")) + "</div>";
    }
    if (red.empty()) {
        red = "<div class=\"small\">" +
            escape_html(tr("generic.waiting_players")) + "</div>";
    }
    if (blue != last_lobby_blue_rml_) {
        last_lobby_blue_rml_ = blue;
        if (lobby_team_blue_ != nullptr) {
            lobby_team_blue_->SetInnerRML(blue);
        }
    }
    if (red != last_lobby_red_rml_) {
        last_lobby_red_rml_ = red;
        if (lobby_team_red_ != nullptr) {
            lobby_team_red_->SetInnerRML(red);
        }
    }
}

void GameUi::update_lobby(const UiLobbyModel& model) {
    if (selected_hero_ != model.selected_hero) {
        selected_hero_ = model.selected_hero;
        update_hero_selection();
    }

    set_text(lobby_status_, model.status);
    set_text(
        lobby_ready_count_,
        std::to_string(model.ready_count) + " / " +
            std::to_string(model.human_count) + " " +
            tr("lobby.ready_suffix"));
    set_text(lobby_server_, model.server_label);
    set_text(
        lobby_local_,
        tr("lobby.you") + " // SLOT " +
            std::to_string(model.local_slot + 1) +
            " // " + hero_label(model.selected_hero, localization_));

    int blue_count = 0;
    int red_count = 0;
    for (const UiPlayerRow& member : model.members) {
        if (member.empty) {
            continue;
        }
        if (member.team == 0) {
            ++blue_count;
        } else {
            ++red_count;
        }
    }
    set_text(lobby_blue_count_, std::to_string(blue_count));
    set_text(lobby_red_count_, std::to_string(red_count));
    render_lobby_teams(model);

    if (model.countdown_ms > 0) {
        set_visible(lobby_countdown_, true);
        set_text(
            lobby_countdown_,
            tr("lobby.starting_in") + " " +
                std::to_string((model.countdown_ms + 999U) / 1000U));
    } else {
        set_visible(lobby_countdown_, false);
    }

    if (lobby_ready_ != nullptr) {
        lobby_ready_->SetInnerRML(escape_html(
            model.local_ready
                ? tr("lobby.cancel_ready")
                : tr("lobby.ready")));
        set_selected(lobby_ready_, model.local_ready);
    }
}

void GameUi::render_feed(const UiMatchModel& model) {
    std::string html;
    for (const UiFeedItem& item : model.feed) {
        html += "<div class=\"feed-row feed-";
        html += item.team == 0 ? "blue" : item.team == 1 ? "red" : "system";
        html += "\">";
        html += escape_html(item.text);
        html += "</div>";
    }
    if (html != last_feed_rml_) {
        last_feed_rml_ = html;
        if (hud_feed_ != nullptr) {
            hud_feed_->SetInnerRML(html);
        }
    }
}

void GameUi::render_scoreboard(const UiMatchModel& model) {
    std::string html;
    for (int team = 0; team < 2; ++team) {
        html += "<div class=\"sb-team-title ";
        html += team_class(team);
        html += "\">";
        html += escape_html(team_name(team, localization_));
        html += "</div>";
        bool any = false;
        for (const UiPlayerRow& player : model.players) {
            if (player.team != team) {
                continue;
            }
            any = true;
            html += "<div class=\"sb-row";
            if (player.local) {
                html += " local";
            }
            html += "\"><span class=\"sb-col sb-name\">";
            html += escape_html(player.name);
            if (player.bot) {
                html += " [BOT]";
            }
            html += "</span><span class=\"sb-col sb-hero\">";
            html += escape_html(hero_label(player.hero, localization_));
            html += "</span><span class=\"sb-col sb-k\">";
            html += std::to_string(player.kills);
            html += "</span><span class=\"sb-col sb-d\">";
            html += std::to_string(player.deaths);
            html += "</span><span class=\"sb-col sb-dmg\">";
            html += std::to_string(player.damage_dealt);
            html += "</span><span class=\"sb-col sb-block\">";
            html += std::to_string(player.blocked_damage);
            html += "</span><span class=\"sb-col sb-hp\">HP ";
            html += std::to_string(player.health);
            html += "</span><span class=\"sb-col sb-status ";
            html += player.alive ? "live" : "dead";
            html += "\">";
            html += escape_html(
                player.alive ? tr("generic.live") : tr("generic.down"));
            html += "</span></div>";
        }
        if (!any) {
            html += "<div class=\"small\">" +
                escape_html(tr("generic.no_players")) + "</div>";
        }
    }
    if (html != last_scoreboard_rml_) {
        last_scoreboard_rml_ = html;
        if (scoreboard_teams_ != nullptr) {
            scoreboard_teams_->SetInnerRML(html);
        }
    }
    set_text(
        scoreboard_score_,
        team_name(0, localization_) + " " +
            std::to_string(model.score0) + "  -  " +
            team_name(1, localization_) + " " +
            std::to_string(model.score1));
}

void GameUi::update_match(const UiMatchModel& model) {
    set_text(
        hud_mode_,
        model.mode == 1 ? "CTRL" : (model.mode == 2 ? "GEM" : "TDM"));
    set_text(hud_score_blue_, std::to_string(model.score0));
    set_text(hud_score_red_, std::to_string(model.score1));
    set_text(hud_clock_, format_clock(model.time_left_seconds));
    set_text(hud_limit_, "/ " + std::to_string(model.score_limit));

    const bool control_mode = model.mode == 1;
    const bool crystal_mode = model.mode == 2;
    set_visible(hud_objective_, control_mode || crystal_mode);
    if (control_mode && hud_objective_fill_ != nullptr) {
        const int value = model.objective_progress_percent;
        std::string label = tr("hud.neutral");
        std::string color = "#7d93a6";
        if (model.objective_owner == 1) {
            label = tr("hud.blue_hold");
            color = "#68b4ff";
        } else if (model.objective_owner == 2) {
            label = tr("hud.red_hold");
            color = "#ff858b";
        } else if (model.objective_capturing == 1) {
            label = tr("hud.blue_capturing");
            color = "#68b4ff";
        } else if (model.objective_capturing == 2) {
            label = tr("hud.red_capturing");
            color = "#ff858b";
        }
        set_text(hud_objective_label_, label);
        set_text(hud_objective_value_, std::to_string(value) + "%");
        hud_objective_fill_->SetProperty(
            "width",
            std::to_string(value) + "%");
        hud_objective_fill_->SetProperty("background-color", color);
    } else if (crystal_mode && hud_objective_fill_ != nullptr) {
        const bool delivering = model.local_carried > 0;
        const std::string color = delivering ? "#ffd166" : "#7d93a6";
        set_text(
            hud_objective_label_,
            delivering ? tr("hud.crystal_deliver") : tr("hud.crystal_run"));
        set_text(
            hud_objective_value_,
            delivering
                ? std::to_string(model.depot_progress_percent) + "%"
                : "x" + std::to_string(model.local_carried));
        hud_objective_fill_->SetProperty(
            "width",
            std::to_string(
                delivering ? model.depot_progress_percent : 0) + "%");
        hud_objective_fill_->SetProperty("background-color", color);
    }

    const int health_percent =
        percent_of(model.local_health, model.local_max_health);
    if (hud_health_fill_ != nullptr) {
        hud_health_fill_->SetProperty(
            "width",
            std::to_string(health_percent) + "%");
    }
    set_text(
        hud_health_text_,
        std::to_string(model.local_health) + " / " +
            std::to_string(model.local_max_health));
    const int shield_percent =
        percent_of(model.local_shield, model.local_max_shield);
    if (hud_shield_fill_ != nullptr) {
        hud_shield_fill_->SetProperty(
            "width",
            std::to_string(shield_percent) + "%");
    }
    set_text(
        hud_shield_text_,
        "SH " + std::to_string(model.local_shield) + " / " +
            std::to_string(model.local_max_shield));
    set_text(
        hud_hero_,
        localization_.text_or(
            "hero." + model.hero_name,
            uppercase(model.hero_name)));
    set_text(
        hud_alive_,
        model.local_alive ? tr("generic.live") : tr("generic.down"));
    set_text(hud_ammo_, std::to_string(model.local_ammo));
    set_text(
        hud_weapon_,
        localization_.text_or(
            "weapon." + model.weapon_name,
            uppercase(model.weapon_name)));
    for (std::size_t slot = 0; slot < model.gadgets.size(); ++slot) {
        const UiGadgetSlot& gadget = model.gadgets[slot];
        const bool ready =
            model.local_alive && gadget.ready && !gadget.active;
        const bool cooling =
            model.local_alive && !gadget.active &&
            gadget.cooldown_seconds > 0;
        const bool active = model.local_alive && gadget.active;
        std::string state = tr("generic.ready");
        if (!model.local_alive) {
            state = tr("generic.down");
        } else if (active) {
            state = tr("generic.active");
        } else if (cooling) {
            state = std::to_string(gadget.cooldown_seconds) + "S";
        }
        set_text(hud_gadget_names_[slot], gadget.name);
        set_text(hud_gadget_keys_[slot], gadget.key);
        set_text(hud_gadget_states_[slot], state);
        Rml::Element* state_element = hud_gadget_states_[slot];
        if (state_element != nullptr) {
            state_element->SetClass("ready", ready);
            state_element->SetClass("cooldown", cooling);
            state_element->SetClass("active", active);
        }
        Rml::Element* fill = hud_gadget_fills_[slot];
        if (fill != nullptr) {
            const int width = gadget.active || gadget.ready
                ? 100
                : gadget.cooldown_percent;
            fill->SetProperty("width", std::to_string(width) + "%");
        }
    }
    if (hud_buff_ != nullptr) {
        const bool shocked =
            model.local_alive && model.local_shock_ticks > 0;
        hud_buff_->SetClass("shock", shocked);
        if (shocked) {
            set_visible(hud_buff_, true);
            set_text(
                hud_buff_,
                tr("hud.shocked") + " " +
                    std::to_string(
                        (model.local_shock_ticks + 59) / 60) +
                    "S");
        } else if (model.local_damage_bonus_ticks > 0) {
            set_visible(hud_buff_, true);
            set_text(
                hud_buff_,
                "X2 " +
                    std::to_string(
                        (model.local_damage_bonus_ticks + 59) / 60) +
                    "S");
        } else if (model.local_supply_heal_ticks > 0) {
            set_visible(hud_buff_, true);
            set_text(hud_buff_, "+HP");
        } else {
            set_visible(hud_buff_, false);
        }
    }
    if (hud_vitals_ != nullptr) {
        hud_vitals_->SetClass(
            "low",
            health_percent <= 30 || !model.local_alive);
    }

    const bool show_death = !model.local_alive && !model.match_over;
    set_visible(hud_death_, show_death);
    if (show_death) {
        set_text(
            hud_death_timer_,
            model.respawn_seconds > 0
                ? tr("hud.respawn_in") + " " +
                    std::to_string(model.respawn_seconds)
                : tr("hud.waiting_respawn"));
    }

    set_visible(damage_flash_, model.damage_flash > 0.01f);

    render_feed(model);
    render_scoreboard(model);

    if (model.notice.empty()) {
        set_visible(hud_notice_, false);
    } else {
        set_visible(hud_notice_, true);
        set_text(hud_notice_, model.notice);
    }

    set_text(
        hud_net_,
        model.net_status + "  //  " + std::to_string(model.fps) + " FPS");
}

void GameUi::update_results(const UiResultsModel& model) {
    std::string title = tr("results.match_over");
    std::string kicker = tr(
        model.online ? "results.online" : "results.offline");
    std::string subtitle;
    if (model.winner == 3) {
        title = tr("results.draw");
        subtitle = tr("results.complete");
    } else if (model.winner == 1 || model.winner == 2) {
        const int winning_team = model.winner - 1;
        title = winning_team == model.local_team
            ? tr("results.victory")
            : tr("results.defeat");
        subtitle = team_name(winning_team, localization_) + " " +
            tr("results.team_wins");
    } else {
        subtitle = tr("results.complete");
    }
    set_text(results_kicker_, kicker);
    set_text(results_title_, title);
    set_text(
        results_score_,
        std::to_string(model.score0) + " : " + std::to_string(model.score1));
    set_text(results_subtitle_, subtitle);

    std::string teams;
    for (int team = 0; team < 2; ++team) {
        teams += "<div class=\"result-team ";
        teams += team_class(team);
        teams += "\"><span class=\"member-name\">";
        teams += escape_html(team_name(team, localization_));
        teams += "</span>";
        bool any = false;
        for (const UiPlayerRow& player : model.players) {
            if (player.team != team) {
                continue;
            }
            any = true;
            teams += "<span class=\"member-meta\">";
            teams += escape_html(player.name);
            teams += " // ";
            teams += escape_html(hero_label(player.hero, localization_));
            teams += "</span>";
        }
        if (!any) {
            teams += "<span class=\"member-meta\">";
            teams += escape_html(tr("generic.no_players"));
            teams += "</span>";
        }
        teams += "</div>";
    }
    if (teams != last_results_teams_rml_) {
        last_results_teams_rml_ = teams;
        if (results_teams_ != nullptr) {
            results_teams_->SetInnerRML(teams);
        }
    }

    set_visible(results_rematch_, model.allow_rematch);
    set_visible(results_menu_, model.allow_menu);
}

void GameUi::render_settings_bindings(const UiSettingsModel& model) {
    std::string html;
    for (const UiSettingsBinding& binding : model.bindings) {
        html += "<div class=\"bind-row";
        if (binding.capturing) {
            html += " capturing";
        }
        html += "\" data-action=\"settings-bind\" data-value=\"";
        html += escape_html(binding.action);
        html += "\"><span class=\"bind-label\">";
        html += escape_html(binding.label);
        html += "</span><strong class=\"bind-key\">";
        html += escape_html(
            binding.capturing
                ? tr("generic.press_key")
                : (binding.key.empty()
                       ? tr("generic.unbound")
                       : binding.key));
        html += "</strong></div>";
    }
    if (html.empty()) {
        html = "<div class=\"small\">" +
            escape_html(tr("generic.no_bindings")) + "</div>";
    }
    if (html != last_settings_bindings_rml_) {
        last_settings_bindings_rml_ = html;
        if (settings_bindings_ != nullptr) {
            settings_bindings_->SetInnerRML(html);
        }
    }
}

void GameUi::update_settings(const UiSettingsModel& model) {
    const auto set_toggle = [&](Rml::Element* element, bool enabled) {
        if (element == nullptr) {
            return;
        }
        element->SetInnerRML(escape_html(
            enabled ? tr("generic.on") : tr("generic.off")));
        element->SetClass("on", enabled);
    };

    set_toggle(settings_audio_toggle_, model.audio_enabled);
    set_toggle(settings_fullscreen_, model.fullscreen);
    set_toggle(settings_vsync_, model.vsync);
    set_toggle(settings_blood_, model.blood_enabled);
    set_text(
        settings_volume_value_,
        std::to_string(model.master_volume) + "%");
    set_text(settings_resolution_, model.resolution);
    set_text(settings_language_, model.language_label);

    if (settings_audio_status_ != nullptr) {
        if (!model.audio_enabled) {
            set_text(settings_audio_status_, tr("settings.audio_off"));
            settings_audio_status_->SetProperty("color", "#8196aa");
        } else if (model.audio_available) {
            set_text(settings_audio_status_, tr("settings.audio_ready"));
            settings_audio_status_->SetProperty("color", "#67e29c");
        } else {
            set_text(
                settings_audio_status_,
                tr("settings.audio_unavailable"));
            settings_audio_status_->SetProperty("color", "#ff858b");
        }
    }

    set_selected(settings_tab_game_, !model.controls_tab);
    set_selected(settings_tab_controls_, model.controls_tab);
    if (settings_page_game_ != nullptr) {
        settings_page_game_->SetClass("hidden", model.controls_tab);
        if (model.controls_tab) {
            settings_page_game_->SetProperty("display", "none");
        } else {
            settings_page_game_->RemoveProperty("display");
        }
    }
    if (settings_page_controls_ != nullptr) {
        settings_page_controls_->SetClass("hidden", !model.controls_tab);
        if (!model.controls_tab) {
            settings_page_controls_->SetProperty("display", "none");
        } else {
            settings_page_controls_->RemoveProperty("display");
        }
    }

    if (document_ != nullptr) {
        Rml::ElementList segments;
        document_->GetElementsByClassName(segments, "volume-seg");
        for (Rml::Element* segment : segments) {
            if (segment == nullptr) {
                continue;
            }
            const Rml::String value =
                segment->GetAttribute<Rml::String>("data-value", "0");
            const int amount = std::atoi(value.c_str());
            segment->SetClass("filled", amount <= model.master_volume);
        }
    }

    set_text(
        settings_status_,
        model.status.empty()
            ? tr("settings.changes_apply")
            : model.status);
    render_settings_bindings(model);
}

void GameUi::update_loading(const UiLoadingModel& model) {
    set_text(loading_kicker_, model.kicker);
    set_text(loading_step_, model.step);
    set_text(loading_detail_, model.detail);
    if (loading_fill_ != nullptr) {
        const int percent = std::clamp(
            static_cast<int>(model.progress * 100.0f),
            0,
            100);
        loading_fill_->SetProperty(
            "width",
            std::to_string(percent) + "%");
    }
    set_visible(loading_progress_, !model.failed);
    set_visible(loading_error_, model.failed);
    if (model.failed) {
        set_text(loading_error_title_, model.error);
        set_text(loading_error_text_, model.recommendation);
        set_text(
            loading_error_recommendation_,
            model.detail);
    }
}

void GameUi::set_language(UiLanguage language) {
    localization_.set_language(language);
    if (document_ != nullptr) {
        document_->SetClass(
            "lang-zh",
            language == UiLanguage::Chinese);
    }
    apply_localization();
}

std::string GameUi::tr(std::string_view key) const {
    return localization_.text(key);
}

void GameUi::apply_localization() {
    if (document_ == nullptr) {
        return;
    }
    const auto visit = [&](auto&& self, Rml::Element* element) -> void {
        if (element == nullptr) {
            return;
        }
        const Rml::String key =
            element->GetAttribute<Rml::String>("data-loc", "");
        if (!key.empty()) {
            set_text(element, tr(std::string(key)));
        }
        for (int index = 0; index < element->GetNumChildren(); ++index) {
            self(self, element->GetChild(index));
        }
    };
    visit(visit, document_);
}

void GameUi::set_controls_text(std::string_view text) {
    set_text(hud_controls_, text);
    set_text(menu_controls_, text);
}

} // namespace nsync::client
