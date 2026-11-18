/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "AsyncDebuggerExport.h"
#include "DisplayListDump.h"
#include "MemoryDump.h"
#include <chrono>
#include <fstream>
#include <type_traits>
#include <utility>

bool debugger_export::AsyncSave::Start(std::filesystem::path path, Data data)
{
    if (Pending() || path.empty()) return false;
    try
    {
        path_ = std::move(path);
        result_ = std::async(std::launch::async, [path = path_, data = std::move(data)] {
            try
            {
                return std::visit([&](const auto& value) -> bool {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, std::vector<std::uint16_t>>)
                        return memory_dump::Save(path, value).has_value();
                    else if constexpr (std::is_same_v<T, std::unique_ptr<video::MemorySnapshot>>)
                        return value && display_list_dump::Save(path, *value).has_value();
                    else
                    {
                        std::ofstream file(path, std::ios::binary | std::ios::trunc);
                        if (!file) return false;
                        file.write(value.data(), static_cast<std::streamsize>(value.size()));
                        file.flush();
                        const bool written = static_cast<bool>(file);
                        file.close();
                        return written && static_cast<bool>(file);
                    }
                }, data);
            }
            catch (...) { return false; }
        });
        return true;
    }
    catch (...) { path_.clear(); return false; }
}

std::optional<bool> debugger_export::AsyncSave::Poll()
{
    if (!Pending() || result_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return std::nullopt;
    return Finish();
}

std::optional<bool> debugger_export::AsyncSave::Finish()
{
    if (!Pending()) return std::nullopt;
    const bool success = result_.get();
    path_.clear();
    return success;
}
