#include "core/entity.h"

#include <algorithm>

namespace nsync::core {
namespace {

std::uint16_t next_generation(std::uint16_t generation) {
    const std::uint32_t next = (static_cast<std::uint32_t>(generation) + 1U) &
                               EntityId::kGenerationMask;
    return static_cast<std::uint16_t>(next == 0 ? 1 : next);
}

} // namespace

EntityId EntityAllocator::create() {
    if (!free_list_.empty()) {
        const std::uint32_t index = free_list_.back();
        free_list_.pop_back();
        return EntityId::make(index, generations_[index]);
    }

    if (next_index_ >= EntityId::kMaxEntities) {
        return EntityId::none();
    }

    const std::uint32_t index = next_index_++;
    generations_.push_back(1);
    return EntityId::make(index, 1);
}

bool EntityAllocator::destroy(EntityId id) {
    if (!alive(id)) {
        return false;
    }

    const std::uint32_t index = id.index();
    generations_[index] = next_generation(generations_[index]);
    free_list_.push_back(index);
    return true;
}

bool EntityAllocator::alive(EntityId id) const {
    if (!id.valid() || id.index() >= generations_.size()) {
        return false;
    }
    if (generations_[id.index()] != id.generation()) {
        return false;
    }
    return std::find(free_list_.begin(), free_list_.end(), id.index()) == free_list_.end();
}

void EntityAllocator::clear() {
    next_index_ = 0;
    generations_.clear();
    free_list_.clear();
}

EntityAllocatorState EntityAllocator::snapshot() const {
    EntityAllocatorState state;
    state.next_index = next_index_;
    state.generations = generations_;
    state.free_list = free_list_;
    return state;
}

bool EntityAllocator::restore(const EntityAllocatorState& state, std::string* error) {
    if (state.next_index > EntityId::kMaxEntities) {
        if (error) {
            *error = "entity allocator next index out of range";
        }
        return false;
    }

    if (state.generations.size() != state.next_index) {
        if (error) {
            *error = "entity allocator generation count mismatch";
        }
        return false;
    }

    std::vector<std::uint8_t> free_flags(state.generations.size(), 0);
    for (const std::uint32_t index : state.free_list) {
        if (index >= state.generations.size() || free_flags[index]) {
            if (error) {
                *error = "entity allocator free list invalid";
            }
            return false;
        }
        free_flags[index] = 1;
    }

    for (std::size_t index = 0; index < state.generations.size(); ++index) {
        const std::uint16_t generation = state.generations[index];
        if (generation == 0 || generation > EntityId::kGenerationMask) {
            if (error) {
                *error = "entity allocator generation out of range";
            }
            return false;
        }
    }

    next_index_ = state.next_index;
    generations_ = state.generations;
    free_list_ = state.free_list;
    return true;
}

std::size_t EntityAllocator::live_count() const {
    return generations_.size() - free_list_.size();
}

} // namespace nsync::core
