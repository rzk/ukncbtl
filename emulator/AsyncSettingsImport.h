/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include "BackgroundJob.h"
#include "PreviewSettings.h"
#include <optional>

namespace preview_settings
{
// The UI owns the reservation. The worker owns the settings snapshot and
// completes the import and replacement before publishing the result.
class AsyncImport
{
public:
    using Result = std::expected<void, IniFileError>;
    explicit AsyncImport(background::Notify notify = {}) : result_(std::move(notify)) {}
    [[nodiscard]] bool Start(std::filesystem::path source, std::filesystem::path destination,
        ImportEncoding encoding, Settings current, bool importMedia);
    [[nodiscard]] bool Pending() const { return result_.Pending(); }
    [[nodiscard]] const std::filesystem::path& Path() const { return source_; }
    [[nodiscard]] std::optional<Result> Poll();
    [[nodiscard]] std::optional<Result> Finish();
private:
    std::filesystem::path source_;
    background::Job<Result> result_;
};
}
