#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace nsync::core {

class ByteWriter {
public:
    void u8(std::uint8_t value);
    void u16(std::uint16_t value);
    void u32(std::uint32_t value);
    void u64(std::uint64_t value);
    void i8(std::int8_t value);
    void i16(std::int16_t value);
    void i32(std::int32_t value);
    void bytes(const void* data, std::size_t size);

    void clear() { data_.clear(); }
    void reserve(std::size_t capacity) { data_.reserve(capacity); }
    [[nodiscard]] const std::vector<std::uint8_t>& data() const { return data_; }
    [[nodiscard]] std::vector<std::uint8_t> take() { return std::move(data_); }
    [[nodiscard]] std::size_t size() const { return data_.size(); }

private:
    std::vector<std::uint8_t> data_;
};

class ByteReader {
public:
    ByteReader(const std::uint8_t* data, std::size_t size);
    explicit ByteReader(const std::vector<std::uint8_t>& data);

    bool u8(std::uint8_t& value);
    bool u16(std::uint16_t& value);
    bool u32(std::uint32_t& value);
    bool u64(std::uint64_t& value);
    bool i8(std::int8_t& value);
    bool i16(std::int16_t& value);
    bool i32(std::int32_t& value);
    bool bytes(void* destination, std::size_t size);
    bool skip(std::size_t size);

    [[nodiscard]] std::size_t remaining() const { return size_ - offset_; }
    [[nodiscard]] std::size_t offset() const { return offset_; }
    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] bool at_end() const { return offset_ == size_; }

private:
    bool require(std::size_t size);

    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t offset_ = 0;
    bool ok_ = true;
};

} // namespace nsync::core
