/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "AsyncSettingsImport.h"
#include <chrono>
#include <utility>

bool preview_settings::AsyncImport::Start(std::filesystem::path source, std::filesystem::path destination,
    ImportEncoding encoding, Settings current, bool importMedia)
{
    if (Pending() || source.empty() || destination.empty() || SameFile(source, destination)) return false;
    try
    {
        source_ = std::move(source);
        result_ = std::async(std::launch::async, [source = source_, destination = std::move(destination),
            encoding, current = std::move(current), importMedia]() -> Result {
            try
            {
                if (SameFile(source, destination)) return std::unexpected(IniFileError::invalid_entry);
                auto imported = ImportLegacy(source, encoding, current, importMedia);
                if (!imported) return std::unexpected(imported.error());
                return Save(destination, *imported);
            }
            catch (...) { return std::unexpected(IniFileError::write_failed); }
        });
        return true;
    }
    catch (...) { source_.clear(); return false; }
}

std::optional<preview_settings::AsyncImport::Result> preview_settings::AsyncImport::Poll()
{
    if (!Pending() || result_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return std::nullopt;
    return Finish();
}

std::optional<preview_settings::AsyncImport::Result> preview_settings::AsyncImport::Finish()
{
    if (!Pending()) return std::nullopt;
    auto result = result_.get();
    source_.clear();
    return result;
}
