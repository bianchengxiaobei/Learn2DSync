#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nsync::core {

class EntityId {
public:
    static constexpr std::uint32_t kIndexBits = 20;
    static constexpr std::uint32_t kGenerationBits = 12;
    static constexpr std::uint32_t kIndexMask = (1U << kIndexBits) - 1U;
    static constexpr std::uint32_t kGenerationMask = (1U << kGenerationBits) - 1U;
    static constexpr std::uint32_t kMaxEntities = kIndexMask + 1U;

    constexpr EntityId() = default;

    static constexpr EntityId from_raw(std::uint32_t raw) {
        EntityId id;
        id.raw_ = raw;
        return id;
    }

    static constexpr EntityId make(std::uint32_t index, std::uint32_t generation) {
        return from_raw(((generation & kGenerationMask) << kIndexBits) | (index & kIndexMask));
    }

    static constexpr EntityId none() { return from_raw(0xFFFFFFFFU); }

    [[nodiscard]] constexpr std::uint32_t raw() const { return raw_; }
    [[nodiscard]] constexpr std::uint32_t index() const { return raw_ & kIndexMask; }
    [[nodiscard]] constexpr std::uint32_t generation() const { return raw_ >> kIndexBits; }
    [[nodiscard]] constexpr bool valid() const { return raw_ != 0xFFFFFFFFU; }

    friend constexpr bool operator==(EntityId a, EntityId b) { return a.raw_ == b.raw_; }
    friend constexpr bool operator!=(EntityId a, EntityId b) { return a.raw_ != b.raw_; }

private:
    std::uint32_t raw_ = 0xFFFFFFFFU;
};

constexpr bool entity_less(EntityId a, EntityId b) {
    if (a.index() != b.index()) {
        return a.index() < b.index();
    }
    return a.generation() < b.generation();
}

struct EntityAllocatorState {
    std::uint32_t next_index = 0;
    std::vector<std::uint16_t> generations;
    std::vector<std::uint32_t> free_list;
};

class EntityAllocator {
public:
    EntityId create();
    bool destroy(EntityId id);
    [[nodiscard]] bool alive(EntityId id) const;

    void clear();
    [[nodiscard]] EntityAllocatorState snapshot() const;
    bool restore(const EntityAllocatorState& state, std::string* error = nullptr);

    [[nodiscard]] std::uint32_t next_index() const { return next_index_; }
    [[nodiscard]] std::size_t live_count() const;

private:
    std::uint32_t next_index_ = 0;
    std::vector<std::uint16_t> generations_;
    std::vector<std::uint32_t> free_list_;
};

template <typename T>
class ComponentPool {
public:
    void clear() {
        components_.clear();
        present_.clear();
        size_ = 0;
    }

    bool add(EntityId id, const T& value) {
        if (!id.valid() || id.index() >= EntityId::kMaxEntities) {
            return false;
        }
        ensure(id.index());
        if (!present_[id.index()]) {
            ++size_;
        }
        components_[id.index()] = value;
        components_[id.index()].id = id;
        present_[id.index()] = 1;
        return true;
    }

    bool remove(EntityId id) {
        if (!id.valid() || id.index() >= present_.size() || !present_[id.index()]) {
            return false;
        }
        if (components_[id.index()].id != id) {
            return false;
        }
        present_[id.index()] = 0;
        components_[id.index()] = T{};
        --size_;
        return true;
    }

    [[nodiscard]] T* get(EntityId id) {
        if (!id.valid() || id.index() >= present_.size() || !present_[id.index()]) {
            return nullptr;
        }
        if (components_[id.index()].id != id) {
            return nullptr;
        }
        return &components_[id.index()];
    }

    [[nodiscard]] const T* get(EntityId id) const {
        if (!id.valid() || id.index() >= present_.size() || !present_[id.index()]) {
            return nullptr;
        }
        if (components_[id.index()].id != id) {
            return nullptr;
        }
        return &components_[id.index()];
    }

    template <typename Fn>
    void for_each(Fn&& fn) {
        for (std::size_t index = 0; index < components_.size(); ++index) {
            if (present_[index]) {
                fn(components_[index]);
            }
        }
    }

    template <typename Fn>
    void for_each(Fn&& fn) const {
        for (std::size_t index = 0; index < components_.size(); ++index) {
            if (present_[index]) {
                fn(components_[index]);
            }
        }
    }

    [[nodiscard]] std::size_t size() const { return size_; }
    [[nodiscard]] bool empty() const { return size_ == 0; }

private:
    void ensure(std::uint32_t index) {
        if (index < components_.size()) {
            return;
        }
        components_.resize(static_cast<std::size_t>(index) + 1);
        present_.resize(static_cast<std::size_t>(index) + 1, 0);
    }

    std::vector<T> components_;
    std::vector<std::uint8_t> present_;
    std::size_t size_ = 0;
};

} // namespace nsync::core
