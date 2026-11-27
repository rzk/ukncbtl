/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include "util/LittleEndian.h"
#include <algorithm>
#include <cstdint>
#include <span>
#include <stdexcept>

// Snapshot fields are little-endian, independent of pointer alignment.
namespace image {
class Writer {
public:
    explicit Writer(std::span<uint8_t> bytes) : bytes_(bytes) {}
    void word(uint16_t value)
    {
        ensure(2);
        little_endian::write<uint16_t>(bytes_.first<2>(), value);
        bytes_ = bytes_.subspan(2);
    }
    void dword(uint32_t value)
    {
        ensure(4);
        little_endian::write<uint32_t>(bytes_.first<4>(), value);
        bytes_ = bytes_.subspan(4);
    }
    void byte(uint8_t value)
    {
        ensure(1);
        bytes_.front() = value;
        bytes_ = bytes_.subspan(1);
    }
    void copy(std::span<const std::byte> source)
    {
        ensure(source.size());
        std::ranges::copy(source, std::as_writable_bytes(bytes_.first(source.size())).begin());
        bytes_ = bytes_.subspan(source.size());
    }
    void skip(size_t count) { ensure(count); bytes_ = bytes_.subspan(count); }
private:
    void ensure(size_t count) const { if (count > bytes_.size()) throw std::out_of_range("snapshot write"); }
    std::span<uint8_t> bytes_;
};
class Reader {
public:
    explicit Reader(std::span<const uint8_t> bytes) : bytes_(bytes) {}
    uint16_t word()
    {
        ensure(2);
        const auto value = little_endian::read<uint16_t>(bytes_.first<2>());
        bytes_ = bytes_.subspan(2);
        return value;
    }
    uint32_t dword()
    {
        ensure(4);
        const auto value = little_endian::read<uint32_t>(bytes_.first<4>());
        bytes_ = bytes_.subspan(4);
        return value;
    }
    uint8_t byte()
    {
        ensure(1);
        const auto value = bytes_.front();
        bytes_ = bytes_.subspan(1);
        return value;
    }
    void copy(std::span<std::byte> target)
    {
        ensure(target.size());
        std::ranges::copy(std::as_bytes(bytes_.first(target.size())), target.begin());
        bytes_ = bytes_.subspan(target.size());
    }
    void skip(size_t count) { ensure(count); bytes_ = bytes_.subspan(count); }
private:
    void ensure(size_t count) const { if (count > bytes_.size()) throw std::out_of_range("snapshot read"); }
    std::span<const uint8_t> bytes_;
};
}
