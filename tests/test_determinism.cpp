#include "test_determinism.h"

#include "core/aim.h"
#include "core/byte_io.h"
#include "core/entity.h"
#include "core/geometry.h"
#include "core/hash.h"
#include "core/heroes.h"
#include "core/input_command.h"
#include "core/map.h"
#include "core/replay.h"
#include "core/rng.h"
#include "core/snapshot.h"
#include "core/weapons.h"
#include "core/world.h"
#include "golden_replay_expected.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

#define CHECK(...) \
    do { \
        if (!(__VA_ARGS__)) { \
            std::fprintf( \
                stderr, \
                "FAIL %s:%d: %s\n", \
                __FILE__, \
                __LINE__, \
                #__VA_ARGS__); \
            ++g_failures; \
        } \
    } while (0)

bool near_raw(nsync::core::Fix32 value, nsync::core::Fix32 expected, std::int32_t tolerance) {
    const std::int64_t difference =
        static_cast<std::int64_t>(value.raw()) - expected.raw();
    return difference >= -tolerance && difference <= tolerance;
}

void test_hash() {
    using namespace nsync::core;
    CHECK(hash64("", 0) == 0xEF46DB3751D8E999ULL);
    CHECK(hash64("hello", 0) == 0x26C7827D889F6DA3ULL);
    CHECK(hash64("hello", 1) != hash64("hello", 0));
    const std::uint64_t value = 2;
    CHECK(hash_combine(1, value) == hash64_bytes(&value, sizeof(value), 1));
}

void test_aim_geometry() {
    using namespace nsync::core;
    WeaponStats weapon{};
    weapon.joint_x = Fix32::from_ratio(3, 50);
    weapon.joint_y = Fix32::from_ratio(21, 100);
    weapon.gun_x = Fix32::from_ratio(51, 50);
    weapon.gun_y = Fix32::from_ratio(21, 100);

    const auto check_mark = [&](Vec2 mark) {
        const Vec2 aim = aim_target_pointer(weapon, Vec2{}, mark);
        const Vec2 muzzle = aim_muzzle_point(weapon, Vec2{}, aim);
        const Vec2 offset = mark - muzzle;
        const Fix32 travel = offset.x * aim.x + offset.y * aim.y;
        CHECK(travel > Fix32::zero());
        const Vec2 closest = muzzle + aim * travel;
        CHECK(near_raw(closest.x, mark.x, 300));
        CHECK(near_raw(closest.y, mark.y, 300));
        return aim;
    };

    const Vec2 mark{Fix32::from_int(2), Fix32::zero()};
    const Vec2 aim = check_mark(mark);
    CHECK(aim.x > Fix32::zero());
    CHECK(aim.y > Fix32::zero());
    const Vec2 muzzle = aim_muzzle_point(weapon, Vec2{}, aim);
    CHECK(muzzle.x > Fix32::one());
    CHECK(muzzle.y < Fix32::zero());

    check_mark(Vec2{Fix32::from_int(2), Fix32::from_int(2)});
    check_mark(Vec2{Fix32::from_int(2), Fix32::from_int(-2)});
    check_mark(Vec2{Fix32::from_int(3), Fix32::from_int(1)});
    check_mark(Vec2{Fix32::from_int(3), Fix32::from_int(-1)});

    const Vec2 behind = aim_target_pointer(
        weapon,
        Vec2{},
        Vec2{Fix32::from_int(-2), Fix32::zero()});
    CHECK(behind.x < Fix32::zero());
    CHECK(near_raw(behind.x, -aim.x, 200));
    CHECK(near_raw(behind.y, aim.y, 200));

    const Vec2 close = aim_target_pointer(
        weapon,
        Vec2{},
        Vec2{Fix32::from_ratio(1, 2), Fix32::zero()});
    CHECK(close.x > Fix32::zero());
}

void test_geometry() {
    using namespace nsync::core;
    const AABB a{{Fix32::from_int(0), Fix32::from_int(0)},
                 {Fix32::from_int(2), Fix32::from_int(2)}};
    const AABB b{{Fix32::from_int(1), Fix32::from_int(1)},
                 {Fix32::from_int(3), Fix32::from_int(3)}};
    const AABB edge{{Fix32::from_int(2), Fix32::from_int(0)},
                    {Fix32::from_int(3), Fix32::from_int(2)}};
    CHECK(a.overlaps(b));
    CHECK(!a.overlaps(edge));
    CHECK(a.contains(Vec2{Fix32::from_int(1), Fix32::from_int(1)}));
    CHECK(!a.contains(Vec2{Fix32::from_int(3), Fix32::from_int(1)}));

    const AABB centered = AABB::from_center(
        Vec2{Fix32::from_int(5), Fix32::from_int(5)},
        Vec2{Fix32::from_int(1), Fix32::from_int(2)});
    CHECK(centered.min == Vec2{Fix32::from_int(4), Fix32::from_int(3)});
    CHECK(centered.max == Vec2{Fix32::from_int(6), Fix32::from_int(7)});
    CHECK(centered.translated(Vec2{Fix32::from_int(1), Fix32::zero()}).min.x ==
          Fix32::from_int(5));
    CHECK(centered.expanded(Fix32::one()).min.x == Fix32::from_int(3));

    Fix32 t_min;
    Fix32 t_max;
    CHECK(segment_intersects_aabb(
        Segment{Vec2{Fix32::from_int(-1), Fix32::one()},
                Vec2{Fix32::from_int(3), Fix32::one()}},
        a,
        t_min,
        t_max));
    CHECK(near_raw(t_min, Fix32::from_ratio(1, 4), 1));
    CHECK(near_raw(t_max, Fix32::from_ratio(3, 4), 1));
    CHECK(segment_intersects_aabb(
        Segment{Vec2{Fix32::one(), Fix32::one()},
                Vec2{Fix32::from_int(2), Fix32::one()}},
        a));
    CHECK(!segment_intersects_aabb(
        Segment{Vec2{Fix32::from_int(3), Fix32::one()},
                Vec2{Fix32::from_int(4), Fix32::one()}},
        a));
    CHECK(!segment_intersects_aabb(
        Segment{Vec2{Fix32::from_int(3), Fix32::from_int(3)},
                Vec2{Fix32::from_int(4), Fix32::from_int(4)}},
        a));
    CHECK(segment_intersects_aabb(
        Segment{Vec2{Fix32::one(), Fix32::one()},
                Vec2{Fix32::one(), Fix32::one()}},
        a));
}

void test_rng() {
    using namespace nsync::core;
    Pcg32 first(42, 54);
    Pcg32 second(42, 54);
    for (int index = 0; index < 32; ++index) {
        CHECK(first.next_u32() == second.next_u32());
    }

    Pcg32 other(42, 55);
    CHECK(first.next_u32() != other.next_u32());

    Pcg32 bounded(7, 9);
    for (int index = 0; index < 1000; ++index) {
        CHECK(bounded.bounded(17) < 17);
    }

    Pcg32 source(123, 9);
    const Pcg32State saved = source.snapshot();
    const std::uint32_t expected = source.next_u32();
    Pcg32 restored;
    CHECK(restored.restore(saved));
    CHECK(restored.next_u32() == expected);

    Pcg32State invalid = saved;
    invalid.increment = 8;
    CHECK(!restored.restore(invalid));
}

void test_entities() {
    using namespace nsync::core;
    EntityAllocator allocator;
    const EntityId first = allocator.create();
    const EntityId second = allocator.create();
    CHECK(first.valid());
    CHECK(first.index() == 0);
    CHECK(first.generation() == 1);
    CHECK(second.index() == 1);
    CHECK(allocator.alive(first));
    CHECK(allocator.alive(second));

    CHECK(allocator.destroy(first));
    CHECK(!allocator.alive(first));
    const EntityId reused = allocator.create();
    CHECK(reused.index() == first.index());
    CHECK(reused.generation() == 2);
    CHECK(allocator.alive(reused));
    CHECK(allocator.live_count() == 2);

    const EntityAllocatorState saved = allocator.snapshot();
    EntityAllocator restored;
    std::string error;
    CHECK(restored.restore(saved, &error));
    CHECK(restored.alive(reused));
    CHECK(restored.alive(second));
    CHECK(!restored.alive(first));

    ComponentPool<Player> players;
    Player player;
    player.id = reused;
    CHECK(players.add(reused, player));
    player.id = second;
    CHECK(players.add(second, player));
    CHECK(players.size() == 2);
    std::vector<std::uint32_t> order;
    players.for_each([&](const Player& value) { order.push_back(value.id.index()); });
    CHECK(order.size() == 2);
    CHECK(order[0] == reused.index());
    CHECK(order[1] == second.index());
    CHECK(players.get(reused) != nullptr);
    CHECK(players.remove(reused));
    CHECK(players.get(reused) == nullptr);
    CHECK(players.size() == 1);
}

void test_byte_io() {
    using namespace nsync::core;
    ByteWriter writer;
    writer.u8(0x12);
    writer.u16(0x3456);
    writer.u32(0x789ABCDE);
    writer.u64(0x0123456789ABCDEFULL);
    writer.i8(-7);
    writer.i16(-1234);
    writer.i32(-12345678);
    const char payload[] = "abc";
    writer.bytes(payload, 3);

    ByteReader reader(writer.data());
    std::uint8_t u8 = 0;
    std::uint16_t u16 = 0;
    std::uint32_t u32 = 0;
    std::uint64_t u64 = 0;
    std::int8_t i8 = 0;
    std::int16_t i16 = 0;
    std::int32_t i32 = 0;
    char text[4]{};
    CHECK(reader.u8(u8));
    CHECK(reader.u16(u16));
    CHECK(reader.u32(u32));
    CHECK(reader.u64(u64));
    CHECK(reader.i8(i8));
    CHECK(reader.i16(i16));
    CHECK(reader.i32(i32));
    CHECK(reader.bytes(text, 3));
    CHECK(u8 == 0x12);
    CHECK(u16 == 0x3456);
    CHECK(u32 == 0x789ABCDE);
    CHECK(u64 == 0x0123456789ABCDEFULL);
    CHECK(i8 == -7);
    CHECK(i16 == -1234);
    CHECK(i32 == -12345678);
    CHECK(text[0] == 'a' && text[1] == 'b' && text[2] == 'c');
    CHECK(reader.ok());
    CHECK(reader.at_end());

    ByteReader truncated(writer.data().data(), writer.data().size() - 1);
    CHECK(!truncated.bytes(text, writer.data().size()));
    CHECK(!truncated.ok());
}

void test_map() {
    using namespace nsync::core;
    constexpr const char* kJson = R"({
        "id": "unit_arena",
        "bounds": [-10, 0, 20, 12],
        "solids": [
            { "rect": [-10, 10, 20, 2] }
        ],
        "platforms": [
            { "rect": [-4, 6, 3, 0.5] }
        ],
        "jump_pads": [
            { "rect": [-2, 4, 2, 0.5], "velocity": 0.25 }
        ],
        "spawns": [
            [-8, 9], [-7, 9], [-6, 9], [-5, 9], [-4, 9],
            [8, 9], [7, 9], [6, 9], [5, 9], [4, 9]
        ]
    })";

    Map map;
    std::string error;
    CHECK(load_map_json(kJson, map, &error));
    CHECK(error.empty());
    CHECK(map.valid(&error));
    CHECK(map.id == "unit_arena");
    CHECK(map.solids.size() == 1);
    CHECK(map.one_way_platforms.size() == 1);
    CHECK(map.platform_hatchway.size() == 1);
    CHECK(map.platform_hatchway[0] == 1);
    CHECK(map.jump_pads.size() == 1);
    CHECK(map.jump_pads[0].velocity == Fix32::from_ratio(1, 4));
    CHECK(map.spawns[0] == Vec2{Fix32::from_int(-8), Fix32::from_int(9)});
    CHECK(map.spawns[5] == Vec2{Fix32::from_int(8), Fix32::from_int(9)});
    const std::vector<std::uint8_t> map_binary = encode_map_bin(map);
    CHECK(!map_binary.empty());
    Map roundtrip;
    CHECK(decode_map_bin(
        map_binary.data(),
        map_binary.size(),
        roundtrip,
        &error));
    CHECK(roundtrip.jump_pads.size() == 1);
    CHECK(roundtrip.jump_pads[0].bounds == map.jump_pads[0].bounds);
    CHECK(roundtrip.platform_hatchway == map.platform_hatchway);

    CHECK(!load_map_json(R"({"id":"broken"})", map, &error));
    CHECK(!error.empty());
    CHECK(!load_map_json(R"({
        "id": "bad_spawns",
        "bounds": [-1, -1, 2, 2],
        "spawns": [[0, 0]]
    })", map, &error));
    CHECK(!error.empty());

    const Map arena = Map::default_arena();
    CHECK(arena.valid(&error));
    CHECK(arena.solids.size() == 1);
    CHECK(arena.one_way_platforms.size() == 2);
    CHECK(arena.spawns[0] == Vec2{Fix32::from_int(-5), Fix32::from_ratio(103, 10)});
    CHECK(arena.spawns[5] == Vec2{Fix32::from_int(5), Fix32::from_ratio(103, 10)});

    const std::filesystem::path map_path =
        std::filesystem::path(NSYNC_SOURCE_DIR) / "assets" / "maps" / "arena_01.json";
    std::ifstream input(map_path, std::ios::binary);
    CHECK(static_cast<bool>(input));
    if (input) {
        const std::string text{
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
        Map loaded;
        CHECK(load_map_json(text, loaded, &error));
        CHECK(loaded.id == arena.id);
        CHECK(loaded.bounds.min == arena.bounds.min);
        CHECK(loaded.bounds.max == arena.bounds.max);
        CHECK(loaded.solids == arena.solids);
        CHECK(loaded.one_way_platforms == arena.one_way_platforms);
        for (std::size_t index = 0; index < arena.spawns.size(); ++index) {
            CHECK(loaded.spawns[index] == arena.spawns[index]);
        }
    }

    World world;
    world.set_map(arena);
    world.reset(1, 2);
    CHECK(world.players().size() == 2);
    const Player* left = world.players().get(EntityId::make(0, 1));
    const Player* right = world.players().get(EntityId::make(1, 1));
    CHECK(left != nullptr);
    CHECK(right != nullptr);
    if (left != nullptr && right != nullptr) {
        CHECK(left->position == arena.spawns[0]);
        CHECK(right->position == arena.spawns[5]);
    }

    InputFrame empty;
    world.step(empty);
    left = world.players().get(EntityId::make(0, 1));
    CHECK(left != nullptr);
    if (left != nullptr) {
        CHECK(left->grounded != 0);
        CHECK(near_raw(
            left->position.y,
            Fix32::from_ratio(103, 10),
            2));
    }

    Map one_way_map;
    one_way_map.id = "one_way_test";
    one_way_map.bounds = AABB{
        Vec2{Fix32::from_int(-5), Fix32::zero()},
        Vec2{Fix32::from_int(5), Fix32::from_int(16)}};
    one_way_map.solids = {
        AABB{
            Vec2{Fix32::from_int(-5), Fix32::from_int(11)},
            Vec2{Fix32::from_int(5), Fix32::from_int(12)}},
    };
    one_way_map.one_way_platforms = {
        AABB{
            Vec2{Fix32::from_int(-2), Fix32::from_ratio(93, 10)},
            Vec2{Fix32::from_int(2), Fix32::from_ratio(98, 10)}},
    };
    one_way_map.platform_hatchway = {1};
    one_way_map.spawns.fill(
        Vec2{Fix32::zero(), Fix32::from_ratio(103, 10)});
    CHECK(one_way_map.valid(&error));

    World jump_world;
    jump_world.set_map(one_way_map);
    jump_world.reset(1, 1);
    jump_world.step(empty);
    InputFrame jump;
    PlayerCommand jump_command;
    jump_command.entity = EntityId::make(0, 1);
    jump_command.command.tick = jump_world.tick();
    jump_command.command.buttons = kButtonJump;
    jump.commands.push_back(jump_command);
    jump_world.step(jump);
    for (int index = 0; index < 80; ++index) {
        jump_world.step(empty);
    }
    const Player* jumped = jump_world.players().get(EntityId::make(0, 1));
    CHECK(jumped != nullptr);
    if (jumped != nullptr) {
        CHECK(jumped->grounded != 0);
        CHECK(near_raw(
            jumped->position.y,
            Fix32::from_ratio(86, 10),
            2));
    }

    InputFrame drop_input;
    PlayerCommand drop_command;
    drop_command.entity = EntityId::make(0, 1);
    drop_command.command.tick = jump_world.tick();
    drop_command.command.buttons = kButtonDown;
    drop_input.commands.push_back(drop_command);
    jump_world.step(drop_input);
    for (int index = 0; index < 3; ++index) {
        jump_world.step(empty);
    }
    if (jumped != nullptr) {
        CHECK(jumped->grounded == 0);
        CHECK(jumped->position.y > Fix32::from_ratio(86, 10));
    }

    Map no_drop_map = one_way_map;
    no_drop_map.platform_hatchway = {0};
    World no_drop_world;
    no_drop_world.set_map(no_drop_map);
    no_drop_world.reset(1, 1);
    Player* no_drop_player =
        no_drop_world.players().get(EntityId::make(0, 1));
    CHECK(no_drop_player != nullptr);
    if (no_drop_player != nullptr) {
        no_drop_player->position = Vec2{
            Fix32::zero(), Fix32::from_ratio(86, 10)};
        no_drop_player->velocity = {};
        no_drop_world.step(empty);
        InputFrame no_drop_input;
        PlayerCommand no_drop_command;
        no_drop_command.entity = no_drop_player->id;
        no_drop_command.command.tick = no_drop_world.tick();
        no_drop_command.command.buttons = kButtonDown;
        no_drop_input.commands.push_back(no_drop_command);
        no_drop_world.step(no_drop_input);
        for (int index = 0; index < 3; ++index) {
            no_drop_world.step(empty);
        }
        CHECK(no_drop_player->grounded != 0);
        CHECK(near_raw(
            no_drop_player->position.y,
            Fix32::from_ratio(86, 10),
            2));
    }

    Map pad_map = one_way_map;
    pad_map.one_way_platforms.clear();
    pad_map.platform_hatchway.clear();
    pad_map.jump_pads.push_back(JumpPad{
        AABB{
            Vec2{Fix32::from_int(-2), Fix32::from_int(8)},
            Vec2{Fix32::from_int(2), Fix32::from_ratio(85, 10)}},
        Fix32::from_ratio(1, 4)});
    CHECK(pad_map.valid(&error));
    World pad_world;
    pad_world.set_map(pad_map);
    pad_world.reset(1, 1);
    Player* pad_player = pad_world.players().get(EntityId::make(0, 1));
    CHECK(pad_player != nullptr);
    if (pad_player != nullptr) {
        pad_player->position = Vec2{
            Fix32::zero(),
            Fix32::from_int(5)};
        pad_player->velocity = {};
        bool landed = false;
        for (int index = 0; index < 60; ++index) {
            pad_world.step(empty);
            if (pad_player->grounded != 0) {
                landed = true;
                break;
            }
        }
        CHECK(landed);
        CHECK(near_raw(
            pad_player->position.y,
            Fix32::from_ratio(73, 10),
            2));
        CHECK(pad_player->velocity.y == Fix32::zero());
        pad_world.step(empty);
        CHECK(pad_player->grounded != 0);
        CHECK(pad_player->velocity.y == Fix32::zero());

        InputFrame pad_jump;
        PlayerCommand pad_jump_command;
        pad_jump_command.entity = pad_player->id;
        pad_jump_command.command.tick = pad_world.tick();
        pad_jump_command.command.buttons = kButtonJump;
        pad_jump.commands.push_back(pad_jump_command);
        pad_world.step(pad_jump);
        for (int index = 0; index < 10; ++index) {
            pad_world.step(empty);
        }
        CHECK(pad_player->velocity.y < -Fix32::from_ratio(3, 10));
    }

    Map embedded_pad_map = one_way_map;
    embedded_pad_map.one_way_platforms.clear();
    embedded_pad_map.platform_hatchway.clear();
    embedded_pad_map.jump_pads = {
        JumpPad{
            AABB{
                Vec2{Fix32::from_int(-2), Fix32::from_int(8)},
                Vec2{Fix32::from_int(2), Fix32::from_ratio(81, 10)}},
            Fix32::from_ratio(1, 4)}};
    embedded_pad_map.solids.push_back(AABB{
        Vec2{Fix32::from_int(-2), Fix32::from_ratio(803, 100)},
        Vec2{Fix32::from_int(2), Fix32::from_ratio(82, 10)}});
    CHECK(embedded_pad_map.valid(&error));

    World embedded_world;
    embedded_world.set_map(embedded_pad_map);
    embedded_world.reset(1, 1);
    Player* embedded_player =
        embedded_world.players().get(EntityId::make(0, 1));
    CHECK(embedded_player != nullptr);
    if (embedded_player != nullptr) {
        embedded_player->position = Vec2{
            Fix32::zero(),
            Fix32::from_int(5)};
        embedded_player->velocity = {};
        bool landed = false;
        for (int index = 0; index < 60; ++index) {
            embedded_world.step(empty);
            if (embedded_player->grounded != 0) {
                landed = true;
                break;
            }
        }
        CHECK(landed);
        CHECK(near_raw(
            embedded_player->position.y,
            Fix32::from_ratio(73, 10),
            2));
    }

    World ledge_world;
    ledge_world.set_map(embedded_pad_map);
    ledge_world.reset(1, 1);
    Player* ledge_player = ledge_world.players().get(EntityId::make(0, 1));
    CHECK(ledge_player != nullptr);
    if (ledge_player != nullptr) {
        ledge_player->position = Vec2{
            Fix32::zero(),
            Fix32::from_ratio(733, 100)};
        ledge_player->velocity = {};
        ledge_player->grounded = 1;
        InputFrame ledge_jump;
        PlayerCommand ledge_command;
        ledge_command.entity = ledge_player->id;
        ledge_command.command.tick = ledge_world.tick();
        ledge_command.command.buttons = kButtonJump;
        ledge_jump.commands.push_back(ledge_command);
        ledge_world.step(ledge_jump);
        for (int index = 0; index < 10; ++index) {
            ledge_world.step(empty);
        }
        CHECK(ledge_player->velocity.y < -Fix32::from_ratio(3, 10));
    }

    World duel;
    duel.set_map(arena);
    duel.reset(1, 2);
    for (int tick = 0; tick < 240 && duel.scores()[0] == 0; ++tick) {
        InputFrame shot;
        PlayerCommand shot_command;
        shot_command.entity = EntityId::make(0, 1);
        shot_command.command.tick = duel.tick();
        shot_command.command.aim_x = 32767;
        shot_command.command.buttons = kButtonFire;
        shot.commands.push_back(shot_command);
        duel.step(shot);
    }
    CHECK(duel.scores()[0] == 1);
    CHECK(duel.scores()[1] == 0);
    const Player* duel_shooter = duel.players().get(EntityId::make(0, 1));
    const Player* duel_victim = duel.players().get(EntityId::make(1, 1));
    CHECK(duel_shooter != nullptr);
    CHECK(duel_victim != nullptr);
    if (duel_shooter != nullptr) {
        CHECK(duel_shooter->kills == 1);
        CHECK(duel_shooter->deaths == 0);
        CHECK(duel_shooter->damage_dealt == 100);
    }
    if (duel_victim != nullptr) {
        CHECK(duel_victim->kills == 0);
        CHECK(duel_victim->deaths == 1);
        CHECK(duel_victim->blocked_damage == 50);
        CHECK(duel_victim->shield == 0);
    }
    for (int tick = 0; tick < 130; ++tick) {
        duel.step(empty);
    }
    const Player* respawned = duel.players().get(EntityId::make(1, 1));
    CHECK(respawned != nullptr);
    if (respawned != nullptr) {
        CHECK(respawned->alive != 0);
        CHECK(respawned->health == Fix32::from_int(100));
        CHECK(respawned->shield == 50);
        CHECK(respawned->blocked_damage == 50);
        CHECK(respawned->deaths == 1);
        CHECK(respawned->position.x == arena.spawns[5].x);
        CHECK(near_raw(respawned->position.y, arena.spawns[5].y, 2));
    }
}

void test_gadgets() {
    using namespace nsync::core;
    const Map arena = Map::default_arena();
    InputFrame empty;

    World regen;
    regen.set_map(arena);
    regen.reset(1, 1);
    Player* shielded = regen.players().get(EntityId::make(0, 1));
    CHECK(shielded != nullptr);
    if (shielded != nullptr) {
        shielded->shield = 10;
        shielded->shield_regen_delay = 1;
        const std::uint16_t before = shielded->shield;
        for (int index = 0; index < 20; ++index) {
            regen.step(empty);
        }
        CHECK(shielded->shield > before);
        CHECK(shielded->shield <= hero_stats(HeroType::Scout).shield);
        CHECK(shielded->shield_regen_delay == 0);
    }

    World battery;
    battery.set_map(arena);
    battery.reset(1, 1);
    Player* scout = battery.players().get(EntityId::make(0, 1));
    CHECK(scout != nullptr);
    if (scout != nullptr) {
        scout->hero = static_cast<std::uint8_t>(HeroType::Scout);
        scout->shield = 9;
        InputFrame press;
        PlayerCommand command;
        command.entity = scout->id;
        command.command.tick = battery.tick();
        command.command.buttons = kButtonGadget2;
        press.commands.push_back(command);
        battery.step(press);
        CHECK(scout->gadget_cooldowns[1] ==
              hero_gadget_stats(HeroType::Scout, 1).cooldown_ticks);
        CHECK(scout->invulnerable_ticks > 0);
    }

    World blade;
    blade.set_map(arena);
    blade.reset(1, 2);
    Player* attacker = blade.players().get(EntityId::make(0, 1));
    Player* victim = blade.players().get(EntityId::make(1, 1));
    CHECK(attacker != nullptr && victim != nullptr);
    if (attacker != nullptr && victim != nullptr) {
        attacker->hero = static_cast<std::uint8_t>(HeroType::Scout);
        victim->hero = static_cast<std::uint8_t>(HeroType::Gunner);
        attacker->aim = Vec2{Fix32::one(), Fix32::zero()};
        attacker->facing = 1;
        victim->position = attacker->position + Vec2{Fix32::from_int(1), Fix32::zero()};
        const Fix32 before = victim->health;
        InputFrame press;
        PlayerCommand command;
        command.entity = attacker->id;
        command.command.tick = blade.tick();
        command.command.buttons = kButtonGadget1;
        press.commands.push_back(command);
        blade.step(press);
        CHECK(victim->health < before);
    }

    World rocket;
    rocket.set_map(arena);
    rocket.reset(1, 1);
    Player* rocket_user = rocket.players().get(EntityId::make(0, 1));
    CHECK(rocket_user != nullptr);
    if (rocket_user != nullptr) {
        rocket_user->hero = static_cast<std::uint8_t>(HeroType::Soldier);
        InputFrame press;
        PlayerCommand command;
        command.entity = rocket_user->id;
        command.command.tick = rocket.tick();
        command.command.buttons = kButtonGadget1;
        press.commands.push_back(command);
        rocket.step(press);
        CHECK(rocket.projectiles().size() == 1);
        rocket.projectiles().for_each([&](const Projectile& projectile) {
            CHECK(projectile.damage == 46);
            CHECK(projectile.explosion_damage == 24);
            CHECK(projectile.blast_falloff_start ==
                  Fix32::from_ratio(55, 100));
            CHECK(projectile.blast_self_ratio ==
                  Fix32::from_ratio(70, 100));
            CHECK(projectile.blast_friend_ratio == Fix32::zero());
            CHECK(projectile.explosion_impulse == Fix32::from_int(700));
            CHECK(projectile.explosion_ground_impulse ==
                  Fix32::from_int(200));
            const Vec2 spawn = projectile.position - projectile.velocity;
            CHECK(near_raw(
                spawn.x - rocket_user->position.x,
                Fix32::from_ratio(6, 5),
                200));
        });
        CHECK(rocket_user->gadget_cooldowns[0] ==
              hero_gadget_stats(HeroType::Soldier, 0).cooldown_ticks);
    }

    World frag;
    frag.set_map(arena);
    frag.reset(1, 1);
    Player* gunner = frag.players().get(EntityId::make(0, 1));
    CHECK(gunner != nullptr);
    if (gunner != nullptr) {
        gunner->hero = static_cast<std::uint8_t>(HeroType::Gunner);
        InputFrame press;
        PlayerCommand command;
        command.entity = gunner->id;
        command.command.tick = frag.tick();
        command.command.buttons = kButtonGadget1;
        press.commands.push_back(command);
        frag.step(press);
        CHECK(frag.projectiles().size() == 1);
        frag.projectiles().for_each([&](const Projectile& projectile) {
            CHECK(projectile.explosion_damage == 160);
            CHECK(projectile.terrain_bounces == 255);
            CHECK(projectile.blast_falloff_start ==
                  Fix32::from_ratio(55, 100));
            CHECK(projectile.blast_self_ratio ==
                  Fix32::from_ratio(70, 100));
            CHECK(projectile.blast_friend_ratio == Fix32::zero());
            CHECK(projectile.explosion_impulse == Fix32::from_int(1000));
            const Vec2 spawn = projectile.position - projectile.velocity;
            CHECK(near_raw(
                spawn.x - gunner->position.x,
                Fix32::from_ratio(91, 100),
                200));
        });
    }

    World static_shield;
    static_shield.set_map(arena);
    static_shield.reset(1, 1);
    Player* shield_user = static_shield.players().get(EntityId::make(0, 1));
    CHECK(shield_user != nullptr);
    if (shield_user != nullptr) {
        shield_user->hero = static_cast<std::uint8_t>(HeroType::Gunner);
        InputFrame press;
        PlayerCommand command;
        command.entity = shield_user->id;
        command.command.tick = static_shield.tick();
        command.command.buttons = kButtonGadget2;
        press.commands.push_back(command);
        static_shield.step(press);
        CHECK(static_shield.projectiles().size() == 1);
        static_shield.projectiles().for_each([&](const Projectile& projectile) {
            CHECK(projectile.stun_ticks ==
                  hero_gadget_stats(HeroType::Gunner, 1).effect_ticks);
            CHECK(projectile.half_size == Fix32::from_ratio(13, 10));
            CHECK(projectile.blast_self_ratio == Fix32::zero());
            CHECK(projectile.blast_friend_ratio == Fix32::zero());
            CHECK(projectile.explode_on_expire == 1);
            const Vec2 spawn = projectile.position - projectile.velocity;
            CHECK(Fix32::abs(spawn.x - shield_user->position.x) <
                  Fix32::from_ratio(1, 100));
        });
    }

    World shield_follow;
    shield_follow.set_map(arena);
    shield_follow.reset(1, 2);
    Player* shield_caster =
        shield_follow.players().get(EntityId::make(0, 1));
    Player* shield_enemy =
        shield_follow.players().get(EntityId::make(1, 1));
    CHECK(shield_caster != nullptr);
    CHECK(shield_enemy != nullptr);
    if (shield_caster != nullptr && shield_enemy != nullptr) {
        shield_caster->hero = static_cast<std::uint8_t>(HeroType::Gunner);
        InputFrame press;
        PlayerCommand command;
        command.entity = shield_caster->id;
        command.command.tick = shield_follow.tick();
        command.command.buttons = kButtonGadget2;
        press.commands.push_back(command);
        shield_follow.step(press);
        CHECK(shield_follow.projectiles().size() == 1);
        shield_caster->position.x += Fix32::from_int(2);
        shield_follow.step(empty);
        shield_follow.projectiles().for_each([&](const Projectile& projectile) {
            CHECK(Fix32::abs(
                      projectile.position.x - shield_caster->position.x) <
                  Fix32::from_ratio(1, 100));
            CHECK(Fix32::abs(
                      projectile.position.y - shield_caster->position.y) <
                  Fix32::from_ratio(1, 100));
        });
        const Fix32 caster_health = shield_caster->health;
        shield_enemy->position = shield_caster->position;
        shield_enemy->shield = 0;
        shield_follow.step(empty);
        CHECK(shield_follow.projectiles().size() == 0);
        CHECK(shield_caster->health == caster_health);
        CHECK(shield_enemy->health ==
              Fix32::from_int(
                  hero_stats(static_cast<HeroType>(shield_enemy->hero))
                      .health - 20));
        CHECK(shield_enemy->stun_ticks > 0);
    }

    World kickstart;
    kickstart.set_map(arena);
    kickstart.reset(1, 1);
    Player* jump_user = kickstart.players().get(EntityId::make(0, 1));
    CHECK(jump_user != nullptr);
    if (jump_user != nullptr) {
        jump_user->hero = static_cast<std::uint8_t>(HeroType::Warrioress);
        InputFrame press;
        PlayerCommand command;
        command.entity = jump_user->id;
        command.command.tick = kickstart.tick();
        command.command.buttons = kButtonGadget1;
        press.commands.push_back(command);
        kickstart.step(press);
        CHECK(jump_user->velocity.x > Fix32::zero());
        CHECK(jump_user->gadget_cooldowns[0] ==
              hero_gadget_stats(HeroType::Warrioress, 0).cooldown_ticks);
    }

    World dash;
    dash.set_map(arena);
    dash.reset(1, 1);
    Player* dash_user = dash.players().get(EntityId::make(0, 1));
    CHECK(dash_user != nullptr);
    if (dash_user != nullptr) {
        dash_user->hero = static_cast<std::uint8_t>(HeroType::Warrioress);
        const Vec2 dash_start = dash_user->position;
        InputFrame press;
        PlayerCommand command;
        command.entity = dash_user->id;
        command.command.tick = dash.tick();
        command.command.buttons = kButtonGadget1;
        press.commands.push_back(command);
        dash.step(press);
        Fix32 peak_speed = Fix32::abs(dash_user->velocity.x);
        for (int index = 0; index < 30; ++index) {
            dash.step(empty);
            peak_speed =
                Fix32::max(peak_speed, Fix32::abs(dash_user->velocity.x));
        }
        CHECK(peak_speed <= Fix32::from_ratio(1, 2));
        CHECK(dash_user->position.x - dash_start.x > Fix32::one());
        CHECK(dash_user->position.x - dash_start.x < Fix32::from_int(8));
    }

    World air_dash;
    air_dash.set_map(arena);
    air_dash.reset(1, 1);
    Player* air_user = air_dash.players().get(EntityId::make(0, 1));
    CHECK(air_user != nullptr);
    if (air_user != nullptr) {
        air_user->hero = static_cast<std::uint8_t>(HeroType::Warrioress);
        const Fix32 start_y = air_user->position.y;
        InputFrame press;
        PlayerCommand command;
        command.entity = air_user->id;
        command.command.tick = air_dash.tick();
        command.command.buttons = kButtonGadget1;
        command.command.aim_y = -32767;
        press.commands.push_back(command);
        air_dash.step(press);
        CHECK(air_user->velocity.y < Fix32::zero());
        for (int index = 0; index < 12; ++index) {
            air_dash.step(empty);
        }
        CHECK(air_user->position.y < start_y - Fix32::from_ratio(1, 2));
    }

    Map wall_map = Map::default_arena();
    const AABB dash_wall{
        Vec2{Fix32::from_ratio(-7, 2), Fix32::from_int(9)},
        Vec2{Fix32::from_ratio(-338, 100), Fix32::from_int(12)}};
    wall_map.solids.push_back(dash_wall);
    World wall_dash;
    wall_dash.set_map(wall_map);
    wall_dash.reset(1, 1);
    Player* wall_user = wall_dash.players().get(EntityId::make(0, 1));
    CHECK(wall_user != nullptr);
    if (wall_user != nullptr) {
        wall_user->hero = static_cast<std::uint8_t>(HeroType::Warrioress);
        InputFrame press;
        PlayerCommand command;
        command.entity = wall_user->id;
        command.command.tick = wall_dash.tick();
        command.command.buttons = kButtonGadget1;
        press.commands.push_back(command);
        wall_dash.step(press);
        for (int index = 0; index < 40; ++index) {
            wall_dash.step(empty);
        }
        const AABB wall_box = AABB::from_center(
            wall_user->position,
            Vec2{
                Fix32::from_ratio(35, 100),
                Fix32::from_ratio(7, 10)});
        CHECK(wall_user->position.x < dash_wall.min.x);
        CHECK(!wall_box.overlaps(dash_wall));
    }

    World jet;
    jet.set_map(arena);
    jet.reset(1, 1);
    Player* jet_user = jet.players().get(EntityId::make(0, 1));
    CHECK(jet_user != nullptr);
    if (jet_user != nullptr) {
        jet_user->hero = static_cast<std::uint8_t>(HeroType::Assault);
        reset_player_gadgets(*jet_user);
        const Fix32 start_y = jet_user->position.y;
        const auto step_buttons = [&](std::uint16_t buttons) {
            InputFrame frame;
            PlayerCommand command;
            command.entity = jet_user->id;
            command.command.tick = jet.tick();
            command.command.buttons = buttons;
            frame.commands.push_back(command);
            jet.step(frame);
        };
        step_buttons(kButtonGadget2 | kButtonJump);
        CHECK(jet_user->velocity.y < Fix32::zero());
        CHECK(jet_user->velocity.y >= -Fix32::from_ratio(1, 10));
        CHECK(jet_user->gadget_active_ticks[1] > 0);
        for (int index = 0; index < 30; ++index) {
            step_buttons(kButtonJump);
            CHECK(jet_user->velocity.y >= -Fix32::from_ratio(1, 10));
        }
        CHECK(jet_user->position.y < start_y - Fix32::one());
        CHECK(jet_user->position.y > start_y - Fix32::from_int(8));
    }

    World droid;
    droid.set_map(arena);
    droid.reset(1, 1);
    Player* droid_user = droid.players().get(EntityId::make(0, 1));
    CHECK(droid_user != nullptr);
    if (droid_user != nullptr) {
        droid_user->hero = static_cast<std::uint8_t>(HeroType::Soldier);
        InputFrame press;
        PlayerCommand command;
        command.entity = droid_user->id;
        command.command.tick = droid.tick();
        command.command.buttons = kButtonGadget2;
        press.commands.push_back(command);
        droid.step(press);
        CHECK(droid.projectiles().size() == 1);
        droid.projectiles().for_each([&](const Projectile& projectile) {
            CHECK(projectile.slow_ticks ==
                  hero_gadget_stats(HeroType::Soldier, 1).effect_ticks);
            const Vec2 spawn = projectile.position - projectile.velocity;
            CHECK(Fix32::abs(spawn.x - droid_user->position.x) <
                  Fix32::from_ratio(1, 100));
        });
    }

    World heal;
    heal.set_map(arena);
    heal.reset(1, 1);
    Player* medic = heal.players().get(EntityId::make(0, 1));
    CHECK(medic != nullptr);
    if (medic != nullptr) {
        medic->hero = static_cast<std::uint8_t>(HeroType::Soldier);
        medic->health = Fix32::from_int(50);
        InputFrame press;
        PlayerCommand command;
        command.entity = medic->id;
        command.command.tick = heal.tick();
        command.command.buttons = kButtonGadget3;
        press.commands.push_back(command);
        heal.step(press);
        const Fix32 before = medic->health;
        for (int index = 0; index < 61; ++index) {
            heal.step(empty);
        }
        CHECK(medic->health > before);
    }

    World injection;
    injection.set_map(arena);
    injection.reset(1, 1);
    Player* injector = injection.players().get(EntityId::make(0, 1));
    CHECK(injector != nullptr);
    if (injector != nullptr) {
        injector->hero = static_cast<std::uint8_t>(HeroType::Gunner);
        injector->health = Fix32::from_int(20);
        InputFrame press;
        PlayerCommand command;
        command.entity = injector->id;
        command.command.tick = injection.tick();
        command.command.buttons = kButtonGadget3;
        press.commands.push_back(command);
        injection.step(press);
        CHECK(injector->health == Fix32::from_int(20));
        for (int index = 0; index < 181; ++index) {
            injection.step(empty);
        }
        CHECK(injector->health.to_int_round() == 104);
    }
}

void test_jetpack_fuel() {
    using namespace nsync::core;
    const Map arena = Map::default_arena();
    const InputFrame empty;

    const auto prepare = [&](World& world) {
        world.set_map(arena);
        world.reset(1, 1);
        Player* player = world.players().get(EntityId::make(0, 1));
        CHECK(player != nullptr);
        if (player != nullptr) {
            player->hero = static_cast<std::uint8_t>(HeroType::Assault);
            reset_player_gadgets(*player);
        }
        return player;
    };
    const auto press_gadget = [](World& world, const Player& player) {
        InputFrame frame;
        PlayerCommand command;
        command.entity = player.id;
        command.command.tick = world.tick();
        command.command.buttons = kButtonGadget2;
        frame.commands.push_back(command);
        world.step(frame);
    };
    const auto step_buttons = [](
        World& world,
        const Player& player,
        std::uint16_t buttons) {
        InputFrame frame;
        PlayerCommand command;
        command.entity = player.id;
        command.command.tick = world.tick();
        command.command.buttons = buttons;
        frame.commands.push_back(command);
        world.step(frame);
    };

    World drain;
    Player* drain_user = prepare(drain);
    if (drain_user != nullptr) {
        CHECK(drain_user->gadget_fuel[1] == kJetPackTuning.capacity);
        press_gadget(drain, *drain_user);
        CHECK(drain_user->gadget_active_ticks[1] > 0);
        CHECK(drain_user->gadget_fuel[1] ==
              kJetPackTuning.capacity -
                  kJetPackTuning.consumption_per_tick);
        for (int tick = 0; tick < 299; ++tick) {
            drain.step(empty);
        }
        CHECK(drain_user->gadget_fuel[1] == Fix32::zero());
        CHECK(drain_user->gadget_active_ticks[1] == 0);
    }

    World toggle;
    Player* toggle_user = prepare(toggle);
    if (toggle_user != nullptr) {
        press_gadget(toggle, *toggle_user);
        for (int tick = 0; tick < 5; ++tick) {
            toggle.step(empty);
        }
        toggle.step(empty);
        press_gadget(toggle, *toggle_user);
        CHECK(toggle_user->gadget_active_ticks[1] == 0);
        const Fix32 fuel_after_off = toggle_user->gadget_fuel[1];
        toggle.step(empty);
        press_gadget(toggle, *toggle_user);
        CHECK(toggle_user->gadget_active_ticks[1] == 0);
        for (int tick = 0; tick < 10; ++tick) {
            toggle.step(empty);
        }
        CHECK(toggle_user->gadget_fuel[1] > fuel_after_off);
        CHECK(toggle_user->gadget_fuel[1] <=
              fuel_after_off + Fix32::from_int(6));
        for (int tick = 0; tick < 60; ++tick) {
            toggle.step(empty);
        }
        toggle.step(empty);
        press_gadget(toggle, *toggle_user);
        CHECK(toggle_user->gadget_active_ticks[1] > 0);
    }

    World gate;
    Player* gate_user = prepare(gate);
    if (gate_user != nullptr) {
        gate_user->gadget_fuel[1] =
            kJetPackTuning.operating_fuel - Fix32::one();
        press_gadget(gate, *gate_user);
        CHECK(gate_user->gadget_active_ticks[1] == 0);
        CHECK(gate_user->gadget_cooldowns[1] == 0);
        gate.step(empty);
        gate_user->gadget_fuel[1] = kJetPackTuning.operating_fuel;
        press_gadget(gate, *gate_user);
        CHECK(gate_user->gadget_active_ticks[1] > 0);
    }

    World shock;
    Player* shock_user = prepare(shock);
    if (shock_user != nullptr) {
        press_gadget(shock, *shock_user);
        CHECK(shock_user->gadget_active_ticks[1] > 0);
        shock_user->stun_ticks = 30;
        shock.step(empty);
        CHECK(shock_user->gadget_active_ticks[1] == 0);
    }

    World model;
    Player* model_user = prepare(model);
    if (model_user != nullptr) {
        const Fix32 start_x = model_user->position.x;
        const Fix32 start_y = model_user->position.y;
        press_gadget(model, *model_user);
        for (int tick = 0; tick < 10; ++tick) {
            model.step(empty);
        }
        CHECK(model_user->velocity.x == Fix32::zero());
        CHECK(model_user->velocity.y == Fix32::zero());
        CHECK(model_user->position.x == start_x);
        CHECK(model_user->position.y == start_y);
        for (int tick = 0; tick < 10; ++tick) {
            step_buttons(model, *model_user, kButtonJump);
        }
        CHECK(model_user->velocity.y < Fix32::zero());
        CHECK(model_user->velocity.y >= -Fix32::from_ratio(1, 10));
        CHECK(model_user->position.y < start_y);
        for (int tick = 0; tick < 10; ++tick) {
            step_buttons(
                model,
                *model_user,
                kButtonLeft | kButtonJump);
        }
        CHECK(model_user->velocity.x < Fix32::zero());
        CHECK(model_user->velocity.x >= -Fix32::from_ratio(1, 10));
        CHECK(model_user->velocity.y < Fix32::zero());
        CHECK(model_user->position.x < start_x);
        for (int tick = 0; tick < 10; ++tick) {
            step_buttons(model, *model_user, kButtonDown);
        }
        CHECK(model_user->velocity.y > Fix32::zero());
        CHECK(model_user->velocity.y <= Fix32::from_ratio(1, 10));
        for (int tick = 0; tick < 10; ++tick) {
            step_buttons(model, *model_user, kButtonRight);
        }
        CHECK(model_user->velocity.x > Fix32::zero());
        CHECK(model_user->velocity.x <= Fix32::from_ratio(1, 10));
        const Fix32 before_slowdown = model_user->velocity.x;
        for (int tick = 0; tick < 10; ++tick) {
            model.step(empty);
        }
        CHECK(model_user->velocity.x < before_slowdown);
        CHECK(model_user->velocity.x > Fix32::zero());
    }
}

void test_weapons_and_match() {
    using namespace nsync::core;
    const Map arena = Map::default_arena();
    InputFrame empty;

    World muzzle;
    muzzle.set_map(arena);
    muzzle.reset(9, 1);
    muzzle.step(empty);
    InputFrame rifle_fire;
    PlayerCommand rifle_command;
    rifle_command.entity = EntityId::make(0, 1);
    rifle_command.command.tick = muzzle.tick();
    rifle_command.command.buttons = kButtonFire;
    rifle_command.command.aim_x = 32767;
    rifle_fire.commands.push_back(rifle_command);
    muzzle.step(rifle_fire);
    const Player* muzzle_player =
        muzzle.players().get(EntityId::make(0, 1));
    CHECK(muzzle_player != nullptr);
    const WeaponStats& rifle = weapon_stats(WeaponType::Rifle);
    if (muzzle_player != nullptr) {
        CHECK(muzzle_player->dispersion_ticks == rifle.dispersion_ticks);
        const Vec2 expected_muzzle =
            aim_muzzle_point(rifle, muzzle_player->position, muzzle_player->aim);
        bool saw_projectile = false;
        muzzle.projectiles().for_each([&](const Projectile& projectile) {
            saw_projectile = true;
            CHECK(projectile.position == expected_muzzle + projectile.velocity);
            CHECK(projectile.life_ticks == rifle.projectile_life - 1);
            CHECK(projectile.half_size == rifle.projectile_half_size);
            CHECK(near_raw(
                projectile.velocity.x,
                rifle.projectile_speed,
                2));
        });
        CHECK(saw_projectile);
    }

    World shotgun;
    shotgun.set_map(arena);
    shotgun.reset(1, 1);
    InputFrame switch_to_shotgun;
    PlayerCommand switch_command;
    switch_command.entity = EntityId::make(0, 1);
    switch_command.command.tick = 0;
    switch_command.command.buttons = kButtonWeaponSwitch;
    switch_to_shotgun.commands.push_back(switch_command);
    shotgun.step(switch_to_shotgun);

    const Player* shotgun_player =
        shotgun.players().get(EntityId::make(0, 1));
    CHECK(shotgun_player != nullptr);
    if (shotgun_player != nullptr) {
        CHECK(shotgun_player->weapon ==
              static_cast<std::uint8_t>(WeaponType::Shotgun));
        CHECK(shotgun_player->ammo ==
              weapon_stats(WeaponType::Shotgun).magazine);
    }

    InputFrame shotgun_fire;
    PlayerCommand shotgun_shot;
    shotgun_shot.entity = EntityId::make(0, 1);
    shotgun_shot.command.tick = shotgun.tick();
    shotgun_shot.command.buttons = kButtonFire;
    shotgun_shot.command.aim_x = 32767;
    shotgun_shot.command.aim_y = -2867;
    shotgun_fire.commands.push_back(shotgun_shot);
    shotgun.step(shotgun_fire);
    const WeaponStats& shotgun_stats = weapon_stats(WeaponType::Shotgun);
    CHECK(shotgun.projectiles().size() == shotgun_stats.pellets);
    shotgun_player = shotgun.players().get(EntityId::make(0, 1));
    CHECK(shotgun_player != nullptr);
    if (shotgun_player != nullptr) {
        CHECK(shotgun_player->ammo == shotgun_stats.magazine - 1);
        const Vec2 expected_muzzle = aim_muzzle_point(
            shotgun_stats,
            shotgun_player->position,
            shotgun_player->aim);
        Fix32 min_vy = Fix32::one();
        Fix32 max_vy = -Fix32::one();
        bool saw_projectile = false;
        shotgun.projectiles().for_each([&](const Projectile& projectile) {
            saw_projectile = true;
            CHECK(projectile.position - projectile.velocity ==
                  expected_muzzle);
            CHECK(projectile.life_ticks == 1);
            CHECK(projectile.half_size ==
                  shotgun_stats.projectile_half_size);
            CHECK(near_raw(
                projectile.velocity.length(),
                shotgun_stats.projectile_speed,
                200));
            min_vy = Fix32::min(min_vy, projectile.velocity.y);
            max_vy = Fix32::max(max_vy, projectile.velocity.y);
        });
        CHECK(saw_projectile);
        CHECK(min_vy < Fix32::zero());
        CHECK(max_vy > Fix32::zero());
        CHECK(max_vy - min_vy > Fix32::one());
        CHECK(Fix32::abs(min_vy) <= shotgun_stats.projectile_speed);
        CHECK(Fix32::abs(max_vy) <= shotgun_stats.projectile_speed);
    }

    World burst;
    burst.set_map(arena);
    burst.reset(1, 1);
    Player* burst_player = burst.players().get(EntityId::make(0, 1));
    CHECK(burst_player != nullptr);
    if (burst_player != nullptr) {
        burst_player->weapon = static_cast<std::uint8_t>(WeaponType::Burst);
        burst_player->ammo = weapon_stats(WeaponType::Burst).magazine;
        InputFrame burst_fire;
        PlayerCommand burst_shot;
        burst_shot.entity = burst_player->id;
        burst_shot.command.tick = burst.tick();
        burst_shot.command.buttons = kButtonFire;
        burst_shot.command.aim_x = 32767;
        burst_fire.commands.push_back(burst_shot);
        burst.step(burst_fire);
        const WeaponStats& burst_stats = weapon_stats(WeaponType::Burst);
        CHECK(burst.projectiles().size() == 1);
        burst.projectiles().for_each([&](const Projectile& projectile) {
            CHECK(projectile.life_ticks == burst_stats.projectile_life - 1);
            CHECK(projectile.half_size ==
                  burst_stats.projectile_half_size);
            CHECK(projectile.terrain_bounces == burst_stats.terrain_bounces);
            CHECK(projectile.explosive == 0);
            CHECK(near_raw(
                projectile.velocity.length(),
                burst_stats.projectile_speed,
                200));
        });
    }

    World auto_reload;
    auto_reload.set_map(arena);
    auto_reload.reset(1, 2);
    Player* reload_shooter =
        auto_reload.players().get(EntityId::make(0, 1));
    CHECK(reload_shooter != nullptr);
    if (reload_shooter != nullptr) {
        reload_shooter->ammo = 1;
    }
    InputFrame last_shot;
    PlayerCommand last_shot_command;
    last_shot_command.entity = EntityId::make(0, 1);
    last_shot_command.command.tick = auto_reload.tick();
    last_shot_command.command.buttons = kButtonFire;
    last_shot_command.command.aim_x = 32767;
    last_shot.commands.push_back(last_shot_command);
    auto_reload.step(last_shot);
    reload_shooter = auto_reload.players().get(EntityId::make(0, 1));
    CHECK(reload_shooter != nullptr);
    if (reload_shooter != nullptr) {
        CHECK(reload_shooter->ammo == 0);
    }
    auto_reload.step(empty);
    reload_shooter = auto_reload.players().get(EntityId::make(0, 1));
    CHECK(reload_shooter != nullptr);
    if (reload_shooter != nullptr) {
        CHECK(reload_shooter->reload_timer > 0);
        for (int index = 0;
             index < weapon_stats(WeaponType::Rifle).reload_ticks;
             ++index) {
            auto_reload.step(empty);
        }
    }
    reload_shooter = auto_reload.players().get(EntityId::make(0, 1));
    CHECK(reload_shooter != nullptr);
    if (reload_shooter != nullptr) {
        CHECK(reload_shooter->ammo ==
              weapon_stats(WeaponType::Rifle).magazine);
    }

    bool saw_critical = false;
    for (std::uint64_t seed = 1; seed <= 64 && !saw_critical; ++seed) {
        World critical;
        critical.set_map(arena);
        critical.reset(seed, 1);
        Player* critical_player =
            critical.players().get(EntityId::make(0, 1));
        CHECK(critical_player != nullptr);
        if (critical_player == nullptr) {
            return;
        }
        critical_player->weapon =
            static_cast<std::uint8_t>(WeaponType::Rifle);
        critical_player->ammo =
            weapon_stats(WeaponType::Rifle).magazine;
        InputFrame critical_fire;
        PlayerCommand critical_shot;
        critical_shot.entity = critical_player->id;
        critical_shot.command.tick = critical.tick();
        critical_shot.command.buttons = kButtonFire;
        critical_shot.command.aim_x = 32767;
        critical_fire.commands.push_back(critical_shot);
        critical.step(critical_fire);
        critical.projectiles().for_each([&](const Projectile& projectile) {
            if (projectile.critical_ratio > Fix32::zero()) {
                saw_critical = true;
                CHECK(projectile.critical_ratio ==
                      Fix32::from_ratio(3, 10));
            }
        });
    }
    CHECK(saw_critical);

    World rocket;
    rocket.set_map(arena);
    rocket.set_match_rules(10000, 1);
    rocket.reset(1, 2);
    InputFrame switch_once;
    PlayerCommand first_switch;
    first_switch.entity = EntityId::make(0, 1);
    first_switch.command.tick = 0;
    first_switch.command.buttons = kButtonWeaponSwitch;
    switch_once.commands.push_back(first_switch);
    rocket.step(switch_once);
    for (int index = 0; index < 15; ++index) {
        rocket.step(empty);
    }
    InputFrame switch_twice;
    PlayerCommand second_switch;
    second_switch.entity = EntityId::make(0, 1);
    second_switch.command.tick = rocket.tick();
    second_switch.command.buttons = kButtonWeaponSwitch;
    switch_twice.commands.push_back(second_switch);
    rocket.step(switch_twice);
    const Player* rocket_player = rocket.players().get(EntityId::make(0, 1));
    CHECK(rocket_player != nullptr);
    if (rocket_player != nullptr) {
        CHECK(rocket_player->weapon ==
              static_cast<std::uint8_t>(WeaponType::Rocket));
    }

    for (int index = 0; index < 240 && !rocket.match_over(); ++index) {
        InputFrame fire;
        PlayerCommand command;
        command.entity = EntityId::make(0, 1);
        command.command.tick = rocket.tick();
        command.command.buttons = kButtonFire;
        command.command.aim_x = 32767;
        fire.commands.push_back(command);
        rocket.step(fire);
    }
    CHECK(rocket.scores()[0] == 1);
    CHECK(rocket.winner() == 1);
    CHECK(rocket.match_over());
    const Player* rocket_attacker =
        rocket.players().get(EntityId::make(0, 1));
    const Player* rocket_victim =
        rocket.players().get(EntityId::make(1, 1));
    CHECK(rocket_attacker != nullptr);
    CHECK(rocket_victim != nullptr);
    if (rocket_attacker != nullptr) {
        CHECK(rocket_attacker->kills == 1);
        CHECK(rocket_attacker->damage_dealt > 0);
    }
    if (rocket_victim != nullptr) {
        CHECK(rocket_victim->deaths == 1);
    }

    World timer;
    timer.set_map(arena);
    timer.reset(1, 2);
    timer.set_match_rules(30, 30);
    for (int index = 0; index < 30; ++index) {
        timer.step(empty);
    }
    CHECK(timer.match_over());
    CHECK(timer.winner() == 3);

    CHECK(hero_stats(HeroType::Scout).health == 100);
    CHECK(hero_stats(HeroType::Gunner).health == 140);
    CHECK(hero_stats(HeroType::Assault).health == 120);
    CHECK(hero_stats(HeroType::Scout).shield == 50);
    CHECK(hero_stats(HeroType::Gunner).shield == 70);
    CHECK(hero_stats(HeroType::Assault).shield == 60);
    CHECK(hero_gadget_stats(HeroType::Scout, 0).type ==
          GadgetType::RubyBlade);
    CHECK(hero_gadget_stats(HeroType::Gunner, 1).type ==
          GadgetType::StaticShield);
    CHECK(hero_gadget_stats(HeroType::Gunner, 2).type ==
          GadgetType::Injection);
    CHECK(hero_gadget_stats(HeroType::Assault, 0).type ==
          GadgetType::RocketSalvo);
    CHECK(hero_gadget_stats(HeroType::Assault, 2).type ==
          GadgetType::Toughness);
    CHECK(hero_gadget_stats(HeroType::Soldier, 0).type ==
          GadgetType::RocketLaunch);
    CHECK(hero_gadget_stats(HeroType::Soldier, 2).type ==
          GadgetType::FirstAid);
    CHECK(hero_stats(HeroType::Gunner).run_speed <
          hero_stats(HeroType::Scout).run_speed);
    CHECK(hero_weapon(HeroType::Scout) == WeaponType::Rocket);
    CHECK(hero_weapon(HeroType::Gunner) == WeaponType::Smg);
    CHECK(hero_weapon(HeroType::Assault) == WeaponType::Shotgun);
    CHECK(hero_weapon(HeroType::Warrioress) == WeaponType::Axe);
    CHECK(weapon_is_melee(WeaponType::Axe));
    CHECK(weapon_stats(WeaponType::Axe).magazine == 0);
    CHECK(weapon_stats(WeaponType::Axe).fire_interval == 84);
    CHECK(weapon_stats(WeaponType::Axe).damage == 65);
    CHECK(weapon_stats(WeaponType::Axe).range ==
          Fix32::from_ratio(135, 100));
    CHECK(weapon_stats(WeaponType::Axe).critical_chance ==
          Fix32::from_ratio(1, 5));
    CHECK(weapon_stats(WeaponType::Axe).critical_ratio ==
          Fix32::from_ratio(3, 10));
    for (std::size_t index = 0; index < content().hero_count(); ++index) {
        Player equipped;
        equipped.hero = static_cast<std::uint8_t>(index);
        equip_hero_default_weapon(equipped);
        const WeaponStats* expected =
            content().find_weapon(content().hero(index).weapon);
        CHECK(expected != nullptr);
        if (expected != nullptr) {
            CHECK(equipped.weapon ==
                  static_cast<std::uint8_t>(expected->type));
            CHECK(equipped.ammo == expected->magazine);
        }

        World hero_fire;
        hero_fire.set_map(arena);
        hero_fire.reset(1, 1);
        Player* shooter = hero_fire.players().get(EntityId::make(0, 1));
        CHECK(shooter != nullptr);
        if (shooter != nullptr) {
            shooter->hero = static_cast<std::uint8_t>(index);
            equip_hero_default_weapon(*shooter);
            const bool melee_weapon =
                expected != nullptr && weapon_is_melee(expected->type);
            InputFrame fire_input;
            PlayerCommand fire_command;
            fire_command.entity = shooter->id;
            fire_command.command.aim_x = 32767;
            bool fired = false;
            for (int tick = 0; tick < 120 && !fired; ++tick) {
                fire_command.command.tick = hero_fire.tick();
                fire_command.command.buttons = kButtonFire;
                fire_input.commands = {fire_command};
                hero_fire.step(fire_input);
                fired = melee_weapon
                    ? shooter->fire_cooldown > 0
                    : !hero_fire.projectiles().empty();
            }
            CHECK(fired);
        }
    }
    World melee_world;
    melee_world.set_map(arena);
    melee_world.reset(1, 2);
    Player* melee_attacker =
        melee_world.players().get(EntityId::make(0, 1));
    Player* melee_victim =
        melee_world.players().get(EntityId::make(1, 1));
    CHECK(melee_attacker != nullptr);
    CHECK(melee_victim != nullptr);
    if (melee_attacker != nullptr && melee_victim != nullptr) {
        melee_attacker->hero =
            static_cast<std::uint8_t>(HeroType::Warrioress);
        equip_hero_default_weapon(*melee_attacker);
        melee_attacker->position =
            Vec2{Fix32::zero(), Fix32::from_ratio(103, 10)};
        melee_attacker->velocity = {};
        melee_attacker->aim = Vec2{Fix32::one(), Fix32::zero()};
        melee_attacker->facing = 1;
        melee_victim->position =
            Vec2{Fix32::one(), Fix32::from_ratio(103, 10)};
        melee_victim->velocity = {};
        melee_victim->health = Fix32::from_int(100);
        melee_victim->shield = 0;
        melee_victim->invulnerable_ticks = 0;
        InputFrame melee_fire;
        PlayerCommand melee_command;
        melee_command.entity = melee_attacker->id;
        melee_command.command.tick = melee_world.tick();
        melee_command.command.buttons = kButtonFire;
        melee_command.command.aim_x = 32767;
        melee_fire.commands.push_back(melee_command);
        melee_world.step(melee_fire);
        CHECK(melee_world.projectiles().empty());
        CHECK(melee_victim->health >= Fix32::from_int(15));
        CHECK(melee_victim->health <= Fix32::from_int(35));

        melee_victim->health = Fix32::from_int(100);
        melee_victim->position =
            Vec2{Fix32::from_int(3), Fix32::from_ratio(103, 10)};
        melee_attacker->fire_cooldown = 0;
        melee_world.step(melee_fire);
        CHECK(melee_victim->health == Fix32::from_int(100));
    }

    CHECK(weapon_stats(WeaponType::Smg).magazine == 40);
    CHECK(weapon_stats(WeaponType::Smg).fire_interval == 8);
    CHECK(weapon_stats(WeaponType::Smg).reload_ticks == 132);
    CHECK(weapon_stats(WeaponType::Smg).damage == 13);
    CHECK(weapon_stats(WeaponType::Smg).projectile_life == 2);
    CHECK(weapon_stats(WeaponType::Smg).dispersion_degrees ==
          Fix32::from_int(6));
    CHECK(weapon_stats(WeaponType::Smg).warmup_ticks == 40);
    CHECK(weapon_stats(WeaponType::Smg).platform_puncher == 1);
    CHECK(weapon_stats(WeaponType::Shotgun).damage == 13);
    CHECK(weapon_stats(WeaponType::Shotgun).pellets == 7);
    CHECK(weapon_stats(WeaponType::Shotgun).fire_interval == 72);
    CHECK(weapon_stats(WeaponType::Shotgun).reload_ticks == 84);
    CHECK(weapon_stats(WeaponType::Shotgun).reload_size == 2);
    CHECK(weapon_stats(WeaponType::Shotgun).divergence_degrees ==
          Fix32::from_int(13));
    CHECK(weapon_stats(WeaponType::Burst).fire_interval == 48);
    CHECK(weapon_stats(WeaponType::Burst).reload_ticks == 120);
    CHECK(weapon_stats(WeaponType::Rifle).critical_chance ==
          Fix32::from_ratio(1, 5));
    CHECK(weapon_stats(WeaponType::Rifle).critical_ratio ==
          Fix32::from_ratio(3, 10));
    CHECK(weapon_stats(WeaponType::Rocket).spread == Fix32::zero());
    CHECK(weapon_stats(WeaponType::Rocket).explosion_impulse ==
          Fix32::from_int(400));
    CHECK(weapon_stats(WeaponType::Rocket).explosion_ground_impulse ==
          Fix32::from_int(100));
    CHECK(weapon_stats(WeaponType::Grenade).explosion_impulse ==
          Fix32::from_int(1000));
    CHECK(weapon_stats(WeaponType::Grenade).explosion_ground_impulse ==
          Fix32::from_int(300));
    CHECK(weapon_stats(WeaponType::Burst).magazine == 6);
    CHECK(weapon_stats(WeaponType::Burst).damage == 60);
    CHECK(weapon_stats(WeaponType::Burst).projectile_life == 60);
    CHECK(weapon_stats(WeaponType::Burst).projectile_speed ==
          Fix32::from_ratio(1, 3));
    CHECK(weapon_stats(WeaponType::Burst).terrain_bounces == 2);
    CHECK(weapon_stats(WeaponType::Rifle).magazine == 15);
    CHECK(weapon_stats(WeaponType::Rifle).reload_ticks == 108);
    CHECK(weapon_stats(WeaponType::Rifle).damage == 16);
    CHECK(weapon_stats(WeaponType::Rocket).magazine == 5);
    CHECK(weapon_stats(WeaponType::Rocket).fire_interval == 48);
    CHECK(weapon_stats(WeaponType::Rocket).reload_ticks == 108);
    CHECK(weapon_stats(WeaponType::Rocket).damage == 36);
    CHECK(weapon_stats(WeaponType::Rocket).explosion_damage == 18);
    CHECK(weapon_stats(WeaponType::Rocket).explode_on_expire == 1);
    CHECK(weapon_stats(WeaponType::Grenade).magazine == 5);
    CHECK(weapon_stats(WeaponType::Grenade).fire_interval == 84);
    CHECK(weapon_stats(WeaponType::Grenade).damage == 0);
    CHECK(weapon_stats(WeaponType::Grenade).explosion_damage == 60);
    CHECK(weapon_stats(WeaponType::Grenade).terrain_bounces == 2);
    CHECK(weapon_stats(WeaponType::Grenade).explode_on_expire == 1);
    CHECK(weapon_stats(WeaponType::Grenade).explosive == 1);

    World bots;
    bots.set_map(arena);
    bots.reset(1, 4);
    bots.set_bot_slots(0x0E);
    const Player* gunner = bots.players().get(EntityId::make(2, 1));
    CHECK(gunner != nullptr);
    if (gunner != nullptr) {
        CHECK(gunner->hero == static_cast<std::uint8_t>(HeroType::Gunner));
        CHECK(gunner->health == Fix32::from_int(140));
        CHECK(gunner->shield == 70);
    }
    World ten_slots;
    ten_slots.set_map(arena);
    ten_slots.reset(1, 10);
    const Player* third_hero = ten_slots.players().get(EntityId::make(4, 1));
    CHECK(third_hero != nullptr);
    if (third_hero != nullptr) {
        CHECK(third_hero->hero ==
              static_cast<std::uint8_t>(HeroType::Assault));
    }

    World bots_again;
    bots_again.set_map(arena);
    bots_again.reset(1, 4);
    bots_again.set_bot_slots(0x0E);
    const Vec2 bot_spawn = arena.spawns[5];
    bool bot_moved = false;
    bool bot_attacked = false;
    for (int index = 0; index < 240; ++index) {
        bots.step(empty);
        bots_again.step(empty);
        CHECK(bots.hash() == bots_again.hash());
        const Player* live_bot = bots.players().get(EntityId::make(1, 1));
        if (live_bot != nullptr && live_bot->position.x != bot_spawn.x) {
            bot_moved = true;
        }
        if (!bots.projectiles().empty() ||
            bots.scores()[0] + bots.scores()[1] > 0) {
            bot_attacked = true;
        }
    }
    CHECK(bots.players().size() == 4);
    const Player* bot = bots.players().get(EntityId::make(1, 1));
    CHECK(bot != nullptr);
    if (bot != nullptr) {
        CHECK(bot->bot != 0);
        CHECK(bot_moved);
        CHECK(bot_attacked);
    }

    World full_match;
    full_match.set_map(arena);
    full_match.reset(1, 4);
    full_match.set_bot_slots(0x0E);
    full_match.set_match_rules(18000, 30);
    for (int index = 0; index < 18000 && !full_match.match_over(); ++index) {
        full_match.step(empty);
    }
    CHECK(full_match.match_over());
    CHECK(full_match.tick() <= 18000);
}

void test_point_blank_weapon_hit() {
    using namespace nsync::core;
    World world;
    world.set_map(Map::default_arena());
    world.reset(1, 2);
    world.step(InputFrame{});

    Player* attacker = nullptr;
    Player* target = nullptr;
    world.players().for_each([&](Player& player) {
        if (player.slot == 0) {
            attacker = &player;
        } else if (player.slot == 1) {
            target = &player;
        }
    });
    CHECK(attacker != nullptr);
    CHECK(target != nullptr);
    if (attacker == nullptr || target == nullptr) {
        return;
    }

    const Vec2 point{Fix32::zero(), Fix32::from_ratio(101, 10)};
    attacker->position = point;
    target->position = point;
    attacker->aim = Vec2{Fix32::one(), Fix32::zero()};
    attacker->facing = 1;
    target->health = Fix32::from_int(100);
    target->shield = 0;
    world.step(InputFrame{});

    const Fix32 before = target->health;
    InputFrame fire;
    PlayerCommand command;
    command.entity = attacker->id;
    command.command.tick = world.tick();
    command.command.buttons = kButtonFire;
    command.command.aim_x = 32767;
    fire.commands.push_back(command);
    world.step(fire);

    CHECK(target->health < before);

    World muzzle_world;
    muzzle_world.set_map(Map::default_arena());
    muzzle_world.reset(1, 2);
    muzzle_world.step(InputFrame{});
    Player* muzzle_attacker = nullptr;
    Player* muzzle_target = nullptr;
    muzzle_world.players().for_each([&](Player& player) {
        if (player.slot == 0) {
            muzzle_attacker = &player;
        } else if (player.slot == 1) {
            muzzle_target = &player;
        }
    });
    CHECK(muzzle_attacker != nullptr);
    CHECK(muzzle_target != nullptr);
    if (muzzle_attacker == nullptr || muzzle_target == nullptr) {
        return;
    }
    muzzle_attacker->position = point;
    muzzle_target->position = point;
    muzzle_target->team = muzzle_attacker->team;
    muzzle_attacker->aim = Vec2{Fix32::one(), Fix32::zero()};
    muzzle_attacker->facing = 1;
    muzzle_world.step(InputFrame{});

    const Vec2 expected_muzzle = aim_muzzle_point(
        weapon_stats(WeaponType::Rifle),
        muzzle_attacker->position,
        muzzle_attacker->aim);
    InputFrame muzzle_fire;
    PlayerCommand muzzle_command;
    muzzle_command.entity = muzzle_attacker->id;
    muzzle_command.command.tick = muzzle_world.tick();
    muzzle_command.command.buttons = kButtonFire;
    muzzle_command.command.aim_x = 32767;
    muzzle_fire.commands.push_back(muzzle_command);
    muzzle_world.step(muzzle_fire);
    bool saw_projectile = false;
    muzzle_world.projectiles().for_each([&](const Projectile& projectile) {
        saw_projectile = true;
        CHECK(near_raw(
            projectile.position.x - projectile.velocity.x,
            expected_muzzle.x,
            2048));
        CHECK(near_raw(
            projectile.position.y - projectile.velocity.y,
            expected_muzzle.y,
            2048));
    });
    CHECK(saw_projectile);
}

void test_projectile_wall_and_point_blank() {
    using namespace nsync::core;
    const Fix32 no_wall = Fix32::from_int(-1000);
    const auto fire_smg = [no_wall](Map map, Fix32 victim_x, Fix32 wall_x) {
        if (wall_x != no_wall) {
            map.solids.push_back(AABB{
                Vec2{wall_x, Fix32::from_int(9)},
                Vec2{wall_x + Fix32::from_ratio(12, 100),
                     Fix32::from_int(11)}});
        }
        World world;
        world.set_map(map);
        world.reset(1, 2);
        world.step(InputFrame{});
        Player* shooter = world.players().get(EntityId::make(0, 1));
        Player* victim = world.players().get(EntityId::make(1, 1));
        CHECK(shooter != nullptr);
        CHECK(victim != nullptr);
        if (shooter == nullptr || victim == nullptr) {
            return Fix32::from_int(100);
        }
        shooter->position =
            Vec2{Fix32::zero(), Fix32::from_ratio(103, 10)};
        shooter->velocity = {};
        victim->position =
            Vec2{victim_x, Fix32::from_ratio(103, 10)};
        victim->velocity = {};
        victim->health = Fix32::from_int(100);
        victim->shield = 0;
        victim->invulnerable_ticks = 0;
        shooter->weapon = static_cast<std::uint8_t>(WeaponType::Smg);
        shooter->ammo = weapon_stats(WeaponType::Smg).magazine;
        shooter->aim = Vec2{Fix32::one(), Fix32::zero()};
        shooter->facing = 1;
        for (int tick = 0; tick < 48; ++tick) {
            InputFrame frame;
            PlayerCommand command;
            command.entity = shooter->id;
            command.command.tick = world.tick();
            command.command.buttons = kButtonFire;
            command.command.aim_x = 32767;
            frame.commands.push_back(command);
            world.step(frame);
            if (victim->health < Fix32::from_int(100)) {
                break;
            }
        }
        return victim->health;
    };

    // A wall between the shooter's centre and the muzzle must not let the
    // shell spawn on the far side.
    CHECK(fire_smg(
              Map::default_arena(),
              Fix32::from_int(2),
              Fix32::from_ratio(4, 10)) == Fix32::from_int(100));
    // Point-blank target that sits after the centre but before the muzzle.
    CHECK(fire_smg(
              Map::default_arena(),
              Fix32::from_ratio(8, 10),
              no_wall) < Fix32::from_int(100));
    // A wall behind the target must not swallow the closer player hit.
    CHECK(fire_smg(
              Map::default_arena(),
              Fix32::from_int(2),
              Fix32::from_int(3)) < Fix32::from_int(100));
}

void test_warmup_and_reload() {
    using namespace nsync::core;
    const Map arena = Map::default_arena();

    World warmup;
    warmup.set_map(arena);
    warmup.reset(1, 2);
    Player* shooter = warmup.players().get(EntityId::make(0, 1));
    Player* victim = warmup.players().get(EntityId::make(1, 1));
    CHECK(shooter != nullptr);
    CHECK(victim != nullptr);
    if (shooter == nullptr || victim == nullptr) {
        return;
    }
    shooter->weapon = static_cast<std::uint8_t>(WeaponType::Smg);
    shooter->ammo = weapon_stats(WeaponType::Smg).magazine;
    shooter->position = Vec2{Fix32::from_int(-5),
                             Fix32::from_ratio(103, 10)};
    shooter->velocity = {};
    victim->position = Vec2{Fix32::from_ratio(86, 10),
                            Fix32::from_ratio(103, 10)};
    victim->velocity = {};
    victim->health = Fix32::from_int(100);
    victim->shield = 0;
    victim->invulnerable_ticks = 0;

    InputFrame fire;
    PlayerCommand command;
    command.entity = shooter->id;
    command.command.buttons = kButtonFire;
    command.command.aim_x = 32767;

    for (int index = 0; index < 40; ++index) {
        command.command.tick = warmup.tick();
        fire.commands.assign(1, command);
        warmup.step(fire);
    }
    CHECK(warmup.projectiles().empty());

    command.command.tick = warmup.tick();
    fire.commands.assign(1, command);
    warmup.step(fire);
    CHECK(warmup.projectiles().size() == 1);
    warmup.projectiles().for_each([&](const Projectile& projectile) {
        CHECK(projectile.platform_puncher == 1);
        CHECK(projectile.range == Fix32::from_int(16));
        CHECK(projectile.falloff_start == Fix32::from_ratio(7, 10));
        CHECK(projectile.falloff_end == Fix32::from_ratio(7, 10));
        CHECK(projectile.life_ticks ==
              weapon_stats(WeaponType::Smg).projectile_life - 1);
    });

    command.command.tick = warmup.tick();
    fire.commands.assign(1, command);
    warmup.step(fire);
    CHECK(warmup.projectiles().empty());
    CHECK(victim->health < Fix32::from_int(100));
    CHECK(victim->health > Fix32::from_int(86));

    World reload;
    reload.set_map(arena);
    reload.reset(1, 1);
    Player* reloader = reload.players().get(EntityId::make(0, 1));
    CHECK(reloader != nullptr);
    if (reloader == nullptr) {
        return;
    }
    const WeaponStats& shotgun = weapon_stats(WeaponType::Shotgun);
    reloader->weapon = static_cast<std::uint8_t>(WeaponType::Shotgun);
    reloader->ammo = 0;
    reloader->reload_timer = 0;
    InputFrame empty;
    reload.step(empty);
    CHECK(reloader->reload_timer == shotgun.reload_ticks);
    for (int index = 0; index < shotgun.reload_ticks; ++index) {
        reload.step(empty);
    }
    CHECK(reloader->ammo == shotgun.reload_size);
    CHECK(reloader->reload_timer == shotgun.reload_ticks);
    for (int index = 0; index < shotgun.reload_ticks; ++index) {
        reload.step(empty);
    }
    CHECK(reloader->ammo == shotgun.reload_size * 2);
    for (int index = 0; index < shotgun.reload_ticks; ++index) {
        reload.step(empty);
    }
    CHECK(reloader->ammo == shotgun.magazine);
    CHECK(reloader->reload_timer == 0);

    reloader->ammo = static_cast<std::uint16_t>(shotgun.magazine - 1);
    reloader->reload_timer = shotgun.reload_ticks;
    reloader->fire_cooldown = 0;
    reloader->fire_latch = 0;
    InputFrame cancel_fire;
    PlayerCommand cancel_command;
    cancel_command.entity = reloader->id;
    cancel_command.command.tick = reload.tick();
    cancel_command.command.buttons = kButtonFire;
    cancel_command.command.aim_x = 32767;
    cancel_fire.commands.push_back(cancel_command);
    const std::uint16_t ammo_before = reloader->ammo;
    reload.step(cancel_fire);
    CHECK(reloader->reload_timer == 0);
    CHECK(reloader->ammo == static_cast<std::uint16_t>(ammo_before - 1));
}
void test_explosion_and_bounce() {
    using namespace nsync::core;
    Map wall_map = Map::default_arena();
    wall_map.solids.push_back(AABB{
        Vec2{Fix32::from_int(4), Fix32::zero()},
        Vec2{Fix32::from_int(5), Fix32::from_int(16)}});

    World rocket;
    rocket.set_map(wall_map);
    rocket.reset(1, 3);
    Player* shooter = rocket.players().get(EntityId::make(0, 1));
    Player* enemy = rocket.players().get(EntityId::make(1, 1));
    Player* friend_player = rocket.players().get(EntityId::make(2, 1));
    CHECK(shooter != nullptr);
    CHECK(enemy != nullptr);
    CHECK(friend_player != nullptr);
    if (shooter == nullptr || enemy == nullptr || friend_player == nullptr) {
        return;
    }
    for (Player* player : {shooter, enemy, friend_player}) {
        player->position = Vec2{Fix32::from_ratio(103, 10),
                                Fix32::from_ratio(103, 10)};
        player->velocity = {};
        player->health = Fix32::from_int(100);
        player->shield = 0;
        player->shield_regen_delay = 1000;
        player->invulnerable_ticks = 0;
    }
    shooter->position.x = Fix32::from_int(3);
    enemy->position.x = Fix32::from_ratio(28, 10);
    friend_player->position.x = Fix32::from_ratio(22, 10);
    shooter->weapon = static_cast<std::uint8_t>(WeaponType::Rocket);
    shooter->ammo = weapon_stats(WeaponType::Rocket).magazine;

    InputFrame fire;
    PlayerCommand command;
    command.entity = shooter->id;
    command.command.tick = rocket.tick();
    command.command.buttons = kButtonFire;
    command.command.aim_x = 32767;
    fire.commands.push_back(command);
    rocket.step(fire);
    CHECK(rocket.projectiles().empty());
    CHECK(near_raw(shooter->health, Fix32::from_int(87), 1));
    CHECK(near_raw(enemy->health, Fix32::from_int(47), 1));
    CHECK(near_raw(friend_player->health, Fix32::from_int(100), 1));
    CHECK(shooter->velocity.x < Fix32::zero());
    CHECK(enemy->velocity.x < Fix32::zero());
    CHECK(friend_player->velocity.x < Fix32::zero());
    CHECK(shooter->velocity.y < Fix32::zero());
    CHECK(enemy->velocity.y < Fix32::zero());

    World grenade;
    grenade.set_map(Map::default_arena());
    grenade.reset(1, 1);
    Player* thrower = grenade.players().get(EntityId::make(0, 1));
    CHECK(thrower != nullptr);
    if (thrower == nullptr) {
        return;
    }
    thrower->position = Vec2{Fix32::zero(),
                             Fix32::from_ratio(103, 10)};
    thrower->velocity = {};
    thrower->weapon = static_cast<std::uint8_t>(WeaponType::Grenade);
    thrower->ammo = weapon_stats(WeaponType::Grenade).magazine;
    InputFrame throw_input;
    PlayerCommand throw_command;
    throw_command.entity = thrower->id;
    throw_command.command.tick = grenade.tick();
    throw_command.command.buttons = kButtonFire;
    throw_command.command.aim_x = 32767;
    throw_command.command.aim_y = 32767;
    throw_input.commands.push_back(throw_command);
    grenade.step(throw_input);
    CHECK(grenade.projectiles().size() == 1);
    grenade.projectiles().for_each([&](const Projectile& projectile) {
        // Aiming into the floor blocks the shot, so the shell starts at the
        // thrower's centre (GunControllerImpl::shoot) and falls from there
        // instead of being reflected off the spawn sweep.
        CHECK(near_raw(projectile.origin.x, thrower->position.x, 2048));
        CHECK(near_raw(projectile.origin.y, thrower->position.y, 2048));
        CHECK(projectile.velocity.y > Fix32::zero());
        CHECK(projectile.terrain_bounces <=
              weapon_stats(WeaponType::Grenade).terrain_bounces);
        CHECK(projectile.explode_on_expire == 1);
    });

    World laser;
    laser.set_map(wall_map);
    laser.reset(1, 1);
    Player* laser_shooter = laser.players().get(EntityId::make(0, 1));
    CHECK(laser_shooter != nullptr);
    if (laser_shooter != nullptr) {
        laser_shooter->position = Vec2{Fix32::from_int(2),
                                       Fix32::from_ratio(103, 10)};
        laser_shooter->velocity = {};
        laser_shooter->weapon = static_cast<std::uint8_t>(WeaponType::Burst);
        laser_shooter->ammo = weapon_stats(WeaponType::Burst).magazine;
        InputFrame laser_input;
        PlayerCommand laser_shot;
        laser_shot.entity = laser_shooter->id;
        laser_shot.command.tick = laser.tick();
        laser_shot.command.buttons = kButtonFire;
        laser_shot.command.aim_x = 32767;
        laser_input.commands.push_back(laser_shot);
        laser.step(laser_input);
        CHECK(laser.projectiles().size() == 1);
        bool bounced = false;
        InputFrame empty;
        for (int tick = 0; tick < 30 && !bounced; ++tick) {
            laser.step(empty);
            laser.projectiles().for_each([&](const Projectile& projectile) {
                if (projectile.velocity.x < Fix32::zero()) {
                    bounced = true;
                    CHECK(projectile.terrain_bounces ==
                          weapon_stats(WeaponType::Burst).terrain_bounces);
                    CHECK(projectile.explosive == 0);
                }
            });
        }
        CHECK(bounced);
    }

    Map floor_map;
    floor_map.id = "laser_floor_test";
    floor_map.bounds = AABB{
        Vec2{Fix32::from_int(-5), Fix32::zero()},
        Vec2{Fix32::from_int(5), Fix32::from_int(12)}};
    floor_map.solids = {
        AABB{
            Vec2{Fix32::from_int(-5), Fix32::from_int(1)},
            Vec2{Fix32::from_int(5), Fix32::from_int(2)}},
        AABB{
            Vec2{Fix32::from_int(-5), Fix32::from_int(8)},
            Vec2{Fix32::from_int(5), Fix32::from_int(9)}},
    };
    floor_map.spawns.fill(
        Vec2{Fix32::zero(), Fix32::from_int(5)});
    std::string floor_error;
    CHECK(floor_map.valid(&floor_error));

    World floor_laser;
    floor_laser.set_map(floor_map);
    floor_laser.reset(1, 1);
    Player* floor_shooter =
        floor_laser.players().get(EntityId::make(0, 1));
    CHECK(floor_shooter != nullptr);
    if (floor_shooter != nullptr) {
        floor_shooter->weapon =
            static_cast<std::uint8_t>(WeaponType::Burst);
        floor_shooter->ammo = weapon_stats(WeaponType::Burst).magazine;
        InputFrame floor_fire;
        PlayerCommand floor_command;
        floor_command.entity = floor_shooter->id;
        floor_command.command.tick = floor_laser.tick();
        floor_command.command.buttons = kButtonFire;
        floor_command.command.aim_y = 32767;
        floor_fire.commands.push_back(floor_command);
        floor_laser.step(floor_fire);
        CHECK(floor_laser.projectiles().size() == 1);
        bool saw_floor_bounce = false;
        InputFrame empty;
        for (int tick = 0;
             tick < 80 && !floor_laser.projectiles().empty();
             ++tick) {
            floor_laser.step(empty);
            floor_laser.projectiles().for_each(
                [&](const Projectile& projectile) {
                    if (projectile.terrain_bounces == 1) {
                        saw_floor_bounce = true;
                    }
                });
        }
        CHECK(saw_floor_bounce);
        CHECK(floor_laser.projectiles().empty());
    }
}


void test_objective_mode() {
    using namespace nsync::core;
    Map map = Map::default_arena();
    map.objective = Objective{
        AABB{
            Vec2{Fix32::from_int(-2), Fix32::from_int(9)},
            Vec2{Fix32::from_int(2), Fix32::from_int(11)}}};

    World world;
    world.set_map(map);
    world.set_game_mode(GameMode::Control);
    world.reset(1, 2);
    world.set_match_rules(10000, 30);
    CHECK(world.game_mode() == GameMode::Control);

    Player* capture = world.players().get(EntityId::make(0, 1));
    Player* defender = world.players().get(EntityId::make(1, 1));
    CHECK(capture != nullptr);
    CHECK(defender != nullptr);
    if (capture == nullptr || defender == nullptr) {
        return;
    }
    capture->position = Vec2{
        Fix32::zero(),
        Fix32::from_ratio(103, 10)};
    capture->velocity = {};
    defender->position = map.spawns[5];
    defender->velocity = {};

    InputFrame empty;
    for (int index = 0; index < static_cast<int>(kObjectiveCaptureTicks) - 1;
         ++index) {
        world.step(empty);
    }
    CHECK(world.objective_owner() == kObjectiveNeutral);
    CHECK(world.objective_progress() == kObjectiveCaptureTicks - 1);
    world.step(empty);
    CHECK(world.objective_owner() == 1);
    CHECK(world.objective_progress() == kObjectiveCaptureTicks);
    CHECK(world.scores()[0] == 0);

    for (int index = 0; index < static_cast<int>(kObjectiveScoreTicks) - 1;
         ++index) {
        world.step(empty);
    }
    CHECK(world.scores()[0] == 0);
    world.step(empty);
    CHECK(world.scores()[0] == 1);

    WorldSnapshot snapshot;
    world.save(snapshot);
    CHECK(snapshot.version == kSnapshotVersion);
    CHECK(snapshot.game_mode == static_cast<std::uint8_t>(GameMode::Control));
    CHECK(snapshot.objective_owner == 1);
    World restored;
    restored.set_map(map);
    std::string error;
    CHECK(restored.restore(snapshot, &error));
    CHECK(error.empty());
    CHECK(restored.hash() == world.hash());
    CHECK(restored.objective_owner() == 1);

    defender->position = Vec2{
        Fix32::from_ratio(1, 2),
        Fix32::from_ratio(103, 10)};
    defender->velocity = {};
    const int score_before = world.scores()[0];
    for (int index = 0; index < 20; ++index) {
        world.step(empty);
    }
    CHECK(world.scores()[0] == score_before);
}

void test_original_control_map_objectives() {
    using namespace nsync::core;
    const auto load = [](const char* name) {
        Map map;
        std::string error;
        const std::filesystem::path path =
            std::filesystem::path(NSYNC_SOURCE_DIR) / "assets" / "maps" / name;
        CHECK(load_map_file(path, map, &error));
        CHECK(map.control_points.has_value());
        return map;
    };

    const Map world5 = load("world5.json");
    const ControlPointGenerator& gen5 = *world5.control_points;
    CHECK(gen5.update_interval == 6);
    CHECK(gen5.start_tick == 600);
    CHECK(gen5.min_interval == 0);
    CHECK(gen5.max_interval == 0);
    CHECK(gen5.instance_limit == 3);
    CHECK(gen5.capture_group_size == 4);
    CHECK(gen5.points.size() == 1);
    CHECK(gen5.points[0].capture_ticks == 20);
    CHECK(gen5.points[0].score_ticks == 120);
    CHECK(gen5.points[0].capture_score == 0);
    CHECK(gen5.points[0].hold_score == 1);
    CHECK(gen5.entries.size() == 3);

    const std::vector<std::uint8_t> binary = encode_map_bin(world5);
    CHECK(!binary.empty());
    Map decoded;
    std::string error;
    CHECK(decode_map_bin(binary.data(), binary.size(), decoded, &error));
    CHECK(decoded.control_points.has_value());
    CHECK(decoded.control_points->entries.size() == 3);
    CHECK(decoded.control_points->points[0].capture_ticks == 20);
    CHECK(map_fingerprint(world5) == map_fingerprint(decoded));

    const Map world6 = load("world6.json");
    const ControlPointGenerator& gen6 = *world6.control_points;
    CHECK(gen6.update_interval == 6);
    CHECK(gen6.start_tick == 300);
    CHECK(gen6.min_interval == 300);
    CHECK(gen6.max_interval == 300);
    CHECK(gen6.instance_limit == 2);
    CHECK(gen6.capture_group_size == 2);
    CHECK(gen6.points.size() == 1);
    CHECK(gen6.points[0].capture_ticks == 40);
    CHECK(gen6.points[0].score_ticks == 0);
    CHECK(gen6.points[0].capture_score == 1);
    CHECK(gen6.points[0].hold_score == 0);
    CHECK(gen6.entries.size() == 9);

    const Map world8 = load("world8.json");
    const ControlPointGenerator& gen8 = *world8.control_points;
    CHECK(gen8.instance_limit == 3);
    CHECK(gen8.capture_group_size == 4);
    CHECK(gen8.points[0].capture_ticks == 20);
    CHECK(gen8.points[0].score_ticks == 120);
}

void test_objective_recapture_semantics() {
    using namespace nsync::core;
    Map map = Map::default_arena();
    Objective objective;
    objective.area = AABB{
        Vec2{Fix32::from_int(-2), Fix32::from_int(9)},
        Vec2{Fix32::from_int(2), Fix32::from_int(11)}};
    objective.capture_ticks = 4;
    objective.score_ticks = 0;
    objective.capture_score = 1;
    objective.hold_score = 0;
    map.objective = objective;

    World world;
    world.set_map(map);
    world.set_game_mode(GameMode::Control);
    world.reset(1, 2);
    world.set_match_rules(10000, 30);

    Player* blue = world.players().get(EntityId::make(0, 1));
    Player* red = world.players().get(EntityId::make(1, 1));
    CHECK(blue != nullptr);
    CHECK(red != nullptr);
    if (blue == nullptr || red == nullptr) {
        return;
    }
    const Vec2 center{
        Fix32::zero(), Fix32::from_ratio(103, 10)};
    blue->position = center;
    blue->velocity = {};
    red->position = map.spawns[5];
    red->velocity = {};

    InputFrame empty;
    for (int index = 0; index < 4; ++index) {
        world.step(empty);
    }
    CHECK(world.objective_owner() == 1);
    CHECK(world.objective_progress() == 4);
    CHECK(world.scores()[0] == 1);

    // Standing on an already captured point must not capture it again.
    world.step(empty);
    CHECK(world.objective_owner() == 1);
    CHECK(world.objective_progress() == 4);
    CHECK(world.scores()[0] == 1);

    // An enemy immediately clears ownership while unwinding the same counter;
    // the bar shrinks from full instead of restarting at zero.  Like the
    // original, the sign of the counter (not the pushing team) decides which
    // side's bar is shown, so the shrinking bar is still team 1's.
    blue->position = map.spawns[4];
    blue->velocity = {};
    red->position = center;
    red->velocity = {};
    world.step(empty);
    CHECK(world.objective_owner() == kObjectiveNeutral);
    CHECK(world.objective_capturing_team() == 1);
    CHECK(world.objective_progress() == 3);

    // Empty points keep their progress (the original never decays).
    red->position = map.spawns[5];
    red->velocity = {};
    world.step(empty);
    CHECK(world.objective_owner() == kObjectiveNeutral);
    CHECK(world.objective_progress() == 3);

    for (int index = 0; index < 10; ++index) {
        red->position = center;
        red->velocity = {};
        world.step(empty);
    }
    CHECK(world.objective_owner() == 2);
    CHECK(world.objective_capturing_team() == 2);
    CHECK(world.objective_progress() == 4);
    CHECK(world.scores()[1] == 1);
}

void test_control_point_generator() {
    using namespace nsync::core;
    Map map = Map::default_arena();
    ControlPointGenerator generator;
    generator.update_interval = 1;
    generator.start_tick = 0;
    generator.min_interval = 0;
    generator.max_interval = 0;
    generator.instance_limit = 2;
    generator.capture_group_size = 2;
    ControlPointDef def;
    def.dimensions = Vec2{Fix32::from_int(2), Fix32::from_int(2)};
    def.capture_ticks = 2;
    def.score_ticks = 0;
    def.capture_score = 1;
    def.hold_score = 0;
    generator.points.push_back(def);
    generator.entries.push_back(Vec2{
        Fix32::from_int(-6), Fix32::from_ratio(103, 10)});
    generator.entries.push_back(Vec2{
        Fix32::from_int(6), Fix32::from_ratio(103, 10)});
    map.control_points = generator;

    World world;
    world.set_map(map);
    world.set_game_mode(GameMode::Control);
    world.reset(1, 4);
    world.set_match_rules(10000, 30);

    Player* blue0 = world.players().get(EntityId::make(0, 1));
    Player* red0 = world.players().get(EntityId::make(1, 1));
    Player* blue1 = world.players().get(EntityId::make(2, 1));
    Player* red1 = world.players().get(EntityId::make(3, 1));
    CHECK(blue0 != nullptr);
    CHECK(red0 != nullptr);
    CHECK(blue1 != nullptr);
    CHECK(red1 != nullptr);
    if (blue0 == nullptr || red0 == nullptr ||
        blue1 == nullptr || red1 == nullptr) {
        return;
    }
    const Vec2 point_a{
        Fix32::from_int(-6), Fix32::from_ratio(103, 10)};
    const Vec2 point_b{
        Fix32::from_int(6), Fix32::from_ratio(103, 10)};
    blue0->position = point_a;
    blue0->velocity = {};
    blue1->position = Vec2{Fix32::from_int(2), Fix32::from_int(4)};
    blue1->velocity = {};
    red0->position = Vec2{Fix32::zero(), Fix32::from_int(2)};
    red0->velocity = {};
    red1->position = Vec2{Fix32::zero(), Fix32::from_int(4)};
    red1->velocity = {};

    InputFrame empty;
    world.step(empty);
    CHECK(world.control_points().size() == 2);

    // One captured point alone does not score or recycle until the group is
    // complete (world6 uses capture_group_size = 2).
    for (int index = 0; index < 2; ++index) {
        world.step(empty);
    }
    CHECK(world.scores()[0] == 0);
    CHECK(world.control_points().size() == 2);
    int owned = 0;
    int neutral = 0;
    for (const ControlPointState& point : world.control_points()) {
        const std::uint8_t owner = world.point_owner(point);
        if (owner == 1) {
            ++owned;
        } else if (owner == 0) {
            ++neutral;
        }
    }
    CHECK(owned == 1);
    CHECK(neutral == 1);

    blue1->position = point_b;
    blue1->velocity = {};
    for (int index = 0; index < 2; ++index) {
        world.step(empty);
    }
    CHECK(world.scores()[0] == 2);
    CHECK(world.control_points().size() == 2);
    for (const ControlPointState& point : world.control_points()) {
        CHECK(point.ticks == 0);
    }

    Map delayed_map = map;
    delayed_map.control_points->start_tick = 5;
    delayed_map.control_points->update_interval = 2;
    World delayed;
    delayed.set_map(delayed_map);
    delayed.set_game_mode(GameMode::Control);
    delayed.reset(1, 2);
    for (int index = 0; index < 6; ++index) {
        delayed.step(empty);
    }
    CHECK(delayed.control_points().empty());
    delayed.step(empty);
    CHECK(delayed.control_points().size() == 2);

    // Hold scoring is counted in engine ticks even though the generator only
    // updates every `update_interval` ticks.
    Map hold_map = map;
    hold_map.control_points->update_interval = 2;
    hold_map.control_points->instance_limit = 1;
    hold_map.control_points->capture_group_size = 0;
    hold_map.control_points->entries.resize(1);
    hold_map.control_points->points[0].capture_ticks = 1;
    hold_map.control_points->points[0].score_ticks = 4;
    hold_map.control_points->points[0].capture_score = 0;
    hold_map.control_points->points[0].hold_score = 1;
    World hold_world;
    hold_world.set_map(hold_map);
    hold_world.set_game_mode(GameMode::Control);
    hold_world.reset(1, 2);
    hold_world.set_match_rules(10000, 30);
    Player* holder = hold_world.players().get(EntityId::make(0, 1));
    Player* enemy = hold_world.players().get(EntityId::make(1, 1));
    CHECK(holder != nullptr);
    CHECK(enemy != nullptr);
    if (holder == nullptr || enemy == nullptr) {
        return;
    }
    holder->position = hold_map.control_points->entries[0];
    holder->velocity = {};
    enemy->position = Vec2{Fix32::zero(), Fix32::from_int(2)};
    enemy->velocity = {};
    for (int index = 0; index < 7; ++index) {
        hold_world.step(empty);
    }
    CHECK(hold_world.scores()[0] == 1);
}

void test_supplies() {
    using namespace nsync::core;
    Map map = Map::default_arena();
    const Vec2 spawn = map.spawns[0];
    const Vec2 far{
        spawn.x + Fix32::from_int(8),
        spawn.y};
    map.supply_spawners = {
        SupplySpawner{{"health_box"}, {spawn}, 1, 60, 60},
        SupplySpawner{{"double_damage"}, {far}, 1, 60, 60},
    };

    World world;
    world.set_map(map);
    world.reset(7, 1);
    Player* player = world.players().get(EntityId::make(0, 1));
    CHECK(player != nullptr);
    if (player == nullptr) {
        return;
    }
    player->health = Fix32::from_int(50);
    world.step(InputFrame{});
    CHECK(world.supplies().size() == 1);
    CHECK(player->health == Fix32::from_int(70));
    CHECK(player->supply_heal_ticks == 900);
    CHECK(player->supply_heal_amount == 75);
    CHECK(player->supply_heal_period == 30);

    WorldSnapshot snapshot;
    world.save(snapshot);
    CHECK(snapshot.supplies.size() == 1);
    CHECK(snapshot.supply_spawners.size() == 2);
    const std::vector<std::uint8_t> encoded = encode_snapshot(snapshot);
    WorldSnapshot decoded;
    std::string error;
    CHECK(decode_snapshot(encoded, decoded, &error));
    CHECK(decoded.supplies.size() == 1);
    CHECK(snapshot_hash(decoded) == snapshot_hash(snapshot));
    CHECK(world.restore(decoded, &error));
    CHECK(world.hash() == snapshot_hash(snapshot));

    player = world.players().get(EntityId::make(0, 1));
    CHECK(player != nullptr);
    if (player == nullptr) {
        return;
    }
    player->position = far;
    player->velocity = {};
    world.step(InputFrame{});
    CHECK(world.supplies().empty());
    CHECK(player->damage_bonus == Fix32::one());
    CHECK(player->damage_bonus_ticks == 600);

    // The pump heals every interval until the buff expires or hp is full.
    const int before = player->health.raw() / Fix32::kOne;
    for (int tick = 0; tick < 30; ++tick) {
        world.step(InputFrame{});
    }
    CHECK(player->health.raw() / Fix32::kOne > before);

    // Real imported map: the first generator must produce a health box.
    Map real;
    const std::filesystem::path path =
        std::filesystem::path(NSYNC_SOURCE_DIR) / "assets" / "maps" /
        "crystal_01.json";
    CHECK(load_map_file(path, real, &error));
    CHECK(real.supply_spawners.size() == 5);
    if (real.supply_spawners.empty()) {
        return;
    }
    World imported;
    imported.set_map(real);
    imported.set_game_mode(GameMode::Crystal);
    imported.reset(3, 1);
    Player* imported_player =
        imported.players().get(EntityId::make(0, 1));
    CHECK(imported_player != nullptr);
    if (imported_player == nullptr) {
        return;
    }
    for (std::uint32_t tick = 0;
         tick < real.supply_spawners[0].start_tick + 5 &&
         imported.supplies().empty();
         ++tick) {
        imported.step(InputFrame{});
    }
    CHECK(!imported.supplies().empty());
    if (imported.supplies().empty()) {
        return;
    }
    imported_player->position = imported.supplies()[0].position;
    imported_player->velocity = {};
    imported_player->health = Fix32::from_int(50);
    imported.step(InputFrame{});
    CHECK(imported_player->health == Fix32::from_int(70));
    CHECK(imported.supplies().size() == 0);

    // double_damage doubles outgoing weapon damage.
    const auto fire_once = [](bool boosted) {
        World damage_world;
        damage_world.set_map(Map::default_arena());
        damage_world.reset(11, 2);
        damage_world.step(InputFrame{});
        Player* attacker = nullptr;
        Player* target = nullptr;
        damage_world.players().for_each([&](Player& player) {
            if (player.slot == 0) {
                attacker = &player;
            } else if (player.slot == 1) {
                target = &player;
            }
        });
        if (attacker == nullptr || target == nullptr) {
            return -1;
        }
        const Vec2 point{Fix32::zero(), Fix32::from_ratio(101, 10)};
        attacker->position = point;
        attacker->aim = Vec2{Fix32::one(), Fix32::zero()};
        attacker->facing = 1;
        target->position = point;
        target->shield = 0;
        target->health = Fix32::from_int(100);
        if (boosted) {
            attacker->damage_bonus = Fix32::one();
            attacker->damage_bonus_ticks = 600;
        }
        const Fix32 before = target->health;
        InputFrame fire;
        PlayerCommand command;
        command.entity = attacker->id;
        command.command.tick = damage_world.tick();
        command.command.buttons = kButtonFire;
        command.command.aim_x = 32767;
        fire.commands.push_back(command);
        damage_world.step(fire);
        return (before - target->health).raw() / Fix32::kOne;
    };
    const int normal_damage = fire_once(false);
    const int boosted_damage = fire_once(true);
    CHECK(normal_damage > 0);
    CHECK(boosted_damage == normal_damage * 2);
}

void test_snapshot_restore() {
    using namespace nsync::core;
    const Replay replay = make_golden_replay(123, 60);
    World original;
    original.reset(123, 2);

    auto step_frame = [&](World& world, std::size_t index) {
        InputFrame input;
        input.commands = replay.frames[index].commands;
        world.step(input);
    };

    for (std::size_t index = 0; index < 25; ++index) {
        step_frame(original, index);
    }

    WorldSnapshot snapshot;
    original.save(snapshot);
    CHECK(snapshot.tick == 25);
    CHECK(snapshot.players.size() == 2);
    CHECK(!snapshot.projectiles.empty());
    CHECK(snapshot.rng.increment % 2 == 1);

    const std::vector<std::uint8_t> encoded = encode_snapshot(snapshot);
    WorldSnapshot decoded;
    std::string error;
    CHECK(decode_snapshot(encoded, decoded, &error));
    CHECK(error.empty());
    CHECK(decoded.tick == snapshot.tick);
    CHECK(decoded.players.size() == snapshot.players.size());
    CHECK(decoded.projectiles.size() == snapshot.projectiles.size());
    CHECK(snapshot_hash(decoded) == snapshot_hash(snapshot));

    std::uint64_t encoded_hash = 0;
    CHECK(snapshot_hash_encoded(encoded, encoded_hash));
    CHECK(encoded_hash == snapshot_hash(snapshot));
    CHECK(snapshot_hash_payload(encoded.data() + 4, encoded.size() - 4) ==
        encoded_hash);
    std::vector<std::uint8_t> bad_hash_length(encoded);
    bad_hash_length[0] ^= 0xFFU;
    std::uint64_t bad_hash = 0;
    CHECK(!snapshot_hash_encoded(bad_hash_length, bad_hash));

    WorldSnapshot stats_snapshot = snapshot;
    const EntityId stats_id = stats_snapshot.players.front().id;
    stats_snapshot.players.front().kills = 7;
    stats_snapshot.players.front().deaths = 3;
    stats_snapshot.players.front().damage_dealt = 4567;
    stats_snapshot.players.front().blocked_damage = 890;
    stats_snapshot.players.front().shield = 30;
    stats_snapshot.players.front().gadget_cooldowns[0] = 123;
    stats_snapshot.players.front().gadget_active_ticks[1] = 7;
    stats_snapshot.players.front().hero =
        static_cast<std::uint8_t>(HeroType::Assault);
    stats_snapshot.players.front().gadget_fuel[1] =
        Fix32::from_ratio(123, 2);
    stats_snapshot.players.front().invulnerable_ticks = 45;
    stats_snapshot.players.front().shield_regen_delay = 88;
    const std::vector<std::uint8_t> stats_encoded =
        encode_snapshot(stats_snapshot);
    WorldSnapshot stats_decoded;
    error.clear();
    CHECK(decode_snapshot(stats_encoded, stats_decoded, &error));
    CHECK(error.empty());
    const Player* stats_player = nullptr;
    for (const Player& player : stats_decoded.players) {
        if (player.id == stats_id) {
            stats_player = &player;
            break;
        }
    }
    CHECK(stats_player != nullptr);
    if (stats_player != nullptr) {
        CHECK(stats_player->kills == 7);
        CHECK(stats_player->deaths == 3);
        CHECK(stats_player->damage_dealt == 4567);
        CHECK(stats_player->blocked_damage == 890);
        CHECK(stats_player->shield == 30);
        CHECK(stats_player->gadget_cooldowns[0] == 123);
        CHECK(stats_player->gadget_active_ticks[1] == 7);
        CHECK(stats_player->gadget_fuel[1] == Fix32::from_ratio(123, 2));
        CHECK(stats_player->invulnerable_ticks == 45);
        CHECK(stats_player->shield_regen_delay == 88);
    }
    CHECK(snapshot_hash(stats_decoded) == snapshot_hash(stats_snapshot));

    World restored;
    CHECK(restored.restore(decoded, &error));
    CHECK(error.empty());
    CHECK(restored.tick() == original.tick());
    CHECK(restored.hash() == original.hash());

    for (std::size_t index = 30; index < replay.frames.size(); ++index) {
        step_frame(original, index);
        step_frame(restored, index);
        CHECK(original.hash() == restored.hash());
        CHECK(original.tick() == restored.tick());
    }

    std::vector<std::uint8_t> truncated(encoded.begin(), encoded.end() - 1);
    CHECK(!decode_snapshot(truncated, decoded, &error));

    WorldSnapshot invalid_version = snapshot;
    invalid_version.version = 999;
    const std::vector<std::uint8_t> invalid_bytes = encode_snapshot(invalid_version);
    CHECK(!decode_snapshot(invalid_bytes, decoded, &error));

    WorldSnapshot invalid_rng = snapshot;
    invalid_rng.rng.increment = 8;
    const std::vector<std::uint8_t> invalid_rng_bytes = encode_snapshot(invalid_rng);
    CHECK(!decode_snapshot(invalid_rng_bytes, decoded, &error));

    WorldSnapshot invalid_entities = snapshot;
    invalid_entities.entities.free_list.push_back(999999);
    const std::vector<std::uint8_t> invalid_entity_bytes = encode_snapshot(invalid_entities);
    CHECK(!decode_snapshot(invalid_entity_bytes, decoded, &error));

    WorldSnapshot invalid_stats = snapshot;
    invalid_stats.players.front().kills = kSnapshotMaxKills + 1;
    const std::vector<std::uint8_t> invalid_stats_bytes =
        encode_snapshot(invalid_stats);
    CHECK(!decode_snapshot(invalid_stats_bytes, decoded, &error));

    invalid_stats = snapshot;
    invalid_stats.players.front().damage_dealt = kSnapshotMaxDamage + 1;
    const std::vector<std::uint8_t> invalid_damage_bytes =
        encode_snapshot(invalid_stats);
    CHECK(!decode_snapshot(invalid_damage_bytes, decoded, &error));

    invalid_stats = snapshot;
    invalid_stats.players.front().shield = kSnapshotMaxShield + 1;
    const std::vector<std::uint8_t> invalid_shield_bytes =
        encode_snapshot(invalid_stats);
    CHECK(!decode_snapshot(invalid_shield_bytes, decoded, &error));

    invalid_stats = snapshot;
    invalid_stats.players.front().blocked_damage = kSnapshotMaxDamage + 1;
    const std::vector<std::uint8_t> invalid_blocked_bytes =
        encode_snapshot(invalid_stats);
    CHECK(!decode_snapshot(invalid_blocked_bytes, decoded, &error));
}

void test_replay_roundtrip(const std::filesystem::path& root) {
    using namespace nsync::core;
    const Replay replay = make_golden_replay(7, 120);
    const std::vector<std::uint8_t> encoded = encode_replay(replay);
    Replay decoded;
    std::string error;
    CHECK(decode_replay(encoded, decoded, &error));
    CHECK(error.empty());
    CHECK(decoded.seed == replay.seed);
    CHECK(decoded.player_count == replay.player_count);
    CHECK(decoded.frames.size() == replay.frames.size());

    ReplayRun first;
    ReplayRun second;
    CHECK(run_replay(replay, 1, first, &error));
    CHECK(run_replay(decoded, 1, second, &error));
    CHECK(first.hash_ticks == second.hash_ticks);
    CHECK(first.hashes == second.hashes);
    CHECK(first.hashes.size() == replay.frames.size() + 1);

    const std::filesystem::path path = root / "golden.replay";
    CHECK(save_replay(path, replay, &error));
    Replay loaded;
    CHECK(load_replay(path, loaded, &error));
    ReplayRun third;
    CHECK(run_replay(loaded, 1, third, &error));
    CHECK(first.hashes == third.hashes);

    const std::vector<std::uint8_t> truncated(encoded.begin(), encoded.end() - 1);
    CHECK(!decode_replay(truncated, decoded, &error));

    Replay invalid_version = replay;
    invalid_version.version = 999;
    const std::vector<std::uint8_t> invalid_version_bytes = encode_replay(invalid_version);
    CHECK(!decode_replay(invalid_version_bytes, decoded, &error));
}

void test_crystal_mode() {
    using namespace nsync::core;

    Map arena;
    std::string error;
    CHECK(load_map_file(
        std::filesystem::path(NSYNC_SOURCE_DIR) / "assets" / "maps" /
            "arena_01.json",
        arena,
        &error));
    CHECK(!arena.drop_spawns.empty());
    CHECK(arena.drop_depots.size() >= 2);

    World world;
    world.set_map(arena);
    world.set_game_mode(GameMode::Crystal);
    world.reset(1, 2);
    CHECK(world.game_mode() == GameMode::Crystal);
    CHECK(world.drops().size() == arena.drop_spawns.size());
    CHECK(world.scores()[0] == 0);
    CHECK(world.scores()[1] == 0);

    const EntityId player_entity = EntityId::make(0, 1);
    InputFrame input;
    PlayerCommand command;
    command.entity = player_entity;
    input.commands.push_back(command);

    const auto carried = [&]() {
        const Player* player = world.players().get(player_entity);
        return player != nullptr ? player->carried_drops : 0;
    };

    world.step(InputFrame{});
    for (int tick = 0; tick < 240 && carried() < 2; ++tick) {
        input.commands[0].command.tick = world.tick();
        input.commands[0].command.buttons = kButtonRight;
        world.step(input);
    }
    CHECK(carried() == 2);

    for (int tick = 0; tick < 540 && world.scores()[0] == 0; ++tick) {
        input.commands[0].command.tick = world.tick();
        input.commands[0].command.buttons = kButtonLeft;
        world.step(input);
    }
    CHECK(world.scores()[0] == 2);
    const Player* player = world.players().get(player_entity);
    CHECK(player != nullptr);
    if (player != nullptr) {
        CHECK(player->crystals_delivered == 2);
        CHECK(player->carried_drops == 0);
    }

    WorldSnapshot snapshot;
    world.save(snapshot);
    CHECK(snapshot.drops.size() == arena.drop_spawns.size());
    World restored;
    restored.set_map(arena);
    CHECK(restored.restore(snapshot, &error));
    CHECK(restored.game_mode() == GameMode::Crystal);
    CHECK(restored.hash() == world.hash());
}

void test_original_crystal_map() {
    using namespace nsync::core;

    Map map;
    std::string error;
    CHECK(load_map_file(
        std::filesystem::path(NSYNC_SOURCE_DIR) / "assets" / "maps" /
            "crystal_01.json",
        map,
        &error));
    CHECK(map.id == "crystal_01");
    CHECK(map.solids.size() >= 400);
    CHECK(map.one_way_platforms.size() == 27);
    CHECK(map.drop_spawns.size() == 13);
    CHECK(map.drop_depots.size() == 2);
    for (const DropDepot& depot : map.drop_depots) {
        CHECK(depot.team == kDropDepotNeutral);
        CHECK(depot.deliver_ticks == 240);
    }

    World world;
    world.set_map(map);
    world.set_game_mode(GameMode::Crystal);
    world.reset(1, 2);
    CHECK(world.drops().size() == 13);

    const EntityId player_entity = EntityId::make(0, 1);
    Vec2 crystal_position{};
    world.drops().for_each([&](const Drop& drop) {
        if (drop.state == static_cast<std::uint8_t>(DropState::Home)) {
            crystal_position = drop.position;
        }
    });
    CHECK(crystal_position != Vec2{});

    for (int tick = 0; tick < 30; ++tick) {
        Player* player = world.players().get(player_entity);
        if (player == nullptr) {
            break;
        }
        player->position = crystal_position;
        player->velocity = Vec2{};
        world.step(InputFrame{});
        if (player->carried_drops > 0) {
            break;
        }
    }
    const Player* carrier = world.players().get(player_entity);
    CHECK(carrier != nullptr);
    CHECK(carrier != nullptr && carrier->carried_drops > 0);

    const Vec2 depot_center = map.drop_depots[0].bounds.center();
    for (int tick = 0; tick < 320; ++tick) {
        Player* player = world.players().get(player_entity);
        if (player == nullptr || world.scores()[0] > 0) {
            break;
        }
        player->position = depot_center;
        player->velocity = Vec2{};
        player->grounded = 1;
        world.step(InputFrame{});
    }
    CHECK(world.scores()[0] > 0);
    const Player* delivered = world.players().get(player_entity);
    CHECK(delivered != nullptr);
    CHECK(delivered != nullptr && delivered->carried_drops == 0);
    CHECK(delivered != nullptr && delivered->crystals_delivered > 0);
}

void test_grenade_bounce_policy() {
    using namespace nsync::core;
    Map map;
    map.id = "grenade_bounce_test";
    map.bounds = AABB{
        Vec2{Fix32::from_int(-10), Fix32::zero()},
        Vec2{Fix32::from_int(10), Fix32::from_int(20)}};
    map.solids = {
        AABB{
            Vec2{Fix32::from_int(3), Fix32::zero()},
            Vec2{Fix32::from_int(4), Fix32::from_int(20)}},
        AABB{
            Vec2{Fix32::from_int(-10), Fix32::from_int(6)},
            Vec2{Fix32::from_int(10), Fix32::from_int(8)}},
    };
    map.spawns.fill(Vec2{Fix32::from_int(0), Fix32::from_int(5)});
    std::string error;
    CHECK(map.valid(&error));

    World world;
    world.set_map(map);
    world.reset(1, 1);
    Player* shooter = world.players().get(EntityId::make(0, 1));
    CHECK(shooter != nullptr);
    if (shooter == nullptr) {
        return;
    }
    shooter->position = Vec2{Fix32::from_int(0), Fix32::from_int(5)};
    shooter->velocity = {};
    shooter->weapon = static_cast<std::uint8_t>(WeaponType::Grenade);
    shooter->ammo = weapon_stats(WeaponType::Grenade).magazine;

    InputFrame fire;
    PlayerCommand command;
    command.entity = shooter->id;
    command.command.tick = world.tick();
    command.command.buttons = kButtonFire;
    command.command.aim_x = 32767;
    command.command.aim_y = 0;
    fire.commands.push_back(command);
    world.step(fire);
    CHECK(world.projectiles().size() == 1);

    bool wall_bounce = false;
    bool floor_bounce = false;
    bool removed = false;
    for (int tick = 0; tick < 120; ++tick) {
        world.step(InputFrame{});
        bool alive = false;
        bool moving_left = false;
        std::uint8_t bounces = 0;
        world.projectiles().for_each([&](const Projectile& projectile) {
            alive = true;
            moving_left = projectile.velocity.x < Fix32::zero();
            bounces = projectile.terrain_bounces;
        });
        if (!alive) {
            removed = true;
            break;
        }
        if (!wall_bounce && moving_left) {
            wall_bounce = true;
            CHECK(bounces == 2);
        }
        if (wall_bounce && bounces == 1) {
            floor_bounce = true;
        }
    }
    CHECK(wall_bounce);
    CHECK(floor_bounce);
    CHECK(removed);
}

void test_grenade_enemy_detonation() {
    using namespace nsync::core;
    Map map;
    map.id = "grenade_enemy_test";
    map.bounds = AABB{
        Vec2{Fix32::from_int(-20), Fix32::zero()},
        Vec2{Fix32::from_int(20), Fix32::from_int(24)}};
    map.solids = {AABB{
        Vec2{Fix32::from_int(-20), Fix32::from_int(13)},
        Vec2{Fix32::from_int(20), Fix32::from_int(14)}}};
    map.spawns.fill(Vec2{Fix32::zero(), Fix32::from_ratio(123, 10)});
    std::string error;
    CHECK(map.valid(&error));

    World world;
    world.set_map(map);
    world.reset(1, 2);
    Player* shooter = world.players().get(EntityId::make(0, 1));
    Player* enemy = world.players().get(EntityId::make(1, 1));
    CHECK(shooter != nullptr);
    CHECK(enemy != nullptr);
    if (shooter == nullptr || enemy == nullptr) {
        return;
    }
    shooter->position = Vec2{Fix32::zero(), Fix32::from_ratio(123, 10)};
    shooter->velocity = {};
    shooter->health = Fix32::from_int(100);
    shooter->shield = 0;
    shooter->shield_regen_delay = 1000;
    shooter->invulnerable_ticks = 0;
    shooter->weapon = static_cast<std::uint8_t>(WeaponType::Grenade);
    shooter->ammo = weapon_stats(WeaponType::Grenade).magazine;

    enemy->position = Vec2{
        Fix32::from_ratio(35, 10), Fix32::from_ratio(123, 10)};
    enemy->velocity = {};
    enemy->health = Fix32::from_int(100);
    enemy->shield = 0;
    enemy->shield_regen_delay = 1000;
    enemy->invulnerable_ticks = 0;

    InputFrame fire;
    PlayerCommand command;
    command.entity = shooter->id;
    command.command.tick = world.tick();
    command.command.buttons = kButtonFire;
    command.command.aim_x = 32767;
    command.command.aim_y = 0;
    fire.commands.push_back(command);
    world.step(fire);
    CHECK(world.projectiles().size() == 1);

    bool removed = false;
    for (int tick = 0; tick < 90 && !removed; ++tick) {
        world.step(InputFrame{});
        removed = world.projectiles().empty();
    }
    CHECK(removed);
    CHECK(enemy->health < Fix32::from_int(100));
}

void test_grenade_wall_bounce_no_detonate() {
    using namespace nsync::core;
    Map map;
    map.id = "grenade_wall_test";
    map.bounds = AABB{
        Vec2{Fix32::from_int(-20), Fix32::zero()},
        Vec2{Fix32::from_int(20), Fix32::from_int(24)}};
    map.solids = {AABB{
        Vec2{Fix32::from_int(4), Fix32::zero()},
        Vec2{Fix32::from_int(5), Fix32::from_int(24)}}};
    map.spawns.fill(Vec2{Fix32::zero(), Fix32::from_int(12)});
    std::string error;
    CHECK(map.valid(&error));

    World world;
    world.set_map(map);
    world.reset(1, 2);
    Player* shooter = world.players().get(EntityId::make(0, 1));
    Player* enemy = world.players().get(EntityId::make(1, 1));
    CHECK(shooter != nullptr);
    CHECK(enemy != nullptr);
    if (shooter == nullptr || enemy == nullptr) {
        return;
    }
    shooter->position = Vec2{Fix32::zero(), Fix32::from_int(12)};
    shooter->velocity = {};
    shooter->health = Fix32::from_int(100);
    shooter->shield = 0;
    shooter->shield_regen_delay = 1000;
    shooter->invulnerable_ticks = 0;
    shooter->weapon = static_cast<std::uint8_t>(WeaponType::Grenade);
    shooter->ammo = weapon_stats(WeaponType::Grenade).magazine;

    enemy->position = Vec2{Fix32::from_int(8), Fix32::from_int(12)};
    enemy->velocity = {};
    enemy->health = Fix32::from_int(100);
    enemy->shield = 0;
    enemy->shield_regen_delay = 1000;
    enemy->invulnerable_ticks = 0;

    InputFrame fire;
    PlayerCommand command;
    command.entity = shooter->id;
    command.command.tick = world.tick();
    command.command.buttons = kButtonFire;
    command.command.aim_x = 32767;
    command.command.aim_y = 0;
    fire.commands.push_back(command);
    world.step(fire);
    CHECK(world.projectiles().size() == 1);

    bool bounced = false;
    for (int tick = 0; tick < 30 && !bounced; ++tick) {
        world.step(InputFrame{});
        if (world.projectiles().empty()) {
            break;
        }
        world.projectiles().for_each([&](const Projectile& projectile) {
            if (projectile.velocity.x < Fix32::zero()) {
                bounced = true;
            }
        });
    }
    CHECK(bounced);
    CHECK(!world.projectiles().empty());
    CHECK(enemy->health == Fix32::from_int(100));
}

void test_grenade_platform_edge() {
    using namespace nsync::core;
    Map map;
    map.id = "grenade_platform_edge_test";
    map.bounds = AABB{
        Vec2{Fix32::from_int(-6), Fix32::zero()},
        Vec2{Fix32::from_int(6), Fix32::from_int(16)}};
    map.solids = {AABB{
        Vec2{Fix32::from_int(-6), Fix32::from_int(12)},
        Vec2{Fix32::from_int(6), Fix32::from_int(14)}}};
    map.one_way_platforms = {AABB{
        Vec2{Fix32::from_int(-1), Fix32::from_int(6)},
        Vec2{Fix32::from_int(1), Fix32::from_ratio(63, 10)}}};
    map.platform_hatchway = {1};
    map.spawns.fill(Vec2{Fix32::zero(), Fix32::from_int(2)});
    std::string error;
    CHECK(map.valid(&error));

    World world;
    world.set_map(map);
    world.reset(1, 1);
    Player* shooter = world.players().get(EntityId::make(0, 1));
    CHECK(shooter != nullptr);
    if (shooter == nullptr) {
        return;
    }
    shooter->position =
        Vec2{Fix32::from_ratio(861, 1000), Fix32::from_int(2)};
    shooter->velocity = {};
    shooter->weapon = static_cast<std::uint8_t>(WeaponType::Grenade);
    shooter->ammo = weapon_stats(WeaponType::Grenade).magazine;

    InputFrame fire;
    PlayerCommand command;
    command.entity = shooter->id;
    command.command.tick = world.tick();
    command.command.buttons = kButtonFire;
    command.command.aim_x = 0;
    command.command.aim_y = 32767;
    fire.commands.push_back(command);
    world.step(fire);
    CHECK(world.projectiles().size() == 1);
    Fix32 spawn_x{};
    world.projectiles().for_each([&](const Projectile& projectile) {
        spawn_x = projectile.position.x;
    });
    CHECK(spawn_x > Fix32::one());
    CHECK(spawn_x < Fix32::from_ratio(12, 10));

    const Fix32 platform_bottom = Fix32::from_ratio(63, 10);
    bool stayed_above = true;
    InputFrame empty;
    for (int tick = 0; tick < 90 && !world.projectiles().empty(); ++tick) {
        world.step(empty);
        world.projectiles().for_each([&](const Projectile& projectile) {
            if (projectile.position.y > platform_bottom) {
                stayed_above = false;
            }
        });
    }
    CHECK(stayed_above);
}

void test_grenade_upward_shot() {
    using namespace nsync::core;
    Map map;
    map.id = "grenade_upward_test";
    map.bounds = AABB{
        Vec2{Fix32::from_int(-8), Fix32::zero()},
        Vec2{Fix32::from_int(8), Fix32::from_int(24)}};
    map.spawns.fill(Vec2{Fix32::zero(), Fix32::from_int(12)});
    std::string error;
    CHECK(map.valid(&error));

    World world;
    world.set_map(map);
    world.reset(1, 1);
    Player* shooter = world.players().get(EntityId::make(0, 1));
    CHECK(shooter != nullptr);
    if (shooter == nullptr) {
        return;
    }
    shooter->position = Vec2{Fix32::zero(), Fix32::from_int(12)};
    shooter->velocity = {};
    shooter->weapon = static_cast<std::uint8_t>(WeaponType::Grenade);
    shooter->ammo = weapon_stats(WeaponType::Grenade).magazine;

    InputFrame fire;
    PlayerCommand command;
    command.entity = shooter->id;
    command.command.tick = world.tick();
    command.command.buttons = kButtonFire;
    command.command.aim_x = 0;
    command.command.aim_y = -32767;
    fire.commands.push_back(command);
    world.step(fire);
    CHECK(world.projectiles().size() == 1);

    Fix32 highest = Fix32::from_int(24);
    bool rose = false;
    InputFrame empty;
    for (int tick = 0; tick < 90 && !world.projectiles().empty(); ++tick) {
        world.step(empty);
        world.projectiles().for_each([&](const Projectile& projectile) {
            if (projectile.velocity.y < Fix32::zero() &&
                projectile.position.y < Fix32::from_int(12)) {
                rose = true;
            }
            if (projectile.position.y < highest) {
                highest = projectile.position.y;
            }
        });
    }
    CHECK(rose);
    CHECK(highest < Fix32::from_int(8));
    CHECK(world.projectiles().empty());
}

void test_grenade_flight_speed() {
    using namespace nsync::core;
    Map map;
    map.id = "grenade_speed_test";
    map.bounds = AABB{
        Vec2{Fix32::from_int(-10), Fix32::zero()},
        Vec2{Fix32::from_int(30), Fix32::from_int(24)}};
    map.spawns.fill(Vec2{Fix32::zero(), Fix32::from_int(12)});
    std::string error;
    CHECK(map.valid(&error));

    World world;
    world.set_map(map);
    world.reset(1, 1);
    Player* shooter = world.players().get(EntityId::make(0, 1));
    CHECK(shooter != nullptr);
    if (shooter == nullptr) {
        return;
    }
    shooter->position = Vec2{Fix32::zero(), Fix32::from_int(12)};
    shooter->velocity = {};
    shooter->weapon = static_cast<std::uint8_t>(WeaponType::Grenade);
    shooter->ammo = weapon_stats(WeaponType::Grenade).magazine;

    InputFrame fire;
    PlayerCommand command;
    command.entity = shooter->id;
    command.command.tick = world.tick();
    command.command.buttons = kButtonFire;
    command.command.aim_x = 32767;
    command.command.aim_y = 0;
    fire.commands.push_back(command);
    world.step(fire);
    CHECK(world.projectiles().size() == 1);
    Fix32 start_x{};
    world.projectiles().for_each([&](const Projectile& projectile) {
        start_x = projectile.position.x;
    });

    InputFrame empty;
    for (int tick = 0; tick < 60; ++tick) {
        world.step(empty);
    }
    Fix32 end_x = start_x;
    world.projectiles().for_each([&](const Projectile& projectile) {
        end_x = projectile.position.x;
    });
    const Fix32 travelled = end_x - start_x;
    CHECK(travelled > Fix32::from_ratio(119, 10));
    CHECK(travelled < Fix32::from_ratio(121, 10));
}

void test_original_shell_rules() {
    using namespace nsync::core;
    const auto rifle_damage_at = [](Fix32 victim_x) {        Map map;
        map.id = "range_test";
        map.bounds = AABB{
            Vec2{Fix32::from_int(-30), Fix32::zero()},
            Vec2{Fix32::from_int(30), Fix32::from_int(16)}};
        map.solids = {AABB{
            Vec2{Fix32::from_int(-30), Fix32::from_int(11)},
            Vec2{Fix32::from_int(30), Fix32::from_int(12)}}};
        World world;
        world.set_map(map);
        world.reset(1, 2);
        world.step(InputFrame{});
        Player* shooter = world.players().get(EntityId::make(0, 1));
        Player* victim = world.players().get(EntityId::make(1, 1));
        CHECK(shooter != nullptr);
        CHECK(victim != nullptr);
        if (shooter == nullptr || victim == nullptr) {
            return Fix32::from_int(100);
        }
        shooter->position =
            Vec2{Fix32::from_int(-25), Fix32::from_ratio(103, 10)};
        shooter->velocity = {};
        shooter->weapon = static_cast<std::uint8_t>(WeaponType::Rifle);
        shooter->ammo = weapon_stats(WeaponType::Rifle).magazine;
        victim->position =
            Vec2{victim_x, Fix32::from_ratio(103, 10)};
        victim->velocity = {};
        victim->health = Fix32::from_int(100);
        victim->shield = 0;
        victim->invulnerable_ticks = 0;
        InputFrame fire;
        PlayerCommand command;
        command.entity = shooter->id;
        command.command.tick = world.tick();
        command.command.buttons = kButtonFire;
        command.command.aim_x = 32767;
        fire.commands.push_back(command);
        world.step(fire);
        for (int tick = 0; tick < 60; ++tick) {
            world.step(InputFrame{});
        }
        return victim->health;
    };
    CHECK(rifle_damage_at(Fix32::from_int(-15)) < Fix32::from_int(100));
    CHECK(rifle_damage_at(Fix32::zero()) == Fix32::from_int(100));

    Map wall_map = Map::default_arena();
    wall_map.solids.push_back(AABB{
        Vec2{Fix32::from_ratio(4, 10), Fix32::from_int(9)},
        Vec2{Fix32::from_ratio(52, 100), Fix32::from_int(11)}});
    World wall_world;
    wall_world.set_map(wall_map);
    wall_world.reset(1, 1);
    wall_world.step(InputFrame{});
    Player* smg = wall_world.players().get(EntityId::make(0, 1));
    CHECK(smg != nullptr);
    if (smg != nullptr) {
        smg->position =
            Vec2{Fix32::zero(), Fix32::from_ratio(103, 10)};
        smg->velocity = {};
        smg->weapon = static_cast<std::uint8_t>(WeaponType::Smg);
        smg->ammo = weapon_stats(WeaponType::Smg).magazine;
        smg->fire_latch = 1;
        smg->warmup_timer = 0;
        smg->fire_cooldown = 0;
        InputFrame fire;
        PlayerCommand command;
        command.entity = smg->id;
        command.command.tick = wall_world.tick();
        command.command.buttons = kButtonFire;
        command.command.aim_x = 32767;
        fire.commands.push_back(command);
        wall_world.step(fire);
        CHECK(smg->ammo == weapon_stats(WeaponType::Smg).magazine);
        CHECK(wall_world.projectiles().empty());
    }

    World shells;
    shells.set_map(Map::default_arena());
    shells.reset(1, 2);
    shells.step(InputFrame{});
    Player* thrower = shells.players().get(EntityId::make(0, 1));
    Player* shooter = shells.players().get(EntityId::make(1, 1));
    CHECK(thrower != nullptr);
    CHECK(shooter != nullptr);
    if (thrower != nullptr && shooter != nullptr) {
        thrower->position =
            Vec2{Fix32::from_int(-5), Fix32::from_ratio(103, 10)};
        thrower->velocity = {};
        thrower->weapon =
            static_cast<std::uint8_t>(WeaponType::Grenade);
        thrower->ammo = weapon_stats(WeaponType::Grenade).magazine;
        shooter->position =
            Vec2{Fix32::from_ratio(35, 10), Fix32::from_ratio(103, 10)};
        shooter->velocity = {};
        shooter->weapon = static_cast<std::uint8_t>(WeaponType::Smg);
        shooter->ammo = weapon_stats(WeaponType::Smg).magazine;
        shooter->health = Fix32::from_int(100);
        shooter->shield = 0;
        shooter->invulnerable_ticks = 0;
        InputFrame throw_fire;
        PlayerCommand throw_command;
        throw_command.entity = thrower->id;
        throw_command.command.tick = shells.tick();
        throw_command.command.buttons = kButtonFire;
        throw_command.command.aim_x = 32767;
        throw_command.command.aim_y = -20000;
        throw_fire.commands.push_back(throw_command);
        shells.step(throw_fire);
        for (int tick = 0; tick < 40; ++tick) {
            shells.step(InputFrame{});
        }
        CHECK(shells.projectiles().size() == 1);
        shooter->fire_latch = 1;
        shooter->warmup_timer = 0;
        shooter->fire_cooldown = 0;
        InputFrame shoot_fire;
        PlayerCommand shoot_command;
        shoot_command.entity = shooter->id;
        shoot_command.command.tick = shells.tick();
        shoot_command.command.buttons = kButtonFire;
        shoot_command.command.aim_x = -32767;
        shoot_fire.commands.push_back(shoot_command);
        Vec2 grenade_position{};
        shells.projectiles().for_each([&](const Projectile& projectile) {
            grenade_position = projectile.position;
        });
        shooter->position = Vec2{
            grenade_position.x + Fix32::from_ratio(18, 10),
            grenade_position.y - Fix32::from_ratio(39, 100)};
        shells.step(shoot_fire);
        CHECK(shells.projectiles().empty());
        CHECK(shooter->health < Fix32::from_int(100));
    }
}

void test_golden_replay() {
    using namespace nsync::core;
    const Replay replay = make_golden_replay(1, nsync::test::kGoldenTickCount);
    ReplayRun run;
    std::string error;
    CHECK(run_replay(replay, nsync::test::kGoldenTickRate, run, &error));
    CHECK(error.empty());
    CHECK(run.hashes.size() == std::size(nsync::test::kGoldenTickHashes));
    CHECK(run.hash_ticks.size() == run.hashes.size());
    for (std::size_t index = 0; index < run.hashes.size(); ++index) {
        CHECK(run.hash_ticks[index] == index * nsync::test::kGoldenTickRate);
        CHECK(run.hashes[index] == nsync::test::kGoldenTickHashes[index]);
    }
}

void test_grenade_platform_launch_spawn() {
    using namespace nsync::core;
    Map map;
    std::string error;
    const std::filesystem::path path =
        std::filesystem::path(NSYNC_SOURCE_DIR) / "assets" / "maps" /
        "world5.json";
    CHECK(load_map_file(path, map, &error));

    World world;
    world.set_map(map);
    world.reset(1, 1);
    Player* shooter = world.players().get(EntityId::make(0, 1));
    CHECK(shooter != nullptr);
    if (shooter == nullptr) {
        return;
    }
    // Stand on platform [55.93, 22.06, 7.48, 0.3] and aim down-right, the
    // way the grenade launcher is normally fired while walking a ledge.
    const Vec2 start{
        Fix32::from_ratio(5651, 100), Fix32::from_ratio(2136, 100)};
    shooter->position = start;
    shooter->velocity = {};
    shooter->weapon = static_cast<std::uint8_t>(WeaponType::Grenade);
    shooter->ammo = weapon_stats(WeaponType::Grenade).magazine;
    shooter->grounded = 1;

    InputFrame fire;
    PlayerCommand command;
    command.entity = shooter->id;
    command.command.tick = world.tick();
    command.command.buttons = kButtonFire;
    command.command.aim_x = 19332;
    command.command.aim_y = 26214;
    fire.commands.push_back(command);
    world.step(fire);
    CHECK(world.projectiles().size() == 1);

    const Fix32 platform_top = Fix32::from_ratio(2206, 100);
    Vec2 origin{};
    Vec2 velocity{};
    world.projectiles().for_each([&](const Projectile& projectile) {
        origin = projectile.origin;
        velocity = projectile.velocity;
    });
    // The muzzle is buried in the platform, so the shot is blocked and the
    // shell starts at the thrower's centre, matching GunControllerImpl::shoot.
    CHECK(near_raw(origin.x, start.x, 2048));
    CHECK(near_raw(origin.y, start.y, 2048));
    // Firing right must keep travelling right.
    CHECK(velocity.x > Fix32::zero());

    InputFrame empty;
    bool reversed = false;
    bool sank = false;
    for (int tick = 0; tick < 30; ++tick) {
        world.step(empty);
        world.projectiles().for_each([&](const Projectile& projectile) {
            if (projectile.velocity.x < Fix32::zero()) {
                reversed = true;
            }
            if (projectile.position.y > platform_top) {
                sank = true;
            }
        });
    }
    // The shell bounces along the platform instead of being reflected or
    // dropping through it (the old "air wall" behaviour).
    CHECK(!reversed);
    CHECK(!sank);
}

void test_border_segment_collision() {
    using namespace nsync::core;
    const Segment diagonal{
        Vec2{Fix32::zero(), Fix32::zero()},
        Vec2{Fix32::from_int(10), Fix32::from_int(10)}};

    Fix32 hit_t{};
    Vec2 normal{};
    CHECK(swept_point_hits_segment(
        Segment{
            Vec2{Fix32::from_int(3), Fix32::from_int(2)},
            Vec2{Fix32::from_int(5), Fix32::from_int(6)}},
        diagonal,
        Fix32::from_ratio(1, 5),
        hit_t,
        normal));
    CHECK(normal.y < Fix32::zero());
    CHECK(!swept_point_hits_segment(
        Segment{
            Vec2{Fix32::from_int(3), Fix32::from_int(2)},
            Vec2{Fix32::from_int(5), Fix32::from_int(3)}},
        diagonal,
        Fix32::from_ratio(1, 5),
        hit_t,
        normal));

    Map map;
    map.id = "border_sweep_test";
    map.bounds = AABB{
        Vec2{Fix32::zero(), Fix32::zero()},
        Vec2{Fix32::from_int(15), Fix32::from_int(15)}};
    map.borders = {diagonal};
    map.solids = {AABB{
        Vec2{Fix32::from_ratio(444, 100), Fix32::from_ratio(444, 100)},
        Vec2{Fix32::from_ratio(506, 100), Fix32::from_ratio(506, 100)}}};
    map.spawns.fill(Vec2{Fix32::from_int(1), Fix32::from_int(1)});
    std::string error;
    CHECK(map.valid(&error));

    const std::vector<std::uint8_t> binary = encode_map_bin(map);
    CHECK(!binary.empty());
    Map decoded;
    CHECK(decode_map_bin(binary.data(), binary.size(), decoded, &error));
    CHECK(decoded.borders.size() == 1);
    CHECK(decoded.borders[0] == diagonal);

    Map imported;
    const std::filesystem::path path =
        std::filesystem::path(NSYNC_SOURCE_DIR) / "assets" / "maps" /
        "crystal_01.json";
    CHECK(load_map_file(path, imported, &error));
    CHECK(!imported.borders.empty());
    CHECK(imported.borders.size() < imported.solids.size());

    World world;
    world.set_map(map);
    world.reset(1, 1);
    Player* shooter = world.players().get(EntityId::make(0, 1));
    CHECK(shooter != nullptr);
    if (shooter == nullptr) {
        return;
    }
    shooter->position = Vec2{Fix32::from_int(4), Fix32::from_ratio(355, 100)};
    shooter->velocity = {};
    shooter->weapon = static_cast<std::uint8_t>(WeaponType::Grenade);
    shooter->ammo = weapon_stats(WeaponType::Grenade).magazine;

    InputFrame fire;
    PlayerCommand command;
    command.entity = shooter->id;
    command.command.tick = world.tick();
    command.command.buttons = kButtonFire;
    command.command.aim_x = 23170;
    command.command.aim_y = 23170;
    fire.commands.push_back(command);
    world.step(fire);
    CHECK(world.projectiles().size() == 1);

    bool bounced = false;
    bool advanced = false;
    for (int tick = 0; tick < 8; ++tick) {
        world.step(InputFrame{});
        world.projectiles().for_each([&](const Projectile& projectile) {
            if (projectile.velocity.x < Fix32::zero()) {
                bounced = true;
            }
            if (projectile.position.x > Fix32::from_ratio(51, 10)) {
                advanced = true;
            }
        });
    }
    CHECK(!bounced);
    CHECK(advanced);

    Map wall_map;
    wall_map.id = "border_wall_test";
    wall_map.bounds = AABB{
        Vec2{Fix32::zero(), Fix32::zero()},
        Vec2{Fix32::from_int(20), Fix32::from_int(20)}};
    wall_map.borders = {Segment{
        Vec2{Fix32::from_int(6), Fix32::zero()},
        Vec2{Fix32::from_int(6), Fix32::from_int(20)}}};
    wall_map.spawns.fill(Vec2{Fix32::from_int(1), Fix32::from_int(1)});
    CHECK(wall_map.valid(&error));

    World wall_world;
    wall_world.set_map(wall_map);
    wall_world.reset(1, 1);
    Player* wall_shooter = wall_world.players().get(EntityId::make(0, 1));
    CHECK(wall_shooter != nullptr);
    if (wall_shooter == nullptr) {
        return;
    }
    wall_shooter->position = Vec2{Fix32::from_int(2), Fix32::from_int(10)};
    wall_shooter->velocity = {};
    wall_shooter->weapon = static_cast<std::uint8_t>(WeaponType::Grenade);
    wall_shooter->ammo = weapon_stats(WeaponType::Grenade).magazine;

    InputFrame wall_fire;
    PlayerCommand wall_command;
    wall_command.entity = wall_shooter->id;
    wall_command.command.tick = wall_world.tick();
    wall_command.command.buttons = kButtonFire;
    wall_command.command.aim_x = 32767;
    wall_command.command.aim_y = 0;
    wall_fire.commands.push_back(wall_command);
    wall_world.step(wall_fire);
    CHECK(wall_world.projectiles().size() == 1);

    bool wall_bounced = false;
    Fix32 wall_hit_x{};
    for (int tick = 0; tick < 40 && !wall_bounced; ++tick) {
        wall_world.step(InputFrame{});
        wall_world.projectiles().for_each([&](const Projectile& projectile) {
            if (projectile.velocity.x < Fix32::zero()) {
                wall_bounced = true;
                wall_hit_x = projectile.position.x;
            }
        });
    }
    CHECK(wall_bounced);
    CHECK(wall_hit_x >= Fix32::from_ratio(578, 100));
    CHECK(wall_hit_x <= Fix32::from_ratio(582, 100));
}

void test_shell_endpoint_grazing() {
    using namespace nsync::core;
    Fix32 hit_t{};
    Vec2 normal{};

    const Segment wall{Vec2{Fix32::from_int(10), Fix32::from_int(10)},
                       Vec2{Fix32::from_int(10), Fix32::from_int(11)}};
    const Segment graze{Vec2{Fix32::from_ratio(981, 100),
                             Fix32::from_ratio(95, 10)},
                        Vec2{Fix32::from_ratio(981, 100),
                             Fix32::from_ratio(105, 10)}};
    CHECK(!swept_point_hits_segment(
        graze, wall, Fix32::from_ratio(1, 5), hit_t, normal));

    const Segment approach{Vec2{Fix32::from_ratio(96, 10),
                                Fix32::from_int(10)},
                           Vec2{Fix32::from_ratio(99, 10),
                                Fix32::from_int(10)}};
    CHECK(swept_point_hits_segment(
        approach, wall, Fix32::from_ratio(1, 5), hit_t, normal));

    const Segment floor{Vec2{Fix32::from_int(10), Fix32::from_int(10)},
                        Vec2{Fix32::from_ratio(14, 1), Fix32::from_int(10)}};
    const Segment rolling{Vec2{Fix32::from_ratio(135, 10),
                               Fix32::from_ratio(98, 10)},
                          Vec2{Fix32::from_ratio(145, 10),
                               Fix32::from_ratio(98, 10)}};
    CHECK(!swept_point_hits_segment(
        rolling, floor, Fix32::from_ratio(1, 5), hit_t, normal));

    const Segment falling{Vec2{Fix32::from_ratio(135, 10),
                               Fix32::from_ratio(97, 10)},
                          Vec2{Fix32::from_ratio(135, 10),
                               Fix32::from_ratio(99, 10)}};
    CHECK(swept_point_hits_segment(
        falling, floor, Fix32::from_ratio(1, 5), hit_t, normal));

    const Segment platform_top{
        Vec2{Fix32::from_ratio(63606, 1000), Fix32::from_ratio(21218, 1000)},
        Vec2{Fix32::from_ratio(55506, 1000), Fix32::from_ratio(21218, 1000)}};
    const Segment from_below{
        Vec2{Fix32::from_int(60), Fix32::from_ratio(217, 10)},
        Vec2{Fix32::from_int(60), Fix32::from_ratio(213, 10)}};
    CHECK(swept_point_hits_segment(
        from_below, platform_top, Fix32::from_ratio(1, 5), hit_t, normal));
    CHECK(!swept_point_hits_segment(
        from_below,
        platform_top,
        Fix32::from_ratio(1, 5),
        hit_t,
        normal,
        true));

    const Segment from_above{
        Vec2{Fix32::from_int(60), Fix32::from_ratio(209, 10)},
        Vec2{Fix32::from_int(60), Fix32::from_ratio(211, 10)}};
    CHECK(swept_point_hits_segment(
        from_above,
        platform_top,
        Fix32::from_ratio(1, 5),
        hit_t,
        normal,
        true));
}

void test_platform_shape_collision() {
    using namespace nsync::core;
    Map map;
    map.id = "platform_shape_test";
    map.bounds = AABB{
        Vec2{Fix32::zero(), Fix32::zero()},
        Vec2{Fix32::from_int(20), Fix32::from_int(20)}};
    const std::array<Vec2, 4> plank{
        Vec2{Fix32::from_int(10), Fix32::from_ratio(99, 10)},
        Vec2{Fix32::from_int(12), Fix32::from_ratio(79, 10)},
        Vec2{Fix32::from_ratio(121, 10), Fix32::from_int(8)},
        Vec2{Fix32::from_ratio(101, 10), Fix32::from_int(10)}};
    map.one_way_platforms = {AABB{
        Vec2{Fix32::from_int(10), Fix32::from_ratio(79, 10)},
        Vec2{Fix32::from_ratio(121, 10), Fix32::from_int(10)}}};
    map.platform_shapes = {plank};
    map.platform_hatchway = {1};
    map.spawns.fill(Vec2{Fix32::from_int(2), Fix32::from_int(2)});
    std::string error;
    CHECK(map.valid(&error));

    const std::vector<std::uint8_t> binary = encode_map_bin(map);
    CHECK(!binary.empty());
    Map decoded;
    CHECK(decode_map_bin(binary.data(), binary.size(), decoded, &error));
    CHECK(decoded.platform_shapes.size() == 1);
    CHECK(decoded.platform_shapes[0] == plank);

    World world;
    world.set_map(map);
    world.reset(1, 1);
    Player* shooter = world.players().get(EntityId::make(0, 1));
    CHECK(shooter != nullptr);
    if (shooter == nullptr) {
        return;
    }
    shooter->position = Vec2{Fix32::from_ratio(85, 10), Fix32::from_int(8)};
    shooter->velocity = {};
    shooter->weapon = static_cast<std::uint8_t>(WeaponType::Grenade);
    shooter->ammo = weapon_stats(WeaponType::Grenade).magazine;

    InputFrame fire;
    PlayerCommand command;
    command.entity = shooter->id;
    command.command.tick = world.tick();
    command.command.buttons = kButtonFire;
    command.command.aim_x = 32767;
    command.command.aim_y = 0;
    fire.commands.push_back(command);
    world.step(fire);
    CHECK(world.projectiles().size() == 1);

    bool bounced = false;
    bool inside_box = false;
    for (int tick = 0; tick < 9; ++tick) {
        world.step(InputFrame{});
        world.projectiles().for_each([&](const Projectile& projectile) {
            if (projectile.velocity.x < Fix32::zero()) {
                bounced = true;
            }
            if (projectile.position.x > Fix32::from_ratio(105, 10) &&
                projectile.position.y > Fix32::from_ratio(79, 10) &&
                projectile.position.y < Fix32::from_int(10)) {
                inside_box = true;
            }
        });
    }
    CHECK(!bounced);
    CHECK(inside_box);
}

void test_shock_blocks_actions() {
    using namespace nsync::core;
    World world;
    world.set_map(Map::default_arena());
    world.reset(1, 2);
    world.step(InputFrame{});
    Player* attacker = world.players().get(EntityId::make(0, 1));
    Player* victim = world.players().get(EntityId::make(1, 1));
    CHECK(attacker != nullptr);
    CHECK(victim != nullptr);
    if (attacker == nullptr || victim == nullptr) {
        return;
    }
    attacker->hero = static_cast<std::uint8_t>(HeroType::Gunner);
    attacker->position =
        Vec2{Fix32::from_int(-5), Fix32::from_ratio(103, 10)};
    attacker->velocity = {};
    victim->position =
        Vec2{Fix32::from_int(-4), Fix32::from_ratio(103, 10)};
    victim->velocity = {};
    victim->weapon = static_cast<std::uint8_t>(WeaponType::Smg);
    victim->ammo = 10;
    victim->reload_timer = 40;
    victim->health = Fix32::from_int(100);
    victim->shield = 0;
    victim->invulnerable_ticks = 0;

    InputFrame press;
    PlayerCommand command;
    command.entity = attacker->id;
    command.command.tick = world.tick();
    command.command.buttons = kButtonGadget2;
    command.command.aim_x = 32767;
    press.commands.push_back(command);
    world.step(press);

    int guard = 0;
    while (victim->stun_ticks == 0 && guard < 60) {
        world.step(InputFrame{});
        ++guard;
    }
    CHECK(victim->stun_ticks > 0);
    CHECK(victim->reload_timer == 0);

    const std::uint8_t weapon_before = victim->weapon;
    InputFrame held;
    PlayerCommand held_command;
    held_command.entity = victim->id;
    held_command.command.buttons = static_cast<std::uint16_t>(
        kButtonFire | kButtonReload | kButtonWeaponSwitch |
        kButtonGadget2);
    held_command.command.aim_x = 32767;
    for (int tick = 0; tick < 12; ++tick) {
        held_command.command.tick = world.tick();
        held.commands.assign(1, held_command);
        world.step(held);
    }
    CHECK(victim->stun_ticks > 0);
    CHECK(victim->ammo == 10);
    CHECK(victim->reload_timer == 0);
    CHECK(victim->weapon == weapon_before);
    CHECK(victim->gadget_cooldowns[1] == 0);
    CHECK(world.projectiles().size() == 0);

    guard = 0;
    while (victim->stun_ticks > 0 && guard < 300) {
        world.step(InputFrame{});
        ++guard;
    }
    CHECK(victim->stun_ticks == 0);
    CHECK(victim->weapon == weapon_before);
    CHECK(victim->ammo == 10);

    InputFrame resumed;
    PlayerCommand resumed_command;
    resumed_command.entity = victim->id;
    resumed_command.command.buttons = kButtonFire;
    resumed_command.command.aim_x = 32767;
    for (int tick = 0; tick < 35; ++tick) {
        resumed_command.command.tick = world.tick();
        resumed.commands.assign(1, resumed_command);
        world.step(resumed);
    }
    CHECK(victim->weapon == weapon_before);
    CHECK(victim->ammo == 10);
    for (int tick = 0; tick < 15; ++tick) {
        resumed_command.command.tick = world.tick();
        resumed.commands.assign(1, resumed_command);
        world.step(resumed);
    }
    CHECK(victim->ammo < 10);

    World cleanse;
    cleanse.set_map(Map::default_arena());
    cleanse.reset(1, 2);
    cleanse.step(InputFrame{});
    Player* caster = cleanse.players().get(EntityId::make(0, 1));
    Player* cleanser = cleanse.players().get(EntityId::make(1, 1));
    CHECK(caster != nullptr);
    CHECK(cleanser != nullptr);
    if (caster == nullptr || cleanser == nullptr) {
        return;
    }
    caster->hero = static_cast<std::uint8_t>(HeroType::Gunner);
    caster->position =
        Vec2{Fix32::from_int(-5), Fix32::from_ratio(103, 10)};
    caster->velocity = {};
    cleanser->hero = static_cast<std::uint8_t>(HeroType::Grenadier);
    cleanser->position =
        Vec2{Fix32::from_int(-4), Fix32::from_ratio(103, 10)};
    cleanser->velocity = {};
    cleanser->health = Fix32::from_int(100);
    cleanser->shield = 0;
    cleanser->invulnerable_ticks = 0;
    cleanse.step(InputFrame{});

    InputFrame shield_press;
    PlayerCommand shield_command;
    shield_command.entity = caster->id;
    shield_command.command.tick = cleanse.tick();
    shield_command.command.buttons = kButtonGadget2;
    shield_command.command.aim_x = 32767;
    shield_press.commands.push_back(shield_command);
    cleanse.step(shield_press);

    guard = 0;
    while (cleanser->stun_ticks == 0 && guard < 60) {
        cleanse.step(InputFrame{});
        ++guard;
    }
    CHECK(cleanser->stun_ticks > 0);

    InputFrame cleanse_press;
    PlayerCommand cleanse_command;
    cleanse_command.entity = cleanser->id;
    cleanse_command.command.tick = cleanse.tick();
    cleanse_command.command.buttons = kButtonGadget3;
    cleanse_press.commands.push_back(cleanse_command);
    cleanse.step(cleanse_press);
    CHECK(cleanser->stun_ticks == 0);
    CHECK(cleanser->gadget_cooldowns[2] > 0);
}

} // namespace

bool run_determinism_tests(const std::filesystem::path& root) {
    g_failures = 0;
    test_hash();
    test_aim_geometry();
    test_geometry();
    test_rng();
    test_entities();
    test_byte_io();
    test_map();
    test_gadgets();
    test_jetpack_fuel();
    test_shock_blocks_actions();
    test_weapons_and_match();
    test_point_blank_weapon_hit();
    test_projectile_wall_and_point_blank();
    test_warmup_and_reload();
    test_explosion_and_bounce();
    test_grenade_bounce_policy();
    test_grenade_enemy_detonation();
    test_grenade_wall_bounce_no_detonate();
    test_grenade_platform_edge();
    test_grenade_upward_shot();
    test_grenade_flight_speed();
    test_grenade_platform_launch_spawn();
    test_border_segment_collision();
    test_shell_endpoint_grazing();
    test_platform_shape_collision();
    test_original_shell_rules();
    test_objective_mode();
    test_original_control_map_objectives();
    test_objective_recapture_semantics();
    test_control_point_generator();
    test_crystal_mode();
    test_original_crystal_map();
    test_supplies();
    test_snapshot_restore();
    test_replay_roundtrip(root);
    test_golden_replay();

    if (g_failures != 0) {
        std::fprintf(stderr, "determinism tests: %d failure(s)\n", g_failures);
        return false;
    }
    std::printf("determinism tests: PASS\n");
    return true;
}

