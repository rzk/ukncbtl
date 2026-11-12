/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <cstdint>
#include <optional>

// Store supplies static Load(name, value) and Save(name, value) operations.
// The name must remain valid for the lifetime of the cached setting.
template <typename T, typename Store>
class CachedSetting
{
public:
    CachedSetting(const wchar_t* name, T defaultValue)
        : name_(name), defaultValue_(defaultValue) {}

    T Get()
    {
        if (!value_)
        {
            auto stored = static_cast<std::uint32_t>(defaultValue_);
            Store::Load(name_, stored);
            value_ = static_cast<T>(stored);
        }
        return *value_;
    }

    void Set(T value)
    {
        value_ = value;
        Store::Save(name_, static_cast<std::uint32_t>(value));
    }

    void Reset() { value_.reset(); }

private:
    const wchar_t* name_;
    T defaultValue_;
    std::optional<T> value_;
};
