#include "core/animation.h"
#include "core/atlas.h"
#include "core/hero_rig.h"
#include "core/content.h"
#include "core/fixed_step.h"
#include "core/input_command.h"
#include "core/fixed_test.h"
#include "core/map.h"
#include "core/world.h"
#include "shared/asset_watcher.h"
#include "shared/config.h"
#include "shared/crash_handler.h"
#include "shared/log.h"
#include "shared/pak.h"
#include "shared/paths.h"
#include "shared/resource.h"
#include "shared/update_check.h"
#include "shared/version.h"
#include "test_determinism.h"
#include "test_net.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

namespace fs = std::filesystem;

int g_failures = 0;

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            std::fprintf( \
                stderr, \
                "FAIL %s:%d: %s\n", \
                __FILE__, \
                __LINE__, \
                #expression); \
            ++g_failures; \
        } \
    } while (0)

fs::path make_temp_root() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root =
        fs::temp_directory_path() / ("nsync_tests_" + std::to_string(stamp));
    std::error_code error;
    fs::create_directories(root, error);
    return root;
}

void write_text(const fs::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary);
    output << text;
}

std::string read_text(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

void test_fixed_step() {
    using namespace std::chrono;

    nsync::core::FixedStepClock clock(60, 5);
    const auto start = nsync::core::FixedStepClock::Clock::time_point{};
    clock.reset(start);

    const auto zero = clock.advance(start);
    CHECK(zero.steps == 0);

    const auto one = clock.advance(start + milliseconds(17));
    CHECK(one.steps == 1);
    CHECK(one.alpha >= 0.0 && one.alpha < 1.0);
    CHECK(one.frame_seconds > 0.0);

    const auto wait = clock.time_until_next_step();
    CHECK(wait > std::chrono::nanoseconds::zero());
    CHECK(wait <= clock.step_duration());

    const auto burst = clock.advance(start + milliseconds(200));
    CHECK(burst.steps == 5);
    CHECK(burst.dropped_seconds > 0.0);
}

void test_config(const fs::path& root) {
    const fs::path path = root / "test.ini";
    write_text(
        path,
        "# comment\n"
        "client.width=1280\n"
        "client.vsync=false\n"
        "log.level=debug\n"
        "broken_line\n");

    nsync::shared::Config config;
    std::string error;
    CHECK(!config.load_file(path, error));
    CHECK(!error.empty());

    write_text(
        path,
        "# comment\n"
        "client.width=1280\n"
        "client.vsync=false\n"
        "log.level=debug\n");

    error.clear();
    CHECK(config.load_file(path, error));
    CHECK(config.get_int("client.width", 0) == 1280);
    CHECK(config.get_bool("client.vsync", true) == false);
    CHECK(config.get_string("log.level", "") == "debug");
    CHECK(config.get_int("missing", 42) == 42);
    CHECK(config.get_int("client.width", 0, 320, 1024) == 1024);

    config.set("client.height", "900");
    const fs::path saved = root / "saved.ini";
    error.clear();
    CHECK(config.save_file(saved, error));
    CHECK(error.empty());

    nsync::shared::Config saved_config;
    CHECK(saved_config.load_file(saved, error));
    CHECK(saved_config.get_int("client.width", 0) == 1280);
    CHECK(saved_config.get_int("client.height", 0) == 900);
    CHECK(saved_config.get_bool("client.vsync", true) == false);

    nsync::shared::Config override;
    override.set("client.width", "1600");
    override.set("input.jump", "F");
    saved_config.merge(override);
    CHECK(saved_config.get_int("client.width", 0) == 1600);
    CHECK(saved_config.get_string("input.jump", "") == "F");
}

void test_resources(const fs::path& root) {
    const fs::path assets = root / "assets";
    const fs::path pak_path = root / "assets.pak";
    std::error_code error;
    fs::create_directories(assets / "ui", error);
    write_text(assets / "ui" / "hello.txt", "hello nsync");

    nsync::shared::ResourceManager directory;
    std::string mount_error;
    CHECK(directory.mount_directory("assets", assets, mount_error));
    CHECK(directory.exists("ui/hello.txt"));
    CHECK(directory.read_text("ui/hello.txt") == "hello nsync");
    CHECK(!directory.exists("../outside.txt"));

    CHECK(nsync::shared::normalize_virtual_path("ui/hello.txt") == "ui/hello.txt");
    CHECK(!nsync::shared::normalize_virtual_path("../outside.txt"));

    std::string pak_error;
    CHECK(nsync::shared::pak::write_pak(assets, pak_path, pak_error));

    nsync::shared::ResourceManager pak;
    CHECK(pak.mount_pak("assets.pak", pak_path, pak_error));
    CHECK(pak.exists("ui/hello.txt"));
    CHECK(pak.read_text("ui/hello.txt") == "hello nsync");
    CHECK(!pak.exists("missing.txt"));
}

void test_asset_watcher(const fs::path& root) {
    using namespace std::chrono_literals;

    const fs::path directory = root / "watch";
    std::error_code error;
    fs::create_directories(directory, error);
    const fs::path content = directory / "content.json";
    const fs::path image = directory / "heroes.png";
    write_text(content, "one");

    nsync::shared::AssetWatcher watcher;
    watcher.watch("content", content);
    watcher.watch("atlas_image", image);
    watcher.prime();
    CHECK(watcher.size() == 2);
    CHECK(watcher.poll().empty());

    const auto original_time = fs::last_write_time(content);
    write_text(content, "two");
    fs::last_write_time(content, original_time + 1s);
    std::vector<std::string> changes = watcher.poll();
    CHECK(changes.size() == 1);
    CHECK(changes[0] == "content");
    CHECK(watcher.poll().empty());

    write_text(image, "png");
    changes = watcher.poll();
    CHECK(changes.size() == 1);
    CHECK(changes[0] == "atlas_image");

    fs::remove(content, error);
    changes = watcher.poll();
    CHECK(changes.size() == 1);
    CHECK(changes[0] == "content");

    write_text(content, "three");
    changes = watcher.poll();
    CHECK(changes.size() == 1);
    CHECK(changes[0] == "content");

    const fs::path alternate = directory / "alternate.json";
    write_text(alternate, "alternate");
    watcher.watch("content", alternate);
    CHECK(watcher.poll().empty());
    const auto alternate_time = fs::last_write_time(alternate);
    write_text(alternate, "alternate changed");
    fs::last_write_time(alternate, alternate_time + 1s);
    changes = watcher.poll();
    CHECK(changes.size() == 1);
    CHECK(changes[0] == "content");

    CHECK(nsync::shared::paths::source_asset_root().has_value());
}

void test_content_pipeline() {
    using namespace nsync::core;

    ContentDatabase database;
    CHECK(database.hero_count() == 10);
    CHECK(database.weapon_count() == 6);
    CHECK(database.find_hero("assault") != nullptr);
    CHECK(database.find_weapon("grenade") != nullptr);
    CHECK(database.drop_count() >= 1);
    CHECK(database.find_drop("crystal") != nullptr);
    CHECK(database.crystal_rules().deliver_ticks > 0);
    CHECK(database.crystal_rules().score_per_delivery > 0);
    CHECK(database.crystal_rules().carry_limit > 0);
    CHECK(database.find_drop("crystal") != nullptr);
    CHECK(database.find_drop("crystal")->max_capacity >= 2);

    const fs::path content_path =
        fs::path(NSYNC_SOURCE_DIR) / "assets" / "data" / "content.json";
    std::string error;
    ContentDatabase loaded;
    CHECK(loaded.load_file(content_path, &error));
    CHECK(error.empty());
    CHECK(loaded.fingerprint() == database.fingerprint());

    ContentDatabase duplicate;
    const std::string duplicate_json = read_text(content_path);
    std::string invalid_error;
    std::string duplicate_text = duplicate_json;
    const std::string second_id = "\"id\": \"gunner\"";
    const std::size_t id_position = duplicate_text.find(second_id);
    CHECK(id_position != std::string::npos);
    if (id_position != std::string::npos) {
        duplicate_text.replace(
            id_position,
            second_id.size(),
            "\"id\": \"scout\"");
    }
    CHECK(!duplicate.load_json(duplicate_text, &invalid_error));
    CHECK(!invalid_error.empty());
}

void test_map_pipeline() {
    using namespace nsync::core;

    const fs::path map_path =
        fs::path(NSYNC_SOURCE_DIR) / "assets" / "maps" / "arena_02.json";
    Map loaded;
    std::string error;
    CHECK(load_map_file(map_path, loaded, &error));
    CHECK(loaded.id == "arena_02");
    CHECK(loaded.solids.size() == 2);
    CHECK(loaded.one_way_platforms.size() == 3);
    CHECK(loaded.objective.has_value());
    CHECK(loaded.drop_spawns.size() == 3);
    CHECK(loaded.drop_depots.size() == 2);
    CHECK(loaded.drop_spawns[0].drop_id == "crystal");

    const std::vector<std::uint8_t> binary = encode_map_bin(loaded);
    CHECK(!binary.empty());
    Map decoded;
    CHECK(decode_map_bin(binary.data(), binary.size(), decoded, &error));
    CHECK(map_fingerprint(loaded) == map_fingerprint(decoded));
    CHECK(decoded.objective.has_value());
    CHECK(decoded.spawns[0].x == loaded.spawns[0].x);
    CHECK(decoded.drop_spawns.size() == loaded.drop_spawns.size());
    CHECK(decoded.drop_depots.size() == loaded.drop_depots.size());
    CHECK(decoded.drop_depots[0].team == loaded.drop_depots[0].team);

    const fs::path bin_path =
        fs::path(NSYNC_SOURCE_DIR) / "assets" / "maps" / "arena_02.bin";
    if (fs::is_regular_file(bin_path)) {
        Map from_bin;
        CHECK(load_map_file(bin_path, from_bin, &error));
        CHECK(map_fingerprint(from_bin) == map_fingerprint(loaded));
    }

    if (binary.size() > 4) {
        CHECK(!decode_map_bin(
            binary.data(),
            binary.size() - 1,
            decoded,
            &error));
    }
}

void test_world1_slope_collision() {
    using namespace nsync::core;

    const fs::path map_path =
        fs::path(NSYNC_SOURCE_DIR) / "assets" / "maps" / "world1.json";
    Map map;
    std::string error;
    CHECK(load_map_file(map_path, map, &error));

    for (int jump_period = 0; jump_period <= 90; jump_period += 90) {
        World world;
        world.set_map(map);
        world.reset(1, 2);
        EntityId entity{};
        world.players().for_each([&](Player& player) {
            if (player.slot == 0) {
                entity = player.id;
                player.position = Vec2{
                    Fix32::from_ratio(1013, 100),
                    Fix32::from_ratio(3385, 100)};
                player.velocity = {};
                player.grounded = 1;
            }
        });
        CHECK(entity.valid());

        bool fell = false;
        for (std::uint32_t tick = 0; tick < 240; ++tick) {
            InputFrame frame;
            PlayerCommand command;
            command.entity = entity;
            command.command.tick = world.tick();
            command.command.buttons = kButtonRight;
            if (jump_period > 0 && (tick % static_cast<std::uint32_t>(jump_period)) == 0U) {
                command.command.buttons |= kButtonJump;
            }
            frame.commands.push_back(command);
            world.step(frame);
            const Player* player = world.players().get(entity);
            CHECK(player != nullptr);
            if (player != nullptr && player->position.y > Fix32::from_int(41)) {
                fell = true;
                break;
            }
        }
        CHECK(!fell);
    }
}

void test_world1_wall_ledge_collision() {
    using namespace nsync::core;

    const fs::path map_path =
        fs::path(NSYNC_SOURCE_DIR) / "assets" / "maps" / "world1.json";
    Map map;
    std::string error;
    CHECK(load_map_file(map_path, map, &error));

    World world;
    world.set_map(map);
    world.reset(1, 1);
    EntityId entity{};
    world.players().for_each([&](Player& player) {
        if (player.slot == 0) {
            entity = player.id;
            player.position = Vec2{
                Fix32::from_ratio(352, 10),
                Fix32::from_ratio(33376, 1000)};
            player.velocity = {};
            player.grounded = 1;
        }
    });
    CHECK(entity.valid());

    InputFrame empty;
    for (int tick = 0; tick < 30; ++tick) {
        world.step(empty);
        const Player* player = world.players().get(entity);
        CHECK(player != nullptr);
        if (player == nullptr) {
            return;
        }
        CHECK(player->position.y > Fix32::from_ratio(33, 1));
        CHECK(player->position.x < Fix32::from_ratio(3524, 100));
    }
    const Player* player = world.players().get(entity);
    CHECK(player != nullptr);
    if (player != nullptr) {
        CHECK(player->grounded != 0);
        CHECK(player->position.y < Fix32::from_ratio(334, 10));
    }
}

void test_world1_slope_surface() {
    using namespace nsync::core;

    const fs::path map_path =
        fs::path(NSYNC_SOURCE_DIR) / "assets" / "maps" / "world1.json";
    Map map;
    std::string error;
    CHECK(load_map_file(map_path, map, &error));

    World world;
    world.set_map(map);
    world.reset(1, 1);
    EntityId entity{};
    world.players().for_each([&](Player& player) {
        if (player.slot == 0) {
            entity = player.id;
            player.position = Vec2{
                Fix32::from_ratio(65, 10),
                Fix32::from_int(32)};
            player.velocity = {};
            player.grounded = 0;
        }
    });
    CHECK(entity.valid());

    InputFrame empty;
    for (int tick = 0; tick < 30; ++tick) {
        world.step(empty);
    }
    const Player* landed = world.players().get(entity);
    CHECK(landed != nullptr);
    if (landed != nullptr) {
        // The authored border line is at y=33.6.  The converted floor must
        // stay at y=33.54; the old +0.7 patch put it at 34.24 and made the
        // hero sink into the grass slope.
        CHECK(landed->position.y < Fix32::from_ratio(33, 1));
        CHECK(landed->grounded != 0);
    }

    for (std::uint32_t tick = 0; tick < 120; ++tick) {
        InputFrame frame;
        PlayerCommand command;
        command.entity = entity;
        command.command.tick = world.tick();
        command.command.buttons = kButtonRight;
        frame.commands.push_back(command);
        world.step(frame);
    }
    const Player* walked = world.players().get(entity);
    CHECK(walked != nullptr);
    if (walked != nullptr) {
        CHECK(walked->position.x > Fix32::from_ratio(85, 10));
    }
}

void test_world1_bridge_step_up() {
    using namespace nsync::core;

    const fs::path map_path =
        fs::path(NSYNC_SOURCE_DIR) / "assets" / "maps" / "world1.json";
    Map map;
    std::string error;
    CHECK(load_map_file(map_path, map, &error));

    World world;
    world.set_map(map);
    world.reset(1, 1);
    EntityId entity{};
    world.players().for_each([&](Player& player) {
        if (player.slot == 0) {
            entity = player.id;
            player.position = Vec2{
                Fix32::from_int(25),
                Fix32::from_ratio(186, 10)};
            player.velocity = {};
            player.grounded = 1;
        }
    });
    CHECK(entity.valid());

    for (std::uint32_t tick = 0; tick < 120; ++tick) {
        InputFrame frame;
        PlayerCommand command;
        command.entity = entity;
        command.command.tick = world.tick();
        command.command.buttons = kButtonRight;
        frame.commands.push_back(command);
        world.step(frame);
    }

    const Player* player = world.players().get(entity);
    CHECK(player != nullptr);
    if (player != nullptr) {
        // Before the horizontal step-up fix the 0.06-thick border lip at the
        // end of the bridge stopped the hero at x=27.39.  The authored cliff
        // top is flush with the bridge, so the hero should walk onto it.
        CHECK(player->position.x > Fix32::from_ratio(278, 10));
        CHECK(player->position.y < Fix32::from_ratio(187, 10));
    }
}

void test_world1_jump_pad_trigger() {
    using namespace nsync::core;

    const fs::path map_path =
        fs::path(NSYNC_SOURCE_DIR) / "assets" / "maps" / "world1.json";
    Map map;
    std::string error;
    CHECK(load_map_file(map_path, map, &error));

    World world;
    world.set_map(map);
    world.reset(1, 1);
    EntityId entity{};
    world.players().for_each([&](Player& player) {
        if (player.slot == 0) {
            entity = player.id;
            player.position = Vec2{
                Fix32::from_int(14),
                Fix32::from_ratio(2834, 100)};
            player.velocity = {};
            player.grounded = 1;
        }
    });
    CHECK(entity.valid());

    InputFrame jump;
    PlayerCommand command;
    command.entity = entity;
    command.command.tick = world.tick();
    command.command.buttons = kButtonJump;
    jump.commands.push_back(command);
    world.step(jump);
    InputFrame empty;
    for (int index = 0; index < 10; ++index) {
        world.step(empty);
    }
    const Player* player = world.players().get(entity);
    CHECK(player != nullptr);
    if (player != nullptr) {
        CHECK(player->velocity.y < -Fix32::from_ratio(3, 10));
    }

    World landing_world;
    landing_world.set_map(map);
    landing_world.reset(1, 1);
    EntityId landing_entity{};
    landing_world.players().for_each([&](Player& player) {
        if (player.slot == 0) {
            landing_entity = player.id;
            player.position = Vec2{
                Fix32::from_int(14),
                Fix32::from_int(25)};
            player.velocity = {};
            player.grounded = 0;
        }
    });
    CHECK(landing_entity.valid());
    bool landed = false;
    for (int tick = 0; tick < 120; ++tick) {
        landing_world.step(empty);
        const Player* landing =
            landing_world.players().get(landing_entity);
        CHECK(landing != nullptr);
        if (landing != nullptr && landing->grounded != 0) {
            landed = true;
            break;
        }
    }
    CHECK(landed);
    const Player* landing =
        landing_world.players().get(landing_entity);
    CHECK(landing != nullptr);
    if (landing != nullptr) {
        CHECK(
            landing->position.y <
            Fix32::from_ratio(2834, 100));
        CHECK(
            landing->position.y >
            Fix32::from_ratio(2828, 100));
    }
}

void test_world1_jump_pad_reach() {
    using namespace nsync::core;

    const fs::path map_path =
        fs::path(NSYNC_SOURCE_DIR) / "assets" / "maps" / "world1.json";
    Map map;
    std::string error;
    CHECK(load_map_file(map_path, map, &error));
    CHECK(map.jump_pads.size() > 5);

    const std::size_t pad_indices[] = {0, 5};
    const Fix32 required_rise[] = {
        Fix32::from_int(8),
        Fix32::from_ratio(53, 10)};
    for (std::size_t index = 0; index < 2; ++index) {
        const JumpPad& pad = map.jump_pads[pad_indices[index]];
        World world;
        world.set_map(map);
        world.reset(1, 1);
        EntityId entity{};
        world.players().for_each([&](Player& player) {
            if (player.slot == 0) {
                entity = player.id;
                player.position = Vec2{
                    pad.bounds.center().x,
                    pad.bounds.min.y - Fix32::from_ratio(7, 10)};
                player.velocity = {};
                player.grounded = 1;
            }
        });
        CHECK(entity.valid());

        InputFrame jump;
        PlayerCommand command;
        command.entity = entity;
        command.command.tick = world.tick();
        command.command.buttons = kButtonJump;
        jump.commands.push_back(command);
        world.step(jump);

        InputFrame empty;
        Fix32 highest = pad.bounds.min.y;
        for (int tick = 0; tick < 300; ++tick) {
            world.step(empty);
            const Player* player = world.players().get(entity);
            CHECK(player != nullptr);
            if (player == nullptr) {
                break;
            }
            if (player->position.y + Fix32::from_ratio(7, 10) < highest) {
                highest =
                    player->position.y + Fix32::from_ratio(7, 10);
            }
            if (tick > 1 && player->velocity.y >= Fix32::zero()) {
                break;
            }
        }
        CHECK(
            pad.bounds.min.y - highest >
            required_rise[index]);
    }
}

void test_sprite_pipeline() {
    using namespace nsync::core;

    const fs::path root = fs::path(NSYNC_SOURCE_DIR) / "assets";
    Atlas atlas;
    std::string error;
    CHECK(atlas.load_file(root / "sprites" / "heroes.atlas.json", &error));
    CHECK(error.empty());
    CHECK(atlas.frames.size() == 84);
    CHECK(atlas.width == 512);
    CHECK(atlas.height > 0);
    const AtlasFrame* idle_frame = atlas.find("scout_idle_0");
    CHECK(idle_frame != nullptr);
    if (idle_frame != nullptr) {
        CHECK(idle_frame->width == 48);
        CHECK(idle_frame->height == 64);
        CHECK(idle_frame->pivot_x == Fix32::from_ratio(1, 2));
        CHECK(idle_frame->pivot_y == Fix32::one());
    }

    AnimationSet animations;
    CHECK(animations.load_file(
        root / "data" / "animations.json",
        &error));
    CHECK(error.empty());
    CHECK(animations.clips.size() == 21);
    const AnimationClip* shoot = animations.find("scout.shoot");
    CHECK(shoot != nullptr);
    if (shoot != nullptr) {
        CHECK(shoot->fps == 12);
        CHECK(shoot->loop);
        CHECK(shoot->frames.size() == 4);
    }

    std::size_t checked_frames = 0;
    for (const AnimationClip& clip : animations.clips) {
        for (const std::string& frame_name : clip.frames) {
            CHECK(atlas.find(frame_name) != nullptr);
            ++checked_frames;
        }
    }
    CHECK(checked_frames == 84);

    Player player;
    player.alive = 1;
    player.grounded = 1;
    CHECK(select_animation_state(player, false) == AnimationState::Idle);
    player.velocity.x = Fix32::from_ratio(1, 10);
    CHECK(select_animation_state(player, false) == AnimationState::Run);
    player.velocity.x = Fix32::zero();
    player.buttons = kButtonFire;
    CHECK(select_animation_state(player, false) == AnimationState::Shoot);
    player.buttons = 0;
    player.grounded = 0;
    player.velocity.y = -Fix32::from_ratio(1, 4);
    CHECK(select_animation_state(player, false) == AnimationState::Jump);
    player.velocity.y = Fix32::from_ratio(1, 4);
    CHECK(select_animation_state(player, false) == AnimationState::Fall);
    CHECK(select_animation_state(player, true) == AnimationState::Hurt);
    player.alive = 0;
    CHECK(select_animation_state(player, true) == AnimationState::Death);
    CHECK(animation_clip_name("scout", AnimationState::Idle) == "scout.idle");

    const fs::path hero_rig_path = root / "data" / "hero_rigs.json";
    if (fs::exists(hero_rig_path)) {
        HeroRigSet rigs;
        CHECK(rigs.load_file(hero_rig_path, &error));
        CHECK(error.empty());
        CHECK(rigs.valid(&error));
        CHECK(rigs.heroes.size() == 10);

        const fs::path original_atlas_path =
            root / "generated" / "original_heroes" / "heroes.atlas.json";
        if (fs::exists(original_atlas_path)) {
            Atlas original_atlas;
            CHECK(original_atlas.load_file(original_atlas_path, &error));
            CHECK(error.empty());
            CHECK(!original_atlas.frames.empty());
            std::size_t checked_parts = 0;
            for (const HeroRig& rig : rigs.heroes) {
                CHECK(!rig.parts.empty());
                CHECK(!rig.weapons.empty());
                for (const HeroPart& part : rig.parts) {
                    CHECK(original_atlas.find(part.sprite) != nullptr);
                    ++checked_parts;
                }
                for (const HeroWeapon& weapon : rig.weapons) {
                    CHECK(original_atlas.find(weapon.sprite) != nullptr);
                }
            }
            CHECK(checked_parts == 80);
        }

        HeroPoseSet poses;
        CHECK(poses.load_file(root / "data" / "hero_poses.json", &error));
        CHECK(error.empty());
        CHECK(poses.valid(&error));
        CHECK(poses.states.size() == 7);
        CHECK(poses.find("idle") != nullptr);
        CHECK(poses.find("death") != nullptr);
    }
}

void test_update_check(const fs::path& root) {
    using namespace nsync::shared;

    CHECK(valid_version(kVersion));
    CHECK(valid_version("1.2"));
    CHECK(!valid_version("1..2"));
    CHECK(!valid_version(""));
    CHECK(compare_versions("1.0.0", "1.0.0") == 0);
    CHECK(compare_versions("1.0.1", "1.0.0") > 0);
    CHECK(compare_versions("1.0", "1.0.0") == 0);
    CHECK(compare_versions("0.9.9", "1.0.0") < 0);

    const fs::path manifest = root / "update.json";
    write_text(
        manifest,
        "{\"version\":\"0.1.1\",\"url\":\"https://example.invalid/nsync.zip\","
        "\"notes\":\"test release\"}");
    UpdateCheckResult result;
    std::string error;
    CHECK(check_for_update(
        manifest.string(),
        "0.1.0",
        result,
        &error));
    CHECK(error.empty());
    CHECK(result.latest == "0.1.1");
    CHECK(result.url == "https://example.invalid/nsync.zip");
    CHECK(result.notes == "test release");
    CHECK(result.update_available);

    write_text(manifest, "{\"version\":\"0.1.0\"}");
    error.clear();
    CHECK(check_for_update(
        manifest.string(),
        "0.1.0",
        result,
        &error));
    CHECK(error.empty());
    CHECK(!result.update_available);

    write_text(manifest, "\xEF\xBB\xBF{\"version\":\"0.1.1\"}");
    error.clear();
    CHECK(check_for_update(
        manifest.string(),
        "0.1.0",
        result,
        &error));
    CHECK(error.empty());
    CHECK(result.update_available);

    write_text(manifest, "{broken");
    error.clear();
    CHECK(!check_for_update(
        manifest.string(),
        "0.1.0",
        result,
        &error));
    CHECK(!error.empty());

    write_text(manifest, "{\"version\":\"1.0.0\"}");
    error.clear();
    CHECK(check_for_update(
        manifest.string(),
        "9.0.0",
        result,
        &error));
    CHECK(error.empty());
    CHECK(!result.update_available);
}

void test_crash_report(const fs::path& root) {
    using namespace nsync::shared;

    set_log_level(LogLevel::Info);
    set_last_snapshot(4242, 0xABCDEF0123456789ULL);
    log(LogLevel::Info, "test", "crash report sentinel 12345");

    const fs::path report = root / "crash_report.txt";
    std::string error;
    CHECK(write_crash_report(
        report,
        "unit-test",
        0xC0000005U,
        0x12345678ULL,
        &error));
    CHECK(error.empty());
    const std::string text = read_text(report);
    CHECK(text.find("app=unit-test") != std::string::npos);
    CHECK(text.find("version=" + std::string(kVersion)) != std::string::npos);
    CHECK(text.find("build_config=" + std::string(kBuildConfig)) !=
        std::string::npos);
    CHECK(text.find("exception_code=0xC0000005") != std::string::npos);
    CHECK(text.find("last_snapshot_tick=4242") != std::string::npos);
    CHECK(text.find("last_snapshot_hash=0xABCDEF0123456789") !=
        std::string::npos);
    CHECK(text.find("crash report sentinel 12345") != std::string::npos);

    const std::vector<std::string> lines = recent_log_lines(8);
    CHECK(!lines.empty());
}

} // namespace

int main() {
    using namespace nsync;

    const fs::path root = make_temp_root();

    if (!core::run_fixed_tests()) {
        ++g_failures;
    }
    if (!run_determinism_tests(root)) {
        ++g_failures;
    }
    if (!run_net_tests()) {
        ++g_failures;
    }
    test_fixed_step();
    test_config(root);
    test_resources(root);
    test_asset_watcher(root);
    test_content_pipeline();
    test_map_pipeline();
    test_world1_slope_collision();
    test_world1_wall_ledge_collision();
    test_world1_slope_surface();
    test_world1_bridge_step_up();
    test_world1_jump_pad_trigger();
    test_world1_jump_pad_reach();
    test_sprite_pipeline();
    test_update_check(root);
    test_crash_report(root);

    std::error_code error;
    fs::remove_all(root, error);

    if (g_failures != 0) {
        std::fprintf(stderr, "tests: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("tests: PASS\n");
    return 0;
}
