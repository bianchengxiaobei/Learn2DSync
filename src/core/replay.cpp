#include "core/replay.h"

#include "core/byte_io.h"
#include "core/world.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <utility>

namespace nsync::core {
namespace {

bool valid_player_count(std::uint16_t count) {
    return count >= 1 && count <= kMaxPlayers;
}

void write_command(ByteWriter& writer, const PlayerCommand& player_command) {
    writer.u32(player_command.entity.raw());
    writer.u32(player_command.command.tick);
    writer.u16(player_command.command.buttons);
    writer.i16(player_command.command.aim_x);
    writer.i16(player_command.command.aim_y);
    writer.u8(player_command.command.sequence);
}

bool read_command(ByteReader& reader, PlayerCommand& player_command) {
    std::uint32_t entity_raw = 0;
    std::uint32_t tick = 0;
    if (!reader.u32(entity_raw) ||
        !reader.u32(tick) ||
        !reader.u16(player_command.command.buttons) ||
        !reader.i16(player_command.command.aim_x) ||
        !reader.i16(player_command.command.aim_y) ||
        !reader.u8(player_command.command.sequence)) {
        return false;
    }
    player_command.entity = EntityId::from_raw(entity_raw);
    player_command.command.tick = tick;
    return true;
}

} // namespace

Replay make_golden_replay(std::uint64_t seed, std::uint32_t tick_count) {
    Replay replay;
    replay.seed = seed;
    replay.player_count = 2;
    replay.frames.reserve(tick_count);

    for (std::uint32_t tick = 0; tick < tick_count; ++tick) {
        ReplayFrame frame;
        frame.tick = tick;
        frame.commands.reserve(replay.player_count);

        for (std::uint16_t slot = 0; slot < replay.player_count; ++slot) {
            const bool right_team = (slot % 2U) != 0;
            PlayerCommand player_command;
            player_command.entity = EntityId::make(slot, 1);
            player_command.command.tick = tick;
            player_command.command.sequence = static_cast<std::uint8_t>(tick & 0xFFU);
            player_command.command.aim_x = right_team ? -32767 : 32767;
            player_command.command.aim_y = 0;

            const bool moving_right = ((tick / 45U) % 2U) == 0U;
            const bool move_right = right_team ? !moving_right : moving_right;
            if (move_right) {
                player_command.command.buttons |= kButtonRight;
            } else {
                player_command.command.buttons |= kButtonLeft;
            }

            if ((tick % 47U) == 0U) {
                player_command.command.buttons |= kButtonJump;
            }
            if ((tick % 3U) != 2U) {
                player_command.command.buttons |= kButtonFire;
            }
            if (tick == 100U) {
                player_command.command.buttons |= kButtonReload;
            }

            frame.commands.push_back(player_command);
        }

        replay.frames.push_back(std::move(frame));
    }

    return replay;
}

std::vector<std::uint8_t> encode_replay(const Replay& replay) {
    ByteWriter payload;
    payload.u32(replay.version);
    payload.u64(replay.seed);
    payload.u16(replay.player_count);
    payload.u32(static_cast<std::uint32_t>(replay.frames.size()));
    for (const ReplayFrame& frame : replay.frames) {
        payload.u32(frame.tick);
        payload.u32(static_cast<std::uint32_t>(frame.commands.size()));
        for (const PlayerCommand& command : frame.commands) {
            write_command(payload, command);
        }
    }

    ByteWriter writer;
    const std::vector<std::uint8_t>& data = payload.data();
    writer.u32(static_cast<std::uint32_t>(data.size()));
    writer.bytes(data.data(), data.size());
    return writer.data();
}

bool decode_replay(
    const std::uint8_t* data,
    std::size_t size,
    Replay& replay,
    std::string* error) {
    ByteReader reader(data, size);
    std::uint32_t payload_size = 0;
    if (!reader.u32(payload_size) || payload_size != reader.remaining()) {
        if (error) {
            *error = "invalid replay length prefix";
        }
        return false;
    }

    Replay result;
    if (!reader.u32(result.version) ||
        !reader.u64(result.seed) ||
        !reader.u16(result.player_count)) {
        if (error) {
            *error = "truncated replay header";
        }
        return false;
    }
    if (result.version != kReplayVersion) {
        if (error) {
            *error = "unsupported replay version";
        }
        return false;
    }
    if (!valid_player_count(result.player_count)) {
        if (error) {
            *error = "invalid replay player count";
        }
        return false;
    }

    std::uint32_t frame_count = 0;
    if (!reader.u32(frame_count) || frame_count > kReplayMaxTicks) {
        if (error) {
            *error = "invalid replay frame count";
        }
        return false;
    }
    result.frames.resize(frame_count);

    for (std::uint32_t frame_index = 0; frame_index < frame_count; ++frame_index) {
        ReplayFrame& frame = result.frames[frame_index];
        std::uint32_t command_count = 0;
        if (!reader.u32(frame.tick) ||
            !reader.u32(command_count) ||
            frame.tick != frame_index ||
            command_count > result.player_count) {
            if (error) {
                *error = "invalid replay frame";
            }
            return false;
        }
        frame.commands.resize(command_count);
        for (PlayerCommand& command : frame.commands) {
            if (!read_command(reader, command) ||
                !command.entity.valid() ||
                command.entity.index() >= result.player_count ||
                command.command.tick != frame.tick) {
                if (error) {
                    *error = "invalid replay command";
                }
                return false;
            }
        }
    }

    if (!reader.at_end()) {
        if (error) {
            *error = "replay has trailing bytes";
        }
        return false;
    }

    replay = std::move(result);
    return true;
}

bool decode_replay(
    const std::vector<std::uint8_t>& data,
    Replay& replay,
    std::string* error) {
    return decode_replay(data.data(), data.size(), replay, error);
}

bool save_replay(
    const std::filesystem::path& path,
    const Replay& replay,
    std::string* error) {
    const std::vector<std::uint8_t> data = encode_replay(replay);
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        if (error) {
            *error = "could not open replay for writing";
        }
        return false;
    }
    output.write(reinterpret_cast<const char*>(data.data()),
                 static_cast<std::streamsize>(data.size()));
    if (!output) {
        if (error) {
            *error = "could not write replay";
        }
        return false;
    }
    return true;
}

bool load_replay(
    const std::filesystem::path& path,
    Replay& replay,
    std::string* error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        if (error) {
            *error = "could not open replay for reading";
        }
        return false;
    }
    const std::vector<std::uint8_t> data{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    return decode_replay(data, replay, error);
}

bool run_replay(
    const Replay& replay,
    std::uint32_t hash_every,
    ReplayRun& run,
    std::string* error) {
    if (!valid_player_count(replay.player_count)) {
        if (error) {
            *error = "invalid replay player count";
        }
        return false;
    }
    if (replay.frames.size() > kReplayMaxTicks) {
        if (error) {
            *error = "replay has too many frames";
        }
        return false;
    }
    if (hash_every == 0) {
        hash_every = 1;
    }

    World world;
    world.reset(replay.seed, replay.player_count);

    ReplayRun result;
    result.hash_ticks.push_back(0);
    result.hashes.push_back(world.hash());

    for (const ReplayFrame& frame : replay.frames) {
        if (frame.tick != world.tick()) {
            if (error) {
                *error = "replay frame tick mismatch";
            }
            return false;
        }

        InputFrame input;
        input.commands = frame.commands;
        world.step(input);

        if ((world.tick() % hash_every) == 0 || world.tick() == replay.frames.size()) {
            result.hash_ticks.push_back(world.tick());
            result.hashes.push_back(world.hash());
        }
    }

    run = std::move(result);
    return true;
}

} // namespace nsync::core
