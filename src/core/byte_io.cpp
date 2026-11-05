#include "core/byte_io.h"

#include <cstring>

namespace nsync::core {

void ByteWriter::u8(std::uint8_t value) {
    data_.push_back(value);
}

void ByteWriter::u16(std::uint16_t value) {
    data_.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    data_.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void ByteWriter::u32(std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        data_.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void ByteWriter::u64(std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        data_.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void ByteWriter::i8(std::int8_t value) {
    u8(static_cast<std::uint8_t>(value));
}

void ByteWriter::i16(std::int16_t value) {
    u16(static_cast<std::uint16_t>(value));
}

void ByteWriter::i32(std::int32_t value) {
    u32(static_cast<std::uint32_t>(value));
}

void ByteWriter::bytes(const void* data, std::size_t size) {
    const auto* source = static_cast<const std::uint8_t*>(data);
    data_.insert(data_.end(), source, source + size);
}

ByteReader::ByteReader(const std::uint8_t* data, std::size_t size)
    : data_(data), size_(size) {}

ByteReader::ByteReader(const std::vector<std::uint8_t>& data)
    : data_(data.data()), size_(data.size()) {}

bool ByteReader::require(std::size_t size) {
    if (!ok_ || size > remaining()) {
        ok_ = false;
        return false;
    }
    return true;
}

bool ByteReader::u8(std::uint8_t& value) {
    if (!require(1)) {
        return false;
    }
    value = data_[offset_++];
    return true;
}

bool ByteReader::u16(std::uint16_t& value) {
    if (!require(2)) {
        return false;
    }
    value = static_cast<std::uint16_t>(data_[offset_]) |
            static_cast<std::uint16_t>(data_[offset_ + 1] << 8U);
    offset_ += 2;
    return true;
}

bool ByteReader::u32(std::uint32_t& value) {
    if (!require(4)) {
        return false;
    }
    value = static_cast<std::uint32_t>(data_[offset_]) |
            (static_cast<std::uint32_t>(data_[offset_ + 1]) << 8U) |
            (static_cast<std::uint32_t>(data_[offset_ + 2]) << 16U) |
            (static_cast<std::uint32_t>(data_[offset_ + 3]) << 24U);
    offset_ += 4;
    return true;
}

bool ByteReader::u64(std::uint64_t& value) {
    if (!require(8)) {
        return false;
    }
    value = 0;
    for (int index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(data_[offset_ + index]) << (index * 8);
    }
    offset_ += 8;
    return true;
}

bool ByteReader::i8(std::int8_t& value) {
    std::uint8_t raw = 0;
    if (!u8(raw)) {
        return false;
    }
    value = static_cast<std::int8_t>(raw);
    return true;
}

bool ByteReader::i16(std::int16_t& value) {
    std::uint16_t raw = 0;
    if (!u16(raw)) {
        return false;
    }
    value = static_cast<std::int16_t>(raw);
    return true;
}

bool ByteReader::i32(std::int32_t& value) {
    std::uint32_t raw = 0;
    if (!u32(raw)) {
        return false;
    }
    value = static_cast<std::int32_t>(raw);
    return true;
}

bool ByteReader::bytes(void* destination, std::size_t size) {
    if (!require(size)) {
        return false;
    }
    std::memcpy(destination, data_ + offset_, size);
    offset_ += size;
    return true;
}

bool ByteReader::skip(std::size_t size) {
    if (!require(size)) {
        return false;
    }
    offset_ += size;
    return true;
}

} // namespace nsync::core
