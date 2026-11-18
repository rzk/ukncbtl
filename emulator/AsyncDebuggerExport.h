/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include "VideoDecoder.h"
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace debugger_export
{
// The UI supplies owned data and retains the path reservation until collection.
class AsyncSave
{
public:
    using Data = std::variant<std::vector<std::uint16_t>, std::unique_ptr<video::MemorySnapshot>, std::string>;
    [[nodiscard]] bool Start(std::filesystem::path path, Data data);
    [[nodiscard]] bool Pending() const { return result_.valid(); }
    [[nodiscard]] const std::filesystem::path& Path() const { return path_; }
    [[nodiscard]] std::optional<bool> Poll();
    [[nodiscard]] std::optional<bool> Finish();
private:
    std::filesystem::path path_;
    std::future<bool> result_;
};
}
