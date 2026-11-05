#pragma once

#include "net/protocol.h"

#include <cstdint>
#include <map>

namespace nsync::net {

struct ActionEventFrame {
    std::uint32_t tick = 0;
    ClientActionMask actions = 0;
    ClientActionMask toggles = 0;
    std::uint8_t param = 0;
    std::uint16_t packets = 0;
};

[[nodiscard]] inline std::uint16_t synchronize_held_action_buttons(
    std::uint16_t raw_buttons,
    std::uint16_t action_buttons,
    std::uint16_t held_mask) {
    return static_cast<std::uint16_t>(
        (action_buttons & static_cast<std::uint16_t>(~held_mask)) |
        (raw_buttons & held_mask));
}

class ActionEventQueue {
public:
    [[nodiscard]] bool enqueue(
        std::uint32_t tick,
        std::uint8_t sequence,
        ClientActionMask actions,
        ClientActionMask toggles,
        std::uint8_t param) {
        if (actions == 0U) {
            return false;
        }
        const std::uint64_t key =
            (static_cast<std::uint64_t>(tick) << 8U) | sequence;
        ClientActionMask& seen = seen_[key];
        const ClientActionMask fresh = actions & ~seen;
        if (fresh == 0U) {
            ++duplicate_packets_;
            return false;
        }
        seen |= fresh;
        ActionEventFrame& frame = frames_[tick];
        frame.tick = tick;
        frame.actions |= fresh;
        frame.toggles |= toggles & fresh;
        if ((fresh & client_action_bit(ClientAction::ActivateGadget)) != 0U) {
            frame.param = param;
        }
        ++frame.packets;
        ++accepted_packets_;
        return true;
    }

    [[nodiscard]] bool consume(std::uint32_t tick, ActionEventFrame& frame) {
        const auto found = frames_.find(tick);
        if (found == frames_.end()) {
            return false;
        }
        frame = found->second;
        frames_.erase(found);
        return true;
    }

    void prune_before(std::uint32_t tick) {
        while (!frames_.empty() && frames_.begin()->first < tick) {
            frames_.erase(frames_.begin());
        }
        while (!seen_.empty() &&
               (seen_.begin()->first >> 8U) < tick) {
            seen_.erase(seen_.begin());
        }
    }

    void clear() {
        frames_.clear();
        seen_.clear();
        accepted_packets_ = 0;
        duplicate_packets_ = 0;
    }

    [[nodiscard]] std::uint64_t accepted_packets() const {
        return accepted_packets_;
    }

    [[nodiscard]] std::uint64_t duplicate_packets() const {
        return duplicate_packets_;
    }

    [[nodiscard]] std::size_t pending_frames() const {
        return frames_.size();
    }

private:
    std::map<std::uint32_t, ActionEventFrame> frames_;
    std::map<std::uint64_t, ClientActionMask> seen_;
    std::uint64_t accepted_packets_ = 0;
    std::uint64_t duplicate_packets_ = 0;
};

} // namespace nsync::net
