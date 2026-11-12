/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

namespace sound {
// Collect interleaved 16-bit stereo samples in little-endian byte order. A full
// batch stays unchanged until Reset, so a backend can submit it synchronously.
template<std::size_t FrameCount>
class StereoPcmBatch {
    static_assert(FrameCount > 0);
public:
    // Returns true when this sample completes the batch. Further samples are
    // rejected until Reset and return false.
    [[nodiscard]] bool Push(uint16_t left, uint16_t right)
    {
        if (used_ == bytes_.size()) return false;
        const auto put = [this](uint16_t sample) {
            bytes_[used_++] = std::bit_cast<char>(static_cast<uint8_t>(sample));
            bytes_[used_++] = std::bit_cast<char>(static_cast<uint8_t>(sample >> 8));
        };
        put(left);
        put(right);
        return used_ == bytes_.size();
    }

    [[nodiscard]] std::span<const char> Bytes() const { return {bytes_.data(), used_}; }
    void Reset() { used_ = 0; }

private:
    std::array<char, FrameCount * 4> bytes_{};
    std::size_t used_ = 0;
};
}
