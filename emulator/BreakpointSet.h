/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

// A compact list for one processor: 15 permanent entries, one temporary entry,
// and a sentinel for the debugger's existing pointer-based readers.
class BreakpointSet
{
public:
    static constexpr size_t Capacity = 16;
    static constexpr uint16_t NoBreakpoint = 0177777;

    BreakpointSet() { Clear(); }

    [[nodiscard]] std::span<const uint16_t> Addresses() const
    { return std::span{addresses_}.first(count_); }
    [[nodiscard]] const uint16_t* Data() const { return addresses_.data(); }
    // Snapshot only permanent entries, regardless of where the temporary entry sits.
    [[nodiscard]] std::array<uint16_t, Capacity - 1> PermanentAddresses() const
    {
        std::array<uint16_t, Capacity - 1> result;
        result.fill(NoBreakpoint);
        size_t index = 0;
        for (const auto address : Addresses())
            if (address != temporary_) result[index++] = address;
        std::sort(result.begin(), result.begin() + index);
        return result;
    }
    [[nodiscard]] bool Contains(uint16_t address) const
    { return std::ranges::contains(Addresses(), address); }

    bool Add(uint16_t address)
    {
        const auto permanentCount = count_ - (temporary_ != NoBreakpoint ? 1 : 0);
        if (address == NoBreakpoint || Contains(address) || permanentCount >= Capacity - 1)
            return false;
        addresses_[count_++] = address;
        return true;
    }

    bool Remove(uint16_t address)
    {
        const auto found = std::ranges::find(Addresses(), address);
        if (found == Addresses().end()) return false;
        const auto index = static_cast<size_t>(found - Addresses().begin());
        addresses_[index] = addresses_[--count_];
        addresses_[count_] = NoBreakpoint;
        if (temporary_ == address) temporary_ = NoBreakpoint;
        return true;
    }

    void SetTemporary(uint16_t address)
    {
        if (temporary_ != NoBreakpoint) Remove(temporary_);
        // A permanent breakpoint at the target must survive cancellation.
        if (address == NoBreakpoint || Contains(address)) return;
        addresses_[count_++] = address;
        temporary_ = address;
    }

    void Clear()
    {
        addresses_.fill(NoBreakpoint);
        count_ = 0;
        temporary_ = NoBreakpoint;
    }

private:
    std::array<uint16_t, Capacity + 1> addresses_{};
    size_t count_ = 0;
    uint16_t temporary_ = NoBreakpoint;
};
